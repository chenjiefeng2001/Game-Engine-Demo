using Avalonia.Controls;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

public readonly record struct DockApplyToken(
    DockRootHostId HostId,
    long Generation,
    string Digest)
{
    public DockApplyToken(long generation, string digest)
        : this(DockRootHostId.Main, generation, digest)
    {
    }
}

/// <summary>
/// Raised only for a user-driven tab selection. The token identifies the
/// visual build that produced the view; callers must reject stale tokens.
/// A handler sets <see cref="Accepted"/> only after the activation is
/// committed; otherwise the visual selection returns to the model state.
/// </summary>
public sealed class DockPanelActivatedEventArgs : EventArgs
{
    public DockPanelActivatedEventArgs(
        DockNodeId groupId,
        PanelInstanceId panel,
        DockApplyToken token)
    {
        GroupId = groupId;
        Panel = panel;
        Token = token;
    }

    public DockNodeId GroupId { get; }
    public PanelInstanceId Panel { get; }
    public DockApplyToken Token { get; }
    public bool Accepted { get; set; }
}

/// <summary>
/// Raised once when a splitter drag ends. The token identifies the visual
/// build that owns the splitter; callers must reject stale tokens or
/// degraded visuals. A handler sets <see cref="Accepted"/> only after the
/// resize is committed; otherwise the visual preview is rolled back.
/// </summary>
public sealed class DockSplitResizeRequestedEventArgs : EventArgs
{
    public DockSplitResizeRequestedEventArgs(
        DockNodeId splitId,
        double firstWeight,
        double secondWeight,
        DockApplyToken token)
    {
        SplitId = splitId;
        FirstWeight = firstWeight;
        SecondWeight = secondWeight;
        Token = token;
    }

    public DockNodeId SplitId { get; }
    public double FirstWeight { get; }
    public double SecondWeight { get; }
    public DockApplyToken Token { get; }
    public bool Accepted { get; set; }
}

/// <summary>
/// Raised once when a tab drag ends over a valid drop target. The token
/// identifies the projection the drag started from; callers must reject stale
/// tokens. A handler sets <see cref="Accepted"/> only after the move is
/// committed; otherwise the preview is discarded and the tree is unchanged.
/// </summary>
public sealed class DockPanelMoveRequestedEventArgs : EventArgs
{
    public DockPanelMoveRequestedEventArgs(
        PanelInstanceId panel,
        DockTarget target,
        DockApplyToken token)
    {
        Panel = panel;
        Target = target ?? throw new ArgumentNullException(nameof(target));
        Token = token;
    }

    public PanelInstanceId Panel { get; }
    public DockTarget Target { get; }
    public DockApplyToken Token { get; }
    public bool Accepted { get; set; }
}

public enum DockPanelAction
{
    Hide,
    Close,
}

/// <summary>
/// Raised by a tab context-menu command. The token identifies the visual
/// build that produced the menu; a stale token or a panel that left the
/// source host is rejected by the coordinator.
/// </summary>
public sealed class DockPanelActionRequestedEventArgs : EventArgs
{
    public DockPanelActionRequestedEventArgs(
        PanelInstanceId panel,
        DockPanelAction action,
        DockApplyToken token)
    {
        Panel = panel;
        Action = action;
        Token = token;
    }

    public PanelInstanceId Panel { get; }
    public DockPanelAction Action { get; }
    public DockApplyToken Token { get; }
    public bool Accepted { get; set; }
}

public enum DockApplyStatus
{
    Unchanged,
    Applied,
    Degraded,
}

public sealed record DockApplyResult(
    DockApplyStatus Status,
    long Generation,
    string CommittedDigest,
    string? AppliedDigest,
    IReadOnlyList<PanelInstanceId> PanelFailures,
    string? Diagnostic)
{
    public bool IsHealthy => Status != DockApplyStatus.Degraded;

    public bool IsCurrent
        => IsHealthy
           && string.Equals(AppliedDigest, CommittedDigest, StringComparison.Ordinal);
}

public interface IDockPanelView : IDisposable
{
    string Title { get; }

    Control Content { get; }
}

public interface IDockPanelViewFactory
{
    /// <summary>
    /// True when this build can create a real view for the panel. Unresolved
    /// panels are opaque identities: they never reach <see cref="Create"/>.
    /// </summary>
    bool CanCreate(PanelInstanceId instance);

    IDockPanelView Create(PanelInstanceId instance);
}

/// <summary>
/// Deterministic visual failure injection point. It is not consulted for
/// layout decisions and must not mutate the model.
/// </summary>
public interface IDockNodeApplyInterceptor
{
    void OnApplying(DockNodeId node, DockApplyToken token);
}
