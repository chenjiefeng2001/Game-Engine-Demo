using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Nova.UI.Core.Tokens;
using Nova.UI.Theme;

namespace Nova.UI.Avalonia;

public sealed class NovaThemeApplication
{
    public NovaThemeApplication(
        ThemeDefinition theme,
        ResourceDictionary resources,
        Styles styles,
        IReadOnlyList<string> resourceKeys)
    {
        Theme = theme;
        Resources = resources;
        Styles = styles;
        ResourceKeys = resourceKeys;
    }

    public ThemeDefinition Theme { get; }
    public ResourceDictionary Resources { get; }
    public Styles Styles { get; }
    public IReadOnlyList<string> ResourceKeys { get; }
}

/// <summary>
/// Maps framework-neutral design tokens to Avalonia resources and global styles.
///
/// This is deliberately the only place that knows about Avalonia types for the
/// theme system. Controls consume classes (nova-button, nova-text-field, ...) and
/// styles; they never hard-code colors or sizes.
/// </summary>
public static class NovaThemeAdapter
{
    private static readonly ConditionalWeakTable<Application, NovaThemeApplication> Applied = new();

    public static NovaThemeApplication Apply(Application application, ThemeDefinition theme)
    {
        ArgumentNullException.ThrowIfNull(application);
        ArgumentNullException.ThrowIfNull(theme);

        var errors = theme.Validate();
        if (errors.Count > 0)
            throw new InvalidOperationException(
                $"Theme '{theme.Id}' is invalid: {string.Join("; ", errors)}");

        if (Applied.TryGetValue(application, out var previous))
        {
            if (previous.Resources is not null)
                application.Resources.MergedDictionaries.Remove(previous.Resources);
            application.Styles.Remove(previous.Styles);
        }

        // Let Fluent's own control themes follow the Nova theme variant; otherwise
        // light Nova tokens would sit on dark Fluent template chrome (or vice versa).
        application.RequestedThemeVariant = theme.IsDark ? ThemeVariant.Dark : ThemeVariant.Light;

        var (resources, keys) = BuildResources(theme.Tokens);
        var styles = BuildStyles(theme.Tokens);

        application.Resources.MergedDictionaries.Add(resources);
        application.Styles.Add(styles);

        var result = new NovaThemeApplication(theme, resources, styles, keys);
        Applied.Remove(application);
        Applied.Add(application, result);
        return result;
    }

    public static bool IsApplied(Application application) => Applied.TryGetValue(application, out _);

    private static (ResourceDictionary, IReadOnlyList<string>) BuildResources(DesignTokenSet tokens)
    {
        var dictionary = new ResourceDictionary();
        var keys = new List<string>(tokens.Tokens.Count * 2);

        foreach (var token in tokens.Tokens.Values)
        {
            switch (token.Type)
            {
                case TokenType.Color:
                    {
                        var color = ParseColor(token);
                        var colorKey = NovaResourceKeys.Color(token.Key);
                        var brushKey = NovaResourceKeys.Brush(token.Key);
                        dictionary.Add(colorKey, color);
                        dictionary.Add(brushKey, new SolidColorBrush(color));
                        keys.Add(colorKey);
                        keys.Add(brushKey);
                        break;
                    }
                case TokenType.Dimension:
                case TokenType.FontSize:
                case TokenType.Number:
                    {
                        var key = NovaResourceKeys.Number(token.Key);
                        dictionary.Add(key, tokens.GetNumber(token.Key));
                        keys.Add(key);
                        break;
                    }
                case TokenType.FontFamily:
                    {
                        var key = NovaResourceKeys.FontFamily(token.Key);
                        dictionary.Add(key, new FontFamily(token.Value));
                        keys.Add(key);
                        break;
                    }
                case TokenType.FontWeight:
                    {
                        var key = NovaResourceKeys.FontWeight(token.Key);
                        dictionary.Add(key, ToFontWeight(tokens.GetFontWeight(token.Key)));
                        keys.Add(key);
                        break;
                    }
                case TokenType.Duration:
                    {
                        var key = NovaResourceKeys.Duration(token.Key);
                        dictionary.Add(key, tokens.GetDuration(token.Key));
                        keys.Add(key);
                        break;
                    }
                default:
                    {
                        var key = $"{NovaResourceKeys.Prefix}.String.{token.Key}";
                        dictionary.Add(key, token.Value);
                        keys.Add(key);
                        break;
                    }
            }
        }

        return (dictionary, keys);
    }

    private static Styles BuildStyles(DesignTokenSet tokens)
    {
        var canvas = Brush(tokens, TokenKeys.BackgroundCanvas);
        var panel = Brush(tokens, TokenKeys.BackgroundPanel);
        var elevated = Brush(tokens, TokenKeys.BackgroundElevated);
        var input = Brush(tokens, TokenKeys.BackgroundInput);
        var hover = Brush(tokens, TokenKeys.BackgroundHover);
        var selected = Brush(tokens, TokenKeys.BackgroundSelected);
        var fg = Brush(tokens, TokenKeys.ForegroundPrimary);
        var fgSecondary = Brush(tokens, TokenKeys.ForegroundSecondary);
        var fgMuted = Brush(tokens, TokenKeys.ForegroundMuted);
        var accent = Brush(tokens, TokenKeys.Accent);
        var accentHover = Brush(tokens, TokenKeys.AccentHover);
        var accentPressed = Brush(tokens, TokenKeys.AccentPressed);
        var border = Brush(tokens, TokenKeys.Border);
        var borderStrong = Brush(tokens, TokenKeys.BorderStrong);
        var danger = Brush(tokens, TokenKeys.Danger);
        var focus = Brush(tokens, TokenKeys.Focus);
        var transparent = Brushes.Transparent;

        var font = new FontFamily(tokens.GetString(TokenKeys.FontUi));
        var mono = new FontFamily(tokens.GetString(TokenKeys.FontMono));
        var sizeSm = tokens.GetNumber(TokenKeys.FontSizeSm);
        var sizeMd = tokens.GetNumber(TokenKeys.FontSizeMd);
        var sizeLg = tokens.GetNumber(TokenKeys.FontSizeLg);
        var height = tokens.GetNumber(TokenKeys.ControlHeight);
        var heightSm = tokens.GetNumber(TokenKeys.ControlHeightSm);
        var paddingSm = new Thickness(tokens.GetNumber(TokenKeys.SpacingSm));
        var paddingMd = new Thickness(tokens.GetNumber(TokenKeys.SpacingMd));
        var splitterSize = tokens.GetNumber(TokenKeys.SplitterSize);
        var radiusSm = new CornerRadius(tokens.GetNumber(TokenKeys.RadiusSm));
        var radiusMd = new CornerRadius(tokens.GetNumber(TokenKeys.RadiusMd));

        var styles = new Styles
        {
            new Style(x => x.OfType<Window>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, canvas),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontFamilyProperty, font),
                    new Setter(TemplatedControl.FontSizeProperty, sizeMd),
                },
            },
            new Style(x => x.OfType<TextBlock>())
            {
                Setters =
                {
                    new Setter(TextBlock.ForegroundProperty, fg),
                    new Setter(TextBlock.FontFamilyProperty, font),
                    new Setter(TextBlock.FontSizeProperty, sizeMd),
                },
            },
            new Style(x => x.OfType<Button>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, elevated),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.BorderThicknessProperty, new Thickness(1)),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(10, 4)),
                    new Setter(TemplatedControl.MinHeightProperty, height),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusMd),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-button"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, elevated),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.BorderThicknessProperty, new Thickness(1)),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(10, 4)),
                    new Setter(TemplatedControl.MinHeightProperty, height),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusMd),
                },
            },
            new Style(x => x.OfType<Button>().Class(":pointerover"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, hover),
                    new Setter(TemplatedControl.BorderBrushProperty, borderStrong),
                },
            },
            new Style(x => x.OfType<Button>().Class(":pressed"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, selected) },
            },
            new Style(x => x.OfType<Button>().Class(":disabled"))
            {
                Setters = { new Setter(TemplatedControl.OpacityProperty, 0.45) },
            },
            new Style(x => x.OfType<Button>().Class("nova-primary"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, accent),
                    new Setter(TemplatedControl.BorderBrushProperty, accent),
                    new Setter(TemplatedControl.ForegroundProperty, Brushes.White),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-primary").Class(":pointerover"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, accentHover),
                    new Setter(TemplatedControl.BorderBrushProperty, accentHover),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-primary").Class(":pressed"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, accentPressed),
                    new Setter(TemplatedControl.BorderBrushProperty, accentPressed),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-danger"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, danger),
                    new Setter(TemplatedControl.BorderBrushProperty, danger),
                    new Setter(TemplatedControl.ForegroundProperty, Brushes.White),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-subtle"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, transparent),
                    new Setter(TemplatedControl.BorderBrushProperty, transparent),
                    new Setter(TemplatedControl.ForegroundProperty, fgSecondary),
                },
            },
            new Style(x => x.OfType<Button>().Class("nova-subtle").Class(":pointerover"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, hover),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                },
            },

            new Style(x => x.OfType<TextBox>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, input),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.BorderThicknessProperty, new Thickness(1)),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(8, 4)),
                    new Setter(TemplatedControl.MinHeightProperty, height),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusMd),
                    new Setter(TextBox.SelectionBrushProperty, selected),
                    new Setter(TextBox.CaretBrushProperty, fg),
                },
            },
            new Style(x => x.OfType<TextBox>().Class(":focus"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BorderBrushProperty, focus),
                },
            },
            new Style(x => x.OfType<TextBox>().Class("nova-invalid"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BorderBrushProperty, danger),
                },
            },
            new Style(x => x.OfType<TextBox>().Class("nova-mono"))
            {
                Setters = { new Setter(TemplatedControl.FontFamilyProperty, mono) },
            },

            new Style(x => x.OfType<ComboBox>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, input),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.MinHeightProperty, height),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusMd),
                },
            },
            new Style(x => x.OfType<ComboBox>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BorderBrushProperty, borderStrong) },
            },

            new Style(x => x.OfType<CheckBox>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.MinHeightProperty, heightSm),
                },
            },

            new Style(x => x.OfType<ToggleButton>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, elevated),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.BorderThicknessProperty, new Thickness(1)),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(8, 2)),
                    new Setter(TemplatedControl.MinHeightProperty, heightSm),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusSm),
                },
            },
            new Style(x => x.OfType<ToggleButton>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, hover) },
            },
            new Style(x => x.OfType<ToggleButton>().Class(":checked"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, selected),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, accent),
                },
            },

            new Style(x => x.OfType<TreeView>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, transparent),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                },
            },
            new Style(x => x.OfType<TreeViewItem>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.MinHeightProperty, heightSm),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(4, 1)),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusSm),
                },
            },
            new Style(x => x.OfType<TreeViewItem>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, hover) },
            },
            new Style(x => x.OfType<TreeViewItem>().Class(":selected"))
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, selected),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                },
            },

            new Style(x => x.OfType<ListBox>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, transparent),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                },
            },
            new Style(x => x.OfType<ListBoxItem>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.MinHeightProperty, heightSm),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(6, 2)),
                    new Setter(TemplatedControl.CornerRadiusProperty, radiusSm),
                },
            },
            new Style(x => x.OfType<ListBoxItem>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, hover) },
            },
            new Style(x => x.OfType<ListBoxItem>().Class(":selected"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, selected) },
            },

            new Style(x => x.OfType<GridSplitter>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, border),
                    new Setter(TemplatedControl.MinWidthProperty, splitterSize),
                    new Setter(TemplatedControl.MinHeightProperty, splitterSize),
                },
            },
            new Style(x => x.OfType<GridSplitter>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, accent) },
            },

            new Style(x => x.OfType<Menu>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, panel),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                },
            },
            new Style(x => x.OfType<MenuItem>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                },
            },
            new Style(x => x.OfType<MenuItem>().Class(":pointerover"))
            {
                Setters = { new Setter(TemplatedControl.BackgroundProperty, hover) },
            },

            new Style(x => x.OfType<Expander>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.FontSizeProperty, sizeSm),
                    new Setter(TemplatedControl.PaddingProperty, new Thickness(0)),
                },
            },
            new Style(x => x.OfType<ScrollViewer>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, transparent),
                },
            },
            new Style(x => x.OfType<ToolTip>())
            {
                Setters =
                {
                    new Setter(TemplatedControl.BackgroundProperty, elevated),
                    new Setter(TemplatedControl.ForegroundProperty, fg),
                    new Setter(TemplatedControl.BorderBrushProperty, border),
                    new Setter(TemplatedControl.PaddingProperty, paddingSm),
                },
            },

            new Style(x => x.OfType<Border>().Class("nova-panel"))
            {
                Setters =
                {
                    new Setter(Border.BackgroundProperty, panel),
                    new Setter(Border.BorderBrushProperty, border),
                    new Setter(Border.BorderThicknessProperty, new Thickness(1)),
                    new Setter(Border.CornerRadiusProperty, radiusSm),
                },
            },
            new Style(x => x.OfType<Border>().Class("nova-elevated"))
            {
                Setters =
                {
                    new Setter(Border.BackgroundProperty, elevated),
                    new Setter(Border.BorderBrushProperty, border),
                    new Setter(Border.BorderThicknessProperty, new Thickness(1)),
                    new Setter(Border.CornerRadiusProperty, radiusMd),
                },
            },
            new Style(x => x.OfType<Border>().Class("nova-toolbar"))
            {
                Setters =
                {
                    new Setter(Border.BackgroundProperty, panel),
                    new Setter(Border.BorderBrushProperty, border),
                    new Setter(Border.BorderThicknessProperty, new Thickness(0, 0, 0, 1)),
                    new Setter(Border.PaddingProperty, paddingSm),
                },
            },
            new Style(x => x.OfType<Border>().Class("nova-divider"))
            {
                Setters =
                {
                    new Setter(Border.BackgroundProperty, border),
                    new Setter(Border.HeightProperty, 1.0),
                },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-muted"))
            {
                Setters = { new Setter(TextBlock.ForegroundProperty, fgMuted) },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-secondary"))
            {
                Setters = { new Setter(TextBlock.ForegroundProperty, fgSecondary) },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-title"))
            {
                Setters =
                {
                    new Setter(TextBlock.FontSizeProperty, sizeLg),
                    new Setter(TextBlock.FontWeightProperty, FontWeight.SemiBold),
                },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-section-title"))
            {
                Setters =
                {
                    new Setter(TextBlock.FontSizeProperty, sizeSm),
                    new Setter(TextBlock.FontWeightProperty, FontWeight.SemiBold),
                    new Setter(TextBlock.ForegroundProperty, fgSecondary),
                },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-property-label"))
            {
                Setters =
                {
                    new Setter(TextBlock.ForegroundProperty, fgSecondary),
                    new Setter(TextBlock.FontSizeProperty, sizeSm),
                    new Setter(TextBlock.VerticalAlignmentProperty, VerticalAlignment.Center),
                },
            },
            new Style(x => x.OfType<TextBlock>().Class("nova-mono"))
            {
                Setters =
                {
                    new Setter(TextBlock.FontFamilyProperty, mono),
                    new Setter(TextBlock.FontSizeProperty, sizeSm),
                },
            },
        };

        _ = paddingMd;
        _ = radiusMd;
        return styles;
    }

    private static SolidColorBrush Brush(DesignTokenSet tokens, string key)
        => new(ParseColor(tokens.Get(key)));

    private static Color ParseColor(DesignToken token)
    {
        if (!RgbaColor.TryParse(token.Value, out var rgba))
            throw new FormatException($"Token '{token.Key}' is not a color: '{token.Value}'.");
        return Color.FromArgb(rgba.A, rgba.R, rgba.G, rgba.B);
    }

    private static FontWeight ToFontWeight(FontWeightValue weight) => weight switch
    {
        FontWeightValue.Thin => FontWeight.Thin,
        FontWeightValue.ExtraLight => FontWeight.ExtraLight,
        FontWeightValue.Light => FontWeight.Light,
        FontWeightValue.Normal => FontWeight.Normal,
        FontWeightValue.Medium => FontWeight.Medium,
        FontWeightValue.SemiBold => FontWeight.SemiBold,
        FontWeightValue.Bold => FontWeight.Bold,
        FontWeightValue.ExtraBold => FontWeight.ExtraBold,
        FontWeightValue.Black => FontWeight.Black,
        _ => FontWeight.Normal,
    };
}
