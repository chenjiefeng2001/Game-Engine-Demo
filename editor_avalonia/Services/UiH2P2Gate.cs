using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI-H2 P2 门禁：空态、Console 密度与 Gizmo 图标统一。
/// 非功能性视觉细节；不改变 T-6-A 行为。
/// </summary>
public static class UiH2P2Gate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[P2] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_uih2p2_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "scratch project opened");
            await Task.Delay(500);

            Check(vm.HasEntities && vm.HasAssets && vm.HasConsoleLines,
                  "populated project exposes non-empty state");
            Check(!Require<TextBlock>(window, "HierarchyEmptyState").IsVisible,
                  "hierarchy empty state is hidden when populated");
            Check(!Require<TextBlock>(window, "AssetEmptyState").IsVisible,
                  "asset empty state is hidden when populated");
            Check(!Require<TextBlock>(window, "ConsoleEmptyState").IsVisible,
                  "console empty state is hidden when populated");

            var console = Require<ListBox>(window, "ConsoleList");
            Check(console.Classes.Contains("console"), "console list uses the compact class");
            var item = console.GetVisualDescendants().OfType<ListBoxItem>().FirstOrDefault();
            Check(item is not null, "console item was realized");
            Check(Math.Abs(item!.FontSize - 12) < 0.1, "console item uses 12px text");
            Check(item.MinHeight >= 22, "console item has compact row height");

            foreach (string name in new[]
                     {
                         "GizmoSelectBtn", "GizmoTranslateBtn", "GizmoRotateBtn",
                         "GizmoScaleBtn", "GizmoGridBtn",
                     })
            {
                var button = Require<Button>(window, name);
                Check(button.Classes.Contains("gizmo"), $"{name} uses the gizmo style");
                Check(button.Content is string glyph && glyph.Length > 0,
                      $"{name} has an icon glyph");
            }

            Click(window, "ConsoleClearBtn");
            await Task.Delay(200);
            Check(!vm.HasConsoleLines, "clear empties the console cache");
            Check(Require<TextBlock>(window, "ConsoleEmptyState").IsVisible,
                  "console empty state appears after clear");

            Click(window, "ToolbarCreateEntityBtn");
            await Task.Delay(300);
            Check(vm.HasConsoleLines, "a real edit writes a console line");
            Check(!Require<TextBlock>(window, "ConsoleEmptyState").IsVisible,
                  "console empty state hides after a new line");

            vm.ResetSession();
            await Task.Delay(200);
            Check(!vm.HasEntities && !vm.HasAssets,
                  "closing the project empties entity/asset projections");
            Check(Require<TextBlock>(window, "HierarchyEmptyState").IsVisible,
                  "hierarchy empty state appears after close");
            Check(Require<TextBlock>(window, "AssetEmptyState").IsVisible,
                  "asset empty state appears after close");

            log("[P2] empty states, console density and gizmo icons PASS");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[P2] FAIL: " + ex);
            log("[P2] FAIL: " + ex.Message);
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
