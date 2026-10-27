using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DockWorkspaceTests
{
    private static readonly DockNodeId LeftGroupId = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroupId = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroupId = new("dock.tabs.bottom");
    private static readonly DockBounds DefaultBounds = new(120d, 90d, 480d, 320d);
    private static readonly DockBounds OtherBounds = new(220d, 140d, 640d, 420d);

    // WX-01: the default workspace is valid, starts at revision 0, and its
    // digest is deterministic and independent of the revision counter.
    [TestMethod]
    public void Wm01_workspace_default_is_valid_and_deterministic()
    {
        var workspace = DockWorkspace.CreateDefault();

        Assert.IsTrue(workspace.Validate().IsValid, workspace.Validate().Summary);
        Assert.AreEqual(0L, workspace.Revision);
        Assert.AreEqual(0, workspace.FloatingRoots.Count);
        Assert.AreEqual(64, workspace.Digest.Length);
        Assert.AreEqual(workspace.MainRoot.Digest, DockWorkspace.CreateDefault().MainRoot.Digest);
        Assert.AreEqual(workspace.Digest, DockWorkspace.CreateDefault().Digest);
        Assert.IsTrue(DockWorkspace.Validate(null).Has("root-missing"));

        var revisioned = DockWorkspace.Create(workspace.MainRoot, revision: 99);
        Assert.AreEqual(99L, revisioned.Revision);
        Assert.AreEqual(workspace.Digest, revisioned.Digest);
    }

    // WM-16: floating then re-docking preserves panel identity, order, active
    // tab and the exact main-root digest that existed before the float.
    [TestMethod]
    public void Wm16_float_and_redock_round_trip_preserves_panel_identity_and_order()
    {
        var original = DockWorkspace.CreateDefault();
        var floated = RequireSuccess(original.FloatPanel(DockPanelIds.Console, DefaultBounds));

        Assert.AreEqual(1L, floated.Revision);
        Assert.AreEqual(1, floated.FloatingRoots.Count);
        var root = floated.FloatingRoots[0];
        Assert.AreEqual("dock.float.1", root.Id.ToString());
        Assert.AreEqual(DefaultBounds, root.Bounds);
        Assert.AreEqual(DefaultBounds, root.RestoreBounds);
        Assert.IsFalse(root.Maximized);
        Assert.AreEqual(new DockReturnTarget(BottomGroupId, 1), root.ReturnTarget);
        Assert.AreEqual("dock.float.1.tabs.1", RequireGroup(root.Root.Root).Id.ToString());
        Assert.AreEqual(DockPanelIds.Console, RequireGroup(root.Root.Root).ActivePanel);
        Assert.AreEqual(3, floated.MainRoot.EnumeratePanels().Count());
        Assert.IsFalse(floated.MainRoot.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.AreEqual(4, floated.EnumeratePanels().Count());
        Assert.AreEqual(4, floated.EnumeratePanels().Distinct().Count());
        Assert.IsTrue(floated.Validate().IsValid, floated.Validate().Summary);

        var restored = RequireSuccess(floated.ReDockFloatRoot(root.Id));
        Assert.AreEqual(2L, restored.Revision);
        Assert.AreEqual(0, restored.FloatingRoots.Count);
        Assert.AreEqual(original.MainRoot.Digest, restored.MainRoot.Digest);
        Assert.AreEqual(original.Digest, restored.Digest);

        var bottom = restored.MainRoot.FindGroup(BottomGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
    }

    // WM-16: the explicit Float target is equivalent to FloatPanel and cannot
    // be used to float an already-floating panel again.
    [TestMethod]
    public void Wm16_move_panel_with_float_target_matches_float_panel()
    {
        var workspace = DockWorkspace.CreateDefault();

        var viaTarget = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Float(DefaultBounds)));
        var direct = RequireSuccess(workspace.FloatPanel(DockPanelIds.Console, DefaultBounds));

        Assert.AreEqual(direct.Revision, viaTarget.Revision);
        Assert.AreEqual(direct.Digest, viaTarget.Digest);
        Assert.AreEqual(
            direct.FloatingRoots[0].ReturnTarget,
            viaTarget.FloatingRoots[0].ReturnTarget);

        AssertWorkspaceFailure(
            viaTarget.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Float(OtherBounds)),
            DockWorkspaceError.InvalidSource,
            viaTarget,
            "float an already-floating panel");
    }

    // WM-16: moving the last floating panel to a main-root edge removes the
    // floating root and allocates new nodes in the main namespace.
    [TestMethod]
    public void Wm16_cross_root_move_float_to_main_edge_uses_main_namespace()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));

        var moved = RequireSuccess(floated.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Edge(LeftGroupId, DockEdge.Right)));

        Assert.AreEqual(2L, moved.Revision);
        Assert.AreEqual(0, moved.FloatingRoots.Count);
        Assert.AreEqual(4, moved.EnumeratePanels().Count());
        Assert.AreEqual(4, moved.EnumeratePanels().Distinct().Count());
        Assert.IsTrue(moved.Validate().IsValid, moved.Validate().Summary);

        var split = moved.MainRoot.FindSplit(new DockNodeId("dock.split.1"));
        Assert.IsNotNull(split);
        Assert.AreEqual(DockOrientation.Horizontal, split!.Orientation);
        var newGroup = RequireGroup(moved.MainRoot.FindGroup(new DockNodeId("dock.tabs.1")));
        AssertPanels(new[] { DockPanelIds.Console }, newGroup.Panels);

        AssertUniqueNodeIds(moved);
    }

    // WM-16: a main-root panel can be moved into a floating tab group without
    // changing its PanelInstanceId or creating a second view identity.
    [TestMethod]
    public void Wm16_cross_root_move_main_to_float_center_preserves_panel_identity()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatGroupId = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;

        var moved = RequireSuccess(floated.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(floatGroupId, 0)));

        Assert.AreEqual(2L, moved.Revision);
        Assert.AreEqual(1, moved.FloatingRoots.Count);
        var group = RequireGroup(moved.FloatingRoots[0].Root.Root);
        AssertPanels(
            new[] { DockPanelIds.Scene, DockPanelIds.Console },
            group.Panels);
        Assert.AreEqual(DockPanelIds.Scene, group.ActivePanel);
        Assert.IsFalse(moved.MainRoot.EnumeratePanels().Contains(DockPanelIds.Scene));
        Assert.AreEqual(4, moved.EnumeratePanels().Count());
        Assert.IsTrue(moved.Validate().IsValid, moved.Validate().Summary);
        AssertUniqueNodeIds(moved);
    }

    // WM-16: moving the last panel out of a floating root drops that root, and
    // the freed float-root id is reused deterministically.
    [TestMethod]
    public void Wm16_cross_root_move_float_to_float_drops_empty_root_and_reuses_id()
    {
        var workspace = DockWorkspace.CreateDefault();
        workspace = RequireSuccess(workspace.FloatPanel(DockPanelIds.Console, DefaultBounds));
        workspace = RequireSuccess(workspace.FloatPanel(DockPanelIds.Inspector, OtherBounds));

        var secondRoot = FloatRoot(workspace, "dock.float.2");
        var secondGroupId = RequireGroup(secondRoot.Root.Root).Id;

        var moved = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Center(secondGroupId)));

        Assert.AreEqual(3L, moved.Revision);
        Assert.AreEqual(1, moved.FloatingRoots.Count);
        Assert.AreEqual("dock.float.2", moved.FloatingRoots[0].Id.ToString());
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            RequireGroup(moved.FloatingRoots[0].Root.Root).Panels);
        Assert.AreEqual(2, moved.MainRoot.EnumeratePanels().Count());

        var refloated = RequireSuccess(moved.FloatPanel(DockPanelIds.Scene, DefaultBounds));
        Assert.AreEqual(4L, refloated.Revision);
        Assert.AreEqual(2, refloated.FloatingRoots.Count);
        Assert.AreEqual("dock.float.1", refloated.FloatingRoots[0].Id.ToString());
        Assert.AreEqual("dock.float.2", refloated.FloatingRoots[1].Id.ToString());
        Assert.IsTrue(refloated.Validate().IsValid, refloated.Validate().Summary);
        AssertUniqueNodeIds(refloated);
    }

    // WM-16: activation and split resizing inside a floating root route to the
    // owning root and keep the workspace globally valid.
    [TestMethod]
    public void Wm16_float_root_activation_and_resize_delegate_to_owning_root()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatRootId = workspace.FloatingRoots[0].Id;
        var firstGroupId = RequireGroup(workspace.FloatingRoots[0].Root.Root).Id;

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(firstGroupId, DockEdge.Right)));
        var splitId = new DockNodeId("dock.float.1.split.1");
        Assert.IsNotNull(FloatRoot(workspace, "dock.float.1").Root.FindSplit(splitId));

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(firstGroupId)));

        var activated = RequireSuccess(workspace.ActivateTab(firstGroupId, DockPanelIds.Console));
        Assert.AreEqual(
            DockPanelIds.Console,
            RequireGroup(FloatRoot(activated, "dock.float.1").Root.FindGroup(firstGroupId)).ActivePanel);

        var resized = RequireSuccess(activated.ResizeSplit(splitId, 1d, 3d));
        var split = FloatRoot(resized, "dock.float.1").Root.FindSplit(splitId)!;
        Assert.AreEqual(0.25d, split.FirstWeight, 1e-9);
        Assert.AreEqual(0.75d, split.SecondWeight, 1e-9);
        Assert.AreEqual(5L, resized.Revision);
        Assert.IsTrue(resized.Validate().IsValid, resized.Validate().Summary);
        AssertUniqueNodeIds(resized);
    }

    // WM-19: merging a multi-group floating root appends panels in pre-order
    // and preserves the target group's active panel.
    [TestMethod]
    public void Wm19_merge_float_root_appends_preorder_and_preserves_target_active()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatRootId = workspace.FloatingRoots[0].Id;
        var firstGroupId = RequireGroup(workspace.FloatingRoots[0].Root.Root).Id;

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(firstGroupId, DockEdge.Right)));

        var preorder = FloatRoot(workspace, "dock.float.1").Root.EnumeratePanels().ToArray();
        AssertPanels(new[] { DockPanelIds.Console, DockPanelIds.Scene }, preorder);

        var merged = RequireSuccess(workspace.MergeFloatRoot(
            floatRootId,
            new DockTarget.Center(LeftGroupId, 0)));

        Assert.AreEqual(3L, merged.Revision);
        Assert.AreEqual(0, merged.FloatingRoots.Count);
        var left = merged.MainRoot.FindGroup(LeftGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Console, DockPanelIds.Scene },
            left.Panels);
        Assert.AreEqual(DockPanelIds.Hierarchy, left.ActivePanel);
        Assert.AreEqual(4, merged.EnumeratePanels().Count());
        Assert.AreEqual(4, merged.EnumeratePanels().Distinct().Count());
        Assert.IsTrue(merged.Validate().IsValid, merged.Validate().Summary);
    }

    // WM-19: when the recorded return group no longer exists, re-docking uses
    // the first main-root group in pre-order and never loses a panel.
    [TestMethod]
    public void Wm19_redock_falls_back_to_first_main_group_when_return_target_is_gone()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Scene, DefaultBounds));

        Assert.AreEqual(SceneGroupId, floated.FloatingRoots[0].ReturnTarget.GroupId);
        Assert.IsNull(floated.MainRoot.FindGroup(SceneGroupId));

        var restored = RequireSuccess(floated.ReDockFloatRoot(floated.FloatingRoots[0].Id));

        var left = RequireGroup(restored.MainRoot.FindGroup(LeftGroupId));
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
            left.Panels);
        Assert.AreEqual(DockPanelIds.Hierarchy, left.ActivePanel);
        Assert.AreEqual(4, restored.EnumeratePanels().Count());
        Assert.AreEqual(0, restored.FloatingRoots.Count);
    }

    // WM-19: a merge that cannot be applied leaves the workspace, revision,
    // digest and floating root untouched.
    [TestMethod]
    public void Wm19_merge_failure_leaves_workspace_and_float_root_unchanged()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatRootId = floated.FloatingRoots[0].Id;

        AssertWorkspaceFailure(
            floated.MergeFloatRoot(
                floatRootId,
                new DockTarget.Center(new DockNodeId("dock.tabs.missing"))),
            DockWorkspaceError.TargetGone,
            floated,
            "merge into a missing group");

        AssertWorkspaceFailure(
            floated.MergeFloatRoot(floatRootId, new DockTarget.Edge(LeftGroupId, DockEdge.Right)),
            DockWorkspaceError.InvalidTarget,
            floated,
            "merge with an edge target");

        var ownGroup = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;
        AssertWorkspaceFailure(
            floated.MergeFloatRoot(floatRootId, new DockTarget.Center(ownGroup)),
            DockWorkspaceError.InvalidTarget,
            floated,
            "merge into a non-main group");

        AssertWorkspaceFailure(
            floated.ReDockFloatRoot(new DockFloatRootId("dock.float.99")),
            DockWorkspaceError.TargetGone,
            floated,
            "re-dock a missing floating root");

        Assert.AreEqual(1, floated.FloatingRoots.Count);
        Assert.AreEqual(floatRootId, floated.FloatingRoots[0].Id);
        Assert.IsTrue(floated.Validate().IsValid, floated.Validate().Summary);
    }

    // WM-20: non-finite, non-positive, or out-of-range bounds are rejected and
    // do not change the workspace.
    [TestMethod]
    public void Wm20_invalid_bounds_are_rejected_without_commit()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatRootId = floated.FloatingRoots[0].Id;

        var invalid = new[]
        {
            new DockBounds(double.NaN, 0d, 100d, 100d),
            new DockBounds(0d, double.PositiveInfinity, 100d, 100d),
            new DockBounds(0d, 0d, 0d, 100d),
            new DockBounds(0d, 0d, 100d, -1d),
            new DockBounds(32769d, 0d, 100d, 100d),
            new DockBounds(0d, 0d, 32769d, 100d),
        };

        foreach (var bounds in invalid)
        {
            AssertWorkspaceFailure(
                floated.UpdateFloatBounds(floatRootId, bounds, maximized: false),
                DockWorkspaceError.InvalidModel,
                floated,
                $"bounds {bounds}");
        }

        AssertWorkspaceFailure(
            floated.FloatPanel(DockPanelIds.Inspector, invalid[2]),
            DockWorkspaceError.InvalidModel,
            floated,
            "float with invalid bounds");
    }

    // WM-20: bounds changes advance the workspace revision and digest, but do
    // not change either root digest or trigger a model-side projection rebuild.
    [TestMethod]
    public void Wm20_bounds_update_changes_workspace_digest_only()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var root = floated.FloatingRoots[0];
        var mainDigest = floated.MainRoot.Digest;
        var rootDigest = root.Root.Digest;
        var workspaceDigest = floated.Digest;

        var updated = RequireSuccess(floated.UpdateFloatBounds(
            root.Id,
            OtherBounds,
            maximized: true));

        Assert.AreEqual(2L, updated.Revision);
        Assert.AreNotEqual(workspaceDigest, updated.Digest);
        Assert.AreEqual(mainDigest, updated.MainRoot.Digest);
        Assert.AreEqual(rootDigest, updated.FloatingRoots[0].Root.Digest);
        Assert.AreEqual(OtherBounds, updated.FloatingRoots[0].Bounds);
        Assert.AreEqual(DefaultBounds, updated.FloatingRoots[0].RestoreBounds);
        Assert.IsTrue(updated.FloatingRoots[0].Maximized);
        Assert.AreEqual(root.Id, updated.FloatingRoots[0].Id);
        Assert.AreEqual(root.ReturnTarget, updated.FloatingRoots[0].ReturnTarget);

        AssertWorkspaceFailure(
            updated.UpdateFloatBounds(root.Id, OtherBounds, maximized: true),
            DockWorkspaceError.AlreadyInState,
            updated,
            "repeat identical bounds");

        var unmaximized = RequireSuccess(updated.UpdateFloatBounds(
            root.Id,
            OtherBounds,
            maximized: false));
        Assert.AreEqual(3L, unmaximized.Revision);
        Assert.AreNotEqual(updated.Digest, unmaximized.Digest);
        Assert.AreEqual(OtherBounds, unmaximized.FloatingRoots[0].RestoreBounds);
        Assert.AreEqual(rootDigest, unmaximized.FloatingRoots[0].Root.Digest);
    }

    // WM-20: workspace digest includes float order, bounds, maximized state and
    // return target, but excludes Revision and the individual root digests'
    // internal bounds state.
    [TestMethod]
    public void Wm20_workspace_digest_covers_float_state_but_not_revision()
    {
        var main = DockWorkspace.CreateDefault().MainRoot;
        var panelA = new PanelInstanceId("editor.float.a");
        var panelB = new PanelInstanceId("editor.float.b");
        var floatA = FloatRootFor("dock.float.1", panelA, DefaultBounds);
        var floatB = FloatRootFor("dock.float.2", panelB, OtherBounds);

        var forward = DockWorkspace.Create(main, new[] { floatA, floatB });
        var reverse = DockWorkspace.Create(main, new[] { floatB, floatA });

        Assert.AreEqual(forward.Digest, reverse.Digest);
        Assert.AreEqual("dock.float.1", forward.FloatingRoots[0].Id.ToString());
        Assert.AreEqual("dock.float.2", forward.FloatingRoots[1].Id.ToString());

        var changedBounds = new DockFloatRoot(
            floatA.Id,
            floatA.Root,
            new DockBounds(10d, 20d, 300d, 200d),
            floatA.Maximized,
            floatA.ReturnTarget);
        var boundsWorkspace = DockWorkspace.Create(main, new[] { changedBounds, floatB });
        Assert.AreNotEqual(forward.Digest, boundsWorkspace.Digest);
        Assert.AreEqual(floatA.Root.Digest, boundsWorkspace.FloatingRoots[0].Root.Digest);

        var changedReturn = new DockFloatRoot(
            floatA.Id,
            floatA.Root,
            floatA.Bounds,
            floatA.Maximized,
            new DockReturnTarget(BottomGroupId, 2));
        var returnWorkspace = DockWorkspace.Create(main, new[] { changedReturn, floatB });
        Assert.AreNotEqual(forward.Digest, returnWorkspace.Digest);
        Assert.AreEqual(floatA.Root.Digest, returnWorkspace.FloatingRoots[0].Root.Digest);

        var changedIndex = new DockFloatRoot(
            floatA.Id,
            floatA.Root,
            floatA.Bounds,
            floatA.Maximized,
            new DockReturnTarget(LeftGroupId, 7));
        var indexWorkspace = DockWorkspace.Create(main, new[] { changedIndex, floatB });
        Assert.AreNotEqual(forward.Digest, indexWorkspace.Digest);

        var revisioned = DockWorkspace.Create(main, new[] { floatA, floatB }, revision: 123);
        Assert.AreEqual(forward.Digest, revisioned.Digest);
    }

    // D7-B: maximized window geometry is transient and must never overwrite the
    // normal restore bounds used for persistence and the W2 digest.
    [TestMethod]
    public void Wm20_maximized_geometry_does_not_overwrite_restore_bounds()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var rootId = floated.FloatingRoots[0].Id;

        var moved = RequireSuccess(floated.UpdateFloatBounds(rootId, OtherBounds, maximized: false));
        Assert.AreEqual(OtherBounds, moved.FloatingRoots[0].Bounds);
        Assert.AreEqual(OtherBounds, moved.FloatingRoots[0].RestoreBounds);

        var maximizedBounds = new DockBounds(-8d, -8d, 1936d, 1056d);
        var maximized = RequireSuccess(
            moved.UpdateFloatBounds(rootId, maximizedBounds, maximized: true));
        Assert.AreEqual(maximizedBounds, maximized.FloatingRoots[0].Bounds);
        Assert.AreEqual(OtherBounds, maximized.FloatingRoots[0].RestoreBounds);
        Assert.IsTrue(maximized.FloatingRoots[0].Maximized);

        var restored = new DockBounds(300d, 200d, 800d, 600d);
        var unmaximized = RequireSuccess(
            maximized.UpdateFloatBounds(rootId, restored, maximized: false));
        Assert.AreEqual(restored, unmaximized.FloatingRoots[0].Bounds);
        Assert.AreEqual(restored, unmaximized.FloatingRoots[0].RestoreBounds);
        Assert.IsFalse(unmaximized.FloatingRoots[0].Maximized);
    }

    // D7-B: W2 covers restore bounds and maximized state but not transient
    // window geometry.
    [TestMethod]
    public void Wm20_workspace_digest_uses_restore_bounds_not_transient_bounds()
    {
        var main = DockWorkspace.CreateDefault().MainRoot;
        var tree = DockTree.Create(Group(
            "dock.float.1.tabs.1",
            new PanelInstanceId("editor.float.a")));
        var normalBounds = new DockBounds(100d, 100d, 640d, 480d);
        var transientA = new DockBounds(0d, 0d, 1280d, 800d);
        var transientB = new DockBounds(-8d, -8d, 1296d, 816d);
        var target = new DockReturnTarget(LeftGroupId, 0);

        var first = DockWorkspace.Create(main, new[]
        {
            new DockFloatRoot(
                new DockFloatRootId("dock.float.1"),
                tree,
                transientA,
                maximized: true,
                target,
                normalBounds),
        });
        var second = DockWorkspace.Create(main, new[]
        {
            new DockFloatRoot(
                new DockFloatRootId("dock.float.1"),
                tree,
                transientB,
                maximized: true,
                target,
                normalBounds),
        });

        Assert.AreEqual(first.Digest, second.Digest);

        var moved = DockWorkspace.Create(main, new[]
        {
            new DockFloatRoot(
                new DockFloatRootId("dock.float.1"),
                tree,
                transientA,
                maximized: true,
                target,
                new DockBounds(120d, 120d, 640d, 480d)),
        });
        Assert.AreNotEqual(first.Digest, moved.Digest);
    }

    // D7-B: rebuilding a floating root for moves, activation or panel removal
    // must carry the restore bounds unchanged.
    [TestMethod]
    public void Wm20_float_root_rebuilds_preserve_restore_bounds()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var rootId = floated.FloatingRoots[0].Id;
        var groupId = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;
        var maximized = RequireSuccess(
            floated.UpdateFloatBounds(rootId, OtherBounds, maximized: true));

        var moved = RequireSuccess(maximized.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(groupId)));
        Assert.AreEqual(DefaultBounds, moved.FloatingRoots[0].RestoreBounds);

        var activated = RequireSuccess(moved.ActivateTab(groupId, DockPanelIds.Console));
        Assert.AreEqual(DefaultBounds, activated.FloatingRoots[0].RestoreBounds);

        var removed = RequireSuccess(activated.RemovePanel(DockPanelIds.Console));
        Assert.AreEqual(DefaultBounds, removed.FloatingRoots[0].RestoreBounds);
    }

    // D7-B: restore bounds are validated separately from transient bounds.
    [TestMethod]
    public void Workspace_validation_rejects_invalid_restore_bounds()
    {
        var main = DockWorkspace.CreateDefault().MainRoot;
        var tree = DockTree.Create(Group(
            "dock.float.1.tabs.1",
            new PanelInstanceId("editor.float.a")));
        var invalid = new DockFloatRoot(
            new DockFloatRootId("dock.float.1"),
            tree,
            DefaultBounds,
            maximized: false,
            new DockReturnTarget(LeftGroupId, 0),
            new DockBounds(0d, 0d, 0d, 100d));

        var validation = DockWorkspace.Validate(main, new[] { invalid });
        Assert.IsTrue(validation.Has("invalid-restore-bounds"), validation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, new[] { invalid }));
    }

    // WM-21: closing a floating root removes only that root and all its panels.
    [TestMethod]
    public void Wm21_close_float_root_removes_only_that_root()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatGroupId = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;
        floated = RequireSuccess(floated.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(floatGroupId)));
        var mainDigest = floated.MainRoot.Digest;

        var closed = RequireSuccess(floated.CloseFloatRoot(floated.FloatingRoots[0].Id));

        Assert.AreEqual(3L, closed.Revision);
        Assert.AreEqual(0, closed.FloatingRoots.Count);
        Assert.AreEqual(mainDigest, closed.MainRoot.Digest);
        Assert.IsFalse(closed.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.IsFalse(closed.EnumeratePanels().Contains(DockPanelIds.Scene));
        Assert.AreEqual(2, closed.EnumeratePanels().Count());
        Assert.IsTrue(closed.Validate().IsValid, closed.Validate().Summary);
    }

    // WM-21: removing one panel keeps a non-empty floating root; removing the
    // last one drops the root. The same operation on the last main-root panel
    // is rejected.
    [TestMethod]
    public void Wm21_remove_last_float_panel_drops_root_and_last_main_panel_is_rejected()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatGroupId = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;
        floated = RequireSuccess(floated.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(floatGroupId)));

        var removedOne = RequireSuccess(floated.RemovePanel(DockPanelIds.Console));
        Assert.AreEqual(3L, removedOne.Revision);
        Assert.AreEqual(1, removedOne.FloatingRoots.Count);
        AssertPanels(
            new[] { DockPanelIds.Scene },
            RequireGroup(removedOne.FloatingRoots[0].Root.Root).Panels);
        Assert.AreEqual(3, removedOne.EnumeratePanels().Count());

        var removedLast = RequireSuccess(removedOne.RemovePanel(DockPanelIds.Scene));
        Assert.AreEqual(4L, removedLast.Revision);
        Assert.AreEqual(0, removedLast.FloatingRoots.Count);
        Assert.AreEqual(2, removedLast.EnumeratePanels().Count());

        var singleMain = DockTree.Create(Group("dock.tabs.solo", DockPanelIds.Hierarchy));
        var floatRoot = FloatRootFor("dock.float.1", DockPanelIds.Console, DefaultBounds);
        var workspace = DockWorkspace.Create(singleMain, new[] { floatRoot });

        AssertWorkspaceFailure(
            workspace.RemovePanel(DockPanelIds.Hierarchy),
            DockWorkspaceError.WouldEmptyMainRoot,
            workspace,
            "remove the last main panel");
    }

    // WX-02: panel, node and floating-root identities are globally unique.
    [TestMethod]
    public void Workspace_identity_is_globally_unique_across_roots()
    {
        var main = DockWorkspace.CreateDefault().MainRoot;
        var other = new PanelInstanceId("editor.other");

        var duplicateNode = new DockFloatRoot(
            new DockFloatRootId("dock.float.1"),
            DockTree.Create(Group("dock.main", other)),
            DefaultBounds,
            maximized: false,
            new DockReturnTarget(LeftGroupId, 0));
        var nodeValidation = DockWorkspace.Validate(main, new[] { duplicateNode });
        Assert.IsTrue(nodeValidation.Has("duplicate-node-id"), nodeValidation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, new[] { duplicateNode }));

        var duplicatePanel = FloatRootFor("dock.float.1", DockPanelIds.Hierarchy, DefaultBounds);
        var panelValidation = DockWorkspace.Validate(main, new[] { duplicatePanel });
        Assert.IsTrue(panelValidation.Has("duplicate-panel"), panelValidation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, new[] { duplicatePanel }));

        var sameIdA = FloatRootFor("dock.float.1", new PanelInstanceId("editor.a"), DefaultBounds);
        var sameIdB = FloatRootFor("dock.float.1", new PanelInstanceId("editor.b"), DefaultBounds);
        var idValidation = DockWorkspace.Validate(main, new[] { sameIdA, sameIdB });
        Assert.IsTrue(idValidation.Has("duplicate-float-root-id"), idValidation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, new[] { sameIdA, sameIdB }));

        var invalidBounds = new DockFloatRoot(
            new DockFloatRootId("dock.float.1"),
            DockTree.Create(Group("dock.float.1.tabs.1", new PanelInstanceId("editor.c"))),
            new DockBounds(0d, 0d, 0d, 100d),
            maximized: false,
            new DockReturnTarget(LeftGroupId, 0));
        var boundsValidation = DockWorkspace.Validate(main, new[] { invalidBounds });
        Assert.IsTrue(boundsValidation.Has("invalid-bounds"), boundsValidation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, new[] { invalidBounds }));

        Assert.ThrowsException<ArgumentException>(
            () => new DockFloatRoot(
                default,
                DockTree.Create(Group("dock.float.1.tabs.1", new PanelInstanceId("editor.d"))),
                DefaultBounds,
                maximized: false,
                new DockReturnTarget(LeftGroupId, 0)));
    }

    // WX-03: the last main-root panel cannot be closed, floated, or moved to a
    // floating root, and every rejected attempt leaves the workspace unchanged.
    [TestMethod]
    public void Last_main_panel_cannot_be_removed_floated_or_moved()
    {
        var main = DockTree.Create(Group("dock.tabs.solo", DockPanelIds.Hierarchy));
        var floatRoot = FloatRootFor("dock.float.1", DockPanelIds.Console, DefaultBounds);
        var workspace = DockWorkspace.Create(main, new[] { floatRoot });
        var floatGroupId = RequireGroup(floatRoot.Root.Root).Id;

        AssertWorkspaceFailure(
            workspace.RemovePanel(DockPanelIds.Hierarchy),
            DockWorkspaceError.WouldEmptyMainRoot,
            workspace,
            "remove last main panel");

        AssertWorkspaceFailure(
            workspace.FloatPanel(DockPanelIds.Hierarchy, DefaultBounds),
            DockWorkspaceError.WouldEmptyMainRoot,
            workspace,
            "float last main panel");

        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Hierarchy,
                new DockTarget.Center(floatGroupId)),
            DockWorkspaceError.WouldEmptyMainRoot,
            workspace,
            "move last main panel into a floating root");

        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Hierarchy,
                new DockTarget.Float(DefaultBounds)),
            DockWorkspaceError.WouldEmptyMainRoot,
            workspace,
            "move last main panel into a new floating root");
    }

    // WX-04: node, panel and floating-root limits are accounted for across the
    // whole workspace, and exceeding the float limit fails atomically.
    [TestMethod]
    public void Workspace_limits_are_enforced_globally_and_fail_atomically()
    {
        var main = DockWorkspace.CreateDefault().MainRoot;
        var tooManyFloats = Enumerable.Range(1, 17)
            .Select(index => FloatRootFor(
                $"dock.float.{index}",
                new PanelInstanceId($"editor.float.{index}"),
                DefaultBounds))
            .ToArray();

        var validation = DockWorkspace.Validate(main, tooManyFloats);
        Assert.IsTrue(validation.Has("float-limit"), validation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(main, tooManyFloats));

        var sixteen = tooManyFloats.Take(16).ToArray();
        var workspace = DockWorkspace.Create(main, sixteen);
        Assert.IsTrue(workspace.Validate().IsValid, workspace.Validate().Summary);

        AssertWorkspaceFailure(
            workspace.FloatPanel(DockPanelIds.Console, DefaultBounds),
            DockWorkspaceError.LimitExceeded,
            workspace,
            "float beyond the root limit");

        var largeMain = BalancedTree("dock.big", "editor.big", 129);
        var largeFloatTree = BalancedTree("dock.float.1", "editor.float.big", 129);
        var largeFloat = new DockFloatRoot(
            new DockFloatRootId("dock.float.1"),
            largeFloatTree,
            DefaultBounds,
            maximized: false,
            new DockReturnTarget(new DockNodeId("dock.big.g0"), 0));

        var largeValidation = DockWorkspace.Validate(largeMain, new[] { largeFloat });
        Assert.IsTrue(largeValidation.Has("panel-limit"), largeValidation.Summary);
        Assert.IsTrue(largeValidation.Has("node-limit"), largeValidation.Summary);
        Assert.ThrowsException<DockModelException>(
            () => DockWorkspace.Create(largeMain, new[] { largeFloat }));
    }

    // WX-05: revision advances exactly once per successful, state-changing
    // operation and never on failures or no-ops.
    [TestMethod]
    public void Revision_advances_only_on_successful_state_changes()
    {
        var workspace = DockWorkspace.CreateDefault();

        AssertWorkspaceFailure(
            workspace.FloatPanel(DockPanelIds.Console, new DockBounds(double.NaN, 0d, 10d, 10d)),
            DockWorkspaceError.InvalidModel,
            workspace,
            "failed float");

        var floated = RequireSuccess(workspace.FloatPanel(DockPanelIds.Console, DefaultBounds));
        Assert.AreEqual(1L, floated.Revision);
        var floatRootId = floated.FloatingRoots[0].Id;

        AssertWorkspaceFailure(
            floated.UpdateFloatBounds(floatRootId, DefaultBounds, maximized: false),
            DockWorkspaceError.AlreadyInState,
            floated,
            "no-op bounds");

        var updated = RequireSuccess(
            floated.UpdateFloatBounds(floatRootId, OtherBounds, maximized: false));
        Assert.AreEqual(2L, updated.Revision);

        AssertWorkspaceFailure(
            updated.MergeFloatRoot(
                floatRootId,
                new DockTarget.Center(new DockNodeId("dock.tabs.missing"))),
            DockWorkspaceError.TargetGone,
            updated,
            "failed merge");

        var restored = RequireSuccess(updated.ReDockFloatRoot(floatRootId));
        Assert.AreEqual(3L, restored.Revision);

        var fromRevision = DockWorkspace.Create(
            DockWorkspace.CreateDefault().MainRoot,
            revision: 41);
        var next = RequireSuccess(fromRevision.FloatPanel(DockPanelIds.Console, DefaultBounds));
        Assert.AreEqual(42L, next.Revision);
    }

    // WX-06: the same operation sequence produces the same digests, revisions,
    // float-root ids and node ids on every run.
    [TestMethod]
    public void Workspace_operation_sequences_are_deterministic()
    {
        var first = RunSequence();
        var second = RunSequence();

        Assert.AreEqual(first.Revision, second.Revision);
        Assert.AreEqual(first.Digest, second.Digest);
        Assert.AreEqual(first.FloatIds, second.FloatIds);
        Assert.AreEqual(first.NodeIds, second.NodeIds);
        Assert.AreEqual(first.Panels, second.Panels);
    }

    // WX-07: restoring the default clears floating roots; repeating it is a
    // no-op that does not advance the revision.
    [TestMethod]
    public void Restore_default_clears_float_roots_and_repeat_is_rejected()
    {
        var original = DockWorkspace.CreateDefault();
        AssertWorkspaceFailure(
            original.RestoreDefault(),
            DockWorkspaceError.AlreadyInState,
            original,
            "restore an already-default workspace");

        var floated = RequireSuccess(
            original.FloatPanel(DockPanelIds.Console, DefaultBounds));
        var updated = RequireSuccess(floated.UpdateFloatBounds(
            floated.FloatingRoots[0].Id,
            OtherBounds,
            maximized: true));

        var restored = RequireSuccess(updated.RestoreDefault());
        Assert.AreEqual(3L, restored.Revision);
        Assert.AreEqual(0, restored.FloatingRoots.Count);
        Assert.AreEqual(original.MainRoot.Digest, restored.MainRoot.Digest);
        Assert.AreEqual(original.Digest, restored.Digest);

        AssertWorkspaceFailure(
            restored.RestoreDefault(),
            DockWorkspaceError.AlreadyInState,
            restored,
            "repeat restore");
    }

    private static (long Revision, string Digest, string FloatIds, string NodeIds, string Panels)
        RunSequence()
    {
        var workspace = DockWorkspace.CreateDefault();
        workspace = RequireSuccess(workspace.FloatPanel(DockPanelIds.Console, DefaultBounds));
        var floatRootId = workspace.FloatingRoots[0].Id;
        var groupId = RequireGroup(workspace.FloatingRoots[0].Root.Root).Id;

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(groupId, DockEdge.Right)));
        workspace = RequireSuccess(workspace.UpdateFloatBounds(
            floatRootId,
            OtherBounds,
            maximized: true));
        workspace = RequireSuccess(workspace.ReDockFloatRoot(floatRootId));

        return (
            workspace.Revision,
            workspace.Digest,
            string.Join(",", workspace.FloatingRoots.Select(root => root.Id.ToString())),
            string.Join(",", workspace.EnumerateNodes().Select(node => node.Id.ToString())),
            string.Join(",", workspace.EnumeratePanels().Select(panel => panel.ToString())));
    }

    private static DockWorkspace RequireSuccess(DockWorkspaceResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Workspace);
        return result.Workspace!;
    }

    private static void AssertWorkspaceFailure(
        DockWorkspaceResult result,
        DockWorkspaceError expectedError,
        DockWorkspace unchanged,
        string what)
    {
        var revision = unchanged.Revision;
        var digest = unchanged.Digest;
        var floatIds = string.Join(
            ",",
            unchanged.FloatingRoots.Select(root => root.Id.ToString()));
        var panelIds = string.Join(
            ",",
            unchanged.EnumeratePanels().Select(panel => panel.ToString()));

        Assert.IsFalse(result.Success, $"{what}: expected failure.");
        Assert.AreEqual(expectedError, result.Error, $"{what}: error.");
        Assert.IsNull(result.Workspace, $"{what}: a failed operation must not return a workspace.");
        Assert.AreEqual(revision, unchanged.Revision, $"{what}: revision must not change.");
        Assert.AreEqual(digest, unchanged.Digest, $"{what}: digest must not change.");
        Assert.AreEqual(
            floatIds,
            string.Join(",", unchanged.FloatingRoots.Select(root => root.Id.ToString())),
            $"{what}: the floating-root list must not change.");
        Assert.AreEqual(
            panelIds,
            string.Join(",", unchanged.EnumeratePanels().Select(panel => panel.ToString())),
            $"{what}: panels must not be lost or duplicated.");
        Assert.IsTrue(unchanged.Validate().IsValid, $"{what}: the original workspace must stay valid.");
    }

    private static void AssertUniqueNodeIds(DockWorkspace workspace)
    {
        var ids = workspace.EnumerateNodes().Select(node => node.Id.ToString()).ToArray();
        Assert.AreEqual(ids.Length, ids.Distinct().Count(), "node ids must be globally unique");
    }

    private static DockFloatRoot FloatRoot(DockWorkspace workspace, string id)
    {
        var root = workspace.FloatingRoots.FirstOrDefault(
            candidate => string.Equals(candidate.Id.ToString(), id, StringComparison.Ordinal));
        Assert.IsNotNull(root, $"Floating root '{id}' was not found.");
        return root!;
    }

    private static DockFloatRoot FloatRootFor(
        string id,
        PanelInstanceId panel,
        DockBounds bounds)
    {
        var groupId = new DockNodeId($"{id}.tabs.1");
        var tree = DockTree.Create(Group(groupId.ToString(), panel));
        return new DockFloatRoot(
            new DockFloatRootId(id),
            tree,
            bounds,
            maximized: false,
            new DockReturnTarget(LeftGroupId, 0));
    }

    private static DockTabGroupNode Group(
        string id,
        PanelInstanceId first,
        params PanelInstanceId[] rest)
    {
        var panels = new[] { first }.Concat(rest).ToArray();
        return new DockTabGroupNode(new DockNodeId(id), panels, first);
    }

    private static DockTree BalancedTree(string nodePrefix, string panelPrefix, int leaves)
    {
        var nodeCounter = 0;
        var panelCounter = 0;
        return DockTree.Create(Build(leaves));

        DockNode Build(int count)
        {
            if (count == 1)
            {
                var panels = Enumerable.Range(0, 1)
                    .Select(_ => new PanelInstanceId($"{panelPrefix}.{panelCounter++}"))
                    .ToArray();
                return new DockTabGroupNode(
                    new DockNodeId($"{nodePrefix}.g{nodeCounter++}"),
                    panels,
                    panels[0]);
            }

            var leftCount = count / 2;
            var rightCount = count - leftCount;
            return new DockSplitNode(
                new DockNodeId($"{nodePrefix}.s{nodeCounter++}"),
                DockOrientation.Horizontal,
                Build(leftCount),
                Build(rightCount),
                0.5d,
                0.5d);
        }
    }

    private static DockSplitNode RequireSplit(DockNode? node)
    {
        Assert.IsInstanceOfType(node, typeof(DockSplitNode));
        return (DockSplitNode)node!;
    }

    private static DockTabGroupNode RequireGroup(DockNode? node)
    {
        Assert.IsInstanceOfType(node, typeof(DockTabGroupNode));
        return (DockTabGroupNode)node!;
    }

    private static void AssertPanels(
        IEnumerable<PanelInstanceId> expected,
        IEnumerable<PanelInstanceId> actual)
    {
        var expectedText = string.Join(",", expected.Select(panel => panel.ToString()));
        var actualText = string.Join(",", actual.Select(panel => panel.ToString()));
        Assert.AreEqual(expectedText, actualText);
    }
}
