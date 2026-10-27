using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DockHiddenLifecycleTests
{
    private static readonly DockNodeId LeftGroupId = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroupId = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroupId = new("dock.tabs.bottom");
    private static readonly DockBounds DefaultBounds = new(120d, 90d, 480d, 320d);
    private static readonly PanelInstanceId Alpha = new("test.alpha");
    private static readonly PanelInstanceId Beta = new("test.beta");
    private static readonly PanelInstanceId Gamma = new("test.gamma");

    // WM-20: hiding a non-active panel keeps identity, order, group ownership
    // and activity; showing it again restores the exact W3 digest.
    [TestMethod]
    public void Wm20_hide_and_show_round_trip_restores_order_active_and_digest()
    {
        var original = DockWorkspace.CreateDefault();
        var hidden = RequireSuccess(original.HidePanel(DockPanelIds.Console));

        Assert.AreEqual(1L, hidden.Revision);
        Assert.AreEqual(4, hidden.EnumeratePanels().Count());
        Assert.AreEqual(3, hidden.EnumerateVisiblePanels().Count());
        AssertPanels(new[] { DockPanelIds.Console }, hidden.EnumerateHiddenPanels());
        Assert.IsTrue(hidden.Validate().IsValid, hidden.Validate().Summary);

        var bottom = hidden.MainRoot.FindGroup(BottomGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels);
        AssertPanels(new[] { DockPanelIds.Console }, bottom.HiddenPanels);
        AssertPanels(new[] { DockPanelIds.Inspector }, bottom.VisiblePanels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
        Assert.IsTrue(bottom.IsHidden(DockPanelIds.Console));
        Assert.IsFalse(bottom.IsHidden(DockPanelIds.Inspector));
        Assert.AreNotEqual(original.Digest, hidden.Digest);

        var shown = RequireSuccess(hidden.ShowPanel(DockPanelIds.Console));
        Assert.AreEqual(2L, shown.Revision);
        Assert.AreEqual(0, shown.EnumerateHiddenPanels().Count());
        Assert.AreEqual(
            DockPanelIds.Inspector,
            shown.MainRoot.FindGroup(BottomGroupId)!.ActivePanel);
        Assert.AreEqual(original.MainRoot.Digest, shown.MainRoot.Digest);
        Assert.AreEqual(original.Digest, shown.Digest);
        Assert.IsTrue(shown.Validate().IsValid, shown.Validate().Summary);

        Assert.AreEqual(0L, original.Revision);
        Assert.AreEqual(0, original.EnumerateHiddenPanels().Count());
        Assert.AreEqual(0, DockWorkspace.CreateDefault().EnumerateHiddenPanels().Count());

        var restored = RequireSuccess(
            RequireSuccess(shown.HidePanel(DockPanelIds.Console)).RestoreDefault());
        Assert.AreEqual(0, restored.EnumerateHiddenPanels().Count());
        Assert.AreEqual(original.Digest, restored.Digest);
        Assert.IsTrue(restored.Validate().IsValid, restored.Validate().Summary);
    }

    // WM-21: hiding the active tab selects the next visible tab, otherwise the
    // nearest preceding visible tab, and never wraps around.
    [TestMethod]
    public void Wm21_hiding_active_tab_picks_next_then_previous_without_wrap()
    {
        var next = GroupTree(new[] { Alpha, Beta, Gamma }, active: Beta);
        Assert.AreEqual(
            Gamma,
            RequireGroup(RequireSuccess(next.HidePanel(Beta)).Root).ActivePanel);

        var previous = GroupTree(new[] { Alpha, Beta, Gamma }, active: Gamma);
        Assert.AreEqual(
            Beta,
            RequireGroup(RequireSuccess(previous.HidePanel(Gamma)).Root).ActivePanel);

        var skipsHidden = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.tabs.skip"),
            new[] { Alpha, Beta, Gamma },
            Beta,
            new[] { Gamma }));
        Assert.AreEqual(
            Alpha,
            RequireGroup(RequireSuccess(skipsHidden.HidePanel(Beta)).Root).ActivePanel);

        var first = GroupTree(new[] { Alpha, Beta }, active: Alpha);
        Assert.AreEqual(
            Beta,
            RequireGroup(RequireSuccess(first.HidePanel(Alpha)).Root).ActivePanel);

        var last = GroupTree(new[] { Alpha, Beta }, active: Beta);
        Assert.AreEqual(
            Alpha,
            RequireGroup(RequireSuccess(last.HidePanel(Beta)).Root).ActivePanel);

        var keepsActivity = GroupTree(new[] { Alpha, Beta, Gamma }, active: Beta);
        var hiddenLast = RequireSuccess(keepsActivity.HidePanel(Gamma));
        Assert.AreEqual(
            Beta,
            RequireGroup(hiddenLast.Root).ActivePanel);
        AssertPanels(
            new[] { Alpha, Beta },
            RequireGroup(hiddenLast.Root).VisiblePanels);
    }

    // WM-22: hiding or closing the last visible panel is rejected atomically;
    // removing a whole source group as part of a move stays legal.
    [TestMethod]
    public void Wm22_last_visible_panel_cannot_be_hidden_or_closed()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console));

        AssertWorkspaceFailure(
            workspace.HidePanel(DockPanelIds.Inspector),
            DockWorkspaceError.WouldEmptyGroup,
            workspace,
            "hide the last visible panel");
        AssertWorkspaceFailure(
            workspace.RemovePanel(DockPanelIds.Inspector),
            DockWorkspaceError.WouldEmptyGroup,
            workspace,
            "close the last visible panel");

        var bottom = workspace.MainRoot.FindGroup(BottomGroupId)!;
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
        AssertPanels(new[] { DockPanelIds.Console }, bottom.HiddenPanels);

        var single = GroupTree(
            new[] { Alpha, Beta },
            active: Alpha,
            hidden: new[] { Beta });
        AssertTreeFailure(
            single.HidePanel(Alpha),
            DockError.WouldEmptyGroup,
            single,
            "hide the only visible panel");

        var onlyPanel = GroupTree(new[] { Alpha });
        AssertTreeFailure(
            onlyPanel.HidePanel(Alpha),
            DockError.WouldEmptyGroup,
            onlyPanel,
            "hide a single-panel group");

        var leftId = new DockNodeId("dock.legal.left");
        var rightId = new DockNodeId("dock.legal.right");
        var split = DockTree.Create(new DockSplitNode(
            new DockNodeId("dock.legal.split"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(leftId, new[] { Alpha }, Alpha),
            new DockTabGroupNode(rightId, new[] { Beta }, Beta),
            0.5d,
            0.5d));

        var moved = RequireSuccess(
            split.MovePanel(Alpha, new DockTarget.Center(rightId)));
        Assert.AreEqual(2, moved.EnumeratePanels().Count());
        var survivor = RequireGroup(moved.Root);
        AssertPanels(new[] { Beta, Alpha }, survivor.Panels);
        Assert.AreEqual(Alpha, survivor.ActivePanel);
        Assert.IsTrue(moved.Validate().IsValid);
    }

    // WM-23: a hidden panel can be closed (model-only removal), while closing
    // a visible panel with hidden siblings still fails; the last main-root
    // panel keeps its existing WouldEmptyMainRoot rule.
    [TestMethod]
    public void Wm23_hidden_panel_can_be_closed()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console));
        var digest = workspace.Digest;

        var closed = RequireSuccess(workspace.RemovePanel(DockPanelIds.Console));
        Assert.AreEqual(workspace.Revision + 1, closed.Revision);
        Assert.AreEqual(3, closed.EnumeratePanels().Count());
        Assert.AreEqual(0, closed.EnumerateHiddenPanels().Count());
        Assert.AreNotEqual(digest, closed.Digest);
        Assert.IsTrue(closed.Validate().IsValid, closed.Validate().Summary);

        var bottom = closed.MainRoot.FindGroup(BottomGroupId)!;
        AssertPanels(new[] { DockPanelIds.Inspector }, bottom.Panels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);

        var singleRoot = DockWorkspace.Create(
            DockTree.Create(new DockTabGroupNode(
                new DockNodeId("dock.tabs.single"),
                new[] { Alpha },
                Alpha)));
        AssertWorkspaceFailure(
            singleRoot.RemovePanel(Alpha),
            DockWorkspaceError.WouldEmptyMainRoot,
            singleRoot,
            "close the last main-root panel");
    }

    // WM-24: hidden panels reject activation, reorder, move and float as a
    // source; every failure leaves the model and W3 digest unchanged.
    [TestMethod]
    public void Wm24_hidden_panel_rejects_activation_reorder_move_and_float()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console));
        var tree = workspace.MainRoot;
        var revision = workspace.Revision;
        var digest = workspace.Digest;

        AssertTreeFailure(
            tree.ActivateTab(BottomGroupId, DockPanelIds.Console),
            DockError.PanelHidden,
            tree,
            "activate a hidden panel");
        AssertTreeFailure(
            tree.ReorderTab(DockPanelIds.Console, BottomGroupId, 0),
            DockError.PanelHidden,
            tree,
            "reorder a hidden panel");
        AssertTreeFailure(
            tree.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Center(SceneGroupId)),
            DockError.PanelHidden,
            tree,
            "move a hidden panel");
        AssertTreeFailure(
            tree.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Edge(SceneGroupId, DockEdge.Left)),
            DockError.PanelHidden,
            tree,
            "edge-move a hidden panel");
        AssertTreeFailure(
            tree.HidePanel(DockPanelIds.Console),
            DockError.AlreadyInState,
            tree,
            "hide an already hidden panel");
        AssertTreeFailure(
            tree.ShowPanel(DockPanelIds.Inspector),
            DockError.AlreadyInState,
            tree,
            "show a visible panel");

        AssertWorkspaceFailure(
            workspace.ActivateTab(BottomGroupId, DockPanelIds.Console),
            DockWorkspaceError.PanelHidden,
            workspace,
            "workspace activate a hidden panel");
        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Center(SceneGroupId)),
            DockWorkspaceError.PanelHidden,
            workspace,
            "workspace move a hidden panel");
        AssertWorkspaceFailure(
            workspace.FloatPanel(DockPanelIds.Console, DefaultBounds),
            DockWorkspaceError.PanelHidden,
            workspace,
            "float a hidden panel");
        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Console,
                new DockTarget.Float(DefaultBounds)),
            DockWorkspaceError.PanelHidden,
            workspace,
            "float a hidden panel through a move target");

        Assert.AreEqual(revision, workspace.Revision);
        Assert.AreEqual(digest, workspace.Digest);

        var shown = RequireSuccess(workspace.ShowPanel(DockPanelIds.Console));
        var activated = RequireSuccess(
            shown.ActivateTab(BottomGroupId, DockPanelIds.Console));
        Assert.AreEqual(
            DockPanelIds.Console,
            activated.MainRoot.FindGroup(BottomGroupId)!.ActivePanel);
    }

    // WM-25: hidden marks travel with panels when groups merge, when a visible
    // panel leaves a group with hidden siblings, and when a float root merges
    // or re-docks.
    [TestMethod]
    public void Wm25_hidden_state_follows_group_merge_and_float_redock()
    {
        var sourceId = new DockNodeId("dock.hidden.source");
        var targetId = new DockNodeId("dock.hidden.target");
        var split = DockTree.Create(new DockSplitNode(
            new DockNodeId("dock.hidden.split"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                sourceId,
                new[] { Alpha, Beta },
                Alpha,
                new[] { Beta }),
            new DockTabGroupNode(targetId, new[] { Gamma }, Gamma),
            0.5d,
            0.5d));

        var merged = RequireSuccess(split.MergeGroups(sourceId, targetId));
        var mergedGroup = merged.FindGroup(targetId)!;
        AssertPanels(new[] { Gamma, Alpha, Beta }, mergedGroup.Panels);
        AssertPanels(new[] { Beta }, mergedGroup.HiddenPanels);
        Assert.AreEqual(Gamma, mergedGroup.ActivePanel);
        Assert.IsNull(merged.FindGroup(sourceId));
        Assert.IsTrue(merged.Validate().IsValid, merged.Validate().Summary);

        var moveSourceId = new DockNodeId("dock.move.source");
        var moveTargetId = new DockNodeId("dock.move.target");
        var delta = new PanelInstanceId("test.delta");
        var moveTree = DockTree.Create(new DockSplitNode(
            new DockNodeId("dock.move.split"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                moveSourceId,
                new[] { Alpha, Beta, Gamma },
                Gamma,
                new[] { Beta }),
            new DockTabGroupNode(moveTargetId, new[] { delta }, delta),
            0.5d,
            0.5d));

        var moved = RequireSuccess(
            moveTree.MovePanel(Gamma, new DockTarget.Center(moveTargetId)));
        var sourceAfter = moved.FindGroup(moveSourceId)!;
        AssertPanels(new[] { Alpha, Beta }, sourceAfter.Panels);
        AssertPanels(new[] { Beta }, sourceAfter.HiddenPanels);
        Assert.AreEqual(Alpha, sourceAfter.ActivePanel);
        Assert.IsFalse(sourceAfter.IsHidden(sourceAfter.ActivePanel!.Value));

        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Scene, DefaultBounds));
        var floatId = workspace.FloatingRoots[0].Id;
        var floatGroupId = RequireGroup(workspace.FloatingRoots[0].Root.Root).Id;

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(floatGroupId)));
        workspace = RequireSuccess(workspace.HidePanel(DockPanelIds.Inspector));
        var floatGroup = RequireGroup(workspace.FloatingRoots[0].Root.Root);
        Assert.AreEqual(DockPanelIds.Scene, floatGroup.ActivePanel);
        AssertPanels(new[] { DockPanelIds.Inspector }, floatGroup.HiddenPanels);
        AssertPanels(
            new[] { DockPanelIds.Scene },
            floatGroup.VisiblePanels);

        var shownInFloat = RequireSuccess(workspace.ShowPanel(DockPanelIds.Inspector));
        Assert.AreEqual(
            DockPanelIds.Scene,
            RequireGroup(shownInFloat.FloatingRoots[0].Root.Root).ActivePanel);
        workspace = RequireSuccess(shownInFloat.HidePanel(DockPanelIds.Inspector));

        var redocked = RequireSuccess(workspace.ReDockFloatRoot(floatId));
        Assert.AreEqual(0, redocked.FloatingRoots.Count);
        AssertPanels(
            new[] { DockPanelIds.Inspector },
            redocked.EnumerateHiddenPanels());

        var left = redocked.MainRoot.FindGroup(LeftGroupId)!;
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene, DockPanelIds.Inspector },
            left.Panels);
        AssertPanels(new[] { DockPanelIds.Inspector }, left.HiddenPanels);
        Assert.AreEqual(DockPanelIds.Hierarchy, left.ActivePanel);
        Assert.AreEqual(4, redocked.EnumeratePanels().Count());
        Assert.AreEqual(3, redocked.EnumerateVisiblePanels().Count());
        Assert.IsTrue(redocked.Validate().IsValid, redocked.Validate().Summary);
    }

    // WM-26: floating or moving the last visible panel out of a group with
    // hidden siblings is rejected atomically; after showing the hidden panel
    // the same operation succeeds.
    [TestMethod]
    public void Wm26_moving_or_floating_last_visible_panel_is_rejected()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console));

        AssertWorkspaceFailure(
            workspace.FloatPanel(DockPanelIds.Inspector, DefaultBounds),
            DockWorkspaceError.WouldEmptyGroup,
            workspace,
            "float the last visible panel");
        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Inspector,
                new DockTarget.Center(SceneGroupId)),
            DockWorkspaceError.WouldEmptyGroup,
            workspace,
            "move the last visible panel to a center target");
        AssertWorkspaceFailure(
            workspace.MovePanel(
                DockPanelIds.Inspector,
                new DockTarget.Edge(SceneGroupId, DockEdge.Bottom)),
            DockWorkspaceError.WouldEmptyGroup,
            workspace,
            "move the last visible panel to an edge target");

        var shown = RequireSuccess(workspace.ShowPanel(DockPanelIds.Console));
        var floated = RequireSuccess(shown.FloatPanel(DockPanelIds.Inspector, DefaultBounds));
        Assert.AreEqual(1, floated.FloatingRoots.Count);
        Assert.AreEqual(
            DockPanelIds.Console,
            floated.MainRoot.FindGroup(BottomGroupId)!.ActivePanel);
    }

    // WM-27: hidden-only groups are rejected by validation and normalization
    // never promotes hidden panels; hidden declarations are canonicalized to
    // panel order and hidden active panels are repaired to a visible panel.
    [TestMethod]
    public void Wm27_hidden_only_groups_are_rejected_and_hidden_order_is_normalized()
    {
        var hiddenOnly = new DockTabGroupNode(
            new DockNodeId("dock.hidden.only"),
            new[] { Alpha, Beta },
            null,
            new[] { Alpha, Beta });

        var validation = DockTree.Validate(hiddenOnly);
        Assert.IsTrue(validation.Has("empty-visible"), validation.Summary);
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(hiddenOnly));

        var normalization = DockTree.Normalize(hiddenOnly);
        var normalized = RequireGroup(normalization.Root);
        AssertPanels(new[] { Alpha, Beta }, normalized.Panels);
        AssertPanels(new[] { Alpha, Beta }, normalized.HiddenPanels);
        Assert.AreEqual(0, normalized.VisiblePanels.Count);
        Assert.IsFalse(DockTree.Validate(normalization.Root).IsValid);

        var outsideGroup = new DockTabGroupNode(
            new DockNodeId("dock.hidden.outside"),
            new[] { Alpha, Beta },
            Beta,
            new[] { Gamma });
        Assert.IsTrue(DockTree.Validate(outsideGroup).Has("invalid-hidden"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(outsideGroup));

        var duplicated = new DockTabGroupNode(
            new DockNodeId("dock.hidden.duplicate"),
            new[] { Alpha, Beta },
            Beta,
            new[] { Alpha, Alpha });
        Assert.IsTrue(DockTree.Validate(duplicated).Has("duplicate-hidden"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(duplicated));

        var hiddenActive = new DockTabGroupNode(
            new DockNodeId("dock.hidden.active"),
            new[] { Alpha, Beta },
            Alpha,
            new[] { Alpha });
        Assert.IsTrue(DockTree.Validate(hiddenActive).Has("hidden-active"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(hiddenActive));

        var emptyHidden = new DockTabGroupNode(
            new DockNodeId("dock.hidden.empty"),
            new[] { Alpha, Beta },
            Alpha,
            new[] { default(PanelInstanceId) });
        Assert.IsTrue(DockTree.Validate(emptyHidden).Has("invalid-hidden"));

        var unordered = new DockTabGroupNode(
            new DockNodeId("dock.hidden.order"),
            new[] { Alpha, Beta, Gamma },
            Alpha,
            new[] { Gamma, Alpha });
        var reordered = DockTree.Normalize(unordered);
        Assert.IsTrue(reordered.Changed);
        var reorderedGroup = RequireGroup(reordered.Root);
        AssertPanels(new[] { Alpha, Gamma }, reorderedGroup.HiddenPanels);
        Assert.AreEqual(Beta, reorderedGroup.ActivePanel);
        Assert.IsTrue(DockTree.Create(reordered.Root!).Validate().IsValid);
    }

    // WM-28: W3 digests are canonical for the hidden subset, independent of
    // declaration order, and accept opaque unknown panel identities.
    [TestMethod]
    public void Wm28_w3_digest_is_canonical_for_hidden_state_and_resolution()
    {
        var groupId = new DockNodeId("dock.hidden.digest");
        var canonical = DockTree.Create(new DockTabGroupNode(
            groupId,
            new[] { Alpha, Beta, Gamma },
            Beta,
            new[] { Alpha, Gamma }));
        var reordered = DockTree.Create(new DockTabGroupNode(
            groupId,
            new[] { Alpha, Beta, Gamma },
            Beta,
            new[] { Gamma, Alpha }));

        AssertPanels(
            new[] { Alpha, Gamma },
            canonical.FindGroup(groupId)!.HiddenPanels);
        AssertPanels(
            new[] { Alpha, Gamma },
            reordered.FindGroup(groupId)!.HiddenPanels);
        Assert.AreEqual(canonical.Digest, reordered.Digest);
        Assert.AreEqual(canonical.Digest, DockTree.ComputeDigest(canonical.Root));

        var visible = GroupTree(new[] { Alpha, Beta, Gamma }, active: Beta);
        var otherHidden = DockTree.Create(new DockTabGroupNode(
            groupId,
            new[] { Alpha, Beta, Gamma },
            Beta,
            new[] { Gamma }));
        Assert.AreNotEqual(visible.Digest, canonical.Digest);
        Assert.AreNotEqual(canonical.Digest, otherHidden.Digest);

        var opaque = new PanelInstanceId("third.party.timeline");
        var unresolvedHidden = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.hidden.unresolved"),
            new[] { opaque, Alpha },
            Alpha,
            new[] { opaque }));
        Assert.IsTrue(unresolvedHidden.Validate().IsValid);
        AssertPanels(new[] { opaque }, unresolvedHidden.EnumerateHiddenPanels());
        AssertPanels(new[] { Alpha }, unresolvedHidden.EnumerateVisiblePanels());
        Assert.AreEqual(
            DockTree.ComputeDigest(unresolvedHidden.Root),
            DockTree.Create(new DockTabGroupNode(
                new DockNodeId("dock.hidden.unresolved"),
                new[] { opaque, Alpha },
                Alpha,
                new[] { opaque })).Digest);
    }

    // WM-29: hidden panels count toward the 256-panel limit and toward global
    // panel identity, exactly like visible panels.
    [TestMethod]
    public void Wm29_hidden_panels_count_toward_limits_and_global_identity()
    {
        var panels = Enumerable.Range(0, 256)
            .Select(index => new PanelInstanceId($"editor.hidden.{index}"))
            .ToArray();
        var hidden = panels.Take(255).ToArray();
        var full = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.hidden.limit"),
            panels,
            panels[255],
            hidden));

        Assert.IsTrue(full.Validate().IsValid, full.Validate().Summary);
        Assert.AreEqual(256, full.EnumeratePanels().Count());
        Assert.AreEqual(255, full.EnumerateHiddenPanels().Count());
        Assert.AreEqual(1, full.EnumerateVisiblePanels().Count());

        var overflowPanels = panels
            .Append(new PanelInstanceId("editor.hidden.256"))
            .ToArray();
        var overflow = new DockTabGroupNode(
            new DockNodeId("dock.hidden.limit"),
            overflowPanels,
            overflowPanels[256],
            overflowPanels.Take(256).ToArray());
        Assert.IsTrue(DockTree.Validate(overflow).Has("panel-limit"));
        Assert.ThrowsException<DockModelException>(() => DockTree.Create(overflow));

        var duplicateAcrossGroups = new DockSplitNode(
            new DockNodeId("dock.hidden.duplicate.split"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                new DockNodeId("dock.hidden.duplicate.left"),
                new[] { Alpha, Beta },
                Alpha,
                new[] { Beta }),
            new DockTabGroupNode(
                new DockNodeId("dock.hidden.duplicate.right"),
                new[] { Beta },
                Beta),
            0.5d,
            0.5d);
        var duplicateValidation = DockTree.Validate(duplicateAcrossGroups);
        Assert.IsTrue(duplicateValidation.Has("duplicate-panel"), duplicateValidation.Summary);
    }

    private static DockTree GroupTree(
        IReadOnlyList<PanelInstanceId> panels,
        PanelInstanceId? active = null,
        IReadOnlyList<PanelInstanceId>? hidden = null)
        => DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.tabs.test"),
            panels,
            active ?? panels[0],
            hidden));

    private static DockTree RequireSuccess(DockOperationResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Tree);
        return result.Tree!;
    }

    private static DockWorkspace RequireSuccess(DockWorkspaceResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Workspace);
        return result.Workspace!;
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

    private static void AssertTreeFailure(
        DockOperationResult result,
        DockError expectedError,
        DockTree unchanged,
        string what)
    {
        var digest = unchanged.Digest;
        var snapshot = TreeSnapshot(unchanged);

        Assert.IsFalse(result.Success, $"{what}: expected failure.");
        Assert.AreEqual(expectedError, result.Error, $"{what}: error.");
        Assert.IsNull(result.Tree, $"{what}: a failed operation must not return a tree.");
        Assert.AreEqual(digest, unchanged.Digest, $"{what}: digest must not change.");
        Assert.AreEqual(snapshot, TreeSnapshot(unchanged), $"{what}: tree must not change.");
        Assert.IsTrue(unchanged.Validate().IsValid, $"{what}: the original tree must stay valid.");
    }

    private static void AssertWorkspaceFailure(
        DockWorkspaceResult result,
        DockWorkspaceError expectedError,
        DockWorkspace unchanged,
        string what)
    {
        var revision = unchanged.Revision;
        var digest = unchanged.Digest;
        var snapshot = WorkspaceSnapshot(unchanged);

        Assert.IsFalse(result.Success, $"{what}: expected failure.");
        Assert.AreEqual(expectedError, result.Error, $"{what}: error.");
        Assert.IsNull(result.Workspace, $"{what}: a failed operation must not return a workspace.");
        Assert.AreEqual(revision, unchanged.Revision, $"{what}: revision must not change.");
        Assert.AreEqual(digest, unchanged.Digest, $"{what}: digest must not change.");
        Assert.AreEqual(
            snapshot,
            WorkspaceSnapshot(unchanged),
            $"{what}: trees, hidden state and active panels must not change.");
        Assert.IsTrue(unchanged.Validate().IsValid, $"{what}: the original workspace must stay valid.");
    }

    private static string WorkspaceSnapshot(DockWorkspace workspace)
        => string.Join(
            ";;",
            new[] { workspace.MainRoot }
                .Concat(workspace.FloatingRoots.Select(root => root.Root))
                .Select(TreeSnapshot));

    private static string TreeSnapshot(DockTree tree)
        => string.Join(
            ";",
            tree.EnumerateNodes()
                .OfType<DockTabGroupNode>()
                .Select(group =>
                    $"{group.Id}[{string.Join(",", group.Panels)}"
                    + $"|h:{string.Join(",", group.HiddenPanels)}"
                    + $"|a:{group.ActivePanel}]"));
}
