using Avalonia;
using Avalonia.Input;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

public enum DockDragEventKind
{
    Begin,
    Move,
    Release,
    Cancel,
}

/// <summary>
/// One visual drag notification from a source host to its owner. Drag events
/// never carry model authority: the coordinator revalidates host tokens and
/// the workspace location before it commits anything.
/// </summary>
public sealed class DockDragEventArgs : EventArgs
{
    private DockDragEventArgs(
        DockDragEventKind kind,
        PanelInstanceId panel,
        DockNodeId groupId,
        DockApplyToken token,
        Point localPoint,
        bool localTargetResolved,
        PixelPoint screenPoint,
        bool hasScreenPoint,
        bool insideSource,
        KeyModifiers modifiers,
        double renderScaling,
        DockScreenArea? screen)
    {
        Kind = kind;
        Panel = panel;
        GroupId = groupId;
        Token = token;
        LocalPoint = localPoint;
        LocalTargetResolved = localTargetResolved;
        ScreenPoint = screenPoint;
        HasScreenPoint = hasScreenPoint;
        InsideSource = insideSource;
        Modifiers = modifiers;
        RenderScaling = renderScaling;
        Screen = screen;
    }

    public DockDragEventKind Kind { get; }

    public PanelInstanceId Panel { get; }

    public DockNodeId GroupId { get; }

    /// <summary>Valid for <see cref="DockDragEventKind.Begin"/>.</summary>
    public DockApplyToken Token { get; }

    public Point LocalPoint { get; }

    public bool LocalTargetResolved { get; }

    public PixelPoint ScreenPoint { get; }

    public bool HasScreenPoint { get; }

    public bool InsideSource { get; }

    public KeyModifiers Modifiers { get; }

    public double RenderScaling { get; }

    /// <summary>
    /// Monitor under the pointer at release, in physical pixels. Null when the
    /// platform cannot name one; consumers must then fall back deterministically
    /// and must not treat one window's scaling as a global factor (ADR-011 O-3).
    /// </summary>
    public DockScreenArea? Screen { get; }

    public static DockDragEventArgs Begin(
        PanelInstanceId panel,
        DockNodeId groupId,
        DockApplyToken token)
        => new(
            DockDragEventKind.Begin,
            panel,
            groupId,
            token,
            default,
            localTargetResolved: false,
            default,
            hasScreenPoint: false,
            insideSource: true,
            KeyModifiers.None,
            renderScaling: 1d,
            screen: null);

    public static DockDragEventArgs Move(
        Point localPoint,
        bool localTargetResolved,
        PixelPoint screenPoint,
        bool hasScreenPoint,
        bool insideSource)
        => new(
            DockDragEventKind.Move,
            default,
            default,
            default,
            localPoint,
            localTargetResolved,
            screenPoint,
            hasScreenPoint,
            insideSource,
            KeyModifiers.None,
            renderScaling: 1d,
            screen: null);

    public static DockDragEventArgs Release(
        Point localPoint,
        bool localTargetResolved,
        PixelPoint screenPoint,
        bool hasScreenPoint,
        bool insideSource,
        KeyModifiers modifiers,
        double renderScaling,
        DockScreenArea? screen = null)
        => new(
            DockDragEventKind.Release,
            default,
            default,
            default,
            localPoint,
            localTargetResolved,
            screenPoint,
            hasScreenPoint,
            insideSource,
            modifiers,
            renderScaling,
            screen);

    public static DockDragEventArgs Cancel()
        => new(
            DockDragEventKind.Cancel,
            default,
            default,
            default,
            default,
            localTargetResolved: false,
            default,
            hasScreenPoint: false,
            insideSource: true,
            KeyModifiers.None,
            renderScaling: 1d,
            screen: null);
}

/// <summary>A host that can originate a local tab drag.</summary>
public interface IDockDragSourceHost
{
    DockRootHostId Id { get; }

    event EventHandler<DockDragEventArgs>? DragEvent;
}

/// <summary>
/// Projection callbacks raised by a host. The coordinator handles them so
/// every host, including floating windows, shares one token-validated path.
/// </summary>
public interface IDockRootHostCallbacks
{
    event EventHandler<DockPanelActivatedEventArgs>? PanelActivated;

    event EventHandler<DockSplitResizeRequestedEventArgs>? SplitResizeRequested;

    event EventHandler<DockPanelMoveRequestedEventArgs>? PanelMoveRequested;

    event EventHandler<DockPanelActionRequestedEventArgs>? PanelActionRequested;
}
