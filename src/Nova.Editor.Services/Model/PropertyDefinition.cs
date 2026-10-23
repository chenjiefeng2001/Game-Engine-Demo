namespace Nova.Editor.Services.Model;

public enum PropertyEditorKind
{
    Number,
    Text,
    Boolean,
    Choice,
}

/// <summary>
/// Editor-facing property metadata. It is not a reflection system: v0.2 keeps
/// the schema explicit so command validation and UI rendering are deterministic.
/// </summary>
public sealed record PropertyDefinition(
    string Key,
    string Label,
    PropertyEditorKind Kind,
    object? DefaultValue = null,
    double? Minimum = null,
    double? Maximum = null,
    double Step = 1,
    string Format = "0.###",
    IReadOnlyList<string>? Options = null)
{
    public bool TryCoerce(object? input, out object? value, out string? error)
    {
        value = null;
        error = null;

        switch (Kind)
        {
            case PropertyEditorKind.Number:
                {
                    double number;
                    if (input is double d) number = d;
                    else if (input is float f) number = f;
                    else if (input is int i) number = i;
                    else if (input is string s &&
                             double.TryParse(s, System.Globalization.NumberStyles.Float,
                                 System.Globalization.CultureInfo.InvariantCulture, out var parsed))
                        number = parsed;
                    else
                    {
                        error = $"'{Label}' expects a number.";
                        return false;
                    }

                    if (double.IsNaN(number) || double.IsInfinity(number))
                    {
                        error = $"'{Label}' must be finite.";
                        return false;
                    }

                    if (Minimum.HasValue) number = Math.Max(number, Minimum.Value);
                    if (Maximum.HasValue) number = Math.Min(number, Maximum.Value);
                    value = number;
                    return true;
                }
            case PropertyEditorKind.Text:
                {
                    var text = input?.ToString() ?? string.Empty;
                    if (text.Length > 512)
                    {
                        error = $"'{Label}' is limited to 512 characters.";
                        return false;
                    }
                    value = text;
                    return true;
                }
            case PropertyEditorKind.Boolean:
                {
                    if (input is bool boolean)
                    {
                        value = boolean;
                        return true;
                    }
                    if (bool.TryParse(input?.ToString(), out var parsedBool))
                    {
                        value = parsedBool;
                        return true;
                    }
                    error = $"'{Label}' expects true/false.";
                    return false;
                }
            case PropertyEditorKind.Choice:
                {
                    var text = input?.ToString() ?? string.Empty;
                    if (Options is null || Options.Count == 0 || Options.Contains(text, StringComparer.Ordinal))
                    {
                        value = text;
                        return true;
                    }
                    error = $"'{Label}' must be one of: {string.Join(", ", Options)}.";
                    return false;
                }
            default:
                error = $"Unsupported editor kind '{Kind}'.";
                return false;
        }
    }
}
