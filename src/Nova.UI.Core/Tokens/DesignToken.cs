using System.Globalization;

namespace Nova.UI.Core.Tokens;

public enum TokenType
{
    Color,
    Dimension,
    FontFamily,
    FontSize,
    FontWeight,
    Number,
    Duration,
    String,
}

/// <summary>Single design decision. Values stay textual so they can be serialized and diffed.</summary>
public sealed record DesignToken
{
    public DesignToken(string key, TokenType type, string value)
    {
        if (string.IsNullOrWhiteSpace(key)) throw new ArgumentException("Token key is required.", nameof(key));
        if (string.IsNullOrWhiteSpace(value)) throw new ArgumentException("Token value is required.", nameof(value));

        Key = key;
        Type = type;
        Value = value;
    }

    public string Key { get; }
    public TokenType Type { get; }
    public string Value { get; }
}

public readonly record struct RgbaColor(byte A, byte R, byte G, byte B)
{
    public static bool TryParse(string? text, out RgbaColor color)
    {
        color = default;
        if (string.IsNullOrWhiteSpace(text)) return false;

        var value = text.Trim();
        if (value.StartsWith('#')) value = value[1..];
        if (value.Length == 6) value = "FF" + value;
        if (value.Length != 8) return false;

        if (!uint.TryParse(value, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var packed))
            return false;

        color = new RgbaColor(
            (byte)((packed >> 24) & 0xFF),
            (byte)((packed >> 16) & 0xFF),
            (byte)((packed >> 8) & 0xFF),
            (byte)(packed & 0xFF));
        return true;
    }

    public string ToHex() => $"#{A:X2}{R:X2}{G:X2}{B:X2}";
}
