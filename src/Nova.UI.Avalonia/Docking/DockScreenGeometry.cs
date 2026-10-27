using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Physical pixel rectangle in virtual-desktop coordinates. Screens and work
/// areas are always physical pixels; converting them to DIP requires one
/// specific screen and that screen's own scaling.
/// </summary>
public readonly record struct DockPixelRect(int X, int Y, int Width, int Height)
{
    public int Right => X + Width;

    public int Bottom => Y + Height;

    public bool IsEmpty => Width <= 0 || Height <= 0;

    public bool Contains(int x, int y)
        => x >= X && x < Right && y >= Y && y < Bottom;

    public bool Contains(DockPixelRect other)
        => !other.IsEmpty
           && other.X >= X
           && other.Y >= Y
           && other.Right <= Right
           && other.Bottom <= Bottom;

    public long OverlapArea(DockPixelRect other)
    {
        var left = Math.Max(X, other.X);
        var top = Math.Max(Y, other.Y);
        var right = Math.Min(Right, other.Right);
        var bottom = Math.Min(Bottom, other.Bottom);
        return right <= left || bottom <= top ? 0L : (long)(right - left) * (bottom - top);
    }
}

/// <summary>
/// One monitor as the host layer sees it. <see cref="Scaling"/> belongs to this
/// monitor alone; it must never be replaced by another window's scaling.
/// </summary>
public readonly record struct DockScreenArea(
    string Device,
    DockPixelRect Bounds,
    DockPixelRect WorkArea,
    double Scaling,
    bool Primary)
{
    public double EffectiveScaling => double.IsFinite(Scaling) && Scaling > 0d ? Scaling : 1d;

    public DockPixelRect EffectiveWorkArea => WorkArea.IsEmpty ? Bounds : WorkArea;
}

/// <summary>
/// Non-client thickness of one window in DIP (outer frame minus client area,
/// split per edge). Zero means "unknown", in which case placement is computed
/// against the client rectangle without a border correction.
/// </summary>
public readonly record struct DockWindowFrame(double Left, double Top, double Right, double Bottom)
{
    public static DockWindowFrame None => default;

    public double Horizontal => Math.Max(0d, Left) + Math.Max(0d, Right);

    public double Vertical => Math.Max(0d, Top) + Math.Max(0d, Bottom);

    public DockWindowFrame Normalized
        => new(
            Math.Max(0d, Finite(Left)),
            Math.Max(0d, Finite(Top)),
            Math.Max(0d, Finite(Right)),
            Math.Max(0d, Finite(Bottom)));

    private static double Finite(double value) => double.IsFinite(value) ? value : 0d;
}

public enum DockPlacementReason
{
    /// <summary>No screen information was available; the bounds are unchanged.</summary>
    NoScreens,

    /// <summary>The input was not a valid model rectangle; the bounds are unchanged.</summary>
    NotPlaceable,

    /// <summary>The window already fits inside one current work area (O-2.1).</summary>
    InsideWorkArea,

    /// <summary>The window was moved or shrunk into a work area (O-2.2/O-2.4).</summary>
    Clamped,

    /// <summary>No work area overlapped; the primary work area was used (O-2.3).</summary>
    PrimaryFallback,
}

public readonly record struct DockScreenPlacement(
    DockBounds Bounds,
    DockScreenArea? Screen,
    DockPlacementReason Reason,
    bool Moved,
    bool Resized)
{
    public bool Changed => Moved || Resized;
}

/// <summary>
/// Deterministic visible-work-area placement for floating windows. The model
/// never sees a screen: every rule here runs in the host layer on explicit
/// screen data (ADR-011 O-1/O-2).
/// </summary>
public static class DockScreenGeometry
{
    public const double DefaultTearOffWidth = 640d;

    public const double DefaultTearOffHeight = 480d;

    private const double Tolerance = 0.0005d;

    /// <summary>Resolves the screen under a physical pixel point.</summary>
    public static DockScreenArea? ScreenFromPoint(
        IReadOnlyList<DockScreenArea>? screens,
        int x,
        int y)
    {
        if (screens is null || screens.Count == 0)
            return null;

        var ordered = OrderedScreens(screens);
        foreach (var screen in ordered)
        {
            if (screen.EffectiveWorkArea.Contains(x, y))
                return screen;
        }

        foreach (var screen in ordered)
        {
            if (screen.Bounds.Contains(x, y))
                return screen;
        }

        return null;
    }

    /// <summary>
    /// Places one window's model geometry (outer frame origin plus client size,
    /// ADR-011 §十一·二) so that its outer frame stays visible and operable
    /// (O-2). <paramref name="preferredScreen"/> is the monitor under the
    /// pointer for tear-off; when null the rule picks the work area with the
    /// largest overlap and falls back to the primary. The result is in the same
    /// model units the window adapter applies, so no caller ever has to mix an
    /// outer frame origin with a client origin.
    /// </summary>
    public static DockScreenPlacement PlaceWindowBounds(
        DockBounds modelDip,
        DockWindowFrame frame,
        IReadOnlyList<DockScreenArea>? screens,
        DockScreenArea? preferredScreen = null)
    {
        if (screens is null || screens.Count == 0)
            return new DockScreenPlacement(modelDip, null, DockPlacementReason.NoScreens, false, false);

        if (!modelDip.IsValid)
            return new DockScreenPlacement(modelDip, null, DockPlacementReason.NotPlaceable, false, false);

        var frameDip = frame.Normalized;
        var outer = new DockBounds(
            modelDip.X,
            modelDip.Y,
            modelDip.Width + frameDip.Horizontal,
            modelDip.Height + frameDip.Vertical);

        var ordered = OrderedScreens(screens);
        DockScreenArea target;
        var usedPrimaryFallback = false;

        if (preferredScreen is { } preferred)
        {
            target = preferred;
        }
        else
        {
            target = ordered[0];
            var chosen = false;
            foreach (var screen in ordered)
            {
                var candidate = ToPixels(outer, screen);
                if (screen.EffectiveWorkArea.Contains(candidate))
                {
                    // O-2.1: a rectangle that already fits somewhere is never
                    // moved to the primary just because no identity was saved.
                    return new DockScreenPlacement(
                        modelDip, screen, DockPlacementReason.InsideWorkArea, false, false);
                }
            }

            var best = 0L;
            foreach (var screen in ordered)
            {
                var overlap = screen.EffectiveWorkArea.OverlapArea(ToPixels(outer, screen));
                if (overlap > best)
                {
                    best = overlap;
                    target = screen;
                    chosen = true;
                }
            }

            if (!chosen)
            {
                var primaryIndex = ordered.FindIndex(screen => screen.Primary);
                target = ordered[primaryIndex >= 0 ? primaryIndex : 0];
                usedPrimaryFallback = true;
            }
        }

        var scaling = target.EffectiveScaling;
        var work = target.EffectiveWorkArea;
        var workX = work.X / scaling;
        var workY = work.Y / scaling;
        var workWidth = work.Width / scaling;
        var workHeight = work.Height / scaling;

        // O-2.4: shrink before positioning; the inner minimum keeps a legal
        // client rectangle even if a work area is smaller than the frame.
        var outerWidth = Math.Min(outer.Width, workWidth);
        var outerHeight = Math.Min(outer.Height, workHeight);
        var outerX = Math.Clamp(outer.X, workX, workX + workWidth - outerWidth);
        var outerY = Math.Clamp(outer.Y, workY, workY + workHeight - outerHeight);

        var placed = new DockBounds(
            Round(outerX),
            Round(outerY),
            Math.Max(1d, Round(outerWidth - frameDip.Horizontal)),
            Math.Max(1d, Round(outerHeight - frameDip.Vertical)));

        var moved = Math.Abs(placed.X - modelDip.X) > Tolerance
                    || Math.Abs(placed.Y - modelDip.Y) > Tolerance;
        var resized = Math.Abs(placed.Width - modelDip.Width) > Tolerance
                      || Math.Abs(placed.Height - modelDip.Height) > Tolerance;

        var reason = !moved && !resized
            ? DockPlacementReason.InsideWorkArea
            : usedPrimaryFallback
                ? DockPlacementReason.PrimaryFallback
                : DockPlacementReason.Clamped;

        return new DockScreenPlacement(placed, target, reason, moved, resized);
    }

    /// <summary>
    /// Declared capture conversion (ADR-011 §十一·二): the outer frame origin
    /// divided by the capturing window's scaling is the model position; the
    /// client size divided by the same scaling is the model size. The platform
    /// outer-rect size is deliberately not used as a model dimension.
    /// </summary>
    public static DockBounds CaptureClientBounds(
        DockPixelRect outerPixels,
        DockPixelRect clientPixels,
        double scaling)
    {
        var scale = double.IsFinite(scaling) && scaling > 0d ? scaling : 1d;
        var width = clientPixels.Width > 0 ? clientPixels.Width / scale : 0d;
        var height = clientPixels.Height > 0 ? clientPixels.Height / scale : 0d;
        return new DockBounds(
            outerPixels.X / scale,
            outerPixels.Y / scale,
            width,
            height);
    }

    /// <summary>
    /// Converts the platform's outer rectangle and the client rectangle (origin
    /// in screen pixels, size in pixels) into per-edge DIP frame margins.
    /// Returns <see cref="DockWindowFrame.None"/> when the rectangles cannot
    /// describe a real frame.
    /// </summary>
    public static DockWindowFrame FrameFromPlatformRects(
        DockPixelRect outerPixels,
        DockPixelRect clientPixels,
        double scaling)
    {
        var scale = double.IsFinite(scaling) && scaling > 0d ? scaling : 1d;
        if (outerPixels.Width <= 0 || outerPixels.Height <= 0
            || clientPixels.Width <= 0 || clientPixels.Height <= 0)
        {
            return DockWindowFrame.None;
        }

        var left = (clientPixels.X - outerPixels.X) / scale;
        var top = (clientPixels.Y - outerPixels.Y) / scale;
        var right = (outerPixels.Right - (clientPixels.X + clientPixels.Width)) / scale;
        var bottom = (outerPixels.Bottom - (clientPixels.Y + clientPixels.Height)) / scale;
        var frame = new DockWindowFrame(left, top, right, bottom).Normalized;
        return frame.Horizontal >= DockBounds.MaxExtent || frame.Vertical >= DockBounds.MaxExtent
            ? DockWindowFrame.None
            : frame;
    }

    private static List<DockScreenArea> OrderedScreens(IReadOnlyList<DockScreenArea> screens)
    {
        var ordered = screens.ToList();
        ordered.Sort(static (first, second) =>
        {
            var primary = second.Primary.CompareTo(first.Primary);
            if (primary != 0)
                return primary;

            var device = string.CompareOrdinal(first.Device, second.Device);
            if (device != 0)
                return device;

            var x = first.Bounds.X.CompareTo(second.Bounds.X);
            return x != 0 ? x : first.Bounds.Y.CompareTo(second.Bounds.Y);
        });

        return ordered;
    }

    private static DockPixelRect ToPixels(DockBounds outerDip, DockScreenArea screen)
    {
        var scaling = screen.EffectiveScaling;
        var left = (int)Math.Round(outerDip.X * scaling);
        var top = (int)Math.Round(outerDip.Y * scaling);
        var right = (int)Math.Round((outerDip.X + outerDip.Width) * scaling);
        var bottom = (int)Math.Round((outerDip.Y + outerDip.Height) * scaling);
        return new DockPixelRect(left, top, right - left, bottom - top);
    }

    private static double Round(double value)
        => Math.Round(value, 3, MidpointRounding.AwayFromZero);
}
