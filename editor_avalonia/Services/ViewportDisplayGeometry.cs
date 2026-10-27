using System;
using Avalonia;
using Avalonia.Media;

namespace AvaloniaEditor.Services;

/// <summary>
/// 图像在承载控件坐标系中的**实际显示矩形**（Slice E / E-C3 / E-C4）。
///
/// 契约：
///   - 原点固定在承载控件的左上角（生产 XAML 是 `HorizontalAlignment=Left`
///     `VerticalAlignment=Top`，因此 destRect 的原点就是 (0,0)）；
///   - 矩形描述的是**图像被绘制出来的那块区域**，不是控件的布局框：
///     控件比图像大 ⇒ 图像只占左上角一块（其余是空白）；
///     控件比图像小 ⇒ 图像被**裁剪**（纯裁剪，不是缩放）；
///   - 缩放（Stretch != None）时矩形可以大于控件（UniformToFill 会溢出并被裁掉）。
/// </summary>
public readonly record struct DisplayRect(double X, double Y, double Width, double Height)
{
    public bool IsFinite =>
        double.IsFinite(X) && double.IsFinite(Y) &&
        double.IsFinite(Width) && double.IsFinite(Height);

    public override string ToString()
        => $"({X:0.##},{Y:0.##}) {Width:0.##}x{Height:0.##}";
}

/// <summary>
/// 计算"图像在控件坐标系中的显示矩形"。
///
/// **为什么它不属于 mapper**：mapper 只做"控件坐标 → 帧像素"的纯算术，
/// 它**不猜呈现模式**（E-C5）。呈现模式是 View 的事实，由这里翻译成矩形，
/// 再交给 mapper（E-C3/E-C4）。
///
/// **裁剪 ≠ 缩放**（D Step 2 已经踩过一次）：
///   - `Stretch.None`（生产模式）⇒ 矩形 = 图像的**自然尺寸**，与控件尺寸无关。
///     控件比图像窄 ⇒ 图像被裁剪，但可见区仍然逐像素对应（比例仍是 1）。
///   - 因此**禁止**用"控件尺寸 == 帧尺寸"来推断是否缩放；
///     也不允许用"控件尺寸 != 帧尺寸"来推断裁剪（那只是裁剪，不是变换）。
/// </summary>
public static class ViewportDisplayGeometry
{
    /// <summary>
    /// 计算显示矩形。
    /// </summary>
    /// <param name="controlBounds">承载控件的布局框（用于缩放模式的可用尺寸）。</param>
    /// <param name="sourcePixelSize">图像的像素尺寸（生产路径 = 该帧的真实宽高）。</param>
    /// <param name="stretch">呈现模式。</param>
    /// <param name="rect">显示矩形；失败时为 default。</param>
    /// <returns>false = 输入不可用（尺寸非正 / 非有限）⇒ 调用方必须 fail-closed。</returns>
    public static bool TryCompute(
        Rect controlBounds,
        Size sourcePixelSize,
        Stretch stretch,
        out DisplayRect rect)
    {
        rect = default;

        double srcW = sourcePixelSize.Width;
        double srcH = sourcePixelSize.Height;
        if (!double.IsFinite(srcW) || !double.IsFinite(srcH) || srcW <= 0.0 || srcH <= 0.0)
            return false;

        double boundsW = controlBounds.Width;
        double boundsH = controlBounds.Height;
        if (!double.IsFinite(boundsW) || !double.IsFinite(boundsH))
            return false;

        switch (stretch)
        {
            case Stretch.None:
                // 自然尺寸、原点锚定。控件更小 ⇒ 被裁剪（**不是**缩放）。
                rect = new DisplayRect(0.0, 0.0, srcW, srcH);
                return true;

            case Stretch.Fill:
                if (boundsW <= 0.0 || boundsH <= 0.0) return false;
                rect = new DisplayRect(0.0, 0.0, boundsW, boundsH);
                return true;

            case Stretch.Uniform:
            {
                if (boundsW <= 0.0 || boundsH <= 0.0) return false;
                double scale = Math.Min(boundsW / srcW, boundsH / srcH);
                if (!(scale > 0.0) || !double.IsFinite(scale)) return false;
                rect = new DisplayRect(0.0, 0.0, srcW * scale, srcH * scale);
                return true;
            }

            case Stretch.UniformToFill:
            {
                if (boundsW <= 0.0 || boundsH <= 0.0) return false;
                double scale = Math.Max(boundsW / srcW, boundsH / srcH);
                if (!(scale > 0.0) || !double.IsFinite(scale)) return false;
                // 溢出部分被裁掉；原点仍在 (0,0)（Left/Top 锚定）。
                rect = new DisplayRect(0.0, 0.0, srcW * scale, srcH * scale);
                return true;
            }

            default:
                return false;
        }
    }
}
