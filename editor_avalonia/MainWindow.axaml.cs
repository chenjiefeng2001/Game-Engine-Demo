using System;
using System.Collections.ObjectModel;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using AvaloniaEditor.Services;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor;

public partial class MainWindow : Window
{
    private readonly EditorHostService _host = new();
    public MainViewModel Vm { get; } = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = Vm;
        Vm.DerivedStateChanged += SyncStatusBar;
        Vm.PropertyChanged += OnVmPropertyChanged;
        Vm.LogToConsole("shell ready (P2 Editor Core)");

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
    }

    private void SyncStatusBar()
    {
        var sb = this.FindControl<TextBlock>("StatusBar");
        if (sb is not null) sb.Text = Vm.StatusText;
    }

    private void OnOpenProject(object? sender, RoutedEventArgs e)
    {
        var root = FindRepoRoot();
        if (root is null) { Vm.LogToConsole("repo root not found"); return; }
        // manifest 内相对路径（assets/gp01/...）按进程 CWD 解析（GP01 运行时语义），
        // 宿主把 CWD 定向到工程根，使脚本/贴图读写落到真实内容。
        Directory.SetCurrentDirectory(root.FullName);
        Vm.OpenProject(Path.Combine(root.FullName, "assets", "gp01", "manifest.json"),
                       Path.Combine(root.FullName, "assets", "gp01", "Main.scene"));
        SyncStatusBar();
    }

    private void OnSaveProject(object? sender, RoutedEventArgs e)
    {
        Vm.SaveProject();
        SyncStatusBar();
    }

    private void OnCreateEntity(object? sender, RoutedEventArgs e)
    {
        if (!Vm.IsOpen) { Vm.LogToConsole("+ Entity ignored: no project"); return; }
        int idx = _host.CreateEntity(null);   // EV_ENTITY_CREATED → VM 自动刷新
        Vm.SelectByName(_host.GetEntityName(idx) ?? "");
        SyncStatusBar();
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
}
