using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Pointer-driven tab drag interaction. The session, hit targets and preview
/// are visual-only; the committed <see cref="DockTree"/> changes exclusively
/// through the single move request raised on release.
/// </summary>
internal sealed class DockDragController : IDisposable
{
    private const double DragThreshold = 4d;
    private const double TabStripMaximum = 36d;
    private const double TopEdgeBand = 10d;
    private const double EdgeBand = 48d;

    private static readonly IBrush AccentBrush =
        new SolidColorBrush(Color.FromArgb(230, 62, 140, 220));
    private static readonly IBrush GhostBackground =
        new SolidColorBrush(Color.FromArgb(224, 36, 40, 46));
    private static readonly IBrush DropFill =
        new SolidColorBrush(Color.FromArgb(56, 62, 140, 220));
    private static readonly IBrush DropBorder =
        new SolidColorBrush(Color.FromArgb(220, 62, 140, 220));

    private readonly ContentControl _root;
    private readonly DockWorkspaceProjector _projector;
    private PendingDrag? _pending;
    private ActiveDrag? _active;
    private Control? _projectedRoot;
    private Canvas? _overlay;
    private Border? _ghost;
    private Border? _dropHint;
    private Border? _insertionMarker;
    private bool _captureArmed;
    private bool _externalPreview;
    private bool _disposed;

    /// <summary>
    /// Raised for the owner-owned cross-window session. Local targets are
    /// still committed through <see cref="DockWorkspaceProjector.RequestPanelMove"/>.
    /// </summary>
    internal event EventHandler<DockDragEventArgs>? DragEvent;

    public DockDragController(ContentControl root, DockWorkspaceProjector projector)
    {
        _root = root ?? throw new ArgumentNullException(nameof(root));
        _projector = projector ?? throw new ArgumentNullException(nameof(projector));
        _root.Focusable = true;
        _root.AddHandler(
            InputElement.PointerPressedEvent,
            OnPointerPressedTunnel,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
        _root.AddHandler(
            InputElement.PointerPressedEvent,
            OnPointerPressedBubble,
            RoutingStrategies.Bubble,
            handledEventsToo: true);
        _root.AddHandler(
            InputElement.PointerMovedEvent,
            OnPointerMoved,
            RoutingStrategies.Bubble,
            handledEventsToo: true);
        _root.AddHandler(
            InputElement.PointerReleasedEvent,
            OnPointerReleased,
            RoutingStrategies.Bubble,
            handledEventsToo: true);
        _root.AddHandler(
            InputElement.PointerCaptureLostEvent,
            OnCaptureLost,
            RoutingStrategies.Direct,
            handledEventsToo: true);
        _root.AddHandler(
            InputElement.KeyDownEvent,
            OnKeyDown,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
    }

    public Control CreateHost(Control content)
    {
        ArgumentNullException.ThrowIfNull(content);
        OnProjectionReplaced();

        var host = new Grid { Name = "DockProjectionHost" };
        host.Children.Add(content);
        _projectedRoot = content;
        _overlay = new Canvas
        {
            Name = "DockDragOverlay",
            IsHitTestVisible = false,
            IsVisible = false,
            ZIndex = 1000,
        };
        host.Children.Add(_overlay);
        return host;
    }

    /// <summary>
    /// Called before a projection replaces the visual tree. An active drag is
    /// cancelled; a press that has not crossed the threshold may rebind to the
    /// rebuilt source panel.
    /// </summary>
    public void OnProjectionReplaced()
    {
        if (_disposed)
            return;

        if (_active is not null)
            RaiseDrag(DockDragEventArgs.Cancel());

        CancelActive(releaseCapture: true);
        ClearPreview();
        _overlay = null;
        _projectedRoot = null;

        // Activation commits rebuild the tree during the press. Keep the
        // pending press captured so the drag can still start afterwards.
        if (_pending is not null
            && TopLevel.GetTopLevel(_root) is not null
            && !ReferenceEquals(_pending.Pointer.Captured, _root))
        {
            _pending.Pointer.Capture(_root);
        }
    }

    public void CancelSession()
    {
        if (_disposed)
            return;

        if (_active is not null)
            RaiseDrag(DockDragEventArgs.Cancel());

        CancelActive(releaseCapture: true);
        _pending = null;
        _captureArmed = false;
        ClearPreview();
    }

    /// <summary>
    /// Resolves a drop target for a drag owned by another host. The point is
    /// local to this projector root; no session or model state is touched.
    /// </summary>
    internal bool TryHitTestDrop(
        Point point,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit)
    {
        hit = default;
        if (_disposed)
            return false;

        var target = ResolveTarget(
            point,
            sourceGroupId,
            sourceIndex: -1,
            out var bounds,
            out var slot);
        if (target is null)
            return false;

        hit = new DockDropTargetHit(target, bounds, slot);
        return true;
    }

    /// <summary>Paints the target preview for a drag owned by another host.</summary>
    internal void ShowExternalPreview(DockDropTargetHit hit)
    {
        if (_disposed || _overlay is null)
            return;

        EnsurePreviewControls();
        _externalPreview = true;
        if (_ghost is not null)
            _ghost.IsVisible = false;

        DrawTargetPreview(hit.Target, hit.Bounds, hit.VisualSlot);
        _overlay.IsVisible = true;
    }

    internal void ClearExternalPreview()
    {
        if (_disposed || !_externalPreview)
            return;

        // Only remove the foreign drop hint; the source host's own ghost and
        // insertion marker belong to its native drag session.
        _externalPreview = false;
        if (_dropHint is not null)
            _dropHint.IsVisible = false;
        if (_insertionMarker is not null)
            _insertionMarker.IsVisible = false;
        if (_overlay is not null && _active is null)
            _overlay.IsVisible = false;
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        CancelSession();
        _disposed = true;
        _overlay = null;
        _projectedRoot = null;
    }

    private void OnPointerPressedTunnel(object? sender, PointerPressedEventArgs e)
    {
        if (_disposed || _pending is not null || _active is not null)
            return;

        if (!e.GetCurrentPoint(_root).Properties.IsLeftButtonPressed)
            return;

        var point = e.GetPosition(_root);
        if (!TryResolveTab(point, out var groupId, out var panel))
            return;

        _pending = new PendingDrag(groupId, panel, point, e.Pointer);
        _captureArmed = true;
    }

    private void OnPointerPressedBubble(object? sender, PointerPressedEventArgs e)
    {
        if (_disposed || !_captureArmed || _pending is null)
            return;

        _captureArmed = false;
        CapturePointer(e.Pointer);
    }

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_disposed)
            return;

        var point = e.GetPosition(_root);
        if (_active is null && _pending is { } pending)
        {
            if (Distance(point, pending.Origin) < DragThreshold)
                return;

            BeginDrag(pending, point);
        }

        if (_active is null)
            return;

        UpdateDrag(point);
        e.Handled = true;
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_disposed)
            return;

        if (_active is { } active)
        {
            var point = e.GetPosition(_root);
            UpdateDrag(point);
            var target = active.Target;
            _active = null;

            if (target is not null)
            {
                RaiseDrag(DockDragEventArgs.Release(
                    point,
                    localTargetResolved: true,
                    default,
                    hasScreenPoint: false,
                    insideSource: true,
                    KeyModifiers.None,
                    renderScaling: 1d));
                _projector.RequestPanelMove(active.Panel, target, active.Token);
            }
            else
            {
                RaiseDrag(CreateRelease(point, e.KeyModifiers));
            }

            ClearPreview();
            ReleasePointer(active.Pointer);
            e.Handled = true;
            return;
        }

        if (_pending is not null)
        {
            var pointer = _pending.Pointer;
            _pending = null;
            _captureArmed = false;
            ReleasePointer(pointer);
        }
    }

    private void OnCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        if (_disposed)
            return;

        if (_active is not null)
            RaiseDrag(DockDragEventArgs.Cancel());

        _pending = null;
        _captureArmed = false;
        CancelActive(releaseCapture: false);
        ClearPreview();
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (_disposed || e.Key != Key.Escape)
            return;

        if (_active is null && _pending is null)
            return;

        CancelSession();
        e.Handled = true;
    }

    private void BeginDrag(PendingDrag pending, Point point)
    {
        _pending = null;

        var tree = _projector.CommittedTree;
        if (tree is null
            || !tree.TryFindPanel(pending.Panel, out var group, out var index)
            || group is null
            || group.Id != pending.GroupId
            || _projector.FindNode(group.Id) is not TabControl tabs
            || !_projector.TryGetCurrentToken(out var token))
        {
            ReleasePointer(pending.Pointer);
            return;
        }

        _active = new ActiveDrag(
            pending.Panel,
            group.Id,
            index,
            token,
            pending.Pointer);
        EnsureGhost(tabs, index);
        RaiseDrag(DockDragEventArgs.Begin(pending.Panel, group.Id, token));
        UpdateDrag(point);
        if (TopLevel.GetTopLevel(_root) is not null)
            _root.Focus();
    }

    private void UpdateDrag(Point point)
    {
        var active = _active;
        if (active is null)
            return;

        active.Target = ResolveTarget(
            point,
            active.GroupId,
            active.SourceIndex,
            out var bounds,
            out var slot);
        active.TargetBounds = bounds;
        active.VisualSlot = slot;
        UpdatePreview(point, active);

        if (active.Target is not null)
        {
            RaiseDrag(DockDragEventArgs.Move(
                point,
                localTargetResolved: true,
                default,
                hasScreenPoint: false,
                insideSource: true));
        }
        else if (TryGetScreenPoint(point, out var screen, out var inside, out _, out _))
        {
            RaiseDrag(DockDragEventArgs.Move(
                point,
                localTargetResolved: false,
                screen,
                hasScreenPoint: true,
                inside));
        }
    }

    private DockTarget? ResolveTarget(
        Point point,
        DockNodeId? sourceGroupId,
        int sourceIndex,
        out Rect bounds,
        out int visualSlot)
    {
        bounds = default;
        visualSlot = -1;

        if (!TryFindGroup(point, out var group, out var tabs, out bounds))
            return null;

        var strip = GetTabStripHeight(tabs, bounds);
        var topBand = Math.Min(TopEdgeBand, strip * 0.5d);
        var localX = point.X - bounds.X;
        var localY = point.Y - bounds.Y;

        if (localY <= topBand)
            return new DockTarget.Edge(group.Id, DockEdge.Top);

        if (localY <= strip)
        {
            visualSlot = ComputeInsertionSlot(group, tabs, point, bounds);
            var index = ToModelIndex(group, visualSlot);
            if (group.Id == sourceGroupId && sourceIndex >= 0 && index > sourceIndex)
                index--;
            return new DockTarget.Center(group.Id, index);
        }

        var contentHeight = bounds.Height - strip;
        var edge = Math.Min(EdgeBand, Math.Min(bounds.Width, contentHeight) * 0.25d);
        var left = localX;
        var right = bounds.Width - localX;
        var bottom = bounds.Height - localY;
        var nearest = double.MaxValue;
        DockEdge? side = null;
        if (left <= edge && left < nearest)
        {
            nearest = left;
            side = DockEdge.Left;
        }

        if (right <= edge && right < nearest)
        {
            nearest = right;
            side = DockEdge.Right;
        }

        if (bottom <= edge && bottom < nearest)
            side = DockEdge.Bottom;

        if (side is not null)
            return new DockTarget.Edge(group.Id, side.Value);

        if (group.Id == sourceGroupId)
            return null;

        return new DockTarget.Center(group.Id, null);
    }

    private bool TryResolveTab(Point point, out DockNodeId groupId, out PanelInstanceId panel)
    {
        groupId = default;
        panel = default;

        var tree = _projector.CommittedTree;
        if (tree is null || !TryFindGroup(point, out var group, out var tabs, out var bounds))
            return false;

        var strip = GetTabStripHeight(tabs, bounds);
        var localY = point.Y - bounds.Y;
        if (localY < 0 || localY > strip || group.VisiblePanels.Count == 0)
            return false;

        var slot = ComputeInsertionSlot(group, tabs, point, bounds);
        var index = Math.Clamp(slot, 0, group.VisiblePanels.Count - 1);
        groupId = group.Id;
        panel = group.VisiblePanels[index];
        return true;
    }

    private bool TryFindGroup(
        Point point,
        out DockTabGroupNode group,
        out TabControl tabs,
        out Rect bounds)
    {
        group = null!;
        tabs = null!;
        bounds = default;

        var tree = _projector.CommittedTree;
        var projectedRoot = _projectedRoot;
        if (tree is null || projectedRoot is null)
            return false;

        var offset = ProjectedRootOffset();
        Control? bestControl = null;
        DockTabGroupNode? bestGroup = null;
        var bestBounds = default(Rect);
        var bestArea = double.MaxValue;

        foreach (var candidate in tree.EnumerateNodes().OfType<DockTabGroupNode>())
        {
            if (_projector.FindNode(candidate.Id) is not TabControl control
                || control.Bounds.Width <= 0
                || control.Bounds.Height <= 0)
            {
                continue;
            }

            var origin = control.TranslatePoint(default, projectedRoot);
            if (origin is null)
                continue;

            var rect = new Rect(
                origin.Value.X + offset.X,
                origin.Value.Y + offset.Y,
                control.Bounds.Width,
                control.Bounds.Height);
            if (!rect.Contains(point))
                continue;

            var area = rect.Width * rect.Height;
            if (area >= bestArea)
                continue;

            bestArea = area;
            bestGroup = candidate;
            bestControl = control;
            bestBounds = rect;
        }

        if (bestGroup is null || bestControl is not TabControl bestTabs)
            return false;

        group = bestGroup;
        tabs = bestTabs;
        bounds = bestBounds;
        return true;
    }

    private int ComputeInsertionSlot(
        DockTabGroupNode group,
        TabControl tabs,
        Point point,
        Rect bounds)
    {
        // The tab strip only shows visible panels; hidden panels keep their
        // model position but have no slot on screen.
        var count = group.VisiblePanels.Count;
        if (count == 0)
            return 0;

        var offset = ProjectedRootOffset();
        var projectedRoot = _projectedRoot;
        var centers = new List<double>(count);
        var usable = projectedRoot is not null;

        for (var index = 0; usable && index < count && index < tabs.Items.Count; index++)
        {
            if (tabs.Items[index] is not TabItem item
                || item.Bounds.Width <= 0
                || item.Bounds.Height <= 0)
            {
                usable = false;
                break;
            }

            var center = item.TranslatePoint(
                new Point(item.Bounds.Width / 2d, 0),
                projectedRoot!);
            if (center is null)
            {
                usable = false;
                break;
            }

            centers.Add(center.Value.X + offset.X);
        }

        int slot;
        if (usable && centers.Count == count)
        {
            slot = 0;
            foreach (var center in centers)
            {
                if (center < point.X)
                    slot++;
            }
        }
        else
        {
            var ratio = Math.Clamp((point.X - bounds.X) / bounds.Width, 0d, 1d);
            slot = (int)Math.Round(ratio * count, MidpointRounding.AwayFromZero);
        }

        return Math.Clamp(slot, 0, count);
    }

    private static int ToModelIndex(DockTabGroupNode group, int visibleSlot)
    {
        var visible = group.VisiblePanels;
        if (visible.Count == 0)
            return group.Panels.Count;

        if (visibleSlot >= visible.Count)
            return group.Panels.Count;

        var target = visible[Math.Max(visibleSlot, 0)];
        for (var index = 0; index < group.Panels.Count; index++)
        {
            if (group.Panels[index] == target)
                return index;
        }

        return group.Panels.Count;
    }

    private double GetTabStripHeight(TabControl tabs, Rect bounds)
    {
        var projectedRoot = _projectedRoot;
        if (projectedRoot is not null
            && tabs.Items.Count > 0
            && tabs.Items[0] is TabItem item
            && item.Bounds.Height > 0)
        {
            var bottom = item.TranslatePoint(
                new Point(0, item.Bounds.Height),
                projectedRoot);
            if (bottom is not null)
            {
                var offset = ProjectedRootOffset();
                var height = bottom.Value.Y + offset.Y - bounds.Y;
                if (height > 0)
                    return Math.Min(height, bounds.Height * 0.5d);
            }
        }

        return Math.Min(TabStripMaximum, bounds.Height * 0.25d);
    }

    private Point ProjectedRootOffset()
    {
        var projectedRoot = _projectedRoot;
        if (projectedRoot is null)
            return default;

        return projectedRoot.TranslatePoint(default, _root) ?? default;
    }

    private void EnsureGhost(TabControl tabs, int index)
    {
        if (_overlay is null)
            return;

        EnsurePreviewControls();

        var title = _projector.FindPanel(_active!.Panel)?.Title;
        if (string.IsNullOrWhiteSpace(title)
            && index >= 0
            && index < tabs.Items.Count
            && tabs.Items[index] is TabItem item)
        {
            title = item.Header?.ToString();
        }

        if (_ghost!.Child is TextBlock text)
            text.Text = string.IsNullOrWhiteSpace(title)
                ? _active.Panel.ToString()
                : title;
        _ghost.IsVisible = true;
        _overlay.IsVisible = true;
    }

    private void EnsurePreviewControls()
    {
        if (_overlay is null || _ghost is not null)
            return;

        _dropHint = new Border
        {
            Name = "DockDropPreview",
            Background = DropFill,
            BorderBrush = DropBorder,
            BorderThickness = new Thickness(2),
            IsHitTestVisible = false,
            IsVisible = false,
        };
        _insertionMarker = new Border
        {
            Name = "DockDropInsertion",
            Width = 3,
            Background = DropBorder,
            IsHitTestVisible = false,
            IsVisible = false,
        };
        _ghost = new Border
        {
            Name = "DockDragGhost",
            Background = GhostBackground,
            BorderBrush = DropBorder,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(3),
            Padding = new Thickness(10, 4),
            MaxWidth = 220,
            IsHitTestVisible = false,
            Child = new TextBlock
            {
                Foreground = Brushes.White,
                FontSize = 12,
                TextTrimming = TextTrimming.CharacterEllipsis,
            },
        };
        _overlay.Children.Add(_dropHint);
        _overlay.Children.Add(_insertionMarker);
        _overlay.Children.Add(_ghost);
    }

    private void UpdatePreview(Point point, ActiveDrag active)
    {
        if (_overlay is null || _ghost is null || _dropHint is null || _insertionMarker is null)
            return;

        var offset = ProjectedRootOffset();
        var local = new Point(point.X - offset.X, point.Y - offset.Y);
        Canvas.SetLeft(_ghost, local.X + 12);
        Canvas.SetTop(_ghost, local.Y + 12);
        _ghost.IsVisible = true;

        var target = active.Target;
        if (target is null)
        {
            _dropHint.IsVisible = false;
            _insertionMarker.IsVisible = false;
            return;
        }

        DrawTargetPreview(target, active.TargetBounds, active.VisualSlot);
    }

    private void DrawTargetPreview(DockTarget target, Rect bounds, int visualSlot)
    {
        if (_dropHint is null || _insertionMarker is null)
            return;

        var offset = ProjectedRootOffset();
        var rect = new Rect(
            bounds.X - offset.X,
            bounds.Y - offset.Y,
            bounds.Width,
            bounds.Height);

        if (target is DockTarget.Edge edge)
        {
            var hint = edge.Side switch
            {
                DockEdge.Left => new Rect(rect.X, rect.Y, rect.Width / 2d, rect.Height),
                DockEdge.Right => new Rect(rect.X + rect.Width / 2d, rect.Y, rect.Width / 2d, rect.Height),
                DockEdge.Top => new Rect(rect.X, rect.Y, rect.Width, rect.Height / 2d),
                _ => new Rect(rect.X, rect.Y + rect.Height / 2d, rect.Width, rect.Height / 2d),
            };
            PositionHint(hint);
            _insertionMarker.IsVisible = false;
            return;
        }

        PositionHint(rect);
        var group = _projector.CommittedTree?.FindGroup(
            (target as DockTarget.Center)!.GroupId);
        var count = group?.VisiblePanels.Count ?? 0;
        if (count <= 0 || visualSlot < 0)
        {
            _insertionMarker.IsVisible = false;
            return;
        }

        var tabs = group is null ? null : _projector.FindNode(group.Id) as TabControl;
        var strip = tabs is null
            ? Math.Min(TabStripMaximum, rect.Height * 0.25d)
            : GetTabStripHeight(tabs, bounds);
        Canvas.SetLeft(_insertionMarker, rect.X + rect.Width * visualSlot / count);
        Canvas.SetTop(_insertionMarker, rect.Y);
        _insertionMarker.Height = strip;
        _insertionMarker.IsVisible = true;
    }

    private void PositionHint(Rect rect)
    {
        if (_dropHint is null)
            return;

        Canvas.SetLeft(_dropHint, rect.X);
        Canvas.SetTop(_dropHint, rect.Y);
        _dropHint.Width = rect.Width;
        _dropHint.Height = rect.Height;
        _dropHint.IsVisible = true;
    }

    private void ClearPreview()
    {
        _externalPreview = false;
        _ghost = null;
        _dropHint = null;
        _insertionMarker = null;
        if (_overlay is null)
            return;

        _overlay.Children.Clear();
        _overlay.IsVisible = false;
    }

    private void CancelActive(bool releaseCapture)
    {
        var pointer = _active?.Pointer;
        _active = null;
        if (releaseCapture && pointer is not null)
            ReleasePointer(pointer);
    }

    private void CapturePointer(IPointer pointer)
    {
        // Unattached controls (headless tests, early lifecycle) have no input
        // root; routed events still drive the session without OS capture.
        if (TopLevel.GetTopLevel(_root) is not null)
            pointer.Capture(_root);
    }

    private void ReleasePointer(IPointer pointer)
    {
        if (ReferenceEquals(pointer.Captured, _root))
            pointer.Capture(null);
    }

    private DockDragEventArgs CreateRelease(Point point, KeyModifiers modifiers)
    {
        if (!TryGetScreenPoint(
                point,
                out var screen,
                out var inside,
                out var scaling,
                out var targetScreen))
        {
            return DockDragEventArgs.Release(
                point,
                localTargetResolved: false,
                default,
                hasScreenPoint: false,
                insideSource: true,
                modifiers,
                renderScaling: 1d);
        }

        return DockDragEventArgs.Release(
            point,
            localTargetResolved: false,
            screen,
            hasScreenPoint: true,
            inside,
            modifiers,
            scaling,
            targetScreen);
    }

    /// <summary>
    /// Converts a root-local drag point to screen pixels. Returns false when
    /// the host is not attached to a top level (headless gates, early lifecycle),
    /// in which case no external session can be resolved.
    /// </summary>
    private bool TryGetScreenPoint(
        Point point,
        out PixelPoint screen,
        out bool insideSource,
        out double scaling,
        out DockScreenArea? targetScreen)
    {
        screen = default;
        insideSource = true;
        scaling = 1d;
        targetScreen = null;

        var top = TopLevel.GetTopLevel(_root);
        var topPoint = top is null ? null : _root.TranslatePoint(point, top);
        if (top is null || topPoint is null)
            return false;

        try
        {
            screen = top.PointToScreen(topPoint.Value);
            targetScreen = FindScreen(top, screen);

            // Scaling here describes the source window and is only used for
            // its own frame test; the pointer's monitor owns the tear-off
            // conversion (ADR-011 O-3.2).
            scaling = top.RenderScaling;
            insideSource = IsInsideTopLevel(top, screen, scaling);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static DockScreenArea? FindScreen(TopLevel top, PixelPoint point)
    {
        try
        {
            var screens = top.Screens;
            var screen = screens?.ScreenFromPoint(point);
            if (screen is null)
                return null;

            return new DockScreenArea(
                screen.DisplayName ?? string.Empty,
                ToPixelRect(screen.Bounds),
                ToPixelRect(screen.WorkingArea),
                screen.Scaling,
                screen.IsPrimary);
        }
        catch
        {
            // A window without a platform implementation cannot answer.
            return null;
        }
    }

    private static DockPixelRect ToPixelRect(PixelRect rect)
        => new(rect.X, rect.Y, rect.Width, rect.Height);

    private static bool IsInsideTopLevel(TopLevel top, PixelPoint screen, double scaling)
    {
        if (top is Window window && window.FrameSize is { } frame)
        {
            if (frame.Width > 0 && frame.Height > 0)
            {
                return Contains(
                    window.Position,
                    frame.Width * scaling,
                    frame.Height * scaling,
                    screen);
            }
        }

        var origin = top.PointToScreen(default);
        return Contains(
            origin,
            top.ClientSize.Width * scaling,
            top.ClientSize.Height * scaling,
            screen);
    }

    private static bool Contains(
        PixelPoint origin,
        double width,
        double height,
        PixelPoint screen)
        => screen.X >= origin.X
           && screen.X < origin.X + width
           && screen.Y >= origin.Y
           && screen.Y < origin.Y + height;

    private void RaiseDrag(DockDragEventArgs args)
        => DragEvent?.Invoke(this, args);

    private static double Distance(Point first, Point second)
    {
        var dx = first.X - second.X;
        var dy = first.Y - second.Y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    private sealed class PendingDrag(
        DockNodeId groupId,
        PanelInstanceId panel,
        Point origin,
        IPointer pointer)
    {
        public DockNodeId GroupId { get; } = groupId;
        public PanelInstanceId Panel { get; } = panel;
        public Point Origin { get; } = origin;
        public IPointer Pointer { get; } = pointer;
    }

    private sealed class ActiveDrag(
        PanelInstanceId panel,
        DockNodeId groupId,
        int sourceIndex,
        DockApplyToken token,
        IPointer pointer)
    {
        public PanelInstanceId Panel { get; } = panel;
        public DockNodeId GroupId { get; } = groupId;
        public int SourceIndex { get; } = sourceIndex;
        public DockApplyToken Token { get; } = token;
        public IPointer Pointer { get; } = pointer;
        public DockTarget? Target { get; set; }
        public Rect TargetBounds { get; set; }
        public int VisualSlot { get; set; } = -1;
    }
}
