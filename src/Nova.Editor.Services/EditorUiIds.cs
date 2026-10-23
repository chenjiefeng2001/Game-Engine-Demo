using Nova.UI;

namespace Nova.Editor.Services;

/// <summary>Stable projection IDs. They are serializable and do not depend on array positions.</summary>
public static class EditorUiIds
{
    public static readonly UiId InspectorHeader = new("inspector.header");
    public static readonly UiId InspectorType = new("inspector.type");
    public static readonly UiId SelectionStatus = new("status.selection");
    public static readonly UiId HierarchyCount = new("hierarchy.count");
    public static readonly UiId ConsoleLast = new("console.last");
    public static readonly UiId ViewportStatus = new("viewport.status");
}
