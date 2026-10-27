using System.Reflection;
using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DockLayoutHistoryTests
{
    private static readonly DockNodeId MainSplitId = new("dock.main");
    private static readonly DockNodeId CenterSplitId = new("dock.split.center");
    private static readonly DockNodeId LeftGroupId = new("dock.tabs.left");
    private static readonly DockNodeId SceneGroupId = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroupId = new("dock.tabs.bottom");
    private static readonly DockBounds FloatBounds = new(120d, 90d, 480d, 320d);
    private static readonly DockBounds MovedBounds = new(220d, 140d, 640d, 420d);
    private static readonly PanelInstanceId Report = new("plugin.report.panel");
    private static readonly PanelInstanceId Timeline = new("plugin.timeline.panel");

    public TestContext? TestContext { get; set; }

    // LH-01: a snapshot keeps layout content and drops the revision, so a
    // replay joins the commit sequence the caller is on instead of rewinding it.
    [TestMethod]
    public void Lh01_capture_keeps_content_and_adopts_the_revision_it_is_replayed_at()
    {
        var workspace = DockWorkspace.CreateDefault();
        workspace = RequireSuccess(workspace.FloatPanel(DockPanelIds.Console, FloatBounds));
        var floatId = workspace.FloatingRoots[0].Id;
        workspace = RequireSuccess(
            workspace.UpdateFloatBounds(floatId, MovedBounds, maximized: true));
        Assert.AreEqual(2L, workspace.Revision);

        var snapshot = DockLayoutSnapshot.Capture(workspace);

        Assert.AreEqual(workspace.Digest, snapshot.Digest);
        Assert.AreEqual(workspace.MainRoot.Digest, snapshot.MainRoot.Digest);
        Assert.AreEqual(1, snapshot.FloatingRoots.Count);

        var restored = snapshot.RestoreWorkspace(revision: 41);
        Assert.AreEqual(41L, restored.Revision, "a replay must adopt the caller's revision");
        Assert.AreEqual(workspace.Digest, restored.Digest);
        Assert.AreEqual(2L, workspace.Revision, "restoring must not touch the source workspace");

        var floated = restored.FloatingRoots[0];
        Assert.AreEqual(floatId, floated.Id);
        Assert.AreEqual(MovedBounds, floated.Bounds);
        Assert.AreEqual(FloatBounds, floated.RestoreBounds);
        Assert.IsTrue(floated.Maximized);
        Assert.AreEqual(BottomGroupId, floated.ReturnTarget.GroupId);
        AssertPanels(workspace.EnumeratePanels(), restored.EnumeratePanels());
    }

    // LH-02: one recorded change becomes one undo step, and the redo step
    // returns the exact content that was undone.
    [TestMethod]
    public void Lh02_undo_restores_the_previous_layout_and_redo_restores_the_new_one()
    {
        var workspace = DockWorkspace.CreateDefault();
        var baseline = DockLayoutSnapshot.Capture(workspace);
        var history = new DockLayoutHistory();
        history.Reset(baseline);

        Assert.AreEqual(1, history.Count);
        Assert.IsFalse(history.CanUndo);
        Assert.IsFalse(history.CanRedo);

        var hidden = Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        Assert.AreSame(hidden, history.Current);
        Assert.AreEqual(1, history.UndoableCount);
        Assert.AreEqual(0, history.RedoableCount);
        Assert.AreEqual(2, history.Count);

        var undone = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(baseline.Digest, undone.Digest);
        Assert.IsFalse(undone.EnumerateHiddenPanels().Any());
        Assert.IsFalse(history.CanUndo);
        Assert.IsTrue(history.CanRedo);
        Assert.AreEqual(1, history.RedoableCount);

        var redone = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(hidden.Digest, redone.Digest);
        AssertPanels(new[] { DockPanelIds.Console }, redone.EnumerateHiddenPanels());
        Assert.IsTrue(history.CanUndo);
        Assert.IsFalse(history.CanRedo);
    }

    // LH-03: walking the cursor keeps every entry and the live model in step.
    [TestMethod]
    public void Lh03_consecutive_undo_and_redo_keep_the_cursor_and_the_model_in_step()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        var steps = new List<DockLayoutSnapshot> { history.Current! };
        steps.Add(Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console)));
        steps.Add(Commit(history, ref workspace, w => w.HidePanel(Report)));
        steps.Add(Commit(history, ref workspace, w => w.ResizeSplit(MainSplitId, 0.4d, 0.6d)));

        Assert.AreEqual(4, history.Count);
        Assert.AreEqual(3, history.UndoableCount);

        for (var depth = 3; depth >= 1; depth--)
        {
            var undone = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
            Assert.AreEqual(steps[depth - 1].Digest, undone.Digest, $"undo to depth {depth - 1}");
            Assert.AreEqual(depth - 1, history.UndoableCount, "undo budget after the replay");
            Assert.AreEqual(4 - depth, history.RedoableCount, "redo budget after the replay");
        }

        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(3, history.RedoableCount);

        for (var depth = 1; depth <= 3; depth++)
        {
            var redone = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
            Assert.AreEqual(steps[depth].Digest, redone.Digest, $"redo to depth {depth}");
            Assert.AreEqual(depth, history.UndoableCount, "undo budget after the replay");
            Assert.AreEqual(3 - depth, history.RedoableCount, "redo budget after the replay");
        }

        Assert.AreEqual(3, history.UndoableCount);
        Assert.IsFalse(history.CanRedo);
    }

    // LH-04: replays are new commits. Revision only ever moves forward, even
    // when the content moves backwards.
    [TestMethod]
    public void Lh04_undo_and_redo_advance_the_revision_instead_of_rewinding_it()
    {
        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        var revisions = new List<long>();
        for (var step = 1; step <= 4; step++)
        {
            var weight = 0.11d + (step * 0.07d);
            Commit(history, ref workspace, w => w.ResizeSplit(MainSplitId, weight, 1d - weight));
            Assert.AreEqual(step, workspace.Revision, $"commit sequence after change {step}");
            revisions.Add(workspace.Revision);
        }

        for (var step = 0; step < 3; step++)
        {
            Replay(history, DockLayoutOperationSource.Undo, ref workspace);
            revisions.Add(workspace.Revision);
        }

        for (var step = 0; step < 2; step++)
        {
            Replay(history, DockLayoutOperationSource.Redo, ref workspace);
            revisions.Add(workspace.Revision);
        }

        for (var index = 1; index < revisions.Count; index++)
            Assert.IsTrue(
                revisions[index] > revisions[index - 1],
                $"revision {revisions[index]} follows {revisions[index - 1]}: a replay must not rewind");

        Assert.AreEqual(9L, revisions[^1], "nine commits advanced the sequence once each");
        Assert.AreEqual(5L, revisions[4], "the first undo joined the sequence at the next revision");
    }

    // LH-05: a new operation after an undo drops only the entries above the
    // cursor; the undo budget below it stays intact.
    [TestMethod]
    public void Lh05_a_new_operation_after_undo_discards_the_redo_branch()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        var first = Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        Commit(history, ref workspace, w => w.HidePanel(Report));
        Assert.AreEqual(3, history.Count);

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(1, history.RedoableCount);

        Commit(
            history,
            ref workspace,
            w => w.ResizeSplit(MainSplitId, 0.35d, 0.65d),
            "discarded 1 redo target");
        Assert.AreEqual(0, history.RedoableCount);
        Assert.AreEqual(3, history.Count);
        Assert.AreEqual(2, history.UndoableCount);

        var back = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(first.Digest, back.Digest, "the undo budget below the cursor survived");
    }

    // LH-06: a commit that reproduces the current content — the shape of the
    // ReplaceMainRoot digest-equal branch — adds no entry and keeps redo.
    [TestMethod]
    public void Lh06_an_unchanged_commit_adds_no_entry_and_keeps_the_redo_branch()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        Commit(history, ref workspace, w => w.HidePanel(Report));
        var currentLayout = Replay(history, DockLayoutOperationSource.Undo, ref workspace);

        var count = history.Count;
        var redoable = history.RedoableCount;
        Assert.AreEqual(1, history.UndoableCount);

        var unchanged = history.Record(DockLayoutSnapshot.Capture(currentLayout));
        Assert.AreEqual(DockLayoutHistoryOutcome.Unchanged, unchanged.Outcome);
        StringAssert.Contains(unchanged.Diagnostic, "already current");
        Assert.AreEqual(count, history.Count);
        Assert.AreEqual(redoable, history.RedoableCount);
        Assert.AreEqual(1, history.UndoableCount);

        var sameEntry = history.Record(history.Current!);
        Assert.AreEqual(DockLayoutHistoryOutcome.Unchanged, sameEntry.Outcome);
        Assert.AreEqual(count, history.Count);
        Assert.AreEqual(redoable, history.RedoableCount);

        Commit(
            history,
            ref workspace,
            w => w.ResizeSplit(MainSplitId, 0.3d, 0.7d),
            "discarded 1 redo target");
        Assert.AreEqual(count, history.Count);
        Assert.AreEqual(2, history.UndoableCount);
    }

    // LH-07: the commit funnel can notify the same commit twice. Neither an
    // operation nor a replay may be counted twice.
    [TestMethod]
    public void Lh07_a_duplicated_notification_adds_no_entry_and_moves_no_cursor()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        var first = Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        var second = Commit(history, ref workspace, w => w.HidePanel(Report));
        Assert.AreEqual(3, history.Count);

        var duplicated = history.Record(second);
        Assert.AreEqual(DockLayoutHistoryOutcome.Unchanged, duplicated.Outcome);
        Assert.AreEqual(3, history.Count);
        Assert.AreEqual(2, history.UndoableCount);

        var target = Target(history, DockLayoutOperationSource.Undo);
        var replayed = history.Record(target, DockLayoutOperationSource.Undo);
        Assert.AreEqual(DockLayoutHistoryOutcome.CursorMoved, replayed.Outcome);
        Assert.AreEqual(1, history.UndoableCount);

        var notifiedAgain = history.Record(target, DockLayoutOperationSource.Undo);
        Assert.AreEqual(DockLayoutHistoryOutcome.Unchanged, notifiedAgain.Outcome);
        Assert.AreEqual(
            1,
            history.UndoableCount,
            "a repeated undo notification moved the cursor twice");
        Assert.AreSame(first, history.Current);
    }

    // LH-08: replays that match nothing the history promised are rejected
    // without touching entries, the cursor, or the redo branch.
    [TestMethod]
    public void Lh08_a_replay_that_matches_no_pending_target_is_rejected()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        var empty = new DockLayoutHistory();
        var noBaseline = empty.Record(
            DockLayoutSnapshot.Capture(workspace),
            DockLayoutOperationSource.Undo);
        Assert.AreEqual(DockLayoutHistoryOutcome.Rejected, noBaseline.Outcome);
        StringAssert.Contains(noBaseline.Diagnostic, "no baseline to replay against");
        Assert.AreEqual(0, empty.Count);
        Assert.IsNull(empty.Current);
        Assert.IsFalse(empty.TryGetUndoTarget(out _));
        Assert.IsFalse(empty.TryGetRedoTarget(out _));

        var seeded = empty.Record(DockLayoutSnapshot.Capture(workspace));
        Assert.AreEqual(DockLayoutHistoryOutcome.Recorded, seeded.Outcome);
        Assert.AreEqual(1, empty.Count);
        Assert.IsNotNull(empty.Current);
        Assert.IsFalse(empty.CanUndo, "the first commit on an unprimed history has no pre-state");

        Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        Commit(history, ref workspace, w => w.HidePanel(Report));
        var count = history.Count;
        var undoable = history.UndoableCount;
        var otherLayout = DockLayoutSnapshot.Capture(DockWorkspace.CreateDefault());

        var wrongTarget = history.Record(otherLayout, DockLayoutOperationSource.Undo);
        Assert.AreEqual(DockLayoutHistoryOutcome.Rejected, wrongTarget.Outcome);
        StringAssert.Contains(wrongTarget.Diagnostic, "does not match the pending undo target");
        Assert.AreEqual(count, history.Count);
        Assert.AreEqual(undoable, history.UndoableCount);

        var nothingToRedo = history.Record(otherLayout, DockLayoutOperationSource.Redo);
        Assert.AreEqual(DockLayoutHistoryOutcome.Rejected, nothingToRedo.Outcome);
        StringAssert.Contains(nothingToRedo.Diagnostic, "no layout left to redo");
        Assert.AreEqual(0, history.RedoableCount);

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(1, history.RedoableCount);
        var skippedRedo = history.Record(otherLayout, DockLayoutOperationSource.Redo);
        Assert.AreEqual(DockLayoutHistoryOutcome.Rejected, skippedRedo.Outcome);
        StringAssert.Contains(skippedRedo.Diagnostic, "does not match the pending redo target");
        Assert.AreEqual(1, history.RedoableCount);
        Assert.AreEqual(1, history.UndoableCount);
        Assert.AreEqual(count, history.Count);
    }

    // LH-09: the frozen budget is 100 undoable layout changes, which needs 101
    // retained entries; capping storage at 100 would have silently cut it to 99.
    [TestMethod]
    public void Lh09_a_hundred_layout_changes_stay_undoable_at_the_capacity_limit()
    {
        Assert.AreEqual(100, DockLayoutHistory.DefaultMaxUndoDepth);

        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory();
        var baseline = DockLayoutSnapshot.Capture(workspace);
        history.Reset(baseline);

        for (var step = 1; step <= 100; step++)
        {
            var weight = 0.1d + (step * 0.001d);
            var recorded = Commit(
                history,
                ref workspace,
                w => w.ResizeSplit(MainSplitId, weight, 1d - weight));
            Assert.AreEqual(step, history.UndoableCount, $"undoable budget after change {step}");
            Assert.AreEqual(step + 1, history.Count, $"entries after change {step}");
            Assert.AreEqual(0, history.RedoableCount);
            Assert.AreSame(recorded, history.Current);
        }

        Assert.AreEqual(100, history.UndoableCount);
        Assert.AreEqual(101, history.Count);
        Assert.AreEqual(
            history.Count - 1,
            history.UndoableCount,
            "the stored entries must exactly cover the reachable undo depth");

        for (var step = 100; step >= 1; step--)
        {
            Replay(history, DockLayoutOperationSource.Undo, ref workspace);
            Assert.AreEqual(step - 1, history.UndoableCount, $"undo budget at step {step}");
            Assert.AreEqual(101 - step, history.RedoableCount, $"redo budget at step {step}");
        }

        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(baseline.Digest, history.Current!.Digest);
        Assert.AreEqual(100, history.RedoableCount);
        Assert.AreEqual(101, history.Count);
    }

    // LH-10: crossing the limit evicts exactly the oldest layout, keeps the
    // budget at 100, and never disturbs the current layout or the cursor.
    [TestMethod]
    public void Lh10_the_newest_change_evicts_the_oldest_and_keeps_a_hundred_undos()
    {
        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        DockLayoutSnapshot? firstChange = null;
        for (var step = 1; step <= 101; step++)
        {
            var weight = 0.1d + (step * 0.001d);
            var recorded = Commit(
                history,
                ref workspace,
                w => w.ResizeSplit(MainSplitId, weight, 1d - weight));
            if (step == 1)
                firstChange = recorded;

            Assert.AreEqual(
                Math.Min(step, 100),
                history.UndoableCount,
                $"undoable budget at step {step}");
            Assert.AreEqual(Math.Min(step + 1, 101), history.Count, $"entry count at step {step}");
            Assert.AreSame(recorded, history.Current, "eviction must not change the current layout");
        }

        Assert.IsNotNull(firstChange);
        Assert.AreEqual(0, history.RedoableCount);

        for (var step = 100; step >= 1; step--)
            Replay(history, DockLayoutOperationSource.Undo, ref workspace);

        Assert.AreEqual(
            firstChange!.Digest,
            history.Current!.Digest,
            "the deepest surviving undo must return the state after the first change");
        Assert.IsFalse(history.CanUndo);
        Assert.IsFalse(
            history.TryGetUndoTarget(out _),
            "the evicted baseline must not stay reachable");
        Assert.AreEqual(100, history.RedoableCount);
    }

    // LH-11: truncating the redo branch frees the entries it held, and further
    // operations refill the budget without ever exceeding it.
    [TestMethod]
    public void Lh11_truncating_the_redo_branch_restores_the_full_capacity_rule()
    {
        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory(maxUndoDepth: 3);
        history.Reset(DockLayoutSnapshot.Capture(workspace));
        Assert.AreEqual(3, history.MaxUndoDepth);

        var changes = new List<DockLayoutSnapshot>();
        for (var step = 1; step <= 5; step++)
        {
            var weight = 0.1d + (step * 0.01d);
            changes.Add(Commit(
                history,
                ref workspace,
                w => w.ResizeSplit(MainSplitId, weight, 1d - weight)));
            Assert.AreEqual(Math.Min(step, 3), history.UndoableCount, $"undoable budget at step {step}");
            Assert.AreEqual(Math.Min(step + 1, 4), history.Count, $"entries at step {step}");
        }

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(1, history.UndoableCount);
        Assert.AreEqual(2, history.RedoableCount);
        Assert.AreEqual(4, history.Count);

        var branch = Commit(
            history,
            ref workspace,
            w => w.ResizeSplit(MainSplitId, 0.29d, 0.71d),
            "discarded 2 redo target(s)");
        Assert.AreEqual(0, history.RedoableCount);
        Assert.AreEqual(2, history.UndoableCount);
        Assert.AreEqual(3, history.Count, "the dropped redo branch freed its entries");
        Assert.AreSame(branch, history.Current);

        Commit(history, ref workspace, w => w.ResizeSplit(MainSplitId, 0.31d, 0.69d));
        var refilled = Commit(history, ref workspace, w => w.ResizeSplit(MainSplitId, 0.33d, 0.67d));
        Assert.AreEqual(3, history.UndoableCount, "the budget refills to the configured limit");
        Assert.AreEqual(4, history.Count, "the budget is never exceeded by refilling");
        Assert.AreSame(refilled, history.Current);

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        var deepest = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(changes[2].Digest, deepest.Digest, "the walk ends on the oldest retained layout");
        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(3, history.RedoableCount);
    }

    // LH-12: one history entry covers every root. Undo restores the main tree
    // and each floating root's identity, geometry and return target together,
    // including panels the close removed from the live workspace.
    [TestMethod]
    public void Lh12_undo_restores_the_main_root_and_every_floating_root_together()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));
        var originalPanels = workspace.EnumeratePanels().ToArray();

        var floatedConsole = Commit(
            history,
            ref workspace,
            w => w.FloatPanel(DockPanelIds.Console, FloatBounds));
        var consoleFloat = workspace.FloatingRoots[0].Id;

        var movedBounds = Commit(
            history,
            ref workspace,
            w => w.UpdateFloatBounds(consoleFloat, MovedBounds, maximized: false));

        var floatedReport = Commit(history, ref workspace, w => w.FloatPanel(Report, FloatBounds));
        Assert.AreEqual(2, workspace.FloatingRoots.Count);
        var floatIds = workspace.FloatingRoots.Select(root => root.Id.ToString()).ToArray();
        var reportReturn = FloatRoot(workspace, floatIds[1]).ReturnTarget;

        var reportClosed = Commit(
            history,
            ref workspace,
            w => w.CloseFloatRoot(new DockFloatRootId(floatIds[1])));
        var bothClosed = Commit(history, ref workspace, w => w.CloseFloatRoot(consoleFloat));
        Assert.AreEqual(0, workspace.FloatingRoots.Count);
        Assert.AreEqual(4, workspace.EnumeratePanels().Count(), "both floated panels left the workspace");
        Assert.AreEqual(6, history.Count);

        var oneFloat = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(reportClosed.Digest, oneFloat.Digest);
        Assert.AreEqual(1, oneFloat.FloatingRoots.Count);
        Assert.AreEqual(consoleFloat, oneFloat.FloatingRoots[0].Id);
        Assert.AreEqual(MovedBounds, oneFloat.FloatingRoots[0].RestoreBounds);
        Assert.IsTrue(oneFloat.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.IsFalse(oneFloat.EnumeratePanels().Contains(Report));

        var bothRoots = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(floatedReport.Digest, bothRoots.Digest);
        Assert.AreEqual(2, bothRoots.FloatingRoots.Count);
        CollectionAssert.AreEqual(
            floatIds,
            bothRoots.FloatingRoots.Select(root => root.Id.ToString()).ToArray());
        Assert.AreEqual(MovedBounds, FloatRoot(bothRoots, consoleFloat.ToString()).RestoreBounds);
        Assert.AreEqual(FloatBounds, FloatRoot(bothRoots, floatIds[1]).RestoreBounds);
        var returned = FloatRoot(bothRoots, floatIds[1]).ReturnTarget;
        Assert.AreEqual(reportReturn.GroupId, returned.GroupId);
        Assert.AreEqual(reportReturn.Index, returned.Index);
        AssertPanels(
            originalPanels.OrderBy(panel => panel.ToString(), StringComparer.Ordinal),
            bothRoots.EnumeratePanels().OrderBy(panel => panel.ToString(), StringComparer.Ordinal),
            "undo must bring every panel identity back, including the closed one");

        var stillMoved = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(movedBounds.Digest, stillMoved.Digest);
        Assert.AreEqual(1, stillMoved.FloatingRoots.Count);

        var onlyFloat = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(floatedConsole.Digest, onlyFloat.Digest);
        Assert.AreEqual(FloatBounds, onlyFloat.FloatingRoots[0].RestoreBounds);

        var pristine = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(0, pristine.FloatingRoots.Count);
        AssertPanels(originalPanels, pristine.EnumeratePanels());
        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(5, history.RedoableCount);

        var firstRedo = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(floatedConsole.Digest, firstRedo.Digest);
        for (var step = 0; step < 4; step++)
            Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(bothClosed.Digest, workspace.Digest);
        Assert.IsFalse(history.CanRedo);
    }

    // LH-13: hidden membership, its order and unresolved panel identity are
    // content, so they survive a replay without any built-in mapping.
    [TestMethod]
    public void Lh13_undo_preserves_hidden_order_and_unresolved_panel_identity()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        AssertPanels(new[] { Timeline }, workspace.EnumerateHiddenPanels());

        var bothHidden = Commit(history, ref workspace, w => w.HidePanel(Report));
        AssertPanels(new[] { Report, Timeline }, workspace.EnumerateHiddenPanels());
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene, DockPanelIds.Inspector, DockPanelIds.Console },
            workspace.EnumerateVisiblePanels());

        Commit(history, ref workspace, w => w.ShowPanel(Timeline));

        var undone = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(bothHidden.Digest, undone.Digest);
        AssertPanels(new[] { Report, Timeline }, undone.EnumerateHiddenPanels());
        Assert.IsTrue(undone.MainRoot.FindGroup(LeftGroupId)!.IsHidden(Report), "hidden state was lost");
        Assert.IsTrue(undone.MainRoot.FindGroup(SceneGroupId)!.IsHidden(Timeline));
        Assert.IsTrue(
            undone.EnumeratePanels().Contains(Report),
            "an unresolved panel must keep its opaque identity through a replay");

        var baseline = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.IsFalse(baseline.MainRoot.FindGroup(LeftGroupId)!.IsHidden(Report));
        AssertPanels(new[] { Timeline }, baseline.EnumerateHiddenPanels());
        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(2, history.RedoableCount);

        var redone = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(bothHidden.Digest, redone.Digest);
        AssertPanels(new[] { Report, Timeline }, redone.EnumerateHiddenPanels());
    }

    // LH-14: closing or resetting a layout stays undoable as pure model
    // content. The identity comes back; the released view never does.
    [TestMethod]
    public void Lh14_undo_after_close_and_reset_restores_identity_without_the_old_view()
    {
        var workspace = DockWorkspace.Create(PanelHeavyTree());
        var history = new DockLayoutHistory();
        var beforeClose = DockLayoutSnapshot.Capture(workspace);
        history.Reset(beforeClose);
        var bottomOrder = workspace.MainRoot.FindGroup(BottomGroupId)!.Panels.ToArray();

        Commit(history, ref workspace, w => w.RemovePanel(DockPanelIds.Console));
        Assert.IsFalse(workspace.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.AreEqual(2, history.Count);

        var panelBack = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(beforeClose.Digest, panelBack.Digest);
        AssertPanels(bottomOrder, panelBack.MainRoot.FindGroup(BottomGroupId)!.Panels);
        Assert.IsTrue(panelBack.Validate().IsValid, panelBack.Validate().Summary);
        Assert.IsTrue(panelBack.EnumeratePanels().Contains(DockPanelIds.Console));

        // Reset is an ordinary history item: undo returns the pre-reset layout,
        // including hidden membership and unresolved identities.
        Commit(history, ref workspace, static w => w.RestoreDefault());
        Assert.IsFalse(workspace.EnumeratePanels().Contains(Report));
        Assert.AreEqual(0, workspace.EnumerateHiddenPanels().Count());
        Assert.AreEqual(3L, workspace.Revision);

        var afterResetUndo = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(beforeClose.Digest, afterResetUndo.Digest);
        Assert.IsTrue(afterResetUndo.EnumeratePanels().Contains(Report));
        Assert.IsTrue(afterResetUndo.EnumeratePanels().Contains(Timeline));
        AssertPanels(new[] { Timeline }, afterResetUndo.EnumerateHiddenPanels());
        Assert.AreNotSame(panelBack, afterResetUndo, "a replay builds a new workspace instance");
        Assert.AreEqual(4L, afterResetUndo.Revision, "undoing a reset advances the revision");
        Assert.IsTrue(history.TryGetRedoTarget(out var redoTarget), "the undone reset stays redoable");
        Assert.AreEqual(2, history.Count, "recording the reset truncated the redo branch it replaced");
        Assert.AreEqual(0, history.UndoableCount);
        Assert.AreEqual(1, history.RedoableCount);
        Assert.IsTrue(
            !string.Equals(redoTarget.Digest, afterResetUndo.Digest, StringComparison.Ordinal),
            "the redo target is the reset layout, not the restored one");

        var resetAgain = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(redoTarget.Digest, resetAgain.Digest);
        Assert.IsFalse(resetAgain.EnumeratePanels().Contains(Report));
        Assert.AreEqual(5L, resetAgain.Revision);
    }

    // LH-15: identical content keeps one identity, but content identity is not
    // history position: the same layout may legally sit at two cursor depths.
    [TestMethod]
    public void Lh15_identical_content_keeps_one_digest_and_two_history_positions()
    {
        var workspace = DockWorkspace.CreateDefault();
        var baseline = DockLayoutSnapshot.Capture(workspace);
        var history = new DockLayoutHistory();
        history.Reset(baseline);

        var hidden = Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        var shownAgain = Commit(history, ref workspace, w => w.ShowPanel(DockPanelIds.Console));

        Assert.AreEqual(baseline.Digest, shownAgain.Digest, "the same layout keeps the same W3 digest");
        Assert.AreNotSame(baseline, shownAgain);
        Assert.AreEqual(3, history.Count, "a repeated layout is still a new history position");
        Assert.AreEqual(2, history.UndoableCount);

        var step = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(hidden.Digest, step.Digest);
        step = Replay(history, DockLayoutOperationSource.Undo, ref workspace);
        Assert.AreEqual(baseline.Digest, step.Digest);
        Assert.IsFalse(history.CanUndo);
        Assert.AreEqual(
            2,
            history.RedoableCount,
            "both positions of the identical layout stay redoable");

        var restored = baseline.RestoreWorkspace(revision: 77);
        Assert.AreEqual(baseline.Digest, restored.Digest);
        Assert.AreEqual(77L, restored.Revision);
        Assert.AreEqual(3, history.Count, "restoring content never rewrites the history");
    }

    // LH-16: the history payload is model content only. Anything that belongs
    // to a live host — views, handles, generations, tokens, revisions — would
    // make an old entry unusable, so it must not be reachable from a snapshot.
    [TestMethod]
    public void Lh16_the_history_payload_carries_model_content_and_no_runtime_state()
    {
        var snapshot = typeof(DockLayoutSnapshot);
        var properties = snapshot.GetProperties(BindingFlags.Public | BindingFlags.Instance);
        CollectionAssert.AreEqual(
            new[] { "Digest", "FloatingRoots", "MainRoot" },
            properties
                .Select(property => property.Name)
                .OrderBy(name => name, StringComparer.Ordinal)
                .ToArray(),
            "a snapshot exposes exactly its captured content");

        foreach (var property in properties)
        {
            var namespaceName = property.PropertyType.Namespace ?? string.Empty;
            Assert.IsTrue(
                namespaceName.StartsWith("Nova.UI", StringComparison.Ordinal)
                || namespaceName.StartsWith("System", StringComparison.Ordinal),
                $"Snapshot member '{property.Name}' has runtime type '{property.PropertyType}'.");
        }

        var forbidden = new[]
        {
            "View", "Handle", "Hwnd", "Token", "Generation", "Revision", "Window", "Control",
        };
        foreach (var member in properties
                     .Cast<MemberInfo>()
                     .Concat(snapshot.GetFields(BindingFlags.Public | BindingFlags.Instance)))
        {
            foreach (var needle in forbidden)
            {
                Assert.IsFalse(
                    member.Name.Contains(needle, StringComparison.OrdinalIgnoreCase),
                    $"Snapshot member '{member.Name}' carries host state '{needle}'.");
            }
        }

        var runtimeAssemblies = typeof(DockLayoutHistory)
            .Assembly
            .GetReferencedAssemblies()
            .Select(name => name.Name ?? string.Empty)
            .Where(name => name.StartsWith("Avalonia", StringComparison.Ordinal))
            .ToArray();
        CollectionAssert.AreEqual(
            Array.Empty<string>(),
            runtimeAssemblies,
            "the layout history must stay free of the UI host assemblies");
    }

    // LH-17: the capacity budget is only credible if 101 retained layouts are
    // affordable. Both numbers below are measured on this machine, not assumed.
    [TestMethod]
    public void Lh17_a_full_history_costs_a_measured_amount_of_memory()
    {
        var workspace = BuildLargeWorkspace(leaves: 48, floatRoots: 8);
        var history = new DockLayoutHistory();
        var splits = workspace.MainRoot
            .EnumerateNodes()
            .OfType<DockSplitNode>()
            .Select(node => node.Id)
            .ToArray();

        var sharedBytes = MeasureAllocatedBytes(() =>
        {
            var live = workspace;
            var kept = new List<DockWorkspace>();
            for (var step = 1; step <= 101; step++)
            {
                var weight = 0.1d + (step * 0.001d);
                live = RequireSuccess(live.ResizeSplit(
                    splits[step % splits.Length],
                    weight,
                    1d - weight));
                history.Record(DockLayoutSnapshot.Capture(live));
                kept.Add(live);
            }

            return kept;
        });

        Assert.AreEqual(100, history.UndoableCount);
        Assert.AreEqual(101, history.Count);

        var detachedBytes = MeasureAllocatedBytes(() =>
        {
            var kept = new List<DockLayoutSnapshot>();
            for (var step = 0; step < 101; step++)
                kept.Add(DockLayoutSnapshot.Capture(BuildLargeWorkspace(48, 8)));

            return kept;
        });

        var sharedPerEntry = sharedBytes / 101d;
        var detachedPerEntry = detachedBytes / 101d;
        TestContext?.WriteLine(
            $"D9-B snapshot memory over 101 layouts of a 48-panel workspace with 8 floating roots: "
            + $"sequential edits sharing untouched subtrees {sharedBytes / 1024d:F1} KiB "
            + $"({sharedPerEntry:F0} B/entry); independently built workspaces "
            + $"{detachedBytes / 1024d:F1} KiB ({detachedPerEntry:F0} B/entry).");

        Assert.IsTrue(
            sharedBytes <= 512 * 1024,
            $"shared-edit history cost {sharedBytes / 1024d:F1} KiB for 101 layouts.");
        Assert.IsTrue(
            detachedBytes <= 16 * 1024 * 1024,
            $"detached history cost {detachedBytes / 1024d:F1} KiB for 101 layouts.");
    }

    // LH-18: settlement calibration replaces only the current entry, and only
    // when the caller proves the entry still carries the expected content. It
    // never adds, evicts or truncates anything, and the redo branch survives.
    [TestMethod]
    public void Lh18_reconcile_replaces_only_the_current_entry_after_the_expected_precondition_matches()
    {
        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory();
        var snapshot = DockLayoutSnapshot.Capture(workspace);

        var missing = history.ReconcileCurrent(snapshot, snapshot);

        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Rejected, missing.Outcome);
        Assert.IsFalse(missing.Accepted);
        Assert.AreEqual(0, history.Count, "a calibration without an entry writes nothing");
        Assert.IsNull(history.Current);

        var baseline = snapshot;
        history.Reset(baseline);
        var committed = Commit(
            history,
            ref workspace,
            w => w.FloatPanel(DockPanelIds.Scene, FloatBounds));

        var expected = DockLayoutSnapshot.Capture(workspace);
        var settledWorkspace = RequireSuccess(workspace.UpdateFloatBounds(
            workspace.FloatingRoots[0].Id,
            MovedBounds,
            maximized: false));
        var settled = DockLayoutSnapshot.Capture(settledWorkspace);
        Assert.AreNotEqual(expected.Digest, settled.Digest, "the settled geometry differs");

        var mismatched = history.ReconcileCurrent(baseline, settled);

        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Rejected, mismatched.Outcome);
        StringAssert.Contains(mismatched.Diagnostic, "no longer matches");
        Assert.AreSame(committed, history.Current, "a mismatch leaves the entry untouched");
        Assert.AreEqual(2, history.Count);
        Assert.AreEqual(1, history.UndoableCount);
        Assert.AreEqual(0, history.RedoableCount);

        var reconciled = history.ReconcileCurrent(expected, settled);

        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Reconciled, reconciled.Outcome);
        Assert.IsTrue(reconciled.Accepted);
        Assert.AreSame(settled, history.Current, "only the current entry was replaced");
        Assert.AreEqual(2, history.Count, "no entry was added or evicted");
        Assert.AreEqual(1, history.UndoableCount, "the cursor did not move");
        Assert.AreEqual(0, history.RedoableCount);
        Assert.IsTrue(history.TryGetUndoTarget(out var undo));
        Assert.AreSame(baseline, undo, "the earlier entry is still the baseline");

        var idempotent = history.ReconcileCurrent(settled, settled);

        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Unchanged, idempotent.Outcome);
        Assert.IsTrue(idempotent.Accepted);
        Assert.AreSame(settled, history.Current);
        Assert.AreEqual(2, history.Count);

        var repeated = history.ReconcileCurrent(expected, settled);

        Assert.AreEqual(
            DockLayoutHistoryReconcileOutcome.Rejected,
            repeated.Outcome,
            "the pre-calibration content is no longer stored");
        Assert.AreSame(settled, history.Current);
        Assert.AreEqual(2, history.Count);

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);

        Assert.AreEqual(1, history.RedoableCount);
        Assert.IsTrue(history.TryGetRedoTarget(out var calibratedRedo));
        Assert.AreEqual(settled.Digest, calibratedRedo.Digest, "the redo replays the settled content");

        var resized = RequireSuccess(workspace.ResizeSplit(MainSplitId, 0.4d, 0.6d));
        var branchReconcile = history.ReconcileCurrent(
            history.Current!,
            DockLayoutSnapshot.Capture(resized));

        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Reconciled, branchReconcile.Outcome);
        Assert.AreEqual(2, history.Count, "calibration never adds an entry");
        Assert.AreEqual(0, history.UndoableCount);
        Assert.AreEqual(1, history.RedoableCount, "the redo branch survives calibration");
        Assert.IsTrue(history.TryGetRedoTarget(out var keptRedo));
        Assert.AreEqual(settled.Digest, keptRedo.Digest, "and its target was not rewritten");
        var branchPlayed = Replay(history, DockLayoutOperationSource.Redo, ref workspace);
        Assert.AreEqual(settled.Digest, branchPlayed.Digest);
        Assert.IsFalse(history.CanRedo);
    }

    // LH-19: replay stays strict. The pre-settlement snapshot of the same float
    // operation is no longer a valid redo target once the current entry was
    // calibrated; only the settled content replays.
    [TestMethod]
    public void Lh19_a_pre_settlement_snapshot_is_rejected_after_calibration()
    {
        var workspace = DockWorkspace.CreateDefault();
        var history = new DockLayoutHistory();
        history.Reset(DockLayoutSnapshot.Capture(workspace));

        Commit(history, ref workspace, w => w.HidePanel(DockPanelIds.Console));
        var floated = RequireSuccess(workspace.FloatPanel(DockPanelIds.Scene, FloatBounds));
        var unsettled = DockLayoutSnapshot.Capture(floated);
        var recorded = history.Record(unsettled);
        Assert.AreEqual(DockLayoutHistoryOutcome.Recorded, recorded.Outcome);
        workspace = floated;

        var settledWorkspace = RequireSuccess(workspace.UpdateFloatBounds(
            workspace.FloatingRoots[0].Id,
            MovedBounds,
            maximized: false));
        var settled = DockLayoutSnapshot.Capture(settledWorkspace);
        var reconciled = history.ReconcileCurrent(unsettled, settled);
        Assert.AreEqual(DockLayoutHistoryReconcileOutcome.Reconciled, reconciled.Outcome);

        Replay(history, DockLayoutOperationSource.Undo, ref workspace);

        Assert.AreEqual(1, history.RedoableCount);
        var staleRedo = history.Record(unsettled, DockLayoutOperationSource.Redo);

        Assert.AreEqual(DockLayoutHistoryOutcome.Rejected, staleRedo.Outcome);
        StringAssert.Contains(staleRedo.Diagnostic, "pending redo target");
        Assert.AreEqual(
            1,
            history.RedoableCount,
            "a rejected replay leaves the redo branch untouched");

        var redone = Replay(history, DockLayoutOperationSource.Redo, ref workspace);

        Assert.AreEqual(settled.Digest, redone.Digest, "only the settled geometry replays");
        Assert.AreEqual(
            3,
            history.Count,
            "baseline, hide and float stay three entries: calibration added none");
        Assert.IsFalse(history.CanRedo);
    }

    private static long MeasureAllocatedBytes(Func<object?> build)
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        var before = GC.GetTotalMemory(forceFullCollection: true);

        var root = build();

        var after = GC.GetTotalMemory(forceFullCollection: true);
        GC.KeepAlive(root);
        Assert.IsTrue(after > before, "the measurement window kept nothing alive");
        return after - before;
    }

    /// <summary>
    /// A main root whose groups each keep a visible panel spare, so a hide, a
    /// close and a float can all follow without emptying a group.
    /// </summary>
    private static DockTree PanelHeavyTree()
        => DockTree.Create(new DockSplitNode(
            MainSplitId,
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                LeftGroupId,
                new[] { DockPanelIds.Hierarchy, Report },
                DockPanelIds.Hierarchy),
            new DockSplitNode(
                CenterSplitId,
                DockOrientation.Vertical,
                new DockTabGroupNode(
                    SceneGroupId,
                    new[] { DockPanelIds.Scene, Timeline },
                    DockPanelIds.Scene,
                    new[] { Timeline }),
                new DockTabGroupNode(
                    BottomGroupId,
                    new[] { DockPanelIds.Inspector, DockPanelIds.Console },
                    DockPanelIds.Inspector),
                0.75d,
                0.25d),
            0.28d,
            0.72d));

    private static DockWorkspace BuildLargeWorkspace(int leaves, int floatRoots)
    {
        var nextNode = 0;
        var groupIds = new List<DockNodeId>();

        DockNode Build(int panelOffset, int count)
        {
            var nodeId = new DockNodeId($"dock.n{nextNode++}");
            if (count == 1)
            {
                var panel = new PanelInstanceId($"test.panel.{panelOffset}");
                groupIds.Add(nodeId);
                return new DockTabGroupNode(nodeId, new[] { panel }, panel);
            }

            var leftCount = count / 2;
            return new DockSplitNode(
                nodeId,
                DockOrientation.Horizontal,
                Build(panelOffset, leftCount),
                Build(panelOffset + leftCount, count - leftCount),
                0.5d,
                0.5d);
        }

        var main = DockTree.Create(Build(0, leaves));
        var floats = new List<DockFloatRoot>();
        for (var index = 1; index <= floatRoots; index++)
        {
            var id = $"dock.float.{index}";
            var panel = new PanelInstanceId($"test.float.panel.{index}");
            floats.Add(new DockFloatRoot(
                new DockFloatRootId(id),
                DockTree.Create(new DockTabGroupNode(
                    new DockNodeId($"{id}.tabs.1"),
                    new[] { panel },
                    panel)),
                new DockBounds(20d + (40d * index), 30d + (40d * index), 400d, 300d),
                maximized: false,
                new DockReturnTarget(groupIds[index % groupIds.Count], 0)));
        }

        return DockWorkspace.Create(main, floats);
    }

    private static DockLayoutSnapshot Target(
        DockLayoutHistory history,
        DockLayoutOperationSource source)
    {
        if (source == DockLayoutOperationSource.Undo)
        {
            Assert.IsTrue(history.TryGetUndoTarget(out var undo), "No undo target is available.");
            return undo;
        }

        Assert.IsTrue(history.TryGetRedoTarget(out var redo), "No redo target is available.");
        return redo;
    }

    private static DockLayoutSnapshot Commit(
        DockLayoutHistory history,
        ref DockWorkspace workspace,
        Func<DockWorkspace, DockWorkspaceResult> change,
        string? expectedDiagnostic = null)
    {
        var result = change(workspace);
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Workspace);
        workspace = result.Workspace!;

        var snapshot = DockLayoutSnapshot.Capture(workspace);
        var outcome = history.Record(snapshot);
        Assert.AreEqual(DockLayoutHistoryOutcome.Recorded, outcome.Outcome, outcome.Diagnostic);
        if (expectedDiagnostic is not null)
            StringAssert.Contains(outcome.Diagnostic, expectedDiagnostic);
        return snapshot;
    }

    private static DockWorkspace Replay(
        DockLayoutHistory history,
        DockLayoutOperationSource source,
        ref DockWorkspace workspace)
    {
        var target = Target(history, source);
        var previous = workspace.Revision;
        workspace = target.RestoreWorkspace(previous + 1);

        var outcome = history.Record(DockLayoutSnapshot.Capture(workspace), source);
        Assert.AreEqual(DockLayoutHistoryOutcome.CursorMoved, outcome.Outcome, outcome.Diagnostic);
        Assert.AreEqual(
            previous + 1,
            workspace.Revision,
            "a replay must carry the next revision of the live commit sequence");
        return workspace;
    }

    private static DockWorkspace RequireSuccess(DockWorkspaceResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Workspace);
        return result.Workspace!;
    }

    private static DockFloatRoot FloatRoot(DockWorkspace workspace, string id)
    {
        var root = workspace.FloatingRoots.FirstOrDefault(
            candidate => string.Equals(candidate.Id.ToString(), id, StringComparison.Ordinal));
        Assert.IsNotNull(root, $"Floating root '{id}' was not found.");
        return root!;
    }

    private static void AssertPanels(
        IEnumerable<PanelInstanceId> expected,
        IEnumerable<PanelInstanceId> actual,
        string? message = null)
    {
        var expectedText = string.Join(",", expected.Select(panel => panel.ToString()));
        var actualText = string.Join(",", actual.Select(panel => panel.ToString()));
        Assert.AreEqual(expectedText, actualText, message ?? "panel order differs");
    }
}
