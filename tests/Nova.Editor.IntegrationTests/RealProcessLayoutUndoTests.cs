using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows.Automation;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D9-D and D9-E real-process gates. It extends the shared D8-E process harness:
/// the editor executable is started with --layout-file, the layout history is driven
/// through the OS-level menu, a real mouse drag and a real tab context menu, and
/// every claim is read back through UI Automation from the running shell and from
/// the layout file the process itself wrote.
/// </summary>
public sealed partial class RealProcessHiddenPanelTests
{
    private const ushort VkControl = 0x11;
    private const ushort VkZ = 0x5A;
    private const uint MouseEventfRightdown = 0x0008;
    private const uint MouseEventfRightup = 0x0010;
    private const double DragStepDelayMs = 35d;

    private const string LayoutUndone = "Layout undone (panel content is not restored)";
    private const string LayoutRedone = "Layout redone (panel content is not restored)";

    [TestMethod]
    public async Task Real_shell_menu_undo_and_redo_replay_a_reset_without_borrowing_ctrl_z()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d9d-rp-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            await SeedHiddenConsoleAsync(layoutPath);

            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                var pid = process.Id;

                await OpenEditMenuAsync(process, mainWindow);
                var fresh = ReadMenuItems(pid);
                SendEscape();
                AssertMenuItem(fresh, "Undo Layout", false, "a restored session has no step yet");
                AssertMenuItem(fresh, "Redo Layout", false, "and no redo branch");
                Assert.IsFalse(
                    ReadTabs(pid).Any(tab => tab.Name == "Console"),
                    "the seeded layout really hides the console the undo must come back to");

                ClickButton(pid, "Reset Layout");
                await WaitForUiAsync(
                    () => ReadTabs(pid).Any(tab => tab.Name == "Console"),
                    MenuTimeout,
                    "the console tab the real Reset Layout button brought back");

                await OpenEditMenuAsync(process, mainWindow);
                AssertMenuItem(ReadMenuItems(pid), "Undo Layout", true, "a reset is one layout step");
                SendEscape();

                await ClickMenuPathAsync(process, mainWindow, "Edit", "Undo Layout");
                await WaitForUiAsync(
                    () => HasStatusText(pid, LayoutUndone),
                    MenuTimeout,
                    $"the status bar reading '{LayoutUndone}'");
                Assert.IsFalse(
                    ReadTabs(pid).Any(tab => tab.Name == "Console"),
                    "the undo put the hidden console back where the session started");

                await ClickMenuPathAsync(process, mainWindow, "Edit", "Redo Layout");
                await WaitForUiAsync(
                    () => HasStatusText(pid, LayoutRedone),
                    MenuTimeout,
                    $"the status bar reading '{LayoutRedone}'");
                Assert.IsTrue(
                    ReadTabs(pid).Any(tab => tab.Name == "Console"),
                    "the redo re-applied the reset");

                // O-4: Ctrl+Z belongs to the scene history. It must neither run a
                // layout undo nor spend the layout step that is still pending.
                ActivateWindow(mainWindow);
                SendEscape();
                SendCtrlZ();
                await Task.Delay(700);

                Assert.IsFalse(
                    ReadTexts(pid).Any(text =>
                        text.StartsWith("Layout undo", StringComparison.Ordinal)),
                    "Ctrl+Z must not run a layout undo. Visible texts: "
                    + $"[{string.Join(" | ", ReadTexts(pid))}].");
                Assert.IsTrue(
                    ReadTabs(pid).Any(tab => tab.Name == "Console"),
                    "Ctrl+Z must leave the layout where the redo put it");

                await OpenEditMenuAsync(process, mainWindow);
                AssertMenuItem(
                    ReadMenuItems(pid),
                    "Undo Layout",
                    true,
                    "Ctrl+Z must leave the pending layout step pending");
                SendEscape();
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            CollectionAssert.AreEqual(
                Array.Empty<string>(),
                CollectHidden(dock),
                "the exited process wrote the redone layout: replay does not bypass persistence.");
            CollectionAssert.Contains(
                CollectPanels(dock),
                "editor.console",
                "and the replayed panel identity is in the file the process itself wrote.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    private static void AssertMenuItem(
        IReadOnlyList<MenuItemEvidence> items,
        string name,
        bool enabled,
        string because)
    {
        var item = items.FirstOrDefault(candidate => candidate.Name == name);
        Assert.IsNotNull(
            item,
            $"'{name}' is not in the Edit menu. Items: "
            + $"[{string.Join(", ", items.Select(candidate => candidate.Name))}].");
        Assert.AreEqual(enabled, item!.Enabled, $"{because} (item={name}).");
    }

    private static async Task OpenEditMenuAsync(Process process, IntPtr mainWindow)
    {
        await ClickMenuPathAsync(process, mainWindow, "Edit");
        await WaitForUiAsync(
            () => ReadMenuItems(process.Id).Any(item => item.Name == "Undo Layout"),
            MenuTimeout,
            "the Edit menu with its layout entries");
    }

    private static void ClickButton(int processId, string name)
    {
        var buttons = FindByControlType(processId, ControlType.Button);
        var button = buttons.FirstOrDefault(candidate => candidate.Current.Name == name);
        Assert.IsNotNull(
            button,
            $"The '{name}' button is not in the running shell. Buttons: "
            + $"[{string.Join(", ", buttons.Select(candidate => candidate.Current.Name))}].");
        ClickCenter(button!);
        Thread.Sleep(300);
    }

    private static void SendCtrlZ()
    {
        keybd_event((byte)VkControl, 0, 0, UIntPtr.Zero);
        keybd_event((byte)VkZ, 0, 0, UIntPtr.Zero);
        keybd_event((byte)VkZ, 0, KeyeventfKeyup, UIntPtr.Zero);
        keybd_event((byte)VkControl, 0, KeyeventfKeyup, UIntPtr.Zero);
        Thread.Sleep(200);
    }

    // RP-L1: a complete real mouse drag of a tab is one layout step. The preview
    // updates while the button is held must not queue an entry each, the single
    // undo has to return both the UIA tab order and the screen pixels the drag
    // started from, and the redo has to replay that same one step.
    [TestMethod]
    public async Task A_real_mouse_tab_drag_is_one_layout_step_and_its_undo_restores_the_tab_order()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d9e-rp-drag-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        await File.WriteAllTextAsync(
            layoutPath,
            WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default));

        try
        {
            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                var pid = process.Id;

                await WaitForUiAsync(
                    () => ReadTabOrder(pid).Length >= 4,
                    MenuTimeout,
                    "the four dock tabs of a default session");

                var before = ReadTabOrder(pid);
                CollectionAssert.AreEqual(
                    new[] { "Hierarchy", "Scene", "Inspector", "Console" },
                    before,
                    "the UIA tab order the drag must return to");

                // The first real click on an inactive window only activates it, so
                // the drag has to start from a foreground window like a user's does.
                ActivateWindow(mainWindow);
                var dragged = RequireTab(pid, "Console");
                var dragStartLeft = dragged.Current.BoundingRectangle.Left;

                DragTabTo(dragged, TabDropPoint(RequireTab(pid, "Inspector")));

                await WaitForUiAsync(
                    () => IsAhead(ReadTabOrder(pid), "Console", "Inspector"),
                    MenuTimeout,
                    "the dragged console tab to land ahead of the inspector tab. Tab rects: "
                    + $"[{string.Join(", ", TabDiag(pid))}].");
                Assert.AreEqual(
                    4,
                    ReadTabOrder(pid).Length,
                    "a move remounts nothing: all four tabs stay in the UI");

                await OpenEditMenuAsync(process, mainWindow);
                var afterDrag = ReadMenuItems(pid);
                AssertMenuItem(afterDrag, "Undo Layout", true, "one complete drag is one layout step");
                AssertMenuItem(afterDrag, "Redo Layout", false, "and it left no redo branch behind");
                SendEscape();

                await ClickMenuPathAsync(process, mainWindow, "Edit", "Undo Layout");
                await WaitForUiAsync(
                    () => HasStatusText(pid, LayoutUndone),
                    MenuTimeout,
                    $"the status bar reading '{LayoutUndone}'");
                CollectionAssert.AreEqual(
                    before,
                    ReadTabOrder(pid),
                    "one undo returned the whole UIA tab order");
                var restoredLeft = RequireTab(pid, "Console").Current.BoundingRectangle.Left;
                Assert.IsTrue(
                    Math.Abs(restoredLeft - dragStartLeft) <= 1d,
                    "one undo returned the tab to the screen column it was dragged from "
                    + $"(before={dragStartLeft:0.##}, after={restoredLeft:0.##}).");

                await ClickMenuPathAsync(process, mainWindow, "Edit", "Redo Layout");
                await WaitForUiAsync(
                    () => HasStatusText(pid, LayoutRedone),
                    MenuTimeout,
                    $"the status bar reading '{LayoutRedone}'");
                Assert.IsTrue(
                    IsAhead(ReadTabOrder(pid), "Console", "Inspector"),
                    "the redo replayed that single step and nothing else");
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            CollectionAssert.AreEqual(
                new[] { "editor.console", "editor.inspector" },
                GroupPanels(dock, "dock.tabs.bottom"),
                "the process exited on the redone order: replay does not bypass persistence.");
            CollectionAssert.AreEqual(
                Array.Empty<string>(),
                CollectHidden(dock),
                "a drag is a move, so nothing was recorded hidden or closed.");
            CollectionAssert.AreEqual(
                new[] { "editor.hierarchy", "editor.scene", "editor.console", "editor.inspector" },
                CollectPanels(dock),
                "and every panel identity survived the drag, the undo and the redo.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // RP-L2: a close through the real tab context menu is one undoable layout
    // step, and the tab the undo brings back is a live view — a real click on it
    // activates it. Activation commits without recording, so it must not spend the
    // history the close left behind.
    [TestMethod]
    public async Task A_real_context_menu_close_undoes_to_a_live_tab_that_costs_no_history()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process gates require an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d9e-rp-close-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        await File.WriteAllTextAsync(
            layoutPath,
            WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default));

        try
        {
            await RunEditorAsync(appPath, layoutPath, async (process, mainWindow) =>
            {
                var pid = process.Id;

                await WaitForUiAsync(
                    () => ReadTabOrder(pid).Length >= 4,
                    MenuTimeout,
                    "the four dock tabs of a default session");

                ActivateWindow(mainWindow);
                RightClick(RequireTab(pid, "Console"));
                await WaitForUiAsync(
                    () => FindMenuItemElement(pid, "Close Panel") is not null,
                    MenuTimeout,
                    "the tab context menu with its Close Panel entry");
                ClickCenter(FindMenuItemElement(pid, "Close Panel")!);

                await WaitForUiAsync(
                    () => ReadTabOrder(pid).All(name => name != "Console"),
                    MenuTimeout,
                    "the real close to take the console tab out of the UI");

                await OpenEditMenuAsync(process, mainWindow);
                AssertMenuItem(
                    ReadMenuItems(pid),
                    "Undo Layout",
                    true,
                    "a context-menu close is one layout step");
                SendEscape();

                await ClickMenuPathAsync(process, mainWindow, "Edit", "Undo Layout");
                await WaitForUiAsync(
                    () => ReadTabOrder(pid).Any(name => name == "Console"),
                    MenuTimeout,
                    "the undo to mount the console tab again");
                Assert.IsTrue(
                    HasStatusText(pid, LayoutUndone),
                    "and state the undo in the status bar. Visible texts: "
                    + $"[{string.Join(" | ", ReadTexts(pid))}].");

                // A restored tab is only worth the claim if it really works: the
                // click goes through the OS input queue and the UIA selection
                // state is the answer.
                ClickCenter(RequireTab(pid, "Console"));
                await WaitForUiAsync(
                    () => ReadTabs(pid).Any(tab => tab.Name == "Console" && tab.Selected),
                    MenuTimeout,
                    "the recreated console tab to activate on a real click");

                await OpenEditMenuAsync(process, mainWindow);
                var afterActivate = ReadMenuItems(pid);
                AssertMenuItem(
                    afterActivate,
                    "Undo Layout",
                    false,
                    "activating the restored tab spent no layout step");
                AssertMenuItem(
                    afterActivate,
                    "Redo Layout",
                    true,
                    "and the closed state is still the one redo away");
                SendEscape();
            });

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var dock = RequireDockSnapshot(saved);
            CollectionAssert.AreEqual(
                Array.Empty<string>(),
                CollectHidden(dock),
                "a close is not a hide: the exited file records no hidden panel.");
            Assert.AreEqual(
                "editor.console",
                GroupActivePanel(dock, "dock.tabs.bottom"),
                "and the real click on the recreated tab is what the process persisted.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // ------------------------------------------------------------------
    // Real mouse interaction helpers
    // ------------------------------------------------------------------

    private static string[] ReadTabOrder(int processId)
    {
        var order = new List<string>();
        foreach (var element in FindByControlType(processId, ControlType.TabItem))
        {
            try
            {
                order.Add(element.Current.Name ?? string.Empty);
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        return order.ToArray();
    }

    private static AutomationElement RequireTab(int processId, string name)
    {
        foreach (var element in FindByControlType(processId, ControlType.TabItem))
        {
            try
            {
                var rect = element.Current.BoundingRectangle;
                if (element.Current.Name == name && !rect.IsEmpty)
                    return element;
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        Assert.Fail(
            $"The '{name}' tab is not in the running shell. Tabs: "
            + $"[{string.Join(", ", ReadTabOrder(processId))}].");
        return null!;
    }

    private static string[] TabDiag(int processId)
    {
        var diag = new List<string>();
        foreach (var element in FindByControlType(processId, ControlType.TabItem))
        {
            try
            {
                var rect = element.Current.BoundingRectangle;
                diag.Add($"{element.Current.Name}({rect.X:0},{rect.Y:0},{rect.Width:0}x{rect.Height:0})");
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        return diag.ToArray();
    }

    private static bool IsAhead(string[] order, string first, string second)
    {
        var firstIndex = Array.IndexOf(order, first);
        var secondIndex = Array.IndexOf(order, second);
        return firstIndex >= 0 && secondIndex >= 0 && firstIndex < secondIndex;
    }

    /// <summary>
    /// Drops on the leading edge of a tab strip entry, which the drag controller
    /// resolves to an insertion in front of it.
    /// </summary>
    private static (int X, int Y) TabDropPoint(AutomationElement target)
    {
        var rect = target.Current.BoundingRectangle;
        Assert.IsFalse(rect.IsEmpty, "The drop target tab has no screen area.");
        return ((int)(rect.Left + rect.Width * 0.25d), (int)(rect.Top + rect.Height / 2d));
    }

    private static void DragTabTo(AutomationElement source, (int X, int Y) target)
    {
        var rect = source.Current.BoundingRectangle;
        Assert.IsFalse(rect.IsEmpty, "The tab to drag has no screen area.");
        var startX = (int)(rect.Left + rect.Width / 2d);
        var startY = (int)(rect.Top + rect.Height / 2d);

        MoveCursorTo(startX, startY);
        mouse_event(MouseEventfLeftdown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(150);

        const int steps = 14;
        for (var step = 1; step <= steps; step++)
        {
            MoveCursorTo(
                startX + (target.X - startX) * step / steps,
                startY + (target.Y - startY) * step / steps);
            Thread.Sleep((int)DragStepDelayMs);
        }

        Thread.Sleep(150);
        mouse_event(MouseEventfLeftup, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(250);
    }

    private static void RightClick(AutomationElement element)
    {
        var rect = element.Current.BoundingRectangle;
        Assert.IsFalse(rect.IsEmpty, "The tab to right-click has no screen area.");
        MoveCursorTo((int)(rect.Left + rect.Width / 2d), (int)(rect.Top + rect.Height / 2d));
        mouse_event(MouseEventfRightdown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MouseEventfRightup, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(250);
    }

    private static void MoveCursorTo(int x, int y)
        => Assert.IsTrue(SetCursorPos(x, y), $"The cursor could not be moved to ({x}, {y}).");

    private static string[] GroupPanels(DockWorkspaceSnapshot dock, string groupId)
        => FindGroup(dock, groupId)?.Panels ?? Array.Empty<string>();

    private static string? GroupActivePanel(DockWorkspaceSnapshot dock, string groupId)
        => FindGroup(dock, groupId)?.ActivePanel;

    private static DockNodeSnapshot? FindGroup(DockWorkspaceSnapshot dock, string groupId)
    {
        DockNodeSnapshot? Search(DockNodeSnapshot? node)
        {
            if (node is null)
                return null;

            if (string.Equals(node.Id, groupId, StringComparison.Ordinal))
                return node;

            return Search(node.First) ?? Search(node.Second);
        }

        var found = Search(dock.Main);
        foreach (var root in dock.Floats)
            found ??= Search(root.Root);

        return found;
    }
}
