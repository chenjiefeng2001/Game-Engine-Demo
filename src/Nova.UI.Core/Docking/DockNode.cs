namespace Nova.UI.Core.Docking;

/// <summary>Horizontal splits place children left/right; vertical splits place them top/bottom.</summary>
public enum DockOrientation
{
    Horizontal,
    Vertical,
}

public enum DockEdge
{
    Left,
    Right,
    Top,
    Bottom,
}

public abstract class DockNode
{
    protected DockNode(DockNodeId id) => Id = id;

    public DockNodeId Id { get; }
}

/// <summary>
/// Binary split. Children and weights may be raw/invalid until the tree is
/// normalized and validated; committed trees always contain exactly two
/// non-null children with normalized weights.
/// </summary>
public sealed class DockSplitNode : DockNode
{
    public DockSplitNode(
        DockNodeId id,
        DockOrientation orientation,
        DockNode? first,
        DockNode? second,
        double firstWeight,
        double secondWeight)
        : base(id)
    {
        Orientation = orientation;
        First = first;
        Second = second;
        FirstWeight = firstWeight;
        SecondWeight = secondWeight;
    }

    public DockOrientation Orientation { get; }
    public DockNode? First { get; }
    public DockNode? Second { get; }
    public double FirstWeight { get; }
    public double SecondWeight { get; }
}

/// <summary>
/// Ordered tab group. <see cref="Panels"/> is the single authority for panel
/// identity and order; hidden panels stay in that order and only lose
/// visibility. A committed group always keeps at least one visible panel.
/// </summary>
public sealed class DockTabGroupNode : DockNode
{
    private readonly HashSet<string> _hidden;

    public DockTabGroupNode(
        DockNodeId id,
        IEnumerable<PanelInstanceId> panels,
        PanelInstanceId? activePanel,
        IEnumerable<PanelInstanceId>? hiddenPanels = null)
        : base(id)
    {
        ArgumentNullException.ThrowIfNull(panels);
        Panels = Array.AsReadOnly(panels.ToArray());

        var declared = hiddenPanels?.ToArray() ?? Array.Empty<PanelInstanceId>();
        HiddenDeclarations = Array.AsReadOnly(declared);
        _hidden = new HashSet<string>(
            declared.Select(panel => panel.Value.Value),
            StringComparer.Ordinal);

        HiddenPanels = Array.AsReadOnly(Panels.Where(IsHidden).ToArray());
        VisiblePanels = Array.AsReadOnly(Panels.Where(panel => !IsHidden(panel)).ToArray());
        ActivePanel = activePanel;
    }

    public IReadOnlyList<PanelInstanceId> Panels { get; }

    /// <summary>
    /// Hidden entries exactly as declared. Invalid entries are preserved here
    /// so validation can reject them instead of silently dropping them.
    /// </summary>
    internal IReadOnlyList<PanelInstanceId> HiddenDeclarations { get; }

    /// <summary>Hidden members in <see cref="Panels"/> order.</summary>
    public IReadOnlyList<PanelInstanceId> HiddenPanels { get; }

    public IReadOnlyList<PanelInstanceId> VisiblePanels { get; }

    public PanelInstanceId? ActivePanel { get; }

    public bool IsHidden(PanelInstanceId panel) => _hidden.Contains(panel.Value);
}
