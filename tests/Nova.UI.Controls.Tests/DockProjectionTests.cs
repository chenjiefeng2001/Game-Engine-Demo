using Avalonia.Controls;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class DockProjectionTests
{
    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId LeftGroup = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");

    [TestMethod]
    public void Projection_maps_split_orientation_order_and_weights()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);

        var result = projector.Apply(tree);

        Assert.AreEqual(DockApplyStatus.Applied, result.Status);
        Assert.IsTrue(result.IsCurrent);
        Assert.AreEqual(4, factory.CallCount);

        var main = RequireNode<Grid>(projector, MainSplit);
        Assert.AreEqual(2, main.ColumnDefinitions.Count);
        Assert.AreEqual(GridUnitType.Star, main.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.28, main.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(0.72, main.ColumnDefinitions[1].Width.Value, 1e-9);
        Assert.AreEqual(0, Grid.GetColumn(main.Children[0]));
        Assert.AreEqual(1, Grid.GetColumn(main.Children[1]));

        var center = RequireNode<Grid>(projector, CenterSplit);
        Assert.AreEqual(2, center.RowDefinitions.Count);
        Assert.AreEqual(0.75, center.RowDefinitions[0].Height.Value, 1e-9);
        Assert.AreEqual(0.25, center.RowDefinitions[1].Height.Value, 1e-9);
        Assert.AreEqual(0, Grid.GetRow(center.Children[0]));
        Assert.AreEqual(1, Grid.GetRow(center.Children[1]));

        Assert.IsNotNull(projector.FindNode(LeftGroup) as TabControl);
        Assert.IsNotNull(projector.FindNode(SceneGroup) as TabControl);
        Assert.IsNotNull(projector.FindNode(BottomGroup) as TabControl);

        var sceneView = FindFakePanel(projector, DockPanelIds.Scene);
        var resized = Require(tree.ResizeSplit(MainSplit, 1, 3));
        var resizeResult = projector.Apply(resized);

        Assert.AreEqual(DockApplyStatus.Applied, resizeResult.Status);
        main = RequireNode<Grid>(projector, MainSplit);
        Assert.AreEqual(0.25, main.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(0.75, main.ColumnDefinitions[1].Width.Value, 1e-9);
        Assert.AreEqual(4, factory.CallCount);
        Assert.AreSame(sceneView, projector.FindPanel(DockPanelIds.Scene));
    }

    [TestMethod]
    public void Projection_syncs_tab_order_and_active_panel_without_recreating_views()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);
        projector.Apply(tree);

        var inspector = projector.FindPanel(DockPanelIds.Inspector);
        var console = projector.FindPanel(DockPanelIds.Console);
        Assert.IsNotNull(inspector);
        Assert.IsNotNull(console);

        var tabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(2, tabs.Items.Count);
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(tabs, 0));
        Assert.AreEqual(DockPanelIds.Console, TagAt(tabs, 1));
        Assert.AreEqual(0, tabs.SelectedIndex);

        var activated = Require(tree.ActivateTab(BottomGroup, DockPanelIds.Console));
        var applied = projector.Apply(activated);

        Assert.AreEqual(DockApplyStatus.Applied, applied.Status);
        tabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(1, tabs.SelectedIndex);
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(4, factory.CallCount);

        var reordered = Require(
            activated.ReorderTab(DockPanelIds.Console, BottomGroup, 0));
        projector.Apply(reordered);

        tabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(DockPanelIds.Console, TagAt(tabs, 0));
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(tabs, 1));
        Assert.AreEqual(0, tabs.SelectedIndex);
        Assert.AreSame(console, projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(4, factory.CallCount);
    }

    [TestMethod]
    public void Projection_distinguishes_panels_with_the_same_title()
    {
        var factory = new RecordingFactory(
            instance => new FakePanelView("Same title", new Border()));
        using var projector = new DockWorkspaceProjector(factory);
        var tree = DockTree.CreateDefault();

        projector.Apply(tree);

        var tabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(2, tabs.Items.Count);
        Assert.AreEqual("Same title", ((TabItem)tabs.Items[0]!).Header);
        Assert.AreEqual("Same title", ((TabItem)tabs.Items[1]!).Header);
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(tabs, 0));
        Assert.AreEqual(DockPanelIds.Console, TagAt(tabs, 1));

        var inspector = FindFakePanel(projector, DockPanelIds.Inspector);
        var console = FindFakePanel(projector, DockPanelIds.Console);
        Assert.AreNotSame(inspector, console);

        var removed = Require(tree.RemovePanel(DockPanelIds.Inspector));
        projector.Apply(removed);

        Assert.AreEqual(1, inspector.DisposeCount);
        Assert.AreEqual(0, console.DisposeCount);
        Assert.IsNull(projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(DockPanelIds.Console, TagAt(RequireNode<TabControl>(projector, BottomGroup), 0));
    }

    [TestMethod]
    public void Repeated_projection_is_idempotent_and_does_not_call_factory()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);

        var first = projector.Apply(tree);
        var main = RequireNode<Grid>(projector, MainSplit);
        var inspector = projector.FindPanel(DockPanelIds.Inspector);

        var second = projector.Apply(tree);

        Assert.AreEqual(DockApplyStatus.Unchanged, second.Status);
        Assert.AreEqual(first.Generation, second.Generation);
        Assert.AreEqual(first.CommittedDigest, second.CommittedDigest);
        Assert.AreSame(main, projector.FindNode(MainSplit));
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreEqual(4, factory.CallCount);
    }

    [TestMethod]
    public void Removing_panels_releases_only_their_views_and_collapses_nodes()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);
        projector.Apply(tree);

        var console = FindFakePanel(projector, DockPanelIds.Console);
        var inspector = FindFakePanel(projector, DockPanelIds.Inspector);

        var withoutConsole = Require(tree.RemovePanel(DockPanelIds.Console));
        projector.Apply(withoutConsole);

        Assert.AreEqual(1, console.DisposeCount);
        Assert.AreEqual(0, inspector.DisposeCount);
        Assert.IsNull(projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(1, RequireNode<TabControl>(projector, BottomGroup).Items.Count);

        var withoutBoth = Require(withoutConsole.RemovePanel(DockPanelIds.Inspector));
        projector.Apply(withoutBoth);

        Assert.AreEqual(1, inspector.DisposeCount);
        Assert.IsNull(projector.FindNode(BottomGroup));
        Assert.IsNull(projector.FindNode(CenterSplit));
        Assert.AreEqual(4, factory.CallCount);

        var repeated = projector.Apply(withoutBoth);
        Assert.AreEqual(DockApplyStatus.Unchanged, repeated.Status);
        Assert.AreEqual(1, console.DisposeCount);
        Assert.AreEqual(1, inspector.DisposeCount);
    }

    [TestMethod]
    public void Moving_a_panel_between_groups_preserves_its_view()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);
        projector.Apply(tree);

        var inspector = FindFakePanel(projector, DockPanelIds.Inspector);
        var scene = FindFakePanel(projector, DockPanelIds.Scene);
        var console = FindFakePanel(projector, DockPanelIds.Console);

        var moved = Require(tree.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(SceneGroup)));
        projector.Apply(moved);

        Assert.AreEqual(4, factory.CallCount);
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(scene, projector.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(console, projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(0, inspector.DisposeCount);

        var sceneTabs = RequireNode<TabControl>(projector, SceneGroup);
        Assert.AreEqual(2, sceneTabs.Items.Count);
        Assert.AreEqual(DockPanelIds.Scene, TagAt(sceneTabs, 0));
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(sceneTabs, 1));
        Assert.AreEqual(1, sceneTabs.SelectedIndex);

        var bottomTabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(1, bottomTabs.Items.Count);
        Assert.AreEqual(DockPanelIds.Console, TagAt(bottomTabs, 0));

        var removed = Require(moved.RemovePanel(DockPanelIds.Inspector));
        projector.Apply(removed);

        Assert.AreEqual(1, inspector.DisposeCount);
        Assert.AreEqual(0, scene.DisposeCount);
        Assert.AreEqual(0, console.DisposeCount);
    }

    [TestMethod]
    public void Failed_model_operation_does_not_touch_projection()
    {
        var tree = DockTree.CreateDefault();
        using var projector = CreateProjector(out var factory);
        var applied = projector.Apply(tree);
        var main = RequireNode<Grid>(projector, MainSplit);
        var inspector = projector.FindPanel(DockPanelIds.Inspector);

        var failed = tree.ActivateTab(BottomGroup, DockPanelIds.Hierarchy);
        Assert.IsFalse(failed.Success);
        Assert.IsNull(failed.Tree);
        Assert.AreEqual(DockError.InvalidSource, failed.Error);

        var repeated = projector.Apply(tree);

        Assert.AreEqual(DockApplyStatus.Unchanged, repeated.Status);
        Assert.AreEqual(applied.Generation, repeated.Generation);
        Assert.AreEqual(applied.CommittedDigest, repeated.CommittedDigest);
        Assert.AreSame(main, projector.FindNode(MainSplit));
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreEqual(4, factory.CallCount);
        Assert.AreEqual(tree.Digest, repeated.CommittedDigest);
    }

    [TestMethod]
    public void Visual_apply_failure_rebuilds_once_with_a_new_generation()
    {
        var interceptor = new TestInterceptor
        {
            FailNode = BottomGroup,
            FailuresRemaining = 1,
        };
        using var projector = CreateProjector(out var factory, interceptor);

        var result = projector.Apply(DockTree.CreateDefault());

        Assert.AreEqual(DockApplyStatus.Applied, result.Status);
        Assert.IsTrue(result.IsCurrent);
        Assert.AreEqual(2, result.Generation);
        Assert.AreEqual(4, factory.CallCount);
        Assert.AreEqual(2, interceptor.Tokens.Select(token => token.Generation).Distinct().Count());
        Assert.IsNotNull(projector.FindNode(MainSplit));
        Assert.IsNotNull(projector.FindNode(BottomGroup));
        Assert.AreEqual(2, RequireNode<TabControl>(projector, BottomGroup).Items.Count);
        Assert.IsFalse(((ContentControl)projector.Root).Content is TextBlock { Name: "DockProjectionDegraded" });
    }

    [TestMethod]
    public void Persistent_visual_failure_degrades_and_retries_the_latest_tree()
    {
        var treeA = DockTree.CreateDefault();
        var interceptor = new TestInterceptor { FailNode = BottomGroup };
        using var projector = CreateProjector(out var factory, interceptor);

        var healthy = projector.Apply(treeA);
        var inspector = projector.FindPanel(DockPanelIds.Inspector);
        var console = projector.FindPanel(DockPanelIds.Console);
        Assert.AreEqual(DockApplyStatus.Applied, healthy.Status);
        Assert.IsNotNull(inspector);
        Assert.IsNotNull(console);

        var treeB = Require(treeA.ActivateTab(BottomGroup, DockPanelIds.Console));
        interceptor.AlwaysFail = true;

        var degraded = projector.Apply(treeB);

        Assert.AreEqual(DockApplyStatus.Degraded, degraded.Status);
        Assert.IsFalse(degraded.IsHealthy);
        Assert.IsFalse(degraded.IsCurrent);
        Assert.AreEqual(healthy.Generation + 2, degraded.Generation);
        Assert.AreEqual(healthy.AppliedDigest, degraded.AppliedDigest);
        Assert.IsFalse(string.Equals(degraded.AppliedDigest, treeB.Digest, StringComparison.Ordinal));
        Assert.IsNull(projector.FindNode(MainSplit));
        var placeholder = ((ContentControl)projector.Root).Content as TextBlock;
        Assert.IsNotNull(placeholder);
        Assert.AreEqual("DockProjectionDegraded", placeholder.Name);
        Assert.AreEqual(4, factory.CallCount);

        var repeated = projector.Apply(treeB);
        Assert.AreEqual(DockApplyStatus.Degraded, repeated.Status);
        Assert.AreEqual(degraded.Generation, repeated.Generation);
        Assert.AreEqual(4, factory.CallCount);

        interceptor.AlwaysFail = false;
        var retried = projector.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retried.Status);
        Assert.IsTrue(retried.IsCurrent);
        Assert.AreEqual(treeB.Digest, retried.CommittedDigest);
        Assert.AreEqual(treeB.Digest, retried.AppliedDigest);
        Assert.AreEqual(degraded.Generation + 1, retried.Generation);
        Assert.AreEqual(4, factory.CallCount);
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, projector.FindPanel(DockPanelIds.Console));

        var bottomTabs = RequireNode<TabControl>(projector, BottomGroup);
        Assert.AreEqual(1, bottomTabs.SelectedIndex);
    }

    [TestMethod]
    public void Panel_factory_failure_is_panel_level_and_retry_only_recreates_the_failure()
    {
        var failConsole = true;
        var factory = new RecordingFactory(instance =>
        {
            if (instance == DockPanelIds.Console && failConsole)
                throw new InvalidOperationException("console unavailable");
            return new FakePanelView($"View {instance}", new Border());
        });
        using var projector = new DockWorkspaceProjector(factory);
        var tree = DockTree.CreateDefault();

        var applied = projector.Apply(tree);

        Assert.AreEqual(DockApplyStatus.Applied, applied.Status);
        Assert.IsTrue(applied.IsCurrent);
        CollectionAssert.Contains(applied.PanelFailures.ToList(), DockPanelIds.Console);
        Assert.AreEqual(4, factory.CallCount);
        Assert.IsNull(projector.FindPanel(DockPanelIds.Console));
        var inspector = projector.FindPanel(DockPanelIds.Inspector);
        var scene = projector.FindPanel(DockPanelIds.Scene);
        Assert.IsNotNull(inspector);
        Assert.IsNotNull(scene);
        Assert.AreEqual("editor.console", FindTabItem(projector, BottomGroup, DockPanelIds.Console).Header);

        var repeated = projector.Apply(tree);
        Assert.AreEqual(DockApplyStatus.Unchanged, repeated.Status);
        CollectionAssert.Contains(repeated.PanelFailures.ToList(), DockPanelIds.Console);
        Assert.AreEqual(4, factory.CallCount);

        failConsole = false;
        var retried = projector.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retried.Status);
        Assert.IsTrue(retried.IsCurrent);
        Assert.AreEqual(2, retried.Generation);
        Assert.AreEqual(0, retried.PanelFailures.Count);
        Assert.AreEqual(5, factory.CallCount);
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(scene, projector.FindPanel(DockPanelIds.Scene));
        Assert.IsNotNull(projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(
            "View editor.console",
            FindTabItem(projector, BottomGroup, DockPanelIds.Console).Header);
    }

    [TestMethod]
    public void Panel_factory_retry_failure_stays_panel_level_and_keeps_healthy_views()
    {
        var factory = new RecordingFactory(instance =>
        {
            if (instance == DockPanelIds.Console)
                throw new InvalidOperationException("console unavailable");
            return new FakePanelView($"View {instance}", new Border());
        });
        using var projector = new DockWorkspaceProjector(factory);
        var tree = DockTree.CreateDefault();

        var applied = projector.Apply(tree);
        var inspector = projector.FindPanel(DockPanelIds.Inspector);
        Assert.IsNotNull(inspector);
        Assert.AreEqual(4, factory.CallCount);

        var retried = projector.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retried.Status);
        Assert.IsTrue(retried.IsCurrent);
        Assert.AreEqual(1, retried.PanelFailures.Count);
        CollectionAssert.Contains(retried.PanelFailures.ToList(), DockPanelIds.Console);
        Assert.AreEqual(5, factory.CallCount);
        Assert.AreSame(inspector, projector.FindPanel(DockPanelIds.Inspector));
        Assert.IsNull(projector.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(
            "editor.console",
            FindTabItem(projector, BottomGroup, DockPanelIds.Console).Header);
    }

    [TestMethod]
    public void Dispose_releases_all_views_once_and_rejects_further_work()
    {
        var tree = DockTree.CreateDefault();
        var factory = new RecordingFactory(
            instance => new FakePanelView($"View {instance}", new Border()));
        var projector = new DockWorkspaceProjector(factory);
        projector.Apply(tree);

        var views = new[]
        {
            FindFakePanel(projector, DockPanelIds.Hierarchy),
            FindFakePanel(projector, DockPanelIds.Scene),
            FindFakePanel(projector, DockPanelIds.Inspector),
            FindFakePanel(projector, DockPanelIds.Console),
        };

        projector.Dispose();

        Assert.IsTrue(projector.IsDisposed);
        Assert.IsNull(((ContentControl)projector.Root).Content);
        foreach (var view in views)
            Assert.AreEqual(1, view.DisposeCount);
        Assert.IsNull(projector.FindPanel(DockPanelIds.Inspector));

        projector.Dispose();
        foreach (var view in views)
            Assert.AreEqual(1, view.DisposeCount);

        Assert.ThrowsException<ObjectDisposedException>(() => projector.Apply(tree));
        Assert.ThrowsException<ObjectDisposedException>(() => projector.Retry());
    }

    private static DockWorkspaceProjector CreateProjector(
        out RecordingFactory factory,
        IDockNodeApplyInterceptor? interceptor = null)
    {
        factory = new RecordingFactory(
            instance => new FakePanelView($"View {instance}", new Border()));
        return new DockWorkspaceProjector(factory, interceptor);
    }

    private static DockTree Require(DockOperationResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Tree);
        return result.Tree!;
    }

    private static T RequireNode<T>(DockWorkspaceProjector projector, DockNodeId id)
        where T : Control
    {
        var control = projector.FindNode(id);
        Assert.IsNotNull(control, $"Node '{id}' is not mounted.");
        return (T)control;
    }

    private static FakePanelView FindFakePanel(
        DockWorkspaceProjector projector,
        PanelInstanceId instance)
    {
        var view = projector.FindPanel(instance);
        Assert.IsNotNull(view, $"Panel '{instance}' has no view.");
        return (FakePanelView)view;
    }

    private static TabItem FindTabItem(
        DockWorkspaceProjector projector,
        DockNodeId groupId,
        PanelInstanceId instance)
    {
        var tabs = RequireNode<TabControl>(projector, groupId);
        foreach (var candidate in tabs.Items)
        {
            if (candidate is TabItem item
                && item.Tag is PanelInstanceId tagged
                && tagged == instance)
            {
                return item;
            }
        }

        Assert.Fail($"Tab '{instance}' was not found in group '{groupId}'.");
        return null!;
    }

    private static PanelInstanceId TagAt(TabControl tabs, int index)
    {
        var item = (TabItem)tabs.Items[index]!;
        Assert.IsNotNull(item.Tag);
        return (PanelInstanceId)item.Tag!;
    }

    private sealed class RecordingFactory(Func<PanelInstanceId, IDockPanelView> create)
        : IDockPanelViewFactory
    {
        public int CallCount { get; private set; }

        public bool CanCreate(PanelInstanceId instance) => true;

        public IDockPanelView Create(PanelInstanceId instance)
        {
            CallCount++;
            return create(instance);
        }
    }

    private sealed class FakePanelView(string title, Control content) : IDockPanelView
    {
        public string Title { get; } = title;

        public Control Content { get; } = content;

        public int DisposeCount { get; private set; }

        public void Dispose() => DisposeCount++;
    }

    private sealed class TestInterceptor : IDockNodeApplyInterceptor
    {
        public DockNodeId? FailNode { get; init; }

        public int FailuresRemaining { get; set; }

        public bool AlwaysFail { get; set; }

        public List<DockApplyToken> Tokens { get; } = [];

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            Tokens.Add(token);

            if (FailNode is not { } failNode || node != failNode)
                return;

            if (!AlwaysFail && FailuresRemaining <= 0)
                return;

            if (!AlwaysFail)
                FailuresRemaining--;

            throw new InvalidOperationException($"Injected visual failure at '{node}'.");
        }
    }
}
