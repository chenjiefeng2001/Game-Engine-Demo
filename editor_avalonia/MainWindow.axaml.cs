using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using AvaloniaEditor.Services;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor;

public partial class MainWindow : Window
{
    private readonly EditorHostService _host;
    public MainViewModel Vm { get; }

    public MainWindow() : this(RequireInjectedHost())
    {
    }

    // ── Phase 7: Viewport texture display ──
    private WriteableBitmap? _viewportBitmap;
    private DispatcherTimer? _viewportTimer;
    private DispatcherTimer? _productionFrameTimer;
    private ViewportFramePresenter? _viewportPresenter;

    public MainWindow(EditorHostService host)
    {
        _host = host ?? throw new ArgumentNullException(nameof(host));
        Vm = new MainViewModel(_host);
        InitializeComponent();
        DataContext = Vm;
        Vm.DerivedStateChanged += SyncStatusBar;
        Vm.PropertyChanged += OnVmPropertyChanged;
        Vm.LogToConsole("shell ready (P2 Editor Core)");

        // ── Phase 7: Viewport texture update timer ──
        // A7-C03：legacy viewport timer 只在 LegacySession backend 下存在。
        // EngineHost 模式由 EditorHostService 持有唯一 pump，UI 不得再驱动任何 tick。
        if (_host.SupportsViewport)
        {
            _viewportTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(33) // ~30 FPS
            };
            _viewportTimer.Tick += OnViewportTimerTick;
            _viewportTimer.Start();
        }
        else
        {
            // ── HRC-3 P2 UI Proof (production) ──
            // 审计结论：EngineHost 后端此前**没有任何 pump owner**（legacy 定时器因
            // SupportsViewport=false 从不启动），UI 显示的一直是 placeholder。
            // 这里建立的是**第一个且唯一**的 pump owner，调用既有单一入口
            // EditorHostService.RuntimeTick —— 不是第二个 tick 源，也没有把 legacy 的
            // ViewportTick + EditorSession_RuntimeTick 双源拆分之一重新引入。
            //
            // 单一职责：pump 一次 → 消费那一帧 → 呈现在 production ViewportImage。
            _viewportPresenter = new ViewportFramePresenter
            {
                ViewportImage = this.FindControl<Image>("ViewportImage"),
                ViewportPlaceholder = this.FindControl<TextBlock>("ViewportPlaceholder"),
            };
            _productionFrameTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(33)
            };
            _productionFrameTimer.Tick += OnProductionFrameTick;
            _productionFrameTimer.Start();
        }

        if (Environment.CommandLine.Contains("--selftest"))
            Opened += (_, _) =>
            {
                int code = Phase0SelfTest.Run(_host);
                Log($"selftest exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate1"))
            Opened += (_, _) =>
            {
                int code = Phase1Gate.Run(Vm, Log);
                Log($"gate1 exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate2"))
            Opened += (_, _) =>
            {
                int code = Phase2Gate.Run(Vm, Log);
                Log($"gate2 exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate3a"))
            Opened += (_, _) =>
            {
                int code = Phase3Gate.Run(Vm, Log);
                Log($"gate3a exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate3b"))
            Opened += (_, _) =>
            {
                int code = Phase3Gate.RunInspector(Vm, Log);
                Log($"gate3b exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate3c"))
            Opened += (_, _) =>
            {
                int code = Phase3Gate.RunAssetBrowser(Vm, Log);
                Log($"gate3c exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate3d"))
            Opened += (_, _) =>
            {
                int code = Phase3Gate.RunScriptEditor(Vm, Log);
                Log($"gate3d exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--gate3g"))
            Opened += (_, _) =>
            {
                int code = Phase3Gate.RunProduction(Vm, Log);
                Log($"gate3g exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--hrc1"))
            Opened += (_, _) =>
            {
                int code = Hrc1Gate.Run(_host, Vm, Log);
                Log($"hrc1 exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--hrc2"))
            Opened += async (_, _) =>
            {
                int code = await Hrc2Gate.RunAsync(_host, Vm, Log);
                Log($"hrc2 exit={code}");
                Environment.Exit(code);
            };

        // 阶段 A/A1：生产 UI 路径证据（关闭 F-1）。所有动作都经真实 XAML 事件驱动。
        if (Environment.CommandLine.Contains("--prodpath"))
            Opened += async (_, _) =>
            {
                int code = await ProductionPathGate.RunAsync(this, _host, Vm, Log);
                Log($"prodpath exit={code}");
                Environment.Exit(code);
            };

        // 阶段 A/A2：F-2 bootstrap trigger contract 证据。
        // 场景由进程启动时已存在的 EDITOR_BRIDGE_FAIL_* 注入决定（每个注入需独立进程）。
        if (Environment.CommandLine.Contains("--boot"))
            Opened += async (_, _) =>
            {
                int code = await BootstrapTriggerGate.RunAsync(this, _host, Vm, Log);
                Log($"boot exit={code}");
                Environment.Exit(code);
            };

        // 阶段 A/A3：UI-state contract 证据（D1 open 状态 / D2 选择意图 / D3 create 回执）。
        // 场景由 backend（--uistatelegacy）与 EDITOR_BRIDGE_FAIL_* 注入共同决定。
        if (Environment.CommandLine.Contains("--uistate"))
            Opened += async (_, _) =>
            {
                int code = await UiStateGate.RunAsync(this, _host, Vm, Log);
                Log($"uistate exit={code}");
                Environment.Exit(code);
            };

        // HRC-3 P2：managed fetch 镜像契约证据（M1~M7）。**不**触碰 WriteableBitmap/ViewportImage。
        if (Environment.CommandLine.Contains("--p2frame"))
            Opened += (_, _) =>
            {
                int code = P2ManagedFrameGate.Run(_host, Log);
                Log($"p2frame exit={code}");
                Environment.Exit(code);
            };

        // HRC-3 D3：managed pick request/result 镜像契约证据（M1~M7 + M8-B）。
        // **不**新增 pump：gate 复用 MainWindow 已建立的唯一 pump owner 与既有 RuntimeTick 入口。
        // M8-A（artifact SHA256 前置）由 tools/run_d3_managed_gate.ps1 把关；未过则本 gate 根本不启动。
        if (Environment.CommandLine.Contains("--d3managed"))
            Opened += (_, _) =>
            {
                int code = D3ManagedPickGate.Run(_host, Log);
                Log($"d3managed exit={code}");
                Environment.Exit(code);
            };


        // HRC-3 P2 UI Proof（U1~U8）：atomic native frame -> managed frame -> production ViewportImage。
        // **不**新增 pump：gate 复用 MainWindow 已建立的唯一 pump owner 与既有 RuntimeTick 入口。
        if (Environment.CommandLine.Contains("--p2ui"))
            Opened += (_, _) =>
            {
                int code = P2UiProofGate.Run(this, _host, Log);
                Log($"p2ui exit={code}");
                Environment.Exit(code);
            };
    }

    private void SyncStatusBar()
    {
        var sb = this.FindControl<TextBlock>("StatusBar");
        if (sb is not null) sb.Text = Vm.StatusText;
    }

    private void OnOpenProject(object? sender, RoutedEventArgs e)
    {
        GuardedUiAction("OnOpenProject", () =>
        {
            var root = FindRepoRoot();
            if (root is null) { Vm.LogToConsole("repo root not found"); return; }
            // manifest 内相对路径（assets/gp01/...）按进程 CWD 解析（GP01 运行时语义），
            // 宿主把 CWD 定向到工程根，使脚本/贴图读写落到真实内容。
            Directory.SetCurrentDirectory(root.FullName);
            Vm.OpenProject(Path.Combine(root.FullName, "assets", "gp01", "manifest.json"),
                           Path.Combine(root.FullName, "assets", "gp01", "Main.scene"));
            // Phase 7-B: Enable engine EditorCamera external input mode
            if (_host.SupportsViewport) _host.SetCameraExternalInput(true);
            SyncStatusBar();
        });
    }

    /// <summary>
    /// A2/F2-05：View 边界的最后一道网 —— host 侧的显式失败绝不允许以异常形式
    /// 逃进 XAML 反射派发（旧行为：bootstrap 失败被包成 TargetInvocationException，
    /// status/stage 诊断全丢）。只拦 host 契约内的失败类型，
    /// 真实编程错误仍然抛出（不静默吞异常）。
    /// </summary>
    private void GuardedUiAction(string operation, Action body)
    {
        try
        {
            body();
        }
        catch (Exception ex) when (ex is HostOperationException
                                       or HostRuntimeNotReadyException
                                       or HostRuntimeUnavailableException
                                       or ObjectDisposedException)
        {
            Vm.ReportHostFailure(operation, ex);
        }
    }

    private void OnSaveProject(object? sender, RoutedEventArgs e)
    {
        Vm.SaveProject();
        SyncStatusBar();
    }

    private void OnCreateEntity(object? sender, RoutedEventArgs e)
    {
        GuardedUiAction("OnCreateEntity", () =>
        {
            // A3/D3：走 VM 的唯一 create 入口，拿结构化 receipt。
            // MainWindow 不再直接调 _host.CreateEntity（"handler 没崩"不等于"语义正确"）。
            // A3/D2：自动选中由 VM 内部的选择意图在刷新时解析，handler 不再自己选一次。
            Vm.CreateEntity(null);
            SyncStatusBar();
        });
    }

    private void OnApplyTransform(object? sender, RoutedEventArgs e)
    {
        Vm.ApplyInspectorPosition();
    }

    /// P3-A：View 仅汇报用户点选；选择状态真相在 VM（AV-004 单一事实源）。
    /// 过滤把选中项藏掉时 ListBox 会临时失选，但不得回写 null 覆盖 VM 选择。
    private void OnHierarchySelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (HierarchyList.SelectedItem is EntityVm evm)
            Vm.SelectedEntity = evm;
    }

    private void OnRenameEntity(object? sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(Vm.RenameInput))
        {
            // 未输入时预填当前选中名，便于就地改名
            if (Vm.SelectedEntity is not null) Vm.RenameInput = Vm.SelectedEntity.Name;
            return;
        }
        Vm.RenameSelected(Vm.RenameInput);
        Vm.RenameInput = "";
    }

    private void OnDeleteEntity(object? sender, RoutedEventArgs e)
    {
        Vm.DeleteSelected();
        SyncStatusBar();
    }

    private void OnAssignSprite(object? sender, RoutedEventArgs e)
    {
        Vm.AssignSpriteToSelected();
    }

    // P3-C C4：双击分派 —— .lua → Script Editor；Texture → 预览信息；其他 → 明确不支持
    private void OnAssetDoubleTapped(object? sender, Avalonia.Input.TappedEventArgs e)
    {
        if (Vm.SelectedAsset is null) return;
        var asset = Vm.SelectedAsset;
        if (asset.TypeName == "Script")
        {
            if (Vm.OpenScript(asset.Index))
            {
                _currentOpenScriptIndex = asset.Index;
                MainTabs.SelectedIndex = 2;   // Script Editor tab
            }
        }
        else if (asset.TypeName == "Texture")
        {
            Vm.LogToConsole($"texture selected: {asset.Path} (GUID {asset.Guid})");
        }
        else
        {
            Vm.LogToConsole($"{asset.Path}: this type is not directly editable");
        }
    }

    private async void OnImportAsset(object? sender, RoutedEventArgs e)
    {
        if (!Vm.IsOpen) { Vm.LogToConsole("import ignored: no project"); return; }
        // C3：File Dialog → Import → ContentRegistry → 自动刷新。
        // 选文件后拷入工程 assets 目录再注册（ImportAsset 要求工程内相对路径）。
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Import asset",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Texture or Script")
                {
                    Patterns = new[] { "*.png", "*.jpg", "*.lua" }
                },
                FilePickerFileTypes.All
            }
        });
        if (files.Count == 0) return;
        var src = files[0].Path.LocalPath;
        string ext = System.IO.Path.GetExtension(src).ToLowerInvariant();
        int type = ext == ".lua" ? 1 : 0;
        string name = System.IO.Path.GetFileName(src);
        var dst = System.IO.Path.Combine(Directory.GetCurrentDirectory(),
            "assets", "gp01", ext == ".lua" ? "scripts" : "tex", name);
        try
        {
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(dst)!);
            System.IO.File.Copy(src, dst, overwrite: true);
        }
        catch (Exception ex)
        {
            Vm.LogToConsole("IMPORT COPY FAILED: " + ex.Message);
            return;
        }
        Vm.ImportAssetFromPath(System.IO.Path.GetRelativePath(Directory.GetCurrentDirectory(), dst)
            .Replace('\\', '/'), type);
    }

    private void OnRenameAsset(object? sender, RoutedEventArgs e)
    {
        if (Vm.SelectedAsset is null) { Vm.LogToConsole("rename asset ignored: no selection"); return; }
        if (string.IsNullOrWhiteSpace(Vm.RenameInput))
        {
            if (Vm.SelectedAsset is not null) Vm.RenameInput = Vm.AssetRenameBaseName;
            return;
        }
        Vm.RenameAssetFromVm(Vm.SelectedAsset.Index, Vm.RenameInput.Trim());
        Vm.RenameInput = "";
    }

    private void OnAssignScript(object? sender, RoutedEventArgs e)
    {
        Vm.AssignScriptToSelected();
    }

    // ── F1 (Component Contract)：Camera Inspector ──
    private void OnAddCamera(object? sender, RoutedEventArgs e) => Vm.AddCameraToSelected();
    private void OnRemoveCamera(object? sender, RoutedEventArgs e) => Vm.RemoveCameraFromSelected();
    private void OnApplyCamera(object? sender, RoutedEventArgs e) => Vm.ApplyCamera();

    private void OnSaveScript(object? sender, RoutedEventArgs e)
    {
        Vm.SaveScript();
    }

    // ── P3-D Runtime：Play / Reload / Stop ──
    private void OnPlay(object? sender, RoutedEventArgs e)
    {
        if (Vm.IsRunning) { Vm.StopPlaying(); return; }
        // Play 当前打开脚本（assetIndex>=0）；未打开则跑项目 director（<0）
        Vm.PlayScript(Vm.ScriptTitle == "(no script)" ? -1 : _currentOpenScriptIndex);
        SyncPlayButton();
    }

    private void OnReloadScript(object? sender, RoutedEventArgs e)
    {
        Vm.ReloadActiveScript();
    }

    private void OnStop(object? sender, RoutedEventArgs e)
    {
        Vm.StopPlaying();
        SyncPlayButton();
    }

    private void SyncPlayButton()
    {
        var btn = this.FindControl<Button>("PlayBtn");
        if (btn is not null) btn.Content = Vm.IsRunning ? "Stop" : "Play";
    }

    /// 当前打开脚本的资产索引（供 Play 直接运行编辑中的脚本）
    private int _currentOpenScriptIndex = -1;

    private void OnScriptTextChanged(object? sender, RoutedEventArgs e)
    {
        // 仅当已有打开的脚本且尚未标记时置脏（避免初始化/装载时的误标记）
        Vm.MarkScriptEdited();
    }

    private async void OnCloseProject(object? sender, RoutedEventArgs e)
    {
        if (Vm.IsDirty)
        {
            // P2-E：未保存编辑 → 提示（无 Undo/Redo，per DF08 非 P0）
            bool ok = await ConfirmDialog.ConfirmAsync(this,
                "Unsaved changes", "Project has unsaved changes. Close anyway?");
            if (!ok) return;
        }
        Vm.ResetSession();
        SyncStatusBar();
    }

    private void OnVmPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.IsDirty))
            SyncDirtyBadge();
        if (e.PropertyName == nameof(MainViewModel.IsRunning))
            SyncPlayButton();
    }

    private void SyncDirtyBadge()
    {
        var badge = this.FindControl<TextBlock>("DirtyBadge");
        if (badge is not null) badge.IsVisible = Vm.IsDirty;
    }

    // ── Phase 7: Viewport texture update ──
    private void OnViewportTimerTick(object? sender, EventArgs e)
    {
        if (!Vm.IsOpen) return;

        // Phase 7-B: Tick the engine viewport (flushes camera input + triggers render)
        _host.ViewportTick(0.033f);

        var (w, h, pixels) = _host.GetViewportPixels();
        if (w <= 0 || h <= 0 || pixels is null) return;

        var image = this.FindControl<Image>("ViewportImage");
        var placeholder = this.FindControl<TextBlock>("ViewportPlaceholder");
        if (image is null) return;

        // Recreate bitmap if size changed
        if (_viewportBitmap is null || _viewportBitmap.PixelSize.Width != w || _viewportBitmap.PixelSize.Height != h)
        {
            _viewportBitmap = new WriteableBitmap(
                new PixelSize(w, h),
                new Vector(96, 96),
                PixelFormat.Rgba8888,
                AlphaFormat.Unpremul);
            image.Source = _viewportBitmap;
            if (placeholder is not null) placeholder.IsVisible = false;
        }

        // Write pixels to bitmap (flip vertically for OpenGL)
        using (var fb = _viewportBitmap.Lock())
        {
            // Copy RGBA8 pixels, flipping Y for OpenGL coordinate system
            for (int y = 0; y < h; y++)
            {
                int srcRow = (h - 1 - y);
                int srcOffset = srcRow * w * 4;
                int dstOffset = y * fb.RowBytes;
                Marshal.Copy(pixels, srcOffset, fb.Address + dstOffset, w * 4);
            }
        }

        // Force image repaint
        image.InvalidateVisual();
    }

    /// <summary>
    /// A7-C03 accepted ≠ executed：GL 依赖 edit 在 pump 帧内完成后才回报真实结果。
    /// </summary>
    private void OnPendingEditCompleted(bool executed, string diagnostic)
    {
        Vm.LogToConsole(executed
            ? "pending edit executed in frame"
            : "PENDING EDIT FAILED: " + diagnostic);
        if (!executed) Vm.ReportPendingEditFailure(diagnostic);
    }

    internal static void Log(string line)
    {
        Console.WriteLine(line);
        try
        {
            var path = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
                "editor_avalonia", "selftest.log");
            File.AppendAllText(Path.GetFullPath(path),
                $"{DateTime.Now:HH:mm:ss.fff} {line}{Environment.NewLine}");
        }
        catch { }
    }

    /// 从可执行目录向上定位仓库根（含 assets/gp01 的最近祖先）
    internal static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

    // ── Viewport（Phase 7）输入转发 ──

    private bool _viewportUnavailableLogged;

    /// <summary>
    /// Viewport/Gizmo/Picking 属 A7 范围外。EngineHost backend 不提供该能力，
    /// 这里集中拦截并只提示一次，避免每个输入事件抛异常（不静默 no-op）。
    /// </summary>
    private bool AcceptViewportInput()
    {
        if (_host.SupportsViewport) return true;
        if (_viewportUnavailableLogged) return false;
        _viewportUnavailableLogged = true;
        Vm.LogToConsole(
            "viewport input is unavailable on the EngineHost backend (out of A7 scope)");
        return false;
    }

    /// 计算鼠标位置到 NDC 坐标（[-1,1]）
    private (float ndcX, float ndcY) PointerToNdc(Avalonia.Input.PointerEventArgs e)
    {
        var bounds = ViewportBorder?.Bounds ?? new Avalonia.Rect(0, 0, 1, 1);
        var pos = e.GetPosition(ViewportBorder);
        float ndcX = (float)(pos.X / bounds.Width * 2.0 - 1.0);
        float ndcY = (float)(1.0 - pos.Y / bounds.Height * 2.0);
        return (ndcX, ndcY);
    }

    /// <summary>
    /// 视口控件尺寸变化 → 通知引擎重建视口（charter Gate 7-A「窗口 resize 时
    /// FBO 自动重建」）。
    ///
    /// 为什么必须转发：引擎侧 EditorCamera 的宽高比只由
    /// EditorSession_SetViewportSize 更新（EditorSession.cpp 的实现会调用
    /// ViewportPanel 的 SetViewportSize + MarkDirty）。不转发的话，投影矩阵的
    /// 宽高比会停留在打开工程那一刻的值，拖动分隔条或缩放窗口后视口会被拉伸。
    ///
    /// 这里不去抖：SizeChanged 在拖动中连续触发，而引擎侧
    /// SetViewportSize 自带「尺寸未变则早退」的判断，且 MarkDirty 只是置位一个
    /// 布尔，重复调用无额外代价。加去抖反而会让首次布局多等一帧。
    /// </summary>
    private void OnViewportSizeChanged(object? sender, Avalonia.SizeChangedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        int w = (int)Math.Round(e.NewSize.Width);
        int h = (int)Math.Round(e.NewSize.Height);
        // 布局过程中会出现 0 尺寸；引擎侧对 <=0 直接忽略，这里同样不推。
        if (w <= 0 || h <= 0) return;
        Vm.SetViewportSize(w, h);
    }

    private void OnViewportPointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        var props = e.GetCurrentPoint(ViewportBorder).Properties;
        int button = props.IsLeftButtonPressed ? 0 :
                     props.IsRightButtonPressed ? 1 :
                     props.IsMiddleButtonPressed ? 2 : -1;
        if (button >= 0)
        {
            Vm.ViewportMouseButton(button, pressed: true);
            e.Handled = true;
        }

        // 左键点击 → 拾取实体
        if (props.IsLeftButtonPressed)
        {
            var (ndcX, ndcY) = PointerToNdc(e);
            int hitEntity = Vm.ViewportPick(ndcX, ndcY);
            if (hitEntity >= 0 && hitEntity < Vm.Entities.Count)
            {
                Vm.SelectedEntity = Vm.Entities[hitEntity];
            }
        }
    }

    private void OnViewportPointerReleased(object? sender, Avalonia.Input.PointerReleasedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        var props = e.GetCurrentPoint(ViewportBorder).Properties;
        int button = props.IsLeftButtonPressed ? 0 :
                     props.IsRightButtonPressed ? 1 :
                     props.IsMiddleButtonPressed ? 2 : -1;
        if (button >= 0)
        {
            Vm.ViewportMouseButton(button, pressed: false);
            e.Handled = true;
        }
    }

    private void OnViewportPointerMoved(object? sender, Avalonia.Input.PointerEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        var pos = e.GetPosition(ViewportBorder);
        Vm.ViewportMouseMove((float)pos.X, (float)pos.Y);
        e.Handled = true;
    }

    private void OnViewportPointerWheelChanged(object? sender, Avalonia.Input.PointerWheelEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        float delta = (float)e.Delta.Y;
        Vm.ViewportScroll(delta);
        e.Handled = true;
    }

    private void OnViewportKeyDown(object? sender, Avalonia.Input.KeyEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.ViewportKeyDown((int)e.Key);
        e.Handled = true;
    }

    private void OnViewportKeyUp(object? sender, Avalonia.Input.KeyEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.ViewportKeyUp((int)e.Key);
        e.Handled = true;
    }

    // ── Gizmo 工具栏 ──

    private void OnGizmoSelect(object? sender, RoutedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.GizmoMode = 0;
    }

    private void OnGizmoTranslate(object? sender, RoutedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.GizmoMode = 1;
    }

    private void OnGizmoRotate(object? sender, RoutedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.GizmoMode = 2;
    }

    private void OnGizmoScale(object? sender, RoutedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.GizmoMode = 3;
    }

    private void OnToggleGrid(object? sender, RoutedEventArgs e)
    {
        if (!AcceptViewportInput()) return;
        Vm.ViewportGridVisible = !Vm.ViewportGridVisible;
    }

    protected override void OnClosed(EventArgs e)
    {
        _viewportTimer?.Stop();
        _viewportTimer = null;
        _productionFrameTimer?.Stop();
        _productionFrameTimer = null;
        _viewportPresenter = null;
        _host.PendingEditCompleted -= OnPendingEditCompleted;
        Vm.Dispose();
        base.OnClosed(e);
    }

    /// <summary>
    /// HRC-3 P2 UI：production 帧循环。**唯一** pump owner —— 调用 EditorHostService.RuntimeTick
    /// 这一个既有入口，然后消费它产生的那一帧并呈现在 ViewportImage 上。
    /// 没有新帧时 presenter 完全不碰控件（绝不把上一帧 bitmap 当成新帧）。
    /// </summary>
    private void OnProductionFrameTick(object? sender, EventArgs e)
    {
        if (_viewportPresenter is null) return;
        if (!Vm.IsOpen) return;

        // 既有唯一 pump 入口（不是新增的第二条 tick 路径）
        _host.RuntimeTick(0.033f);

        // 消费该帧；没有新帧就什么都不做
        _viewportPresenter.PresentLatestFrame(_host);

        // 左下角叠加信息。
        // 复用 presenter 的 LastDisplayed 而不是自行记录帧号：presenter 已经
        // 拒绝了乱序/回退帧（Present 里比较 frame.FrameId <= _displayedFrameId），
        // 所以这里的 FrameId 必然是**真实呈现过**的那一帧。
        var shown = _viewportPresenter.LastDisplayed;
        if (shown is null) return;
        Vm.UpdateViewportStatus(shown.DisplayedFrameId, shown.Width, shown.Height,
                                shown.NonClearPixels);
    }

    private static EditorHostService RequireInjectedHost()
        => throw new InvalidOperationException("MainWindow requires the composition host");
}
