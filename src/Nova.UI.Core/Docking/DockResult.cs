namespace Nova.UI.Core.Docking;

public enum DockError
{
    None,
    InvalidSource,
    InvalidTarget,
    DuplicatePanel,
    TargetGone,
    AlreadyInState,
    InvalidModel,
    PanelHidden,
    WouldEmptyGroup,
}

/// <summary>
/// Operation result. A failed operation returns no tree; the caller's current
/// tree is immutable and therefore remains unchanged.
/// </summary>
public readonly record struct DockOperationResult(
    bool Success,
    DockTree? Tree,
    DockError Error,
    string Diagnostic)
{
    public static DockOperationResult Ok(DockTree tree, string diagnostic = "")
        => new(true, tree, DockError.None, diagnostic);

    public static DockOperationResult Fail(DockError error, string diagnostic)
        => new(false, null, error, diagnostic);
}

public abstract record DockTarget
{
    public sealed record Center(DockNodeId GroupId, int? Index = null) : DockTarget;

    public sealed record Edge(DockNodeId GroupId, DockEdge Side) : DockTarget;

    /// <summary>Workspace-level target that creates a new floating root.</summary>
    public sealed record Float(DockBounds Bounds) : DockTarget;

    public sealed record Invalid(string Reason) : DockTarget;
}
