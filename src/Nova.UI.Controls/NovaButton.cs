using Avalonia;
using Avalonia.Controls;

namespace Nova.UI.Controls;

/// <summary>Button with token-backed variants. Uses the standard Fluent template.</summary>
public class NovaButton : Button
{
    public static readonly StyledProperty<NovaButtonKind> KindProperty =
        AvaloniaProperty.Register<NovaButton, NovaButtonKind>(nameof(Kind));

    public NovaButton()
    {
        Classes.Add(NovaControlClasses.Button);
    }

    protected override Type StyleKeyOverride => typeof(Button);

    public NovaButtonKind Kind
    {
        get => GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == KindProperty)
            ApplyKind(change.GetNewValue<NovaButtonKind>());
    }

    private void ApplyKind(NovaButtonKind kind)
    {
        foreach (var variant in NovaControlClasses.Variants)
            Classes.Remove(variant);

        var className = NovaControlClasses.For(kind);
        if (!string.IsNullOrEmpty(className))
            Classes.Add(className);
    }
}

/// <summary>Compact square button; the glyph is provided by the caller.</summary>
public class NovaIconButton : NovaButton
{
    public static readonly StyledProperty<string?> GlyphProperty =
        AvaloniaProperty.Register<NovaIconButton, string?>(nameof(Glyph));

    public NovaIconButton()
    {
        Kind = NovaButtonKind.Subtle;
        MinWidth = 28;
        Padding = new Thickness(4);
        Classes.Add("nova-icon-button");
    }

    protected override Type StyleKeyOverride => typeof(Button);

    public string? Glyph
    {
        get => GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == GlyphProperty)
            Content = Glyph;
    }
}
