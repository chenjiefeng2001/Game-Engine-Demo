using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Services.Layout;

/// <summary>
/// Immutable answer to "may the current dock state be written?". A blocked
/// capture never changes the model or host health.
/// </summary>
public sealed record DockPersistenceCapture(
    bool CanPersist,
    string? Reason,
    long Revision,
    DockWorkspaceSnapshot? Snapshot)
{
    public static DockPersistenceCapture Blocked(string reason, long revision)
        => new(false, reason, revision, null);

    public static DockPersistenceCapture Ready(long revision, DockWorkspaceSnapshot snapshot)
        => new(true, null, revision, snapshot);
}

/// <summary>
/// Bridge from the editor workspace to a live dock coordinator. The editor
/// owns scheduling; the source owns model/host convergence checks.
/// </summary>
public interface IDockPersistenceSource
{
    long Revision { get; }

    event EventHandler? PersistenceStateChanged;

    DockPersistenceCapture Capture();

    void CancelPendingInteraction();
}

/// <summary>
/// Optional view of a live dock source: the panel identities this build cannot
/// resolve right now. The editor re-arms the unresolved-panel protection from
/// this, because an undo can bring an unknown identity back into a workspace
/// that was previously clean.
/// </summary>
public interface IDockUnresolvedPanelSource
{
    IReadOnlyList<PanelInstanceId> UnresolvedPanels { get; }
}

/// <summary>
/// Optional view of a live dock source that keeps an in-process layout history.
/// Layout commands are registered in this layer, so the shell reaches replay
/// here instead of referencing the dock facade.
/// </summary>
public interface IDockLayoutHistorySource
{
    bool CanUndoLayout { get; }

    bool CanRedoLayout { get; }

    DockOperationResult UndoLayout();

    DockOperationResult RedoLayout();
}

/// <summary>
/// What one layout-history replay did. The shell renders this as status text so
/// a rejected replay is visible instead of only logged.
/// </summary>
public sealed class DockLayoutHistoryEventArgs(bool undone, DockOperationResult result) : EventArgs
{
    public bool Undone { get; } = undone;

    public DockOperationResult Result { get; } = result;
}
