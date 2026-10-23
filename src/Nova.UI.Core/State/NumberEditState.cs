using System.Globalization;

namespace Nova.UI.Core.State;

/// <summary>
/// Intermediate editing state for numeric text fields.
///
/// Typing "1." or "-" is valid *while editing* even though it is not yet a number.
/// The model is only updated on commit (Enter, blur, spinner). This class is pure
/// and testable; the Avalonia control is a thin adapter over it.
/// </summary>
public sealed class NumberEditState
{
    private string _text = string.Empty;
    private double? _value;
    private double? _textValue;
    private bool _dirty;

    public NumberEditState(string format = "0.###")
    {
        Format = string.IsNullOrWhiteSpace(format) ? "0.###" : format;
    }

    public string Format { get; }

    public string Text => _text;

    /// <summary>True when the current text can be committed.</summary>
    public bool HasValidValue => _textValue.HasValue;

    /// <summary>The last committed value. Intermediate invalid text does not change it.</summary>
    public double? Value => _value;

    /// <summary>True when text differs from the last committed value.</summary>
    public bool IsDirty => _dirty;

    public void SetValue(double? value)
    {
        _value = value;
        _textValue = value;
        _text = FormatValue(value);
        _dirty = false;
    }

    /// <summary>Accepts any intermediate text; parsing is best-effort and never throws.</summary>
    public void SetText(string? text)
    {
        _text = text ?? string.Empty;
        _textValue = TryParse(_text, out var parsed) ? parsed : null;
        _dirty = true;
    }

    /// <summary>
    /// Commits the current text, clamping to the supplied range. Returns false for
    /// empty/invalid text or NaN/Infinity. On failure the text is preserved so the
    /// user can correct it.
    /// </summary>
    public bool TryCommit(double? minimum, double? maximum, out double value)
    {
        value = 0;
        if (!TryParse(_text, out var parsed))
            return false;

        if (minimum.HasValue) parsed = Math.Max(parsed, minimum.Value);
        if (maximum.HasValue) parsed = Math.Min(parsed, maximum.Value);

        _value = parsed;
        _textValue = parsed;
        _text = FormatValue(parsed);
        _dirty = false;
        value = parsed;
        return true;
    }

    public bool TryStep(double delta, double? minimum, double? maximum, out double value)
    {
        value = 0;
        if (!double.IsFinite(delta)) return false;

        var origin = _textValue ?? _value;
        if (!origin.HasValue)
            return false;

        var stepped = origin.Value + delta;
        if (!double.IsFinite(stepped)) return false;

        if (minimum.HasValue) stepped = Math.Max(stepped, minimum.Value);
        if (maximum.HasValue) stepped = Math.Min(stepped, maximum.Value);
        if (!double.IsFinite(stepped)) return false;

        SetValue(stepped);
        value = stepped;
        return true;
    }

    public void Revert()
    {
        _textValue = _value;
        _text = FormatValue(_value);
        _dirty = false;
    }

    private string FormatValue(double? value)
        => value.HasValue ? value.Value.ToString(Format, CultureInfo.InvariantCulture) : string.Empty;

    private static bool TryParse(string text, out double value)
    {
        value = 0;
        if (string.IsNullOrWhiteSpace(text)) return false;
        if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value)) return false;
        return !double.IsNaN(value) && !double.IsInfinity(value);
    }
}
