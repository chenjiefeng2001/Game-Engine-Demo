using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Styling;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI-H2 P1-C 门禁：Dark 基线、语义表面与前景可读性。
/// </summary>
public static class UiH2P1CGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[P1C] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_uih2p1c_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "scratch project opened");
            await Task.Delay(500);

            Check(Application.Current?.RequestedThemeVariant == ThemeVariant.Dark,
                  "application theme baseline is Dark");
            Check(IsDark(window.Background), "window background is a dark surface");
            Check(IsLight(window.Foreground), "window foreground is a light text color");
            Check(Contrast(window.Foreground, window.Background) >= 4.5,
                  "window text contrast is at least 4.5:1");

            var constants = new[]
            {
                "HierarchyList", "AssetList", "ConsoleList",
                "InspectorPanel", "ConsoleTabSurface", "AssetTabSurface",
                "ScriptTabSurface", "MainTabs", "StatusBarSurface",
            };
            foreach (string name in constants)
            {
                var control = window.FindControl<Control>(name);
                Check(control is not null, $"control '{name}' was found");
                IBrush? background = control switch
                {
                    Border b => b.Background,
                    TabControl t => t.Background,
                    ListBox l => l.Background,
                    _ => null,
                };
                Check(IsDark(background), $"{name} has an opaque dark surface");
                Check(IsOpaque(background), $"{name} surface is opaque");
            }

            var hierarchy = Require<ListBox>(window, "HierarchyList");
            var console = Require<ListBox>(window, "ConsoleList");
            var filter = Require<TextBox>(window, "ConsoleFilterBox");
            var scriptEmpty = Require<TextBlock>(window, "ScriptEmptyTitle");
            var status = Require<TextBlock>(window, "StatusBar");
            Check(IsLight(hierarchy.Foreground), "hierarchy text is light");
            Check(IsLight(console.Foreground), "console text is light");
            Check(IsLight(filter.Foreground), "console filter text is light");
            Check(IsLight(scriptEmpty.Foreground), "script empty-state text is light");
            Check(IsLight(status.Foreground), "status text is light");
            Check(Contrast(console.Foreground, console.Background) >= 4.5,
                  "console text contrast is at least 4.5:1");
            Check(Contrast(filter.Foreground, filter.Background) >= 4.5,
                  "filter text contrast is at least 4.5:1");

            var tabs = Require<TabControl>(window, "MainTabs");
            Check(tabs.SelectedItem is TabItem selected
                  && IsDark(selected.Background)
                  && IsLight(selected.Foreground),
                  "selected tab has a dark surface and light text");

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
                await Task.Delay(250);
                Check(IsDark(window.Background) && IsLight(window.Foreground),
                      $"theme surfaces stay consistent at {width}x{height}");
                Check(IsDark(console.Background) && IsLight(console.Foreground),
                      $"console surfaces stay consistent at {width}x{height}");
            }

            log("[P1C] Dark baseline, semantic surfaces and contrast PASS");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[P1C] FAIL: " + ex);
            log("[P1C] FAIL: " + ex.Message);
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

    private static bool IsOpaque(IBrush? brush)
        => brush is ISolidColorBrush solid && solid.Color.A == 255;

    private static bool IsDark(IBrush? brush)
        => brush is ISolidColorBrush solid && Luminance(solid.Color) < 0.3;

    private static bool IsLight(IBrush? brush)
        => brush is ISolidColorBrush solid && Luminance(solid.Color) > 0.35;

    private static double Contrast(IBrush? a, IBrush? b)
    {
        if (a is not ISolidColorBrush sa || b is not ISolidColorBrush sb) return 0;
        double la = Luminance(sa.Color) + 0.05;
        double lb = Luminance(sb.Color) + 0.05;
        return Math.Max(la, lb) / Math.Min(la, lb);
    }

    private static double Luminance(Color color)
    {
        static double Linear(byte value)
        {
            double c = value / 255.0;
            return c <= 0.03928 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * Linear(color.R)
               + 0.7152 * Linear(color.G)
               + 0.0722 * Linear(color.B);
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
