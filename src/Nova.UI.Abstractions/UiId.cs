using System.Globalization;

namespace Nova.UI;

/// <summary>
/// Stable identity for a UI element, command, panel, or model projection.
/// IDs are deliberately opaque strings: they can come from a scene document,
/// a saved layout, or a command table. Never derive identity from an array index.
/// </summary>
public readonly record struct UiId
{
    public static UiId Empty => default;

    public UiId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new ArgumentException("A UiId must be a non-empty string.", nameof(value));
        Value = value.Trim();
    }

    public string Value { get; }

    public bool IsEmpty => string.IsNullOrEmpty(Value);

    public static UiId For(string scope, string localId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(scope);
        ArgumentException.ThrowIfNullOrWhiteSpace(localId);
        return new UiId($"{scope}.{localId}");
    }

    public static bool TryParse(string? value, out UiId id)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            id = Empty;
            return false;
        }

        id = new UiId(value);
        return true;
    }

    public override string ToString() => Value ?? string.Empty;

    public static implicit operator string(UiId id) => id.Value ?? string.Empty;

    public static explicit operator UiId(string value) => new(value);

    public static string CreateStable(string scope, long counter)
        => string.Create(CultureInfo.InvariantCulture, $"{scope}.{counter}");
}
