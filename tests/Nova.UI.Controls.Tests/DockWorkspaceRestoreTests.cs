using Avalonia.Controls;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class DockWorkspaceRestoreTests
{
    [TestMethod]
    public void Prepare_failure_merges_the_float_inside_the_candidate_without_advancing_revision()
    {
        var (candidate, floatId) = CreateCandidate();
        var revision = candidate.Revision;
        var floatHost = new FakeHost(DockRootHostDescriptor.ForFloat(floatId))
        {
            PrepareResult = DockHostResult.Fail(
                DockHostError.ProjectionFailed,
                "injected prepare failure"),
        };

        var result = DockWorkspaceCoordinator.Restore(
            candidate,
            new FakeHost(DockRootHostDescriptor.ForMain()),
            CreateRegistry(),
            _ => floatHost);

        Assert.IsTrue(result.Success, result.Failure);
        var coordinator = result.Coordinator!;
        try
        {
            CollectionAssert.AreEqual(new[] { floatId }, result.MergedFloatRoots.ToArray());
            Assert.AreEqual(0, coordinator.Workspace.FloatingRoots.Count);
            Assert.AreEqual(revision, coordinator.Workspace.Revision);
            Assert.AreEqual(1, floatHost.DisposeCount);
            Assert.IsTrue(coordinator.FindHost(DockRootHostId.Main)!.IsHealthy);
            Assert.IsTrue(result.Diagnostics.Any(text => text.Contains("could not be restored")));
        }
        finally
        {
            coordinator.Dispose();
        }
    }

    [TestMethod]
    public void Apply_failure_degrades_the_float_host_and_keeps_the_float_root()
    {
        var (candidate, floatId) = CreateCandidate();
        var revision = candidate.Revision;
        var floatHost = new FakeHost(DockRootHostDescriptor.ForFloat(floatId))
        {
            ApplyResult = DockHostResult.Fail(
                DockHostError.ProjectionFailed,
                "injected apply failure"),
        };

        var result = DockWorkspaceCoordinator.Restore(
            candidate,
            new FakeHost(DockRootHostDescriptor.ForMain()),
            CreateRegistry(),
            _ => floatHost);

        Assert.IsTrue(result.Success, result.Failure);
        var coordinator = result.Coordinator!;
        try
        {
            Assert.AreEqual(0, result.MergedFloatRoots.Count);
            Assert.AreEqual(1, coordinator.Workspace.FloatingRoots.Count);
            Assert.AreEqual(floatId, coordinator.Workspace.FloatingRoots[0].Id);
            Assert.AreEqual(revision, coordinator.Workspace.Revision);
            Assert.IsFalse(floatHost.IsHealthy);
            Assert.IsNull(floatHost.Current);
            Assert.IsTrue(result.Diagnostics.Any(text => text.Contains("could not apply")));
        }
        finally
        {
            coordinator.Dispose();
        }
    }

    [TestMethod]
    public void Show_failure_degrades_the_float_host_and_keeps_the_float_root()
    {
        var (candidate, floatId) = CreateCandidate();
        var revision = candidate.Revision;
        var floatHost = new FakeHost(DockRootHostDescriptor.ForFloat(floatId))
        {
            ShowResult = DockHostResult.Fail(DockHostError.ShowFailed, "injected show failure"),
        };

        var result = DockWorkspaceCoordinator.Restore(
            candidate,
            new FakeHost(DockRootHostDescriptor.ForMain()),
            CreateRegistry(),
            _ => floatHost);

        Assert.IsTrue(result.Success, result.Failure);
        var coordinator = result.Coordinator!;
        try
        {
            Assert.AreEqual(0, result.MergedFloatRoots.Count);
            Assert.AreEqual(1, coordinator.Workspace.FloatingRoots.Count);
            Assert.AreEqual(revision, coordinator.Workspace.Revision);
            Assert.IsFalse(floatHost.IsVisible);
            Assert.IsFalse(floatHost.IsHealthy);
            Assert.IsNotNull(floatHost.Current);
            Assert.IsTrue(result.Diagnostics.Any(text => text.Contains("could not show")));
        }
        finally
        {
            coordinator.Dispose();
        }
    }

    private static (DockWorkspace Workspace, DockFloatRootId FloatId) CreateCandidate()
    {
        var floated = DockWorkspace.CreateDefault().FloatPanel(
            DockPanelIds.Console,
            new DockBounds(140, 120, 520, 340));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        var workspace = floated.Workspace!;
        return (workspace, workspace.FloatingRoots[0].Id);
    }

    private static DockPanelViewRegistry CreateRegistry()
        => new(new FakePanelFactory());

    // ADR-011 DM-B03 — 现状基线测量（O-2.5 / O-1）：把一个完全落在所有工作区之外的浮动根
    // 从磁盘形状的快照恢复进运行时，模型校验与恢复管线都不会表达"越界"这件事，几何被原样
    // 采纳。ADR-007 冻结了"模型只含数字几何"，所以可预见的钳制只能落在宿主层；本门禁固定的
    // 是"哪一层没有钳制"，不是断言该行为正确。若 D10-C 在宿主加入 O-2 钳制，此门禁的预期
    // 随之改为"恢复结果被钳制"，由 O-2 验收门禁替换。
    [TestMethod]
    public void DmB03_restore_adopts_a_float_outside_every_work_area_without_reporting_a_defect()
    {
        var offScreen = new DockBounds(20000d, 20000d, 640d, 480d);

        var floated = DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, offScreen);
        Assert.IsTrue(floated.Success, floated.Diagnostic);

        // Round-trip through the persisted snapshot shape so the gate exercises the
        // load path, not only the in-memory model the test just built.
        var decoded = DockWorkspaceSnapshotCodec.ToWorkspace(
            DockWorkspaceSnapshotCodec.FromWorkspace(floated.Workspace!));
        Assert.IsTrue(decoded.IsValid, decoded.Diagnostic);
        var candidate = decoded.Workspace!;

        var validation = candidate.Validate();
        Assert.IsTrue(validation.IsValid, validation.Summary);
        Assert.AreEqual(
            offScreen,
            candidate.FloatingRoots.Single().RestoreBounds,
            "The restore candidate must not rewrite off-screen geometry on the way in.");

        var result = DockWorkspaceCoordinator.Restore(
            candidate,
            new FakeHost(DockRootHostDescriptor.ForMain()),
            CreateRegistry(),
            descriptor => new FakeHost(descriptor));

        Assert.IsTrue(result.Success, result.Failure);
        var coordinator = result.Coordinator!;
        try
        {
            Assert.AreEqual(0, result.MergedFloatRoots.Count);
            var restored = coordinator.Workspace.FloatingRoots.Single();
            Assert.AreEqual(offScreen, restored.Bounds);
            Assert.AreEqual(offScreen, restored.RestoreBounds);
        }
        finally
        {
            coordinator.Dispose();
        }
    }

    private sealed class FakePanelFactory : IDockPanelViewFactory
    {
        public bool CanCreate(PanelInstanceId instance) => true;

        public IDockPanelView Create(PanelInstanceId instance)
            => new FakePanelView();
    }

    private sealed class FakePanelView : IDockPanelView
    {
        public string Title => "Panel";

        public Control Content { get; } = new Border();

        public void Dispose()
        {
        }
    }

    private sealed class FakeHost(DockRootHostDescriptor descriptor) : IDockRootHost
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

        public DockHostResult? ApplyResult { get; set; }

        public DockHostResult? ShowResult { get; set; }

        public int DisposeCount { get; private set; }

        public bool Disposed { get; private set; }

        public DockHostResult Prepare(DockTree root)
            => PrepareResult ?? DockHostResult.Ok();

        public DockHostResult Show()
        {
            if (ShowResult is { } failure)
                return failure;

            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Apply(DockTree root)
        {
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
            if (Current is null)
                return DockHostResult.Fail(DockHostError.RetryFailed, "not applied");

            _projectionHealthy = true;
            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Close()
        {
            IsVisible = false;
            return DockHostResult.Ok();
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
        }
    }
}
