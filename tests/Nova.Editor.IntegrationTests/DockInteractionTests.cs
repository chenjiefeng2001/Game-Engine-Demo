using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Nova.Editor.Services;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class DockInteractionTests
{
    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");

    [TestMethod]
    public void Splitter_drag_previews_without_commit_and_commits_once_on_release()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        Assert.IsTrue(splitter.IsDragging);

        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));

        Assert.AreEqual(GridUnitType.Pixel, grid.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(380d, grid.ColumnDefinitions[0].Width.Value, 0.001);
        Assert.AreEqual(620d, grid.ColumnDefinitions[1].Width.Value, 0.001);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0.28d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);

        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.IsFalse(splitter.IsDragging);
        Assert.AreEqual(generation + 1, dock.Current.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.AreEqual(0.38d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(0.62d, dock.Tree.FindSplit(MainSplit)!.SecondWeight, 1e-9);
        Assert.AreEqual(dock.Tree.Digest, dock.Current.CommittedDigest);
        Assert.IsTrue(dock.Current.IsCurrent);

        var committed = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreNotSame(grid, committed);
        Assert.AreEqual(GridUnitType.Star, committed.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.38d, committed.ColumnDefinitions[0].Width.Value, 1e-6);
        Assert.AreEqual(0.62d, committed.ColumnDefinitions[1].Width.Value, 1e-6);
        Assert.IsNotNull(SplitterIn(committed));
        Assert.AreSame(splitter, SplitterIn(grid));

        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
        foreach (var view in views.Values)
            Assert.AreEqual(0, ((RecordingView)view).DisposeCount);
    }

    [TestMethod]
    public void Vertical_splitter_uses_row_geometry_and_matches_model_weights()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var grid = Require<Grid>(dock.FindNode(CenterSplit));
        var splitter = SplitterIn(grid);
        Assert.AreEqual(0, Grid.GetRow(splitter));
        Assert.AreEqual(Avalonia.Layout.VerticalAlignment.Bottom, splitter.VerticalAlignment);
        Assert.AreEqual(6d, splitter.Bounds.Height, 0.001);

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(0, 100));

        Assert.AreEqual(700d, grid.RowDefinitions[0].Height.Value, 0.001);
        Assert.AreEqual(100d, grid.RowDefinitions[1].Height.Value, 0.001);
        Assert.AreEqual(0.75d, dock.Tree.FindSplit(CenterSplit)!.FirstWeight, 1e-9);

        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(0.875d, dock.Tree.FindSplit(CenterSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(0.125d, dock.Tree.FindSplit(CenterSplit)!.SecondWeight, 1e-9);
        var committed = Require<Grid>(dock.FindNode(CenterSplit));
        Assert.AreNotSame(grid, committed);
        Assert.AreEqual(0.875d, committed.RowDefinitions[0].Height.Value, 1e-6);
        Assert.AreEqual(0.125d, committed.RowDefinitions[1].Height.Value, 1e-6);
        Assert.IsTrue(dock.Current!.IsCurrent);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Splitter_cancel_restores_the_model_weights_without_commit()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        splitter.CancelDrag();

        Assert.IsFalse(splitter.IsDragging);
        Assert.AreEqual(GridUnitType.Star, grid.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(0.72d, grid.ColumnDefinitions[1].Width.Value, 1e-9);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        Assert.IsFalse(splitter.CompleteDrag());
    }

    [TestMethod]
    public void Splitter_cancels_when_pointer_capture_is_lost()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        LoseCapture(splitter);

        Assert.IsFalse(splitter.IsDragging);
        Assert.AreEqual(GridUnitType.Star, grid.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Splitter_no_op_release_restores_the_visual_without_commit()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.IsFalse(splitter.IsDragging);
        Assert.AreEqual(GridUnitType.Star, grid.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Splitter_rejects_extents_below_the_minimum_pane_size()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid, 50, 800);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(5, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(GridUnitType.Star, grid.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Splitter_clamps_an_extreme_drag_to_the_minimum_pane_width()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(-10_000, 0));

        Assert.AreEqual(32d, grid.ColumnDefinitions[0].Width.Value, 0.001);
        Assert.AreEqual(968d, grid.ColumnDefinitions[1].Width.Value, 0.001);

        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(0.032d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(0.968d, dock.Tree.FindSplit(MainSplit)!.SecondWeight, 1e-9);
        Assert.IsTrue(dock.Current!.IsCurrent);
        var committed = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(0.032d, committed.ColumnDefinitions[0].Width.Value, 1e-6);
    }

    [TestMethod]
    public void Splitter_commit_survives_one_visual_failure_with_a_single_model_update()
    {
        var viewModel = CreateViewModel();
        var interceptor = new TestInterceptor();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, interceptor, factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;

        interceptor.FailNode = CenterSplit;
        interceptor.FailuresRemaining = 1;
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(DockApplyStatus.Applied, dock.Current.Status);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(generation + 2, dock.Current.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.AreEqual(0.38d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(4, factory.Calls.Count);
        var committed = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(0.38d, committed.ColumnDefinitions[0].Width.Value, 1e-6);
        Assert.AreEqual(dock.Tree.Digest, dock.Current.CommittedDigest);
    }

    [TestMethod]
    public void Splitter_commit_degrades_on_persistent_failure_and_retry_restores_weights()
    {
        var viewModel = CreateViewModel();
        var interceptor = new TestInterceptor();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, interceptor, factory);

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var healthyGeneration = dock.Current!.Generation;

        interceptor.AlwaysFail = true;
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(DockApplyStatus.Degraded, dock.Current!.Status);
        Assert.IsFalse(dock.Current.IsCurrent);
        Assert.AreEqual(healthyGeneration + 2, dock.Current.Generation);
        Assert.AreEqual(0.38d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(4, factory.Calls.Count);

        Layout(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(0.38d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(healthyGeneration + 2, dock.Current.Generation);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);

        interceptor.AlwaysFail = false;
        var retry = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.AreEqual(dock.Tree.Digest, retry.CommittedDigest);
        Assert.AreEqual(4, factory.Calls.Count);
        var recovered = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(GridUnitType.Star, recovered.ColumnDefinitions[0].Width.GridUnitType);
        Assert.AreEqual(0.38d, recovered.ColumnDefinitions[0].Width.Value, 1e-6);
        Assert.AreEqual(0.62d, recovered.ColumnDefinitions[1].Width.Value, 1e-6);
    }

    [TestMethod]
    public void Stale_splitter_callbacks_cannot_change_the_committed_model()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var staleSplitter = SplitterIn(grid);
        var resized = dock.ResizeSplit(MainSplit, 1, 3);
        Assert.IsTrue(resized.Success, resized.Diagnostic);
        var generation = dock.Current!.Generation;
        Assert.AreEqual(0.25d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);

        Layout(grid);
        RaiseThumb(staleSplitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(staleSplitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(staleSplitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(0.25d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(0.75d, dock.Tree.FindSplit(MainSplit)!.SecondWeight, 1e-9);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(0.28d, grid.ColumnDefinitions[0].Width.Value, 1e-9);
        var current = Require<Grid>(dock.FindNode(MainSplit));
        Assert.AreEqual(0.25d, current.ColumnDefinitions[0].Width.Value, 1e-9);
        Assert.AreEqual(4, factory.Calls.Count);
    }

    [TestMethod]
    public void Panel_retry_refreshes_the_splitter_token()
    {
        var viewModel = CreateViewModel();
        var factory = new FlakyFactory(new EditorDockPanelFactory(viewModel))
        {
            FailOnce = DockPanelIds.Console,
        };
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console },
            dock.Current.PanelFailures.ToArray());

        var retry = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retry.Status);
        Assert.IsTrue(retry.IsCurrent);
        Assert.IsTrue(retry.Generation > generation);
        Assert.AreEqual(0, retry.PanelFailures.Count);
        Assert.AreSame(grid, dock.FindNode(MainSplit));
        Assert.AreSame(splitter, SplitterIn(grid));

        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(0.38d, dock.Tree.FindSplit(MainSplit)!.FirstWeight, 1e-9);
        Assert.AreEqual(retry.Generation + 1, dock.Current.Generation);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(5, factory.Calls);
    }

    [TestMethod]
    public void Tab_selection_commits_once_and_rejected_stale_selection_rolls_back()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var initial = Require<TabControl>(dock.FindNode(BottomGroup));
        var views = SnapshotViews(dock);
        Assert.IsTrue(dock.ResizeSplit(MainSplit, 1, 3).Success);
        var generation = dock.Current!.Generation;
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        initial.SelectedIndex = 1;

        Assert.AreEqual(0, initial.SelectedIndex);
        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);

        var current = Require<TabControl>(dock.FindNode(BottomGroup));
        current.SelectedIndex = 1;

        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreEqual(generation + 1, dock.Current.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);

        var committed = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.AreEqual(1, committed.SelectedIndex);

        current.SelectedIndex = 0;

        Assert.AreEqual(0, current.SelectedIndex);
        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreEqual(generation + 1, dock.Current.Generation);
        Assert.AreEqual(1, stateChanges);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Same_title_tabs_switch_by_instance_without_recreating_views()
    {
        var viewModel = CreateViewModel();
        var factory = new SameTitleFactory();
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);

        var tabs = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.AreEqual("Same title", HeaderAt(tabs, 0));
        Assert.AreEqual("Same title", HeaderAt(tabs, 1));
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(tabs, 0));
        Assert.AreEqual(DockPanelIds.Console, TagAt(tabs, 1));
        var inspector = Require<SameTitleView>(dock.FindPanel(DockPanelIds.Inspector));
        var console = Require<SameTitleView>(dock.FindPanel(DockPanelIds.Console));
        Assert.AreNotSame(inspector, console);
        Assert.AreEqual(4, factory.Calls);

        tabs.SelectedIndex = 1;

        Assert.AreEqual(
            DockPanelIds.Console,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreSame(inspector, dock.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, dock.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(4, factory.Calls);
        Assert.IsTrue(dock.Current!.IsCurrent);

        var refreshed = Require<TabControl>(dock.FindNode(BottomGroup));
        Assert.AreEqual(1, refreshed.SelectedIndex);
        Assert.AreEqual(DockPanelIds.Inspector, TagAt(refreshed, 0));
        Assert.AreEqual(DockPanelIds.Console, TagAt(refreshed, 1));

        refreshed.SelectedIndex = 0;

        Assert.AreEqual(
            DockPanelIds.Inspector,
            dock.Tree.FindGroup(BottomGroup)!.ActivePanel);
        Assert.AreSame(inspector, dock.FindPanel(DockPanelIds.Inspector));
        Assert.AreSame(console, dock.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(4, factory.Calls);
    }

    private static EditorShellViewModel CreateViewModel()
    {
        var path = Path.Combine(
            Path.GetTempPath(), $"nova-dock-interaction-{Guid.NewGuid():N}.json");
        return new EditorShellViewModel(EditorWorkspace.CreateSample(path));
    }

    private static void Layout(Control control, double width = 1000, double height = 800)
    {
        control.Measure(new Size(width, height));
        control.Arrange(new Rect(0, 0, width, height));
        Assert.AreEqual(width, control.Bounds.Width, 0.001);
        Assert.AreEqual(height, control.Bounds.Height, 0.001);
    }

    private static DockSplitterThumb SplitterIn(Grid grid)
        => grid.Children.OfType<DockSplitterThumb>().Single();

    private static void RaiseThumb(Thumb thumb, RoutedEvent routedEvent, Vector vector)
        => thumb.RaiseEvent(new VectorEventArgs
        {
            RoutedEvent = routedEvent,
            Vector = vector,
        });

    private static void LoseCapture(DockSplitterThumb splitter)
    {
        // Thumb only raises DragCompleted from capture loss while it holds a
        // pointer origin; seed that state to exercise the real framework path.
        var dragState = typeof(Thumb).GetField(
            "_lastPoint",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(dragState);
        dragState.SetValue(splitter, (Point?)new Point(0, 0));

        var args = new PointerCaptureLostEventArgs(splitter, pointer: null!);
        var method = typeof(DockSplitterThumb).GetMethod(
            "OnPointerCaptureLost",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(method);
        method.Invoke(splitter, new object[] { args });
    }

    private static Dictionary<PanelInstanceId, IDockPanelView> SnapshotViews(
        EditorDockWorkspace dock)
        => new()
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

    private static void AssertViewsAreSame(
        EditorDockWorkspace dock,
        Dictionary<PanelInstanceId, IDockPanelView> views)
    {
        foreach (var (panel, view) in views)
            Assert.AreSame(view, dock.FindPanel(panel), panel.ToString());
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
    }

    private static string? HeaderAt(TabControl tabs, int index)
        => (tabs.Items[index] as TabItem)?.Header as string;

    private static PanelInstanceId TagAt(TabControl tabs, int index)
    {
        var item = (TabItem)tabs.Items[index]!;
        Assert.IsNotNull(item.Tag);
        return (PanelInstanceId)item.Tag!;
    }

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

    private sealed class SameTitleFactory : IDockPanelViewFactory
    {
        public int Calls { get; private set; }

        public bool CanCreate(PanelInstanceId instance) => true;

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls++;
            return new SameTitleView();
        }
    }

    private sealed class SameTitleView : IDockPanelView
    {
        public string Title => "Same title";

        public Control Content { get; } = new Border();

        public int DisposeCount { get; private set; }

        public void Dispose() => DisposeCount++;
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
