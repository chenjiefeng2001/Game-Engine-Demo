namespace Nova.UI.Core.Docking;

/// <summary>
/// Floating-root geometry in logical (DIP) coordinates. The model validates
/// numeric values only; display clamping belongs to the host adapter.
/// </summary>
public readonly record struct DockBounds(double X, double Y, double Width, double Height)
{
    public const double MaxCoordinate = 32768d;
    public const double MaxExtent = 32768d;

    public bool IsValid =>
        double.IsFinite(X) && X >= -MaxCoordinate && X <= MaxCoordinate
        && double.IsFinite(Y) && Y >= -MaxCoordinate && Y <= MaxCoordinate
        && double.IsFinite(Width) && Width > 0d && Width <= MaxExtent
        && double.IsFinite(Height) && Height > 0d && Height <= MaxExtent;
}
