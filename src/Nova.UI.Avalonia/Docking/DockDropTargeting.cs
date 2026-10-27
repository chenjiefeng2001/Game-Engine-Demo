using Avalonia;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>Visual-only target resolved during a drag. Bounds are host-local.</summary>
public readonly record struct DockDropTargetHit(DockTarget Target, Rect Bounds, int VisualSlot);

/// <summary>
/// A root host that can resolve cross-window drop targets and paint the
/// external drop preview. Implemented by the main projector host and real
/// floating-window hosts.
/// </summary>
public interface IDockDropTargetHost
{
    DockRootHostId Id { get; }

    bool IsVisible { get; }

    bool TryHitTestLocal(
        Point point,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit);

    bool TryHitTestScreen(
        PixelPoint screenPoint,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit);

    void ShowExternalDropPreview(DockDropTargetHit hit);

    void ClearExternalDropPreview();
}

/// <summary>
/// Optional float-host capability: set geometry before Show. Bounds are model
/// geometry — the outer frame origin plus the client size in DIP (ADR-011
/// §十一·二) — so the host can apply them without re-deriving a frame origin.
/// <paramref name="preferredScreen"/> is the monitor under the pointer for a
/// tear-off; null lets placement pick the work area with the largest overlap.
/// </summary>
public interface IDockFloatHostBounds
{
    void SetWindowBounds(
        DockBounds bounds,
        bool maximized,
        DockScreenArea? preferredScreen = null);

    /// <summary>Geometry the window really has; false when it is not visible.</summary>
    bool TryGetWindowBounds(out DockBounds bounds);
}
