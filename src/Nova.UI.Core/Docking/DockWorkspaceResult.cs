namespace Nova.UI.Core.Docking;

public enum DockWorkspaceError
{
    None,
    InvalidSource,
    InvalidTarget,
    DuplicatePanel,
    TargetGone,
    AlreadyInState,
    InvalidModel,
    WouldEmptyMainRoot,
    LimitExceeded,
    PanelHidden,
    WouldEmptyGroup,
}

/// <summary>
/// Workspace operation result. A failed operation returns no workspace; the
/// receiver is immutable and remains unchanged.
/// </summary>
public readonly record struct DockWorkspaceResult(
    bool Success,
    DockWorkspace? Workspace,
    DockWorkspaceError Error,
    string Diagnostic)
{
    public static DockWorkspaceResult Ok(DockWorkspace workspace, string diagnostic = "")
        => new(true, workspace, DockWorkspaceError.None, diagnostic);

    public static DockWorkspaceResult Fail(DockWorkspaceError error, string diagnostic)
        => new(false, null, error, diagnostic);
}

/// <summary>
/// Where a panel instance currently lives. A null <see cref="FloatRootId"/>
/// means the main root.
/// </summary>
public readonly record struct DockWorkspaceLocation(
    DockFloatRootId? FloatRootId,
    DockNodeId GroupId,
    int Index)
{
    public bool IsMain => FloatRootId is null;
}
