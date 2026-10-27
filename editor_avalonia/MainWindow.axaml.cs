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
using AvaloniaEditor.Native;
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
    private ViewportFramePresenter? _viewportPresenter;

    // ── Slice E：viewport 尺寸协商（View 侧）──
    //
    // 契约（E-C6a）：capacity = 1，**last-wins** —— 连续变化只保留最新目标尺寸，
    // 不排队、不逐次重建。View 侧合并一次，native 侧再合并一次。
    private bool _viewportBaselineCaptured;
    private int _viewportBaselineWidth;
    private int _viewportBaselineHeight;
    private int _viewportTargetWidth;
    private int _viewportTargetHeight;
    private bool _viewportResizePosted;

    // SizeChanged 反馈防护：帧尺寸变化会连带改变控件布局，这种变化**不是**用户 resize，
    // 必须忽略，否则会把刚发布的帧尺寸重新登记成控件裁剪后的尺寸（反馈回路）。
    private ulong _lastSeenDisplayedFrameId;
    private double _lastControlWidth;
    private double _lastControlHeight;
    private bool _hasLastControlSize;

    /// <summary>已派发的 viewport 尺寸请求次数（gate 证据用）。</summary>
    internal int ViewportResizeDispatchCount { get; private set; }

    /// <summary>被 native 拒绝的 viewport 尺寸请求次数（gate 证据用）。</summary>
    internal int ViewportResizeRejectedCount { get; private set; }

    /// <summary>最近一次派发的目标尺寸（gate 证据用）。</summary>
    internal (int width, int height)? LastRequestedViewportSize { get; private set; }

    /// <summary>当前显示帧的回执（gate 证据用；null = 还没有显示过任何帧）。</summary>
    internal ViewportDisplayReceipt? LastDisplayedFrame => _viewportPresenter?.LastDisplayed;

    /// <summary>
    /// 初始布局尺寸（基线，**不**派发给 native）。null = 还没有完成第一次有效布局。
    /// </summary>
    internal (int width, int height)? ViewportBaselineSize
        => _viewportBaselineCaptured ? (_viewportBaselineWidth, _viewportBaselineHeight) : null;

    public MainWindow(EditorHostService host)
    {
        _host = host ?? throw new ArgumentNullException(nameof(host));
        Vm = new MainViewModel(_host);
        InitializeComponent();
        DataContext = Vm;
        Vm.DerivedStateChanged += SyncStatusBar;
        Vm.PropertyChanged += OnVmPropertyChanged;
        Vm.LogToConsole("shell ready (P2 Editor Core)");

        // Commit B：pending edit 的执行回执。构造期只订阅一次，OnClosed 对称取消；
        // 事件由 pump 在 UI 线程同步触发（不是 native runtime 线程）。
        _host.PendingEditCompleted += OnPendingEditCompleted;

        // ── Phase 7: Viewport texture update timer ──
        // A7-C03：legacy viewport timer 只在 LegacySession backend 下存在。
        // EngineHost 模式由 EditorHostService 持有唯一 pump；UI 只消费 FrameAdvanced，
        // 不得再建立自己的 pump 定时器（否则会出现双 pump / 双倍模拟速度）。
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
            // 单一驱动边界：EditorHostService.BeginRuntime 的定时器是**唯一** pump owner。
            // 这里只订阅它发布的帧：pump 一帧 → FrameAdvanced → 消费并呈现。
            // 本窗口没有任何周期性 RuntimeTick 调用。
            _viewportPresenter = new ViewportFramePresenter
            {
                ViewportImage = this.FindControl<Image>("ViewportImage"),
                ViewportPlaceholder = this.FindControl<TextBlock>("ViewportPlaceholder"),
            };
            _host.FrameAdvanced += OnFrameAdvanced;

            // ── Slice E：viewport 尺寸变化 → Dispatch(SET_VIEWPORT_SIZE) ──
            // 这条接线在 E 之前**完全不存在**（审计 A1：SizeChanged 命中 0），
            // 所以控件缩放根本到不了 native，帧尺寸被钉死在 800×600。
            // 这里只做"登记"：真正的重建发生在 native 的下一帧 current-context 窗口内。
            if (_viewportPresenter.ViewportImage is { } viewportImage)
            {
                viewportImage.SizeChanged += OnViewportImageSizeChanged;
            }
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
        // gate 用 RuntimeTick 逐帧取证；它是显式测试泵，不是产品 UI 的第二驱动源。
        if (Environment.CommandLine.Contains("--p2ui"))
            Opened += (_, _) =>
            {
                int code = P2UiProofGate.Run(this, _host, Log);
                Log($"p2ui exit={code}");
                Environment.Exit(code);
            };

        // Commit A 回归：唯一 pump owner。通过真实 Open 按钮启动 runtime，然后在
        // 固定时间窗口内比较 pump 次数与帧数，并断言频率没有翻倍（第二驱动源）。
        if (Environment.CommandLine.Contains("--pumpowner"))
            Opened += async (_, _) =>
            {
                int code = await PumpOwnershipGate.RunAsync(this, _host, Vm, Log);
                Log($"pumpowner exit={code}");
                Environment.Exit(code);
            };

        // Commit B 回归：accepted ≠ executed 的异步回执（成功 / native 失败 / 关闭解绑）。
        if (Environment.CommandLine.Contains("--pendingedit"))
            Opened += async (_, _) =>
            {
                int code = await PendingEditFeedbackGate.RunAsync(this, _host, Vm, Log);
                Log($"pendingedit exit={code}");
                Environment.Exit(code);
            };

        // Slice D 回归：生产 viewport pick（点击 -> canonical 像素 -> PICK -> 帧内结果）。
        //
        // ⚠ flag 名不得与既有 flag 构成前缀关系：`--prodpathpick` 含 `--prodpath`，
        // 会让 `--prodpath` 的 handler 也被注册（实测两个 gate 同时跑并互相打断）。
        // 因此用 `--prodpick`。
        if (Environment.CommandLine.Contains("--prodpick"))
            Opened += async (_, _) =>
            {
                int code = await ProductionPickGate.RunAsync(this, _host, Vm, Log);
                Log($"prodpick exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--viewportsize"))
            Opened += async (_, _) =>
            {
                int code = await ViewportSizeGate.RunAsync(this, _host, Vm, Log);
                Log($"viewportsize exit={code}");
                Environment.Exit(code);
            };

        if (Environment.CommandLine.Contains("--viewportrobustness"))
            Opened += async (_, _) =>
            {
                int code = await ViewportRobustnessGate.RunAsync(this, _host, Vm, Log);
                Log($"viewportrobustness exit={code}");
                Environment.Exit(code);
            };

        // Commit C 回归：选择通知与状态栏同步（模型对 ≠ 控件对）。
        if (Environment.CommandLine.Contains("--selstatus"))
            Opened += async (_, _) =>
            {
                int code = await SelectionStatusGate.RunAsync(this, _host, Vm, Log);
                Log($"selstatus exit={code}");
                Environment.Exit(code);
            };
    }

    private void SyncStatusBar()
    {
        // 幂等：由 StatusText 的 PropertyChanged 驱动，同时保留显式调用点（handler 末尾）。
        var sb = this.FindControl<TextBlock>("StatusBar");
        if (sb is not null && sb.Text != Vm.StatusText) sb.Text = Vm.StatusText;
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
        // Commit C3：状态栏是 StatusText 的投影。之前只有 handler 末尾手动调
        // SyncStatusBar()，于是**没有 handler** 的路径（native 事件驱动的集合刷新、
        // pump 故障上报）模型已改、控件未改。改为按通知同步后，两条路径统一。
        if (e.PropertyName == nameof(MainViewModel.StatusText))
            SyncStatusBar();
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
    /// 只有 executed=true 才允许 UI 呈现“完成”；失败进入 ScriptError / Console，
    /// 并由这里把最新状态同步到状态栏。
    /// </summary>
    private void OnPendingEditCompleted(bool executed, string diagnostic)
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            // 事件契约是 UI 线程；越界时不得跨线程改控件。
            Vm.LogToConsole("PENDING EDIT EVENT OFF UI THREAD (A7-C04 violation)");
            return;
        }

        Vm.CompletePendingEdit(executed, diagnostic);
        SyncStatusBar();
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
    /// Slice D **D-C3**：viewport 输入所有权。
    ///
    /// 旧行为：`!SupportsViewport` 就恒返回 false —— EngineHost（production）后端上
    /// **所有** viewport 输入在第一行就被拦截，pick 路径从未可达（D-A1）。
    ///
    /// 新行为：
    ///   - **鼠标**（点击/移动/滚轮）两种后端都接受：它是 viewport 的固有交互，
    ///     与 legacy camera 无关；
    ///   - **键盘**仅当 viewport 真的持有键盘焦点时才转发（D-C3）—— 否则会抢掉
    ///     文本框/脚本编辑器的按键；
    ///   - legacy 专属能力（`ViewportTick` / `SetCameraExternalInput` / gizmo 等）
    ///     仍只在 legacy 后端可用，EngineHost 上仍会被 `LegacyOnly` 显式拒绝。
    /// </summary>
    private bool AcceptViewportPointer() => true;

    /// <summary>D-C3：键盘只在 viewport 持有焦点时消费。</summary>
    private bool AcceptViewportKeyboard()
    {
        if (ViewportBorder is null) return false;
        if (ViewportBorder.IsKeyboardFocusWithin) return true;
        if (!_viewportKeyboardBlockedLogged)
        {
            _viewportKeyboardBlockedLogged = true;
            Vm.LogToConsole("viewport keyboard ignored: the viewport does not hold keyboard focus");
        }
        return false;
    }

    private bool _viewportKeyboardBlockedLogged;

    /// <summary>
    /// legacy 专属 viewport 能力（camera / gizmo / legacy tick）的门禁。
    /// 与输入所有权分开：输入是 UI 事实，能力是 backend 事实。
    /// </summary>
    private bool AcceptLegacyViewportCapability()
    {
        if (_host.SupportsViewport) return true;
        if (_viewportUnavailableLogged) return false;
        _viewportUnavailableLogged = true;
        Vm.LogToConsole(
            "legacy viewport capability (camera/gizmo/tick) is unavailable on the EngineHost backend");
        return false;
    }

    private void OnViewportPointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
    {
        if (!AcceptViewportPointer()) return;

        // D-C3：点击 viewport 即取得键盘焦点（否则 KeyDown 永远不会被派发到这里）。
        ViewportBorder?.Focus();

        // D-A2 修正：legacy 的鼠标转发用 viewport 局部坐标（legacy 自己的契约），
        // 与 pick 的 canonical 像素坐标**是两件事**，不得混用。
        if (AcceptLegacyViewportCapability())
        {
            var props = e.GetCurrentPoint(ViewportBorder).Properties;
            int button = props.IsLeftButtonPressed ? 0 :
                         props.IsRightButtonPressed ? 1 :
                         props.IsMiddleButtonPressed ? 2 : -1;
            if (button >= 0)
            {
                Vm.ViewportMouseButton(button, pressed: true);
                e.Handled = true;
            }
        }

        // 左键点击 → production pick（request → 下一帧 → fetch；**不**在这里改选中）。
        var properties = e.GetCurrentPoint(ViewportBorder).Properties;
        if (properties.IsLeftButtonPressed)
        {
            RequestPickAt(e);
            e.Handled = true;
        }
    }

    /// <summary>
    /// Slice E：viewport 控件的布局尺寸变化 ⇒ 登记新的目标尺寸。
    ///
    /// 三条冻结规则：
    ///   1. **初始布局是基线，不派发**：ABI 的默认尺寸是 800×600，
    ///      "project 打开后、尚未收到 SET_VIEWPORT_SIZE 时的尺寸"就是默认尺寸。
    ///      第一次布局只是把控件摆好，不是用户的 resize 请求 ——
    ///      如果把它也派发出去，既有 P1/P2 的 800×600 证据面就会被改写。
    ///   2. **capacity = 1 / last-wins**：同一 UI turn 内的多次变化只保留最后一个
    ///      目标尺寸（这里用一次 Post 合并；native 侧同样再合并一次）。
    ///   3. **只登记，不重建**：真正的 FBO 重建发生在 native 的下一帧
    ///      current-context 窗口内（E-C6a/E-C6e）。
    /// </summary>
    private void OnViewportImageSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        if (_host.Backend != HostBackend.EngineHost) return;   // legacy 是禁止路径

        var bounds = ViewportImage?.Bounds ?? default;
        int width = (int)Math.Round(bounds.Width);
        int height = (int)Math.Round(bounds.Height);
        if (width <= 0 || height <= 0) return;   // 还没布局好：不登记

        // 帧尺寸变化连带的布局变化：忽略，避免反馈回路。
        var displayed = _viewportPresenter?.LastDisplayed;
        if (displayed is not null && displayed.DisplayedFrameId != _lastSeenDisplayedFrameId)
        {
            _lastSeenDisplayedFrameId = displayed.DisplayedFrameId;
            _lastControlWidth = bounds.Width;
            _lastControlHeight = bounds.Height;
            _hasLastControlSize = true;
            // 首次有效布局仍作为基线记录；帧变化本身不派发。
            if (!_viewportBaselineCaptured)
            {
                _viewportBaselineCaptured = true;
                _viewportBaselineWidth = width;
                _viewportBaselineHeight = height;
                _viewportTargetWidth = width;
                _viewportTargetHeight = height;
            }
            return;
        }
        if (_hasLastControlSize
            && Math.Abs(bounds.Width - _lastControlWidth) < 0.5
            && Math.Abs(bounds.Height - _lastControlHeight) < 0.5)
        {
            return;   // 控件尺寸未变：不重复派发
        }

        if (!_viewportBaselineCaptured)
        {
            // 基线 = 第一次有效布局（不派发，见规则 1）。
            _viewportBaselineCaptured = true;
            _viewportBaselineWidth = width;
            _viewportBaselineHeight = height;
            _viewportTargetWidth = width;
            _viewportTargetHeight = height;
            return;
        }
        if (width == _viewportTargetWidth && height == _viewportTargetHeight) return;

        _viewportTargetWidth = width;
        _viewportTargetHeight = height;
        _lastControlWidth = bounds.Width;
        _lastControlHeight = bounds.Height;
        _hasLastControlSize = true;

        if (_viewportResizePosted) return;   // 已排一个合并任务：目标尺寸已被覆盖
        _viewportResizePosted = true;
        Dispatcher.UIThread.Post(() =>
        {
            _viewportResizePosted = false;
            RequestViewportResize(_viewportTargetWidth, _viewportTargetHeight);
        }, DispatcherPriority.Background);
    }

    /// <summary>
    /// 把目标尺寸派发给 native（只登记；重建由 native 在下一帧完成）。
    /// 非法尺寸在这里就被挡掉（零尺寸 / 超限尺寸），native 侧同样会拒绝 ——
    /// 两道闸是**同一套规则**的两次执行，不是两套规则。
    /// </summary>
    internal bool RequestViewportResize(int width, int height)
    {
        if (_host.Backend != HostBackend.EngineHost) return false;
        if (width <= 0 || height <= 0)
        {
            ViewportResizeRejectedCount++;
            return false;
        }
        if (width > EditorBridgeApi.EngineHostPresentationMaxWidth
            || height > EditorBridgeApi.EngineHostPresentationMaxHeight)
        {
            ViewportResizeRejectedCount++;
            Vm.LogToConsole(
                $"viewport resize skipped: {width}x{height} exceeds the maximum presentation size "
                + $"({EditorBridgeApi.EngineHostPresentationMaxWidth}x"
                + $"{EditorBridgeApi.EngineHostPresentationMaxHeight})");
            return false;
        }

        LastRequestedViewportSize = (width, height);
        ViewportResizeDispatchCount++;
        if (!_host.SetViewportSize(width, height))
        {
            ViewportResizeRejectedCount++;
            Vm.LogToConsole(
                $"viewport resize rejected by the host: {width}x{height} "
                + "(the current presentation size is unchanged)");
            return false;
        }
        return true;
    }

    /// <summary>
    /// Slice D **D-C1 / D-C2**：把一次点击映射成 canonical 像素并提交 pick 请求。
    ///
    /// 三条硬约束（都在这里集中，不散落到 handler）：
    ///   1. **不传 NDC**：映射只经 <see cref="ViewportPickMapper"/>；
    ///   2. **几何前提不成立就拒绝**（`GeometryMismatch`），不猜 offset/scale；
    ///   3. **越界显式拒绝**（`OutsideFrame`），不 clamp 成边缘假命中。
    /// </summary>
    private void RequestPickAt(Avalonia.Input.PointerEventArgs e)
    {
        var image = ViewportImage;
        if (image is null)
        {
            Vm.LogToConsole("pick ignored: production ViewportImage is not bound");
            return;
        }

        // 坐标以 **ViewportImage** 为参照：Stretch=None + Left/Top 对齐下，
        // 帧 1:1 绘制在控件原点，因此"相对控件的点"就是"相对帧的像素"。
        var pos = e.GetPosition(image);
        RequestPickAtPoint(pos.X, pos.Y);
    }

    /// <summary>
    /// Slice D 的生产 pick 入口（handler 与 gate 共用同一入口，gate 不另开旁路）。
    /// <paramref name="pointerX"/>/<paramref name="pointerY"/> 是**相对 ViewportImage**
    /// 的坐标 —— 与 `e.GetPosition(ViewportImage)` 完全同源。
    /// 返回 true 表示请求已被 native 受理（**不代表**命中）。
    ///
    /// Slice E（E-C3/E-C4）：映射输入不再是"编译期常量 800×600"，而是
    ///   - **当前显示帧的真实尺寸**（来自该帧的 packet header）；
    ///   - **实际显示矩形**（由 View 计算：图像在控件坐标系里被绘制的那块区域）。
    /// mapper 只做算术（floor + 半开区间 + 越界拒绝），**不猜呈现模式**。
    /// </summary>
    internal bool RequestPickAtPoint(double pointerX, double pointerY)
    {
        var image = ViewportImage;
        if (image is null)
        {
            Vm.LogToConsole("pick ignored: production ViewportImage is not bound");
            return false;
        }

        // 帧尺寸必须来自**当前显示的那一帧**（packet header），不是常量，也不是控件尺寸。
        var displayed = _viewportPresenter?.LastDisplayed;
        if (displayed is null)
        {
            Vm.LogToConsole("pick skipped: no presentation frame has been displayed yet");
            return false;
        }
        int frameWidth = displayed.Width;
        int frameHeight = displayed.Height;

        // 显示矩形：View 负责算（E-C3/E-C4）。Stretch=None（生产）⇒ 自然尺寸，
        // 控件更窄时是**裁剪**而不是缩放（比例仍是 1）。
        if (!ViewportDisplayGeometry.TryCompute(
                image.Bounds,
                new Size(frameWidth, frameHeight),
                image.Stretch,
                out var displayedRect))
        {
            Vm.LogToConsole(
                "pick skipped: the displayed geometry is not usable "
                + $"(stretch={image.Stretch}, laid-out {image.Bounds.Width:0.#}x{image.Bounds.Height:0.#}, "
                + $"frame {frameWidth}x{frameHeight}); refusing to guess an offset/scale (E-C3)");
            return false;
        }

        // 一致性守卫：控件上的位图必须就是当前显示帧的尺寸（否则映射的坐标系不对）。
        var source = image.Source as WriteableBitmap;
        if (source is null
            || source.PixelSize.Width != frameWidth
            || source.PixelSize.Height != frameHeight)
        {
            Vm.LogToConsole(
                "pick skipped: the displayed bitmap is not the current presentation frame size "
                + $"(source={image.Source?.GetType().Name}, "
                + $"{source?.PixelSize.Width}x{source?.PixelSize.Height} "
                + $"vs frame {frameWidth}x{frameHeight}); refusing to map (E-C3)");
            return false;
        }

        var status = ViewportPickMapper.TryMap(
            pointerX, pointerY,
            displayedRect.X, displayedRect.Y,
            displayedRect.Width, displayedRect.Height,
            frameWidth, frameHeight,
            out var point);

        switch (status)
        {
            case PickMapStatus.GeometryMismatch:
                Vm.LogToConsole(
                    "pick skipped: the display geometry is not usable "
                    + $"(control {image.Bounds.Width:0.#}x{image.Bounds.Height:0.#}, "
                    + $"displayed {displayedRect}, frame {frameWidth}x{frameHeight}); "
                    + "refusing to guess an offset/scale (E-C3)");
                return false;
            case PickMapStatus.OutsideFrame:
                Vm.LogToConsole(
                    $"pick skipped: ({pointerX:0.##},{pointerY:0.##}) is outside the rendered frame "
                    + "(rejected, never clamped)");
                return false;
            case PickMapStatus.InvalidCoordinate:
                Vm.LogToConsole("pick skipped: non-finite pointer coordinate");
                return false;
        }

        return Vm.RequestViewportPick(point.X, point.Y);
    }

    private void OnViewportPointerReleased(object? sender, Avalonia.Input.PointerReleasedEventArgs e)
    {
        if (!AcceptViewportPointer()) return;
        if (!AcceptLegacyViewportCapability()) return;
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
        if (!AcceptViewportPointer()) return;
        if (!AcceptLegacyViewportCapability()) return;
        var pos = e.GetPosition(ViewportBorder);
        Vm.ViewportMouseMove((float)pos.X, (float)pos.Y);
        e.Handled = true;
    }

    private void OnViewportPointerWheelChanged(object? sender, Avalonia.Input.PointerWheelEventArgs e)
    {
        if (!AcceptViewportPointer()) return;
        if (!AcceptLegacyViewportCapability()) return;
        float delta = (float)e.Delta.Y;
        Vm.ViewportScroll(delta);
        e.Handled = true;
    }

    private void OnViewportKeyDown(object? sender, Avalonia.Input.KeyEventArgs e)
    {
        if (!AcceptViewportKeyboard()) return;
        if (!AcceptLegacyViewportCapability()) return;
        Vm.ViewportKeyDown((int)e.Key);
        e.Handled = true;
    }

    private void OnViewportKeyUp(object? sender, Avalonia.Input.KeyEventArgs e)
    {
        if (!AcceptViewportKeyboard()) return;
        if (!AcceptLegacyViewportCapability()) return;
        Vm.ViewportKeyUp((int)e.Key);
        e.Handled = true;
    }

    // ── Gizmo 工具栏 ──

    private void OnGizmoSelect(object? sender, RoutedEventArgs e)
    {
        if (!AcceptLegacyViewportCapability()) return;
        Vm.GizmoMode = 0;
    }

    private void OnGizmoTranslate(object? sender, RoutedEventArgs e)
    {
        if (!AcceptLegacyViewportCapability()) return;
        Vm.GizmoMode = 1;
    }

    private void OnGizmoRotate(object? sender, RoutedEventArgs e)
    {
        if (!AcceptLegacyViewportCapability()) return;
        Vm.GizmoMode = 2;
    }

    private void OnGizmoScale(object? sender, RoutedEventArgs e)
    {
        if (!AcceptLegacyViewportCapability()) return;
        Vm.GizmoMode = 3;
    }

    private void OnToggleGrid(object? sender, RoutedEventArgs e)
    {
        if (!AcceptLegacyViewportCapability()) return;
        Vm.ViewportGridVisible = !Vm.ViewportGridVisible;
    }

    protected override void OnClosed(EventArgs e)
    {
        _viewportTimer?.Stop();
        _viewportTimer = null;
        _host.FrameAdvanced -= OnFrameAdvanced;
        _viewportPresenter = null;
        _host.PendingEditCompleted -= OnPendingEditCompleted;
        Vm.Dispose();
        base.OnClosed(e);
    }

    /// <summary>
    /// HRC-3 P2 UI：production 帧消费。service 的唯一 pump 发布 FrameAdvanced 后，
    /// 这里只取回并呈现该帧；**绝不**在这里调用 RuntimeTick（否则就是第二个 pump）。
    /// 没有新帧时 presenter 完全不碰控件（不把上一帧 bitmap 当成新帧）。
    /// </summary>
    private void OnFrameAdvanced()
    {
        if (_viewportPresenter is null || !Vm.IsOpen) return;
        try
        {
            _viewportPresenter.PresentLatestFrame(_host);

            // Slice D **D-C4**：pick 结果与帧走**同一个**唯一 pump 发布点 ——
            // 不新建定时器、不在 UI 里轮询、不阻塞。
            // 传入当前已呈现帧，使 stale 结果可被显式区分（D3-C4）。
            Vm.TryConsumeViewportPick(_viewportPresenter.DisplayedFrameId);
        }
        catch (Exception ex) when (ex is HostOperationException
                                       or HostRuntimeNotReadyException
                                       or HostRuntimeUnavailableException)
        {
            // 呈现失败必须可见，且不能让异常逃出 DispatcherTimer tick。
            Vm.ReportHostFailure("present frame", ex);
        }
    }

    private static EditorHostService RequireInjectedHost()
        => throw new InvalidOperationException("MainWindow requires the composition host");
}
