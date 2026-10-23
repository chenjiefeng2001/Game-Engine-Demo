namespace Nova.UI.Controls;

public enum NovaButtonKind
{
    Default,
    Primary,
    Subtle,
    Danger,
}

/// <summary>
/// Style class vocabulary shared by controls and NovaThemeAdapter. Keeping this
/// pure makes the visual contract testable without initializing Avalonia.
/// </summary>
public static class NovaControlClasses
{
    public const string Button = "nova-button";
    public const string Primary = "nova-primary";
    public const string Subtle = "nova-subtle";
    public const string Danger = "nova-danger";
    public const string TextField = "nova-text-field";
    public const string NumberField = "nova-number-field";
    public const string Invalid = "nova-invalid";
    public const string PropertyRow = "nova-property-row";
    public const string InspectorSection = "nova-inspector-section";
    public const string Surface = "nova-panel";
    public const string Elevated = "nova-elevated";
    public const string Toolbar = "nova-toolbar";
    public const string Divider = "nova-divider";
    public const string Mono = "nova-mono";
    public const string Muted = "nova-muted";
    public const string SectionTitle = "nova-section-title";
    public const string PropertyLabel = "nova-property-label";

    public static string For(NovaButtonKind kind) => kind switch
    {
        NovaButtonKind.Primary => Primary,
        NovaButtonKind.Subtle => Subtle,
        NovaButtonKind.Danger => Danger,
        _ => string.Empty,
    };

    public static IReadOnlyList<string> Variants { get; } =
        [Primary, Subtle, Danger];

    /// <summary>P0 surface delivered by v0.2. Used by contract tests and the playground.</summary>
    public static IReadOnlyList<string> P0Controls { get; } =
    [
        "Box", "Row", "Column", "Grid", "ScrollArea", "Surface",
        "Text", "Icon", "Divider",
        "Button", "IconButton", "TextField", "NumberField", "Toggle", "Select",
        "PropertyRow", "InspectorSection", "TreeView", "ListView", "Splitter",
    ];
}
