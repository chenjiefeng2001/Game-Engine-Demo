using System.Reflection;
using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D9-A-10 settlement gate (F1-01). The first show of a floating window fails,
/// so the committed history entry holds the requested rectangle P while the
/// window has no place yet. The retry places the window on S, and the current
/// entry must follow the window — not the request — before replay is released.
/// </summary>
[TestClass]
public sealed class DockSettlementCalibrationTests
{
    private const int MaxLayoutSteps = 24;

    private static readonly DockNodeId MainSplit = new("dock.main");
    private static readonly DockNodeId LeftGroup = new("dock.tabs.left");
    private static readonly DockNodeId BottomGroup = new("dock.tabs.bottom");
    private static readonly DockBounds Requested = new(2400d, 1400d, 640d, 480d);
    private static readonly DockBounds Settled = new(1920d, 1072d, 640d, 480d);

    private static readonly DockScreenArea Primary = new(
        "primary",
        new DockPixelRect(0, 0, 2560, 1600),
        new DockPixelRect(0, 0, 2560, 1552),
        1d,
        Primary: true);

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-f1-settlement", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    [TestMethod]
    public void F1_01_a_failed_first_show_settles_the_committed_entry_on_retry()
    {
        var windows = new List<FailOnceWindow>();
        EditorDockWorkspace? created = null;
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        created = new EditorDockWorkspace(
            viewModel,
            interceptor: null,
            panelFactory: new EditorDockPanelFactory(viewModel),
            floatHostFactory: descriptor =>
            {
                var window = new FailOnceWindow { FailNextShow = windows.Count == 0 };
                window.ScreenList.Add(Primary);
                windows.Add(window);
                return new DockWindowRootHost(descriptor, RegistryOf(created!), window);
            });
        var dock = created;

        try
        {
            var baseline = dock.Workspace.Digest;
            Assert.AreEqual(0L, dock.Revision, "a fresh workspace starts at revision zero");

            // (1) The float commit is written, then its first show fails: the
            // model keeps the requested rectangle P and the workspace degrades.
            var floated = dock.MovePanel(DockPanelIds.Scene, new DockTarget.Float(Requested));

            Assert.IsTrue(floated.Success, floated.Diagnostic);
            Assert.IsTrue(dock.HasDegradedHosts, "the failed show degrades the floating host");
            Assert.AreEqual(1, dock.Workspace.FloatingRoots.Count);
            Assert.AreEqual(
                Requested,
                dock.Workspace.FloatingRoots[0].Bounds,
                "the committed model holds the requested rectangle, not a settled one");
            Assert.AreEqual(1L, dock.Revision, "the commit advanced the revision exactly once");
            Assert.AreEqual(1, windows.Count);
            Assert.AreEqual(1, windows[0].ShowCount);
            Assert.IsFalse(windows[0].IsVisible, "the first show never opened the window");
            Assert.IsFalse(dock.Capture().CanPersist, "a degraded workspace cannot be persisted");
            Assert.IsFalse(dock.CanUndoLayout, "degradation parks the recorded entry");
            Assert.IsFalse(dock.CanRedoLayout, "the failed commit left nothing to redo");

            // (2) Every layout entry is refused before the model moves.
            var degradedDigest = dock.Workspace.Digest;
            var degradedRevision = dock.Revision;
            var refused = new[]
            {
                dock.ActivateTab(BottomGroup, DockPanelIds.Console),
                dock.ResizeSplit(MainSplit, 0.4d, 0.6d),
                dock.HidePanel(DockPanelIds.Console),
                dock.RemovePanel(DockPanelIds.Console),
                dock.MovePanel(DockPanelIds.Console, new DockTarget.Center(LeftGroup)),
                dock.MovePanel(DockPanelIds.Inspector, new DockTarget.Float(Requested)),
                dock.UndoLayout(),
                dock.RedoLayout(),
            };

            foreach (var attempt in refused)
            {
                Assert.IsFalse(attempt.Success, "a degraded workspace refuses every layout entry");
                StringAssert.Contains(attempt.Diagnostic, "degraded");
            }

            Assert.AreEqual(degradedDigest, dock.Workspace.Digest, "no refusal changed the model");
            Assert.AreEqual(degradedRevision, dock.Revision, "no refusal advanced the revision");
            Assert.IsTrue(dock.HasDegradedHosts, "the host still needs its retry");
            Assert.IsFalse(dock.Capture().CanPersist, "persistence stays blocked");
            Assert.IsFalse(dock.CanUndoLayout, "and replay stays parked");
            Assert.IsFalse(dock.CanRedoLayout);

            // (3) Retry shows the window on S and calibrates the current entry:
            // no new commit, no new entry, no cursor move.
            var retried = dock.Retry();

            Assert.AreEqual(DockApplyStatus.Applied, retried.Status, retried.Diagnostic);
            Assert.IsFalse(dock.HasDegradedHosts, "the retry healed the floating host");
            Assert.AreEqual(1L, dock.Revision, "settlement is not a layout commit");
            var settledRoot = dock.Workspace.FloatingRoots.Single();
            Assert.AreEqual(Settled, settledRoot.Bounds, "the effective geometry is in the model");
            Assert.AreEqual(Settled, settledRoot.RestoreBounds);
            Assert.IsTrue(windows[0].IsVisible, "the retried window is open");
            Assert.AreEqual(2, windows[0].ShowCount);
            Assert.AreEqual(Settled, windows[0].Bounds);
            Assert.IsTrue(dock.CanUndoLayout, "the single commit is still undoable");
            Assert.IsFalse(dock.CanRedoLayout, "calibration did not create a redo branch");
            var settledDigest = dock.Workspace.Digest;
            Assert.AreNotEqual(
                degradedDigest,
                settledDigest,
                "the settled geometry changed the persistable content");

            // (4) Persistence now records S, not the pre-settlement P.
            var captured = dock.Capture();
            Assert.IsTrue(captured.CanPersist, captured.Reason);
            Assert.AreEqual(1L, captured.Revision);
            Assert.IsNotNull(captured.Snapshot);
            Assert.AreEqual(1, captured.Snapshot!.Floats.Length);
            AssertSnapshotBounds(Settled, captured.Snapshot.Floats[0].Bounds);
            Assert.IsFalse(captured.Snapshot.Floats[0].Maximized);

            // (5) Undo returns to the baseline and is an ordinary new commit.
            var undone = dock.UndoLayout();

            Assert.IsTrue(undone.Success, undone.Diagnostic);
            Assert.AreEqual(baseline, dock.Workspace.Digest, "undo restores the baseline layout");
            Assert.AreEqual(2L, dock.Revision, "the replay is a new commit");
            Assert.AreEqual(0, dock.Workspace.FloatingRoots.Count);
            Assert.IsFalse(windows[0].IsVisible, "the replay closed the floating window");
            Assert.IsTrue(dock.CanRedoLayout);
            Assert.IsFalse(dock.CanUndoLayout);

            // (6) Redo replays the calibrated entry — S, not P — and succeeds.
            var redone = dock.RedoLayout();

            Assert.IsTrue(redone.Success, redone.Diagnostic);
            Assert.AreEqual(
                settledDigest,
                dock.Workspace.Digest,
                "redo replays the settled entry, not the pre-settlement request");
            Assert.AreEqual(3L, dock.Revision);
            Assert.AreEqual(Settled, dock.Workspace.FloatingRoots.Single().Bounds);
            Assert.AreEqual(2, windows.Count, "the redo created a fresh floating host");
            Assert.IsTrue(windows[1].IsVisible);
            Assert.AreEqual(
                Settled,
                windows[1].Bounds,
                "the replayed window opens on the settled rectangle");

            // (7) Model, window, history and snapshot agree; the calibrated
            // commit is still exactly one history entry.
            var afterReplay = dock.Capture();
            Assert.IsTrue(afterReplay.CanPersist, afterReplay.Reason);
            Assert.IsNotNull(afterReplay.Snapshot);
            AssertSnapshotBounds(Settled, afterReplay.Snapshot!.Floats[0].Bounds);
            Assert.IsTrue(dock.CanUndoLayout);
            Assert.IsFalse(dock.CanRedoLayout);

            Assert.AreEqual(
                1,
                WalkLayoutHistory(dock, settledDigest, baseline),
                "the float commit and its calibration are one entry");

            var final = dock.Capture();
            Assert.IsTrue(final.CanPersist, final.Reason);
            Assert.AreEqual(settledDigest, dock.Workspace.Digest);
            AssertSnapshotBounds(Settled, final.Snapshot!.Floats[0].Bounds);
            Assert.AreEqual(Settled, dock.Workspace.FloatingRoots.Single().Bounds);
            Assert.AreEqual(Settled, windows[^1].Bounds, "the window agrees with the model");
            Assert.IsTrue(windows[^1].IsVisible);
        }
        finally
        {
            dock.Dispose();
        }
    }

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

    private static void AssertSnapshotBounds(DockBounds expected, DockBoundsSnapshot actual)
    {
        Assert.AreEqual(expected.X, actual.X, "snapshot X");
        Assert.AreEqual(expected.Y, actual.Y, "snapshot Y");
        Assert.AreEqual(expected.Width, actual.Width, "snapshot width");
        Assert.AreEqual(expected.Height, actual.Height, "snapshot height");
    }

    /// <summary>
    /// The workspace owns its view registry privately; a real window host needs
    /// the same registry so floated panels keep their mounted views. The factory
    /// runs after construction, so the field is always initialized here.
    /// </summary>
    private static DockPanelViewRegistry RegistryOf(EditorDockWorkspace dock)
    {
        var field = typeof(EditorDockWorkspace).GetField(
            "_registry",
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(field);
        return (DockPanelViewRegistry)field.GetValue(dock)!;
    }

    private sealed class FailOnceWindow : IDockFloatWindow
    {
        public Control? Content { get; set; }

        public string Title { get; set; } = string.Empty;

        public bool FailNextShow { get; set; }

        public int ShowCount { get; private set; }

        public bool IsVisible { get; private set; }

        public bool IsMaximized { get; private set; }

        public DockBounds Bounds { get; private set; }

        public List<DockScreenArea> ScreenList { get; } = [];

        public IReadOnlyList<DockScreenArea> Screens => ScreenList;

        public DockWindowFrame FrameMargins { get; set; } = DockWindowFrame.None;

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
            ShowCount++;
            if (FailNextShow)
            {
                FailNextShow = false;
                throw new InvalidOperationException("injected first-show failure");
            }

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
