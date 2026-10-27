namespace Nova.UI.Core.Layout;

/// <summary>
/// Serializable workspace shell layout. Schema v3 adds optional hidden panels
/// to tab groups; v1 shell fields and the v2 dock snapshot remain readable.
/// </summary>
public sealed record WorkspaceLayout
{
    public const int CurrentSchemaVersion = 3;
    public const double MinPanelWidth = 160;
    public const double MinPanelHeight = 120;
    public const double MinConsoleHeight = 80;
    public const double MaxConsoleHeight = 600;
    public const double MinWindowWidth = 800;
    public const double MinWindowHeight = 600;

    public int SchemaVersion { get; init; } = CurrentSchemaVersion;
    public double WindowWidth { get; init; } = 1280;
    public double WindowHeight { get; init; } = 800;
    public double HierarchyWidth { get; init; } = 240;
    public double InspectorWidth { get; init; } = 300;
    public double ConsoleHeight { get; init; } = 180;
    public bool ShowHierarchy { get; init; } = true;
    public bool ShowInspector { get; init; } = true;
    public bool ShowConsole { get; init; } = true;
    public string ThemeId { get; init; } = "nova.dark";

    /// <summary>Null means the default dock workspace.</summary>
    public DockWorkspaceSnapshot? Dock { get; init; }

    public static WorkspaceLayout Default { get; } = new();
}
