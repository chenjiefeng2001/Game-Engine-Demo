using Avalonia.Controls;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

/// <summary>
/// D10-C C-1/C-3 host gates (ADR-011 §十一·三): the window adapter owns screen
/// data, clamps on first show and the coordinator writes the settled geometry
/// back into the screen-agnostic model.
/// </summary>
[TestClass]
public sealed class DockFloatPlacementHostTests
{
    private static readonly DockScreenArea Primary = new(
        "primary",
        new DockPixelRect(0, 0, 2560, 1600),
        new DockPixelRect(0, 0, 2560, 1552),
        1d,
        Primary: true);

    private static readonly DockScreenArea Secondary = new(
        "secondary",
        new DockPixelRect(2560, 0, 1920, 1080),
        new DockPixelRect(2560, 0, 1920, 1040),
        1d,
        Primary: false);

    // HOST-01 — O-2.3/O-2.4/O-2.5: an off-desktop rectangle is clamped to the
    // primary work area before the window is allowed to appear there.
    [TestMethod]
    public void Host01_show_clamps_an_off_screen_float_into_the_primary_work_area()
    {
        var (host, window) = CreateHost(
            new DockBounds(20000d, 20000d, 640d, 480d),
            screens: new[] { Primary, Secondary });
        try
        {
            Assert.AreEqual(DockPlacementReason.PrimaryFallback, host.LastPlacement!.Value.Reason);
            Assert.AreEqual(new DockBounds(1920d, 1072d, 640d, 480d), host.SettledBounds);
            Assert.AreEqual(host.SettledBounds, window.Bounds);
        }
        finally
        {
            host.Dispose();
        }
    }

    // HOST-02 — O-2.1: a float already inside a secondary work area is never
    // pulled to the primary just because no screen identity was persisted.
    [TestMethod]
    public void Host02_show_keeps_a_fully_visible_float_on_a_secondary_screen()
    {
        var bounds = new DockBounds(2660d, 100d, 640d, 480d);
        var (host, window) = CreateHost(bounds, screens: new[] { Primary, Secondary });
        try
        {
            Assert.AreEqual(DockPlacementReason.InsideWorkArea, host.LastPlacement!.Value.Reason);
            Assert.AreEqual(Secondary, host.LastPlacement.Value.Screen);
            Assert.AreEqual(bounds, host.SettledBounds);
            Assert.AreEqual(bounds, window.Bounds);
        }
        finally
        {
            host.Dispose();
        }
    }

    // HOST-03 — O-3.1: tear-off geometry is placed on the monitor under the
    // pointer, even though its DIP rectangle came from another screen.
    [TestMethod]
    public void Host03_show_places_a_tear_off_on_the_preferred_pointer_screen()
    {
        var (host, window) = CreateHost(
            new DockBounds(100d, 100d, 640d, 480d),
            preferredScreen: Secondary,
            screens: new[] { Primary, Secondary });
        try
        {
            Assert.AreEqual(DockPlacementReason.Clamped, host.LastPlacement!.Value.Reason);
            Assert.AreEqual(Secondary, host.LastPlacement.Value.Screen);
            Assert.AreEqual(new DockBounds(2560d, 100d, 640d, 480d), host.SettledBounds);
            Assert.AreEqual(host.SettledBounds, window.Bounds);
        }
        finally
        {
            host.Dispose();
        }
    }

    // HOST-04 — O-2.5: the measured frame margin participates in the clamp, so
    // the outer rectangle (not just the client area) stays inside the work area.
    // The model position is the outer origin, so the settled X/Y is 1904/1033.
    [TestMethod]
    public void Host04_show_keeps_the_outer_frame_inside_the_work_area()
    {
        var frame = new DockWindowFrame(8d, 31d, 8d, 8d);
        var (host, window) = CreateHost(
            new DockBounds(2000d, 1200d, 640d, 480d),
            frame,
            screens: new[] { Primary });
        try
        {
            Assert.AreEqual(new DockBounds(1904d, 1033d, 640d, 480d), host.SettledBounds);
            Assert.AreEqual(host.SettledBounds, window.Bounds);
            Assert.AreEqual(2560d, window.Bounds.X + window.Bounds.Width + frame.Horizontal);
            Assert.AreEqual(1552d, window.Bounds.Y + window.Bounds.Height + frame.Vertical);
        }
        finally
        {
            host.Dispose();
        }
    }

    // HOST-05 — the model follows the geometry the window actually settled on,
    // while the revision stays at the single committed float operation.
    [TestMethod]
    public void Host05_the_coordinator_writes_settled_float_geometry_back_into_the_model()
    {
        var harness = CreateCoordinator(Primary, Secondary);
        try
        {
            var result = harness.Coordinator.FloatPanel(
                DockPanelIds.Scene,
                new DockBounds(20000d, 20000d, 640d, 480d));

            Assert.IsTrue(result.Success, result.Diagnostic);
            var root = harness.Coordinator.Workspace.FloatingRoots.Single();
            Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
            Assert.AreEqual(new DockBounds(1920d, 1072d, 640d, 480d), root.Bounds);
            Assert.AreEqual(root.Bounds, root.RestoreBounds);
            Assert.AreEqual(
                root.Bounds,
                harness.Windows[DockRootHostId.ForFloat(root.Id)].Bounds);
        }
        finally
        {
            harness.Coordinator.Dispose();
        }
    }

    // HOST-06 — restored geometry is clamped on first show and the layout
    // history baseline is re-seated, so undo returns to the visible rectangle
    // rather than resurrecting the off-screen saved one (D9 boundary intact).
    [TestMethod]
    public void Host06_restore_clamps_and_reseats_the_history_baseline()
    {
        var candidate = DockWorkspace.CreateDefault()
            .FloatPanel(DockPanelIds.Scene, new DockBounds(20000d, 20000d, 640d, 480d))
            .Workspace!;
        var windows = new Dictionary<DockRootHostId, TestFloatWindow>();
        var views = new RecordingFactory();
        var registry = new DockPanelViewRegistry(views);
        var main = new DockProjectorRootHost(DockRootHostDescriptor.ForMain(), registry);

        var restored = DockWorkspaceCoordinator.Restore(
            candidate,
            main,
            registry,
            descriptor =>
            {
                var window = new TestFloatWindow();
                window.ScreenList.Add(Primary);
                window.ScreenList.Add(Secondary);
                windows.Add(descriptor.Id, window);
                return new DockWindowRootHost(descriptor, registry, window);
            });

        Assert.IsTrue(restored.Success, restored.Failure);
        var coordinator = restored.Coordinator!;
        try
        {
            var restoredFloat = coordinator.Workspace.FloatingRoots.Single();
            Assert.AreEqual(new DockBounds(1920d, 1072d, 640d, 480d), restoredFloat.Bounds);
            Assert.AreEqual(restoredFloat.Bounds, restoredFloat.RestoreBounds);

            var second = coordinator.FloatPanel(
                DockPanelIds.Inspector,
                new DockBounds(100d, 100d, 640d, 480d));
            Assert.IsTrue(second.Success, second.Diagnostic);
            Assert.IsTrue(coordinator.CanUndoLayout);

            var undo = coordinator.UndoLayout();

            Assert.IsTrue(undo.Success, undo.Diagnostic);
            var undone = coordinator.Workspace.FloatingRoots.Single();
            Assert.AreEqual(restoredFloat.Id, undone.Id);
            Assert.AreEqual(new DockBounds(1920d, 1072d, 640d, 480d), undone.Bounds);
            Assert.AreEqual(undone.Bounds, undone.RestoreBounds);
        }
        finally
        {
            coordinator.Dispose();
        }
    }

    private static (DockWindowRootHost Host, TestFloatWindow Window) CreateHost(
        DockBounds bounds,
        DockWindowFrame frame = default,
        DockScreenArea? preferredScreen = null,
        IReadOnlyList<DockScreenArea>? screens = null)
    {
        var views = new RecordingFactory();
        var registry = new DockPanelViewRegistry(views);
        var floatRoot = DockWorkspace.CreateDefault()
            .FloatPanel(DockPanelIds.Scene, bounds)
            .Workspace!
            .FloatingRoots[0];
        var window = new TestFloatWindow
        {
            FrameMargins = frame,
        };
        if (screens is not null)
            window.ScreenList.AddRange(screens);

        var host = new DockWindowRootHost(
            DockRootHostDescriptor.ForFloat(floatRoot.Id),
            registry,
            window);
        Assert.IsTrue(host.Prepare(floatRoot.Root).Success);
        Assert.IsTrue(host.Apply(floatRoot.Root).Success);
        host.SetWindowBounds(bounds, maximized: false, preferredScreen);

        var show = host.Show();
        Assert.IsTrue(show.Success, show.Diagnostic);
        return (host, window);
    }

    private static CoordinatorHarness CreateCoordinator(params DockScreenArea[] screens)
    {
        var views = new RecordingFactory();
        var registry = new DockPanelViewRegistry(views);
        var main = new DockProjectorRootHost(DockRootHostDescriptor.ForMain(), registry);
        var windows = new Dictionary<DockRootHostId, TestFloatWindow>();
        var coordinator = new DockWorkspaceCoordinator(
            DockWorkspace.CreateDefault(),
            main,
            registry,
            descriptor =>
            {
                var window = new TestFloatWindow();
                window.ScreenList.AddRange(screens);
                windows.Add(descriptor.Id, window);
                return new DockWindowRootHost(descriptor, registry, window);
            });
        return new CoordinatorHarness(coordinator, windows);
    }

    private sealed class CoordinatorHarness(
        DockWorkspaceCoordinator coordinator,
        Dictionary<DockRootHostId, TestFloatWindow> windows) : IDisposable
    {
        public DockWorkspaceCoordinator Coordinator { get; } = coordinator;

        public Dictionary<DockRootHostId, TestFloatWindow> Windows { get; } = windows;

        public void Dispose() => Coordinator.Dispose();
    }

    private sealed class RecordingFactory : IDockPanelViewFactory
    {
        public bool CanCreate(PanelInstanceId instance) => true;

        public IDockPanelView Create(PanelInstanceId instance) => new TestPanelView(instance);

        private sealed class TestPanelView(PanelInstanceId instance) : IDockPanelView
        {
            public string Title { get; } = instance.ToString();

            public Control Content { get; } = new Border();

            public void Dispose()
            {
            }
        }
    }

    private sealed class TestFloatWindow : IDockFloatWindow
    {
        public Control? Content { get; set; }

        public string Title { get; set; } = string.Empty;

        public bool IsVisible { get; private set; }

        public bool IsMaximized { get; private set; }

        public DockBounds Bounds { get; private set; }

        public List<DockScreenArea> ScreenList { get; } = [];

        public IReadOnlyList<DockScreenArea> Screens => ScreenList;

        public DockWindowFrame FrameMargins { get; set; } = DockWindowFrame.None;

        // The dialogs are driven by the host, never raised by this window fake.
        public event EventHandler<DockFloatWindowClosingEventArgs>? Closing
        {
            add { }
            remove { }
        }

        public event EventHandler? BoundsChanged
        {
            add { }
            remove { }
        }

        public void Show(DockBounds bounds, double screenScaling = 0d)
        {
            Bounds = bounds;
            IsVisible = true;
        }

        public void ApplyBounds(DockBounds bounds, bool maximized, double screenScaling = 0d)
        {
            Bounds = bounds;
            IsMaximized = maximized;
        }

        public void Close()
        {
            IsVisible = false;
        }

        public void Dispose()
        {
            IsVisible = false;
            Content = null;
        }
    }
}
