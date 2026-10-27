using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DockTreeTests
{
    private static readonly DockNodeId MainId = new("dock.main");
    private static readonly DockNodeId LeftGroupId = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroupId = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroupId = new("dock.tabs.bottom");
    private static readonly PanelInstanceId Assets = new("editor.assets");
    private static readonly PanelInstanceId Missing = new("editor.missing");

    // WM-01: default layout loads and validates without a revision concept.
    [TestMethod]
    public void Wm01_default_tree_validates()
    {
        var tree = DockTree.CreateDefault();
        var validation = tree.Validate();

        Assert.IsTrue(validation.IsValid, validation.Summary);
        Assert.AreEqual(4, tree.EnumeratePanels().Count());
        Assert.AreEqual(4, tree.EnumeratePanels().Distinct().Count());
        Assert.AreEqual(64, tree.Digest.Length);

        var main = RequireSplit(tree.Root);
        Assert.AreEqual(MainId, main.Id);
        Assert.AreEqual(DockOrientation.Horizontal, main.Orientation);
        Assert.AreEqual(0.28d, main.FirstWeight, 1e-9);

        var center = RequireSplit(main.Second);
        Assert.AreEqual(DockOrientation.Vertical, center.Orientation);
        Assert.AreEqual(0.75d, center.FirstWeight, 1e-9);
        AssertPanels(new[] { DockPanelIds.Scene }, RequireGroup(center.First).Panels);
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            RequireGroup(center.Second).Panels);
        Assert.AreEqual(DockPanelIds.Inspector, RequireGroup(center.Second).ActivePanel);

        Assert.AreEqual(tree.Digest, DockTree.CreateDefault().Digest);
        Assert.AreEqual(tree.Digest, DockTree.ComputeDigest(tree.Root));
    }

    // WM-02: duplicate DockNodeId is rejected.
    [TestMethod]
    public void Wm02_duplicate_node_id_is_rejected()
    {
        var duplicate = new DockSplitNode(
            new DockNodeId("dock.dup.split"),
            DockOrientation.Horizontal,
            Group("dock.dup", DockPanelIds.Hierarchy),
            Group("dock.dup", DockPanelIds.Scene),
            0.5d,
            0.5d);

        var validation = DockTree.Validate(duplicate);
        Assert.IsFalse(validation.IsValid);
        Assert.IsTrue(validation.Has("duplicate-node-id"), validation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(duplicate));
    }

    // WM-03: duplicate PanelInstanceId is rejected.
    [TestMethod]
    public void Wm03_duplicate_panel_instance_is_rejected()
    {
        var duplicate = new DockSplitNode(
            new DockNodeId("dock.dup.split"),
            DockOrientation.Vertical,
            Group("dock.dup.left", DockPanelIds.Hierarchy),
            Group("dock.dup.right", DockPanelIds.Scene, DockPanelIds.Hierarchy),
            0.5d,
            0.5d);

        var validation = DockTree.Validate(duplicate);
        Assert.IsFalse(validation.IsValid);
        Assert.IsTrue(validation.Has("duplicate-panel"), validation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(duplicate));
    }

    // WM-04: an active panel outside its group is rejected; normalization repairs it.
    [TestMethod]
    public void Wm04_active_panel_outside_group_is_rejected_or_normalized()
    {
        var group = new DockTabGroupNode(
            new DockNodeId("dock.tabs.invalid-active"),
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
            Missing);

        var validation = DockTree.Validate(group);
        Assert.IsFalse(validation.IsValid);
        Assert.IsTrue(validation.Has("invalid-active"), validation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(group));

        var normalization = DockTree.Normalize(group);
        Assert.IsTrue(normalization.Changed);
        Assert.IsTrue(
            normalization.Diagnostics.Any(diagnostic =>
                diagnostic.StartsWith("active-panel-normalized:", StringComparison.Ordinal)),
            string.Join("; ", normalization.Diagnostics));

        var repaired = DockTree.Create(normalization.Root!);
        Assert.IsTrue(repaired.Validate().IsValid);
        Assert.AreEqual(DockPanelIds.Hierarchy, RequireGroup(repaired.Root).ActivePanel);
        Assert.AreEqual(
            DockTree.ComputeDigest(DockTree.Normalize(group).Root),
            DockTree.ComputeDigest(normalization.Root));
    }

    // WM-05: a split requires exactly two children; committed trees reject one/none.
    [TestMethod]
    public void Wm05_split_requires_exactly_two_children()
    {
        var oneChild = new DockSplitNode(
            new DockNodeId("dock.one-child"),
            DockOrientation.Horizontal,
            Group("dock.one-child.tabs", DockPanelIds.Hierarchy),
            null,
            0.5d,
            0.5d);
        Assert.IsTrue(DockTree.Validate(oneChild).Has("invalid-split"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(oneChild));

        var noChildren = new DockSplitNode(
            new DockNodeId("dock.no-children"),
            DockOrientation.Vertical,
            null,
            null,
            0.5d,
            0.5d);
        Assert.IsTrue(DockTree.Validate(noChildren).Has("invalid-split"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(noChildren));
    }

    // WM-06: non-positive or non-finite weights are rejected and never repaired.
    [TestMethod]
    public void Wm06_invalid_weights_are_rejected_without_repair()
    {
        var invalidWeights = new[]
        {
            new[] { 0d, 1d },
            new[] { -0.1d, 1.1d },
            new[] { double.NaN, 0.5d },
            new[] { double.PositiveInfinity, 0.5d },
        };

        foreach (var weights in invalidWeights)
        {
            var split = new DockSplitNode(
                new DockNodeId("dock.invalid-weight"),
                DockOrientation.Horizontal,
                Group("dock.invalid-weight.left", DockPanelIds.Hierarchy),
                Group("dock.invalid-weight.right", DockPanelIds.Scene),
                weights[0],
                weights[1]);

            Assert.IsTrue(DockTree.Validate(split).Has("invalid-weight"));

            var normalization = DockTree.Normalize(split);
            Assert.IsFalse(normalization.Changed);
            Assert.AreSame(split, normalization.Root);
            Assert.IsTrue(DockTree.Validate(normalization.Root).Has("invalid-weight"));
            Assert.ThrowsException<DockModelException>(() => DockTree.Create(split));
        }
    }

    // WM-07: a finite positive weight sum away from 1 is normalized deterministically.
    [TestMethod]
    public void Wm07_weight_sum_deviation_is_normalized_and_diagnosed()
    {
        var split = new DockSplitNode(
            new DockNodeId("dock.weight-sum"),
            DockOrientation.Horizontal,
            Group("dock.weight-sum.left", DockPanelIds.Hierarchy),
            Group("dock.weight-sum.right", DockPanelIds.Scene),
            0.3d,
            0.8d);

        Assert.IsTrue(DockTree.Validate(split).Has("invalid-weight"));

        var normalization = DockTree.Normalize(split);
        Assert.IsTrue(normalization.Changed);
        Assert.IsTrue(
            normalization.Diagnostics.Any(diagnostic =>
                diagnostic.StartsWith("weights-normalized:", StringComparison.Ordinal)),
            string.Join("; ", normalization.Diagnostics));

        var normalized = RequireSplit(normalization.Root!);
        Assert.AreEqual(0.3d / 1.1d, normalized.FirstWeight, 1e-9);
        Assert.AreEqual(0.8d / 1.1d, normalized.SecondWeight, 1e-9);
        Assert.AreEqual(1d, normalized.FirstWeight + normalized.SecondWeight, 1e-9);

        var tree = DockTree.Create(normalized);
        Assert.IsTrue(tree.Validate().IsValid);

        var second = DockTree.Normalize(tree.Root);
        Assert.IsFalse(second.Changed);
        Assert.AreEqual(tree.Digest, DockTree.ComputeDigest(second.Root));
    }

    // WM-08: moving a panel to its current position is rejected.
    [TestMethod]
    public void Wm08_move_to_same_position_is_rejected()
    {
        var tree = DockTree.CreateDefault();
        var digest = tree.Digest;

        AssertFailure(
            tree.MovePanel(DockPanelIds.Scene, new DockTarget.Center(SceneGroupId, 0)),
            DockError.AlreadyInState,
            tree,
            "same-position move");

        AssertFailure(
            tree.MovePanel(DockPanelIds.Scene, new DockTarget.Edge(SceneGroupId, DockEdge.Left)),
            DockError.AlreadyInState,
            tree,
            "edge-split the only panel");

        Assert.AreEqual(digest, tree.Digest);
    }

    // WM-09: missing source or target fails without mutation.
    [TestMethod]
    public void Wm09_missing_target_fails_without_mutation()
    {
        var tree = DockTree.CreateDefault();

        AssertFailure(
            tree.MovePanel(DockPanelIds.Console, new DockTarget.Center(new DockNodeId("dock.tabs.missing"))),
            DockError.TargetGone,
            tree,
            "missing move target");

        AssertFailure(
            tree.MovePanel(Missing, new DockTarget.Center(BottomGroupId)),
            DockError.InvalidSource,
            tree,
            "missing move source");
    }

    // WM-10: edge docking produces a deterministic binary split.
    [TestMethod]
    public void Wm10_edge_docking_creates_correct_binary_split()
    {
        var right = DockTree.CreateDefault();
        var rightResult = right.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(LeftGroupId, DockEdge.Right));

        Assert.IsTrue(rightResult.Success, rightResult.Diagnostic);
        var rightTree = rightResult.Tree!;
        Assert.IsTrue(rightTree.Validate().IsValid, rightTree.Validate().Summary);

        var rightRoot = RequireSplit(rightTree.Root);
        Assert.AreEqual(MainId, rightRoot.Id);
        Assert.IsInstanceOfType(rightRoot.Second, typeof(DockTabGroupNode));
        var rightEdge = RequireSplit(rightRoot.First);
        Assert.AreEqual(new DockNodeId("dock.split.1"), rightEdge.Id);
        Assert.AreEqual(DockOrientation.Horizontal, rightEdge.Orientation);
        Assert.AreEqual(LeftGroupId, RequireGroup(rightEdge.First).Id);
        var rightGroup = RequireGroup(rightEdge.Second);
        Assert.AreEqual(new DockNodeId("dock.tabs.1"), rightGroup.Id);
        AssertPanels(new[] { DockPanelIds.Scene }, rightGroup.Panels);
        Assert.AreEqual(DockPanelIds.Scene, rightGroup.ActivePanel);

        var bottom = DockTree.CreateDefault();
        var bottomResult = bottom.MovePanel(
            DockPanelIds.Console,
            new DockTarget.Edge(LeftGroupId, DockEdge.Bottom));

        Assert.IsTrue(bottomResult.Success, bottomResult.Diagnostic);
        var bottomTree = bottomResult.Tree!;
        Assert.IsTrue(bottomTree.Validate().IsValid, bottomTree.Validate().Summary);

        var bottomRoot = RequireSplit(bottomTree.Root);
        Assert.AreEqual(MainId, bottomRoot.Id);
        var bottomEdge = RequireSplit(bottomRoot.First);
        Assert.AreEqual(DockOrientation.Vertical, bottomEdge.Orientation);
        Assert.AreEqual(LeftGroupId, RequireGroup(bottomEdge.First).Id);
        AssertPanels(new[] { DockPanelIds.Console }, RequireGroup(bottomEdge.Second).Panels);

        var repeat = DockTree.CreateDefault().MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(LeftGroupId, DockEdge.Right));
        Assert.IsTrue(repeat.Success, repeat.Diagnostic);
        Assert.AreEqual(rightTree.Digest, repeat.Tree!.Digest);
        Assert.AreEqual(4, right.EnumeratePanels().Count());
        Assert.AreEqual(SceneGroupId, right.FindGroup(SceneGroupId)!.Id);
    }

    // WM-11: empty groups and single-child splits collapse during normalization.
    [TestMethod]
    public void Wm11_empty_groups_and_single_child_splits_collapse()
    {
        var tree = SingleBottomGroup();

        var first = tree.RemovePanel(DockPanelIds.Inspector);
        Assert.IsTrue(first.Success, first.Diagnostic);
        var afterFirst = first.Tree!;
        Assert.IsTrue(afterFirst.Validate().IsValid);

        var root = RequireSplit(afterFirst.Root);
        var collapsedRight = RequireGroup(root.Second);
        Assert.AreEqual(SceneGroupId, collapsedRight.Id);
        Assert.AreEqual(2, afterFirst.EnumeratePanels().Count());

        var second = afterFirst.RemovePanel(DockPanelIds.Hierarchy);
        Assert.IsTrue(second.Success, second.Diagnostic);
        var afterSecond = second.Tree!;
        Assert.IsTrue(afterSecond.Validate().IsValid);

        var remaining = RequireGroup(afterSecond.Root);
        Assert.AreEqual(SceneGroupId, remaining.Id);
        Assert.AreEqual(1, afterSecond.EnumeratePanels().Count());

        var digest = afterSecond.Digest;
        AssertFailure(
            afterSecond.RemovePanel(DockPanelIds.Scene),
            DockError.InvalidModel,
            afterSecond,
            "remove the last panel");
        Assert.AreEqual(digest, afterSecond.Digest);
    }

    // WM-12: normalization is deterministic and idempotent.
    [TestMethod]
    public void Wm12_normalization_is_deterministic_and_idempotent()
    {
        var inner = new DockSplitNode(
            new DockNodeId("dock.inner"),
            DockOrientation.Vertical,
            new DockTabGroupNode(
                new DockNodeId("dock.inner.tabs"),
                new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
                Missing),
            null,
            0.5d,
            0.5d);
        var outer = new DockSplitNode(
            new DockNodeId("dock.outer"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                new DockNodeId("dock.empty"),
                Array.Empty<PanelInstanceId>(),
                null),
            inner,
            0.4d,
            0.8d);

        Assert.IsFalse(DockTree.Validate(outer).IsValid);

        var first = DockTree.Normalize(outer);
        Assert.IsTrue(first.Changed);
        var group = RequireGroup(first.Root!);
        Assert.AreEqual(new DockNodeId("dock.inner.tabs"), group.Id);
        Assert.AreEqual(DockPanelIds.Hierarchy, group.ActivePanel);
        AssertPanels(new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene }, group.Panels);

        var tree = DockTree.Create(first.Root!);
        Assert.IsTrue(tree.Validate().IsValid);
        var digest = tree.Digest;

        var second = DockTree.Normalize(tree.Root);
        Assert.IsFalse(second.Changed);
        Assert.AreEqual(digest, DockTree.ComputeDigest(second.Root));
        Assert.AreEqual(digest, DockTree.ComputeDigest(DockTree.Normalize(outer).Root));
    }

    // WM-13: repeated valid activation is idempotent.
    [TestMethod]
    public void Wm13_repeated_activation_is_idempotent()
    {
        var tree = DockTree.CreateDefault();

        var first = tree.ActivateTab(BottomGroupId, DockPanelIds.Console);
        Assert.IsTrue(first.Success, first.Diagnostic);
        Assert.IsTrue(first.Tree!.Validate().IsValid);
        Assert.AreEqual(
            DockPanelIds.Console,
            first.Tree.FindGroup(BottomGroupId)!.ActivePanel);

        var digest = first.Tree.Digest;
        AssertFailure(
            first.Tree.ActivateTab(BottomGroupId, DockPanelIds.Console),
            DockError.AlreadyInState,
            first.Tree,
            "repeat activation");
        Assert.AreEqual(digest, first.Tree.Digest);
        Assert.AreEqual(
            DockPanelIds.Inspector,
            tree.FindGroup(BottomGroupId)!.ActivePanel);
    }

    // WM-14: depth, node and panel limits are rejected.
    [TestMethod]
    public void Wm14_depth_node_and_panel_limits_are_rejected()
    {
        var deep = Chain(65);
        Assert.IsTrue(DockTree.Validate(deep).Has("depth-limit"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(deep));

        var large = BalancedTree(257);
        var largeValidation = DockTree.Validate(large);
        Assert.IsTrue(largeValidation.Has("node-limit"), largeValidation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(large));

        var manyPanels = BalancedTree(129, panelsPerLeaf: 2);
        var panelValidation = DockTree.Validate(manyPanels);
        Assert.IsTrue(panelValidation.Has("panel-limit"), panelValidation.Summary);
        Assert.IsFalse(panelValidation.Has("node-limit"), panelValidation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(manyPanels));
    }

    // WM-18: failed operations leave the tree and digest unchanged.
    [TestMethod]
    public void Wm18_failed_operations_leave_tree_unchanged()
    {
        var tree = DockTree.CreateDefault();
        var digest = tree.Digest;

        AssertFailure(
            tree.RemovePanel(Missing),
            DockError.InvalidSource,
            tree,
            "remove missing panel");
        AssertFailure(
            tree.MovePanel(DockPanelIds.Console, new DockTarget.Center(new DockNodeId("dock.tabs.missing"))),
            DockError.TargetGone,
            tree,
            "move to missing group");
        AssertFailure(
            tree.ActivateTab(BottomGroupId, DockPanelIds.Hierarchy),
            DockError.InvalidSource,
            tree,
            "activate a panel outside the group");
        AssertFailure(
            tree.ReorderTab(DockPanelIds.Hierarchy, BottomGroupId, 0),
            DockError.InvalidSource,
            tree,
            "reorder a panel outside the group");
        AssertFailure(
            tree.ResizeSplit(MainId, 0d, 1d),
            DockError.InvalidModel,
            tree,
            "resize with an invalid weight");
        AssertFailure(
            tree.AddPanel(DockPanelIds.Console, new DockTarget.Center(BottomGroupId)),
            DockError.DuplicatePanel,
            tree,
            "add an existing panel instance");
        AssertFailure(
            tree.AddPanel(Assets, new DockTarget.Invalid("not droppable")),
            DockError.InvalidTarget,
            tree,
            "add with an invalid target");
        AssertFailure(
            tree.MergeGroups(SceneGroupId, SceneGroupId),
            DockError.AlreadyInState,
            tree,
            "merge a group into itself");
        AssertFailure(
            tree.MergeGroups(new DockNodeId("dock.tabs.missing"), BottomGroupId),
            DockError.TargetGone,
            tree,
            "merge a missing source group");

        Assert.AreEqual(digest, tree.Digest);
    }

    [TestMethod]
    public void Successful_operations_return_new_trees_without_mutating_the_receiver()
    {
        var tree = DockTree.CreateDefault();
        var digest = tree.Digest;

        var added = tree.AddPanel(Assets, new DockTarget.Center(BottomGroupId));
        Assert.IsTrue(added.Success, added.Diagnostic);

        Assert.AreEqual(digest, tree.Digest);
        Assert.AreEqual(4, tree.EnumeratePanels().Count());
        Assert.AreNotEqual(digest, added.Tree!.Digest);
        Assert.AreEqual(5, added.Tree.EnumeratePanels().Count());
    }

    [TestMethod]
    public void Add_panel_appends_or_inserts_and_activates()
    {
        var tree = DockTree.CreateDefault();
        var before = tree.Digest;

        var appended = tree.AddPanel(Assets, new DockTarget.Center(BottomGroupId));
        Assert.IsTrue(appended.Success, appended.Diagnostic);
        var appendedTree = appended.Tree!;
        Assert.IsTrue(appendedTree.Validate().IsValid);
        var bottom = appendedTree.FindGroup(BottomGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console, Assets },
            bottom.Panels);
        Assert.AreEqual(Assets, bottom.ActivePanel);
        Assert.AreEqual(5, appendedTree.EnumeratePanels().Count());
        Assert.AreEqual(before, tree.Digest);
        Assert.AreNotEqual(before, appendedTree.Digest);

        var inserted = tree.AddPanel(Assets, new DockTarget.Center(BottomGroupId, 1));
        Assert.IsTrue(inserted.Success, inserted.Diagnostic);
        AssertPanels(
            new[] { DockPanelIds.Inspector, Assets, DockPanelIds.Console },
            inserted.Tree!.FindGroup(BottomGroupId)!.Panels);

        AssertFailure(
            tree.AddPanel(default, new DockTarget.Center(BottomGroupId)),
            DockError.InvalidSource,
            tree,
            "add an empty panel id");
        AssertFailure(
            tree.AddPanel(Assets, new DockTarget.Center(new DockNodeId("dock.tabs.missing"))),
            DockError.TargetGone,
            tree,
            "add to a missing group");
    }

    [TestMethod]
    public void Add_panel_on_edge_of_root_group_replaces_the_root()
    {
        var tree = DockTree.Create(Group("dock.root", DockPanelIds.Hierarchy));

        var result = tree.AddPanel(Assets, new DockTarget.Edge(new DockNodeId("dock.root"), DockEdge.Top));
        Assert.IsTrue(result.Success, result.Diagnostic);
        var root = RequireSplit(result.Tree!.Root);
        Assert.AreEqual(DockOrientation.Vertical, root.Orientation);
        AssertPanels(new[] { Assets }, RequireGroup(root.First).Panels);
        AssertPanels(new[] { DockPanelIds.Hierarchy }, RequireGroup(root.Second).Panels);
        Assert.IsTrue(result.Tree.Validate().IsValid);
    }

    [TestMethod]
    public void Move_panel_across_groups_and_within_group()
    {
        var tree = DockTree.CreateDefault();

        var across = tree.MovePanel(DockPanelIds.Console, new DockTarget.Center(LeftGroupId));
        Assert.IsTrue(across.Success, across.Diagnostic);
        var acrossTree = across.Tree!;
        Assert.IsTrue(acrossTree.Validate().IsValid);
        var left = acrossTree.FindGroup(LeftGroupId)!;
        AssertPanels(new[] { DockPanelIds.Hierarchy, DockPanelIds.Console }, left.Panels);
        Assert.AreEqual(DockPanelIds.Console, left.ActivePanel);
        var bottom = acrossTree.FindGroup(BottomGroupId)!;
        AssertPanels(new[] { DockPanelIds.Inspector }, bottom.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
        Assert.AreEqual(4, acrossTree.EnumeratePanels().Count());

        var within = tree.MovePanel(DockPanelIds.Inspector, new DockTarget.Center(BottomGroupId, 1));
        Assert.IsTrue(within.Success, within.Diagnostic);
        var reordered = within.Tree!.FindGroup(BottomGroupId)!;
        AssertPanels(new[] { DockPanelIds.Console, DockPanelIds.Inspector }, reordered.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, reordered.ActivePanel);
    }

    [TestMethod]
    public void Move_panel_to_edge_of_its_own_group_splits_the_survivors()
    {
        var tree = DockTree.CreateDefault();

        var result = tree.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Edge(BottomGroupId, DockEdge.Right));
        Assert.IsTrue(result.Success, result.Diagnostic);
        var moved = result.Tree!;
        Assert.IsTrue(moved.Validate().IsValid);

        var main = RequireSplit(moved.Root);
        var center = RequireSplit(main.Second);
        var split = RequireSplit(center.Second);
        Assert.AreEqual(DockOrientation.Horizontal, split.Orientation);
        var survivor = RequireGroup(split.First);
        Assert.AreEqual(BottomGroupId, survivor.Id);
        AssertPanels(new[] { DockPanelIds.Console }, survivor.Panels);
        Assert.AreEqual(DockPanelIds.Console, survivor.ActivePanel);
        AssertPanels(new[] { DockPanelIds.Inspector }, RequireGroup(split.Second).Panels);
    }

    [TestMethod]
    public void Reorder_tab_preserves_active_panel_and_rejects_no_ops()
    {
        var tree = DockTree.CreateDefault();

        var result = tree.ReorderTab(DockPanelIds.Console, BottomGroupId, 0);
        Assert.IsTrue(result.Success, result.Diagnostic);
        var reordered = result.Tree!;
        Assert.IsTrue(reordered.Validate().IsValid);
        var bottom = reordered.FindGroup(BottomGroupId)!;
        AssertPanels(new[] { DockPanelIds.Console, DockPanelIds.Inspector }, bottom.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);

        AssertFailure(
            reordered.ReorderTab(DockPanelIds.Console, BottomGroupId, 0),
            DockError.AlreadyInState,
            reordered,
            "repeat reorder");
        AssertFailure(
            reordered.ReorderTab(DockPanelIds.Scene, BottomGroupId, 0),
            DockError.InvalidSource,
            reordered,
            "reorder a panel outside the group");
        AssertFailure(
            reordered.ReorderTab(DockPanelIds.Console, new DockNodeId("dock.tabs.missing"), 0),
            DockError.TargetGone,
            reordered,
            "reorder in a missing group");
    }

    [TestMethod]
    public void Merge_groups_appends_source_panels_and_preserves_target_activity()
    {
        var tree = DockTree.CreateDefault();
        var before = tree.Digest;

        var result = tree.MergeGroups(SceneGroupId, BottomGroupId);
        Assert.IsTrue(result.Success, result.Diagnostic);
        var merged = result.Tree!;
        Assert.IsTrue(merged.Validate().IsValid, merged.Validate().Summary);

        var bottom = merged.FindGroup(BottomGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console, DockPanelIds.Scene },
            bottom.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
        Assert.IsNull(merged.FindGroup(SceneGroupId));
        Assert.AreEqual(4, merged.EnumeratePanels().Count());
        Assert.AreEqual(4, merged.EnumeratePanels().Distinct().Count());

        var main = RequireSplit(merged.Root);
        Assert.AreEqual(LeftGroupId, RequireGroup(main.First).Id);
        Assert.AreEqual(BottomGroupId, RequireGroup(main.Second).Id);
        Assert.AreEqual(before, tree.Digest);
        Assert.AreNotEqual(before, merged.Digest);

        AssertFailure(
            merged.MergeGroups(SceneGroupId, BottomGroupId),
            DockError.TargetGone,
            merged,
            "merge an already removed source group");
        AssertFailure(
            tree.MergeGroups(SceneGroupId, new DockNodeId("dock.tabs.missing")),
            DockError.TargetGone,
            tree,
            "merge into a missing target group");
    }

    [TestMethod]
    public void Resize_split_normalizes_weights_and_rejects_invalid_input()
    {
        var tree = DockTree.CreateDefault();

        var result = tree.ResizeSplit(MainId, 1d, 3d);
        Assert.IsTrue(result.Success, result.Diagnostic);
        var resizedTree = result.Tree!;
        var split = resizedTree.FindSplit(MainId)!;
        Assert.AreEqual(0.25d, split.FirstWeight, 1e-9);
        Assert.AreEqual(0.75d, split.SecondWeight, 1e-9);
        Assert.IsTrue(resizedTree.Validate().IsValid);

        AssertFailure(
            resizedTree.ResizeSplit(MainId, 1d, 3d),
            DockError.AlreadyInState,
            resizedTree,
            "repeat resize");
        AssertFailure(
            resizedTree.ResizeSplit(MainId, double.NaN, 1d),
            DockError.InvalidModel,
            resizedTree,
            "NaN weight");
        AssertFailure(
            resizedTree.ResizeSplit(MainId, 0d, 1d),
            DockError.InvalidModel,
            resizedTree,
            "zero weight");
        AssertFailure(
            tree.ResizeSplit(new DockNodeId("dock.split.missing"), 0.5d, 0.5d),
            DockError.InvalidTarget,
            tree,
            "resize a missing split");
    }

    [TestMethod]
    public void Remove_panel_collapses_empty_structures()
    {
        var tree = DockTree.CreateDefault();

        var result = tree.RemovePanel(DockPanelIds.Scene);
        Assert.IsTrue(result.Success, result.Diagnostic);
        var removed = result.Tree!;
        Assert.IsTrue(removed.Validate().IsValid);

        Assert.IsNull(removed.FindGroup(SceneGroupId));
        Assert.AreEqual(3, removed.EnumeratePanels().Count());
        var main = RequireSplit(removed.Root);
        Assert.AreEqual(LeftGroupId, RequireGroup(main.First).Id);
        Assert.AreEqual(BottomGroupId, RequireGroup(main.Second).Id);
        Assert.AreEqual(DockPanelIds.Inspector, RequireGroup(main.Second).ActivePanel);

        AssertFailure(
            removed.RemovePanel(DockPanelIds.Scene),
            DockError.InvalidSource,
            removed,
            "remove an already removed panel");
    }

    [TestMethod]
    public void Unknown_panel_ids_are_opaque_and_do_not_need_a_registry()
    {
        var timeline = new PanelInstanceId("plugin.timeline");
        var tree = DockTree.Create(Group("dock.tabs.custom", timeline, DockPanelIds.Hierarchy));

        Assert.IsTrue(tree.Validate().IsValid);
        Assert.IsTrue(tree.TryFindPanel(timeline, out var group, out var index));
        Assert.AreEqual(new DockNodeId("dock.tabs.custom"), group!.Id);
        Assert.AreEqual(0, index);

        var added = tree.AddPanel(new PanelInstanceId("plugin.assets"), new DockTarget.Center(group.Id));
        Assert.IsTrue(added.Success, added.Diagnostic);
        Assert.IsTrue(added.Tree!.Validate().IsValid);
    }

    private static DockTree SingleBottomGroup()
    {
        return DockTree.Create(new DockSplitNode(
            new DockNodeId("dock.main"),
            DockOrientation.Horizontal,
            Group("dock.tabs.left", DockPanelIds.Hierarchy),
            new DockSplitNode(
                new DockNodeId("dock.split.center"),
                DockOrientation.Vertical,
                Group("dock.tabs.scene", DockPanelIds.Scene),
                Group("dock.tabs.bottom", DockPanelIds.Inspector),
                0.6d,
                0.4d),
            0.4d,
            0.6d));
    }

    private static DockTabGroupNode Group(
        string id,
        PanelInstanceId first,
        params PanelInstanceId[] rest)
    {
        var panels = new[] { first }.Concat(rest).ToArray();
        return new DockTabGroupNode(new DockNodeId(id), panels, first);
    }

    private static DockNode Chain(int splits)
    {
        DockNode node = Group("dock.chain.0", DockPanelIds.Hierarchy);
        for (var index = 0; index < splits; index++)
        {
            node = new DockSplitNode(
                new DockNodeId($"dock.chain.split.{index}"),
                DockOrientation.Vertical,
                node,
                Group($"dock.chain.leaf.{index}", new PanelInstanceId($"editor.chain.{index}")),
                0.5d,
                0.5d);
        }

        return node;
    }

    private static DockNode BalancedTree(int leaves, int panelsPerLeaf = 1)
    {
        var nodeCounter = 0;
        var panelCounter = 0;
        return Build(leaves);

        DockNode Build(int count)
        {
            if (count == 1)
            {
                var panels = Enumerable.Range(0, panelsPerLeaf)
                    .Select(_ => new PanelInstanceId($"editor.balanced.{panelCounter++}"))
                    .ToArray();
                return new DockTabGroupNode(
                    new DockNodeId($"dock.balanced.g{nodeCounter++}"),
                    panels,
                    panels[0]);
            }

            var leftCount = count / 2;
            var rightCount = count - leftCount;
            return new DockSplitNode(
                new DockNodeId($"dock.balanced.s{nodeCounter++}"),
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

    private static void AssertFailure(
        DockOperationResult result,
        DockError expectedError,
        DockTree unchanged,
        string what)
    {
        Assert.IsFalse(result.Success, $"{what}: expected failure.");
        Assert.AreEqual(expectedError, result.Error, $"{what}: error.");
        Assert.IsNull(result.Tree, $"{what}: a failed operation must not return a tree.");
        Assert.IsTrue(unchanged.Validate().IsValid, $"{what}: the original tree must stay valid.");
    }
}
