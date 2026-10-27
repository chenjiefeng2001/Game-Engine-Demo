using System;

namespace AvaloniaEditor.Services;

/// <summary>一次 UI 点击映射到 canonical pick 坐标的结果。</summary>
public enum PickMapStatus
{
    /// <summary>映射成功，<c>X/Y</c> 是 canonical top-left 像素坐标。</summary>
    Ok,
    /// <summary>点在已渲染帧之外。**显式拒绝**（E-C3/E-C4），绝不 clamp 成边缘像素。</summary>
    OutsideFrame,
    /// <summary>坐标含非有限值（NaN / ±Inf）。</summary>
    InvalidCoordinate,
    /// <summary>
    /// 显示变换不可用（帧尺寸/显示尺寸非正、缩放非有限或为零）。
    /// **fail-closed**：拒绝 pick，而不是按猜测的 offset/scale 映射。
    /// </summary>
    GeometryMismatch,
}

/// <summary>canonical pick 坐标（presentation frame 的 top-left 像素）。</summary>
public readonly record struct PickPoint(int X, int Y)
{
    public override string ToString() => $"({X},{Y})";
}

/// <summary>
/// Slice D/E **E-C3 / E-C4**：Avalonia 指针坐标 → canonical pick 像素坐标的**唯一**映射。
///
/// 契约：
///   - canonical = presented frame 的像素坐标，**top-left 原点**，半开区间
///     <c>[0, frameWidth) × [0, frameHeight)</c>；
///   - **帧尺寸来自该帧本身**（packet header），不是编译期常量；
///   - 映射依据**实际显示变换**：调用方传入"图像在控件坐标系中的完整矩形"
///     （<paramref name="displayedX"/>…），mapper 据此推导每个轴的缩放比；
///   - 越界**拒绝**，绝不 clamp；非有限值、非正尺寸、零/负缩放一律拒绝。
///
/// 关于"裁剪"与"缩放"的区别（E-C5，D Step 2 已踩过一次）：
///   控件比帧窄、`Stretch=None` 时，图像被裁剪但仍然是 **1:1 且原点锚定**，
///   因此调用方传入的矩形是 `(0, 0, frameW, frameH)` —— 与"缩放"完全无关。
///   **禁止**用"控件尺寸 == 帧尺寸"来推断是否缩放。
/// </summary>
public static class ViewportPickMapper
{
    /// <summary>
    /// 把指针位置映射为 canonical 像素。
    /// </summary>
    /// <param name="pointerX">指针 X（相对承载控件的本地坐标）。</param>
    /// <param name="pointerY">指针 Y（同上）。</param>
    /// <param name="displayedX">完整图像矩形在控件坐标系中的 X 原点（可为负：UniformToFill 溢出）。</param>
    /// <param name="displayedY">同上，Y 原点。</param>
    /// <param name="displayedWidth">图像实际绘制宽度（缩放时与帧宽不同）。</param>
    /// <param name="displayedHeight">图像实际绘制高度。</param>
    /// <param name="frameWidth">该帧的真实宽度（来自 packet header）。</param>
    /// <param name="frameHeight">该帧的真实高度。</param>
    /// <param name="point">canonical 像素；仅 <see cref="PickMapStatus.Ok"/> 时有效。</param>
    public static PickMapStatus TryMap(
        double pointerX, double pointerY,
        double displayedX, double displayedY,
        double displayedWidth, double displayedHeight,
        int frameWidth, int frameHeight,
        out PickPoint point)
    {
        point = default;

        if (frameWidth <= 0 || frameHeight <= 0)
            return PickMapStatus.GeometryMismatch;
        if (!double.IsFinite(displayedX) || !double.IsFinite(displayedY)
            || !double.IsFinite(displayedWidth) || !double.IsFinite(displayedHeight))
            return PickMapStatus.GeometryMismatch;
        if (displayedWidth <= 0.0 || displayedHeight <= 0.0)
            return PickMapStatus.GeometryMismatch;

        if (!double.IsFinite(pointerX) || !double.IsFinite(pointerY))
            return PickMapStatus.InvalidCoordinate;

        double scaleX = displayedWidth / frameWidth;
        double scaleY = displayedHeight / frameHeight;
        if (!(scaleX > 0.0) || !(scaleY > 0.0)
            || !double.IsFinite(scaleX) || !double.IsFinite(scaleY))
            return PickMapStatus.GeometryMismatch;

        // floor（不是 round）：像素 p 覆盖 [p, p+1)，与半开区间语义一致。
        // 用 round 会让 p+0.5 归到 p+1，使像素边界上的点击落到相邻像素。
        double frameX = (pointerX - displayedX) / scaleX;
        double frameY = (pointerY - displayedY) / scaleY;
        if (!double.IsFinite(frameX) || !double.IsFinite(frameY))
            return PickMapStatus.InvalidCoordinate;

        int x = (int)Math.Floor(frameX);
        int y = (int)Math.Floor(frameY);

        // 越界显式拒绝，绝不 clamp（clamp 会把越界点击变成边缘假命中）。
        if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight)
            return PickMapStatus.OutsideFrame;

        point = new PickPoint(x, y);
        return PickMapStatus.Ok;
    }
}
