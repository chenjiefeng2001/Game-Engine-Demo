namespace Nova.UI.Core.Docking;

internal static class DockValidator
{
    public static DockValidationResult Validate(DockNode? root)
    {
        var issues = new List<DockValidationIssue>();
        var issueCodes = new HashSet<string>(StringComparer.Ordinal);

        void Add(string code, string message)
        {
            if (issueCodes.Add(code))
                issues.Add(new DockValidationIssue(code, message));
        }

        if (root is null)
        {
            Add("root-missing", "The dock tree must have a root node.");
            return new DockValidationResult(issues);
        }

        var nodeIds = new HashSet<string>(StringComparer.Ordinal);
        var panelIds = new HashSet<string>(StringComparer.Ordinal);
        var visited = new HashSet<DockNode>(ReferenceEqualityComparer.Instance);
        var nodeCount = 0;
        var panelCount = 0;

        var stack = new Stack<(DockNode Node, int Depth)>();
        stack.Push((root, 1));

        while (stack.Count > 0)
        {
            var (node, depth) = stack.Pop();

            if (!visited.Add(node))
            {
                Add("shared-node", "A DockNode instance appears more than once in the tree.");
                continue;
            }

            nodeCount++;
            if (nodeCount > DockLimits.MaxNodes)
                Add("node-limit", $"Dock tree nodes exceed {DockLimits.MaxNodes}.");

            if (depth > DockLimits.MaxDepth)
                Add("depth-limit", $"Dock tree depth exceeds {DockLimits.MaxDepth}.");

            if (node.Id.IsEmpty)
                Add("invalid-node-id", "A DockNode has an empty DockNodeId.");
            else if (!nodeIds.Add(node.Id.Value))
                Add("duplicate-node-id", $"Duplicate DockNodeId '{node.Id}'.");

            switch (node)
            {
                case DockTabGroupNode group:
                    ValidateGroup(group, Add, panelIds, ref panelCount);
                    break;

                case DockSplitNode split:
                    ValidateSplit(split, Add);
                    if (split.First is not null)
                        stack.Push((split.First, depth + 1));
                    if (split.Second is not null)
                        stack.Push((split.Second, depth + 1));
                    break;

                default:
                    Add("unknown-node", $"Unsupported DockNode type '{node.GetType().Name}'.");
                    break;
            }
        }

        if (panelCount > DockLimits.MaxPanels)
            Add("panel-limit", $"Dock tree panels exceed {DockLimits.MaxPanels}.");

        return new DockValidationResult(issues);
    }

    private static void ValidateGroup(
        DockTabGroupNode group,
        Action<string, string> add,
        HashSet<string> panelIds,
        ref int panelCount)
    {
        if (group.Panels.Count == 0)
        {
            add("empty-group", $"Tab group '{group.Id}' is empty.");
            return;
        }

        var localPanels = new HashSet<string>(StringComparer.Ordinal);
        foreach (var panel in group.Panels)
        {
            if (panel.IsEmpty)
            {
                add("invalid-panel-id", $"Tab group '{group.Id}' contains an empty PanelInstanceId.");
                continue;
            }

            if (!localPanels.Add(panel.Value))
                add("duplicate-panel", $"Tab group '{group.Id}' contains duplicate panel '{panel}'.");

            if (!panelIds.Add(panel.Value))
                add("duplicate-panel", $"Duplicate PanelInstanceId '{panel}'.");

            panelCount++;
        }

        if (group.ActivePanel is null || !localPanels.Contains(group.ActivePanel.Value.Value))
            add("invalid-active", $"Tab group '{group.Id}' has an active panel outside its panels.");

        var hiddenPanels = new HashSet<string>(StringComparer.Ordinal);
        foreach (var hidden in group.HiddenDeclarations)
        {
            if (hidden.IsEmpty)
            {
                add(
                    "invalid-hidden",
                    $"Tab group '{group.Id}' declares an empty hidden PanelInstanceId.");
                continue;
            }

            if (!localPanels.Contains(hidden.Value.Value))
            {
                add(
                    "invalid-hidden",
                    $"Tab group '{group.Id}' hides '{hidden}', which is not one of its panels.");
            }

            if (!hiddenPanels.Add(hidden.Value.Value))
            {
                add(
                    "duplicate-hidden",
                    $"Tab group '{group.Id}' hides '{hidden}' more than once.");
            }
        }

        if (group.ActivePanel is { } active && hiddenPanels.Contains(active.Value.Value))
        {
            add(
                "hidden-active",
                $"Tab group '{group.Id}' has a hidden active panel '{active}'.");
        }

        if (group.VisiblePanels.Count == 0)
        {
            add(
                "empty-visible",
                $"Tab group '{group.Id}' has no visible panels.");
        }
    }

    private static void ValidateSplit(DockSplitNode split, Action<string, string> add)
    {
        if (split.First is null || split.Second is null)
            add("invalid-split", $"Split '{split.Id}' does not have exactly two children.");

        if (!IsValidWeight(split.FirstWeight) || !IsValidWeight(split.SecondWeight))
        {
            add("invalid-weight", $"Split '{split.Id}' has a non-positive or non-finite weight.");
            return;
        }

        if (Math.Abs(split.FirstWeight + split.SecondWeight - 1d) > DockLimits.WeightTolerance)
            add("invalid-weight", $"Split '{split.Id}' weights do not sum to 1.");
    }

    private static bool IsValidWeight(double weight)
        => double.IsFinite(weight) && weight > 0;
}
