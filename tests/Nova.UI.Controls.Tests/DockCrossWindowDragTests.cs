using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class DockCrossWindowDragTests
{
    private static readonly DockFloatRootId Float1 = new("dock.float.1");
    private static readonly DockFloatRootId Float2 = new("dock.float.2");
    private static readonly DockNodeId MainBottom = new("dock.tabs.bottom");
    private static readonly DockNodeId MainScene = new("dock.tabs.scene");
    private static readonly DockBounds FloatBounds = new(120, 80, 640, 480);
    private static readonly DockBounds OtherBounds = new(260, 160, 800, 600);
    private static readonly PanelInstanceId[] AllPanels =
    [
        DockPanelIds.Hierarchy,
        DockPanelIds.Scene,
        DockPanelIds.Inspector,
        DockPanelIds.Console,
    ];

    [TestMethod]
    public void Cross_root_moves_reuse_views_and_remove_the_emptied_float_host()
    {
        using var harness = CreateHarness();
        var scene = RequireView(harness.Registry, DockPanelIds.Scene);
        var console = RequireView(harness.Registry, DockPanelIds.Console);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var float1 = RequireFloat(harness, Float1);
        var float1Group = FloatGroup(harness, Float1);
        float1.LocalHit = HitAt(float1Group);
        var mainToken = Token(harness.Main);
        var floatToken = Token(float1);

        Assert.IsTrue(
            harness.Coordinator.BeginDrag(
                DockRootHostId.Main,
                DockPanelIds.Console,
                mainToken).Success);

        var hit = harness.Coordinator.UpdateDrag(float1.Id, new Point(10, 10));

        Assert.IsNotNull(hit);
        Assert.IsTrue(float1.PreviewShowCount > 0);
        var moved = harness.Coordinator.CommitDrag();

        Assert.IsTrue(moved.Success, moved.Diagnostic);
        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene, DockPanelIds.Console },
            harness.Coordinator.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToArray());
        Assert.AreEqual(4, harness.Views.CallCount);
        RequireSameView(scene, harness.Registry, DockPanelIds.Scene);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);
        Assert.AreNotEqual(mainToken, Token(harness.Main));
        Assert.AreNotEqual(floatToken, Token(float1));

        var mainToken2 = Token(harness.Main);
        var floatToken2 = Token(float1);
        harness.Main.LocalHit = HitAt(MainBottom);
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(float1.Id, DockPanelIds.Scene, floatToken2).Success);
        Assert.IsNotNull(
            harness.Coordinator.UpdateDrag(DockRootHostId.Main, new Point(4, 4)));

        var back = harness.Coordinator.CommitDrag();

        Assert.IsTrue(back.Success, back.Diagnostic);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console },
            harness.Coordinator.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToArray());
        RequireSameView(scene, harness.Registry, DockPanelIds.Scene);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreNotEqual(mainToken2, Token(harness.Main));
        Assert.AreNotEqual(floatToken2, Token(float1));

        var floatToken3 = Token(float1);
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(float1.Id, DockPanelIds.Console, floatToken3).Success);
        Assert.IsNotNull(
            harness.Coordinator.UpdateDrag(DockRootHostId.Main, new Point(4, 4)));

        var last = harness.Coordinator.CommitDrag();

        Assert.IsTrue(last.Success, last.Diagnostic);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.IsNull(harness.Coordinator.FindHost(float1.Id));
        Assert.AreEqual(1, float1.CloseCount);
        Assert.AreEqual(1, float1.DisposeCount);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(0, ((FakePanelView)console.View!).DisposeCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
    }

    [TestMethod]
    public void Target_token_is_revalidated_at_commit_after_the_target_root_changes()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
        var float1 = RequireFloat(harness, Float1);
        var float2 = RequireFloat(harness, Float2);
        float2.LocalHit = HitAt(FloatGroup(harness, Float2));
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(float1.Id, DockPanelIds.Scene, Token(float1)).Success);
        Assert.IsNotNull(harness.Coordinator.UpdateDrag(float2.Id, new Point(8, 8)));

        var changed = harness.Coordinator.MovePanel(
            DockPanelIds.Hierarchy,
            new DockTarget.Center(FloatGroup(harness, Float2)));

        Assert.IsTrue(changed.Success, changed.Diagnostic);
        var revision = harness.Coordinator.Workspace.Revision;

        var commit = harness.Coordinator.CommitDrag();

        Assert.IsFalse(commit.Success);
        Assert.IsFalse(commit.ModelCommitted);
        Assert.AreEqual(DockHostError.StaleToken, commit.Host.Error);
        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        var workspace = harness.Coordinator.Workspace;
        Assert.IsTrue(workspace.TryLocatePanel(DockPanelIds.Scene, out var sceneLocation));
        Assert.AreEqual(Float1, sceneLocation.FloatRootId);
        Assert.IsTrue(workspace.TryLocatePanel(DockPanelIds.Hierarchy, out var hierarchy));
        Assert.AreEqual(Float2, hierarchy.FloatRootId);
    }

    [TestMethod]
    public void Source_token_is_revalidated_at_commit_after_the_source_root_changes()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var float1 = RequireFloat(harness, Float1);
        float1.LocalHit = HitAt(FloatGroup(harness, Float1));
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(
                DockRootHostId.Main,
                DockPanelIds.Console,
                Token(harness.Main)).Success);
        Assert.IsNotNull(harness.Coordinator.UpdateDrag(float1.Id, new Point(6, 6)));

        var resized = harness.Coordinator.ResizeSplit(
            DockRootHostId.Main,
            new DockNodeId("dock.main"),
            0.5,
            0.5);

        Assert.IsTrue(resized.Success, resized.Diagnostic);
        var revision = harness.Coordinator.Workspace.Revision;

        var commit = harness.Coordinator.CommitDrag();

        Assert.IsFalse(commit.Success);
        Assert.IsFalse(commit.ModelCommitted);
        Assert.AreEqual(DockHostError.StaleToken, commit.Host.Error);
        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.IsTrue(
            harness.Coordinator.Workspace.TryLocatePanel(
                DockPanelIds.Console,
                out var consoleLocation));
        Assert.IsTrue(consoleLocation.IsMain);
    }

    [TestMethod]
    public void Bounds_changes_on_another_float_root_do_not_invalidate_the_target_token()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
        var scene = RequireView(harness.Registry, DockPanelIds.Scene);
        var console = RequireView(harness.Registry, DockPanelIds.Console);
        var float1 = RequireFloat(harness, Float1);
        var float2 = RequireFloat(harness, Float2);
        float1.LocalHit = HitAt(FloatGroup(harness, Float1));
        var float2Token = Token(float2);

        Assert.IsTrue(
            harness.Coordinator.BeginDrag(
                DockRootHostId.Main,
                DockPanelIds.Console,
                Token(harness.Main)).Success);
        Assert.IsNotNull(harness.Coordinator.UpdateDrag(float1.Id, new Point(12, 12)));

        var movedBounds = new DockBounds(340, 220, 720, 520);
        var bounds = harness.Coordinator.UpdateFloatBounds(Float2, movedBounds, false);

        Assert.IsTrue(bounds.Success, bounds.Diagnostic);
        Assert.AreEqual(float2Token, Token(float2));

        var commit = harness.Coordinator.CommitDrag();

        Assert.IsTrue(commit.Success, commit.Diagnostic);
        Assert.AreEqual(float2Token, Token(float2));
        Assert.AreEqual(movedBounds, harness.Coordinator.Workspace.FloatingRoots[1].Bounds);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene, DockPanelIds.Console },
            harness.Coordinator.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToArray());
        RequireSameView(scene, harness.Registry, DockPanelIds.Scene);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Release_without_ctrl_outside_or_screen_point_cancels_the_drag()
    {
        using var harness = CreateHarness();
        var console = RequireView(harness.Registry, DockPanelIds.Console);
        var revision = harness.Coordinator.Workspace.Revision;
        var token = Token(harness.Main);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        Assert.IsTrue(harness.Coordinator.IsDragging);

        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(500, 400),
                hasScreenPoint: true,
                insideSource: false,
                KeyModifiers.None,
                renderScaling: 1d));

        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(500, 400),
                hasScreenPoint: true,
                insideSource: true,
                KeyModifiers.Control,
                renderScaling: 1d));

        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                default,
                hasScreenPoint: false,
                insideSource: false,
                KeyModifiers.Control,
                renderScaling: 1d));

        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Ctrl_release_outside_the_main_window_tears_off_a_float_root()
    {
        using var harness = CreateHarness();
        var console = RequireView(harness.Registry, DockPanelIds.Console);
        var token = Token(harness.Main);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(400, 300),
                hasScreenPoint: true,
                insideSource: false,
                KeyModifiers.Control,
                renderScaling: 2d));

        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        var floatRoot = harness.Coordinator.Workspace.FloatingRoots[0];
        Assert.AreEqual(new DockBounds(200, 150, 640, 480), floatRoot.Bounds);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console },
            floatRoot.Root.EnumeratePanels().ToArray());
        Assert.AreEqual(Float1, floatRoot.Id);
        Assert.AreEqual(1, harness.FloatHosts.Count);
        Assert.AreEqual(4, harness.Views.CallCount);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);
        Assert.AreEqual(0, ((FakePanelView)console.View!).DisposeCount);
    }

    [TestMethod]
    public void A_resolved_target_wins_over_tear_off()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var float1 = RequireFloat(harness, Float1);
        float1.ScreenHit = HitAt(FloatGroup(harness, Float1));
        var console = RequireView(harness.Registry, DockPanelIds.Console);

        harness.Main.Raise(
            DockDragEventArgs.Begin(
                DockPanelIds.Console,
                MainBottom,
                Token(harness.Main)));
        harness.Main.Raise(
            DockDragEventArgs.Move(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(10, 10),
                hasScreenPoint: true,
                insideSource: false));

        Assert.IsTrue(harness.Coordinator.IsDragging);
        Assert.IsTrue(float1.PreviewShowCount > 0);

        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(10, 10),
                hasScreenPoint: true,
                insideSource: false,
                KeyModifiers.Control,
                renderScaling: 1d));

        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene, DockPanelIds.Console },
            harness.Coordinator.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToArray());
        Assert.AreEqual(Float1, harness.Coordinator.Workspace.FloatingRoots[0].Id);
        RequireSameView(console, harness.Registry, DockPanelIds.Console);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Tear_off_from_a_float_source_is_rejected_without_touching_the_model()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var float1 = RequireFloat(harness, Float1);
        var revision = harness.Coordinator.Workspace.Revision;
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(float1.Id, DockPanelIds.Scene, Token(float1)).Success);

        var result = harness.Coordinator.CommitTearOff(FloatBounds);

        Assert.IsFalse(result.Success);
        Assert.IsFalse(result.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.InvalidSource, result.ModelError);
        Assert.IsFalse(harness.Coordinator.IsDragging);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
    }

    [TestMethod]
    public void Tear_off_bounds_are_clamped_to_the_model_limits()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(
                DockRootHostId.Main,
                DockPanelIds.Console,
                Token(harness.Main)).Success);

        var result = harness.Coordinator.CommitTearOff(
            new DockBounds(double.NaN, 99999d, double.PositiveInfinity, -5d));

        Assert.IsTrue(result.Success, result.Diagnostic);
        var bounds = harness.Coordinator.Workspace.FloatingRoots[0].Bounds;
        Assert.AreEqual(
            new DockBounds(0d, DockBounds.MaxCoordinate, 640d, 1d),
            bounds);
    }

    // D10-C C-2 (ADR-011 §十一·三 / O-3.1/O-3.2): the monitor under the pointer
    // supplies the scaling; the source window's render scaling is not a global
    // conversion factor.
    [TestMethod]
    public void Dmc01_tear_off_uses_the_pointer_screen_scaling_not_the_source_window()
    {
        using var harness = CreateHarness();
        var token = Token(harness.Main);
        var pointerScreen = new DockScreenArea(
            "pointer",
            new DockPixelRect(0, 0, 3840, 2160),
            new DockPixelRect(0, 0, 3840, 2120),
            1.5d,
            Primary: true);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(2400, 1500),
                hasScreenPoint: true,
                insideSource: false,
                KeyModifiers.Control,
                renderScaling: 1d,
                screen: pointerScreen));

        var root = harness.Coordinator.Workspace.FloatingRoots.Single();
        Assert.AreEqual(new DockBounds(1600d, 1000d, 640d, 480d), root.Bounds);
        var host = RequireFloat(harness, root.Id);
        Assert.AreEqual(pointerScreen, host.RequestedScreen);
    }

    [TestMethod]
    public void Dmc01b_tear_off_falls_back_to_the_event_scaling_when_no_screen_is_known()
    {
        using var harness = CreateHarness();
        var token = Token(harness.Main);

        harness.Main.Raise(
            DockDragEventArgs.Begin(DockPanelIds.Console, MainBottom, token));
        harness.Main.Raise(
            DockDragEventArgs.Release(
                new Point(0, 0),
                localTargetResolved: false,
                new PixelPoint(400, 300),
                hasScreenPoint: true,
                insideSource: false,
                KeyModifiers.Control,
                renderScaling: 0d));

        var root = harness.Coordinator.Workspace.FloatingRoots.Single();
        Assert.AreEqual(new DockBounds(400d, 300d, 640d, 480d), root.Bounds);
        Assert.IsNull(RequireFloat(harness, root.Id).RequestedScreen);
    }

    // D10-C C-1 layering (ADR-011 §十一·三): a host without screen information
    // receives the model rectangle unchanged. Visibility clamping belongs to the
    // window host (HOST-01..04), never to the model or the coordinator.
    [TestMethod]
    public void Dmc02_tear_off_without_screen_data_leaves_the_model_screen_agnostic()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.BeginDrag(
                DockRootHostId.Main,
                DockPanelIds.Console,
                Token(harness.Main)).Success);

        var result = harness.Coordinator.CommitTearOff(
            new DockBounds(20000d, 20000d, 640d, 480d));

        Assert.IsTrue(result.Success, result.Diagnostic);
        var root = harness.Coordinator.Workspace.FloatingRoots.Single();
        Assert.IsTrue(root.Bounds.IsValid);
        Assert.AreEqual(new DockBounds(20000d, 20000d, 640d, 480d), root.Bounds);
        Assert.IsNull(RequireFloat(harness, root.Id).RequestedScreen);
    }

    private static Harness CreateHarness()
    {
        var views = new RecordingFactory(instance => new FakePanelView(instance));
        var registry = new DockPanelViewRegistry(views);
        var main = new FakeRootHost(DockRootHostDescriptor.ForMain(), registry);
        var floats = new Dictionary<DockRootHostId, FakeRootHost>();
        var coordinator = new DockWorkspaceCoordinator(
            DockWorkspace.CreateDefault(),
            main,
            registry,
            descriptor =>
            {
                var host = new FakeRootHost(descriptor, registry);
                floats.Add(host.Id, host);
                return host;
            });

        foreach (var panel in AllPanels)
            registry.GetOrCreate(panel);

        return new Harness(coordinator, registry, views, main, floats);
    }

    private static DockNodeId FloatGroup(Harness harness, DockFloatRootId rootId)
    {
        var root = harness.Coordinator.Workspace.FloatingRoots
            .Single(candidate => candidate.Id == rootId);
        return root.Root.EnumerateNodes().OfType<DockTabGroupNode>().First().Id;
    }

    private static DockDropTargetHit HitAt(DockNodeId group)
        => new(new DockTarget.Center(group), new Rect(0, 0, 100, 100), 0);

    private static DockApplyToken Token(FakeRootHost host)
    {
        Assert.IsTrue(host.TryGetCurrentToken(out var token));
        return token;
    }

    private static FakeRootHost RequireFloat(Harness harness, DockFloatRootId rootId)
    {
        var hostId = DockRootHostId.ForFloat(rootId);
        Assert.IsTrue(
            harness.FloatHosts.TryGetValue(hostId, out var host),
            $"No float host was created for '{rootId}'.");
        return host!;
    }

    private static DockPanelViewRegistration RequireView(
        DockPanelViewRegistry registry,
        PanelInstanceId panel)
    {
        Assert.IsTrue(registry.TryGet(panel, out var registration));
        return registration;
    }

    private static void RequireSameView(
        DockPanelViewRegistration expected,
        DockPanelViewRegistry registry,
        PanelInstanceId panel)
    {
        var actual = RequireView(registry, panel);
        Assert.AreSame(expected.View, actual.View);
        Assert.AreSame(expected.Host, actual.Host);
    }

    private sealed class Harness(
        DockWorkspaceCoordinator coordinator,
        DockPanelViewRegistry registry,
        RecordingFactory views,
        FakeRootHost mainHost,
        Dictionary<DockRootHostId, FakeRootHost> floatHosts) : IDisposable
    {
        public DockWorkspaceCoordinator Coordinator { get; } = coordinator;

        public DockPanelViewRegistry Registry { get; } = registry;

        public RecordingFactory Views { get; } = views;

        public FakeRootHost Main { get; } = mainHost;

        public Dictionary<DockRootHostId, FakeRootHost> FloatHosts { get; } = floatHosts;

        public void Dispose() => Coordinator.Dispose();
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

    private sealed class FakePanelView(PanelInstanceId instance) : IDockPanelView
    {
        public string Title { get; } = instance.ToString();

        public Control Content { get; } = new Border();

        public int DisposeCount { get; private set; }

        public void Dispose() => DisposeCount++;
    }

    private sealed class FakeRootHost
        : IDockRootHost, IDockDropTargetHost, IDockDragSourceHost, IDockFloatHostBounds
    {
        private readonly DockPanelViewRegistry _registry;
        private DockApplyResult? _current;
        private long _generation;
        private bool _disposed;

        public FakeRootHost(DockRootHostDescriptor descriptor, DockPanelViewRegistry registry)
        {
            Id = descriptor.Id;
            Kind = descriptor.Kind;
            _registry = registry;
        }

        public DockRootHostId Id { get; }

        public DockRootKind Kind { get; }

        public bool IsVisible { get; private set; }

        public bool IsHealthy => !_disposed && IsVisible && _current is { IsHealthy: true };

        public DockApplyResult? Current => _current;

        public DockBounds? RequestedBounds { get; private set; }

        public bool RequestedMaximized { get; private set; }

        public DockScreenArea? RequestedScreen { get; private set; }

        public void SetWindowBounds(
            DockBounds bounds,
            bool maximized,
            DockScreenArea? preferredScreen = null)
        {
            RequestedBounds = bounds;
            RequestedMaximized = maximized;
            RequestedScreen = preferredScreen;
        }

        public bool TryGetWindowBounds(out DockBounds bounds)
        {
            bounds = default;
            return false;
        }

        public DockDropTargetHit? LocalHit { get; set; }

        public DockDropTargetHit? ScreenHit { get; set; }

        public int PreviewShowCount { get; private set; }

        public int PreviewClearCount { get; private set; }

        public int CloseCount { get; private set; }

        public int DisposeCount { get; private set; }

        public event EventHandler<DockDragEventArgs>? DragEvent;

        public DockHostResult Prepare(DockTree root)
        {
            if (_disposed)
                return DockHostResult.Fail(DockHostError.CreationFailed, "disposed");

            var validation = root.Validate();
            return validation.IsValid
                ? DockHostResult.Ok()
                : DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    validation.Summary);
        }

        public DockHostResult Show()
        {
            if (_disposed)
                return DockHostResult.Fail(DockHostError.ShowFailed, "disposed");

            if (_current is not { IsHealthy: true })
                return DockHostResult.Fail(DockHostError.ShowFailed, "no projection");

            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Apply(DockTree root)
        {
            if (_disposed)
                return DockHostResult.Fail(DockHostError.ProjectionFailed, "disposed");

            _generation++;
            _current = new DockApplyResult(
                DockApplyStatus.Applied,
                _generation,
                root.Digest,
                root.Digest,
                Array.Empty<PanelInstanceId>(),
                null);
            return DockHostResult.Ok();
        }

        public DockHostResult Retry()
        {
            if (_disposed)
                return DockHostResult.Fail(DockHostError.RetryFailed, "disposed");

            return _current is null
                ? DockHostResult.Fail(DockHostError.RetryFailed, "no projection")
                : DockHostResult.Ok();
        }

        public DockHostResult Close()
        {
            if (_disposed)
                return DockHostResult.Ok();

            CloseCount++;
            IsVisible = false;
            return DockHostResult.Ok();
        }

        public bool TryGetCurrentToken(out DockApplyToken token)
        {
            if (_current is null)
            {
                token = default;
                return false;
            }

            token = new DockApplyToken(
                Id,
                _current.Generation,
                _current.CommittedDigest);
            return true;
        }

        public Control? FindNode(DockNodeId id) => null;

        public IDockPanelView? FindPanel(PanelInstanceId instance)
            => _registry.TryGet(instance, out var registration)
                ? registration.View
                : null;

        public bool TryHitTestLocal(
            Point point,
            DockNodeId? sourceGroupId,
            out DockDropTargetHit hit)
        {
            hit = LocalHit ?? default;
            return LocalHit is not null;
        }

        public bool TryHitTestScreen(
            PixelPoint screenPoint,
            DockNodeId? sourceGroupId,
            out DockDropTargetHit hit)
        {
            hit = ScreenHit ?? default;
            return ScreenHit is not null;
        }

        public void ShowExternalDropPreview(DockDropTargetHit hit) => PreviewShowCount++;

        public void ClearExternalDropPreview() => PreviewClearCount++;

        public void Dispose()
        {
            DisposeCount++;
            _disposed = true;
            IsVisible = false;
        }

        public void Raise(DockDragEventArgs e) => DragEvent?.Invoke(this, e);
    }
}
