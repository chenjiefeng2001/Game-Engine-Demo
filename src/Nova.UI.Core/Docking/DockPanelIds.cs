namespace Nova.UI.Core.Docking;

/// <summary>
/// Built-in panel instance ids used by the default dock tree. Instance ids are
/// stable identities, never titles or array positions.
/// </summary>
public static class DockPanelIds
{
    public static readonly PanelInstanceId Hierarchy = new("editor.hierarchy");
    public static readonly PanelInstanceId Scene = new("editor.scene");
    public static readonly PanelInstanceId Inspector = new("editor.inspector");
    public static readonly PanelInstanceId Console = new("editor.console");
}
