using Avalonia.Controls;
using Avalonia.Interactivity;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class DockFloatWindowLifecycleTests
{
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly DockFloatRootId Float1 = new("dock.float.1");
    private static readonly DockNodeId Float1Group = new("dock.float.1.tabs.1");
    private static readonly DockBounds FloatBounds = new(120, 80, 640, 480);
    private static readonly DockBounds MovedBounds = new(260, 180, 800, 600);

    [TestMethod]
    public void Floating_a_panel_shows_a_real_window_host_after_the_model_commits()
    {
        using var harness = CreateHarness();
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var mainToken = GetToken(harness.MainHost);

        var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsTrue(result.ModelCommitted);
        Assert.AreEqual(1, result.Workspace.Revision);
        Assert.AreEqual(1, harness.WindowCount);
        var window = harness.Windows[0];
        Assert.AreEqual(1, window.ShowCount);
        Assert.IsTrue(window.IsVisible);
        Assert.AreEqual(FloatBounds, window.Bounds);
        Assert.IsFalse(window.IsMaximized);
        Assert.IsNotNull(window.Content);
        Assert.IsFalse(FindRecoveryBanner(window).IsVisible);

        var host = RequireFloatHost(harness, Float1);
        Assert.IsTrue(host.IsVisible);
        Assert.IsTrue(host.IsHealthy);
        Assert.AreEqual(0, harness.ReleaseFailureCount);
        Assert.AreSame(scene.Host, RequireRegistration(harness.Registry, DockPanelIds.Scene).Host);
        Assert.AreSame(scene.View, host.FindPanel(DockPanelIds.Scene));
        Assert.IsNotNull(host.FindNode(Float1Group));
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreNotEqual(mainToken, GetToken(harness.MainHost));
    }

    [TestMethod]
    public void Tab_context_menu_floats_the_panel_and_float_tabs_do_not_offer_it()
    {
        using var harness = CreateHarness();
        var mainTabs = (TabControl)harness.MainHost.FindNode(SceneGroup)!;
        var sceneTab = mainTabs.Items.OfType<TabItem>().Single(
            item => item.Tag is PanelInstanceId panel && panel == DockPanelIds.Scene);
        Assert.IsNotNull(sceneTab.ContextMenu);
        var floatCommand = (MenuItem)sceneTab.ContextMenu!.Items[0]!;
        Assert.AreEqual("Float Panel", floatCommand.Header);
        var revision = harness.Coordinator.Workspace.Revision;

        floatCommand.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

        Assert.AreEqual(revision + 1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.WindowCount);
        Assert.IsTrue(harness.Windows[0].IsVisible);
        var host = RequireFloatHost(harness, Float1);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreSame(scene.View, host.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        var floatTabs = (TabControl)host.FindNode(Float1Group)!;
        var floatTab = floatTabs.Items.OfType<TabItem>().Single();
        Assert.IsNotNull(floatTab.ContextMenu);
        CollectionAssert.AreEqual(
            new[] { "Hide Panel", "Close Panel" },
            floatTab.ContextMenu!.Items.OfType<MenuItem>()
                .Select(item => (string)item.Header!)
                .ToArray());
    }

    [TestMethod]
    public void Window_bounds_changes_update_the_model_without_retokenizing()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var host = RequireFloatHost(harness, Float1);
        var window = harness.Windows[0];
        var floatToken = GetToken(host);
        var mainToken = GetToken(harness.MainHost);
        var node = host.FindNode(Float1Group);
        var view = host.FindPanel(DockPanelIds.Scene);

        window.MoveTo(MovedBounds, maximized: true);

        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(MovedBounds, harness.Coordinator.Workspace.FloatingRoots[0].Bounds);
        Assert.IsTrue(harness.Coordinator.Workspace.FloatingRoots[0].Maximized);
        Assert.AreEqual(floatToken, GetToken(host));
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreSame(node, host.FindNode(Float1Group));
        Assert.AreSame(view, host.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void System_close_redocks_reuses_the_view_and_closes_the_window_once()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var window = harness.Windows[0];
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

        window.RequestSystemClose();

        Assert.IsFalse(window.IsVisible);
        Assert.AreEqual(1, window.CloseCount);
        Assert.AreEqual(1, window.DisposeCount);
        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.AreSame(
            scene.Host,
            RequireRegistration(harness.Registry, DockPanelIds.Scene).Host);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        var bottom = harness.Coordinator.Workspace.MainRoot.FindGroup(
            new DockNodeId("dock.tabs.left"));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
            bottom!.Panels.ToArray());
    }

    [TestMethod]
    public void Canceled_system_close_keeps_the_model_and_the_window_open()
    {
        var wrapper = new SwitchableMainHost();
        using var harness = CreateHarness(mainHost: wrapper);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var window = harness.Windows[0];
        var host = RequireFloatHost(harness, Float1);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var revision = harness.Coordinator.Workspace.Revision;

        wrapper.FailPrepare = true;
        window.RequestSystemClose();

        Assert.IsTrue(window.IsVisible);
        Assert.AreEqual(0, window.CloseCount);
        Assert.AreEqual(0, window.DisposeCount);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.IsTrue(host.IsHealthy);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);

        wrapper.FailPrepare = false;
        window.RequestSystemClose();

        Assert.IsFalse(window.IsVisible);
        Assert.AreEqual(1, window.DisposeCount);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
    }

    [TestMethod]
    public void Show_failure_keeps_the_committed_root_and_retry_restores_the_window()
    {
        using var harness = CreateHarness();
        Assert.AreEqual(0, harness.Windows.Count);
        harness.FailNextShow = true;

        var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsFalse(result.Success);
        Assert.IsTrue(result.ModelCommitted);
        Assert.AreEqual(DockHostError.ShowFailed, result.Host.Error);
        Assert.AreEqual(1, harness.WindowCount);
        var window = harness.Windows[0];
        Assert.IsFalse(window.IsVisible);
        Assert.AreEqual(0, window.CloseCount);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        var host = RequireFloatHost(harness, Float1);
        Assert.IsFalse(host.IsHealthy);
        Assert.IsTrue(harness.Coordinator.HasDegradedHosts);
        Assert.AreEqual(4, harness.Views.CallCount);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var mainToken = GetToken(harness.MainHost);

        harness.FailNextShow = false;
        window.ThrowOnShow = false;
        var retried = harness.Coordinator.Retry(DockRootHostId.ForFloat(Float1));

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(window.IsVisible);
        Assert.AreEqual(2, window.ShowCount);
        Assert.IsTrue(host.IsHealthy);
        Assert.IsFalse(harness.Coordinator.HasDegradedHosts);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreSame(scene.View, host.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Shutdown_closes_real_windows_without_redocking()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(
                DockPanelIds.Inspector, new DockBounds(240, 160, 800, 600)).Success);
        var windows = harness.Windows.ToArray();
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var inspector = RequireRegistration(harness.Registry, DockPanelIds.Inspector);
        var revision = harness.Coordinator.Workspace.Revision;

        var shutdown = harness.Coordinator.BeginShutdown();

        Assert.IsTrue(shutdown.Success, shutdown.Diagnostic);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(2, harness.Coordinator.Workspace.FloatingRoots.Count);
        foreach (var window in windows)
        {
            Assert.IsFalse(window.IsVisible);
            Assert.AreEqual(1, window.CloseCount);
            Assert.AreEqual(1, window.DisposeCount);
        }

        var late = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsFalse(late.Success);
        Assert.AreEqual(DockHostError.ShuttingDown, late.Host.Error);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);

        harness.Coordinator.Dispose();
        harness.Coordinator.Dispose();

        Assert.AreEqual(1, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(1, ((FakePanelView)inspector.View!).DisposeCount);
        foreach (var window in windows)
        {
            Assert.AreEqual(1, window.CloseCount);
            Assert.AreEqual(1, window.DisposeCount);
        }
    }

    private static Harness CreateHarness(IDockRootHost? mainHost = null)
    {
        var views = new RecordingFactory(instance => new FakePanelView(instance));
        var registry = new DockPanelViewRegistry(views);
        var inner = new DockProjectorRootHost(
            DockRootHostDescriptor.ForMain(),
            registry);
        IDockRootHost main = inner;
        if (mainHost is SwitchableMainHost switchable)
        {
            switchable.Inner = inner;
            main = switchable;
        }

        var harness = new Harness(registry, views, main);
        var coordinator = new DockWorkspaceCoordinator(
            DockWorkspace.CreateDefault(),
            main,
            registry,
            descriptor =>
            {
                var window = new FakeFloatWindow
                {
                    ThrowOnShow = harness.FailNextShow,
                };
                harness.Windows.Add(window);
                var host = new DockWindowRootHost(descriptor, registry, window);
                harness.FloatHosts.Add(host);
                return host;
            });
        harness.Coordinator = coordinator;
        return harness;
    }

    private static Border FindRecoveryBanner(FakeFloatWindow window)
    {
        var shell = (Grid)window.Content!;
        return (Border)shell.Children.First(
            control => control is Border { Name: "DockFloatRecovery" });
    }

    private static DockWindowRootHost RequireFloatHost(Harness harness, DockFloatRootId rootId)
    {
        var host = harness.Coordinator.FindHost(DockRootHostId.ForFloat(rootId));
        Assert.IsNotNull(host, $"Floating host '{rootId}' is not registered.");
        return (DockWindowRootHost)host!;
    }

    private static DockPanelViewRegistration RequireRegistration(
        DockPanelViewRegistry registry,
        PanelInstanceId instance)
    {
        Assert.IsTrue(registry.TryGet(instance, out var registration));
        return registration;
    }

    private static DockApplyToken GetToken(IDockRootHost host)
    {
        Assert.IsTrue(host.TryGetCurrentToken(out var token));
        return token;
    }

    private sealed class Harness(
        DockPanelViewRegistry registry,
        RecordingFactory views,
        IDockRootHost mainHost) : IDisposable
    {
        public DockWorkspaceCoordinator Coordinator { get; set; } = null!;

        public DockPanelViewRegistry Registry { get; } = registry;

        public RecordingFactory Views { get; } = views;

        public IDockRootHost MainHost { get; } = mainHost;

        public List<FakeFloatWindow> Windows { get; } = [];

        public List<DockWindowRootHost> FloatHosts { get; } = [];

        public bool FailNextShow { get; set; }

        public int WindowCount => Windows.Count;

        public int ReleaseFailureCount => Registry.ReleaseFailureCount;

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

    private sealed class FakeFloatWindow : IDockFloatWindow
    {
        public Control? Content { get; set; }

        public string Title { get; set; } = string.Empty;

        public bool IsVisible { get; private set; }

        public bool IsMaximized { get; private set; }

        public DockBounds Bounds { get; private set; }

        public List<DockScreenArea> ScreenList { get; } = [];

        public IReadOnlyList<DockScreenArea> Screens => ScreenList;

        public DockWindowFrame FrameMargins { get; set; } = DockWindowFrame.None;

        public double LastScreenScaling { get; private set; }

        public bool ThrowOnShow { get; set; }

        public int ShowCount { get; private set; }

        public int CloseCount { get; private set; }

        public int DisposeCount { get; private set; }

        public event EventHandler<DockFloatWindowClosingEventArgs>? Closing;

        public event EventHandler? BoundsChanged;

        public void Show(DockBounds bounds, double screenScaling = 0d)
        {
            ShowCount++;
            if (ThrowOnShow)
                throw new InvalidOperationException("show failed");

            LastScreenScaling = screenScaling;
            Bounds = bounds;
            IsVisible = true;
        }

        public void ApplyBounds(DockBounds bounds, bool maximized, double screenScaling = 0d)
        {
            LastScreenScaling = screenScaling;
            Bounds = bounds;
            IsMaximized = maximized;
        }

        public void Close()
        {
            CloseCount++;
            IsVisible = false;
        }

        public void Dispose()
        {
            DisposeCount++;
            IsVisible = false;
            Content = null;
        }

        public void RequestSystemClose()
        {
            if (!IsVisible)
                return;

            var args = new DockFloatWindowClosingEventArgs();
            Closing?.Invoke(this, args);
            if (args.Cancel)
                return;

            CloseCount++;
            IsVisible = false;
        }

        public void MoveTo(DockBounds bounds, bool maximized)
        {
            if (!IsVisible)
                return;

            Bounds = bounds;
            IsMaximized = maximized;
            BoundsChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private sealed class SwitchableMainHost : IDockRootHost
    {
        public IDockRootHost Inner { get; set; } = null!;

        public bool FailPrepare { get; set; }

        public DockRootHostId Id => Inner.Id;

        public DockRootKind Kind => Inner.Kind;

        public bool IsVisible => Inner.IsVisible;

        public bool IsHealthy => Inner.IsHealthy;

        public DockApplyResult? Current => Inner.Current;

        public DockHostResult Prepare(DockTree root)
            => FailPrepare
                ? DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    "main prepare failed")
                : Inner.Prepare(root);

        public DockHostResult Show() => Inner.Show();

        public DockHostResult Apply(DockTree root) => Inner.Apply(root);

        public DockHostResult Retry() => Inner.Retry();

        public DockHostResult Close() => Inner.Close();

        public bool TryGetCurrentToken(out DockApplyToken token)
            => Inner.TryGetCurrentToken(out token);

        public Control? FindNode(DockNodeId id) => Inner.FindNode(id);

        public IDockPanelView? FindPanel(PanelInstanceId instance)
            => Inner.FindPanel(instance);

        public void Dispose() => Inner.Dispose();
    }
}
