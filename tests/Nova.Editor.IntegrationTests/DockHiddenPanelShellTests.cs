using Avalonia.Controls;
using Avalonia.Interactivity;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Controls;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class DockHiddenPanelShellTests
{
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly PanelInstanceId UnknownScene = new("third.party.scene");
    private static readonly PanelInstanceId UnknownChat = new("third.party.chat");

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d8d-shell-tests", Guid.NewGuid().ToString("N"));
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
    public async Task Tab_context_menu_hide_unmounts_and_the_hidden_panels_menu_restores_it()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var console = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));
            Assert.AreEqual(4, factory.Calls.Count);

            var tab = RequireTab(dock, BottomGroup, DockPanelIds.Console);
            CollectionAssert.AreEqual(
                new[] { "Float Panel", "Hide Panel", "Close Panel" },
                tab.ContextMenu!.Items.OfType<MenuItem>()
                    .Select(item => (string)item.Header!)
                    .ToArray());
            ClickMenu(tab.ContextMenu, "Hide Panel");

            Assert.IsTrue(dock.HiddenPanels.Contains(DockPanelIds.Console));
            Assert.AreEqual(1, console.DisposeCount);
            Assert.IsNull(dock.FindPanel(DockPanelIds.Console));
            Assert.AreEqual(
                1,
                Require<TabControl>(dock.FindNode(BottomGroup)).Items.Count);
            var group = dock.Workspace.MainRoot.FindGroup(BottomGroup)!;
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Inspector },
                group.VisiblePanels.ToArray());
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Console },
                group.HiddenPanels.ToArray());
            Assert.AreEqual(DockPanelIds.Inspector, group.ActivePanel);
            Assert.IsFalse(
                Require<Border>(shell.FindControl<Border>("DockDegradedBanner"))
                    .IsVisible);

            var hiddenMenu = Require<MenuItem>(shell.FindControl<MenuItem>("HiddenPanelsMenu"));
            var showItem = hiddenMenu.Items.OfType<MenuItem>().Single();
            Assert.AreEqual(DockPanelIds.Console.ToString(), (string)showItem.Header!);
            showItem.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.AreEqual(0, dock.HiddenPanels.Count);
            Assert.AreEqual(5, factory.Calls.Count);
            Assert.AreEqual(DockPanelIds.Console, factory.Calls[4]);
            var recreated = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));
            Assert.AreNotSame(console, recreated);
            Assert.AreEqual(1, console.DisposeCount);
            Assert.AreEqual(0, recreated.DisposeCount);
            group = dock.Workspace.MainRoot.FindGroup(BottomGroup)!;
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Inspector, DockPanelIds.Console },
                group.VisiblePanels.ToArray());
            Assert.AreEqual(DockPanelIds.Inspector, group.ActivePanel);
            Assert.AreEqual(
                2,
                Require<TabControl>(dock.FindNode(BottomGroup)).Items.Count);
            Assert.IsTrue(dock.Current!.IsCurrent);
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task Tab_context_menu_close_removes_the_panel_and_releases_its_view_once()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var console = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));

            ClickMenu(
                RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu,
                "Close Panel");

            Assert.IsFalse(dock.Workspace.EnumeratePanels().Contains(DockPanelIds.Console));
            Assert.AreEqual(1, console.DisposeCount);
            Assert.IsNull(dock.FindPanel(DockPanelIds.Console));
            Assert.AreEqual(4, factory.Calls.Count);
            Assert.AreEqual(
                1,
                Require<TabControl>(dock.FindNode(BottomGroup)).Items.Count);
            Assert.IsNull(shell.FindControl<MenuItem>("HiddenPanelsMenu")!.Items
                .OfType<MenuItem>()
                .FirstOrDefault(item => (string?)item.Header == "editor.console"));
            Assert.IsTrue(dock.Current!.IsCurrent);
            Assert.IsFalse(
                Require<Border>(shell.FindControl<Border>("DockDegradedBanner"))
                    .IsVisible);
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task Unknown_panels_render_placeholders_with_status_diagnostics()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var workspace = viewModel.Workspace;

            Assert.IsNull(dock.RestoreFailure);
            Assert.AreEqual(DockApplyStatus.Applied, dock.Current!.Status);
            Assert.IsTrue(dock.Current.IsCurrent);
            Assert.AreEqual(2, factory.Calls.Count);
            CollectionAssert.AreEquivalent(
                new[] { DockPanelIds.Hierarchy, DockPanelIds.Inspector },
                factory.Calls.ToArray());
            Assert.IsFalse(
                Require<Border>(shell.FindControl<Border>("DockDegradedBanner"))
                    .IsVisible);

            CollectionAssert.AreEquivalent(
                new[] { UnknownScene, UnknownChat },
                dock.UnresolvedPanels.ToArray());
            CollectionAssert.AreEqual(
                new[] { UnknownChat },
                dock.HiddenPanels.ToArray());
            RequirePlaceholder(dock, SceneGroup, UnknownScene);
            var sceneTabs = Require<TabControl>(dock.FindNode(SceneGroup));
            Assert.AreEqual("third.party.scene", HeaderAt(sceneTabs, 0));

            var diagnostic = Require<TextBlock>(
                shell.FindControl<TextBlock>("DockDiagnosticText"));
            Assert.IsTrue(diagnostic.IsVisible);
            StringAssert.Contains(diagnostic.Text, "2 panel(s) unavailable in this build");
            StringAssert.Contains(diagnostic.Text, "autosave paused, use Save Layout");
            Assert.IsTrue(workspace.IsPersistenceSuppressed);

            var hiddenMenu = Require<MenuItem>(shell.FindControl<MenuItem>("HiddenPanelsMenu"));
            var chatItem = hiddenMenu.Items.OfType<MenuItem>().Single(
                item => (string)item.Header! == "third.party.chat");
            chatItem.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

            Assert.AreEqual(0, dock.HiddenPanels.Count);
            Assert.AreEqual(2, factory.Calls.Count);
            RequirePlaceholder(dock, BottomGroup, UnknownChat);
            var bottom = Require<TabControl>(dock.FindNode(BottomGroup));
            Assert.AreEqual(2, bottom.Items.Count);
            Assert.AreEqual("Inspector", HeaderAt(bottom, 0));
            Assert.AreEqual("third.party.chat", HeaderAt(bottom, 1));

            var saved = await workspace.SaveLayoutAsync();
            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
            Assert.IsFalse(workspace.IsPersistenceSuppressed);

            Assert.IsTrue(dock.ActivateTab(BottomGroup, UnknownChat).Success);
            StringAssert.Contains(diagnostic.Text, "2 panel(s) unavailable in this build");
            Assert.IsFalse(diagnostic.Text!.Contains("autosave paused", StringComparison.Ordinal));

            var raw = await File.ReadAllTextAsync(_layoutPath);
            StringAssert.Contains(raw, "third.party.scene");
            StringAssert.Contains(raw, "third.party.chat");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task Reset_layout_button_clears_unresolved_and_hidden_and_saves_a_clean_layout()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var workspace = viewModel.Workspace;
            var inspector = Require<RecordingView>(dock.FindPanel(DockPanelIds.Inspector));

            // The seeded layout leaves Inspector as the only visible panel in
            // its group (WouldEmptyGroup). Surface the unresolved chat panel
            // first so Inspector can be hidden through the real tab menu.
            var hiddenMenu = Require<MenuItem>(shell.FindControl<MenuItem>("HiddenPanelsMenu"));
            hiddenMenu.Items.OfType<MenuItem>().Single(
                    item => (string)item.Header! == "third.party.chat")
                .RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
            RequirePlaceholder(dock, BottomGroup, UnknownChat);

            ClickMenu(
                RequireTab(dock, BottomGroup, DockPanelIds.Inspector).ContextMenu,
                "Hide Panel");
            Assert.AreEqual(1, inspector.DisposeCount);
            Assert.IsNull(dock.FindPanel(DockPanelIds.Inspector));

            var saveProbe = new SaveCompletion(workspace);
            Require<NovaButton>(shell.FindControl<NovaButton>("ResetLayoutButton"))
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var saved = await saveProbe.Next();

            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
            Assert.IsFalse(workspace.IsPersistenceSuppressed);
            CollectionAssert.AreEquivalent(
                new[]
                {
                    DockPanelIds.Hierarchy,
                    DockPanelIds.Scene,
                    DockPanelIds.Inspector,
                    DockPanelIds.Console,
                },
                dock.Workspace.EnumeratePanels().ToArray());
            Assert.AreEqual(0, dock.Workspace.EnumerateHiddenPanels().Count());
            Assert.AreEqual(0, dock.UnresolvedPanels.Count);
            Assert.AreEqual(0, dock.HiddenPanels.Count);
            Assert.IsNull(dock.FindPanel(UnknownScene));
            Assert.IsNull(dock.FindPanel(UnknownChat));
            Assert.AreEqual(1, inspector.DisposeCount);
            Assert.AreEqual(5, factory.Calls.Count);

            var emptiedMenu = Require<MenuItem>(shell.FindControl<MenuItem>("HiddenPanelsMenu"));
            Assert.AreEqual(1, emptiedMenu.Items.Count);
            Assert.IsFalse(Require<MenuItem>(emptiedMenu.Items[0]).IsEnabled);

            var diagnostic = Require<TextBlock>(
                shell.FindControl<TextBlock>("DockDiagnosticText"));
            Assert.IsFalse(diagnostic.IsVisible);

            var raw = await File.ReadAllTextAsync(_layoutPath);
            StringAssert.Contains(raw, "\"schemaVersion\": 3");
            Assert.IsFalse(raw.Contains("third.party", StringComparison.Ordinal));
            var parsed = WorkspaceLayoutSerializer.Deserialize(raw);
            Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
            Assert.AreEqual(
                4,
                parsed.DockWorkspace!.EnumeratePanels().Count());
            Assert.AreEqual(
                0,
                parsed.DockWorkspace.EnumerateHiddenPanels().Count());
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    // D9-C: the layout history is process-local, so undoing an explicit reset can
    // put unknown identities back into a workspace whose pause that reset had
    // already lifted. The D8 protection re-arms on the reappearance, the file on
    // disk keeps the clean layout, and only an explicit save lifts it again.
    [TestMethod]
    public async Task Undo_after_a_reset_returns_unknown_panels_and_pauses_persistence_again()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var workspace = viewModel.Workspace;
            var diagnostic = Require<TextBlock>(
                shell.FindControl<TextBlock>("DockDiagnosticText"));
            Assert.IsTrue(workspace.IsPersistenceSuppressed);
            Assert.IsTrue(diagnostic.IsVisible);

            var saveProbe = new SaveCompletion(workspace);
            Require<NovaButton>(shell.FindControl<NovaButton>("ResetLayoutButton"))
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var reset = await saveProbe.Next();

            Assert.AreEqual(LayoutSaveStatus.Saved, reset.Status, reset.Error);
            Assert.IsFalse(workspace.IsPersistenceSuppressed);
            Assert.IsFalse(diagnostic.IsVisible);
            Assert.AreEqual(0, dock.UnresolvedPanels.Count);
            var cleanFile = await File.ReadAllTextAsync(_layoutPath);
            Assert.IsFalse(cleanFile.Contains("third.party", StringComparison.Ordinal));

            Assert.IsTrue(dock.CanUndoLayout, "the reset is one undo step");
            var resetCalls = factory.Calls.Count;

            var undone = dock.UndoLayout();

            Assert.IsTrue(undone.Success, undone.Diagnostic);
            CollectionAssert.AreEquivalent(
                new[] { UnknownScene, UnknownChat },
                dock.UnresolvedPanels.ToArray());
            CollectionAssert.AreEqual(new[] { UnknownChat }, dock.HiddenPanels.ToArray());
            Assert.AreEqual(
                resetCalls,
                factory.Calls.Count,
                "the undo brings the unknown panels back as placeholders");
            RequirePlaceholder(dock, SceneGroup, UnknownScene);
            Assert.IsTrue(
                workspace.IsPersistenceSuppressed,
                "a reappearing unknown panel re-arms the unresolved-panel pause");
            Assert.IsNotNull(workspace.PersistenceSuppressionReason);
            StringAssert.Contains(workspace.PersistenceSuppressionReason, "third.party.scene");
            Assert.IsTrue(diagnostic.IsVisible, "the pause is stated in the shell");
            StringAssert.Contains(diagnostic.Text, "2 panel(s) unavailable in this build");
            StringAssert.Contains(diagnostic.Text, "autosave paused, use Save Layout");
            Assert.AreEqual(
                cleanFile,
                await File.ReadAllTextAsync(_layoutPath),
                "the undone model never reached the file");

            var forced = await workspace.SaveLayoutAsync();

            Assert.AreEqual(LayoutSaveStatus.Saved, forced.Status, forced.Error);
            Assert.IsFalse(workspace.IsPersistenceSuppressed);
            var rewritten = await File.ReadAllTextAsync(_layoutPath);
            Assert.AreNotEqual(cleanFile, rewritten);
            StringAssert.Contains(rewritten, "third.party.scene");
            StringAssert.Contains(rewritten, "third.party.chat");
            StringAssert.Contains(diagnostic.Text, "2 panel(s) unavailable in this build");
            Assert.IsFalse(
                diagnostic.Text!.Contains("autosave paused", StringComparison.Ordinal));
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
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

    private static void RequirePlaceholder(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var tab = RequireTab(dock, groupId, panel);
        var host = Require<ContentControl>(tab.Content);
        var placeholder = Require<TextBlock>(host.Content);
        Assert.AreEqual("DockPanelUnavailable", placeholder.Name);
        StringAssert.Contains(placeholder.Text ?? string.Empty, panel.ToString());
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
    }

    private static string? HeaderAt(TabControl tabs, int index)
        => (tabs.Items[index] as TabItem)?.Header as string;

    private sealed class SaveCompletion
    {
        private TaskCompletionSource<LayoutSaveResult> _next = Create();

        public SaveCompletion(EditorWorkspace workspace)
            => workspace.LayoutSaveCompleted += OnCompleted;

        public Task<LayoutSaveResult> Next()
            => _next.Task.WaitAsync(TimeSpan.FromSeconds(10));

        private void OnCompleted(object? sender, LayoutSaveCompletedEventArgs e)
        {
            _next.TrySetResult(e.Result);
            _next = Create();
        }

        private static TaskCompletionSource<LayoutSaveResult> Create()
            => new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class RecordingFactory : IDockPanelViewFactory
    {
        private readonly IDockPanelViewFactory _inner;

        public RecordingFactory(IDockPanelViewFactory inner) => _inner = inner;

        public List<PanelInstanceId> Calls { get; } = [];

        public bool CanCreate(PanelInstanceId instance) => _inner.CanCreate(instance);

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls.Add(instance);
            return new RecordingView(_inner.Create(instance));
        }
    }

    private sealed class RecordingView : IDockPanelView
    {
        private readonly IDockPanelView _inner;

        public RecordingView(IDockPanelView inner) => _inner = inner;

        public string Title => _inner.Title;

        public Control Content => _inner.Content;

        public int DisposeCount { get; private set; }

        public void Dispose()
        {
            DisposeCount++;
            _inner.Dispose();
        }
    }
}
