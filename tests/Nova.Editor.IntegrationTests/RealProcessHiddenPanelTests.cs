using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Automation;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D8-E real-process gates. The editor executable is started with
/// --layout-file, driven through the real shell (Win32 mouse input located by
/// OS UI Automation, WM_CLOSE), and every data claim is checked against the
/// layout file the process itself wrote. This is the production path the user
/// exercises; no test-only entry points are involved.
/// </summary>
[TestClass]
public sealed partial class RealProcessHiddenPanelTests
{
    private const uint WmClose = 0x0010;
    private const uint MouseEventfLeftdown = 0x0002;
    private const uint MouseEventfLeftup = 0x0004;
    private const uint InputKeyboard = 1;
    private const ushort VkEscape = 0x1B;
    private const ushort VkMenu = 0x12;
    private const uint KeyeventfKeyup = 0x0002;
    private const int SwShow = 5;

    private static readonly TimeSpan WindowTimeout = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan ExitTimeout = TimeSpan.FromSeconds(60);
    private static readonly TimeSpan MenuTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan SaveTimeout = TimeSpan.FromSeconds(15);

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d8e-real-process", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    [TestMethod]
    public async Task Hidden_console_stays_hidden_across_two_real_restarts_and_the_menu_lists_it()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp1-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            await SeedHiddenConsoleAsync(layoutPath);
            var seeded = await ReadCanonicalLayoutAsync(layoutPath);
            CollectionAssert.AreEqual(
                new[] { "editor.console" },
                CollectHidden(RequireDockSnapshot(seeded)),
                "The seeded layout must hide the console panel.");

            for (var run = 1; run <= 2; run++)
            {
                await RunEditorAsync(appPath, layoutPath, AssertHiddenShellUiAsync);

                var saved = await ReadCanonicalLayoutAsync(layoutPath);
                CollectionAssert.AreEqual(
                    new[] { "editor.console" },
                    CollectHidden(RequireDockSnapshot(saved)),
                    $"Run {run}: the hidden panel must survive the restart in the saved layout.");
            }
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Real_hidden_panels_menu_restores_the_console_and_refreshes_menu_state()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp2-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            await SeedHiddenConsoleAsync(layoutPath);

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await AssertHiddenShellUiAsync(process, mainWindow);

                await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels", "editor.console");
                await WaitForUiAsync(
                    () =>
                    {
                        var tabs = ReadTabs(process.Id);
                        var console = tabs.FirstOrDefault(tab => tab.Name == "Console");
                        var inspector = tabs.FirstOrDefault(tab => tab.Name == "Inspector");
                        return console is { Enabled: true }
                               && inspector is { Selected: true }
                               && tabs.All(tab => tab.Name != "editor.console");
                    },
                    MenuTimeout,
                    "the restored console tab with the inspector still selected");

                // The View > Hidden Panels menu must refresh through the real
                // UI thread event path: the panel entry is gone and the
                // disabled placeholder is back.
                SendEscape();
                await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels");
                await WaitForUiAsync(
                    () =>
                    {
                        var items = ReadMenuItems(process.Id);
                        return items.Any(item => item.Name == "No hidden panels" && !item.Enabled)
                               && items.All(item => item.Name != "editor.console");
                    },
                    MenuTimeout,
                    "the refreshed hidden panels menu after the restore");
                SendEscape();

                await WaitForDockAsync(
                    layoutPath,
                    dock => CollectHidden(dock).Count == 0,
                    SaveTimeout,
                    "the autosaved layout without hidden panels");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            Assert.AreEqual(0, CollectHidden(dock).Count, "The restart must not reintroduce the hidden state.");
            Assert.IsTrue(
                SnapshotContainsPanel(dock.Main, "editor.console"),
                "The restored console panel must be part of the saved main root.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Unknown_panels_survive_two_real_restarts_without_rewriting_the_layout()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp3-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            // Whitespace-wrapped bytes: only the process itself could turn this
            // into canonical JSON, so byte equality proves nothing rewrote it.
            var seeded = "\r\n\t" + CreateUnknownPanelJson() + "\r\n";
            await File.WriteAllTextAsync(layoutPath, seeded);

            for (var run = 1; run <= 2; run++)
            {
                await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
                {
                    await WaitForUiAsync(
                        () => ReadTabs(process.Id).Any(tab => tab.Name == "third.party.scene"),
                        MenuTimeout,
                        "the unresolved scene placeholder tab");
                    await WaitForUiAsync(
                        () => ReadTexts(process.Id).Any(
                            text => text.Contains("third.party.scene", StringComparison.Ordinal)),
                        MenuTimeout,
                        "the unresolved panel placeholder text");
                    await WaitForUiAsync(
                        () => ReadTexts(process.Id).Any(
                            text => text.Contains("autosave paused", StringComparison.Ordinal)),
                        MenuTimeout,
                        "the paused-autosave diagnostic");

                    // Suppression is a pause, not a save: the shell must never
                    // render a saved-layout report while it holds.
                    AssertNoStatusText(
                        process.Id,
                        "Layout saved",
                        $"Run {run}: a suppressed autosave must not report a saved layout.");
                });

                Assert.AreEqual(
                    seeded,
                    await File.ReadAllTextAsync(layoutPath),
                    $"Run {run}: the exit snapshot must stay suppressed for unresolved panels.");
                Assert.AreEqual(
                    0,
                    Directory.GetFiles(directory, "layout.json.corrupt-*").Length,
                    "An unresolved layout is not corruption and must not be quarantined.");
                Assert.AreEqual(0, Directory.GetFiles(directory, "*.tmp").Length);
            }
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Real_save_layout_persists_unknown_identities_and_hidden_state_faithfully()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp4-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var unknownJson = CreateUnknownPanelJson();
            var seeded = "\r\n\t" + unknownJson + "\r\n";
            await File.WriteAllTextAsync(layoutPath, seeded);

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await WaitForUiAsync(
                    () => ReadTabs(process.Id).Any(tab => tab.Name == "third.party.scene"),
                    MenuTimeout,
                    "the unresolved scene placeholder tab");

                // While persistence is suppressed the shell must not claim a
                // saved layout, so the later success report is attributable to
                // the menu click below.
                AssertNoStatusText(
                    process.Id,
                    "Layout saved",
                    "A suppressed startup must not report a saved layout.");

                await ClickMenuPathAsync(process, mainWindow, "File", "Save Layout");

                await WaitForUiAsync(
                    () => HasStatusText(process.Id, "Layout saved"),
                    SaveTimeout,
                    "the saved report in the status bar");
                await WaitForUiAsync(
                    () =>
                    {
                        var raw = ReadLayoutText(layoutPath);
                        return raw != seeded
                               && raw.Contains("third.party.scene", StringComparison.Ordinal)
                               && raw.Contains("third.party.chat", StringComparison.Ordinal);
                    },
                    SaveTimeout,
                    "the explicit save rewriting the layout with the unknown identities");
                await WaitForUiAsync(
                    () => !ReadTexts(process.Id).Any(
                        text => text.Contains("autosave paused", StringComparison.Ordinal)),
                    MenuTimeout,
                    "the autosave-resumed diagnostic after the explicit save");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            CollectionAssert.AreEqual(
                new[] { "third.party.chat" },
                CollectHidden(dock),
                "The hidden state of the unknown panel must be written back faithfully.");
            Assert.IsTrue(
                CollectPanels(dock).Contains("third.party.scene", StringComparer.Ordinal),
                "The unknown scene identity must be written back with its original ID.");
            Assert.IsTrue(
                CollectPanels(dock).Contains("third.party.chat", StringComparer.Ordinal),
                "The unknown chat identity must be written back with its original ID.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Real_reset_layout_clears_hidden_and_unknown_panels_and_saves_a_clean_layout()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp5-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var seeded = "\r\n\t" + CreateUnknownPanelJson() + "\r\n";
            await File.WriteAllTextAsync(layoutPath, seeded);

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await WaitForUiAsync(
                    () => ReadTabs(process.Id).Any(tab => tab.Name == "third.party.scene"),
                    MenuTimeout,
                    "the unresolved scene placeholder tab");

                await ClickMenuPathAsync(process, mainWindow, "File", "Reset Layout");

                await WaitForUiAsync(
                    () =>
                    {
                        var raw = ReadLayoutText(layoutPath);
                        return !raw.Contains("third.party", StringComparison.Ordinal)
                               && raw.Contains("\"schemaVersion\": 3", StringComparison.Ordinal);
                    },
                    SaveTimeout,
                    "the clean layout file written by the reset command");
                await WaitForUiAsync(
                    () => ReadTabs(process.Id).Any(tab => tab.Name == "Console"),
                    MenuTimeout,
                    "the console tab restored by the reset");
                await WaitForUiAsync(
                    () => !ReadTexts(process.Id).Any(
                        text => text.Contains("unavailable", StringComparison.Ordinal)),
                    MenuTimeout,
                    "the diagnostic cleared by the reset");

                SendEscape();
                await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels");
                await WaitForUiAsync(
                    () =>
                    {
                        var items = ReadMenuItems(process.Id);
                        return items.Any(item => item.Name == "No hidden panels" && !item.Enabled);
                    },
                    MenuTimeout,
                    "the emptied hidden panels menu after the reset");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            CollectionAssert.AreEquivalent(
                new[] { "editor.hierarchy", "editor.scene", "editor.inspector", "editor.console" },
                CollectPanels(dock),
                "The reset layout must contain exactly the built-in panels.");
            Assert.AreEqual(0, CollectHidden(dock).Count, "The reset layout must not hide any panel.");
            Assert.AreEqual(0, dock.Floats.Length);

            // The clean layout must restore on the next start without any
            // hidden or unresolved panel reappearing.
            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await WaitForUiAsync(
                    () =>
                    {
                        var tabs = ReadTabs(process.Id);
                        return tabs.Any(tab => tab.Name == "Console")
                               && tabs.All(
                                   tab => tab.Name != "third.party.scene"
                                          && tab.Name != "third.party.chat");
                    },
                    MenuTimeout,
                    "the clean default tabs after the restart");
            });

            var resaved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(0, CollectHidden(RequireDockSnapshot(resaved)).Count);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Locked_layout_file_isolates_save_failure_and_recovers_after_unlock()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp6-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            await SeedHiddenConsoleAsync(layoutPath);
            var seeded = await File.ReadAllTextAsync(layoutPath);

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await WaitForUiAsync(
                    () => ReadTabs(process.Id).Any(tab => tab.Name == "Hierarchy"),
                    MenuTimeout,
                    "the startup tabs");

                using (new FileStream(layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                {
                    // An unchanged layout is deduplicated into a skip, so the
                    // gate first makes the workspace dirty while the file is
                    // locked: restoring the console through the real menu.
                    await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels", "editor.console");
                    await WaitForUiAsync(
                        () => ReadTabs(process.Id).Any(tab => tab.Name == "Console"),
                        MenuTimeout,
                        "the restored console tab");

                    await ClickMenuPathAsync(process, mainWindow, "File", "Save Layout");

                    // Both the debounced autosave and the explicit menu command
                    // report through the same status element; the click below is
                    // the production path, so the failure text is user-visible.
                    await WaitForUiAsync(
                        () => ReadTexts(process.Id).Any(
                            text => text.StartsWith("Layout save failed:", StringComparison.Ordinal)),
                        MenuTimeout,
                        "the failed-save report in the status bar");
                    AssertNoStatusText(
                        process.Id,
                        "Layout saved",
                        "A failed save must not report success.");

                    Assert.IsFalse(process.HasExited, "The editor must survive a failed layout save.");
                    Assert.AreNotEqual(
                        IntPtr.Zero,
                        FindWindow(process.Id, IsMainWindow),
                        "The editor window must stay alive after a failed save.");
                }

                // The atomic replace failed, so the previous bytes are intact.
                Assert.AreEqual(
                    seeded,
                    await File.ReadAllTextAsync(layoutPath),
                    "A failed save must leave the previous layout intact.");

                await ClickMenuPathAsync(process, mainWindow, "File", "Save Layout");
                await WaitForUiAsync(
                    () => HasStatusText(process.Id, "Layout saved"),
                    SaveTimeout,
                    "the saved report after the retried save");
                await WaitForDockAsync(
                    layoutPath,
                    dock => CollectHidden(dock).Count == 0
                            && SnapshotContainsPanel(dock.Main, "editor.console"),
                    SaveTimeout,
                    "the successful save after the unlock");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            Assert.AreEqual(
                0,
                CollectHidden(dock).Count,
                "The recovered save must persist the console panel as visible.");
            Assert.IsTrue(
                SnapshotContainsPanel(dock.Main, "editor.console"),
                "The recovered save must keep the restored console in the main root.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Hidden_console_inside_a_float_redocks_and_stays_hidden_after_restart()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d8e-rp7-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = CreateHiddenFloatSnapshot() });

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                var floatWindow = await WaitForWindowAsync(
                    process,
                    handle => IsFloatWindow(handle),
                    "the restored floating window");

                await WaitForUiAsync(
                    () =>
                    {
                        var tabs = ReadTabs(process.Id);
                        return tabs.Any(tab => tab.Name == "Scene")
                               && tabs.All(tab => tab.Name != "Console");
                    },
                    MenuTimeout,
                    "the floating scene tab with the console still hidden");

                Assert.IsTrue(
                    PostMessage(floatWindow, WmClose, IntPtr.Zero, IntPtr.Zero),
                    "WM_CLOSE could not be posted to the floating window.");
                Assert.IsTrue(
                    await WaitForConditionAsync(
                        () => FindWindow(process.Id, IsFloatWindow) == IntPtr.Zero,
                        TimeSpan.FromSeconds(15)),
                    "The floating window did not close after WM_CLOSE.");

                await WaitForDockAsync(
                    layoutPath,
                    dock => dock.Floats.Length == 0
                            && CollectHidden(dock).Contains("editor.console", StringComparer.Ordinal),
                    SaveTimeout,
                    "the redocked layout with the hidden console in the main root");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            Assert.AreEqual(0, dock.Floats.Length, "The closed float must not be saved.");
            CollectionAssert.AreEqual(
                new[] { "editor.console" },
                CollectHidden(dock),
                "The hidden state must survive the float redock.");
            Assert.IsTrue(
                SnapshotContainsPanel(dock.Main, "editor.scene"),
                "The redocked scene panel must be in the main root.");

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                await WaitForUiAsync(
                    () =>
                    {
                        var tabs = ReadTabs(process.Id);
                        return tabs.Any(tab => tab.Name == "Scene")
                               && tabs.All(tab => tab.Name != "Console");
                    },
                    MenuTimeout,
                    "the redocked tabs without the hidden console");

                await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels");
                await WaitForUiAsync(
                    () => ReadMenuItems(process.Id).Any(
                        item => item.Name == "editor.console" && item.Enabled),
                    MenuTimeout,
                    "the hidden console menu entry after the restart");
                SendEscape();
            });

            var resaved = await ReadCanonicalLayoutAsync(layoutPath);
            CollectionAssert.AreEqual(
                new[] { "editor.console" },
                CollectHidden(RequireDockSnapshot(resaved)),
                "The hidden panel must survive the second restart.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // ------------------------------------------------------------------
    // Real-process harness
    // ------------------------------------------------------------------

    private static async Task RunEditorAsync(
        string appPath,
        string layoutPath,
        Func<Process, IntPtr, Task> interact)
    {
        var startInfo = new ProcessStartInfo(appPath)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(appPath)!,
        };
        startInfo.ArgumentList.Add("--layout-file");
        startInfo.ArgumentList.Add(layoutPath);

        using var process = Process.Start(startInfo)
                            ?? throw new InvalidOperationException(
                                $"Could not start the editor process: {appPath}");
        try
        {
            var mainWindow = await WaitForWindowAsync(
                process,
                handle => IsMainWindow(handle),
                "the main editor window");

            await interact(process, mainWindow);

            Assert.IsTrue(
                PostMessage(mainWindow, WmClose, IntPtr.Zero, IntPtr.Zero),
                "WM_CLOSE could not be posted to the main editor window.");
            if (!await WaitForExitAsync(process, ExitTimeout))
                Assert.Fail($"The editor process did not exit within {ExitTimeout.TotalSeconds:0}s.");
            Assert.AreEqual(0, process.ExitCode, "The editor process reported a failing exit code.");
        }
        finally
        {
            if (!process.HasExited)
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch
                {
                    // Best-effort cleanup of the process started by this test.
                }

                await process.WaitForExitAsync();
            }
        }
    }

    private static async Task AssertHiddenShellUiAsync(Process process, IntPtr mainWindow)
    {
        await WaitForUiAsync(
            () =>
            {
                var tabs = ReadTabs(process.Id);
                return tabs.Count >= 3 && tabs.All(tab => tab.Name != "Console");
            },
            MenuTimeout,
            "the shell tabs without the hidden console panel");

        await ClickMenuPathAsync(process, mainWindow, "View", "Hidden Panels");
        await WaitForUiAsync(
            () =>
            {
                var items = ReadMenuItems(process.Id);
                return items.Any(item => item.Name == "editor.console" && item.Enabled)
                       && items.All(item => item.Name != "No hidden panels");
            },
            MenuTimeout,
            "the hidden panels menu listing editor.console");
        SendEscape();
    }

    // ------------------------------------------------------------------
    // UI Automation reads (items, availability, selection state, texts)
    // ------------------------------------------------------------------

    private sealed record MenuItemEvidence(string Name, bool Enabled);

    private sealed record TabEvidence(string Name, bool Selected, bool Enabled);

    private static IReadOnlyList<MenuItemEvidence> ReadMenuItems(int processId)
    {
        var result = new List<MenuItemEvidence>();
        foreach (var element in FindByControlType(processId, ControlType.MenuItem))
        {
            try
            {
                var current = element.Current;
                result.Add(new MenuItemEvidence(current.Name, current.IsEnabled));
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
                // The element vanished between FindAll and Current; the next
                // poll re-reads the tree.
            }
        }

        return result;
    }

    private static IReadOnlyList<TabEvidence> ReadTabs(int processId)
    {
        var result = new List<TabEvidence>();
        foreach (var element in FindByControlType(processId, ControlType.TabItem))
        {
            try
            {
                var current = element.Current;
                var selected = element.TryGetCurrentPattern(SelectionItemPattern.Pattern, out var pattern)
                               && pattern is SelectionItemPattern selection
                               && selection.Current.IsSelected;
                result.Add(new TabEvidence(current.Name, selected, current.IsEnabled));
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        return result;
    }

    private static IReadOnlyList<string> ReadTexts(int processId)
    {
        var result = new List<string>();
        foreach (var element in FindByControlType(processId, ControlType.Text))
        {
            try
            {
                result.Add(element.Current.Name ?? string.Empty);
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        return result;
    }

    /// <summary>
    /// Proves a status report is visible in the running shell: the text is read
    /// through the OS UI Automation provider, not from the view-model.
    /// </summary>
    private static bool HasStatusText(int processId, string expected)
        => ReadTexts(processId).Any(text => text == expected);

    private static void AssertNoStatusText(int processId, string unexpected, string because)
    {
        var texts = ReadTexts(processId);
        Assert.IsFalse(
            texts.Any(text => text == unexpected),
            $"{because} Visible texts: [{string.Join(" | ", texts)}].");
    }

    private static IReadOnlyList<AutomationElement> FindByControlType(int processId, ControlType type)
    {
        var result = new List<AutomationElement>();
        try
        {
            var byPid = new PropertyCondition(AutomationElement.ProcessIdProperty, processId);
            var windows = AutomationElement.RootElement.FindAll(TreeScope.Children, byPid);
            var byType = new PropertyCondition(AutomationElement.ControlTypeProperty, type);
            foreach (var window in windows.Cast<AutomationElement>())
                result.AddRange(
                    window.FindAll(TreeScope.Descendants, byType).Cast<AutomationElement>());
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }

        return result;
    }

    private static async Task ClickMenuPathAsync(Process process, IntPtr mainWindow, params string[] path)
    {
        for (var level = 0; level < path.Length; level++)
        {
            var isLeaf = level == path.Length - 1;
            var next = isLeaf ? null : path[level + 1];

            var opened = false;
            for (var attempt = 0; attempt < 5 && !opened; attempt++)
            {
                ActivateWindow(mainWindow);
                var element = FindMenuItemElement(process.Id, path[level]);
                if (element is null)
                {
                    await Task.Delay(400);
                    continue;
                }

                ClickCenter(element);
                await Task.Delay(500);

                // A leftover open menu or a swallowed first click (window
                // activation) is recovered by clicking again; the submenu
                // entry proves the level actually opened.
                if (isLeaf || FindMenuItemElement(process.Id, next!) is not null)
                    opened = true;
            }

            if (!opened)
                Assert.Fail(
                    $"The menu item '{path[level]}' could not be clicked so that "
                    + $"'{next ?? "<the command>"}' appears. Visible items: [{DescribeMenuItems(process.Id)}].");
        }

        await Task.Delay(250);
    }

    private static void ActivateWindow(IntPtr handle)
    {
        ShowWindow(handle, SwShow);
        SetForegroundWindow(handle);
        if (GetForegroundWindow() != handle)
        {
            // A background test host cannot always move the foreground by
            // itself; a neutral ALT keystroke unlocks SetForegroundWindow.
            keybd_event((byte)VkMenu, 0, 0, UIntPtr.Zero);
            keybd_event((byte)VkMenu, 0, KeyeventfKeyup, UIntPtr.Zero);
            SetForegroundWindow(handle);
        }

        Thread.Sleep(250);
    }

    private static string DescribeMenuItems(int processId)
        => string.Join(
            ", ",
            ReadMenuItems(processId).Select(item => $"'{item.Name}' (enabled={item.Enabled})"));

    private static AutomationElement? FindMenuItemElement(int processId, string name)
    {
        foreach (var element in FindByControlType(processId, ControlType.MenuItem))
        {
            try
            {
                var current = element.Current;
                if (current.Name == name && current.IsEnabled && !current.BoundingRectangle.IsEmpty)
                    return element;
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        return null;
    }

    private static void ClickCenter(AutomationElement element)
    {
        var rect = element.Current.BoundingRectangle;
        Assert.IsFalse(rect.IsEmpty, "The menu item to click has no screen area.");
        var x = (int)(rect.Left + rect.Width / 2);
        var y = (int)(rect.Top + rect.Height / 2);
        Assert.IsTrue(SetCursorPos(x, y), $"The cursor could not be moved to ({x}, {y}).");
        Thread.Sleep(120);
        mouse_event(MouseEventfLeftdown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MouseEventfLeftup, 0, 0, 0, UIntPtr.Zero);
    }

    private static async Task WaitForUiAsync(Func<bool> probe, TimeSpan timeout, string description)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var reached = false;
            try
            {
                reached = probe();
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }

            if (reached)
                return;

            await Task.Delay(150);
        }

        Assert.Fail($"The editor UI did not reach {description} within {timeout.TotalSeconds:0}s.");
    }

    // ------------------------------------------------------------------
    // Seed helpers
    // ------------------------------------------------------------------

    private static async Task SeedHiddenConsoleAsync(string layoutPath)
    {
        var workspace = EditorWorkspace.CreateSample(layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        using (var dock = new EditorDockWorkspace(
                   viewModel,
                   panelFactory: new EditorDockPanelFactory(viewModel)))
        {
            var hidden = dock.HidePanel(DockPanelIds.Console);
            Assert.IsTrue(hidden.Success, hidden.Diagnostic);

            workspace.AttachDockPersistence(dock, dirty: true);
            var saved = await workspace.SaveLayoutAsync();
            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        }
    }

    private static string CreateUnknownPanelJson()
    {
        var hidden = DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        var json = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 333,
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(hidden.Workspace!),
            });
        return json
            .Replace("\"editor.scene\"", "\"third.party.scene\"", StringComparison.Ordinal)
            .Replace("\"editor.console\"", "\"third.party.chat\"", StringComparison.Ordinal);
    }

    private static DockWorkspaceSnapshot CreateHiddenFloatSnapshot()
    {
        var workspace = DockWorkspace.CreateDefault();

        var floated = workspace.FloatPanel(
            DockPanelIds.Console,
            new DockBounds(120, 140, 480, 320));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        workspace = floated.Workspace!;

        var floatRoot = workspace.FloatingRoots.Single();
        var floatGroup = floatRoot.Root.EnumerateNodes().OfType<DockTabGroupNode>().Single();

        var moved = workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(floatGroup.Id));
        Assert.IsTrue(moved.Success, moved.Diagnostic);
        workspace = moved.Workspace!;

        var hidden = workspace.HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        workspace = hidden.Workspace!;

        return DockWorkspaceSnapshotCodec.FromWorkspace(workspace);
    }

    // ------------------------------------------------------------------
    // Layout file helpers
    // ------------------------------------------------------------------

    private static async Task WriteNonCanonicalAsync(string path, WorkspaceLayout layout)
    {
        var canonical = WorkspaceLayoutSerializer.Serialize(layout);
        // Whitespace never changes the parse result, so only the process's own
        // save can make the file canonical again.
        await File.WriteAllTextAsync(path, "\r\n\t" + canonical + "\r\n");
    }

    private static async Task<WorkspaceLayout> ReadCanonicalLayoutAsync(string path)
    {
        var raw = await File.ReadAllTextAsync(path);
        var parsed = WorkspaceLayoutSerializer.Deserialize(raw);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(
            WorkspaceLayoutSerializer.Serialize(parsed.Layout),
            raw,
            "The editor must write canonical layout JSON.");
        return parsed.Layout;
    }

    private static bool TryReadDockSnapshot(string path, out DockWorkspaceSnapshot? dock)
    {
        dock = null;
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(
                stream,
                Encoding.UTF8,
                detectEncodingFromByteOrderMarks: true);
            var parsed = WorkspaceLayoutSerializer.Deserialize(reader.ReadToEnd());
            if (parsed.UsedFallback || parsed.Layout.Dock is not { } candidate)
                return false;

            dock = candidate;
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static string ReadLayoutText(string path)
    {
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(
                stream,
                Encoding.UTF8,
                detectEncodingFromByteOrderMarks: true);
            return reader.ReadToEnd();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return string.Empty;
        }
    }

    private static async Task<DockWorkspaceSnapshot> WaitForDockAsync(
        string path,
        Func<DockWorkspaceSnapshot, bool> condition,
        TimeSpan timeout,
        string description)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (TryReadDockSnapshot(path, out var dock)
                && dock is not null
                && condition(dock))
            {
                return dock;
            }

            await Task.Delay(200);
        }

        Assert.Fail($"The layout file did not reach {description} within {timeout.TotalSeconds:0}s.");
        return null!;
    }

    private static DockWorkspaceSnapshot RequireDockSnapshot(WorkspaceLayout layout)
    {
        Assert.IsNotNull(layout.Dock, "The saved layout must contain a dock snapshot.");
        return layout.Dock!;
    }

    private static List<string> CollectHidden(DockWorkspaceSnapshot dock)
    {
        var hidden = new List<string>();
        void Collect(DockNodeSnapshot? node)
        {
            if (node is null)
                return;

            if (node.Hidden is { Length: > 0 })
                hidden.AddRange(node.Hidden);

            Collect(node.First);
            Collect(node.Second);
        }

        Collect(dock.Main);
        foreach (var root in dock.Floats)
            Collect(root.Root);
        return hidden;
    }

    private static List<string> CollectPanels(DockWorkspaceSnapshot dock)
    {
        var panels = new List<string>();
        void Collect(DockNodeSnapshot? node)
        {
            if (node is null)
                return;

            if (node.Panels is { Length: > 0 })
                panels.AddRange(node.Panels);

            Collect(node.First);
            Collect(node.Second);
        }

        Collect(dock.Main);
        foreach (var root in dock.Floats)
            Collect(root.Root);
        return panels;
    }

    private static bool SnapshotContainsPanel(DockNodeSnapshot? node, string panel)
    {
        if (node is null)
            return false;

        if (node.Panels is { } panels && panels.Contains(panel, StringComparer.Ordinal))
            return true;

        return SnapshotContainsPanel(node.First, panel)
               || SnapshotContainsPanel(node.Second, panel);
    }

    // ------------------------------------------------------------------
    // Window/process helpers (shared pattern with RealProcessRestoreTests)
    // ------------------------------------------------------------------

    private static async Task<IntPtr> WaitForWindowAsync(
        Process process,
        Func<IntPtr, bool> predicate,
        string description)
    {
        var deadline = DateTime.UtcNow + WindowTimeout;
        while (DateTime.UtcNow < deadline)
        {
            var handle = FindWindow(process.Id, predicate);
            if (handle != IntPtr.Zero)
                return handle;

            if (process.HasExited)
            {
                Assert.Fail(
                    $"The editor process exited with code {process.ExitCode} before showing {description}.");
            }

            await Task.Delay(100);
        }

        Assert.Fail($"The editor process did not show {description} within {WindowTimeout.TotalSeconds:0}s.");
        return IntPtr.Zero;
    }

    private static async Task<bool> WaitForConditionAsync(Func<bool> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (condition())
                return true;

            await Task.Delay(100);
        }

        return false;
    }

    private static async Task<bool> WaitForExitAsync(Process process, TimeSpan timeout)
    {
        using var cancellation = new CancellationTokenSource(timeout);
        try
        {
            await process.WaitForExitAsync(cancellation.Token);
            return true;
        }
        catch (OperationCanceledException)
        {
            return false;
        }
    }

    private static IntPtr FindWindow(int processId, Func<IntPtr, bool> predicate)
    {
        var result = IntPtr.Zero;
        EnumWindows(
            (handle, _) =>
            {
                GetWindowThreadProcessId(handle, out var owner);
                if (owner != processId || !IsWindowVisible(handle) || !predicate(handle))
                    return true;

                result = handle;
                return false;
            },
            IntPtr.Zero);
        return result;
    }

    private static bool IsMainWindow(IntPtr handle)
        => GetWindowText(handle).StartsWith("Nova Editor", StringComparison.Ordinal);

    private static bool IsFloatWindow(IntPtr handle)
        => GetWindowText(handle).StartsWith("Nova —", StringComparison.Ordinal);

    private static string GetWindowText(IntPtr handle)
    {
        var text = new StringBuilder(512);
        GetWindowTextCore(handle, text, text.Capacity);
        return text.ToString();
    }

    private static string ResolveAppPath()
    {
        var testDirectory = new DirectoryInfo(AppContext.BaseDirectory);
        // The editor app itself always targets net9.0 even when this test
        // assembly builds against net9.0-windows for the UIA client.
        var targetFramework = testDirectory.Name.EndsWith("-windows", StringComparison.Ordinal)
            ? testDirectory.Name[..^"-windows".Length]
            : testDirectory.Name;
        var configuration = testDirectory.Parent?.Name ?? "Debug";
        var repoRoot = testDirectory.Parent?.Parent?.Parent?.Parent?.Parent?.FullName;
        Assert.IsNotNull(repoRoot, "The repository root could not be resolved from the test output.");
        return Path.Combine(
            repoRoot!, "src", "Nova.Editor.App", "bin", configuration, targetFramework,
            "Nova.Editor.App.exe");
    }

    private static void TryDeleteDirectory(string directory)
    {
        try
        {
            Directory.Delete(directory, recursive: true);
        }
        catch
        {
            // A leftover temp directory must not hide the actual gate result.
        }
    }

    private delegate bool EnumWindowsProc(IntPtr handle, IntPtr parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    private static extern int GetWindowTextCore(IntPtr handle, StringBuilder text, int count);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr handle);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(
        IntPtr handle,
        uint message,
        IntPtr wParam,
        IntPtr lParam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr handle);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindow(IntPtr handle, int command);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    private static extern void mouse_event(
        uint flags,
        uint dx,
        uint dy,
        uint data,
        UIntPtr extraInfo);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, NativeInput[] inputs, int size);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeInput
    {
        public uint Type;
        public InputUnion Data;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)]
        public MouseInput Mouse;

        [FieldOffset(0)]
        public KeyboardInput Keyboard;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int X;
        public int Y;
        public uint MouseData;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort VirtualKey;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    private static void SendEscape()
    {
        var down = new NativeInput { Type = InputKeyboard, Data = new InputUnion { Keyboard = new KeyboardInput { VirtualKey = VkEscape } } };
        var up = new NativeInput { Type = InputKeyboard, Data = new InputUnion { Keyboard = new KeyboardInput { VirtualKey = VkEscape, Flags = KeyeventfKeyup } } };
        var inputs = new[] { down, up };
        Assert.AreEqual(
            2u,
            SendInput(2u, inputs, Marshal.SizeOf<NativeInput>()),
            "The escape keystroke could not be delivered to close the open menu.");
        Thread.Sleep(200);
    }
}
