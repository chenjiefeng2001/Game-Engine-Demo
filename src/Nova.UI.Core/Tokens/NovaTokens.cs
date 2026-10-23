namespace Nova.UI.Core.Tokens;

/// <summary>Built-in token palettes. Additional themes should be data, not code branches in controls.</summary>
public static class NovaTokens
{
    public static DesignTokenSet Dark { get; } = Create("nova.dark", new Dictionary<string, string>
    {
        [TokenKeys.BackgroundCanvas] = "#15171B",
        [TokenKeys.BackgroundPanel] = "#1D2025",
        [TokenKeys.BackgroundElevated] = "#252931",
        [TokenKeys.BackgroundInput] = "#14161A",
        [TokenKeys.BackgroundHover] = "#2C313A",
        [TokenKeys.BackgroundSelected] = "#314056",
        [TokenKeys.ForegroundPrimary] = "#E7EAF0",
        [TokenKeys.ForegroundSecondary] = "#B1B8C4",
        [TokenKeys.ForegroundMuted] = "#7E8694",
        [TokenKeys.Accent] = "#4C8DFF",
        [TokenKeys.AccentHover] = "#6AA1FF",
        [TokenKeys.AccentPressed] = "#3C73D4",
        [TokenKeys.Border] = "#333841",
        [TokenKeys.BorderStrong] = "#4A515D",
        [TokenKeys.Danger] = "#E0605E",
        [TokenKeys.Warning] = "#D7A648",
        [TokenKeys.Success] = "#52B788",
        [TokenKeys.Focus] = "#86B4FF",
        [TokenKeys.Overlay] = "#B3000000",
    });

    public static DesignTokenSet Light { get; } = Create("nova.light", new Dictionary<string, string>
    {
        [TokenKeys.BackgroundCanvas] = "#F2F4F7",
        [TokenKeys.BackgroundPanel] = "#FAFBFC",
        [TokenKeys.BackgroundElevated] = "#FFFFFF",
        [TokenKeys.BackgroundInput] = "#FFFFFF",
        [TokenKeys.BackgroundHover] = "#E8ECF2",
        [TokenKeys.BackgroundSelected] = "#D7E5FF",
        [TokenKeys.ForegroundPrimary] = "#1B1F26",
        [TokenKeys.ForegroundSecondary] = "#4B5563",
        [TokenKeys.ForegroundMuted] = "#7A8494",
        [TokenKeys.Accent] = "#2F6FDE",
        [TokenKeys.AccentHover] = "#3F7FF0",
        [TokenKeys.AccentPressed] = "#245BB8",
        [TokenKeys.Border] = "#D5DAE2",
        [TokenKeys.BorderStrong] = "#B4BCC8",
        [TokenKeys.Danger] = "#C64545",
        [TokenKeys.Warning] = "#A87417",
        [TokenKeys.Success] = "#2E8761",
        [TokenKeys.Focus] = "#2F6FDE",
        [TokenKeys.Overlay] = "#66000000",
    });

    private static DesignTokenSet Create(string id, IReadOnlyDictionary<string, string> colors)
    {
        var tokens = new List<DesignToken>();

        foreach (var (key, value) in colors)
            tokens.Add(new DesignToken(key, TokenType.Color, value));

        Add(tokens, TokenType.FontFamily, TokenKeys.FontUi, "Segoe UI, Microsoft YaHei UI, Noto Sans CJK SC");
        Add(tokens, TokenType.FontFamily, TokenKeys.FontMono, "Cascadia Mono, Consolas, Noto Sans Mono");
        Add(tokens, TokenType.FontSize, TokenKeys.FontSizeXs, "11");
        Add(tokens, TokenType.FontSize, TokenKeys.FontSizeSm, "12");
        Add(tokens, TokenType.FontSize, TokenKeys.FontSizeMd, "13");
        Add(tokens, TokenType.FontSize, TokenKeys.FontSizeLg, "16");
        Add(tokens, TokenType.FontSize, TokenKeys.FontSizeXl, "20");
        Add(tokens, TokenType.FontWeight, TokenKeys.FontWeightRegular, "Normal");
        Add(tokens, TokenType.FontWeight, TokenKeys.FontWeightSemiBold, "SemiBold");

        Add(tokens, TokenType.Dimension, TokenKeys.SpacingXxs, "2");
        Add(tokens, TokenType.Dimension, TokenKeys.SpacingXs, "4");
        Add(tokens, TokenType.Dimension, TokenKeys.SpacingSm, "8");
        Add(tokens, TokenType.Dimension, TokenKeys.SpacingMd, "12");
        Add(tokens, TokenType.Dimension, TokenKeys.SpacingLg, "16");
        Add(tokens, TokenType.Dimension, TokenKeys.SpacingXl, "24");

        Add(tokens, TokenType.Dimension, TokenKeys.RadiusSm, "3");
        Add(tokens, TokenType.Dimension, TokenKeys.RadiusMd, "5");
        Add(tokens, TokenType.Dimension, TokenKeys.RadiusLg, "8");

        Add(tokens, TokenType.Dimension, TokenKeys.ControlHeightSm, "24");
        Add(tokens, TokenType.Dimension, TokenKeys.ControlHeight, "28");
        Add(tokens, TokenType.Dimension, TokenKeys.ControlHeightLg, "32");
        Add(tokens, TokenType.Dimension, TokenKeys.IconSize, "16");
        Add(tokens, TokenType.Dimension, TokenKeys.PanelHeaderHeight, "30");
        Add(tokens, TokenType.Dimension, TokenKeys.SplitterSize, "6");
        Add(tokens, TokenType.Dimension, TokenKeys.PanelMinWidth, "160");
        Add(tokens, TokenType.Dimension, TokenKeys.PanelMinHeight, "120");

        Add(tokens, TokenType.Duration, TokenKeys.DurationFast, "120");
        Add(tokens, TokenType.Duration, TokenKeys.DurationNormal, "180");

        return new DesignTokenSet(id, tokens);
    }

    private static void Add(
        ICollection<DesignToken> tokens,
        TokenType type,
        string key,
        string value)
        => tokens.Add(new DesignToken(key, type, value));
}
