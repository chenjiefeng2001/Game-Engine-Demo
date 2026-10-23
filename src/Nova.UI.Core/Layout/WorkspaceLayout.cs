namespace Nova.UI.Core.Layout;

/// <summary>
/// Serializable workspace shell layout. v0.2 deliberately models a fixed
/// four-region shell (hierarchy / viewport / inspector / console) rather than a
/// full dock tree. The v0.3 docking model will migrate these fields.
/// </summary>
public sealed record WorkspaceLayout
{
    public const int CurrentSchemaVersion = 1;
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

    public static WorkspaceLayout Default { get; } = new();
}
