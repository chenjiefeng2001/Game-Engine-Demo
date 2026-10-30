using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI-H2 P1-A 门禁：可调整分区、最小尺寸与 1:1 Viewport 居中。
/// 真实窗口 + 真实控件；不修改 T-6-A 行为。
/// </summary>
public static class UiH2P1AGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[P1A] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_uih2p1a_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "scratch project opened");
            await Task.Delay(600);

            Check(window.MinWidth >= 900 && window.MinHeight >= 600,
                  "window minimum dimensions are at least 900x600");
            var grid = Require<Grid>(window, "MainGrid");
            Check(grid.ColumnDefinitions.Count == 5, "main grid has five columns");
            Check(grid.ColumnDefinitions[0].MinWidth == 200, "hierarchy min width is 200");
            Check(grid.ColumnDefinitions[2].MinWidth == 320, "viewport min width is 320");
            Check(grid.ColumnDefinitions[4].MinWidth == 240, "inspector min width is 240");
            Check(grid.RowDefinitions[0].MinHeight == 240, "main row min height is 240");
            Check(grid.RowDefinitions[2].MinHeight == 140, "bottom row min height is 140");
            Check(grid.RowDefinitions[2].MaxHeight == 360, "bottom row max height is 360");

            var hierarchySplitter = Require<GridSplitter>(window, "HierarchySplitter");
            var inspectorSplitter = Require<GridSplitter>(window, "InspectorSplitter");
            var consoleSplitter = Require<GridSplitter>(window, "ConsoleSplitter");
            Check(hierarchySplitter.IsEffectivelyVisible && inspectorSplitter.IsEffectivelyVisible
                  && consoleSplitter.IsEffectivelyVisible,
                  "all three GridSplitters are visible");

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
                await Task.Delay(300);

                var console = Require<ListBox>(window, "ConsoleList");
                var inspector = Require<Border>(window, "InspectorPanel");
                Rect consoleRect = BoundsIn(console, window);
                Rect inspectorRect = BoundsIn(inspector, window);
                Check(!Overlaps(consoleRect, inspectorRect),
                      $"Console overlaps Inspector at {width}x{height}");

                var border = Require<Border>(window, "ViewportBorder");
                var image = Require<Image>(window, "ViewportImage");
                if (image.Bounds.Width <= 0 || image.Bounds.Height <= 0)
                {
                    for (int i = 0; i < 10 && image.Bounds.Width <= 0; i++)
                    {
                        await Task.Delay(200);
                        window.UpdateLayout();
                    }
                }
                Check(image.Bounds.Width > 0 && image.Bounds.Height > 0,
                      $"ViewportImage has no rendered size at {width}x{height}");
                Check(border.ClipToBounds, "ViewportBorder does not clip overflow");

                Point? borderOrigin = border.TranslatePoint(new Point(0, 0), window);
                Point? imageOrigin = image.TranslatePoint(new Point(0, 0), window);
                Check(borderOrigin is not null && imageOrigin is not null,
                      "viewport elements translate to the window");
                double borderCx = borderOrigin!.Value.X + border.Bounds.Width / 2;
                double borderCy = borderOrigin.Value.Y + border.Bounds.Height / 2;
                double imageCx = imageOrigin!.Value.X + image.Bounds.Width / 2;
                double imageCy = imageOrigin.Value.Y + image.Bounds.Height / 2;
                Check(Math.Abs(borderCx - imageCx) <= 1.5
                      && Math.Abs(borderCy - imageCy) <= 1.5,
                      $"ViewportImage is not centered at {width}x{height}");

                log($"[P1A] {width}x{height} no-overlap + centered 1:1 viewport");
            }

            // Layout responds to splitter column/row changes (pointer drag is covered by capture).
            window.Width = 1100;
            window.Height = 720;
            window.UpdateLayout();
            await Task.Delay(200);
            var hierarchy = Require<ListBox>(window, "HierarchyList");
            var consoleList = Require<ListBox>(window, "ConsoleList");
            double hierarchyBefore = hierarchy.Bounds.Width;
            double consoleBefore = consoleList.Bounds.Height;

            grid.ColumnDefinitions[0].Width = new GridLength(320);
            window.UpdateLayout();
            await Task.Delay(200);
            Check(hierarchy.Bounds.Width > hierarchyBefore + 60,
                  "hierarchy column responds to splitter width change");
            grid.ColumnDefinitions[0].Width = new GridLength(240);

            grid.RowDefinitions[2].Height = new GridLength(280);
            window.UpdateLayout();
            await Task.Delay(200);
            Check(consoleList.Bounds.Height > consoleBefore + 40,
                  "bottom row responds to splitter height change");
            grid.RowDefinitions[2].Height = new GridLength(220);
            window.UpdateLayout();

            var consoleSurface = Require<Border>(window, "ConsoleTabSurface");
            Check(IsOpaque(consoleSurface.Background), "Console tab surface remains opaque");
            log("[P1A] splitters and minimum constraints verified");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[P1A] FAIL: " + ex);
            log("[P1A] FAIL: " + ex.Message);
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
