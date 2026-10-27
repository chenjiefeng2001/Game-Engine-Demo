using Nova.UI;

namespace Nova.UI.Core.Docking;

/// <summary>Stable identity for one node in the dock tree.</summary>
public readonly record struct DockNodeId(UiId Value)
{
    public DockNodeId(string value) : this(new UiId(value)) { }

    public bool IsEmpty => Value.IsEmpty;

    public override string ToString() => Value.Value ?? string.Empty;
}

/// <summary>
/// Stable identity for one panel instance. Unique panels use their panel type
/// id; duplicate instances would require a stable suffix. Titles are never ids.
/// </summary>
public readonly record struct PanelInstanceId(UiId Value)
{
    public PanelInstanceId(string value) : this(new UiId(value)) { }

    public bool IsEmpty => Value.IsEmpty;

    public override string ToString() => Value.Value ?? string.Empty;
}
