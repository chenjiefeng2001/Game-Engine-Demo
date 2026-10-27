using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Nova.Editor.Services;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D9-E fault matrix. Every gate drives a real interaction — an actual splitter
/// thumb, an actual pointer press/move/release on the projected root, an actual
/// tab context menu — and then counts the layout history by replaying it,
/// because the history has no public counter by design.
/// </summary>
[TestClass]
public sealed class DockLayoutFaultMatrixTests
{
    private const int MaxLayoutSteps = 24;

    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId CenterSplit = new("dock.split.center");
    private static readonly DockNodeId SceneGroup = new("dock.tabs.scene");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly DockBounds FloatBounds = new(200, 160, 640, 480);

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d9e-fault-matrix", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    // LM-01: while the pointer is down the splitter changes only pixel previews,
    // so one complete drag is exactly one layout step and every aborted drag —
    // cancel, lost capture, zero distance — is none.
    [TestMethod]
    public void A_splitter_drag_is_one_layout_step_and_every_abort_is_none()
    {
        using var dock = CreateDock(out var baseline);
        Assert.IsFalse(dock.CanUndoLayout, "the session baseline is not an entry");

        var grid = LayoutMain(dock);
        var splitter = SplitterIn(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        for (var frame = 0; frame < 5; frame++)
            RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(20, 0));

        Assert.AreEqual(baseline, dock.Workspace.Digest, "a preview is not a layout change");
        Assert.IsFalse(dock.CanUndoLayout, "five preview frames recorded no layout step");

        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        var committed = dock.Workspace.Digest;
        Assert.IsTrue(dock.CanUndoLayout, "the release committed the drag once");
        Assert.AreEqual(
            1,
            WalkLayoutHistory(dock, committed, baseline),
            "one complete drag is one entry");

        grid = LayoutMain(dock);
        splitter = SplitterIn(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(50, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        var newest = dock.Workspace.Digest;
        Assert.AreNotEqual(committed, newest, "the second drag changed the layout");
        Assert.AreEqual(2, WalkLayoutHistory(dock, newest, baseline));

        // Cancelled, capture-lost and zero-distance drags all stop before the
        // commit funnel, so the entry count stays where the second drag left it.
        grid = LayoutMain(dock);
        splitter = SplitterIn(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        splitter.CancelDrag();
        Assert.IsFalse(splitter.CompleteDrag(), "a cancelled drag cannot commit");
        Assert.AreEqual(newest, dock.Workspace.Digest, "the cancel committed nothing");

        grid = LayoutMain(dock);
        splitter = SplitterIn(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        LoseCapture(splitter);
        Assert.AreEqual(newest, dock.Workspace.Digest, "a lost capture committed nothing");

        grid = LayoutMain(dock);
        splitter = SplitterIn(grid);
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));
        Assert.AreEqual(newest, dock.Workspace.Digest, "a zero-distance release committed nothing");

        Assert.AreEqual(2, WalkLayoutHistory(dock, newest, baseline));
    }

    // LM-02: an aborted panel drag records nothing, and an open drag session is a
    // hard block for replay in both directions. The release that ends the session
    // is one commit and truncates the redo branch like any other new change.
    [TestMethod]
    public void A_drag_session_blocks_replay_and_its_release_records_one_step()
    {
        using var dock = CreateDock(out var baseline);
        var revision = dock.Revision;

        var pointer = NewPointer();
        LayoutMain(dock);
        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);
        Assert.IsTrue(dock.IsDragging, "the real pointer opened a drag session");
        dock.Root.RaiseEvent(new PointerCaptureLostEventArgs(dock.Root, pointer));
        Release(dock.Root, new Point(300, 615), pointer);

        Assert.IsFalse(dock.IsDragging);
        Assert.AreEqual(baseline, dock.Workspace.Digest, "a lost capture committed no move");
        Assert.AreEqual(revision, dock.Revision, "and advanced no revision");
        Assert.IsFalse(dock.CanUndoLayout, "an aborted drag is not a layout step");

        Assert.IsTrue(dock.HidePanel(DockPanelIds.Console).Success);
        var first = dock.Workspace.Digest;
        Assert.IsTrue(dock.ShowPanel(DockPanelIds.Console).Success);

        Assert.IsTrue(dock.UndoLayout().Success);
        var cursor = dock.Workspace.Digest;
        Assert.AreEqual(first, cursor, "one undo returned the hidden console");
        Assert.IsTrue(dock.CanRedoLayout, "the undone show waits for a redo");
        revision = dock.Revision;

        pointer = NewPointer();
        LayoutMain(dock);
        Press(dock.Root, new Point(640, 15), pointer);
        Move(dock.Root, new Point(500, 615), pointer);

        Assert.IsTrue(dock.IsDragging, "a second session opened");
        Assert.IsFalse(dock.CanUndoLayout, "a drag session blocks undo");
        Assert.IsFalse(dock.CanRedoLayout, "and redo");

        var blockedUndo = dock.UndoLayout();
        var blockedRedo = dock.RedoLayout();

        Assert.IsFalse(blockedUndo.Success, "the blocked undo committed nothing");
        Assert.IsFalse(blockedRedo.Success, "the blocked redo committed nothing");
        Assert.AreEqual(cursor, dock.Workspace.Digest, "the layout stayed at the cursor");
        Assert.AreEqual(revision, dock.Revision, "and the revision stayed put");

        Release(dock.Root, new Point(500, 615), pointer);

        var dragged = dock.Workspace.Digest;
        Assert.AreNotEqual(cursor, dragged, "the release committed the move once");
        Assert.IsFalse(dock.CanRedoLayout, "a new commit truncates the redo branch");
        Assert.AreEqual(
            2,
            WalkLayoutHistory(dock, dragged, baseline),
            "the undone show is gone from the history, leaving the hide and the drag");
    }

    // LM-03: a float and the re-dock that reverses it are one entry each. Replaying
    // them returns the whole multi-root workspace — structure, panel identity,
    // floating geometry and the recorded return target — while the panel's own view
    // survives the trip between hosts untouched.
    [TestMethod]
    public void A_float_and_its_re_dock_replay_the_whole_multi_root_workspace()
    {
        var faults = new FloatFaults();
        using var dock = CreateFloatingDock(faults, out var baseline, out var views);

        Assert.IsTrue(
            dock.MovePanel(DockPanelIds.Scene, new DockTarget.Float(FloatBounds)).Success);
        var floated = dock.Workspace.Digest;
        var floatRoot = dock.Workspace.FloatingRoots.Single();
        Assert.AreEqual(1, dock.FloatCount, "the panel left the main root");
        Assert.IsNull(dock.Tree.FindGroup(SceneGroup), "and its group collapsed away");
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene },
            floatRoot.Root.EnumerateVisiblePanels().ToArray(),
            "the floating root holds exactly the moved panel");
        Assert.AreEqual(FloatBounds, floatRoot.Bounds, "with the requested geometry");
        Assert.AreEqual(1, WalkLayoutHistory(dock, floated, baseline), "one float is one entry");

        Assert.IsTrue(
            dock.MovePanel(DockPanelIds.Scene, new DockTarget.Center(BottomGroup, 1)).Success);
        var reDocked = dock.Workspace.Digest;
        Assert.AreEqual(0, dock.FloatCount, "the re-dock closed the floating root");
        var group = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Scene, DockPanelIds.Console },
            group.Panels.ToArray(),
            "and dropped the panel into the requested slot");
        Assert.AreEqual(
            2,
            WalkLayoutHistory(dock, reDocked, baseline),
            "the float and the re-dock are two separate entries");
        Assert.AreEqual(4, views.Calls.Count, "no replay rebuilt a single panel view");
        CollectionAssert.AreEqual(
            new int[] { 0, 0, 0, 0 },
            views.Views.Select(view => view.DisposeCount).ToArray(),
            "and none of them was released on the way");

        Assert.IsTrue(dock.UndoLayout().Success);

        Assert.AreEqual(floated, dock.Workspace.Digest, "the undo brought the float back");
        Assert.AreEqual(1, dock.FloatCount);
        var restored = dock.Workspace.FloatingRoots.Single();
        Assert.AreEqual(floatRoot.Id, restored.Id, "as the same floating identity");
        Assert.AreEqual(FloatBounds, restored.Bounds, "with the recorded geometry");
        Assert.AreEqual(
            floatRoot.ReturnTarget,
            restored.ReturnTarget,
            "and the recorded return target");
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene },
            restored.Root.EnumerateVisiblePanels().ToArray(),
            "holding the same panel");
        Assert.IsNull(dock.Tree.FindGroup(SceneGroup), "the main root is a docked-free match");

        Assert.IsTrue(dock.RedoLayout().Success);

        Assert.AreEqual(reDocked, dock.Workspace.Digest, "the redo re-applied the re-dock");
        Assert.AreEqual(0, dock.FloatCount);
        Assert.IsFalse(dock.CanRedoLayout, "and spent the branch");
    }

    // LM-04: the history holds model identity only. Undoing a real context-menu
    // Close brings back the panel's identity, order, hidden membership, active tab
    // and geometry through a brand-new view — never the released one.
    [TestMethod]
    public void Undo_of_a_real_close_restores_identity_order_visibility_and_active_tab()
    {
        using var dock = CreateDock(out var baseline, out var views);

        var pointer = NewPointer();
        LayoutMain(dock);
        Press(dock.Root, new Point(640, 15), pointer);
        Move(dock.Root, new Point(640, 615), pointer);
        Release(dock.Root, new Point(640, 615), pointer);

        var moved = dock.Workspace.Digest;
        var group = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Scene, DockPanelIds.Console },
            group.Panels.ToArray(),
            "the real drop put the scene tab between the bottom panels");

        var hiddenView = Require<RecordingView>(dock.FindPanel(DockPanelIds.Scene));
        ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Scene).ContextMenu, "Hide Panel");
        var hidden = dock.Workspace.Digest;
        Assert.IsNull(dock.FindPanel(DockPanelIds.Scene), "the hidden panel left the visual tree");
        Assert.AreEqual(
            1,
            hiddenView.DisposeCount,
            "and the frozen D8-D release policy gave its view back, exactly as a close does");
        var activeBeforeClose = Require<DockTabGroupNode>(
            dock.Tree.FindGroup(BottomGroup)).ActivePanel;
        Assert.AreNotEqual(
            DockPanelIds.Scene,
            activeBeforeClose,
            "hiding the active tab passed it to a visible neighbour");
        var closedView = Require<RecordingView>(dock.FindPanel(DockPanelIds.Console));
        ClickMenu(RequireTab(dock, BottomGroup, DockPanelIds.Console).ContextMenu, "Close Panel");
        var closed = dock.Workspace.Digest;
        Assert.AreEqual(1, closedView.DisposeCount, "the closed view was released");
        Assert.IsNull(dock.FindPanel(DockPanelIds.Console));
        var callsBeforeRestore = views.Calls.Count;

        var undoneClose = dock.UndoLayout();

        Assert.IsTrue(undoneClose.Success, undoneClose.Diagnostic);
        Assert.AreEqual(hidden, dock.Workspace.Digest, "undo returned the pre-close layout");
        group = Require<DockTabGroupNode>(dock.Tree.FindGroup(BottomGroup));
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Inspector, DockPanelIds.Scene, DockPanelIds.Console },
            group.Panels.ToArray(),
            "identity and order came back in their recorded slots");
        CollectionAssert.AreEqual(
            new[] { DockPanelIds.Scene },
            group.HiddenPanels.ToArray(),
            "the other panel stayed hidden exactly as recorded");
        Assert.AreEqual(activeBeforeClose, group.ActivePanel, "and the active tab matches");
        Assert.IsNull(dock.FindPanel(DockPanelIds.Scene), "a recorded hidden panel stays unmounted");
        var restoredView = dock.FindPanel(DockPanelIds.Console);
        Assert.IsNotNull(restoredView, "the closed panel is mounted again");
        Assert.AreNotSame(closedView, restoredView, "a released view is never reused");
        Assert.AreEqual(0, Require<RecordingView>(restoredView).DisposeCount);
        Assert.AreEqual(1, closedView.DisposeCount, "and it is not released twice");
        Assert.AreEqual(
            callsBeforeRestore + 1,
            views.Calls.Count,
            "the registry created exactly one fresh view for it");
        Assert.AreEqual(DockPanelIds.Console, views.Calls[^1]);

        Assert.IsTrue(dock.UndoLayout().Success);

        var restoredScene = Require<RecordingView>(dock.FindPanel(DockPanelIds.Scene));

        Assert.AreEqual(moved, dock.Workspace.Digest, "the next undo un-does the hide");
        Assert.AreNotSame(
            hiddenView,
            restoredScene,
            "and the panel comes back as a fresh view: the replay restores the layout, never the content");
        Assert.AreEqual(0, restoredScene.DisposeCount);
        Assert.AreEqual(1, hiddenView.DisposeCount, "the hidden view is not released twice");
        Assert.AreEqual(callsBeforeRestore + 2, views.Calls.Count);
        Assert.AreEqual(DockPanelIds.Scene, views.Calls[^1]);

        Assert.IsTrue(dock.UndoLayout().Success);
        Assert.AreEqual(baseline, dock.Workspace.Digest, "and the third undo returns the drop");

        Assert.IsTrue(dock.RedoLayout().Success);
        Assert.AreEqual(moved, dock.Workspace.Digest);
        Assert.IsTrue(dock.RedoLayout().Success);
        Assert.AreEqual(hidden, dock.Workspace.Digest);
        Assert.IsTrue(dock.RedoLayout().Success);
        Assert.AreEqual(closed, dock.Workspace.Digest, "the whole chain replays forward");
        Assert.IsNull(dock.FindPanel(DockPanelIds.Console), "the console is closed again");
        Assert.IsFalse(dock.CanRedoLayout, "the forward walk spent the branch");
    }

    // LM-05: a projection failure the host absorbs by rebuilding advances two host
    // generations for one commit, and a rapid mixed burst records exactly one
    // entry per successful commit. A mid-history commit drops the redo branch.
    [TestMethod]
    public void Rebuilt_projections_and_rapid_bursts_record_exactly_one_entry_each()
    {
        var interceptor = new TestInterceptor();
        using var dock = CreateDock(interceptor, out var baseline);
        var stateChanges = 0;
        dock.StateChanged += (_, _) => stateChanges++;

        var grid = LayoutMain(dock);
        var splitter = SplitterIn(grid);
        var generation = dock.Current!.Generation;
        interceptor.FailNode = CenterSplit;
        interceptor.FailuresRemaining = 1;
        RaiseThumb(splitter, Thumb.DragStartedEvent, new Vector(0, 0));
        RaiseThumb(splitter, Thumb.DragDeltaEvent, new Vector(100, 0));
        RaiseThumb(splitter, Thumb.DragCompletedEvent, new Vector(0, 0));

        Assert.AreEqual(
            DockApplyStatus.Applied,
            dock.Current!.Status,
            "the host rebuilt itself out of the injected failure");
        Assert.AreEqual(
            generation + 2,
            dock.Current.Generation,
            "the commit and the rebuild advanced two host generations");
        Assert.AreEqual(1, stateChanges, "for one state notification");
        var rebuilt = dock.Workspace.Digest;
        Assert.AreEqual(
            1,
            WalkLayoutHistory(dock, rebuilt, baseline),
            "and exactly one layout entry");

        Assert.IsTrue(dock.HidePanel(DockPanelIds.Console).Success);
        Assert.IsTrue(dock.ShowPanel(DockPanelIds.Console).Success);
        Assert.IsTrue(dock.ResizeSplit(MainSplit, 1, 3).Success);

        var pointer = NewPointer();
        LayoutMain(dock);
        Press(dock.Root, new Point(930, 615), pointer);
        Move(dock.Root, new Point(300, 615), pointer);
        Release(dock.Root, new Point(300, 615), pointer);

        Assert.IsTrue(dock.RemovePanel(DockPanelIds.Inspector).Success);
        var newest = dock.Workspace.Digest;

        Assert.AreEqual(
            6,
            WalkLayoutHistory(dock, newest, baseline),
            "six successful commits are six entries, whatever the notification count");

        for (var step = 0; step < 3; step++)
            Assert.IsTrue(dock.UndoLayout().Success, $"undo step {step}");
        Assert.IsTrue(dock.CanRedoLayout, "three undone entries wait for a redo");

        Assert.IsTrue(dock.ResizeSplit(CenterSplit, 1, 2).Success);
        var truncated = dock.Workspace.Digest;

        Assert.IsFalse(dock.CanRedoLayout, "the new commit truncated the redo branch");
        var dropped = dock.RedoLayout();

        Assert.IsFalse(dropped.Success, "the dropped branch is not replayable");
        Assert.AreEqual(truncated, dock.Workspace.Digest, "a refused redo moves nothing");

        Assert.AreEqual(
            4,
            WalkLayoutHistory(dock, truncated, baseline),
            "the history is the three reached states plus the new commit");
    }

    // LM-06: a replay that has to install a floating root prepares that host
    // before adopting the model. When the prepare refuses, the redo stops short of
    // the commit — model, cursor, revision and floating roots all stay put, the
    // orphan host is disposed and no host is left degraded.
    [TestMethod]
    public void A_float_host_that_cannot_be_prepared_stops_the_replay_before_the_commit()
    {
        var faults = new FloatFaults();
        using var dock = CreateFloatingDock(faults, out var baseline);

        Assert.IsTrue(
            dock.MovePanel(DockPanelIds.Scene, new DockTarget.Float(FloatBounds)).Success);
        var floated = dock.Workspace.Digest;
        Assert.AreEqual(1, dock.FloatCount, "the panel really floated");
        Assert.IsTrue(dock.CanUndoLayout, "one float is one entry");
        Assert.IsFalse(dock.CanRedoLayout);

        var undone = dock.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.AreEqual(baseline, dock.Workspace.Digest, "the undo closed the floating root");
        Assert.AreEqual(0, dock.FloatCount);
        Assert.IsTrue(dock.CanRedoLayout, "the undone float waits for a redo");
        var createdBeforeReplay = faults.Created;
        var disposedBeforeReplay = faults.Disposed;
        var revision = dock.Revision;

        faults.PrepareFails = true;
        var rejected = dock.RedoLayout();

        Assert.IsFalse(rejected.Success, "the replayed floating root could not be prepared");
        StringAssert.Contains(rejected.Diagnostic, "injected float prepare failure");
        Assert.AreEqual(baseline, dock.Workspace.Digest, "the model was never adopted");
        Assert.AreEqual(revision, dock.Revision, "and no revision was spent");
        Assert.AreEqual(0, dock.FloatCount, "the floating root never came back");
        Assert.AreEqual(
            createdBeforeReplay + 1,
            faults.Created,
            "the replay did build the host its target needs");
        Assert.AreEqual(
            disposedBeforeReplay + 1,
            faults.Disposed,
            "and released the orphan when the prepare refused");
        Assert.IsTrue(dock.CanRedoLayout, "the cursor did not move");
        Assert.IsFalse(dock.CanUndoLayout, "and the refused replay appended nothing");
        Assert.IsFalse(dock.HasDegradedHosts, "a prepare failure is not a degraded host");

        faults.PrepareFails = false;
        var redone = dock.RedoLayout();

        Assert.IsTrue(redone.Success, redone.Diagnostic);
        Assert.AreEqual(floated, dock.Workspace.Digest, "the same target now replays");
        Assert.AreEqual(1, dock.FloatCount, "on a freshly created host");
        Assert.AreEqual(createdBeforeReplay + 2, faults.Created);
        Assert.IsFalse(dock.CanRedoLayout, "the refused attempt left the cursor where it belongs");
        Assert.IsTrue(dock.CanUndoLayout);
        Assert.AreEqual(
            1,
            WalkLayoutHistory(dock, floated, baseline),
            "the refused replay added no entry to the float's one step");
    }

    // LM-07: "model committed, host failed" moves the cursor with the commit and
    // parks replay until Retry. D9-A-10's degraded gate refuses every further
    // layout change before the model moves; neither the failed replay, the
    // refused change nor the retry appends an entry, and the released cursor is
    // the replay's target.
    [TestMethod]
    public void A_committed_replay_that_fails_the_host_parks_replay_without_recording()
    {
        var interceptor = new TestInterceptor();
        using var dock = CreateDock(interceptor, out var baseline);

        Assert.IsTrue(dock.HidePanel(DockPanelIds.Console).Success);
        var hidden = dock.Workspace.Digest;
        Assert.IsTrue(dock.UndoLayout().Success);
        var revisionBeforeRedo = dock.Revision;
        Assert.IsTrue(dock.CanRedoLayout, "one entry waits for the redo");

        interceptor.AlwaysFail = true;
        var failedRedo = dock.RedoLayout();

        Assert.IsTrue(failedRedo.Success, failedRedo.Diagnostic);
        Assert.AreEqual(hidden, dock.Workspace.Digest, "the replayed model is authoritative");
        Assert.AreNotEqual(revisionBeforeRedo, dock.Revision, "and it really committed");
        Assert.IsTrue(dock.IsDegraded, "while the host could not render it");
        Assert.IsFalse(dock.CanUndoLayout, "the degraded host parks undo");
        Assert.IsFalse(dock.CanRedoLayout, "and redo");
        var degradedRevision = dock.Revision;

        var blocked = dock.UndoLayout();

        Assert.IsFalse(blocked.Success, "no replay may commit onto a broken host");
        Assert.AreEqual(hidden, dock.Workspace.Digest, "the parked replay left the model alone");
        Assert.AreEqual(degradedRevision, dock.Revision, "and the revision alone");

        var refused = dock.ResizeSplit(MainSplit, 1, 3);

        Assert.IsFalse(refused.Success, "a direct change is refused while degraded");
        Assert.AreEqual(hidden, dock.Workspace.Digest, "the refusal left the model alone");
        Assert.AreEqual(degradedRevision, dock.Revision, "and the revision alone");
        Assert.IsTrue(dock.IsDegraded, "the host still needs its retry");

        interceptor.AlwaysFail = false;
        var retried = dock.Retry();

        Assert.AreEqual(DockApplyStatus.Applied, retried.Status);
        Assert.IsFalse(dock.IsDegraded);
        Assert.IsTrue(dock.CanUndoLayout, "retry releases the parked cursor");
        Assert.IsFalse(dock.CanRedoLayout, "the failed replay left nothing to redo");
        Assert.AreEqual(hidden, dock.Workspace.Digest, "retry re-projected, it did not replay");

        // Only the hide ever reached the recorder: the undo, the failed redo, the
        // blocked replay, the refused degraded change and the retry are not entries.
        Assert.AreEqual(1, WalkLayoutHistory(dock, hidden, baseline));
        Assert.AreEqual(baseline, UndoOnceDigest(dock), "the first undo returns the baseline");
    }

    private static string UndoOnceDigest(EditorDockWorkspace dock)
    {
        var newest = dock.Workspace.Digest;
        Assert.IsTrue(dock.UndoLayout().Success);
        var previous = dock.Workspace.Digest;
        Assert.IsTrue(dock.RedoLayout().Success);
        Assert.AreEqual(newest, dock.Workspace.Digest, "the probe returned to the cursor");
        return previous;
    }

    /// <summary>
    /// Replays the whole history down to the session baseline and back up, and
    /// returns how many entries it took. The history exposes no counter on
    /// purpose, so the count is proven by replaying it.
    /// </summary>
    private static int WalkLayoutHistory(
        EditorDockWorkspace dock,
        string newest,
        string baseline)
    {
        var steps = 0;
        while (dock.CanUndoLayout)
        {
            var undone = dock.UndoLayout();
            Assert.IsTrue(undone.Success, undone.Diagnostic);
            steps++;
            Assert.IsTrue(steps <= MaxLayoutSteps, "the undo walk never terminated");
        }

        Assert.AreEqual(baseline, dock.Workspace.Digest, "the undo walk ends on the baseline");

        while (dock.CanRedoLayout)
        {
            var redone = dock.RedoLayout();
            Assert.IsTrue(redone.Success, redone.Diagnostic);
        }

        Assert.AreEqual(newest, dock.Workspace.Digest, "the redo walk returns to the newest layout");
        return steps;
    }

    private EditorDockWorkspace CreateDock(out string baseline)
        => CreateDock(null, out baseline, out _);

    private EditorDockWorkspace CreateDock(out string baseline, out RecordingFactory views)
        => CreateDock(null, out baseline, out views);

    private EditorDockWorkspace CreateDock(
        IDockNodeApplyInterceptor? interceptor,
        out string baseline)
        => CreateDock(interceptor, out baseline, out _);

    private EditorDockWorkspace CreateDock(
        IDockNodeApplyInterceptor? interceptor,
        out string baseline,
        out RecordingFactory views)
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        views = factory;
        var dock = new EditorDockWorkspace(viewModel, interceptor, factory);
        LayoutMain(dock);
        baseline = dock.Workspace.Digest;
        return dock;
    }

    private EditorDockWorkspace CreateFloatingDock(FloatFaults floatFaults, out string baseline)
        => CreateFloatingDock(floatFaults, out baseline, out _);

    private EditorDockWorkspace CreateFloatingDock(
        FloatFaults floatFaults,
        out string baseline,
        out RecordingFactory views)
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var factory = new RecordingFactory(new EditorDockPanelFactory(viewModel));
        views = factory;
        var dock = new EditorDockWorkspace(
            viewModel,
            null,
            factory,
            descriptor => new FaultyFloatHost(descriptor, floatFaults));
        LayoutMain(dock);
        baseline = dock.Workspace.Digest;
        return dock;
    }

    private Grid LayoutMain(EditorDockWorkspace dock)
    {
        var grid = Require<Grid>(dock.FindNode(MainSplit));
        Layout(grid);
        return grid;
    }

    private static void Layout(Control control, double width = 1000, double height = 800)
    {
        control.Measure(new Size(width, height));
        control.Arrange(new Rect(0, 0, width, height));
        Assert.AreEqual(width, control.Bounds.Width, 0.001);
        Assert.AreEqual(height, control.Bounds.Height, 0.001);
    }

    private static DockSplitterThumb SplitterIn(Grid grid)
        => grid.Children.OfType<DockSplitterThumb>().Single();

    private static void RaiseThumb(Thumb thumb, RoutedEvent routedEvent, Vector vector)
        => thumb.RaiseEvent(new VectorEventArgs
        {
            RoutedEvent = routedEvent,
            Vector = vector,
        });

    private static void LoseCapture(DockSplitterThumb splitter)
    {
        // Thumb only raises DragCompleted from capture loss while it holds a
        // pointer origin; seed that state to exercise the real framework path.
        var dragState = typeof(Thumb).GetField(
            "_lastPoint",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(dragState);
        dragState.SetValue(splitter, (Point?)new Point(0, 0));

        var args = new PointerCaptureLostEventArgs(splitter, pointer: null!);
        var method = typeof(DockSplitterThumb).GetMethod(
            "OnPointerCaptureLost",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(method);
        method.Invoke(splitter, new object[] { args });
    }

    private static TabItem RequireTab(
        EditorDockWorkspace dock,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var tabs = Require<TabControl>(dock.FindNode(groupId));
        var item = tabs.Items.OfType<TabItem>().Single(
            candidate => candidate.Tag is PanelInstanceId tag && tag == panel);
        Assert.IsNotNull(item.ContextMenu);
        return item;
    }

    private static void ClickMenu(ContextMenu? menu, string header)
    {
        Assert.IsNotNull(menu);
        var item = menu.Items.OfType<MenuItem>().Single(
            candidate => (string)candidate.Header! == header);
        item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));
    }

    private static Avalonia.Input.Pointer NewPointer()
        => new(1, PointerType.Mouse, isPrimary: true);

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
        public DockNodeId? FailNode { get; set; }
        public int FailuresRemaining { get; set; }
        public bool AlwaysFail { get; set; }

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            if (!AlwaysFail && (FailNode != node || FailuresRemaining <= 0))
                return;

            if (!AlwaysFail)
                FailuresRemaining--;
            throw new InvalidOperationException("injected visual failure");
        }
    }

    /// <summary>
    /// Fault flags shared by every floating host the workspace builds, because a
    /// replay creates a fresh host for the same floating root each time.
    /// </summary>
    private sealed class FloatFaults
    {
        public bool PrepareFails { get; set; }
        public int Created { get; set; }
        public int Disposed { get; set; }
    }

    /// <summary>
    /// A floating host injected through the production <see cref="DockRootHostFactory"/>
    /// seam, so a floating root can be installed, replayed onto and refused without
    /// an OS window.
    /// </summary>
    private sealed class FaultyFloatHost : IDockRootHost
    {
        private readonly DockRootHostDescriptor _descriptor;
        private readonly FloatFaults _faults;
        private long _generation;
        private string? _digest;

        public FaultyFloatHost(DockRootHostDescriptor descriptor, FloatFaults faults)
        {
            _descriptor = descriptor;
            _faults = faults;
            faults.Created++;
        }

        public DockRootHostId Id => _descriptor.Id;

        public DockRootKind Kind => _descriptor.Kind;

        public bool IsVisible { get; private set; }

        public bool IsHealthy { get; private set; } = true;

        public DockApplyResult? Current { get; private set; }

        public DockHostResult Prepare(DockTree root)
            => _faults.PrepareFails
                ? DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    "injected float prepare failure")
                : DockHostResult.Ok();

        public DockHostResult Show()
        {
            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Apply(DockTree root)
        {
            _generation++;
            _digest = root.Digest;
            IsHealthy = true;
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
            IsHealthy = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Close()
        {
            IsVisible = false;
            return DockHostResult.Ok();
        }

        public bool TryGetCurrentToken(out DockApplyToken token)
        {
            if (IsHealthy && _digest is not null)
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
            IsVisible = false;
            _faults.Disposed++;
        }
    }
}
