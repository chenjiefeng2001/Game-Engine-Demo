using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Interactivity;
using Nova.UI.Core.State;

namespace Nova.UI.Controls;

/// <summary>
/// Numeric field with an intermediate editing state. Invalid text is preserved
/// while typing; the model value only changes on Enter/blur/spinner.
/// </summary>
public class NovaNumberField : TextBox
{
    public static readonly StyledProperty<double?> ValueProperty =
        AvaloniaProperty.Register<NovaNumberField, double?>(
            nameof(Value), defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<double?> MinimumProperty =
        AvaloniaProperty.Register<NovaNumberField, double?>(nameof(Minimum));

    public static readonly StyledProperty<double?> MaximumProperty =
        AvaloniaProperty.Register<NovaNumberField, double?>(nameof(Maximum));

    public static readonly StyledProperty<double> StepProperty =
        AvaloniaProperty.Register<NovaNumberField, double>(nameof(Step), 1d);

    public static readonly StyledProperty<string> FormatProperty =
        AvaloniaProperty.Register<NovaNumberField, string>(nameof(Format), "0.###");

    private NumberEditState _edit;
    private bool _updating;

    public NovaNumberField()
    {
        _edit = new NumberEditState(Format);
        Classes.Add(NovaControlClasses.NumberField);
        TextAlignment = TextAlignment.Right;
    }

    protected override Type StyleKeyOverride => typeof(TextBox);

    public double? Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public double? Minimum
    {
        get => GetValue(MinimumProperty);
        set => SetValue(MinimumProperty, value);
    }

    public double? Maximum
    {
        get => GetValue(MaximumProperty);
        set => SetValue(MaximumProperty, value);
    }

    public double Step
    {
        get => GetValue(StepProperty);
        set => SetValue(StepProperty, value);
    }

    public string Format
    {
        get => GetValue(FormatProperty);
        set => SetValue(FormatProperty, value);
    }

    public bool IsInvalid => Classes.Contains(NovaControlClasses.Invalid);

    /// <summary>Raised only after a successful commit.</summary>
    public event EventHandler<double>? ValueCommitted;

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == ValueProperty && !_updating)
        {
            _edit.SetValue(Value);
            SetText(_edit.Text);
            SetInvalid(false);
        }
        else if (change.Property == FormatProperty && !_updating)
        {
            var value = _edit.Value;
            _edit = new NumberEditState(Format);
            _edit.SetValue(value);
            SetText(_edit.Text);
            SetInvalid(false);
        }
        else if (change.Property == TextProperty && !_updating)
        {
            _edit.SetText(Text);
            SetInvalid(!_edit.HasValidValue && !string.IsNullOrWhiteSpace(Text));
        }
    }

    protected override void OnLostFocus(RoutedEventArgs e)
    {
        base.OnLostFocus(e);
        Commit();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        switch (e.Key)
        {
            case Key.Enter:
                Commit();
                e.Handled = true;
                return;
            case Key.Up:
                StepBy(Step);
                e.Handled = true;
                return;
            case Key.Down:
                StepBy(-Step);
                e.Handled = true;
                return;
            case Key.Escape:
                _edit.Revert();
                SetText(_edit.Text);
                SetInvalid(false);
                e.Handled = true;
                return;
        }

        base.OnKeyDown(e);
    }

    public bool Commit()
    {
        if (!_edit.TryCommit(Minimum, Maximum, out var value))
        {
            SetInvalid(true);
            return false;
        }

        SetInvalid(false);
        SetText(_edit.Text);
        SetCurrentValue(ValueProperty, value);
        ValueCommitted?.Invoke(this, value);
        return true;
    }

    public bool StepBy(double delta)
    {
        if (!_edit.TryStep(delta, Minimum, Maximum, out var value))
            return false;

        SetInvalid(false);
        SetText(_edit.Text);
        SetCurrentValue(ValueProperty, value);
        ValueCommitted?.Invoke(this, value);
        return true;
    }

    private void SetText(string text)
    {
        _updating = true;
        try
        {
            SetCurrentValue(TextProperty, text);
        }
        finally
        {
            _updating = false;
        }
    }

    private void SetInvalid(bool invalid)
    {
        if (invalid) Classes.Add(NovaControlClasses.Invalid);
        else Classes.Remove(NovaControlClasses.Invalid);
    }
}
