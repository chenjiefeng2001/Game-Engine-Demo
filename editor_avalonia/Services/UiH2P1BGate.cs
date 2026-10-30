using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI-H2 P1-B 门禁：工具栏分组、主/危险按钮、运行态 badge 与禁用状态。
/// 真实窗口 + 真实按钮；不改变 T-6-A 行为。
/// </summary>
public static class UiH2P1BGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[P1B] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_uih2p1b_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "scratch project opened");
            await Task.Delay(400);

            var global = Require<StackPanel>(window, "ToolbarGlobalGroup");
            var edit = Require<StackPanel>(window, "ToolbarEditGroup");
            var transport = Require<StackPanel>(window, "ToolbarTransportGroup");
            Check(global.Bounds.Width > 0 && edit.Bounds.Width > 0 && transport.Bounds.Width > 0,
                  "all toolbar groups have a layout");
            double globalX = OriginX(global, window);
            double editX = OriginX(edit, window);
            double transportX = OriginX(transport, window);
            Check(globalX < editX && editX < transportX,
                  "toolbar groups are ordered global -> edit -> transport");

            Check(!window.GetVisualDescendants()
                    .OfType<TextBlock>()
                    .Any(t => (t.Text ?? "").Contains("[P3-D]", StringComparison.Ordinal)),
                  "the developer [P3-D] hint is absent from the toolbar");

            var play = Require<Button>(window, "PlayBtn");
            var reload = Require<Button>(window, "ReloadBtn");
            var stop = Require<Button>(window, "StopBtn");
            Check(play.Classes.Contains("primary"), "Play is the primary toolbar action");
            Check(Require<Button>(window, "DeleteEntityBtn").Classes.Contains("danger"),
                  "Delete is styled as a destructive action");
            Check(Require<Button>(window, "RemoveCameraBtn").Classes.Contains("danger"),
                  "Remove Camera is styled as a destructive action");
            Check(Require<Button>(window, "RemoveRigidBodyBtn").Classes.Contains("danger"),
                  "Remove RigidBody is styled as a destructive action");

            var runningBadge = Require<Border>(window, "RunningBadge");
            var statusBadge = Require<TextBlock>(window, "RunningStatusBadge");
            Check(!runningBadge.IsVisible && !statusBadge.IsVisible,
                  "runtime badges start hidden");
            Check(play.Content as string == "Play", "Play starts in the idle state");
            Check(!reload.IsEnabled && !stop.IsEnabled,
                  "transport secondary actions start disabled");

            Click(window, "PlayBtn");
            await Task.Delay(700);
            Check(vm.IsRunning, "Play reaches the running state through the toolbar");
            Check(play.Content as string == "Stop", "Play flips to Stop while running");
            Check(runningBadge.IsVisible && statusBadge.IsVisible,
                  "toolbar and status running badges are visible");
            Check(reload.IsEnabled && stop.IsEnabled,
                  "Reload/Stop enable while running");

            Click(window, "StopBtn");
            await Task.Delay(300);
            Check(!vm.IsRunning, "Stop leaves the running state");
            Check(play.Content as string == "Play", "Play flips back to Play");
            Check(!runningBadge.IsVisible && !statusBadge.IsVisible,
                  "runtime badges hide after Stop");
            Check(!reload.IsEnabled && !stop.IsEnabled,
                  "transport secondary actions disable after Stop");

            Click(window, "ToolbarCreateEntityBtn");
            await Task.Delay(300);
            Check(Require<TextBlock>(window, "DirtyBadge").IsVisible,
                  "a real edit reveals the dirty badge");

            log("[P1B] toolbar grouping, primary/danger actions and runtime state PASS");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[P1B] FAIL: " + ex);
            log("[P1B] FAIL: " + ex.Message);
            return 1;
        }
        finally
        {
            try { vm.ResetSession(); } catch { }
            if (scratch is not null)
            {
                try { Directory.Delete(scratch, recursive: true); } catch { }
            }
        }
    }

    private static double OriginX(Control control, Visual root)
    {
        var origin = control.TranslatePoint(new Avalonia.Point(0, 0), root);
        Check(origin is not null, "control translates to the window");
        return origin!.Value.X;
    }

    private static T Require<T>(MainWindow window, string name) where T : Control
    {
        var control = window.FindControl<T>(name);
        Check(control is not null, $"control '{name}' was found");
        return control!;
    }

    private static void Click(MainWindow window, string name)
    {
        var button = window.FindControl<Button>(name);
        Check(button is not null, $"button '{name}' was found");
        button!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
    }

    private static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source))
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), overwrite: true);
        foreach (var dir in Directory.GetDirectories(source))
            CopyDirectory(dir, Path.Combine(target, Path.GetFileName(dir)));
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
