using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.VisualTree;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI-H2-A P0 门禁：真实窗口下的区域不重叠、Tab 表面可读、Inspector 滚动可达。
/// 只验证布局与可访问性；T-6-A 行为由既有门禁单独回归。
/// </summary>
public static class UiH2AGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[H2A] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_uih2a_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));

            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "scratch project opened");
            Check(vm.SelectByName("Director"), "Director selected through the VM projection");
            Click(window, "AddCameraBtn");
            Click(window, "AddRigidBodyBtn");
            await Task.Delay(250);
            Check(vm.HasCamera && vm.HasRigidBody,
                  "Camera and RigidBody components were added through the real buttons");

            var sizes = new (int Width, int Height)[]
            {
                (900, 600),
                (1100, 720),
                (1440, 900),
                (1920, 1080),
            };

            foreach (var (width, height) in sizes)
            {
                window.Width = width;
                window.Height = height;
                window.UpdateLayout();
                await Task.Delay(350);

                var console = Require<ListBox>(window, "ConsoleList");
                var inspector = Require<Border>(window, "InspectorPanel");
                var scroll = Require<ScrollViewer>(window, "InspectorScroll");
                var apply = Require<Button>(window, "ApplyRigidBodyBtn");
                var viewport = Require<Grid>(window, "ViewportHost");

                Rect consoleRect = BoundsIn(console, window);
                Rect inspectorRect = BoundsIn(inspector, window);
                Rect viewportRect = BoundsIn(viewport, window);

                Check(!Overlaps(consoleRect, inspectorRect),
                      $"ConsoleList overlaps InspectorPanel at {width}x{height}");
                Check(!Overlaps(consoleRect, viewportRect),
                      $"ConsoleList overlaps ViewportHost at {width}x{height}");
                Check(inspectorRect.Right <= window.ClientSize.Width + 0.5
                      && inspectorRect.Bottom <= window.ClientSize.Height + 0.5,
                      $"InspectorPanel exceeds the window at {width}x{height}");

                scroll.Offset = new Vector(0, Math.Max(0, scroll.Extent.Height - scroll.Viewport.Height));
                window.UpdateLayout();
                await Task.Delay(150);

                Rect applyRect = BoundsIn(apply, window);
                Check(apply.IsEffectivelyVisible,
                      $"Apply RigidBody is not effectively visible at {width}x{height}");
                Check(applyRect.Top >= -0.5
                      && applyRect.Bottom <= window.ClientSize.Height + 0.5
                      && applyRect.Left >= -0.5
                      && applyRect.Right <= window.ClientSize.Width + 0.5,
                      $"Apply RigidBody is outside the window at {width}x{height}");

                if (width <= 1100)
                    Check(scroll.Extent.Height > scroll.Viewport.Height,
                          $"Inspector does not require scrolling at {width}x{height}");

                log($"[H2A] {width}x{height} no-overlap + Inspector reachable");
            }

            // Tab 内容/空态在最小尺寸下也必须可读。
            window.Width = 900;
            window.Height = 600;
            window.UpdateLayout();
            await Task.Delay(250);
            var tabs = Require<TabControl>(window, "MainTabs");
            var consoleSurface = Require<Border>(window, "ConsoleTabSurface");
            var assetSurface = Require<Border>(window, "AssetTabSurface");
            var scriptSurface = Require<Border>(window, "ScriptTabSurface");
            Check(IsOpaque(consoleSurface.Background), "Console tab surface is not opaque");
            Check(IsOpaque(assetSurface.Background), "Asset tab surface is not opaque");
            Check(IsOpaque(scriptSurface.Background), "Script tab surface is not opaque");
            Check(IsOpaque(tabs.Background), "TabControl background is not opaque");

            var filter = Require<TextBox>(window, "ConsoleFilterBox");
            var clear = Require<Button>(window, "ConsoleClearBtn");
            Check(IsLight(filter.Foreground), "Console filter foreground is not readable");
            Check(IsLight(clear.Foreground), "Console clear foreground is not readable");
            Check(IsLight(Require<TextBlock>(window, "ScriptEmptyTitle").Foreground),
                  "Script empty-state title foreground is not readable");

            tabs.SelectedIndex = 2;
            window.UpdateLayout();
            await Task.Delay(250);
            var emptyTitle = Require<TextBlock>(window, "ScriptEmptyTitle");
            Check(emptyTitle.IsEffectivelyVisible, "Script empty-state title is not visible");
            log("[H2A] tab surfaces and script empty-state readability checked at 900x600");

            tabs.SelectedIndex = 0;
            window.UpdateLayout();
            log("[H2A] all real-window P0 checks passed");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[H2A] FAIL: " + ex);
            log("[H2A] FAIL: " + ex.Message);
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

    private static T Require<T>(MainWindow window, string name) where T : Control
    {
        var control = window.FindControl<T>(name);
        Check(control is not null, $"control '{name}' was found");
        return control!;
    }

    private static Rect BoundsIn(Visual visual, Visual root)
    {
        Point? origin = visual.TranslatePoint(new Point(0, 0), root);
        Check(origin is not null, "control has a translation to the window");
        return new Rect(origin!.Value, visual.Bounds.Size);
    }

    private static bool Overlaps(Rect a, Rect b)
        => a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;

    private static bool IsOpaque(IBrush? brush)
        => brush is ISolidColorBrush solid && solid.Color.A == 255;

    private static bool IsLight(IBrush? brush)
        => brush is ISolidColorBrush solid
           && (solid.Color.R + solid.Color.G + solid.Color.B) / 3 >= 150;

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
