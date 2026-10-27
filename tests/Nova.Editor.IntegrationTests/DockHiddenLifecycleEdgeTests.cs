using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D8-E headless gates for shell reopen (scenario 7) and combined failures
/// (scenario 8): the hidden and unresolved lifecycles must survive a shell
/// reopen without double releases, and a panel factory failure must stay
/// isolated from the unresolved persistence semantics.
/// </summary>
[TestClass]
public sealed class DockHiddenLifecycleEdgeTests
{
    private static readonly DockNodeId LeftGroup = new("dock.tabs.left");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly PanelInstanceId UnknownScene = new("third.party.scene");
    private static readonly PanelInstanceId UnknownChat = new("third.party.chat");

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d8e-headless", Guid.NewGuid().ToString("N"));
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
    public async Task Closing_and_reopening_the_shell_keeps_hidden_panels_without_double_releases()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var firstViewModel = new EditorShellViewModel(workspace);
        var firstFactory = new RecordingFactory(new EditorDockPanelFactory(firstViewModel));
        var firstShell = new EditorShellView(firstViewModel, null, firstFactory);
        RecordingView console;
        try
        {
            await firstShell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(firstShell.DockWorkspace);
            console = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));
            Assert.AreEqual(4, firstFactory.Calls.Count);

            // The shell's auto-save debounce is the production write path for
            // a hide; wait for its completion instead of racing it with an
            // explicit save that the dedupe gate could skip.
            var autosaved = new TaskCompletionSource<LayoutSaveResult>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            void OnAutosaved(object? sender, LayoutSaveCompletedEventArgs e)
                => autosaved.TrySetResult(e.Result);
            workspace.LayoutSaveCompleted += OnAutosaved;

            Assert.IsTrue(dock.HidePanel(DockPanelIds.Console).Success);
            Assert.AreEqual(1, console.DisposeCount);
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Inspector },
                dock.Workspace.MainRoot.FindGroup(BottomGroup)!.VisiblePanels.ToArray());

            var saved = await autosaved.Task.WaitAsync(TimeSpan.FromSeconds(10));
            workspace.LayoutSaveCompleted -= OnAutosaved;
            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);

            var persisted = WorkspaceLayoutSerializer.Deserialize(
                await File.ReadAllTextAsync(_layoutPath));
            Assert.IsFalse(persisted.UsedFallback, persisted.Reason);
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Console },
                persisted.DockWorkspace!.EnumerateHiddenPanels().ToArray());

            // Closing the shell releases its workspace; the already-released
            // hidden view must not be touched again.
            dock.Dispose();
            Assert.AreEqual(1, console.DisposeCount);
            Assert.ThrowsException<ObjectDisposedException>(
                () => dock.ActivateTab(BottomGroup, DockPanelIds.Inspector));
        }
        finally
        {
            if (firstShell.DockWorkspace is { IsDisposed: false } undisposed)
                undisposed.Dispose();
        }

        // Reopening the shell on the same workspace restores the hidden panel
        // without creating a view for it and without touching old views.
        var secondViewModel = new EditorShellViewModel(workspace);
        var secondFactory = new RecordingFactory(new EditorDockPanelFactory(secondViewModel));
        var secondShell = new EditorShellView(secondViewModel, null, secondFactory);
        try
        {
            await secondShell.InitializeAsync();
            var reopened = Require<EditorDockWorkspace>(secondShell.DockWorkspace);

            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Console },
                reopened.HiddenPanels.ToArray());
            CollectionAssert.AreEquivalent(
                new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene, DockPanelIds.Inspector },
                secondFactory.Calls.ToArray());
            Assert.AreEqual(
                1,
                console.DisposeCount,
                "The reopened shell must not release the previous view again.");

            var group = reopened.Workspace.MainRoot.FindGroup(BottomGroup)!;
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Inspector },
                group.VisiblePanels.ToArray());
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Console },
                group.HiddenPanels.ToArray());

            var shown = reopened.ShowPanel(DockPanelIds.Console);
            Assert.IsTrue(shown.Success, shown.Diagnostic);
            Assert.AreEqual(4, secondFactory.Calls.Count);
            CollectionAssert.Contains(secondFactory.Calls, DockPanelIds.Console);
            var recreated = Require<RecordingView>(reopened.FindPanel(DockPanelIds.Console));
            Assert.AreNotSame(console, recreated);
            Assert.AreEqual(0, recreated.DisposeCount);
            Assert.AreEqual(1, console.DisposeCount);
        }
        finally
        {
            secondShell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task Factory_failure_and_unresolved_panels_stay_isolated_and_save_faithfully()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var factory = new FailingFactory(
            new EditorDockPanelFactory(viewModel),
            DockPanelIds.Hierarchy);
        var shell = new EditorShellView(viewModel, null, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);

            // The failed hierarchy panel is owned by the registry as a panel
            // failure; the workspace itself stays interpretable and the
            // unresolved/hidden semantics are untouched.
            Assert.IsNull(dock.RestoreFailure);
            Assert.IsFalse(dock.IsDegraded);
            Assert.IsFalse(
                Require<Border>(shell.FindControl<Border>("DockDegradedBanner")).IsVisible);
            CollectionAssert.AreEquivalent(
                new[] { UnknownScene, UnknownChat },
                dock.UnresolvedPanels.ToArray());
            CollectionAssert.AreEqual(
                new[] { UnknownChat },
                dock.HiddenPanels.ToArray());
            Assert.IsTrue(workspace.IsPersistenceSuppressed);
            RequireFailurePlaceholder(dock, LeftGroup, DockPanelIds.Hierarchy);
            CollectionAssert.AreEquivalent(
                new[] { DockPanelIds.Hierarchy, DockPanelIds.Inspector },
                factory.Calls.ToArray());
            Assert.IsNull(
                dock.FindPanel(DockPanelIds.Hierarchy),
                "A failed panel must not expose a view until it is retried.");
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Inspector));

            // The explicit save writes the unknown identities faithfully and
            // clears the suppression even while a panel view keeps failing.
            var saved = await workspace.SaveLayoutAsync();
            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
            Assert.IsFalse(workspace.IsPersistenceSuppressed);

            var raw = await File.ReadAllTextAsync(_layoutPath);
            StringAssert.Contains(raw, "third.party.scene");
            StringAssert.Contains(raw, "third.party.chat");

            // A retry with the factory fixed recovers only the failed panel.
            factory.FailNext = false;
            var retried = dock.Retry();
            Assert.IsTrue(retried.IsHealthy, retried.Diagnostic);
            Assert.IsFalse(dock.IsDegraded);
            Assert.IsNotNull(dock.FindPanel(DockPanelIds.Hierarchy));
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

    private static void RequirePlaceholder(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var host = Require<ContentControl>(RequireTab(dock, groupId, panel).Content);
        var placeholder = Require<TextBlock>(host.Content);
        Assert.AreEqual("DockPanelUnavailable", placeholder.Name);
        StringAssert.Contains(placeholder.Text ?? string.Empty, panel.ToString());
    }

    private static void RequireFailurePlaceholder(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var host = Require<ContentControl>(RequireTab(dock, groupId, panel).Content);
        var placeholder = Require<TextBlock>(host.Content);
        Assert.AreEqual("DockPanelFailure", placeholder.Name);
        StringAssert.Contains(placeholder.Text ?? string.Empty, panel.ToString());
    }

    private static TabItem RequireTab(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var tabs = Require<TabControl>(dock.FindNode(groupId));
        return tabs.Items.OfType<TabItem>().Single(
            candidate => candidate.Tag is PanelInstanceId tag && tag == panel);
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
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

    private sealed class FailingFactory : IDockPanelViewFactory
    {
        private readonly IDockPanelViewFactory _inner;

        public FailingFactory(IDockPanelViewFactory inner, PanelInstanceId failing)
        {
            _inner = inner;
            FailingPanel = failing;
        }

        public PanelInstanceId FailingPanel { get; }

        public bool FailNext { get; set; } = true;

        public List<PanelInstanceId> Calls { get; } = [];

        public bool CanCreate(PanelInstanceId instance) => _inner.CanCreate(instance);

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls.Add(instance);
            if (instance == FailingPanel && FailNext)
                throw new InvalidOperationException("hierarchy unavailable");
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
