using Avalonia.Controls;
using Avalonia.Interactivity;
using Nova.Editor.Services;
using Nova.Editor.Views;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Controls;
using Nova.UI.Core.Docking;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class DockShellIntegrationTests
{
    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");

    [TestMethod]
    public async Task Editor_shell_hosts_the_real_dock_projection()
    {
        var viewModel = CreateViewModel();
        var shell = new EditorShellView(viewModel);
        try
        {
            Assert.IsNull(shell.DockWorkspace);
            Assert.IsNull(shell.FindControl<ContentControl>("DockRoot")!.Content);

            await shell.InitializeAsync();
            var dock = shell.DockWorkspace;
            Assert.IsNotNull(dock);
            Assert.IsNotNull(dock.Current);
            Assert.AreEqual(DockApplyStatus.Applied, dock.Current.Status);
            Assert.IsTrue(dock.Current.IsCurrent);

            var host = shell.FindControl<ContentControl>("DockRoot");
            Assert.IsNotNull(host);
            Assert.AreSame(dock.Root, host.Content);

            var banner = Require<Border>(shell.FindControl<Border>("DockDegradedBanner"));
            Assert.IsFalse(banner.IsVisible);

            Assert.IsInstanceOfType(
                dock.FindPanel(DockPanelIds.Hierarchy), typeof(HierarchyPanelView));
            Assert.IsInstanceOfType(
                dock.FindPanel(DockPanelIds.Scene), typeof(ScenePanelView));
            Assert.IsInstanceOfType(
                dock.FindPanel(DockPanelIds.Inspector), typeof(InspectorPanelView));
            Assert.IsInstanceOfType(
                dock.FindPanel(DockPanelIds.Console), typeof(ConsolePanelView));

            var bottom = Require<TabControl>(dock.FindNode(BottomGroup));
            Assert.AreEqual(2, bottom.Items.Count);
            Assert.AreEqual("Inspector", HeaderAt(bottom, 0));
            Assert.AreEqual("Console", HeaderAt(bottom, 1));
            Assert.AreEqual(0, bottom.SelectedIndex);
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public void Default_dock_tree_maps_to_real_splits_and_tabs()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var main = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(2, main.ColumnDefinitions.Count);
        Assert.AreEqual(0.28d, main.ColumnDefinitions[0].Width.Value, 0.001);
        Assert.AreEqual(0.72d, main.ColumnDefinitions[1].Width.Value, 0.001);

        var center = Require<Grid>(dock.FindNode(CenterSplit));
        Assert.AreEqual(2, center.RowDefinitions.Count);
        Assert.AreEqual(0.75d, center.RowDefinitions[0].Height.Value, 0.001);
        Assert.AreEqual(0.25d, center.RowDefinitions[1].Height.Value, 0.001);

        var hierarchy = Require<TabControl>(dock.FindNode(new DockNodeId("dock.tabs.left")));
        Assert.AreEqual("Hierarchy", HeaderAt(hierarchy, 0));
        Assert.AreEqual(0, hierarchy.SelectedIndex);

        var scene = Require<TabControl>(dock.FindNode(new DockNodeId("dock.tabs.scene")));
        Assert.AreEqual("Scene", HeaderAt(scene, 0));

        Assert.AreEqual(4, factory.Calls.Count);
        Assert.AreEqual(4, factory.Views.Count);
        CollectionAssert.AreEquivalent(
            new[]
            {
                DockPanelIds.Hierarchy,
                DockPanelIds.Scene,
                DockPanelIds.Inspector,
                DockPanelIds.Console,
            },
            factory.Calls.ToArray());
    }

    [TestMethod]
    public void Repeated_apply_is_idempotent_and_reuses_real_views()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var views = new Dictionary<PanelInstanceId, IDockPanelView>
        {
            [DockPanelIds.Hierarchy] = Require<IDockPanelView>(
                dock.FindPanel(DockPanelIds.Hierarchy)),
            [DockPanelIds.Scene] = Require<IDockPanelView>(
                dock.FindPanel(DockPanelIds.Scene)),
            [DockPanelIds.Inspector] = Require<IDockPanelView>(
                dock.FindPanel(DockPanelIds.Inspector)),
            [DockPanelIds.Console] = Require<IDockPanelView>(
                dock.FindPanel(DockPanelIds.Console)),
        };
        var generation = dock.Current!.Generation;

        var result = dock.Apply(dock.Tree);

        Assert.AreEqual(DockApplyStatus.Unchanged, result.Status);
        Assert.AreEqual(generation, result.Generation);
        Assert.AreEqual(4, factory.Calls.Count);
        foreach (var (panel, view) in views)
            Assert.AreSame(view, dock.FindPanel(panel), panel.ToString());
    }

    [TestMethod]
    public void Failed_model_operation_leaves_the_visual_tree_untouched()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var hierarchy = Require<RecordingView>(dock.FindPanel(DockPanelIds.Hierarchy));

        var failed = dock.Tree.ActivateTab(
            BottomGroup, DockPanelIds.Scene);

        Assert.IsFalse(failed.Success);
        Assert.IsNull(failed.Tree);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreSame(hierarchy, dock.FindPanel(DockPanelIds.Hierarchy));
        Assert.AreEqual(4, factory.Calls.Count);

        var reapplied = dock.Apply(dock.Tree);
        Assert.AreEqual(DockApplyStatus.Unchanged, reapplied.Status);
        Assert.AreEqual(generation, reapplied.Generation);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Moving_a_panel_preserves_its_view_and_removal_releases_once()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var hierarchy = Require<RecordingView>(dock.FindPanel(DockPanelIds.Hierarchy));
        var console = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));

        var moved = dock.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Center(new DockNodeId("dock.tabs.scene")));

        Assert.IsTrue(moved.Success, moved.Diagnostic);
        Assert.AreEqual(4, factory.Calls.Count);
        Assert.AreSame(console, dock.FindPanel(DockPanelIds.Console));
        Assert.AreSame(hierarchy, dock.FindPanel(DockPanelIds.Hierarchy));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene, DockPanelIds.Console },
            dock.Tree.FindGroup(new DockNodeId("dock.tabs.scene"))!.Panels.ToArray());

        var removed = dock.RemovePanel(DockPanelIds.Console);

        Assert.IsTrue(removed.Success, removed.Diagnostic);
        Assert.AreEqual(1, console.DisposeCount);
        Assert.AreEqual(4, factory.Calls.Count);
        Assert.IsNull(dock.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(0, hierarchy.DisposeCount);
    }

    [TestMethod]
    public void Visual_failure_rebuilds_once_and_persistent_failure_degrades()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var once = new TestInterceptor { FailNode = CenterSplit, FailuresRemaining = 1 };
        using (var recovered = new EditorDockWorkspace(viewModel, once, factory))
        {
            Assert.AreEqual(DockApplyStatus.Applied, recovered.Current!.Status);
            Assert.IsTrue(recovered.Current.IsCurrent);
            Assert.AreEqual(2, recovered.Current.Generation);
            Assert.AreEqual(4, factory.Calls.Count);
            Assert.AreEqual(0, recovered.Current.PanelFailures.Count);
        }

        var persistent = new TestInterceptor { FailNode = CenterSplit, AlwaysFail = true };
        var persistentFactory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var degraded = new EditorDockWorkspace(
            viewModel, persistent, persistentFactory);

        Assert.AreEqual(DockApplyStatus.Degraded, degraded.Current!.Status);
        Assert.IsFalse(degraded.Current.IsCurrent);
        Assert.AreEqual(2, degraded.Current.Generation);
        Assert.AreEqual(0, persistentFactory.Calls.Count);
        Assert.AreEqual(
            "DockProjectionDegraded",
            ((TextBlock)((ContentControl)degraded.Root).Content!).Name);

        var repeat = degraded.Apply(degraded.Tree);
        Assert.AreEqual(DockApplyStatus.Degraded, repeat.Status);
        Assert.AreEqual(2, repeat.Generation);
        Assert.AreEqual(0, persistentFactory.Calls.Count);

        persistent.AlwaysFail = false;
        var retry = degraded.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.AreEqual(3, retry.Generation);
        Assert.AreEqual(4, persistentFactory.Calls.Count);
        Assert.AreEqual(0, retry.PanelFailures.Count);
    }

    [TestMethod]
    public void Current_tab_activation_updates_the_model_and_visuals()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var bottom = Require<TabControl>(dock.FindNode(BottomGroup));
        var inspector = Require<RecordingView>(dock.FindPanel(DockPanelIds.Inspector));
        var console = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));

        bottom.SelectedIndex = 1;

        var group = dock.Tree.FindGroup(BottomGroup);
        Assert.IsNotNull(group);
        Assert.AreEqual(DockPanelIds.Console, group.ActivePanel);
        Assert.IsTrue(dock.Current!.IsCurrent);
        Assert.AreEqual(4, factory.Calls.Count);
        Assert.AreSame(inspector, dock.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, dock.FindPanel(DockPanelIds.Console));

        var refreshed = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.AreNotSame(bottom, refreshed);
        Assert.AreEqual(1, refreshed.SelectedIndex);
    }

    [TestMethod]
    public void Panel_retry_refreshes_the_current_tab_token()
    {
        var viewModel = CreateViewModel();
        var factory = new FlakyFactory(new EditorDockPanelFactory(viewModel))
        {
            FailOnce = DockPanelIds.Console,
        };
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        Assert.AreEqual(DockApplyStatus.Applied, dock.Current!.Status);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console },
            dock.Current.PanelFailures.ToArray());
        var bottom = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.AreEqual(DockPanelIds.Console.ToString(), HeaderAt(bottom, 1));

        var generation = dock.Current.Generation;
        var retry = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.IsTrue(retry.Generation > generation);
        Assert.AreEqual(0, retry.PanelFailures.Count);
        Assert.AreEqual(5, factory.Calls);
        Assert.AreSame(bottom, dock.FindNode(BottomGroup));
        Assert.AreEqual("Console", HeaderAt(bottom, 1));

        bottom.SelectedIndex = 1;

        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.IsTrue(dock.Current!.IsCurrent);
    }

    [TestMethod]
    public void Stale_tab_callbacks_from_previous_generations_are_ignored()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var first = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.IsTrue(dock.ResizeSplit(MainSplit, 1, 3).Success);
        var second = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.IsTrue(dock.ResizeSplit(MainSplit, 3, 1).Success);
        var third = Require<TabControl>(dock.FindNode(BottomGroup));
        var generation = dock.Current!.Generation;

        first.SelectedIndex = 1;
        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);

        second.SelectedIndex = 1;
        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreEqual(generation, dock.Current.Generation);

        third.SelectedIndex = 1;

        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Degraded_state_rejects_callbacks_and_retry_uses_the_latest_tree()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var interceptor = new TestInterceptor();
        using var dock = new EditorDockWorkspace(viewModel, interceptor, factory);

        var stale = Require<TabControl>(dock.FindNode(BottomGroup));
        var healthyGeneration = dock.Current!.Generation;

        interceptor.AlwaysFail = true;
        var committed = dock.ResizeSplit(MainSplit, 1, 3);
        Assert.IsTrue(committed.Success, committed.Diagnostic);
        Assert.AreEqual(DockApplyStatus.Degraded, dock.Current!.Status);
        var degradedGeneration = dock.Current.Generation;

        stale.SelectedIndex = 1;
        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreEqual(degradedGeneration, dock.Current.Generation);

        var repeat = dock.Apply(dock.Tree);
        Assert.AreEqual(DockApplyStatus.Degraded, repeat.Status);
        Assert.AreEqual(degradedGeneration, repeat.Generation);

        interceptor.AlwaysFail = false;
        var retry = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.IsTrue(retry.Generation > degradedGeneration);
        Assert.IsTrue(retry.Generation > healthyGeneration);
        var main = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(0.25d, main.ColumnDefinitions[0].Width.Value, 0.001);
        Assert.AreEqual(0.75d, main.ColumnDefinitions[1].Width.Value, 0.001);

        var current = Require<TabControl>(dock.FindNode(BottomGroup));
        current.SelectedIndex = 1;
        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.IsTrue(dock.Current!.IsCurrent);
    }

    [TestMethod]
    public void Callback_after_dispose_is_ignored()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var tabs = Require<TabControl>(dock.FindNode(BottomGroup));

        dock.Dispose();
        tabs.SelectedIndex = 1;

        Assert.IsTrue(dock.IsDisposed);
        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.ThrowsException<ObjectDisposedException>(
            () => dock.Apply(DockTree.CreateDefault()));
    }

    [TestMethod]
    public async Task Shell_shows_a_degraded_banner_and_retry_recovers()
    {
        var viewModel = CreateViewModel();
        var interceptor = new TestInterceptor { FailNode = CenterSplit, AlwaysFail = true };
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var shell = new EditorShellView(viewModel, interceptor, factory);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var banner = Require<Border>(shell.FindControl<Border>("DockDegradedBanner"));
            var text = Require<TextBlock>(shell.FindControl<TextBlock>("DockDegradedText"));
            var retry = Require<NovaButton>(shell.FindControl<NovaButton>("DockRetryButton"));
            var digest = dock.Tree.Digest;

            Assert.AreEqual(DockApplyStatus.Degraded, dock.Current!.Status);
            Assert.IsTrue(dock.IsDegraded);
            Assert.IsTrue(banner.IsVisible);
            StringAssert.Contains(text.Text, "injected visual failure");
            Assert.IsTrue(retry.IsEnabled);

            var generation = dock.Current.Generation;
            retry.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));

            Assert.AreEqual(DockApplyStatus.Degraded, dock.Current!.Status);
            Assert.IsTrue(banner.IsVisible);
            Assert.IsTrue(retry.IsEnabled);
            Assert.IsTrue(dock.Current.Generation > generation);
            StringAssert.Contains(text.Text, "injected visual failure");

            interceptor.AlwaysFail = false;
            retry.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));

            Assert.AreEqual(DockApplyStatus.Applied, dock.Current!.Status);
            Assert.IsTrue(dock.Current.IsCurrent);
            Assert.AreEqual(digest, dock.Current.CommittedDigest);
            Assert.IsFalse(dock.IsDegraded);
            Assert.IsFalse(banner.IsVisible);
            Assert.AreEqual(4, factory.Calls.Count);
            Assert.IsNotNull(dock.FindNode(MainSplit));
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public void Retry_blocks_layout_mutations_and_reentry_while_in_progress()
    {
        var viewModel = CreateViewModel();
        var interceptor = new TestInterceptor { FailNode = CenterSplit, AlwaysFail = true };
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, interceptor, factory);

        InvalidOperationException? blockedResize = null;
        InvalidOperationException? blockedRetry = null;
        interceptor.Probe = _ =>
        {
            try
            {
                dock.ResizeSplit(MainSplit, 1, 1);
            }
            catch (InvalidOperationException ex)
            {
                blockedResize = ex;
            }

            try
            {
                dock.Retry();
            }
            catch (InvalidOperationException ex)
            {
                blockedRetry = ex;
            }
        };

        interceptor.AlwaysFail = false;
        var retry = dock.Retry();

        Assert.IsNotNull(blockedResize);
        Assert.IsNotNull(blockedRetry);
        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.IsFalse(dock.IsRetrying);
        Assert.AreEqual(dock.Tree.Digest, retry.CommittedDigest);
    }

    private static EditorShellViewModel CreateViewModel()
    {
        var path = Path.Combine(
            Path.GetTempPath(), $"nova-dock-shell-{Guid.NewGuid():N}.json");
        return new EditorShellViewModel(EditorWorkspace.CreateSample(path));
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
    }

    private static string? HeaderAt(TabControl tabs, int index)
        => (tabs.Items[index] as TabItem)?.Header as string;

    private sealed class RecordingFactory : IDockPanelViewFactory
    {
        private readonly IDockPanelViewFactory _inner;

        public RecordingFactory(IDockPanelViewFactory inner) => _inner = inner;

        public List<PanelInstanceId> Calls { get; } = [];
        public List<RecordingView> Views { get; } = [];

        public bool CanCreate(PanelInstanceId instance) => _inner.CanCreate(instance);

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls.Add(instance);
            var view = new RecordingView(_inner.Create(instance));
            Views.Add(view);
            return view;
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

    private sealed class FlakyFactory : IDockPanelViewFactory
    {
        private readonly IDockPanelViewFactory _inner;

        public FlakyFactory(IDockPanelViewFactory inner) => _inner = inner;

        public PanelInstanceId? FailOnce { get; set; }

        public int Calls { get; private set; }

        public bool CanCreate(PanelInstanceId instance) => _inner.CanCreate(instance);

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls++;
            if (FailOnce == instance)
            {
                FailOnce = null;
                throw new InvalidOperationException("injected panel factory failure");
            }

            return _inner.Create(instance);
        }
    }

    private sealed class TestInterceptor : IDockNodeApplyInterceptor
    {
        public DockNodeId? FailNode { get; set; }
        public int FailuresRemaining { get; set; }
        public bool AlwaysFail { get; set; }
        public Action<DockNodeId>? Probe { get; set; }

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            Probe?.Invoke(node);

            if (!AlwaysFail && (FailNode != node || FailuresRemaining <= 0))
                return;

            if (!AlwaysFail)
                FailuresRemaining--;
            throw new InvalidOperationException("injected visual failure");
        }
    }
}
