using System;
using System.Collections.ObjectModel;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
using AvaloniaEditor.Services;

namespace AvaloniaEditor;

public partial class MainWindow : Window
{
    private readonly EditorHostService _host = new();
    public ObservableCollection<string> LogLines { get; } = new();

    public MainWindow()
    {
        InitializeComponent();
        LogList.ItemsSource = LogLines;
        _host.NativeEvent += (type, payload) =>
            OnUi(() => Log($"EV[{type}] {payload}"));

        if (Environment.CommandLine.Contains("--selftest"))
            Opened += (_, _) =>
            {
                int code = Phase0SelfTest.Run(_host);
                Title = code == 0 ? "PHASE0 GATE: ALL GREEN"
                                  : "PHASE0 GATE: FAIL";
                Log($"selftest exit={code}");
                // 确定性退出：避免启动期 Close 与 desktop lifetime 竞态
                System.Environment.Exit(code);
            };
    }

    private void OnOpenProject(object? sender, RoutedEventArgs e)
    {
        try
        {
            var root = FindRepoRoot() ?? throw new InvalidOperationException(
                "repo root not found (assets/gp01 missing)");
            const string manifest = "assets/gp01/manifest.json";
            const string scene = "assets/gp01/Main.scene";

            if (!_host.OpenProject(manifest, scene))
            {
                Log("OPEN FAILED: " + _host.GetLastError());
                return;
            }
            var (entities, assets) = _host.GetCounts();
            StatusBar.Text = $"project loaded: {entities} objects, {assets} assets";
            Log($"round-trip check: GetEntityCount={entities}");
        }
        catch (Exception ex) { Log("ERROR: " + ex.Message); }
    }

    private void OnCreateEntity(object? sender, RoutedEventArgs e)
    {
        if (!_host.IsOpen) { Log("+ Entity ignored: no project"); return; }
        int idx = _host.CreateEntity(null);   // 自动命名 Entity_N
        Log($"created idx={idx} name={_host.GetEntityName(idx)}");
        var (entities, _) = _host.GetCounts();
        StatusBar.Text = $"project loaded: {entities} objects";
    }

    private void OnSaveProject(object? sender, RoutedEventArgs e)
    {
        if (!_host.IsOpen) { Log("Save ignored: no project"); return; }
        Log(_host.SaveProject() ? "project saved" : "SAVE FAILED: " + _host.GetLastError());
    }

    private void Log(string line)
    {
        LogLines.Add($"[{DateTime.Now:HH:mm:ss.fff}] {line}");
        if (LogList.ItemCount > 0)
            LogList.ScrollIntoView(LogList.ItemCount - 1);
    }

    private void OnUi(Action a)
        => Avalonia.Threading.Dispatcher.UIThread.Post(a);

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
