namespace Nova.UI.Core.Docking;

internal static class DockWorkspaceValidator
{
    public static DockValidationResult Validate(
        DockTree? mainRoot,
        IReadOnlyList<DockFloatRoot> floatingRoots)
    {
        var issues = new List<DockValidationIssue>();
        var codes = new HashSet<string>(StringComparer.Ordinal);

        void Add(string code, string message)
        {
            if (codes.Add(code))
                issues.Add(new DockValidationIssue(code, message));
        }

        if (mainRoot is null)
        {
            Add("root-missing", "The workspace must have a main dock root.");
            return new DockValidationResult(issues);
        }

        var nodeIds = new HashSet<string>(StringComparer.Ordinal);
        var panelIds = new HashSet<string>(StringComparer.Ordinal);
        var floatIds = new HashSet<string>(StringComparer.Ordinal);
        var totalNodes = 0;
        var totalPanels = 0;

        Accumulate("main", mainRoot, Add, nodeIds, panelIds, ref totalNodes, ref totalPanels);

        foreach (var root in floatingRoots)
        {
            if (root is null)
            {
                Add("invalid-float-root", "A floating root entry is null.");
                continue;
            }

            if (root.Id.IsEmpty)
            {
                Add("invalid-float-root-id", "A floating root has an empty DockFloatRootId.");
            }
            else if (!floatIds.Add(root.Id.ToString()))
            {
                Add("duplicate-float-root-id", $"Duplicate DockFloatRootId '{root.Id}'.");
            }

            if (!root.Bounds.IsValid)
                Add("invalid-bounds", $"Floating root '{root.Id}' has invalid bounds.");

            if (!root.RestoreBounds.IsValid)
                Add("invalid-restore-bounds", $"Floating root '{root.Id}' has invalid restore bounds.");

            Accumulate($"float '{root.Id}'", root.Root, Add, nodeIds, panelIds,
                ref totalNodes, ref totalPanels);
        }

        if (floatingRoots.Count > DockLimits.MaxFloatRoots)
            Add("float-limit", $"Floating roots exceed {DockLimits.MaxFloatRoots}.");

        if (totalNodes > DockLimits.MaxNodes)
            Add("node-limit", $"Workspace dock nodes exceed {DockLimits.MaxNodes}.");

        if (totalPanels > DockLimits.MaxPanels)
            Add("panel-limit", $"Workspace dock panels exceed {DockLimits.MaxPanels}.");

        return new DockValidationResult(issues);
    }

    private static void Accumulate(
        string label,
        DockTree tree,
        Action<string, string> add,
        HashSet<string> nodeIds,
        HashSet<string> panelIds,
        ref int totalNodes,
        ref int totalPanels)
    {
        foreach (var issue in DockValidator.Validate(tree.Root).Issues)
            add(issue.Code, $"{label}: {issue.Message}");

        foreach (var node in tree.EnumerateNodes())
        {
            totalNodes++;
            if (!nodeIds.Add(node.Id.ToString()))
            {
                add(
                    "duplicate-node-id",
                    $"DockNodeId '{node.Id}' appears more than once in the workspace.");
            }
        }

        foreach (var panel in tree.EnumeratePanels())
        {
            totalPanels++;
            if (!panelIds.Add(panel.ToString()))
            {
                add(
                    "duplicate-panel",
                    $"PanelInstanceId '{panel}' appears more than once in the workspace.");
            }
        }
    }
}
