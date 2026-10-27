namespace Nova.UI.Core.Docking;

/// <summary>
/// Why one commit is reported to <see cref="DockLayoutHistory"/>. The source
/// keeps a replayed undo or redo from being appended as a fresh operation, even
/// when the notification funnel cannot tell the two calls apart.
/// </summary>
public enum DockLayoutOperationSource
{
    /// <summary>A layout change the user made directly.</summary>
    Direct,

    /// <summary>A commit that applied the pending undo target.</summary>
    Undo,

    /// <summary>A commit that applied the pending redo target.</summary>
    Redo,
}

public enum DockLayoutHistoryOutcome
{
    /// <summary>The commit became a new undoable history entry.</summary>
    Recorded,

    /// <summary>The commit replayed a pending target, so only the cursor moved.</summary>
    CursorMoved,

    /// <summary>The commit carries the layout that is already current; history untouched.</summary>
    Unchanged,

    /// <summary>The commit matches nothing the history can accept; history untouched.</summary>
    Rejected,
}

/// <summary>
/// Result of one <see cref="DockLayoutHistory.Record"/> call. A rejected or
/// unchanged result never moves the cursor and never discards the redo branch.
/// </summary>
public readonly record struct DockLayoutHistoryResult(
    DockLayoutHistoryOutcome Outcome,
    string Diagnostic)
{
    public bool Accepted =>
        Outcome is DockLayoutHistoryOutcome.Recorded or DockLayoutHistoryOutcome.CursorMoved;
}

public enum DockLayoutHistoryReconcileOutcome
{
    /// <summary>The current entry was replaced with the settled layout.</summary>
    Reconciled,

    /// <summary>The current entry already carries the settled layout; untouched.</summary>
    Unchanged,

    /// <summary>The stored entry is missing or does not match the expected content; untouched.</summary>
    Rejected,
}

/// <summary>
/// Result of one <see cref="DockLayoutHistory.ReconcileCurrent"/> call. This is
/// settlement calibration, not a commit: a rejected or unchanged result leaves
/// every entry, the cursor and the redo branch exactly where they were.
/// </summary>
public readonly record struct DockLayoutHistoryReconcileResult(
    DockLayoutHistoryReconcileOutcome Outcome,
    string Diagnostic)
{
    public bool Accepted =>
        Outcome is DockLayoutHistoryReconcileOutcome.Reconciled
            or DockLayoutHistoryReconcileOutcome.Unchanged;
}

/// <summary>
/// Bounded, in-process cursor over committed workspace layouts. Only successful
/// model commits are reported here: a failed or unchanged commit adds no entry,
/// an undo or redo commit moves the cursor without appending, and a new
/// operation after an undo discards the redo branch.
/// </summary>
public sealed class DockLayoutHistory
{
    public const int DefaultMaxUndoDepth = 100;

    private readonly List<DockLayoutSnapshot> _entries = [];
    private readonly int _maxUndoDepth;
    private int _cursor = -1;

    /// <param name="maxUndoDepth">
    /// How many committed layout changes stay undoable. The content the first
    /// of them returns to is retained as well, so the entry count can reach
    /// <c>maxUndoDepth + 1</c> and a full budget is never one undo short.
    /// </param>
    public DockLayoutHistory(int maxUndoDepth = DefaultMaxUndoDepth)
    {
        if (maxUndoDepth <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxUndoDepth),
                "The undo depth must be positive.");
        }

        _maxUndoDepth = maxUndoDepth;
    }

    public int MaxUndoDepth => _maxUndoDepth;

    /// <summary>Stored layouts: the current one plus every undo and redo target.</summary>
    public int Count => _entries.Count;

    /// <summary>Committed layout changes that can still be undone.</summary>
    public int UndoableCount => Math.Max(_cursor, 0);

    /// <summary>Undone changes that can still be redone.</summary>
    public int RedoableCount => _cursor < 0 ? 0 : _entries.Count - _cursor - 1;

    public bool CanUndo => UndoableCount > 0;

    public bool CanRedo => RedoableCount > 0;

    public DockLayoutSnapshot? Current => _cursor < 0 ? null : _entries[_cursor];

    /// <summary>Adopts a known layout as the only entry, forgetting every recorded change.</summary>
    public void Reset(DockLayoutSnapshot baseline)
    {
        ArgumentNullException.ThrowIfNull(baseline);

        _entries.Clear();
        _entries.Add(baseline);
        _cursor = 0;
    }

    public bool TryGetUndoTarget(out DockLayoutSnapshot target)
    {
        if (CanUndo)
        {
            target = _entries[_cursor - 1];
            return true;
        }

        target = null!;
        return false;
    }

    public bool TryGetRedoTarget(out DockLayoutSnapshot target)
    {
        if (CanRedo)
        {
            target = _entries[_cursor + 1];
            return true;
        }

        target = null!;
        return false;
    }

    public DockLayoutHistoryResult Record(
        DockLayoutSnapshot committed,
        DockLayoutOperationSource source = DockLayoutOperationSource.Direct)
    {
        ArgumentNullException.ThrowIfNull(committed);

        if (_cursor < 0)
            return SeedFirstCommit(committed, source);

        if (committed.SameContent(_entries[_cursor]))
        {
            return new DockLayoutHistoryResult(
                DockLayoutHistoryOutcome.Unchanged,
                "The commit carries the layout that is already current.");
        }

        return source switch
        {
            DockLayoutOperationSource.Undo => Replay(committed, backwards: true),
            DockLayoutOperationSource.Redo => Replay(committed, backwards: false),
            _ => Append(committed),
        };
    }

    /// <summary>
    /// Replaces the current entry with the settled geometry of the same commit.
    /// The caller must prove the entry still carries the pre-settlement content;
    /// a mismatch is rejected before anything is written.
    /// </summary>
    public DockLayoutHistoryReconcileResult ReconcileCurrent(
        DockLayoutSnapshot expectedCurrent,
        DockLayoutSnapshot settled)
    {
        ArgumentNullException.ThrowIfNull(expectedCurrent);
        ArgumentNullException.ThrowIfNull(settled);

        if (_cursor < 0)
        {
            return new DockLayoutHistoryReconcileResult(
                DockLayoutHistoryReconcileOutcome.Rejected,
                "The layout history has no current entry to calibrate.");
        }

        var current = _entries[_cursor];
        if (!expectedCurrent.SameContent(current))
        {
            return new DockLayoutHistoryReconcileResult(
                DockLayoutHistoryReconcileOutcome.Rejected,
                "The current layout entry no longer matches the expected pre-calibration content.");
        }

        if (settled.SameContent(current))
        {
            return new DockLayoutHistoryReconcileResult(
                DockLayoutHistoryReconcileOutcome.Unchanged,
                "The settled layout already matches the current entry.");
        }

        _entries[_cursor] = settled;
        return new DockLayoutHistoryReconcileResult(
            DockLayoutHistoryReconcileOutcome.Reconciled,
            "The current layout entry was calibrated to the settled geometry.");
    }

    private DockLayoutHistoryResult SeedFirstCommit(
        DockLayoutSnapshot committed,
        DockLayoutOperationSource source)
    {
        if (source != DockLayoutOperationSource.Direct)
        {
            return new DockLayoutHistoryResult(
                DockLayoutHistoryOutcome.Rejected,
                "The layout history has no baseline to replay against.");
        }

        _entries.Add(committed);
        _cursor = 0;
        return new DockLayoutHistoryResult(
            DockLayoutHistoryOutcome.Recorded,
            "The first layout commit became the history baseline.");
    }

    private DockLayoutHistoryResult Replay(DockLayoutSnapshot committed, bool backwards)
    {
        if (!(backwards ? CanUndo : CanRedo))
        {
            return new DockLayoutHistoryResult(
                DockLayoutHistoryOutcome.Rejected,
                backwards
                    ? "There is no layout left to undo."
                    : "There is no layout left to redo.");
        }

        var pending = _entries[backwards ? _cursor - 1 : _cursor + 1];
        if (!committed.SameContent(pending))
        {
            return new DockLayoutHistoryResult(
                DockLayoutHistoryOutcome.Rejected,
                backwards
                    ? "The commit does not match the pending undo target."
                    : "The commit does not match the pending redo target.");
        }

        _cursor += backwards ? -1 : 1;
        return new DockLayoutHistoryResult(
            DockLayoutHistoryOutcome.CursorMoved,
            backwards
                ? "Undone one layout change."
                : "Redone one layout change.");
    }

    private DockLayoutHistoryResult Append(DockLayoutSnapshot committed)
    {
        var dropped = _entries.Count - _cursor - 1;
        if (dropped > 0)
            _entries.RemoveRange(_cursor + 1, dropped);

        _entries.Add(committed);
        _cursor = _entries.Count - 1;

        while (_cursor > _maxUndoDepth)
        {
            _entries.RemoveAt(0);
            _cursor--;
        }

        return new DockLayoutHistoryResult(
            DockLayoutHistoryOutcome.Recorded,
            dropped > 0
                ? $"Recorded one layout change and discarded {dropped} redo target(s)."
                : "Recorded one layout change.");
    }
}
