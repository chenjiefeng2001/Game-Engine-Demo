using System;
using System.Collections.ObjectModel;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
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

    private void OnSaveScript(object? sender, RoutedEventArgs e)
    {
        Vm.SaveScript();
    }

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
