namespace Nova.UI.Core.Tokens;

/// <summary>
/// Canonical token keys. Controls, themes, tests, and documentation all refer to
/// these constants instead of scattering literals.
/// </summary>
public static class TokenKeys
{
    public const string BackgroundCanvas = "color.bg.canvas";
    public const string BackgroundPanel = "color.bg.panel";
    public const string BackgroundElevated = "color.bg.elevated";
    public const string BackgroundInput = "color.bg.input";
    public const string BackgroundHover = "color.bg.hover";
    public const string BackgroundSelected = "color.bg.selected";
    public const string ForegroundPrimary = "color.fg.primary";
    public const string ForegroundSecondary = "color.fg.secondary";
    public const string ForegroundMuted = "color.fg.muted";
    public const string Accent = "color.accent";
    public const string AccentHover = "color.accent.hover";
    public const string AccentPressed = "color.accent.pressed";
    public const string Border = "color.border";
    public const string BorderStrong = "color.border.strong";
    public const string Danger = "color.danger";
    public const string Warning = "color.warning";
    public const string Success = "color.success";
    public const string Focus = "color.focus";
    public const string Overlay = "color.overlay";

    public const string FontUi = "font.family.ui";
    public const string FontMono = "font.family.mono";
    public const string FontSizeXs = "font.size.xs";
    public const string FontSizeSm = "font.size.sm";
    public const string FontSizeMd = "font.size.md";
    public const string FontSizeLg = "font.size.lg";
    public const string FontSizeXl = "font.size.xl";
    public const string FontWeightRegular = "font.weight.regular";
    public const string FontWeightSemiBold = "font.weight.semibold";

    public const string SpacingXxs = "spacing.xxs";
    public const string SpacingXs = "spacing.xs";
    public const string SpacingSm = "spacing.sm";
    public const string SpacingMd = "spacing.md";
    public const string SpacingLg = "spacing.lg";
    public const string SpacingXl = "spacing.xl";

    public const string RadiusSm = "radius.sm";
    public const string RadiusMd = "radius.md";
    public const string RadiusLg = "radius.lg";

    public const string ControlHeightSm = "size.control.height.sm";
    public const string ControlHeight = "size.control.height";
    public const string ControlHeightLg = "size.control.height.lg";
    public const string IconSize = "size.icon";
    public const string PanelHeaderHeight = "size.panel.header";
    public const string SplitterSize = "size.splitter";
    public const string PanelMinWidth = "size.panel.min-width";
    public const string PanelMinHeight = "size.panel.min-height";

    public const string DurationFast = "duration.fast";
    public const string DurationNormal = "duration.normal";

    public static IReadOnlyList<string> Required { get; } =
    [
        BackgroundCanvas, BackgroundPanel, BackgroundElevated, BackgroundInput,
        BackgroundHover, BackgroundSelected,
        ForegroundPrimary, ForegroundSecondary, ForegroundMuted,
        Accent, AccentHover, AccentPressed, Border, BorderStrong,
        Danger, Warning, Success, Focus, Overlay,
        FontUi, FontMono, FontSizeXs, FontSizeSm, FontSizeMd, FontSizeLg, FontSizeXl,
        FontWeightRegular, FontWeightSemiBold,
        SpacingXxs, SpacingXs, SpacingSm, SpacingMd, SpacingLg, SpacingXl,
        RadiusSm, RadiusMd, RadiusLg,
        ControlHeightSm, ControlHeight, ControlHeightLg, IconSize,
        PanelHeaderHeight, SplitterSize, PanelMinWidth, PanelMinHeight,
        DurationFast, DurationNormal,
    ];
}
