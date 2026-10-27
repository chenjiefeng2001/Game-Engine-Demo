namespace Nova.UI.Core.Docking;

public sealed record DockNormalizationResult(
    DockNode? Root,
    bool Changed,
    IReadOnlyList<string> Diagnostics);

internal static class DockNormalizer
{
    public static DockNormalizationResult Normalize(DockNode? root)
    {
        var diagnostics = new List<string>();
        var changed = false;
        var normalized = NormalizeNode(root, diagnostics, ref changed);
        return new DockNormalizationResult(normalized, changed, diagnostics);
    }

    private static DockNode? NormalizeNode(
        DockNode? node,
        List<string> diagnostics,
        ref bool changed)
    {
        switch (node)
        {
            case null:
                return null;

            case DockTabGroupNode group:
                if (group.Panels.Count == 0)
                {
                    diagnostics.Add($"empty-group-removed:{group.Id}");
                    changed = true;
                    return null;
                }

                var active = group.ActivePanel;
                if (active is not { } current || !IsVisible(group, current))
                {
                    var replacement = FirstVisible(group);
                    if (replacement is { } visible)
                    {
                        diagnostics.Add($"active-panel-normalized:{group.Id}");
                        changed = true;
                        active = visible;
                    }
                }

                var canonicalHidden = CanonicalizeHidden(group);
                if (canonicalHidden is not null)
                {
                    diagnostics.Add($"hidden-panels-normalized:{group.Id}");
                    changed = true;
                }

                if (active == group.ActivePanel && canonicalHidden is null)
                    return group;

                return new DockTabGroupNode(
                    group.Id,
                    group.Panels,
                    active,
                    canonicalHidden ?? group.HiddenDeclarations);

            case DockSplitNode split:
                var first = NormalizeNode(split.First, diagnostics, ref changed);
                var second = NormalizeNode(split.Second, diagnostics, ref changed);

                if (first is null && second is null)
                {
                    diagnostics.Add($"empty-split-removed:{split.Id}");
                    changed = true;
                    return null;
                }

                if (first is null || second is null)
                {
                    diagnostics.Add($"split-collapsed:{split.Id}");
                    changed = true;
                    return first ?? second;
                }

                var firstWeight = split.FirstWeight;
                var secondWeight = split.SecondWeight;
                var weightsChanged = false;
                if (double.IsFinite(firstWeight) && double.IsFinite(secondWeight)
                    && firstWeight > 0 && secondWeight > 0)
                {
                    var sum = firstWeight + secondWeight;
                    if (double.IsFinite(sum)
                        && Math.Abs(sum - 1d) > DockLimits.WeightTolerance)
                    {
                        firstWeight /= sum;
                        secondWeight /= sum;
                        diagnostics.Add($"weights-normalized:{split.Id}");
                        weightsChanged = true;
                        changed = true;
                    }
                }

                if (ReferenceEquals(first, split.First)
                    && ReferenceEquals(second, split.Second)
                    && !weightsChanged)
                {
                    return split;
                }

                return new DockSplitNode(
                    split.Id,
                    split.Orientation,
                    first,
                    second,
                    firstWeight,
                    secondWeight);

            default:
                return node;
        }
    }

    private static bool Contains(IReadOnlyList<PanelInstanceId> panels, PanelInstanceId panel)
    {
        for (var index = 0; index < panels.Count; index++)
        {
            if (panels[index] == panel)
                return true;
        }

        return false;
    }

    private static bool IsVisible(DockTabGroupNode group, PanelInstanceId panel)
        => Contains(group.Panels, panel) && !group.IsHidden(panel);

    private static PanelInstanceId? FirstVisible(DockTabGroupNode group)
    {
        for (var index = 0; index < group.Panels.Count; index++)
        {
            var panel = group.Panels[index];
            if (!group.IsHidden(panel))
                return panel;
        }

        return null;
    }

    /// <summary>
    /// Returns the hidden subset in panel order, or null when declarations are
    /// already canonical or invalid (invalid ones are rejected by validation).
    /// </summary>
    private static PanelInstanceId[]? CanonicalizeHidden(DockTabGroupNode group)
    {
        if (group.HiddenDeclarations.Count == 0)
            return null;

        var declared = new HashSet<string>(StringComparer.Ordinal);
        foreach (var panel in group.HiddenDeclarations)
        {
            if (panel.IsEmpty || !Contains(group.Panels, panel) || !declared.Add(panel.Value))
                return null;
        }

        var canonical = group.Panels.Where(panel => declared.Contains(panel.Value)).ToArray();
        if (canonical.Length == group.HiddenDeclarations.Count)
        {
            var unchanged = true;
            for (var index = 0; index < canonical.Length; index++)
            {
                if (canonical[index] != group.HiddenDeclarations[index])
                {
                    unchanged = false;
                    break;
                }
            }

            if (unchanged)
                return null;
        }

        return canonical;
    }
}
