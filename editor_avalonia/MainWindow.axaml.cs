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
        Vm.LogToConsole("shell ready (P1-A/B/C)");

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
