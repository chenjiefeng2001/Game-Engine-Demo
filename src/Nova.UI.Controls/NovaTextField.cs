using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace Nova.UI.Controls;

/// <summary>
/// Text field with a single invalid-state hook and an explicit commit signal.
/// Commit is raised on Enter/blur; callers decide whether to route a command.
/// </summary>
public class NovaTextField : TextBox
{
    public static readonly StyledProperty<bool> IsInvalidProperty =
        AvaloniaProperty.Register<NovaTextField, bool>(nameof(IsInvalid));

    private string _committedText = string.Empty;

    public NovaTextField()
    {
        Classes.Add(NovaControlClasses.TextField);
    }

    protected override Type StyleKeyOverride => typeof(TextBox);

    public bool IsInvalid
    {
        get => GetValue(IsInvalidProperty);
        set => SetValue(IsInvalidProperty, value);
    }

    /// <summary>Raised when the user presses Enter or leaves the field.</summary>
    public event EventHandler<string>? TextCommitted;

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == IsInvalidProperty)
        {
            if (IsInvalid) Classes.Add(NovaControlClasses.Invalid);
            else Classes.Remove(NovaControlClasses.Invalid);
        }
    }

    protected override void OnLostFocus(RoutedEventArgs e)
    {
        base.OnLostFocus(e);
        CommitIfChanged();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            CommitIfChanged(force: true);
            e.Handled = true;
            return;
        }

        base.OnKeyDown(e);
    }

    public void CommitIfChanged(bool force = false)
    {
        var text = Text ?? string.Empty;
        if (!force && string.Equals(text, _committedText, StringComparison.Ordinal))
            return;

        _committedText = text;
        TextCommitted?.Invoke(this, text);
    }

    public void MarkCommitted(string text)
    {
        _committedText = text ?? string.Empty;
    }
}
