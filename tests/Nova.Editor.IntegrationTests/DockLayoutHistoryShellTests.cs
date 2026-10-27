using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Controls;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D9-D shell gates. Layout replay is reached through the real Edit menu items,
/// its availability is the real menu enablement state, and its outcome is the
/// real status-bar text — not a view-model field.
/// </summary>
[TestClass]
public sealed class DockLayoutHistoryShellTests
{
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly PanelInstanceId UnknownScene = new("third.party.scene");
    private static readonly PanelInstanceId UnknownChat = new("third.party.chat");

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d9d-shell-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    // LS-01: the entries are plain-labelled, start disabled, and one real tab
    // hide becomes exactly one clickable undo step whose outcome is visible.
    [TestMethod]
    public async Task Edit_menu_undo_layout_undoes_a_real_hide_and_states_it()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new EditorDockPanelFactory(viewModel);
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var undo = Require<MenuItem>(shell.FindControl<MenuItem>("UndoLayoutMenuItem"));
            var redo = Require<MenuItem>(shell.FindControl<MenuItem>("RedoLayoutMenuItem"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));

            StringAssert.Contains((string)undo.Header!, "Undo Layout");
            StringAssert.Contains((string)redo.Header!, "Redo Layout");
            Assert.IsFalse(undo.IsEnabled, "a fresh session has nothing to undo");
            Assert.IsFalse(redo.IsEnabled);

            ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Hide Panel");

            Assert.IsNull(dock.FindPanel(DockPanelIds.Console));
            Assert.IsTrue(undo.IsEnabled, "one hide is one undo step");
            Assert.IsFalse(redo.IsEnabled);

            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console), "the panel is mounted again");
            StringAssert.Contains(status.Text!, "Layout undone");
            StringAssert.Contains(status.Text!, "panel content is not restored");
            Assert.IsFalse(undo.IsEnabled, "the history is back at its baseline");
            Assert.IsTrue(redo.IsEnabled);
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // LS-02: redo re-applies the undone change through the menu item, and the
    // pair leaves the layout exactly where the session started.
    [TestMethod]
    public async Task Edit_menu_redo_layout_reapplies_the_undone_hide_and_states_it()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var shell = new EditorShellView(
            viewModel, null, new EditorDockPanelFactory(viewModel));
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var undo = Require<MenuItem>(shell.FindControl<MenuItem>("UndoLayoutMenuItem"));
            var redo = Require<MenuItem>(shell.FindControl<MenuItem>("RedoLayoutMenuItem"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));
            var start = dock.Tree.Digest;

            ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Hide Panel");
            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console));

            redo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.IsNull(dock.FindPanel(DockPanelIds.Console), "the redo hid it again");
            StringAssert.Contains(status.Text!, "Layout redone");
            Assert.IsTrue(undo.IsEnabled);
            Assert.IsFalse(redo.IsEnabled);

            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.AreEqual(start, dock.Tree.Digest, "undo lands back on the session baseline");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // LS-03: the layout history never borrows a shortcut. Ctrl+Z stays the scene
    // command, and neither history moves the other one.
    [TestMethod]
    public async Task Ctrl_Z_stays_the_scene_history_and_layout_replay_has_no_gesture()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var shell = new EditorShellView(
            viewModel, null, new EditorDockPanelFactory(viewModel));
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);

            var sceneBinding = shell.KeyBindings.FirstOrDefault(
                binding => ReferenceEquals(binding.Command, viewModel.UndoCommand));
            Assert.IsNotNull(sceneBinding, "the shell must still bind the scene undo");
            StringAssert.Contains(sceneBinding!.Gesture?.ToString(), "Ctrl");
            StringAssert.Contains(sceneBinding.Gesture!.ToString(), "Z");
            Assert.IsFalse(
                shell.KeyBindings.Any(binding =>
                    ReferenceEquals(binding.Command, viewModel.UndoLayoutCommand)
                    || ReferenceEquals(binding.Command, viewModel.RedoLayoutCommand)),
                "layout replay must not steal a shortcut");

            Assert.IsNull(Descriptor(workspace, NovaCommandIds.UndoLayout).Gesture);
            Assert.IsNull(Descriptor(workspace, NovaCommandIds.RedoLayout).Gesture);
            Assert.AreEqual("Ctrl+Z", Descriptor(workspace, NovaCommandIds.Undo).Gesture);

            ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Hide Panel");
            var hiddenDigest = dock.Tree.Digest;
            Assert.IsTrue(workspace.Router.Execute(NovaCommandIds.AddNode).Success);
            var sceneUndoSteps = workspace.History.UndoCount;

            var sceneUndone = workspace.Router.Execute(NovaCommandIds.Undo);

            Assert.IsTrue(sceneUndone.Success, sceneUndone.Message);
            Assert.AreEqual(hiddenDigest, dock.Tree.Digest, "the scene undo left the layout alone");
            Assert.AreEqual(sceneUndoSteps - 1, workspace.History.UndoCount);
            Assert.IsTrue(dock.CanUndoLayout, "the scene undo left the layout step pending");

            var layoutUndone = workspace.Router.Execute(NovaCommandIds.UndoLayout);

            Assert.IsTrue(layoutUndone.Success, layoutUndone.Message);
            Assert.AreEqual(sceneUndoSteps - 1, workspace.History.UndoCount,
                "the layout undo left the scene history alone");
            Assert.AreNotEqual(hiddenDigest, dock.Tree.Digest);
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console), "the hidden panel came back");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // LS-04: a host that cannot be rendered parks both entries — replaying a
    // layout onto a broken host would only bury the failure deeper — and the
    // shell's own retry control releases the same pending step.
    [TestMethod]
    public async Task A_degraded_host_disables_layout_undo_until_retry_recovers()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var interceptor = new TestInterceptor { FailNode = BottomGroup };
        var shell = new EditorShellView(
            viewModel, interceptor, new EditorDockPanelFactory(viewModel));
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var undo = Require<MenuItem>(shell.FindControl<MenuItem>("UndoLayoutMenuItem"));
            var redo = Require<MenuItem>(shell.FindControl<MenuItem>("RedoLayoutMenuItem"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));
            var banner = Require<TextBlock>(shell.FindControl<TextBlock>("DockDegradedText"));

            ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Hide Panel");
            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console), "the clean undo worked");
            Assert.IsFalse(undo.IsEnabled);
            Assert.IsTrue(redo.IsEnabled);

            interceptor.AlwaysFail = true;
            redo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.IsTrue(dock.IsDegraded, "the redo committed the model and failed the host");
            StringAssert.Contains(status.Text!, "Layout redone");
            StringAssert.Contains(banner.Text!, "injected visual failure");
            Assert.IsFalse(redo.IsEnabled, "a degraded host parks layout redo");
            Assert.IsFalse(undo.IsEnabled, "a degraded host parks layout undo");

            interceptor.AlwaysFail = false;
            Require<NovaButton>(shell.FindControl<NovaButton>("DockRetryButton"))
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));

            Assert.IsFalse(dock.IsDegraded);
            Assert.IsTrue(undo.IsEnabled, "retry releases the parked step");
            Assert.IsFalse(redo.IsEnabled, "and leaves the cursor where the failed replay put it");

            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console), "the parked undo then runs");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // LS-05: a layout command the workspace cannot honour refuses out loud. The
    // menu item's own bound command is invoked, so what the user reads is the
    // shell's status bar, not a log line.
    [TestMethod]
    public async Task A_refused_layout_command_is_stated_in_the_status_bar()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var shell = new EditorShellView(
            viewModel, null, new EditorDockPanelFactory(viewModel));
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var undo = Require<MenuItem>(shell.FindControl<MenuItem>("UndoLayoutMenuItem"));
            var redo = Require<MenuItem>(shell.FindControl<MenuItem>("RedoLayoutMenuItem"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));
            var start = dock.Tree.Digest;

            Assert.AreSame(viewModel.UndoLayoutCommand, undo.Command,
                "the menu item invokes the layout command");
            undo.Command!.Execute(null);

            StringAssert.Contains(status.Text!, "layout.undo");
            StringAssert.Contains(status.Text!, "unavailable");
            Assert.AreEqual(start, dock.Tree.Digest, "a refused replay changes nothing");

            ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Hide Panel");
            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            StringAssert.Contains(status.Text!, "Layout undone");

            undo.Command!.Execute(null);
            var undone = dock.Tree.Digest;

            StringAssert.Contains(status.Text!, "layout.undo");
            StringAssert.Contains(status.Text!, "unavailable");
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Console),
                "the refused replay left the undone panel mounted");
            Assert.AreEqual(undone, dock.Tree.Digest, "an exhausted undo branch replays nothing");
            redo.Command!.Execute(null);
            StringAssert.Contains(status.Text!, "Layout redone");

            dock.BeginShutdown();
            var parked = dock.Tree.Digest;

            Assert.IsFalse(undo.IsEnabled, "shutdown parks layout replay");
            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            undo.Command!.Execute(null);

            StringAssert.Contains(status.Text!, "layout.undo");
            StringAssert.Contains(status.Text!, "unavailable");
            Assert.AreEqual(parked, dock.Tree.Digest, "the shutting-down workspace stayed put");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // LS-06: the Reset Layout contract through its production entry. The real
    // button is two steps in one command — a model reset that discards hidden
    // and unresolved identities, then an explicit save — so it is the save step
    // that ends the ADR-009 persistence pause, and the reset earns no credit for
    // it. Undoing that reset is a fresh appearance of the unresolved identities,
    // which re-arms the pause before anything else gets to write: autosave is
    // stopped by a real follow-up change, and the exit snapshot is refused too.
    // LM-08 asserts the same states one layer down, at the workspace.
    [TestMethod]
    public async Task The_real_reset_button_ends_the_pause_and_its_undo_rearms_it()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var shell = new EditorShellView(
            viewModel, null, new EditorDockPanelFactory(viewModel));
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var undo = Require<MenuItem>(shell.FindControl<MenuItem>("UndoLayoutMenuItem"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));
            var saves = new SaveProbe(workspace);
            var seedDigest = dock.Workspace.Digest;

            CollectionAssert.AreEquivalent(
                new[] { UnknownScene, UnknownChat },
                dock.UnresolvedPanels.ToArray(),
                "the seed loads as an unresolved session");
            Assert.IsTrue(workspace.IsPersistenceSuppressed);
            Assert.IsFalse(undo.IsEnabled, "a session that has done nothing has nothing to undo");

            Require<NovaButton>(shell.FindControl<NovaButton>("ResetLayoutButton"))
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var saved = await saves.Next();

            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
            StringAssert.Contains(status.Text!, "Layout saved");
            Assert.AreEqual(0, dock.UnresolvedPanels.Count, "the reset discarded both unknown ids");
            Assert.IsFalse(
                workspace.IsPersistenceSuppressed,
                "the command's explicit save step is what ends the pause");
            Assert.IsTrue(undo.IsEnabled, "the reset is one undo step");
            var cleanFile = await File.ReadAllTextAsync(_layoutPath);
            Assert.IsFalse(
                cleanFile.Contains("third.party", StringComparison.Ordinal),
                "and wrote the clean layout");

            undo.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            StringAssert.Contains(status.Text!, "Layout undone");
            Assert.AreEqual(seedDigest, dock.Workspace.Digest, "the undone reset restored the seed");
            CollectionAssert.AreEquivalent(
                new[] { UnknownScene, UnknownChat },
                dock.UnresolvedPanels.ToArray(),
                "both unknown identities came back");
            Assert.IsTrue(
                dock.HiddenPanels.Contains(UnknownChat),
                "with the hidden state the seed was loaded with");
            Assert.IsTrue(
                workspace.IsPersistenceSuppressed,
                "their reappearance re-armed the pause");
            StringAssert.Contains(workspace.PersistenceSuppressionReason, "third.party.scene");

            // A real follow-up change is exactly what the re-armed pause has to
            // stop: nothing may reach the store now, least of all the write that
            // would erase the identities the user just asked back.
            var completionsAfterUndo = saves.Count;
            ClickMenu(
                RequireTab(dock, BottomGroup, DockPanelIds.Inspector).ContextMenu,
                "Hide Panel");
            await Task.Delay(900);

            Assert.AreEqual(completionsAfterUndo, saves.Count, "the paused session wrote nothing");
            Assert.AreEqual(
                cleanFile,
                await File.ReadAllTextAsync(_layoutPath),
                "and the file stayed as the compliant reset left it");

            var snapshot = workspace.CaptureShutdownSnapshot(out var snapshotReason);

            Assert.IsNull(snapshot, "the exit snapshot is refused while the pause is re-armed");
            StringAssert.Contains(snapshotReason, "cannot resolve");
            Assert.AreEqual(
                cleanFile,
                await File.ReadAllTextAsync(_layoutPath),
                "and the refused snapshot left the clean file untouched");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    private static CommandDescriptor Descriptor(EditorWorkspace workspace, UiId id)
    {
        var descriptor = workspace.Router.Commands.FirstOrDefault(
            candidate => candidate.Id == id);
        Assert.IsNotNull(descriptor, $"command '{id}' is not registered");
        return descriptor!;
    }

    private static TabItem RequireTab(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var tabs = Require<TabControl>(dock.FindNode(groupId));
        var item = tabs.Items.OfType<TabItem>().Single(
            candidate => candidate.Tag is PanelInstanceId tag && tag == panel);
        Assert.IsNotNull(item.ContextMenu);
        return item;
    }

    private static void ClickMenu(ContextMenu? menu, string header)
    {
        Assert.IsNotNull(menu);
        var item = menu.Items.OfType<MenuItem>().Single(
            candidate => (string)candidate.Header! == header);
        item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
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

    private sealed class SaveProbe
    {
        private TaskCompletionSource<LayoutSaveResult> _next = Create();

        public SaveProbe(EditorWorkspace workspace)
            => workspace.LayoutSaveCompleted += OnCompleted;

        public int Count { get; private set; }

        public Task<LayoutSaveResult> Next()
            => _next.Task.WaitAsync(TimeSpan.FromSeconds(10));

        private void OnCompleted(object? sender, LayoutSaveCompletedEventArgs e)
        {
            Count++;
            _next.TrySetResult(e.Result);
            _next = Create();
        }

        private static TaskCompletionSource<LayoutSaveResult> Create()
            => new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class TestInterceptor : IDockNodeApplyInterceptor
    {
        public DockNodeId? FailNode { get; set; }
        public int FailuresRemaining { get; set; }
        public bool AlwaysFail { get; set; }

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            if (!AlwaysFail && (FailNode != node || FailuresRemaining <= 0))
                return;

            if (!AlwaysFail)
                FailuresRemaining--;
            throw new InvalidOperationException("injected visual failure");
        }
    }
}
