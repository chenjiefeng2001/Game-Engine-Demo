using Avalonia.Controls;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class DockWorkspaceCoordinatorTests
{
    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId LeftGroup = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly DockNodeId Float1Group = new("dock.float.1.tabs.1");
    private static readonly DockNodeId Float2Group = new("dock.float.2.tabs.1");
    private static readonly DockFloatRootId Float1 = new("dock.float.1");
    private static readonly DockFloatRootId Float2 = new("dock.float.2");
    private static readonly DockBounds FloatBounds = new(120, 80, 640, 480);
    private static readonly DockBounds OtherBounds = new(240, 160, 800, 600);
    private static readonly PanelInstanceId UnknownPanel = new("third.party.assets");

    [TestMethod]
    public void Coordinator_projects_the_main_root_with_a_main_host_token()
    {
        using var harness = CreateHarness();

        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.IsTrue(harness.MainHost.IsVisible);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.IsNotNull(harness.MainHost.FindNode(MainSplit));

        var token = GetToken(harness.MainHost);
        Assert.AreEqual(DockRootHostId.Main, token.HostId);
        Assert.AreEqual(harness.Coordinator.Workspace.MainRoot.Digest, token.Digest);
    }

    [TestMethod]
    public void Floating_a_panel_reuses_its_registered_view_and_commits_once()
    {
        using var harness = CreateHarness();
        var sceneBefore = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var mainTokenBefore = GetToken(harness.MainHost);

        var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsTrue(result.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.None, result.ModelError);
        Assert.AreEqual(1, result.Workspace.Revision);
        Assert.AreEqual(1, result.Workspace.FloatingRoots.Count);
        Assert.AreEqual(Float1, result.Workspace.FloatingRoots[0].Id);
        Assert.AreEqual(FloatBounds, result.Workspace.FloatingRoots[0].Bounds);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(4, harness.Registry.Count);

        var sceneAfter = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreSame(sceneBefore.Host, sceneAfter.Host);
        Assert.AreSame(sceneBefore.View, sceneAfter.View);
        Assert.AreEqual(0, ((FakePanelView)sceneAfter.View!).DisposeCount);

        var floatHost = RequireFloatHost(harness, Float1);
        Assert.IsTrue(floatHost.IsVisible);
        Assert.IsTrue(floatHost.IsHealthy);
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(sceneAfter.View, floatHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            sceneAfter.Host,
            FindTabItem(floatHost, Float1Group, DockPanelIds.Scene).Content);

        var mainTokenAfter = GetToken(harness.MainHost);
        Assert.AreEqual(DockRootHostId.Main, mainTokenAfter.HostId);
        Assert.AreNotEqual(mainTokenBefore, mainTokenAfter);
    }

    [TestMethod]
    public void System_close_redocks_the_float_and_reuses_its_view()
    {
        using var harness = CreateHarness();
        var floated = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        var floatHost = RequireFloatHost(harness, Float1);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

        var result = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsTrue(result.ModelCommitted);
        Assert.AreEqual(2, result.Workspace.Revision);
        Assert.AreEqual(0, result.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsFalse(floatHost.IsVisible);
        Assert.IsNull(floatHost.Current);
        Assert.AreEqual(4, harness.Views.CallCount);

        var sceneAfter = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreSame(scene.Host, sceneAfter.Host);
        Assert.AreEqual(0, ((FakePanelView)sceneAfter.View!).DisposeCount);

        var left = result.Workspace.MainRoot.FindGroup(LeftGroup);
        Assert.IsNotNull(left);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
            left!.Panels.ToList());
        Assert.AreSame(
            sceneAfter.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Scene).Content);
    }

    [TestMethod]
    public void Moving_between_floating_roots_reuses_the_view_and_disposes_only_the_source_host()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);

        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var sourceHost = RequireFloatHost(harness, Float1);
        var targetHost = RequireFloatHost(harness, Float2);
        var mainToken = GetToken(harness.MainHost);

        var result = harness.Coordinator.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(Float2Group));

        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.AreEqual(3, result.Workspace.Revision);
        Assert.AreEqual(1, result.Workspace.FloatingRoots.Count);
        Assert.AreEqual(Float2, result.Workspace.FloatingRoots[0].Id);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Scene },
            result.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToList());
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsFalse(sourceHost.IsVisible);
        Assert.IsNull(sourceHost.Current);
        Assert.IsTrue(targetHost.IsHealthy);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreSame(
            scene.Host,
            RequireRegistration(harness.Registry, DockPanelIds.Scene).Host);
        Assert.AreSame(
            scene.Host,
            FindTabItem(targetHost, Float2Group, DockPanelIds.Scene).Content);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
    }

    [TestMethod]
    public void Main_root_rebuild_preserves_float_host_tokens_and_views()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var floatHost = RequireFloatHost(harness, Float1);
        var floatToken = GetToken(floatHost);
        var floatNode = floatHost.FindNode(Float1Group);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var mainToken = GetToken(harness.MainHost);

        var result = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console,
            mainToken);

        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.AreEqual(2, result.Workspace.Revision);

        var mainAfter = GetToken(harness.MainHost);
        Assert.AreEqual(DockRootHostId.Main, mainAfter.HostId);
        Assert.AreNotEqual(mainToken, mainAfter);
        Assert.AreEqual(floatToken, GetToken(floatHost));
        Assert.AreSame(floatNode, floatHost.FindNode(Float1Group));
        Assert.AreSame(scene.View, floatHost.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
    }

    [TestMethod]
    public void Prepare_failure_leaves_the_model_and_existing_hosts_unchanged()
    {
        FakeRootHost? fake = null;
        using var harness = CreateHarness(floatHostFactory: (descriptor, _) =>
        {
            fake = new FakeRootHost(descriptor)
            {
                PrepareResult = DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    "prepare failed"),
            };
            return fake;
        });
        var workspaceBefore = harness.Coordinator.Workspace;
        var tokenBefore = GetToken(harness.MainHost);
        var mainNodeBefore = harness.MainHost.FindNode(MainSplit);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

        var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsFalse(result.Success);
        Assert.IsFalse(result.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.None, result.ModelError);
        Assert.AreEqual(DockHostError.ProjectionFailed, result.Host.Error);
        Assert.AreSame(workspaceBefore, harness.Coordinator.Workspace);
        Assert.AreEqual(0, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.AreEqual(1, fake!.PrepareCount);
        Assert.AreEqual(0, fake.ApplyCount);
        Assert.AreEqual(1, fake.DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(tokenBefore, GetToken(harness.MainHost));
        Assert.IsTrue(harness.MainHost.IsVisible);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreSame(mainNodeBefore, harness.MainHost.FindNode(MainSplit));
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            scene.Host,
            FindTabItem(harness.MainHost, SceneGroup, DockPanelIds.Scene).Content);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
    }

    [TestMethod]
    public void Target_prepare_failure_cancels_system_close_without_touching_the_float_host()
    {
        FaultyMainHost? main = null;
        FakeRootHost? floatHost = null;
        using var harness = CreateHarness(
            mainWrapper: inner => main = new FaultyMainHost(inner),
            floatHostFactory: (descriptor, _) => floatHost = new FakeRootHost(descriptor));
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var workspaceBefore = harness.Coordinator.Workspace;
        var mainNodeBefore = harness.MainHost.FindNode(MainSplit);
        var mainTokenBefore = GetToken(harness.MainHost);
        var hierarchy = RequireRegistration(harness.Registry, DockPanelIds.Hierarchy);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.IsTrue(floatHost!.IsVisible);
        Assert.IsTrue(floatHost.IsHealthy);
        Assert.AreEqual(1, main!.PrepareCount);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);

        main.PrepareResult = DockHostResult.Fail(
            DockHostError.ProjectionFailed,
            "main cannot prepare the merge");

        var canceled = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsFalse(canceled.Success);
        Assert.IsFalse(canceled.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.None, canceled.ModelError);
        Assert.AreEqual(DockHostError.ClosingCanceled, canceled.Host.Error);
        Assert.AreEqual(DockRootHostId.Main, canceled.HostId);
        Assert.AreSame(workspaceBefore, harness.Coordinator.Workspace);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(2, harness.Coordinator.HostCount);
        Assert.AreSame(floatHost, harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsTrue(floatHost.IsVisible);
        Assert.IsTrue(floatHost.IsHealthy);
        Assert.AreEqual(0, floatHost.CloseCount);
        Assert.AreEqual(0, floatHost.DisposeCount);
        Assert.AreEqual(2, main.PrepareCount);
        Assert.IsNotNull(main.LastPrepared);
        CollectionAssert.Contains(
            main.LastPrepared!.EnumeratePanels().ToList(),
            DockPanelIds.Scene);
        CollectionAssert.DoesNotContain(
            harness.Coordinator.Workspace.MainRoot.EnumeratePanels().ToList(),
            DockPanelIds.Scene);
        Assert.AreEqual(mainTokenBefore, GetToken(harness.MainHost));
        Assert.IsTrue(harness.MainHost.IsVisible);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreSame(mainNodeBefore, harness.MainHost.FindNode(MainSplit));
        Assert.AreSame(
            hierarchy.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Hierarchy).Content);
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreSame(
            scene.Host,
            RequireRegistration(harness.Registry, DockPanelIds.Scene).Host);
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.AreEqual(4, harness.Views.CallCount);

        var explicitMerge = harness.Coordinator.MergeFloatRoot(
            Float1,
            new DockTarget.Center(LeftGroup));

        Assert.IsFalse(explicitMerge.Success);
        Assert.IsFalse(explicitMerge.ModelCommitted);
        Assert.AreEqual(DockHostError.ProjectionFailed, explicitMerge.Host.Error);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(0, floatHost.CloseCount);
        Assert.AreEqual(0, floatHost.DisposeCount);

        main.PrepareResult = null;
        var redocked = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsTrue(redocked.Success, redocked.Diagnostic);
        Assert.AreEqual(2, redocked.Workspace.Revision);
        Assert.AreEqual(0, redocked.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, floatHost.CloseCount);
        Assert.AreEqual(1, floatHost.DisposeCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            scene.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Scene).Content);
    }

    // D9-A-10: a system close whose target host is already unavailable is
    // refused before the model moves. Retry brings the target back, and the
    // same request then re-docks the float and restores its view.
    [TestMethod]
    public void Target_unavailable_after_system_close_refuses_the_merge_until_retry()
    {
        FaultyMainHost? main = null;
        using var harness = CreateHarness(mainWrapper: inner => main = new FaultyMainHost(inner));
        Assert.IsTrue(harness.MainHost.IsVisible);
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        var digest = harness.Coordinator.Workspace.Digest;

        main!.Hide();
        main.ShowResult = DockHostResult.Fail(
            DockHostError.ShowFailed,
            "main window unavailable");
        Assert.IsFalse(harness.MainHost.IsHealthy);

        var refused = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsFalse(refused.Success);
        Assert.IsFalse(refused.ModelCommitted, "the gate runs before the model changes");
        Assert.AreEqual(DockHostError.HostUnavailable, refused.Host.Error);
        Assert.AreEqual(1, refused.Workspace.Revision);
        Assert.AreEqual(digest, refused.Workspace.Digest);
        Assert.AreEqual(1, refused.Workspace.FloatingRoots.Count);
        Assert.AreEqual(2, harness.Coordinator.HostCount);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.AreEqual(4, harness.Views.CallCount);

        main.ShowResult = null;
        var retried = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(harness.MainHost.IsVisible);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision, "a retry is not a commit");

        var closed = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsTrue(closed.Success, closed.Diagnostic);
        Assert.AreEqual(2, closed.Workspace.Revision);
        Assert.AreEqual(0, closed.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            scene.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Scene).Content);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Host_creation_failure_does_not_change_the_model()
    {
        using (var harness = CreateHarness(floatHostFactory: (_, _) =>
            throw new InvalidOperationException("host unavailable")))
        {
            var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

            Assert.IsFalse(result.Success);
            Assert.IsFalse(result.ModelCommitted);
            Assert.AreEqual(DockHostError.CreationFailed, result.Host.Error);
            StringAssert.Contains(result.Diagnostic, "host unavailable");
            Assert.AreEqual(0, harness.Coordinator.Workspace.Revision);
            Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
            Assert.AreEqual(1, harness.Coordinator.HostCount);
            Assert.AreEqual(4, harness.Views.CallCount);
        }

        FakeRootHost? invalid = null;
        using (var harness = CreateHarness(floatHostFactory: (_, _) =>
        {
            invalid = new FakeRootHost(
                DockRootHostDescriptor.ForFloat(new DockFloatRootId("dock.float.9")));
            return invalid;
        }))
        {
            var result = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

            Assert.IsFalse(result.Success);
            Assert.IsFalse(result.ModelCommitted);
            Assert.AreEqual(DockHostError.CreationFailed, result.Host.Error);
            Assert.AreEqual(1, invalid!.DisposeCount);
            Assert.AreEqual(1, harness.Coordinator.HostCount);
        }
    }

    [TestMethod]
    public void Show_failure_commits_the_model_and_redocking_recovers_the_view()
    {
        FakeRootHost? fake = null;
        using var harness = CreateHarness(floatHostFactory: (descriptor, _) =>
        {
            fake = new FakeRootHost(descriptor)
            {
                ShowResult = DockHostResult.Fail(
                    DockHostError.ShowFailed,
                    "window unavailable"),
            };
            return fake;
        });
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

        var floated = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsFalse(floated.Success);
        Assert.IsTrue(floated.ModelCommitted);
        Assert.AreEqual(DockHostError.ShowFailed, floated.Host.Error);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(1, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, fake!.ShowCount);
        Assert.IsFalse(fake.IsVisible);
        Assert.IsFalse(fake.IsHealthy);
        Assert.IsNotNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);

        var refused = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsFalse(refused.Success, "a degraded workspace refuses the re-dock");
        Assert.IsFalse(refused.ModelCommitted);
        Assert.AreEqual(DockHostError.HostUnavailable, refused.Host.Error);
        Assert.AreEqual(1, refused.Workspace.Revision);
        Assert.AreEqual(1, refused.Workspace.FloatingRoots.Count);

        fake!.ShowResult = null;
        var retried = harness.Coordinator.Retry(DockRootHostId.ForFloat(Float1));

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(fake.IsVisible);
        Assert.IsTrue(fake.IsHealthy);
        Assert.AreEqual(2, fake.ShowCount, "the retry showed the same host");
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision, "a retry is not a commit");

        var recovered = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsTrue(recovered.Success, recovered.Diagnostic);
        Assert.IsTrue(recovered.ModelCommitted);
        Assert.AreEqual(2, recovered.Workspace.Revision);
        Assert.AreEqual(0, recovered.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, fake.CloseCount);
        Assert.AreEqual(1, fake.DisposeCount);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            scene.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Scene).Content);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Model_failure_cancels_close_requests_and_hosts_are_destroyed_only_after_commit()
    {
        FakeRootHost? fake = null;
        using var harness = CreateHarness(floatHostFactory: (descriptor, _) =>
        {
            fake = new FakeRootHost(descriptor);
            return fake;
        });
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var revision = harness.Coordinator.Workspace.Revision;

        var missingClose = harness.Coordinator.RequestCloseFloatRoot(
            new DockFloatRootId("dock.float.99"));

        Assert.IsFalse(missingClose.Success);
        Assert.IsFalse(missingClose.ModelCommitted);
        Assert.AreEqual(DockHostError.ClosingCanceled, missingClose.Host.Error);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(0, fake!.CloseCount);
        Assert.AreEqual(0, fake.DisposeCount);
        Assert.AreSame(fake, harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));

        var missingTarget = harness.Coordinator.MergeFloatRoot(
            Float1,
            new DockTarget.Center(new DockNodeId("dock.tabs.missing")));

        Assert.IsFalse(missingTarget.Success);
        Assert.IsFalse(missingTarget.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.TargetGone, missingTarget.ModelError);
        Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(0, fake.CloseCount);
        Assert.AreEqual(0, fake.DisposeCount);

        long revisionAtDispose = -1;
        var floatsAtDispose = -1;
        fake.OnDispose = () =>
        {
            revisionAtDispose = harness.Coordinator.Workspace.Revision;
            floatsAtDispose = harness.Coordinator.Workspace.FloatingRoots.Count;
        };

        var redocked = harness.Coordinator.ReDockFloatRoot(Float1);

        Assert.IsTrue(redocked.Success, redocked.Diagnostic);
        Assert.AreEqual(revision + 1, revisionAtDispose);
        Assert.AreEqual(0, floatsAtDispose);
        Assert.AreEqual(1, fake.CloseCount);
        Assert.AreEqual(1, fake.DisposeCount);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
    }

    [TestMethod]
    public void Explicit_close_releases_only_the_closed_panels()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(harness.Coordinator.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(Float1Group)).Success);

        var floatHost = RequireFloatHost(harness, Float1);
        var mainToken = GetToken(harness.MainHost);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var inspector = RequireRegistration(harness.Registry, DockPanelIds.Inspector);
        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);

        var first = harness.Coordinator.ClosePanel(DockPanelIds.Scene);

        Assert.IsTrue(first.Success, first.Diagnostic);
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector },
            harness.Coordinator.Workspace.FloatingRoots[0].Root.EnumeratePanels().ToArray());
        Assert.AreEqual(1, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(0, ((FakePanelView)inspector.View!).DisposeCount);
        Assert.AreEqual(3, harness.Registry.Count);
        Assert.IsTrue(floatHost.IsHealthy);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));

        var second = harness.Coordinator.ClosePanel(DockPanelIds.Inspector);

        Assert.IsTrue(second.Success, second.Diagnostic);
        Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsFalse(floatHost.IsVisible);
        Assert.AreEqual(1, ((FakePanelView)inspector.View!).DisposeCount);
        Assert.AreEqual(2, harness.Registry.Count);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
    }

    [TestMethod]
    public void Closing_the_last_main_panel_is_rejected_at_every_host_layer()
    {
        using var harness = CreateHarness();
        foreach (var panel in new[]
                 {
                     DockPanelIds.Scene,
                     DockPanelIds.Inspector,
                     DockPanelIds.Console,
                 })
        {
            Assert.IsTrue(harness.Coordinator.FloatPanel(panel, FloatBounds).Success);
        }

        Assert.AreEqual(1, harness.Coordinator.Workspace.MainRoot.EnumeratePanels().Count());
        Assert.AreEqual(3, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(4, harness.Coordinator.HostCount);
        var workspace = harness.Coordinator.Workspace;
        var mainToken = GetToken(harness.MainHost);
        var floatTokens = new List<DockApplyToken>();
        foreach (var root in workspace.FloatingRoots)
        {
            var host = harness.Coordinator.FindHost(DockRootHostId.ForFloat(root.Id));
            Assert.IsNotNull(host);
            floatTokens.Add(GetToken(host!));
        }

        var closed = harness.Coordinator.ClosePanel(DockPanelIds.Hierarchy);

        Assert.IsFalse(closed.Success);
        Assert.IsFalse(closed.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.WouldEmptyMainRoot, closed.ModelError);
        Assert.AreSame(workspace, harness.Coordinator.Workspace);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreEqual(4, harness.Registry.Count);

        var floated = harness.Coordinator.MovePanel(
            DockPanelIds.Hierarchy,
            new DockTarget.Float(FloatBounds));

        Assert.IsFalse(floated.Success);
        Assert.IsFalse(floated.ModelCommitted);
        Assert.AreEqual(DockWorkspaceError.WouldEmptyMainRoot, floated.ModelError);
        Assert.AreSame(workspace, harness.Coordinator.Workspace);
        Assert.AreEqual(3, harness.Coordinator.Workspace.FloatingRoots.Count);
        Assert.AreEqual(4, harness.Coordinator.HostCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        for (var index = 0; index < workspace.FloatingRoots.Count; index++)
        {
            var host = harness.Coordinator.FindHost(
                DockRootHostId.ForFloat(workspace.FloatingRoots[index].Id));
            Assert.IsNotNull(host);
            Assert.AreEqual(floatTokens[index], GetToken(host!));
        }
    }

    [TestMethod]
    public void Bounds_updates_commit_without_rebuilding_hosts_or_tokens()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var floatHost = RequireFloatHost(harness, Float1);
        var mainToken = GetToken(harness.MainHost);
        var floatToken = GetToken(floatHost);
        var mainNode = harness.MainHost.FindNode(MainSplit);
        var floatNode = floatHost.FindNode(Float1Group);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var workspace = harness.Coordinator.Workspace;
        var mainDigest = workspace.MainRoot.Digest;
        var floatDigest = workspace.FloatingRoots[0].Root.Digest;
        var workspaceDigest = workspace.Digest;

        var updated = harness.Coordinator.UpdateFloatBounds(
            Float1,
            OtherBounds,
            maximized: true);

        Assert.IsTrue(updated.Success, updated.Diagnostic);
        Assert.AreEqual(2, updated.Workspace.Revision);
        Assert.AreEqual(OtherBounds, updated.Workspace.FloatingRoots[0].Bounds);
        Assert.IsTrue(updated.Workspace.FloatingRoots[0].Maximized);
        Assert.AreEqual(mainDigest, updated.Workspace.MainRoot.Digest);
        Assert.AreEqual(floatDigest, updated.Workspace.FloatingRoots[0].Root.Digest);
        Assert.AreNotEqual(workspaceDigest, updated.Workspace.Digest);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreEqual(floatToken, GetToken(floatHost));
        Assert.AreSame(mainNode, harness.MainHost.FindNode(MainSplit));
        Assert.AreSame(floatNode, floatHost.FindNode(Float1Group));
        Assert.AreSame(scene.View, floatHost.FindPanel(DockPanelIds.Scene));
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsTrue(floatHost.IsVisible);

        var invalid = harness.Coordinator.UpdateFloatBounds(
            Float1,
            new DockBounds(0, 0, 0, 100),
            maximized: false);

        Assert.IsFalse(invalid.Success);
        Assert.AreEqual(DockWorkspaceError.InvalidModel, invalid.ModelError);
        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(floatToken, GetToken(floatHost));
    }

    // D9-A-10: while one float host is degraded, a move into that host is
    // refused before the model changes; retrying the degraded host leaves the
    // other hosts, their projections and the revision exactly where they were.
    [TestMethod]
    public void Retry_recovers_only_the_degraded_float_host()
    {
        var failScene = new TestInterceptor();
        using var harness = CreateHarness(floatHostFactory: (descriptor, registry) =>
            new DockProjectorRootHost(
                descriptor,
                registry,
                descriptor.Id == DockRootHostId.ForFloat(Float1) ? failScene : null));

        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Center(Float1Group)).Success);
        var float1Host = RequireFloatHost(harness, Float1);
        var float2Host = RequireFloatHost(harness, Float2);
        Assert.IsTrue(float1Host.IsVisible);
        Assert.IsTrue(float2Host.IsVisible);

        failScene.AlwaysFail = true;
        var degraded = harness.Coordinator.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Center(Float1Group, 0));

        Assert.IsFalse(degraded.Success);
        Assert.IsTrue(degraded.ModelCommitted, "the reorder committed before the projection failed");
        Assert.AreEqual(DockHostError.ProjectionFailed, degraded.Host.Error);
        Assert.IsFalse(float1Host.IsHealthy);
        Assert.IsTrue(float1Host.IsVisible, "the window is still open, its projection is not");
        var degradedRevision = harness.Coordinator.Workspace.Revision;
        var degradedDigest = harness.Coordinator.Workspace.Digest;
        var mainToken = GetToken(harness.MainHost);
        var float2Token = GetToken(float2Host);
        var float2Node = float2Host.FindNode(Float2Group);

        var blocked = harness.Coordinator.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(Float1Group));

        Assert.IsFalse(blocked.Success);
        Assert.IsFalse(blocked.ModelCommitted, "the degraded gate runs before the model changes");
        Assert.AreEqual(DockHostError.HostUnavailable, blocked.Host.Error);
        Assert.AreEqual(degradedRevision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(degradedDigest, harness.Coordinator.Workspace.Digest);
        Assert.IsNotNull(float2Host.FindPanel(DockPanelIds.Inspector));

        var geometry = harness.Coordinator.UpdateFloatBounds(
            Float1,
            OtherBounds,
            maximized: false);

        Assert.IsFalse(geometry.Success, "a degraded workspace refuses a float geometry update");
        Assert.IsFalse(geometry.ModelCommitted, "geometry is not written while degraded");
        Assert.AreEqual(DockHostError.HostUnavailable, geometry.Host.Error);
        Assert.AreEqual(degradedRevision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(degradedDigest, harness.Coordinator.Workspace.Digest);

        failScene.AlwaysFail = false;
        var retried = harness.Coordinator.Retry(DockRootHostId.ForFloat(Float1));

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(float1Host.IsHealthy);
        Assert.IsTrue(float1Host.IsVisible);
        Assert.IsNotNull(float1Host.FindPanel(DockPanelIds.Scene));
        Assert.IsNotNull(float1Host.FindPanel(DockPanelIds.Console));
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreSame(
            scene.Host,
            FindTabItem(float1Host, Float1Group, DockPanelIds.Scene).Content);
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        Assert.AreSame(
            console.Host,
            FindTabItem(float1Host, Float1Group, DockPanelIds.Console).Content);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreEqual(float2Token, GetToken(float2Host));
        Assert.AreSame(float2Node, float2Host.FindNode(Float2Group));
        Assert.AreEqual(degradedRevision, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(degradedDigest, harness.Coordinator.Workspace.Digest);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Failed_retry_keeps_the_model_and_the_degraded_host()
    {
        FakeRootHost? fake = null;
        using var harness = CreateHarness(floatHostFactory: (descriptor, _) =>
        {
            fake = new FakeRootHost(descriptor)
            {
                ApplyResult = DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    "apply failed"),
                RetryResult = DockHostResult.Fail(
                    DockHostError.RetryFailed,
                    "retry failed"),
            };
            return fake;
        });
        var floatHostId = DockRootHostId.ForFloat(Float1);

        var floated = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);

        Assert.IsFalse(floated.Success);
        Assert.IsTrue(floated.ModelCommitted);
        Assert.IsFalse(fake!.IsHealthy);
        var workspace = harness.Coordinator.Workspace;
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        Assert.AreEqual(1, workspace.Revision);

        var failedRetry = harness.Coordinator.Retry(floatHostId);

        Assert.IsFalse(failedRetry.Success);
        Assert.IsFalse(failedRetry.ModelCommitted);
        Assert.AreEqual(DockHostError.RetryFailed, failedRetry.Host.Error);
        Assert.AreSame(workspace, harness.Coordinator.Workspace);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.IsFalse(fake.IsHealthy);
        Assert.AreSame(fake, harness.Coordinator.FindHost(floatHostId));
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);

        fake.RetryResult = null;
        var recovered = harness.Coordinator.Retry(floatHostId);

        Assert.IsTrue(recovered.Success, recovered.Diagnostic);
        Assert.IsTrue(fake.IsHealthy);
        Assert.IsTrue(fake.IsVisible);
        Assert.AreEqual(floatHostId, GetToken(fake).HostId);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Stale_cross_host_and_foreign_node_tokens_are_rejected()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var floatHost = RequireFloatHost(harness, Float1);
        var floatHostId = DockRootHostId.ForFloat(Float1);
        var mainToken = GetToken(harness.MainHost);
        var floatToken = GetToken(floatHost);
        var workspace = harness.Coordinator.Workspace;

        var crossHost = new DockApplyToken(
            floatHostId,
            mainToken.Generation,
            mainToken.Digest);
        var crossHostResult = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console,
            crossHost);

        Assert.IsFalse(crossHostResult.Success);
        Assert.AreEqual(DockHostError.StaleToken, crossHostResult.Host.Error);

        var staleGeneration = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console,
            mainToken with { Generation = mainToken.Generation - 1 });
        Assert.IsFalse(staleGeneration.Success);
        Assert.AreEqual(DockHostError.StaleToken, staleGeneration.Host.Error);

        var staleDigest = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console,
            mainToken with { Digest = floatToken.Digest });
        Assert.IsFalse(staleDigest.Success);
        Assert.AreEqual(DockHostError.StaleToken, staleDigest.Host.Error);

        var foreignNode = harness.Coordinator.ActivateTab(
            floatHostId,
            BottomGroup,
            DockPanelIds.Scene,
            floatToken);
        Assert.IsFalse(foreignNode.Success);
        Assert.AreEqual(DockHostError.StaleToken, foreignNode.Host.Error);

        var foreignSplit = harness.Coordinator.ResizeSplit(
            DockRootHostId.Main,
            CenterSplit,
            1,
            3,
            floatToken);
        Assert.IsFalse(foreignSplit.Success);
        Assert.AreEqual(DockHostError.StaleToken, foreignSplit.Host.Error);

        Assert.AreSame(workspace, harness.Coordinator.Workspace);
        Assert.AreEqual(1, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(mainToken, GetToken(harness.MainHost));
        Assert.AreEqual(floatToken, GetToken(floatHost));

        var accepted = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console,
            mainToken);

        Assert.IsTrue(accepted.Success, accepted.Diagnostic);
        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(floatToken, GetToken(floatHost));
    }

    [TestMethod]
    public void Host_failure_after_system_close_keeps_the_model_and_recovers_with_retry()
    {
        var mainFailure = new TestInterceptor();
        using var harness = CreateHarness(mainInterceptor: mainFailure);
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var floatHost = RequireFloatHost(harness, Float1);

        mainFailure.AlwaysFail = true;
        var closed = harness.Coordinator.RequestCloseFloatRoot(Float1);

        Assert.IsFalse(closed.Success);
        Assert.IsTrue(closed.ModelCommitted);
        Assert.AreEqual(DockHostError.ProjectionFailed, closed.Host.Error);
        Assert.AreEqual(DockRootHostId.Main, closed.HostId);
        Assert.AreEqual(2, closed.Workspace.Revision);
        Assert.AreEqual(0, closed.Workspace.FloatingRoots.Count);
        Assert.IsFalse(floatHost.IsVisible);
        Assert.IsFalse(harness.MainHost.IsHealthy);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);

        mainFailure.AlwaysFail = false;
        var retried = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(
            scene.Host,
            FindTabItem(harness.MainHost, LeftGroup, DockPanelIds.Scene).Content);
        Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
        Assert.AreEqual(4, harness.Views.CallCount);
    }

    [TestMethod]
    public void Shutdown_closes_float_hosts_without_redocking_and_blocks_late_closes()
    {
        var harness = CreateHarness();
        try
        {
            Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
            Assert.IsTrue(
                harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
            var float1Host = RequireFloatHost(harness, Float1);
            var float2Host = RequireFloatHost(harness, Float2);
            var views = RequireViews(
                harness,
                DockPanelIds.Hierarchy,
                DockPanelIds.Scene,
                DockPanelIds.Inspector,
                DockPanelIds.Console);
            var revision = harness.Coordinator.Workspace.Revision;

            var shutdown = harness.Coordinator.BeginShutdown();

            Assert.IsTrue(shutdown.Success, shutdown.Diagnostic);
            Assert.IsTrue(harness.Coordinator.IsShuttingDown);
            Assert.AreEqual(1, harness.Coordinator.HostCount);
            Assert.IsFalse(float1Host.IsVisible);
            Assert.IsFalse(float2Host.IsVisible);
            Assert.IsNull(float1Host.Current);
            Assert.IsNull(float2Host.Current);
            Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);
            Assert.AreEqual(2, harness.Coordinator.Workspace.FloatingRoots.Count);

            var late = harness.Coordinator.RequestCloseFloatRoot(Float1);

            Assert.IsFalse(late.Success);
            Assert.IsFalse(late.ModelCommitted);
            Assert.AreEqual(DockHostError.ShuttingDown, late.Host.Error);
            Assert.AreEqual(revision, harness.Coordinator.Workspace.Revision);

            harness.Coordinator.Dispose();

            Assert.IsTrue(harness.Coordinator.IsDisposed);
            Assert.AreEqual(0, harness.Registry.Count);
            foreach (var view in views)
                Assert.AreEqual(1, view.DisposeCount);

            harness.Coordinator.Dispose();
            foreach (var view in views)
                Assert.AreEqual(1, view.DisposeCount);

            Assert.ThrowsException<ObjectDisposedException>(
                () => harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds));
        }
        finally
        {
            harness.Dispose();
        }
    }

    [TestMethod]
    public void Panel_factory_failure_is_owned_by_the_registry_and_retry_recovers_it()
    {
        var failConsole = true;
        var views = new RecordingFactory(instance =>
        {
            if (instance == DockPanelIds.Console && failConsole)
                throw new InvalidOperationException("console unavailable");
            return new FakePanelView($"View {instance}");
        });
        using var harness = CreateHarness(views);

        Assert.AreEqual(4, views.CallCount);
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.IsTrue(harness.Registry.TryGet(DockPanelIds.Console, out var failed));
        Assert.IsTrue(failed.Failed);
        Assert.IsNotNull(harness.MainHost.Current);
        CollectionAssert.Contains(
            harness.MainHost.Current!.PanelFailures.ToList(),
            DockPanelIds.Console);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);
        var hierarchy = RequireRegistration(harness.Registry, DockPanelIds.Hierarchy);
        var mainToken = GetToken(harness.MainHost);

        failConsole = false;
        var retried = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.AreEqual(5, views.CallCount);
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.IsTrue(harness.Registry.TryGet(DockPanelIds.Console, out var recovered));
        Assert.IsFalse(recovered.Failed);
        Assert.IsNotNull(recovered.View);
        Assert.AreEqual(0, harness.MainHost.Current!.PanelFailures.Count);
        Assert.AreSame(scene.View, harness.MainHost.FindPanel(DockPanelIds.Scene));
        Assert.AreSame(hierarchy.View, harness.MainHost.FindPanel(DockPanelIds.Hierarchy));
        var retriedToken = GetToken(harness.MainHost);
        Assert.AreNotEqual(mainToken.Generation, retriedToken.Generation);
        Assert.AreEqual(DockRootHostId.Main, retriedToken.HostId);
        Assert.AreEqual(
            harness.Coordinator.Workspace.MainRoot.Digest,
            retriedToken.Digest);

        var repeated = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(repeated.Success);
        Assert.AreEqual(5, views.CallCount);
        Assert.AreEqual(retriedToken, GetToken(harness.MainHost));
    }

    [TestMethod]
    public void Close_and_release_failures_are_reported_after_the_model_commit()
    {
        FakeRootHost? fake = null;
        using (var harness = CreateHarness(floatHostFactory: (descriptor, _) =>
        {
            fake = new FakeRootHost(descriptor)
            {
                CloseResult = DockHostResult.Fail(
                    DockHostError.CloseFailed,
                    "close failed"),
            };
            return fake;
        }))
        {
            Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
            var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

            var closed = harness.Coordinator.CloseFloatRoot(Float1);

            Assert.IsFalse(closed.Success);
            Assert.IsTrue(closed.ModelCommitted);
            Assert.AreEqual(DockHostError.CloseFailed, closed.Host.Error);
            Assert.AreEqual(2, harness.Coordinator.Workspace.Revision);
            Assert.AreEqual(0, harness.Coordinator.Workspace.FloatingRoots.Count);
            Assert.AreEqual(1, fake!.CloseCount);
            Assert.AreEqual(1, fake.DisposeCount);
            Assert.AreEqual(3, harness.Registry.Count);
            Assert.AreEqual(1, ((FakePanelView)scene.View!).DisposeCount);
        }

        var views = new RecordingFactory(instance => new FakePanelView(
            $"View {instance}",
            throwOnDispose: instance == DockPanelIds.Console));
        using (var harness = CreateHarness(views))
        {
            var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
            var inspector = RequireRegistration(harness.Registry, DockPanelIds.Inspector);

            var closed = harness.Coordinator.ClosePanel(DockPanelIds.Console);

            Assert.IsFalse(closed.Success);
            Assert.IsTrue(closed.ModelCommitted);
            Assert.AreEqual(DockHostError.CloseFailed, closed.Host.Error);
            Assert.AreEqual(1, harness.Registry.ReleaseFailureCount);
            StringAssert.Contains(
                harness.Registry.LastDiagnostic ?? string.Empty,
                "editor.console");
            Assert.AreEqual(3, harness.Registry.Count);
            Assert.AreEqual(1, ((FakePanelView)console.View!).DisposeCount);
            Assert.AreEqual(0, ((FakePanelView)inspector.View!).DisposeCount);
            Assert.IsTrue(harness.MainHost.IsHealthy);
        }
    }

    [TestMethod]
    public void Hiding_a_panel_unmounts_and_releases_its_view_and_showing_recreates_it()
    {
        using var harness = CreateHarness();
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var view = (FakePanelView)console.View!;
        Assert.AreEqual(4, harness.Views.CallCount);

        var hidden = harness.Coordinator.HidePanel(DockPanelIds.Console);

        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        Assert.IsTrue(hidden.ModelCommitted);
        var group = harness.Coordinator.Workspace.MainRoot.FindGroup(BottomGroup)!;
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector },
            group.VisiblePanels.ToArray());
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Console },
            group.HiddenPanels.ToArray());
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            group.Panels.ToArray());
        Assert.AreEqual(DockPanelIds.Inspector, group.ActivePanel);
        Assert.AreEqual(3, harness.Registry.Count);
        Assert.IsFalse(harness.Registry.TryGet(DockPanelIds.Console, out _));
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(
            1,
            ((TabControl)harness.MainHost.FindNode(BottomGroup)!).Items.Count);
        Assert.AreEqual(1, view.DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);

        var repeated = harness.Coordinator.HidePanel(DockPanelIds.Console);

        Assert.IsFalse(repeated.Success);
        Assert.AreEqual(DockWorkspaceError.AlreadyInState, repeated.ModelError);
        Assert.AreEqual(1, view.DisposeCount);

        var shown = harness.Coordinator.ShowPanel(DockPanelIds.Console);

        Assert.IsTrue(shown.Success, shown.Diagnostic);
        group = harness.Coordinator.Workspace.MainRoot.FindGroup(BottomGroup)!;
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            group.VisiblePanels.ToArray());
        Assert.AreEqual(0, group.HiddenPanels.Count);
        Assert.AreEqual(DockPanelIds.Inspector, group.ActivePanel);
        Assert.AreEqual(5, harness.Views.CallCount);
        var recreated = RequireRegistration(harness.Registry, DockPanelIds.Console);
        Assert.AreNotSame(view, recreated.View);
        Assert.AreEqual(0, ((FakePanelView)recreated.View!).DisposeCount);
        Assert.AreSame(
            recreated.Host,
            FindTabItem(harness.MainHost, BottomGroup, DockPanelIds.Console).Content);
        Assert.AreEqual(
            2,
            ((TabControl)harness.MainHost.FindNode(BottomGroup)!).Items.Count);
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    [TestMethod]
    public void Closing_a_hidden_panel_does_not_release_its_view_twice()
    {
        using var harness = CreateHarness();
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var view = (FakePanelView)console.View!;

        Assert.IsTrue(harness.Coordinator.HidePanel(DockPanelIds.Console).Success);
        var hiddenRevision = harness.Coordinator.Workspace.Revision;

        var closed = harness.Coordinator.ClosePanel(DockPanelIds.Console);

        Assert.IsTrue(closed.Success, closed.Diagnostic);
        Assert.AreEqual(hiddenRevision + 1, closed.Workspace.Revision);
        Assert.IsFalse(
            harness.Coordinator.Workspace.EnumeratePanels().Contains(DockPanelIds.Console));
        var group = harness.Coordinator.Workspace.MainRoot.FindGroup(BottomGroup)!;
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector },
            group.Panels.ToArray());
        Assert.AreEqual(3, harness.Registry.Count);
        Assert.AreEqual(1, view.DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    [TestMethod]
    public void Unresolved_panels_mount_a_placeholder_without_calling_the_factory()
    {
        var tree = DockTree.CreateDefault().HidePanel(DockPanelIds.Console).Tree!;
        var added = tree.AddPanel(UnknownPanel, new DockTarget.Center(BottomGroup));
        Assert.IsTrue(added.Success, added.Diagnostic);
        var workspace = DockWorkspace.Create(
            added.Tree!, Array.Empty<DockFloatRoot>(), revision: 0);
        var views = new RecordingFactory(
            instance => new FakePanelView($"View {instance}"),
            canCreate: instance => instance != UnknownPanel);
        using var harness = CreateHarness(views, workspace: workspace);

        Assert.AreEqual(3, views.CallCount);
        var registration = RequireRegistration(harness.Registry, UnknownPanel);
        Assert.IsNull(registration.View);
        Assert.IsTrue(registration.Unresolved);
        Assert.IsFalse(registration.Failed);
        Assert.AreEqual(DockApplyStatus.Applied, harness.MainHost.Current!.Status);
        Assert.IsTrue(harness.MainHost.Current.IsCurrent);
        Assert.AreEqual(0, harness.MainHost.Current.PanelFailures.Count);
        Assert.IsNull(harness.MainHost.FindPanel(UnknownPanel));
        RequirePlaceholder(harness.MainHost, BottomGroup, UnknownPanel);

        Assert.IsTrue(
            harness.Coordinator.ActivateTab(
                DockRootHostId.Main,
                BottomGroup,
                DockPanelIds.Inspector,
                GetToken(harness.MainHost)).Success);
        var activated = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            UnknownPanel,
            GetToken(harness.MainHost));
        Assert.IsTrue(activated.Success, activated.Diagnostic);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(3, views.CallCount);

        var retried = harness.Coordinator.Retry(DockRootHostId.Main);
        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(3, views.CallCount);
        RequirePlaceholder(harness.MainHost, BottomGroup, UnknownPanel);

        var moved = harness.Coordinator.MovePanel(
            UnknownPanel, new DockTarget.Center(SceneGroup));
        Assert.IsTrue(moved.Success, moved.Diagnostic);
        Assert.AreEqual(3, views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        var sceneGroup = harness.Coordinator.Workspace.MainRoot.FindGroup(SceneGroup)!;
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene, UnknownPanel },
            sceneGroup.VisiblePanels.ToArray());
        RequirePlaceholder(harness.MainHost, SceneGroup, UnknownPanel);

        var hidden = harness.Coordinator.HidePanel(UnknownPanel);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        Assert.IsFalse(harness.Registry.TryGet(UnknownPanel, out _));
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(3, views.CallCount);

        var shown = harness.Coordinator.ShowPanel(UnknownPanel);
        Assert.IsTrue(shown.Success, shown.Diagnostic);
        var restored = RequireRegistration(harness.Registry, UnknownPanel);
        Assert.IsTrue(restored.Unresolved);
        Assert.IsNull(restored.View);
        Assert.AreNotSame(registration.Host, restored.Host);
        RequirePlaceholder(harness.MainHost, SceneGroup, UnknownPanel);
        Assert.AreEqual(3, views.CallCount);

        var closed = harness.Coordinator.ClosePanel(UnknownPanel);
        Assert.IsTrue(closed.Success, closed.Diagnostic);
        Assert.IsFalse(
            harness.Coordinator.Workspace.EnumeratePanels().Contains(UnknownPanel));
        Assert.AreEqual(3, harness.Registry.Count);
        Assert.AreEqual(3, views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    [TestMethod]
    public void Hiding_with_a_failed_projection_keeps_the_model_and_retry_does_not_recreate()
    {
        var interceptor = new TestInterceptor();
        using var harness = CreateHarness(mainInterceptor: interceptor);
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var view = (FakePanelView)console.View!;
        interceptor.AlwaysFail = true;

        var hidden = harness.Coordinator.HidePanel(DockPanelIds.Console);

        Assert.IsTrue(hidden.ModelCommitted);
        Assert.IsFalse(hidden.Success);
        Assert.AreEqual(DockHostError.ProjectionFailed, hidden.Host.Error);
        Assert.AreEqual(1, view.DisposeCount);
        Assert.IsFalse(harness.Registry.TryGet(DockPanelIds.Console, out _));
        Assert.IsFalse(harness.MainHost.IsHealthy);
        Assert.IsTrue(
            harness.Coordinator.Workspace.EnumerateHiddenPanels()
                .Contains(DockPanelIds.Console));

        interceptor.AlwaysFail = false;
        var retry = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(retry.Success, retry.Diagnostic);
        Assert.IsTrue(harness.MainHost.IsHealthy);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(
            1,
            ((TabControl)harness.MainHost.FindNode(BottomGroup)!).Items.Count);
    }

    [TestMethod]
    public void Redocking_a_float_keeps_hidden_identity_without_recreating_its_view()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(
            harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(
            harness.Coordinator.MovePanel(
                DockPanelIds.Console, new DockTarget.Center(Float1Group)).Success);
        var floatHost = RequireFloatHost(harness, Float1);
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var scene = RequireRegistration(harness.Registry, DockPanelIds.Scene);

        Assert.IsTrue(harness.Coordinator.HidePanel(DockPanelIds.Console).Success);

        Assert.AreEqual(1, ((FakePanelView)console.View!).DisposeCount);
        Assert.AreEqual(
            1,
            ((TabControl)floatHost.FindNode(Float1Group)!).Items.Count);
        Assert.IsFalse(harness.Registry.TryGet(DockPanelIds.Console, out _));
        Assert.AreEqual(4, harness.Views.CallCount);

        var redocked = harness.Coordinator.ReDockFloatRoot(Float1);

        Assert.IsTrue(redocked.Success, redocked.Diagnostic);
        Assert.IsNull(harness.Coordinator.FindHost(DockRootHostId.ForFloat(Float1)));
        Assert.IsFalse(floatHost.IsVisible);
        var workspace = harness.Coordinator.Workspace;
        Assert.IsTrue(workspace.EnumerateHiddenPanels().Contains(DockPanelIds.Console));
        Assert.IsFalse(workspace.EnumerateVisiblePanels().Contains(DockPanelIds.Console));
        Assert.IsTrue(workspace.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.IsTrue(workspace.TryLocatePanel(DockPanelIds.Console, out var location));
        var group = workspace.MainRoot.FindGroup(location.GroupId)!;
        Assert.IsTrue(group.HiddenPanels.Contains(DockPanelIds.Console));
        Assert.IsFalse(group.VisiblePanels.Contains(DockPanelIds.Console));
        var mergedTabs = (TabControl)harness.MainHost.FindNode(location.GroupId)!;
        Assert.AreEqual(group.VisiblePanels.Count, mergedTabs.Items.Count);
        Assert.IsFalse(mergedTabs.Items.OfType<TabItem>().Any(
            item => item.Tag is PanelInstanceId panel && panel == DockPanelIds.Console));
        Assert.AreSame(
            scene.View,
            RequireRegistration(harness.Registry, DockPanelIds.Scene).View);
        Assert.AreEqual(0, ((FakePanelView)scene.View!).DisposeCount);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    [TestMethod]
    public void Restoring_the_default_layout_drops_unresolved_and_hidden_identities()
    {
        var tree = DockTree.CreateDefault().HidePanel(DockPanelIds.Console).Tree!;
        var added = tree.AddPanel(UnknownPanel, new DockTarget.Center(BottomGroup));
        Assert.IsTrue(added.Success, added.Diagnostic);
        var workspace = DockWorkspace.Create(
            added.Tree!, Array.Empty<DockFloatRoot>(), revision: 0);
        var views = new RecordingFactory(
            instance => new FakePanelView($"View {instance}"),
            canCreate: instance => instance != UnknownPanel);
        using var harness = CreateHarness(views, workspace: workspace);
        Assert.AreEqual(3, views.CallCount);
        Assert.IsTrue(RequireRegistration(harness.Registry, UnknownPanel).Unresolved);

        var reset = harness.Coordinator.RestoreDefault();

        Assert.IsTrue(reset.Success, reset.Diagnostic);
        CollectionAssert.AreEquivalent(
            new[]
            {
                DockPanelIds.Hierarchy,
                DockPanelIds.Scene,
                DockPanelIds.Inspector,
                DockPanelIds.Console,
            },
            harness.Coordinator.Workspace.EnumeratePanels().ToArray());
        Assert.AreEqual(0, harness.Coordinator.Workspace.EnumerateHiddenPanels().Count());
        Assert.IsFalse(
            harness.Coordinator.Workspace.EnumeratePanels().Contains(UnknownPanel));
        Assert.IsFalse(harness.Registry.TryGet(UnknownPanel, out _));
        Assert.AreEqual(4, harness.Registry.Count);
        Assert.AreEqual(4, views.CallCount);
        Assert.AreEqual(0, harness.Registry.ReleaseFailureCount);
        Assert.IsNotNull(harness.MainHost.FindPanel(DockPanelIds.Console));
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    [TestMethod]
    public void Replacing_the_main_root_releases_panels_that_become_hidden()
    {
        using var harness = CreateHarness();
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var view = (FakePanelView)console.View!;
        var hiddenTree = harness.Coordinator.Workspace.MainRoot
            .HidePanel(DockPanelIds.Console).Tree!;

        var replaced = harness.Coordinator.ReplaceMainRoot(hiddenTree);

        Assert.IsTrue(replaced.Success, replaced.Diagnostic);
        Assert.AreEqual(1, view.DisposeCount);
        Assert.IsFalse(harness.Registry.TryGet(DockPanelIds.Console, out _));
        Assert.IsTrue(
            harness.Coordinator.Workspace.EnumerateHiddenPanels()
                .Contains(DockPanelIds.Console));
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Console));
        Assert.AreEqual(
            1,
            ((TabControl)harness.MainHost.FindNode(BottomGroup)!).Items.Count);
        Assert.AreEqual(4, harness.Views.CallCount);
        Assert.IsTrue(harness.MainHost.IsHealthy);
    }

    // LW-01: the coordinator seeds its baseline from the workspace it was built
    // with, so the first undo lands exactly on the layout the session started from.
    [TestMethod]
    public void The_first_undo_returns_to_the_baseline_layout_and_keeps_the_view()
    {
        using var harness = CreateHarness();
        var baseline = harness.Coordinator.Workspace.Digest;

        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "the baseline alone is not undoable");
        Assert.IsFalse(harness.Coordinator.CanRedoLayout);

        var floated = harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds);
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        Assert.IsTrue(harness.Coordinator.CanUndoLayout, "a committed layout change is undoable");

        var undone = harness.Coordinator.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.IsTrue(undone.ModelCommitted, undone.Diagnostic);
        Assert.AreEqual(baseline, undone.Workspace.Digest, "undo returns the baseline content");
        Assert.AreEqual(2, undone.Workspace.Revision, "a replay is a new commit, not a rewind");
        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "the baseline is the oldest entry");
        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "the undone change stays redoable");
        Assert.AreEqual(1, harness.Coordinator.HostCount, "the replayed floating host is gone");
        Assert.AreEqual(4, harness.Views.CallCount, "the floated view was never released");
        Assert.IsNotNull(harness.MainHost.FindPanel(DockPanelIds.Scene));
    }

    // LW-02: only a committed model may move the cursor. A rejected operation
    // must not append, must not discard the pending redo branch, and must not
    // change the revision.
    [TestMethod]
    public void A_rejected_operation_records_nothing_and_keeps_the_redo_branch()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var undone = harness.Coordinator.UndoLayout();
        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "the undone change waits for a redo");

        var rejected = harness.Coordinator.HidePanel(DockPanelIds.Hierarchy);

        Assert.IsFalse(rejected.ModelCommitted, "hiding a group's only visible panel is refused");
        Assert.AreEqual(DockWorkspaceError.WouldEmptyGroup, rejected.ModelError);
        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "a rejection keeps the pending redo branch");
        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "a rejection records no entry");
        Assert.AreEqual(
            undone.Workspace.Revision,
            harness.Coordinator.Workspace.Revision,
            "a rejection commits nothing");
        Assert.AreEqual(
            undone.Workspace.Digest,
            harness.Coordinator.Workspace.Digest,
            "a rejection leaves the current layout alone");
    }

    // LW-03: tab activation changes committed content but is navigation, so it
    // never becomes an undo step of its own.
    [TestMethod]
    public void Activating_a_tab_does_not_become_an_undo_step()
    {
        using var harness = CreateHarness();
        var before = harness.Coordinator.Workspace;

        var activated = harness.Coordinator.ActivateTab(
            DockRootHostId.Main,
            BottomGroup,
            DockPanelIds.Console);

        Assert.IsTrue(activated.Success, activated.Diagnostic);
        Assert.AreEqual(1, activated.Workspace.Revision, "activation is a commit");
        Assert.AreNotEqual(
            before.Digest,
            activated.Workspace.Digest,
            "the active tab is part of the layout content");
        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "navigation is not an undo step");
        Assert.IsFalse(harness.Coordinator.CanRedoLayout);

        var nothing = harness.Coordinator.UndoLayout();

        Assert.IsFalse(nothing.ModelCommitted, "there is no layout change to undo");
        Assert.AreEqual(DockWorkspaceError.AlreadyInState, nothing.ModelError);
        Assert.AreEqual(
            activated.Workspace.Digest,
            nothing.Workspace.Digest,
            "the refused replay keeps the current layout");
    }

    // LW-04: "model committed, projection failed" is recorded exactly once even
    // though the host degrades, and replay stays unavailable until Retry makes
    // the host healthy again. Retry itself records nothing.
    [TestMethod]
    public void A_degraded_commit_is_recorded_once_and_replay_waits_for_a_healthy_host()
    {
        var interceptor = new TestInterceptor();
        using var harness = CreateHarness(mainInterceptor: interceptor);
        var baseline = harness.Coordinator.Workspace.Digest;

        interceptor.AlwaysFail = true;
        var hidden = harness.Coordinator.HidePanel(DockPanelIds.Console);

        Assert.IsFalse(hidden.Success, hidden.Diagnostic);
        Assert.IsTrue(hidden.ModelCommitted, "the model stays authoritative while the host fails");
        Assert.IsTrue(harness.Coordinator.HasDegradedHosts, "the failed projection degrades the host");
        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "a degraded host blocks undo");
        Assert.IsFalse(harness.Coordinator.CanRedoLayout, "and redo");

        var blocked = harness.Coordinator.UndoLayout();

        Assert.IsFalse(blocked.ModelCommitted, "no replay may commit while degraded");
        Assert.AreEqual(DockHostError.HostUnavailable, blocked.Host.Error);
        Assert.AreEqual(
            hidden.Workspace.Revision,
            harness.Coordinator.Workspace.Revision,
            "the blocked replay leaves the revision alone");

        interceptor.AlwaysFail = false;
        var retried = harness.Coordinator.Retry(DockRootHostId.Main);

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.IsFalse(harness.Coordinator.HasDegradedHosts);
        Assert.IsTrue(harness.Coordinator.CanUndoLayout, "retry restores the replay without a new entry");

        var undone = harness.Coordinator.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.AreEqual(
            baseline,
            undone.Workspace.Digest,
            "the undo target is the baseline, not a retry artifact");
    }

    // LW-05: a replay is a commit, so repeating the same round trip must walk
    // the same fixed set of entries. If a replay appended, the second pass would
    // land on a different digest.
    [TestMethod]
    public void Replaying_a_layout_moves_the_cursor_without_appending_entries()
    {
        using var harness = CreateHarness();
        var baseline = harness.Coordinator.Workspace.Digest;
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
        var newest = harness.Coordinator.Workspace.Digest;

        for (var pass = 1; pass <= 3; pass++)
        {
            var first = harness.Coordinator.UndoLayout();
            Assert.IsTrue(first.Success, $"pass {pass}: {first.Diagnostic}");
            var second = harness.Coordinator.UndoLayout();
            Assert.IsTrue(second.Success, $"pass {pass}: {second.Diagnostic}");
            Assert.AreEqual(baseline, second.Workspace.Digest, $"pass {pass}: two undos reach the baseline");
            Assert.IsFalse(harness.Coordinator.CanUndoLayout, $"pass {pass}: the baseline is oldest");

            var third = harness.Coordinator.RedoLayout();
            Assert.IsTrue(third.Success, $"pass {pass}: {third.Diagnostic}");
            var fourth = harness.Coordinator.RedoLayout();
            Assert.IsTrue(fourth.Success, $"pass {pass}: {fourth.Diagnostic}");
            Assert.AreEqual(newest, fourth.Workspace.Digest, $"pass {pass}: two redos reach the newest layout");
            Assert.IsFalse(harness.Coordinator.CanRedoLayout, $"pass {pass}: replay appends nothing");
        }

        Assert.IsTrue(harness.Coordinator.UndoLayout().Success, "the budget survives the passes");
    }

    // LW-06: the redo branch is dropped only by a new successful commit.
    [TestMethod]
    public void A_new_change_after_undo_discards_the_redo_branch()
    {
        using var harness = CreateHarness();
        var baseline = harness.Coordinator.Workspace.Digest;
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var floated = harness.Coordinator.Workspace.Digest;
        Assert.IsTrue(harness.Coordinator.UndoLayout().Success);

        var hidden = harness.Coordinator.HidePanel(DockPanelIds.Console);

        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        Assert.IsFalse(harness.Coordinator.CanRedoLayout, "the floated layout is no longer redoable");
        Assert.AreNotEqual(floated, harness.Coordinator.Workspace.Digest);

        var redo = harness.Coordinator.RedoLayout();

        Assert.IsFalse(redo.ModelCommitted, "there is nothing left to redo");
        Assert.AreEqual(DockWorkspaceError.AlreadyInState, redo.ModelError);

        var undone = harness.Coordinator.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.AreEqual(baseline, undone.Workspace.Digest, "the truncated branch left the baseline");
        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "the new change is redoable again");

        var redone = harness.Coordinator.RedoLayout();

        Assert.IsTrue(redone.Success, redone.Diagnostic);
        Assert.AreEqual(hidden.Workspace.Digest, redone.Workspace.Digest);
    }

    // LW-07: history holds model identity only. A closed panel comes back
    // through the registry as a brand-new view, never as the disposed one.
    [TestMethod]
    public void Undo_restores_a_closed_panel_through_a_newly_created_view()
    {
        using var harness = CreateHarness();
        var console = RequireRegistration(harness.Registry, DockPanelIds.Console);
        var closedView = (FakePanelView)console.View!;

        var closed = harness.Coordinator.ClosePanel(DockPanelIds.Console);
        Assert.IsTrue(closed.Success, closed.Diagnostic);
        Assert.AreEqual(1, closedView.DisposeCount);
        Assert.IsFalse(harness.Registry.TryGet(DockPanelIds.Console, out _));

        var undone = harness.Coordinator.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.IsTrue(
            undone.Workspace.EnumeratePanels().Contains(DockPanelIds.Console),
            "the closed panel's identity is back in its group");
        Assert.AreEqual(5, harness.Views.CallCount, "the registry creates a fresh view");
        var restored = RequireRegistration(harness.Registry, DockPanelIds.Console);
        Assert.AreNotSame(closedView, restored.View, "a released view is never reused");
        Assert.AreEqual(0, ((FakePanelView)restored.View!).DisposeCount);
        Assert.AreEqual(1, closedView.DisposeCount, "the released view is not disposed twice");
        Assert.IsNotNull(harness.MainHost.FindPanel(DockPanelIds.Console));
    }

    // LW-08: hidden membership and floating geometry are snapshot content, so
    // redo restores both and recreates the host that undo closed.
    [TestMethod]
    public void Redo_restores_hidden_membership_and_float_geometry()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var hidden = harness.Coordinator.HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);

        Assert.IsTrue(harness.Coordinator.UndoLayout().Success, "undo drops the hidden panel");
        Assert.IsTrue(harness.Coordinator.UndoLayout().Success, "undo drops the float");
        Assert.AreEqual(1, harness.Coordinator.HostCount);
        Assert.AreEqual(0, harness.Coordinator.Workspace.EnumerateHiddenPanels().Count());

        Assert.IsTrue(harness.Coordinator.RedoLayout().Success);
        var redone = harness.Coordinator.RedoLayout();

        Assert.IsTrue(redone.Success, redone.Diagnostic);
        Assert.AreEqual(hidden.Workspace.Digest, redone.Workspace.Digest);
        Assert.AreEqual(2, harness.Coordinator.HostCount, "the redo recreated the floating host");
        Assert.AreEqual(1, redone.Workspace.FloatingRoots.Count);
        Assert.AreEqual(FloatBounds, redone.Workspace.FloatingRoots[0].Bounds);
        Assert.IsTrue(
            redone.Workspace.EnumerateHiddenPanels().Contains(DockPanelIds.Console),
            "the hidden panel is back in its group without a mounted view");
        Assert.IsNull(harness.MainHost.FindPanel(DockPanelIds.Console));
        Assert.IsNotNull(RequireFloatHost(harness, Float1).FindPanel(DockPanelIds.Scene));
    }

    // LW-09: an in-progress drag and the shutdown sequence are both hard blocks;
    // neither may commit, record or discard the pending redo branch.
    [TestMethod]
    public void A_drag_or_shutdown_blocks_replay_without_touching_the_history()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Inspector, OtherBounds).Success);
        Assert.IsTrue(harness.Coordinator.UndoLayout().Success);
        var current = harness.Coordinator.Workspace.Digest;
        Assert.IsTrue(harness.Coordinator.CanUndoLayout, "one more layout step is still behind");
        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "the undone float waits for a redo");

        var began = harness.Coordinator.BeginDrag(
            DockRootHostId.Main,
            DockPanelIds.Hierarchy,
            GetToken(harness.MainHost));

        Assert.IsTrue(began.Success, began.Diagnostic);
        Assert.IsFalse(harness.Coordinator.CanRedoLayout, "an open drag session blocks a replay");

        var blocked = harness.Coordinator.RedoLayout();

        Assert.IsFalse(blocked.ModelCommitted, "the blocked replay commits nothing");
        Assert.AreEqual(DockHostError.NoTarget, blocked.Host.Error);
        Assert.AreEqual(current, blocked.Workspace.Digest, "a blocked replay changes nothing");

        harness.Coordinator.CancelDrag();

        Assert.IsTrue(harness.Coordinator.CanRedoLayout, "cancelling the session frees the replay");

        Assert.IsTrue(harness.Coordinator.BeginShutdown().Success);
        Assert.IsFalse(harness.Coordinator.CanUndoLayout, "shutdown blocks undo");
        Assert.IsFalse(harness.Coordinator.CanRedoLayout, "and redo");

        var duringShutdown = harness.Coordinator.RedoLayout();

        Assert.IsFalse(duringShutdown.ModelCommitted, "a shutdown replay commits nothing");
        Assert.AreEqual(DockHostError.ShuttingDown, duringShutdown.Host.Error);
        Assert.AreEqual(current, duringShutdown.Workspace.Digest, "and keeps the redo branch");
    }

    // LW-10: a replay prepares every host before adopting the model, so a
    // prepare failure leaves the workspace, the cursor and the views untouched.
    [TestMethod]
    public void A_replay_that_cannot_prepare_rolls_back_neither_model_nor_cursor()
    {
        FaultyMainHost? faulty = null;
        using var harness = CreateHarness(mainWrapper: inner => faulty = new FaultyMainHost(inner));
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var current = harness.Coordinator.Workspace;

        faulty!.PrepareResult = DockHostResult.Fail(
            DockHostError.ProjectionFailed,
            "injected prepare failure");
        var rejected = harness.Coordinator.UndoLayout();

        Assert.IsFalse(rejected.ModelCommitted, "the replay never committed");
        Assert.AreEqual(DockHostError.ProjectionFailed, rejected.Host.Error);
        Assert.AreEqual(
            current.Digest,
            harness.Coordinator.Workspace.Digest,
            "the floated layout is still the committed model");
        Assert.AreEqual(current.Revision, harness.Coordinator.Workspace.Revision);
        Assert.IsTrue(harness.Coordinator.CanUndoLayout, "the cursor did not move");
        Assert.IsFalse(harness.Coordinator.CanRedoLayout, "a rejected replay records nothing");
        Assert.AreEqual(2, harness.Coordinator.HostCount, "the floating host is still installed");

        faulty.PrepareResult = null;
        var retried = harness.Coordinator.UndoLayout();

        Assert.IsTrue(retried.Success, retried.Diagnostic);
        Assert.AreEqual(1, harness.Coordinator.HostCount, "the retry of the same target now commits");
    }

    // LW-11: one completed geometry update is one entry, and a geometry update
    // that changes nothing is not a commit at all.
    [TestMethod]
    public void A_float_geometry_update_is_one_undo_step_and_a_no_op_is_none()
    {
        using var harness = CreateHarness();
        Assert.IsTrue(harness.Coordinator.FloatPanel(DockPanelIds.Scene, FloatBounds).Success);
        var floated = harness.Coordinator.Workspace.Digest;

        var moved = harness.Coordinator.UpdateFloatBounds(Float1, OtherBounds, maximized: false);

        Assert.IsTrue(moved.Success, moved.Diagnostic);
        Assert.AreEqual(OtherBounds, moved.Workspace.FloatingRoots[0].Bounds);

        var unchanged = harness.Coordinator.UpdateFloatBounds(Float1, OtherBounds, maximized: false);

        Assert.IsFalse(unchanged.ModelCommitted, "identical geometry is not a commit");
        Assert.AreEqual(DockWorkspaceError.AlreadyInState, unchanged.ModelError);

        var undone = harness.Coordinator.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.AreEqual(
            floated,
            undone.Workspace.Digest,
            "the no-op geometry call added no entry");
        Assert.AreEqual(FloatBounds, undone.Workspace.FloatingRoots[0].Bounds);
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
    }

    private static void RequirePlaceholder(
        IDockRootHost host,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var tab = FindTabItem(host, groupId, panel);
        var placeholder = Require<TextBlock>(Require<ContentControl>(tab.Content).Content);
        Assert.AreEqual("DockPanelUnavailable", placeholder.Name);
    }

    private static Harness CreateHarness(
        RecordingFactory? views = null,
        Func<DockRootHostDescriptor, DockPanelViewRegistry, IDockRootHost>? floatHostFactory = null,
        TestInterceptor? mainInterceptor = null,
        Func<IDockRootHost, IDockRootHost>? mainWrapper = null,
        DockWorkspace? workspace = null)
    {
        views ??= new RecordingFactory(instance => new FakePanelView($"View {instance}"));
        var registry = new DockPanelViewRegistry(views);
        IDockRootHost mainHost = new DockProjectorRootHost(
            DockRootHostDescriptor.ForMain(),
            registry,
            mainInterceptor);
        mainHost = mainWrapper?.Invoke(mainHost) ?? mainHost;
        var realFloatHosts = new List<DockProjectorRootHost>();

        DockRootHostFactory factory = descriptor =>
        {
            if (floatHostFactory is not null)
                return floatHostFactory(descriptor, registry);

            var host = new DockProjectorRootHost(descriptor, registry);
            realFloatHosts.Add(host);
            return host;
        };

        var coordinator = new DockWorkspaceCoordinator(
            workspace ?? DockWorkspace.CreateDefault(),
            mainHost,
            registry,
            factory);
        return new Harness(coordinator, registry, views, mainHost, realFloatHosts);
    }

    private static DockProjectorRootHost RequireFloatHost(Harness harness, DockFloatRootId rootId)
    {
        var host = harness.Coordinator.FindHost(DockRootHostId.ForFloat(rootId));
        Assert.IsNotNull(host, $"Floating host '{rootId}' is not registered.");
        return (DockProjectorRootHost)host!;
    }

    private static DockPanelViewRegistration RequireRegistration(
        DockPanelViewRegistry registry,
        PanelInstanceId instance)
    {
        Assert.IsTrue(
            registry.TryGet(instance, out var registration),
            $"Panel '{instance}' is not registered.");
        return registration;
    }

    private static FakePanelView[] RequireViews(Harness harness, params PanelInstanceId[] panels)
        => panels
            .Select(panel => (FakePanelView)RequireRegistration(harness.Registry, panel).View!)
            .ToArray();

    private static DockApplyToken GetToken(IDockRootHost host)
    {
        Assert.IsTrue(
            host.TryGetCurrentToken(out var token),
            $"Host '{host.Id}' has no current token.");
        return token;
    }

    private static TabItem FindTabItem(
        IDockRootHost host,
        DockNodeId groupId,
        PanelInstanceId instance)
    {
        var tabs = host.FindNode(groupId) as TabControl;
        Assert.IsNotNull(tabs, $"Group '{groupId}' is not mounted on host '{host.Id}'.");
        foreach (var candidate in tabs!.Items)
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

    private sealed class Harness(
        DockWorkspaceCoordinator coordinator,
        DockPanelViewRegistry registry,
        RecordingFactory views,
        IDockRootHost mainHost,
        IReadOnlyList<DockProjectorRootHost> realFloatHosts) : IDisposable
    {
        public DockWorkspaceCoordinator Coordinator { get; } = coordinator;

        public DockPanelViewRegistry Registry { get; } = registry;

        public RecordingFactory Views { get; } = views;

        public IDockRootHost MainHost { get; } = mainHost;

        public IReadOnlyList<DockProjectorRootHost> RealFloatHosts { get; } = realFloatHosts;

        public void Dispose() => Coordinator.Dispose();
    }

    private sealed class RecordingFactory(
        Func<PanelInstanceId, IDockPanelView> create,
        Func<PanelInstanceId, bool>? canCreate = null)
        : IDockPanelViewFactory
    {
        public List<PanelInstanceId> Calls { get; } = [];

        public int CallCount => Calls.Count;

        public bool CanCreate(PanelInstanceId instance) => canCreate?.Invoke(instance) ?? true;

        public IDockPanelView Create(PanelInstanceId instance)
        {
            Calls.Add(instance);
            return create(instance);
        }
    }

    private sealed class FakePanelView(string title, bool throwOnDispose = false)
        : IDockPanelView
    {
        public string Title { get; } = title;

        public Control Content { get; } = new Border();

        public int DisposeCount { get; private set; }

        public void Dispose()
        {
            DisposeCount++;
            if (throwOnDispose)
                throw new InvalidOperationException($"View '{Title}' failed to dispose.");
        }
    }

    private sealed class FaultyMainHost(IDockRootHost inner) : IDockRootHost
    {
        private bool _hidden;

        public DockRootHostId Id => inner.Id;

        public DockRootKind Kind => inner.Kind;

        public bool IsVisible => !_hidden && inner.IsVisible;

        public bool IsHealthy => IsVisible && inner.IsHealthy;

        public void Hide() => _hidden = true;

        public DockApplyResult? Current => inner.Current;

        public DockHostResult? PrepareResult { get; set; }

        public DockHostResult? ShowResult { get; set; }

        public int PrepareCount { get; private set; }

        public int ShowCount { get; private set; }

        public DockTree? LastPrepared { get; private set; }

        public DockHostResult Prepare(DockTree root)
        {
            PrepareCount++;
            LastPrepared = root;
            return PrepareResult ?? inner.Prepare(root);
        }

        public DockHostResult Show()
        {
            ShowCount++;
            if (ShowResult is { } failure)
                return failure;

            var result = inner.Show();
            if (result.Success)
                _hidden = false;

            return result;
        }

        public DockHostResult Apply(DockTree root) => inner.Apply(root);

        public DockHostResult Retry() => inner.Retry();

        public DockHostResult Close() => inner.Close();

        public bool TryGetCurrentToken(out DockApplyToken token)
            => inner.TryGetCurrentToken(out token);

        public Control? FindNode(DockNodeId id) => inner.FindNode(id);

        public IDockPanelView? FindPanel(PanelInstanceId instance)
            => inner.FindPanel(instance);

        public void Dispose() => inner.Dispose();
    }

    private sealed class FakeRootHost(DockRootHostDescriptor descriptor) : IDockRootHost
    {
        private long _generation;
        private string? _digest;
        private bool _projectionHealthy;

        public DockRootHostId Id { get; } = descriptor.Id;

        public DockRootKind Kind { get; } = descriptor.Kind;

        public bool IsVisible { get; private set; }

        public bool IsHealthy => !Disposed && _projectionHealthy && IsVisible;

        public DockApplyResult? Current { get; private set; }

        public DockHostResult? PrepareResult { get; set; }

        public DockHostResult? ShowResult { get; set; }

        public DockHostResult? ApplyResult { get; set; }

        public DockHostResult? RetryResult { get; set; }

        public DockHostResult? CloseResult { get; set; }

        public Action? OnDispose { get; set; }

        public int PrepareCount { get; private set; }

        public int ApplyCount { get; private set; }

        public int ShowCount { get; private set; }

        public int RetryCount { get; private set; }

        public int CloseCount { get; private set; }

        public int DisposeCount { get; private set; }

        public bool Disposed { get; private set; }

        public DockTree? PreparedRoot { get; private set; }

        public DockTree? AppliedRoot { get; private set; }

        public DockHostResult Prepare(DockTree root)
        {
            PrepareCount++;
            PreparedRoot = root;
            return PrepareResult ?? DockHostResult.Ok();
        }

        public DockHostResult Show()
        {
            ShowCount++;
            if (ShowResult is { } failure)
                return failure;

            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Apply(DockTree root)
        {
            ApplyCount++;
            AppliedRoot = root;
            if (ApplyResult is { } failure)
            {
                _projectionHealthy = false;
                Current = null;
                return failure;
            }

            _generation++;
            _digest = root.Digest;
            _projectionHealthy = true;
            Current = new DockApplyResult(
                DockApplyStatus.Applied,
                _generation,
                _digest,
                _digest,
                Array.Empty<PanelInstanceId>(),
                Diagnostic: null);
            return DockHostResult.Ok();
        }

        public DockHostResult Retry()
        {
            RetryCount++;
            if (RetryResult is { } failure)
                return failure;

            if (AppliedRoot is { } root)
            {
                _generation++;
                _digest = root.Digest;
                Current = new DockApplyResult(
                    DockApplyStatus.Applied,
                    _generation,
                    _digest,
                    _digest,
                    Array.Empty<PanelInstanceId>(),
                    Diagnostic: null);
            }

            _projectionHealthy = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Close()
        {
            CloseCount++;
            IsVisible = false;
            return CloseResult ?? DockHostResult.Ok();
        }

        public bool TryGetCurrentToken(out DockApplyToken token)
        {
            if (_projectionHealthy && _digest is not null)
            {
                token = new DockApplyToken(Id, _generation, _digest);
                return true;
            }

            token = default;
            return false;
        }

        public Control? FindNode(DockNodeId id) => null;

        public IDockPanelView? FindPanel(PanelInstanceId instance) => null;

        public void Dispose()
        {
            DisposeCount++;
            Disposed = true;
            IsVisible = false;
            _projectionHealthy = false;
            OnDispose?.Invoke();
        }
    }

    private sealed class TestInterceptor : IDockNodeApplyInterceptor
    {
        public bool AlwaysFail { get; set; }

        public int FailureCount { get; private set; }

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            if (!AlwaysFail)
                return;

            FailureCount++;
            throw new InvalidOperationException($"Injected visual failure at '{node}'.");
        }
    }
}
