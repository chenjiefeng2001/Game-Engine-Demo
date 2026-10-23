using System.Collections.Immutable;
using System.Globalization;

namespace Nova.UI.Core.Tokens;

/// <summary>
/// Immutable token set. A token can be looked up by key and parsed into the
/// strongly typed value the host needs (color, thickness, font size, ...).
/// </summary>
public sealed class DesignTokenSet
{
    private readonly ImmutableDictionary<string, DesignToken> _tokens;

    public DesignTokenSet(string id, IEnumerable<DesignToken> tokens)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        Id = id;

        var builder = ImmutableDictionary.CreateBuilder<string, DesignToken>(StringComparer.Ordinal);
        foreach (var token in tokens)
        {
            if (!builder.TryAdd(token.Key, token))
                throw new ArgumentException($"Duplicate design token '{token.Key}'.", nameof(tokens));
        }

        _tokens = builder.ToImmutable();
    }

    public string Id { get; }

    public IReadOnlyDictionary<string, DesignToken> Tokens => _tokens;

    public bool TryGet(string key, out DesignToken token) => _tokens.TryGetValue(key, out token!);

    public DesignToken Get(string key)
        => _tokens.TryGetValue(key, out var token)
            ? token
            : throw new KeyNotFoundException($"Design token '{key}' is missing from '{Id}'.");

    public string GetString(string key) => Get(key).Value;

    public double GetNumber(string key)
    {
        var value = Get(key).Value;
        if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
            throw new FormatException($"Token '{key}' is not numeric: '{value}'.");
        return number;
    }

    public TimeSpan GetDuration(string key)
        => TimeSpan.FromMilliseconds(GetNumber(key));

    public RgbaColor GetColor(string key)
        => RgbaColor.TryParse(Get(key).Value, out var color)
            ? color
            : throw new FormatException($"Token '{key}' is not a color: '{Get(key).Value}'.");

    public FontWeightValue GetFontWeight(string key)
        => Enum.TryParse<FontWeightValue>(Get(key).Value, ignoreCase: true, out var weight)
            ? weight
            : throw new FormatException($"Token '{key}' is not a font weight: '{Get(key).Value}'.");

    /// <summary>Validates required keys and the syntax of each token type.</summary>
    public IReadOnlyList<string> Validate(IEnumerable<string> requiredKeys)
    {
        var errors = new List<string>();

        foreach (var key in requiredKeys)
        {
            if (!_tokens.ContainsKey(key))
                errors.Add($"missing token '{key}'");
        }

        foreach (var token in _tokens.Values)
        {
            switch (token.Type)
            {
                case TokenType.Color when !RgbaColor.TryParse(token.Value, out _):
                    errors.Add($"token '{token.Key}' has invalid color '{token.Value}'");
                    break;
                case TokenType.Dimension:
                case TokenType.FontSize:
                case TokenType.Number:
                case TokenType.Duration:
                    if (!double.TryParse(token.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var n) ||
                        double.IsNaN(n) || double.IsInfinity(n))
                    {
                        errors.Add($"token '{token.Key}' has invalid number '{token.Value}'");
                    }
                    break;
                case TokenType.FontWeight when !Enum.TryParse<FontWeightValue>(token.Value, true, out _):
                    errors.Add($"token '{token.Key}' has invalid weight '{token.Value}'");
                    break;
            }
        }

        return errors;
    }
}

/// <summary>Framework-neutral weight names, mapped to Avalonia.FontWeight by the adapter.</summary>
public enum FontWeightValue
{
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900,
}
