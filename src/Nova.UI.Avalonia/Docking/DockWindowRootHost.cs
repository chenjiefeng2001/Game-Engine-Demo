using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

public sealed class DockFloatBoundsChangedEventArgs(DockBounds bounds, bool maximized)
    : EventArgs
{
    public DockBounds Bounds { get; } = bounds;

    public bool Maximized { get; } = maximized;
}

/// <summary>
/// Floating root host backed by a real top-level window. The window is created
/// hidden by the factory, projected only after the model commits, and its X
/// button is translated into a workspace close request by the owner.
/// </summary>
public sealed class DockWindowRootHost
    : IDockRootHost, IDockDropTargetHost, IDockFloatHostBounds, IDockRootHostCallbacks,
      IDockDragSourceHost
{
    private static readonly IBrush RecoveryBackground =
        new SolidColorBrush(Color.FromArgb(235, 44, 30, 30));

    private static readonly IBrush RecoveryBorder =
        new SolidColorBrush(Color.FromArgb(220, 214, 96, 96));

    private readonly DockRootHostDescriptor _descriptor;
    private readonly IDockFloatWindow _window;
    private readonly DockWorkspaceProjector _projector;
    private readonly Border _recoveryBanner;
    private readonly TextBlock _recoveryText;
    private DockBounds _bounds = new(120, 120, 640, 480);
    private DockScreenArea? _preferredScreen;
    private bool _maximized;
    private bool _visible;
    private bool _closeRequestActive;
    private bool _closeApproved;
    private bool _disposed;

    public DockWindowRootHost(
        DockRootHostDescriptor descriptor,
        DockPanelViewRegistry registry,
        IDockFloatWindow window,
        IDockNodeApplyInterceptor? interceptor = null)
    {
        if (descriptor.Id.IsEmpty)
            throw new ArgumentException("A dock host id must be non-empty.", nameof(descriptor));
        if (descriptor.Kind != DockRootKind.Float || descriptor.FloatRootId is null)
            throw new ArgumentException("A floating host requires a float descriptor.", nameof(descriptor));

        _descriptor = descriptor;
        _window = window ?? throw new ArgumentNullException(nameof(window));
        _projector = new DockWorkspaceProjector(registry, descriptor.Id, interceptor);
        _projector.PanelActivated += (_, e) => PanelActivated?.Invoke(this, e);
        _projector.SplitResizeRequested += (_, e) => SplitResizeRequested?.Invoke(this, e);
        _projector.PanelMoveRequested += (_, e) => PanelMoveRequested?.Invoke(this, e);
        _projector.PanelActionRequested += (_, e) => PanelActionRequested?.Invoke(this, e);
        _projector.DragEvent += (_, e) => DragEvent?.Invoke(this, e);

        _recoveryText = new TextBlock
        {
            TextWrapping = TextWrapping.Wrap,
            Foreground = Brushes.White,
            Margin = new Thickness(12, 8, 12, 0),
        };
        var retry = new Button
        {
            Content = "Retry panel",
            Margin = new Thickness(12, 6, 12, 10),
            HorizontalAlignment = HorizontalAlignment.Left,
        };
        retry.Click += (_, _) => Retry();

        _recoveryBanner = new Border
        {
            Name = "DockFloatRecovery",
            IsVisible = false,
            VerticalAlignment = VerticalAlignment.Top,
            Background = RecoveryBackground,
            BorderBrush = RecoveryBorder,
            BorderThickness = new Thickness(0, 0, 0, 1),
            Child = new StackPanel { Children = { _recoveryText, retry } },
        };

        var shell = new Grid();
        shell.Children.Add(_projector.Root);
        shell.Children.Add(_recoveryBanner);
        _window.Content = shell;
        _window.Closing += OnWindowClosing;
        _window.BoundsChanged += OnWindowBoundsChanged;
    }

    public DockRootHostId Id => _descriptor.Id;

    public DockRootKind Kind => _descriptor.Kind;

    public DockFloatRootId FloatRootId => _descriptor.FloatRootId!.Value;

    public bool IsVisible => !_disposed && _visible && _window.IsVisible;

    public bool IsHealthy
        => IsVisible && _projector.Current is { IsHealthy: true };

    public DockApplyResult? Current => _projector.Current;

    public Control Root => _projector.Root;

    /// <summary>Raised after the model committed so the owner can re-dock.</summary>
    public event EventHandler? CloseRequested;

    /// <summary>Raised after the user moved, resized or maximized the window.</summary>
    public event EventHandler<DockFloatBoundsChangedEventArgs>? BoundsChanged;

    public event EventHandler<DockPanelActivatedEventArgs>? PanelActivated;

    public event EventHandler<DockSplitResizeRequestedEventArgs>? SplitResizeRequested;

    public event EventHandler<DockPanelMoveRequestedEventArgs>? PanelMoveRequested;

    public event EventHandler<DockPanelActionRequestedEventArgs>? PanelActionRequested;

    public event EventHandler<DockDragEventArgs>? DragEvent;

    /// <summary>
    /// Result of the placement applied on the last Show; null when the window
    /// already had a place or the platform could not describe its screens.
    /// </summary>
    public DockScreenPlacement? LastPlacement { get; private set; }

    public void SetWindowBounds(
        DockBounds bounds,
        bool maximized,
        DockScreenArea? preferredScreen = null)
    {
        if (_disposed)
            return;

        _bounds = bounds;
        _maximized = maximized;
        _preferredScreen = preferredScreen;
        if (_window.IsVisible)
        {
            _window.ApplyBounds(
                bounds,
                maximized,
                preferredScreen?.EffectiveScaling ?? 0d);
        }
    }

    public bool TryGetWindowBounds(out DockBounds bounds)
    {
        if (_disposed || !_visible || !_window.IsVisible)
        {
            bounds = default;
            return false;
        }

        bounds = _window.Bounds;
        return bounds.IsValid;
    }

    /// <summary>Normal geometry the host will apply when the window is unmaximized.</summary>
    public DockBounds SettledBounds => _bounds;

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

        if (_window.IsVisible)
        {
            _visible = true;
            return DockHostResult.Ok();
        }

        try
        {
            _closeApproved = false;
            var placed = PlaceForShow(_bounds);
            _bounds = placed.Bounds;
            LastPlacement = placed;
            var scaling = placed.Screen?.EffectiveScaling ?? 0d;
            _window.Show(_bounds, scaling);
            _visible = _window.IsVisible;
            _window.ApplyBounds(_bounds, _maximized, scaling);

            // The non-client frame is only measurable once the platform window
            // exists; one corrective pass keeps the outer frame inside the work
            // area without any fixed delay (ADR-011 O-2/O-5).
            var framed = PlaceForShow(_bounds);
            if (framed.Changed)
            {
                _bounds = framed.Bounds;
                LastPlacement = framed;
                _window.ApplyBounds(
                    _bounds,
                    _maximized,
                    framed.Screen?.EffectiveScaling ?? 0d);
            }

            _preferredScreen = null;
            return _visible
                ? DockHostResult.Ok()
                : DockHostResult.Fail(
                    DockHostError.ShowFailed,
                    "The floating window did not become visible.");
        }
        catch (Exception ex)
        {
            _visible = false;
            return DockHostResult.Fail(
                DockHostError.ShowFailed,
                $"{ex.GetType().Name}: {ex.Message}");
        }
    }

    private DockScreenPlacement PlaceForShow(DockBounds bounds)
        => DockScreenGeometry.PlaceWindowBounds(
            bounds,
            _window.FrameMargins,
            _window.Screens,
            _preferredScreen);

    public DockHostResult Apply(DockTree root)
    {
        if (_disposed)
            return DockHostResult.Fail(DockHostError.ProjectionFailed, "The host is disposed.");

        ArgumentNullException.ThrowIfNull(root);
        try
        {
            var result = _projector.Apply(root);
            UpdateTitle();
            UpdateRecoveryState();
            return result.Status == DockApplyStatus.Degraded
                ? DockHostResult.Fail(
                    DockHostError.ProjectionFailed,
                    result.Diagnostic ?? "Dock projection degraded.")
                : DockHostResult.Ok(result.Diagnostic ?? string.Empty);
        }
        catch (Exception ex)
        {
            UpdateRecoveryState();
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
            UpdateTitle();
            UpdateRecoveryState();
            return result.Status == DockApplyStatus.Degraded
                ? DockHostResult.Fail(
                    DockHostError.RetryFailed,
                    result.Diagnostic ?? "Dock projection is still degraded.")
                : DockHostResult.Ok(result.Diagnostic ?? string.Empty);
        }
        catch (Exception ex)
        {
            UpdateRecoveryState();
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
        _closeApproved = true;

        if (_closeRequestActive)
            return DockHostResult.Ok();

        try
        {
            ClearExternalDropPreview();
            _window.Close();
            return DockHostResult.Ok();
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(
                DockHostError.CloseFailed,
                $"{ex.GetType().Name}: {ex.Message}");
        }
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
        _closeApproved = true;
        _window.Closing -= OnWindowClosing;
        _window.BoundsChanged -= OnWindowBoundsChanged;
        try
        {
            _window.Dispose();
        }
        catch
        {
            // Releasing visuals must not mask the committed model.
        }

        _projector.Dispose();
    }

    private void OnWindowClosing(object? sender, DockFloatWindowClosingEventArgs e)
    {
        if (_disposed || _closeApproved)
            return;

        e.Cancel = true;
        if (_closeRequestActive)
            return;

        _closeRequestActive = true;
        try
        {
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
        finally
        {
            _closeRequestActive = false;
        }

        if (_closeApproved)
            e.Cancel = false;
    }

    private void OnWindowBoundsChanged(object? sender, EventArgs e)
    {
        if (_disposed || !_visible)
            return;

        BoundsChanged?.Invoke(
            this,
            new DockFloatBoundsChangedEventArgs(_window.Bounds, _window.IsMaximized));
    }

    private void UpdateRecoveryState()
    {
        var current = _projector.Current;
        var degraded = current is { Status: DockApplyStatus.Degraded };
        _recoveryBanner.IsVisible = degraded;
        if (!degraded)
            return;

        _recoveryText.Text = string.IsNullOrWhiteSpace(current!.Diagnostic)
            ? "This floating panel could not be rendered."
            : $"This floating panel could not be rendered. {current.Diagnostic}";
    }

    private void UpdateTitle()
    {
        var first = _projector.CommittedTree?.EnumerateVisiblePanels().FirstOrDefault();
        var title = first is { } panel ? _projector.FindPanel(panel)?.Title : null;
        _window.Title = string.IsNullOrWhiteSpace(title)
            ? "Nova Floating Panel"
            : $"Nova — {title}";
    }
}
