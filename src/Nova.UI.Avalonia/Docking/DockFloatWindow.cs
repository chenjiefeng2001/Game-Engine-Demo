using System.ComponentModel;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

public sealed class DockFloatWindowClosingEventArgs : CancelEventArgs
{
}

/// <summary>
/// OS window adapter used by <see cref="DockWindowRootHost"/>. The host owns
/// lifecycle semantics; the adapter only creates, shows, moves and closes the
/// platform window.
/// </summary>
public interface IDockFloatWindow : IDisposable
{
    Control? Content { get; set; }

    string Title { get; set; }

    bool IsVisible { get; }

    bool IsMaximized { get; }

    DockBounds Bounds { get; }

    /// <summary>Current monitor set; empty when the platform cannot answer.</summary>
    IReadOnlyList<DockScreenArea> Screens { get; }

    /// <summary>Non-client thickness in DIP; none when the platform cannot answer.</summary>
    DockWindowFrame FrameMargins { get; }

    event EventHandler<DockFloatWindowClosingEventArgs>? Closing;

    event EventHandler? BoundsChanged;

    /// <summary>
    /// Shows the window. <paramref name="screenScaling"/> is the scaling of the
    /// monitor the window is being placed on; zero means "use the window's own
    /// current scaling" (ADR-011 O-3.2).
    /// </summary>
    void Show(DockBounds bounds, double screenScaling = 0d);

    void ApplyBounds(DockBounds bounds, bool maximized, double screenScaling = 0d);

    void Close();
}

/// <summary>
/// Real Avalonia top-level window. Geometry is model DIP: the position is the
/// outer frame origin divided by the placing screen's scaling, the size is the
/// real client area (ADR-011 §十一·二).
/// </summary>
public sealed class AvaloniaFloatWindow : IDockFloatWindow
{
    private readonly Window _window;
    private readonly Window? _owner;
    private bool _suppressBounds;
    private bool _closeApproved;
    private bool _disposed;

    public AvaloniaFloatWindow(Window? owner, string title)
    {
        _owner = owner;
        _window = new Window
        {
            Title = title,
            ShowInTaskbar = false,
            WindowStartupLocation = WindowStartupLocation.Manual,
            MinWidth = 240,
            MinHeight = 160,
            CanResize = true,
        };

        _window.Closing += OnClosing;
        _window.PositionChanged += OnBoundsChanged;
        _window.SizeChanged += OnBoundsChanged;
        _window.PropertyChanged += OnPropertyChanged;
    }

    public Control? Content
    {
        get => _window.Content as Control;
        set => _window.Content = value;
    }

    public string Title
    {
        get => _window.Title ?? string.Empty;
        set => _window.Title = value;
    }

    public bool IsVisible => _window.IsVisible;

    /// <summary>
    /// Avalonia updates <see cref="Window.WindowState"/> after the platform has
    /// already moved and resized the window. The intermediate position event
    /// must still be classified as maximized, otherwise it would overwrite the
    /// normal restore bounds with the maximized geometry.
    /// </summary>
    public bool IsMaximized
        => _window.WindowState == WindowState.Maximized || IsPlatformZoomed();

    public DockBounds Bounds => CaptureBounds();

    public IReadOnlyList<DockScreenArea> Screens
    {
        get
        {
            try
            {
                var screens = _window.Screens;
                if (screens is null)
                    return Array.Empty<DockScreenArea>();

                return screens.All
                    .Select(screen => new DockScreenArea(
                        screen.DisplayName ?? string.Empty,
                        ToPixelRect(screen.Bounds),
                        ToPixelRect(screen.WorkingArea),
                        screen.Scaling,
                        screen.IsPrimary))
                    .ToArray();
            }
            catch
            {
                // A window without a platform implementation cannot answer yet.
                return Array.Empty<DockScreenArea>();
            }
        }
    }

    public DockWindowFrame FrameMargins => MeasureFrameMargins();

    public event EventHandler<DockFloatWindowClosingEventArgs>? Closing;

    public event EventHandler? BoundsChanged;

    public void Show(DockBounds bounds, double screenScaling = 0d)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ApplyBoundsCore(bounds, maximized: false, screenScaling);
        if (_owner is not null && _owner.IsVisible)
            _window.Show(_owner);
        else
            _window.Show();
    }

    public void ApplyBounds(DockBounds bounds, bool maximized, double screenScaling = 0d)
    {
        if (_disposed)
            return;

        ApplyBoundsCore(bounds, maximized, screenScaling);
    }

    public void Close()
    {
        if (_disposed)
            return;

        _closeApproved = true;
        if (_window.IsVisible)
            _window.Close();
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        _closeApproved = true;
        _window.Closing -= OnClosing;
        _window.PositionChanged -= OnBoundsChanged;
        _window.SizeChanged -= OnBoundsChanged;
        _window.PropertyChanged -= OnPropertyChanged;
        if (_window.IsVisible)
            _window.Close();
        _window.Content = null;
    }

    private void ApplyBoundsCore(DockBounds bounds, bool maximized, double screenScaling = 0d)
    {
        _suppressBounds = true;
        try
        {
            _window.Width = bounds.Width;
            _window.Height = bounds.Height;
            _window.Position = ToPixelPoint(bounds, screenScaling);
            _window.WindowState = maximized ? WindowState.Maximized : WindowState.Normal;
        }
        finally
        {
            _suppressBounds = false;
        }
    }

    private DockBounds CaptureBounds()
    {
        var scaling = _window.RenderScaling > 0 ? _window.RenderScaling : 1d;

        // The real client area, not the styled Width/Height: the platform may
        // have capped the window, in which case the style property is stale.
        var client = _window.ClientSize;
        var width = client.Width > 0 ? client.Width : FallbackExtent(_window.Width);
        var height = client.Height > 0 ? client.Height : FallbackExtent(_window.Height);
        return new DockBounds(
            _window.Position.X / scaling,
            _window.Position.Y / scaling,
            width,
            height);
    }

    private static double FallbackExtent(double value)
        => double.IsFinite(value) && value > 0 ? value : 0d;

    private PixelPoint ToPixelPoint(DockBounds bounds, double screenScaling)
    {
        var windowScaling = _window.RenderScaling > 0 ? _window.RenderScaling : 1d;
        var scaling = double.IsFinite(screenScaling) && screenScaling > 0d
            ? screenScaling
            : windowScaling;
        return new PixelPoint(
            (int)Math.Round(bounds.X * scaling),
            (int)Math.Round(bounds.Y * scaling));
    }

    private DockWindowFrame MeasureFrameMargins()
    {
        if (!OperatingSystem.IsWindows())
            return DockWindowFrame.None;

        try
        {
            var handle = _window.TryGetPlatformHandle();
            if (handle is null)
                return DockWindowFrame.None;

            if (!GetWindowRect(handle.Handle, out var outer)
                || !GetClientRect(handle.Handle, out var client))
            {
                return DockWindowFrame.None;
            }

            var origin = new WinPoint();
            if (!ClientToScreen(handle.Handle, ref origin))
                return DockWindowFrame.None;

            var scaling = _window.RenderScaling > 0 ? _window.RenderScaling : 1d;
            return DockScreenGeometry.FrameFromPlatformRects(
                new DockPixelRect(
                    outer.Left,
                    outer.Top,
                    outer.Right - outer.Left,
                    outer.Bottom - outer.Top),
                new DockPixelRect(
                    origin.X,
                    origin.Y,
                    client.Right - client.Left,
                    client.Bottom - client.Top),
                scaling);
        }
        catch
        {
            return DockWindowFrame.None;
        }
    }

    private static DockPixelRect ToPixelRect(PixelRect rect)
        => new(rect.X, rect.Y, rect.Width, rect.Height);

    private void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (_disposed || _closeApproved)
            return;

        var args = new DockFloatWindowClosingEventArgs();
        Closing?.Invoke(this, args);
        e.Cancel = args.Cancel;
    }

    private void OnBoundsChanged(object? sender, EventArgs e)
    {
        if (_disposed || _suppressBounds || !_window.IsVisible)
            return;

        BoundsChanged?.Invoke(this, EventArgs.Empty);
    }

    private void OnPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == Window.WindowStateProperty)
            OnBoundsChanged(sender, EventArgs.Empty);
    }

    private bool IsPlatformZoomed()
    {
        if (!OperatingSystem.IsWindows())
            return false;

        var handle = _window.TryGetPlatformHandle();
        return handle is not null && IsZoomed(handle.Handle);
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsZoomed(IntPtr handle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr handle, out WinRect rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetClientRect(IntPtr handle, out WinRect rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClientToScreen(IntPtr handle, ref WinPoint point);

    [StructLayout(LayoutKind.Sequential)]
    private struct WinRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WinPoint
    {
        public int X;
        public int Y;
    }
}
