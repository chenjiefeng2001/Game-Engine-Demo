using Avalonia;
using Avalonia.Controls;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Root host backed by a real <see cref="DockWorkspaceProjector"/>. D6-C has no
/// OS window; Prepare validates the candidate, Apply projects it, and Show
/// exposes the host to the coordinator.
/// </summary>
public sealed class DockProjectorRootHost
    : IDockRootHost, IDockDropTargetHost, IDockRootHostCallbacks, IDockDragSourceHost
{
    private readonly DockRootHostDescriptor _descriptor;
    private readonly DockWorkspaceProjector _projector;
    private bool _visible;
    private bool _disposed;

    public DockProjectorRootHost(
        DockRootHostDescriptor descriptor,
        DockPanelViewRegistry registry,
        IDockNodeApplyInterceptor? interceptor = null)
    {
        if (descriptor.Id.IsEmpty)
            throw new ArgumentException("A dock host id must be non-empty.", nameof(descriptor));

        _descriptor = descriptor;
        _projector = new DockWorkspaceProjector(registry, descriptor.Id, interceptor);
        _projector.PanelActivated += (_, e) => PanelActivated?.Invoke(this, e);
        _projector.SplitResizeRequested += (_, e) => SplitResizeRequested?.Invoke(this, e);
        _projector.PanelMoveRequested += (_, e) => PanelMoveRequested?.Invoke(this, e);
        _projector.PanelActionRequested += (_, e) => PanelActionRequested?.Invoke(this, e);
        _projector.DragEvent += (_, e) => _dragEvent?.Invoke(this, e);
    }

    public DockRootHostId Id => _descriptor.Id;

    public DockRootKind Kind => _descriptor.Kind;

    public bool IsVisible => _visible;

    public Control Root => _projector.Root;

    public event EventHandler<DockPanelActivatedEventArgs>? PanelActivated;

    public event EventHandler<DockSplitResizeRequestedEventArgs>? SplitResizeRequested;

    public event EventHandler<DockPanelMoveRequestedEventArgs>? PanelMoveRequested;

    public event EventHandler<DockPanelActionRequestedEventArgs>? PanelActionRequested;

    private event EventHandler<DockDragEventArgs>? _dragEvent;

    public event EventHandler<DockDragEventArgs>? DragEvent
    {
        add => _dragEvent += value;
        remove => _dragEvent -= value;
    }

    public bool IsHealthy
        => !_disposed && _visible && _projector.Current is { IsHealthy: true };

    public DockApplyResult? Current => _projector.Current;

    public DockHostResult Prepare(DockTree root)
    {
        if (_disposed)
            return DockHostResult.Fail(DockHostError.CreationFailed, "The host is disposed.");

        ArgumentNullException.ThrowIfNull(root);
        var validation = root.Validate();
        return validation.IsValid
            ? DockHostResult.Ok()
            : DockHostResult.Fail(
                DockHostError.ProjectionFailed,
                $"The candidate root is invalid: {validation.Summary}");
    }

    public DockHostResult Show()
    {
        if (_disposed)
            return DockHostResult.Fail(DockHostError.ShowFailed, "The host is disposed.");

        if (_projector.Current is not { IsHealthy: true })
        {
            return DockHostResult.Fail(
                DockHostError.ShowFailed,
                "The host has no healthy projection to show.");
        }

        _visible = true;
        return DockHostResult.Ok();
    }

    public DockHostResult Apply(DockTree root)
    {
        if (_disposed)
            return DockHostResult.Fail(DockHostError.ProjectionFailed, "The host is disposed.");

        ArgumentNullException.ThrowIfNull(root);
        try
        {
            var result = _projector.Apply(root);
            return result.Status == DockApplyStatus.Degraded
                ? DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    result.Diagnostic ?? "Dock projection degraded.")
                : DockHostResult.Ok(result.Diagnostic ?? string.Empty);
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(
                DockHostError.ProjectionFailed,
                $"{ex.GetType().Name}: {ex.Message}");
        }
    }

    public DockHostResult Retry()
    {
        if (_disposed)
            return DockHostResult.Fail(DockHostError.RetryFailed, "The host is disposed.");

        try
        {
            var result = _projector.Retry();
            return result.Status == DockApplyStatus.Degraded
                ? DockHostResult.Fail(
                    DockHostError.RetryFailed,
                    result.Diagnostic ?? "Dock projection is still degraded.")
                : DockHostResult.Ok(result.Diagnostic ?? string.Empty);
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(
                DockHostError.RetryFailed,
                $"{ex.GetType().Name}: {ex.Message}");
        }
    }

    public DockHostResult Close()
    {
        if (_disposed)
            return DockHostResult.Ok();

        _visible = false;
        return DockHostResult.Ok();
    }

    public bool TryGetCurrentToken(out DockApplyToken token)
        => _projector.TryGetCurrentToken(out token);

    public Control? FindNode(DockNodeId id) => _projector.FindNode(id);

    public IDockPanelView? FindPanel(PanelInstanceId instance)
        => _projector.FindPanel(instance);

    public bool TryHitTestLocal(
        Point point,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit)
        => _projector.TryHitTestDrop(point, sourceGroupId, out hit);

    public bool TryHitTestScreen(
        PixelPoint screenPoint,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit)
    {
        var local = _projector.Root.PointToClient(screenPoint);
        return TryHitTestLocal(local, sourceGroupId, out hit);
    }

    public void ShowExternalDropPreview(DockDropTargetHit hit)
        => _projector.ShowExternalDropPreview(hit);

    public void ClearExternalDropPreview()
        => _projector.ClearExternalDropPreview();

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        _visible = false;
        _dragEvent = null;
        _projector.Dispose();
    }
}
