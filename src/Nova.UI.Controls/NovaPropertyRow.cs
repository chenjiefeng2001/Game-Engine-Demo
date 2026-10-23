using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Media;
using Avalonia.Layout;

namespace Nova.UI.Controls;

/// <summary>
/// Label + editor row used by the inspector. The editor is any control supplied
/// as <see cref="Value"/>; this row only owns layout and tokenized classes.
/// </summary>
public class NovaPropertyRow : UserControl
{
    public static readonly StyledProperty<string> LabelProperty =
        AvaloniaProperty.Register<NovaPropertyRow, string>(nameof(Label), string.Empty);

    public static readonly StyledProperty<string?> DescriptionProperty =
        AvaloniaProperty.Register<NovaPropertyRow, string?>(nameof(Description));

    public static readonly StyledProperty<object?> ValueProperty =
        AvaloniaProperty.Register<NovaPropertyRow, object?>(nameof(Value));

    public static readonly StyledProperty<double> LabelWidthProperty =
        AvaloniaProperty.Register<NovaPropertyRow, double>(nameof(LabelWidth), 132d);

    private readonly TextBlock _label;
    private readonly TextBlock _description;
    private readonly ContentPresenter _presenter;
    private readonly ColumnDefinition _labelColumn;

    public NovaPropertyRow()
    {
        Classes.Add(NovaControlClasses.PropertyRow);

        _labelColumn = new ColumnDefinition { Width = new GridLength(LabelWidth) };
        _label = new TextBlock
        {
            Classes = { NovaControlClasses.PropertyLabel },
            VerticalAlignment = VerticalAlignment.Center,
            TextTrimming = TextTrimming.CharacterEllipsis,
        };
        _description = new TextBlock
        {
            Classes = { NovaControlClasses.Muted },
            FontSize = 11,
            Margin = new Thickness(0, 2, 0, 0),
            IsVisible = false,
        };
        _presenter = new ContentPresenter
        {
            VerticalAlignment = VerticalAlignment.Center,
        };

        var editorPanel = new StackPanel { Spacing = 2 };
        editorPanel.Children.Add(_presenter);
        editorPanel.Children.Add(_description);

        var grid = new Grid
        {
            ColumnDefinitions =
            {
                _labelColumn,
                new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) },
            },
            Margin = new Thickness(0, 2),
        };
        grid.Children.Add(_label);
        Grid.SetColumn(editorPanel, 1);
        grid.Children.Add(editorPanel);

        Content = grid;
        ApplyLabel();
        ApplyDescription();
    }

    protected override Type StyleKeyOverride => typeof(UserControl);

    public string Label
    {
        get => GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public string? Description
    {
        get => GetValue(DescriptionProperty);
        set => SetValue(DescriptionProperty, value);
    }

    public object? Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public double LabelWidth
    {
        get => GetValue(LabelWidthProperty);
        set => SetValue(LabelWidthProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        switch (change.Property)
        {
            case var property when property == LabelProperty:
                ApplyLabel();
                break;
            case var property when property == DescriptionProperty:
                ApplyDescription();
                break;
            case var property when property == ValueProperty:
                _presenter.Content = Value;
                break;
            case var property when property == LabelWidthProperty:
                _labelColumn.Width = new GridLength(Math.Max(0, LabelWidth));
                break;
        }
    }

    private void ApplyLabel() => _label.Text = Label;

    private void ApplyDescription()
    {
        _description.Text = Description;
        _description.IsVisible = !string.IsNullOrWhiteSpace(Description);
    }
}
