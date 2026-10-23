using Avalonia;
using Avalonia.Controls;

namespace Nova.UI.Controls;

/// <summary>
/// Labeled on/off control. Built on CheckBox so keyboard/focus/checked visuals
/// come from a stable Fluent template instead of a hand-rolled switch.
/// </summary>
public class NovaToggle : CheckBox
{
    public static readonly StyledProperty<string?> LabelProperty =
        AvaloniaProperty.Register<NovaToggle, string?>(nameof(Label));

    public NovaToggle()
    {
        Classes.Add("nova-toggle");
    }

    protected override Type StyleKeyOverride => typeof(CheckBox);

    public string? Label
    {
        get => GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == LabelProperty)
            Content = Label;
    }
}

public class NovaSelect : ComboBox
{
    public NovaSelect()
    {
        Classes.Add("nova-select");
    }

    protected override Type StyleKeyOverride => typeof(ComboBox);
}
