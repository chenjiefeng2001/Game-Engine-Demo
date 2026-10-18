using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;   // Avalonia 11: PixelFormat / AlphaFormat live here
using Avalonia.Threading;
using AvaloniaEditor.Native;

namespace AvaloniaEditor.Services;

/// <summary>
/// UI 侧"已显示帧"回执。**只用于 gate / 诊断**，不是产品状态。
///
/// 它的存在是为了让 U3 能证明的是"control 当前显示的 frame 是 N"，
/// 而不是"代码曾经创建过一张 bitmap"。
/// </summary>
public sealed class ViewportDisplayReceipt
{
    public ulong DisplayedFrameId { get; init; }
    public int Width { get; init; }
    public int Height { get; init; }

    /// <summary>displayed bitmap 中的非 clear 像素数（U5 的判据）。</summary>
    public int NonClearPixels { get; init; }

    /// <summary>该回执对应的 WriteableBitmap 实例（U1：像素必须来自同一个 packet）。</summary>
    public WriteableBitmap Bitmap { get; init; } = null!;

    /// <summary>该回执是否真的绑定到了 production ViewportImage（U2）。</summary>
    public bool BoundToViewportImage { get; init; }

    /// <summary>bitmap 行序相对 packet 是否被上下翻转（GL 原生是左下角起，Avalonia 是左上角起）。</summary>
    public bool VerticallyFlipped { get; init; }

    /// <summary>presentation FBO 的 clear color（0.05,0.05,0.07,1.0）→ 13/13/18。</summary>
    public const byte ClearR = 13, ClearG = 13, ClearB = 18;

    public override string ToString()
        => $"ViewportDisplayReceipt(frame={DisplayedFrameId}, {Width}x{Height}, " +
           $"nonClear={NonClearPixels}, bound={BoundToViewportImage}, flipped={VerticallyFlipped})";
}

/// <summary>
/// HRC-3 P2 UI Proof：把**已经取到的** <see cref="PresentationFrame"/> 呈现在
/// production <c>ViewportImage</c> 上。
///
/// 硬边界（本类严格遵守）：
///   - **不新增 ABI**：只调用已存在的 P2 Fetch 与唯一 pump 入口；
///   - **不调用 GL / glReadPixels / FBO**：本类完全不知道 GL 的存在；
///   - **不复用 legacy ViewportPanel 路径**：不碰 ViewportTick / GetViewportPixels；
///   - **不新增第二个 pump**：pump 由 <see cref="EditorHostService.RuntimeTick"/> 单一入口驱动；
///   - **不把上一帧 bitmap 当成新帧**：没有新帧就**完全不碰**控件；
///   - **不自行生成 frame_id**：一律用 packet 里的 frame_id；
///   - **不回退**：frame_id 必须严格递增，否则拒绝呈现（U4）。
/// </summary>
public sealed class ViewportFramePresenter
{
    private WriteableBitmap? _bitmap;
    private ulong _displayedFrameId;

    public ViewportDisplayReceipt? LastDisplayed { get; private set; }

    public ulong DisplayedFrameId => _displayedFrameId;

    /// <summary>GL 原生行序（左下角起）与 Avalonia WriteableBitmap（��上角起）相反，故需要翻转。</summary>
    public const bool VerticallyFlipped = true;

    /// <summary>
    /// 从 host 消费"下一个新帧"并呈现。
    /// 返回 true 表示确实呈现了一帧**新的** frame；false 表示没有新帧（控件保持原样，绝不假装）。
    /// </summary>
    public bool PresentLatestFrame(EditorHostService host)
    {
        FrameFetchResult fetched = host.TryFetchPresentationFrame();
        if (!fetched.HasNewFrame || fetched.Frame is null)
        {
            // 没有新帧：**一个字节都不动**，不复用旧 bitmap 冒充新帧。
            return false;
        }
        return Present(fetched.Frame);
    }

    /// <summary>
    /// 呈现一个已取到的帧。**回退保护**：frame_id 必须严格大于当前已显示的 frame_id，
    /// 否则拒绝（U4：迟到的旧帧不得覆盖新帧）。
    /// </summary>
    public bool Present(PresentationFrame frame)
    {
        if (frame is null) throw new ArgumentNullException(nameof(frame));
        Dispatcher.UIThread.VerifyAccess();   // 呈现必须在 UI 线程

        if (frame.FrameId <= _displayedFrameId)
        {
            // 乱序/回退：拒绝呈现，绝不让 UI 退回旧帧。
            return false;
        }

        int w = frame.Width;
        int h = frame.Height;
        byte[] rgba = frame.Rgba;

        var image = ViewportImage;
        if (image is null) throw new InvalidOperationException("production ViewportImage is not bound");

        // 固定 800x600；D4 Resize 仍 Deferred，因此这里不处理尺寸协商。
        if (_bitmap is null || _bitmap.PixelSize.Width != w || _bitmap.PixelSize.Height != h)
        {
            _bitmap = new WriteableBitmap(
                new PixelSize(w, h),
                new Vector(96, 96),
                PixelFormat.Rgba8888,
                AlphaFormat.Unpremul);
        }

        // GL 原生是"左下角起"，Avalonia 的 WriteableBitmap 是"左上角起" ⇒ 逐行翻转。
        using (var fb = _bitmap.Lock())
        {
            for (int y = 0; y < h; ++y)
            {
                int srcRow = VerticallyFlipped ? (h - 1 - y) : y;
                int srcOffset = srcRow * w * 4;
                int dstOffset = y * fb.RowBytes;
                Marshal.Copy(rgba, srcOffset, fb.Address + dstOffset, w * 4);
            }
        }

        // U2：真正绑定 production 控件，而不是 legacy placeholder / 旧 bitmap。
        image.Source = _bitmap;
        image.InvalidateVisual();
        if (ViewportPlaceholder is not null) ViewportPlaceholder.IsVisible = false;

        _displayedFrameId = frame.FrameId;
        LastDisplayed = new ViewportDisplayReceipt
        {
            DisplayedFrameId = frame.FrameId,
            Width = w,
            Height = h,
            NonClearPixels = frame.CountNonClearPixels(
                ViewportDisplayReceipt.ClearR, ViewportDisplayReceipt.ClearG,
                ViewportDisplayReceipt.ClearB),
            Bitmap = _bitmap,
            BoundToViewportImage = ReferenceEquals(image.Source, _bitmap),
            VerticallyFlipped = VerticallyFlipped,
        };
        return true;
    }

    // ── 由 MainWindow 在构造时注入的 production 控件（不自己 FindControl 以便 gate 断言）──
    public Image? ViewportImage { get; set; }
    public TextBlock? ViewportPlaceholder { get; set; }
}
