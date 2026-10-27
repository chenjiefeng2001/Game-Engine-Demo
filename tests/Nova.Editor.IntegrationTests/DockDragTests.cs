using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Nova.Editor.Services;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class DockDragTests
{
    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId LeftGroup = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");

    [TestMethod]
    public void Same_group_drag_reorders_tabs_with_one_commit_and_reuses_views()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(500, 615), pointer);

        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);

        Move(dock.Root, new Point(300, 615), pointer);
        Release(dock.Root, new Point(300, 615), pointer);

        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console, DockPanelIds.Inspector },
            bottom.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Console, bottom.ActivePanel);
        Assert.AreEqual(generation + 1, dock.Current!.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.AreEqual(dock.Tree.Digest, dock.Current.CommittedDigest);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
        foreach (var view in views.Values)
            Assert.AreEqual(0, ((RecordingView)view).DisposeCount);
    }

    [TestMethod]
    public void Cross_group_drop_on_tab_strip_moves_panel_and_reuses_view()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        MovePanelByDrag(
            dock, pointer,
            new Point(640, 15),
            new Point(640, 615));

        Assert.IsNull(dock.Tree.FindGroup(SceneGroup));
        Assert.IsNull(dock.Tree.FindGroup(CenterSplit));
        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Scene, DockPanelIds.Console },
            bottom.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Scene, bottom.ActivePanel);
        Assert.AreEqual(generation + 1, dock.Current!.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.IsTrue(dock.Current.IsCurrent);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
        foreach (var view in views.Values)
            Assert.AreEqual(0, ((RecordingView)view).DisposeCount);
    }

    [TestMethod]
    public void Left_edge_drop_creates_horizontal_split_before_target()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        MovePanelByDrag(
            dock, pointer,
            new Point(930, 615),
            new Point(20, 400));

        var main = Require<DockSplitNode>(dock.Tree.Root);
        Assert.AreEqual(MainSplit, main.Id);
        Assert.AreEqual(DockOrientation.Horizontal, main.Orientation);
        var split = Require<DockSplitNode>(main.First);
        Assert.AreEqual(new DockNodeId("dock.split.1"), split.Id);
        Assert.AreEqual(DockOrientation.Horizontal, split.Orientation);
        Assert.AreEqual(0.5d, split.FirstWeight, 1e-9);
        Assert.AreEqual(0.5d, split.SecondWeight, 1e-9);
        var moved = Require<DockTabGroupNode>(split.First);
        Assert.AreEqual(new DockNodeId("dock.tabs.1"), moved.Id);
        CollectionAssert.AreEqual(new[] { DockPanelIds.Console }, moved.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Console, moved.ActivePanel);
        var left = Require<DockTabGroupNode>(split.Second);
        Assert.AreEqual(LeftGroup, left.Id);
        CollectionAssert.AreEqual(new[] { DockPanelIds.Hierarchy }, left.Panels.ToArray());
        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(new[] { DockPanelIds.Inspector }, bottom.Panels.ToArray());
        Assert.IsTrue(dock.Tree.Validate().IsValid);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Bottom_edge_drop_creates_vertical_split_after_target()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        MovePanelByDrag(
            dock, pointer,
            new Point(140, 15),
            new Point(640, 580));

        var root = Require<DockSplitNode>(dock.Tree.Root);
        Assert.AreEqual(CenterSplit, root.Id);
        Assert.AreEqual(DockOrientation.Vertical, root.Orientation);
        Assert.AreEqual(
            0.75d,
            root.FirstWeight,
            1e-9,
            "the surviving parent split keeps its own weights");
        Assert.AreEqual(0.25d, root.SecondWeight, 1e-9);
        var split = Require<DockSplitNode>(root.First);
        Assert.AreEqual(new DockNodeId("dock.split.1"), split.Id);
        Assert.AreEqual(DockOrientation.Vertical, split.Orientation);
        Assert.AreEqual(0.5d, split.FirstWeight, 1e-9);
        Assert.AreEqual(0.5d, split.SecondWeight, 1e-9);
        var scene = Require<DockTabGroupNode>(split.First);
        Assert.AreEqual(SceneGroup, scene.Id);
        CollectionAssert.AreEqual(new[] { DockPanelIds.Scene }, scene.Panels.ToArray());
        var moved = Require<DockTabGroupNode>(split.Second);
        Assert.AreEqual(new DockNodeId("dock.tabs.1"), moved.Id);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Hierarchy }, moved.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Hierarchy, moved.ActivePanel);
        var bottom = Require<DockTabGroupNode>(root.Second);
        Assert.AreEqual(BottomGroup, bottom.Id);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels.ToArray());
        Assert.IsTrue(dock.Tree.Validate().IsValid);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Drop_on_source_content_center_is_rejected_without_layout_change()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(640, 700), pointer);
        Release(dock.Root, new Point(640, 700), pointer);

        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current!.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Capture_loss_cancels_the_drag_without_commit()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);
        dock.Root.RaiseEvent(new PointerCaptureLostEventArgs(dock.Root, pointer));
        Release(dock.Root, new Point(300, 615), pointer);

        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current!.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Release_outside_the_workspace_discards_the_preview()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);
        Move(dock.Root, new Point(1400, 1000), pointer);
        Release(dock.Root, new Point(1400, 1000), pointer);

        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current!.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Rebuild_during_drag_discards_the_stale_release()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);

        var resized = Require<DockTree>(
            dock.Tree.ResizeSplit(MainSplit, 0.3d, 0.7d).Tree);
        var applied = dock.Apply(resized);
        Assert.AreEqual(DockApplyStatus.Applied, applied.Status);
        Assert.AreEqual(generation + 1, applied.Generation);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));

        Release(dock.Root, new Point(320, 615), pointer);

        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels.ToArray());
        Assert.AreEqual(resized.Digest, dock.Tree.Digest);
        Assert.AreEqual(generation + 1, dock.Current!.Generation);
        Assert.AreEqual(1, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Visual_failure_during_drag_degrades_and_retry_recovers()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        var interceptor = new TestInterceptor();
        using var dock = new EditorDockWorkspace(viewModel, interceptor, factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);

        interceptor.AlwaysFail = true;
        var resized = Require<DockTree>(
            dock.Tree.ResizeSplit(MainSplit, 0.3d, 0.7d).Tree);
        var degraded = dock.Apply(resized);

        Assert.AreEqual(DockApplyStatus.Degraded, degraded.Status);
        Assert.IsTrue(dock.IsDegraded);
        Assert.AreEqual(generation + 2, degraded.Generation);
        Assert.AreEqual(1, stateChanges);

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);
        Release(dock.Root, new Point(300, 615), pointer);

        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels.ToArray());
        Assert.AreEqual(resized.Digest, dock.Tree.Digest);
        Assert.AreEqual(generation + 2, dock.Current!.Generation);
        Assert.AreEqual(1, stateChanges);

        interceptor.AlwaysFail = false;
        var recovered = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, recovered.Status);
        Assert.IsFalse(dock.IsDegraded);
        Assert.IsTrue(dock.Current!.IsCurrent);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels.ToArray());
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Press_on_inactive_tab_rebinds_after_activation_rebuild()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);

        var activated = dock.ActivateTab(BottomGroup, DockPanelIds.Console);
        Assert.IsTrue(activated.Success, activated.Diagnostic);
        Assert.AreEqual(generation + 1, dock.Current!.Generation);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));

        Move(dock.Root, new Point(300, 615), pointer);
        Release(dock.Root, new Point(300, 615), pointer);

        var bottom = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console, DockPanelIds.Inspector },
            bottom.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Console, bottom.ActivePanel);
        Assert.AreEqual(generation + 2, dock.Current!.Generation);
        Assert.AreEqual(2, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Drag_below_threshold_does_not_commit()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var generation = dock.Current!.Generation;
        var digest = dock.Tree.Digest;
        var views = SnapshotViews(dock);
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(932, 616), pointer);
        Release(dock.Root, new Point(932, 616), pointer);

        Assert.AreEqual(digest, dock.Tree.Digest);
        Assert.AreEqual(generation, dock.Current!.Generation);
        Assert.AreEqual(0, stateChanges);
        Assert.AreEqual(4, factory.Calls.Count);
        AssertViewsAreSame(dock, views);
    }

    [TestMethod]
    public void Main_window_drag_opens_a_coordinator_session_for_cross_window_moves()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var pointer = NewPointer();

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(500, 615), pointer);

        Assert.IsTrue(dock.IsDragging);

        Release(dock.Root, new Point(500, 615), pointer);
        Assert.IsFalse(dock.IsDragging);
    }

    [TestMethod]
    public void Persistence_capture_is_blocked_while_a_real_drag_session_is_open()
    {
        var viewModel = CreateViewModel();
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        using var dock = new EditorDockWorkspace(viewModel, panelFactory: factory);
        Layout(Require<Grid>(dock.FindNode(MainSplit)));
        var pointer = NewPointer();

        Assert.IsTrue(dock.Capture().CanPersist);

        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(500, 615), pointer);

        var blocked = dock.Capture();
        Assert.IsFalse(blocked.CanPersist);
        StringAssert.Contains(blocked.Reason, "drag");

        Release(dock.Root, new Point(500, 615), pointer);
        var settled = dock.Capture();
        Assert.IsTrue(settled.CanPersist, settled.Reason);
    }

    private static void MovePanelByDrag(
        EditorDockWorkspace dock,
        IPointer pointer,
        Point source,
        Point target)
    {
        Press(dock.Root, source, pointer);
        Move(dock.Root, target, pointer);
        Release(dock.Root, target, pointer);
    }

    private static EditorShellViewModel CreateViewModel()
    {
        var path = Path.Combine(
            Path.GetTempPath(), $"nova-dock-drag-{Guid.NewGuid():N}.json");
        return new EditorShellViewModel(EditorWorkspace.CreateSample(path));
    }

    private static void Layout(Control control, double width = 1000, double height = 800)
    {
        control.Measure(new Size(width, height));
        control.Arrange(new Rect(0, 0, width, height));
        Assert.AreEqual(width, control.Bounds.Width, 0.001);
        Assert.AreEqual(height, control.Bounds.Height, 0.001);
    }

    private static Pointer NewPointer() => new(1, PointerType.Mouse, isPrimary: true);

    private static void Press(Control root, Point point, IPointer pointer)
        => root.RaiseEvent(new PointerPressedEventArgs(
            root,
            pointer,
            root,
            point,
            timestamp: 0,
            new PointerPointProperties(
                RawInputModifiers.LeftMouseButton,
                PointerUpdateKind.LeftButtonPressed),
            KeyModifiers.None));

    private static void Move(Control root, Point point, IPointer pointer)
        => root.RaiseEvent(new PointerEventArgs(
            InputElement.PointerMovedEvent,
            root,
            pointer,
            root,
            point,
            timestamp: 1,
            new PointerPointProperties(
                RawInputModifiers.LeftMouseButton,
                PointerUpdateKind.LeftButtonPressed),
            KeyModifiers.None));

    private static void Release(Control root, Point point, IPointer pointer)
        => root.RaiseEvent(new PointerReleasedEventArgs(
            root,
            pointer,
            root,
            point,
            timestamp: 2,
            new PointerPointProperties(
                RawInputModifiers.None,
                PointerUpdateKind.LeftButtonReleased),
            KeyModifiers.None,
            MouseButton.Left));

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

    private sealed class TestInterceptor : IDockNodeApplyInterceptor
    {
        public bool AlwaysFail { get; set; }

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            if (AlwaysFail)
                throw new InvalidOperationException("injected visual failure");
        }
    }
}
