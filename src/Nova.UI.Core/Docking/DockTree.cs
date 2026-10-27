namespace Nova.UI.Core.Docking;

public sealed class DockModelException : Exception
{
    public DockModelException(string message, IReadOnlyList<DockValidationIssue>? issues = null)
        : base(message)
    {
        Issues = issues ?? Array.Empty<DockValidationIssue>();
    }

    public IReadOnlyList<DockValidationIssue> Issues { get; }
}

/// <summary>
/// Immutable main-workspace dock tree. Operations never mutate the receiver:
/// they normalize and validate a candidate, then return a new tree. A failed
/// operation returns no tree, so the current tree and digest are unchanged.
/// </summary>
public sealed class DockTree
{
    private const double WeightComparisonTolerance = 1e-9;
    private const string DefaultNodeIdPrefix = "dock";

    private readonly string _nodeIdPrefix;

    private DockTree(DockNode root, string nodeIdPrefix)
    {
        Root = root;
        _nodeIdPrefix = nodeIdPrefix;
    }

    public DockNode Root { get; }

    public string Digest => DockTreeDigest.Compute(Root);

    internal string NodeIdPrefix => _nodeIdPrefix;

    public static DockTree Create(DockNode root)
        => CreateWithPrefix(root, DefaultNodeIdPrefix);

    internal static DockTree CreateWithPrefix(DockNode root, string nodeIdPrefix)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentException.ThrowIfNullOrWhiteSpace(nodeIdPrefix);

        var validation = DockValidator.Validate(root);
        if (!validation.IsValid)
        {
            throw new DockModelException(
                $"Dock tree is invalid: {validation.Summary}",
                validation.Issues);
        }

        return new DockTree(root, nodeIdPrefix);
    }

    internal DockTree WithNodeIdPrefix(string nodeIdPrefix)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(nodeIdPrefix);
        return string.Equals(_nodeIdPrefix, nodeIdPrefix, StringComparison.Ordinal)
            ? this
            : new DockTree(Root, nodeIdPrefix);
    }

    public static DockTree CreateDefault()
    {
        var main = new DockSplitNode(
            new DockNodeId("dock.main"),
            DockOrientation.Horizontal,
            new DockTabGroupNode(
                new DockNodeId("dock.tabs.left"),
                new[] { DockPanelIds.Hierarchy },
                DockPanelIds.Hierarchy),
            new DockSplitNode(
                new DockNodeId("dock.split.center"),
                DockOrientation.Vertical,
                new DockTabGroupNode(
                    new DockNodeId("dock.tabs.scene"),
                    new[] { DockPanelIds.Scene },
                    DockPanelIds.Scene),
                new DockTabGroupNode(
                    new DockNodeId("dock.tabs.bottom"),
                    new[] { DockPanelIds.Inspector, DockPanelIds.Console },
                    DockPanelIds.Inspector),
                0.75d,
                0.25d),
            0.28d,
            0.72d);

        return Create(main);
    }

    public static DockValidationResult Validate(DockNode? root)
        => DockValidator.Validate(root);

    public static DockNormalizationResult Normalize(DockNode? root)
        => DockNormalizer.Normalize(root);

    public static string ComputeDigest(DockNode? root)
        => DockTreeDigest.Compute(root);

    public DockValidationResult Validate()
        => DockValidator.Validate(Root);

    public IEnumerable<DockNode> EnumerateNodes()
        => EnumerateNodesFrom(Root);

    public IEnumerable<PanelInstanceId> EnumeratePanels()
        => EnumerateNodes()
            .OfType<DockTabGroupNode>()
            .SelectMany(group => group.Panels);

    public IEnumerable<PanelInstanceId> EnumerateVisiblePanels()
        => EnumerateNodes()
            .OfType<DockTabGroupNode>()
            .SelectMany(group => group.VisiblePanels);

    public IEnumerable<PanelInstanceId> EnumerateHiddenPanels()
        => EnumerateNodes()
            .OfType<DockTabGroupNode>()
            .SelectMany(group => group.HiddenPanels);

    public DockTabGroupNode? FindGroup(DockNodeId groupId)
        => EnumerateNodes()
            .OfType<DockTabGroupNode>()
            .FirstOrDefault(group => group.Id == groupId);

    public DockSplitNode? FindSplit(DockNodeId splitId)
        => EnumerateNodes()
            .OfType<DockSplitNode>()
            .FirstOrDefault(split => split.Id == splitId);

    public bool TryFindPanel(PanelInstanceId panel, out DockTabGroupNode? group, out int index)
    {
        foreach (var candidate in EnumerateNodes().OfType<DockTabGroupNode>())
        {
            var panelIndex = IndexOf(candidate.Panels, panel);
            if (panelIndex < 0)
                continue;

            group = candidate;
            index = panelIndex;
            return true;
        }

        group = null;
        index = -1;
        return false;
    }

    public DockOperationResult AddPanel(PanelInstanceId instance, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);

        if (instance.IsEmpty)
            return Fail(DockError.InvalidSource, "PanelInstanceId is empty.");

        if (TryFindPanel(instance, out _, out _))
            return Fail(DockError.DuplicatePanel, $"Panel instance '{instance}' already exists.");

        switch (target)
        {
            case DockTarget.Invalid invalid:
                return Fail(DockError.InvalidTarget, invalid.Reason);

            case DockTarget.Center center:
                var targetGroup = FindGroup(center.GroupId);
                if (targetGroup is null)
                    return Fail(DockError.TargetGone, $"Target group '{center.GroupId}' does not exist.");

                var panels = targetGroup.Panels.ToList();
                var index = ClampIndex(center.Index ?? panels.Count, panels.Count);
                panels.Insert(index, instance);
                var newGroup = new DockTabGroupNode(
                    targetGroup.Id,
                    panels,
                    instance,
                    targetGroup.HiddenPanels);
                return Commit(ReplaceNode(targetGroup.Id, _ => newGroup));

            case DockTarget.Edge edge:
                if (FindGroup(edge.GroupId) is null)
                    return Fail(DockError.TargetGone, $"Target group '{edge.GroupId}' does not exist.");

                return Commit(PlaceEdge(Root, edge.GroupId, edge.Side, instance));

            default:
                return Fail(DockError.InvalidTarget, "Unsupported dock target.");
        }
    }

    public DockOperationResult MovePanel(PanelInstanceId instance, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);

        if (instance.IsEmpty || !TryFindPanel(instance, out var sourceGroup, out var sourceIndex))
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in the dock tree.");

        if (sourceGroup!.IsHidden(instance))
        {
            return Fail(
                DockError.PanelHidden,
                $"Panel instance '{instance}' is hidden and cannot be moved.");
        }

        switch (target)
        {
            case DockTarget.Invalid invalid:
                return Fail(DockError.InvalidTarget, invalid.Reason);

            case DockTarget.Center center:
                var targetGroup = FindGroup(center.GroupId);
                if (targetGroup is null)
                    return Fail(DockError.TargetGone, $"Target group '{center.GroupId}' does not exist.");

                if (sourceGroup!.Id == targetGroup.Id)
                {
                    var remaining = sourceGroup.Panels.ToList();
                    remaining.RemoveAt(sourceIndex);
                    var index = ClampIndex(center.Index ?? remaining.Count, remaining.Count);
                    if (index == sourceIndex)
                        return Fail(DockError.AlreadyInState, "Panel is already at the requested position.");

                    remaining.Insert(index, instance);
                    var sameGroup = new DockTabGroupNode(
                        sourceGroup.Id,
                        remaining,
                        instance,
                        sourceGroup.HiddenPanels);
                    return Commit(ReplaceNode(sourceGroup.Id, _ => sameGroup));
                }
                else
                {
                    if (WouldLeaveNoVisiblePanel(sourceGroup, instance))
                    {
                        return Fail(
                            DockError.WouldEmptyGroup,
                            $"Moving '{instance}' would leave group '{sourceGroup.Id}' without a visible panel.");
                    }

                    var rootAfterRemoval = RemovePanelCore(Root, instance, out _);
                    var survivor = FindGroupIn(rootAfterRemoval, targetGroup.Id);
                    if (survivor is null)
                    {
                        return Fail(
                            DockError.TargetGone,
                            $"Target group '{center.GroupId}' disappeared during the move.");
                    }

                    var panels = survivor.Panels.ToList();
                    var index = ClampIndex(center.Index ?? panels.Count, panels.Count);
                    panels.Insert(index, instance);
                    var newGroup = new DockTabGroupNode(
                        survivor.Id,
                        panels,
                        instance,
                        survivor.HiddenPanels);
                    var root = ReplaceNodeIn(rootAfterRemoval, survivor.Id, _ => newGroup);
                    return Commit(root);
                }

            case DockTarget.Edge edge:
                var edgeTarget = FindGroup(edge.GroupId);
                if (edgeTarget is null)
                    return Fail(DockError.TargetGone, $"Target group '{edge.GroupId}' does not exist.");

                if (sourceGroup!.Id == edgeTarget.Id && sourceGroup.Panels.Count == 1)
                {
                    return Fail(
                        DockError.AlreadyInState,
                        "A panel cannot be split away from itself.");
                }

                if (WouldLeaveNoVisiblePanel(sourceGroup, instance))
                {
                    return Fail(
                        DockError.WouldEmptyGroup,
                        $"Moving '{instance}' would leave group '{sourceGroup.Id}' without a visible panel.");
                }

                var rootAfterEdgeRemoval = RemovePanelCore(Root, instance, out _);
                return Commit(PlaceEdge(rootAfterEdgeRemoval, edge.GroupId, edge.Side, instance));

            default:
                return Fail(DockError.InvalidTarget, "Unsupported dock target.");
        }
    }

    public DockOperationResult ReorderTab(PanelInstanceId instance, DockNodeId groupId, int index)
    {
        var group = FindGroup(groupId);
        if (group is null)
            return Fail(DockError.TargetGone, $"Target group '{groupId}' does not exist.");

        var sourceIndex = IndexOf(group.Panels, instance);
        if (sourceIndex < 0)
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in group '{groupId}'.");

        if (group.IsHidden(instance))
        {
            return Fail(
                DockError.PanelHidden,
                $"Panel instance '{instance}' is hidden and cannot be reordered.");
        }

        var targetIndex = ClampIndex(index, group.Panels.Count - 1);
        if (targetIndex == sourceIndex)
            return Fail(DockError.AlreadyInState, "Panel is already at the requested index.");

        var panels = group.Panels.ToList();
        panels.RemoveAt(sourceIndex);
        panels.Insert(targetIndex, instance);
        var newGroup = new DockTabGroupNode(
            group.Id,
            panels,
            group.ActivePanel,
            group.HiddenPanels);
        return Commit(ReplaceNode(group.Id, _ => newGroup));
    }

    public DockOperationResult ActivateTab(DockNodeId groupId, PanelInstanceId instance)
    {
        var group = FindGroup(groupId);
        if (group is null)
            return Fail(DockError.TargetGone, $"Target group '{groupId}' does not exist.");

        if (IndexOf(group.Panels, instance) < 0)
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in group '{groupId}'.");

        if (group.IsHidden(instance))
            return Fail(DockError.PanelHidden, $"Panel instance '{instance}' is hidden.");

        if (group.ActivePanel == instance)
            return Fail(DockError.AlreadyInState, "Panel is already active.");

        var newGroup = new DockTabGroupNode(
            group.Id,
            group.Panels,
            instance,
            group.HiddenPanels);
        return Commit(ReplaceNode(group.Id, _ => newGroup));
    }

    public DockOperationResult HidePanel(PanelInstanceId instance)
    {
        if (instance.IsEmpty || !TryFindPanel(instance, out var group, out var index))
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in the dock tree.");

        if (group!.IsHidden(instance))
            return Fail(DockError.AlreadyInState, $"Panel instance '{instance}' is already hidden.");

        if (group.VisiblePanels.Count == 1)
        {
            return Fail(
                DockError.WouldEmptyGroup,
                $"Hiding '{instance}' would leave group '{group.Id}' without a visible panel.");
        }

        var hidden = group.Panels
            .Where(panel => panel == instance || group.IsHidden(panel))
            .ToArray();
        var active = group.ActivePanel == instance
            ? FindReplacementVisible(group, index)
            : group.ActivePanel;
        var newGroup = new DockTabGroupNode(group.Id, group.Panels, active, hidden);
        return Commit(ReplaceNode(group.Id, _ => newGroup));
    }

    public DockOperationResult ShowPanel(PanelInstanceId instance)
    {
        if (instance.IsEmpty || !TryFindPanel(instance, out var group, out _))
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in the dock tree.");

        if (!group!.IsHidden(instance))
            return Fail(DockError.AlreadyInState, $"Panel instance '{instance}' is not hidden.");

        var hidden = group.Panels
            .Where(panel => group.IsHidden(panel) && panel != instance)
            .ToArray();
        var newGroup = new DockTabGroupNode(
            group.Id,
            group.Panels,
            group.ActivePanel,
            hidden);
        return Commit(ReplaceNode(group.Id, _ => newGroup));
    }

    public DockOperationResult RemovePanel(PanelInstanceId instance)
    {
        if (instance.IsEmpty || !TryFindPanel(instance, out var group, out _))
            return Fail(DockError.InvalidSource, $"Panel instance '{instance}' is not in the dock tree.");

        if (WouldLeaveNoVisiblePanel(group!, instance))
        {
            return Fail(
                DockError.WouldEmptyGroup,
                $"Closing '{instance}' would leave group '{group!.Id}' without a visible panel.");
        }

        var root = RemovePanelCore(Root, instance, out _);
        return Commit(root);
    }

    public DockOperationResult MergeGroups(DockNodeId sourceGroupId, DockNodeId targetGroupId)
    {
        var sourceGroup = FindGroup(sourceGroupId);
        if (sourceGroup is null)
            return Fail(DockError.TargetGone, $"Source group '{sourceGroupId}' does not exist.");

        var targetGroup = FindGroup(targetGroupId);
        if (targetGroup is null)
            return Fail(DockError.TargetGone, $"Target group '{targetGroupId}' does not exist.");

        if (sourceGroup.Id == targetGroup.Id)
            return Fail(DockError.AlreadyInState, "A group cannot be merged into itself.");

        var mergedPanels = targetGroup.Panels.Concat(sourceGroup.Panels).ToArray();
        var mergedHidden = targetGroup.HiddenPanels.Concat(sourceGroup.HiddenPanels).ToArray();
        var mergedTarget = new DockTabGroupNode(
            targetGroup.Id,
            mergedPanels,
            targetGroup.ActivePanel,
            mergedHidden);
        var root = ReplaceNode(targetGroup.Id, _ => mergedTarget);

        var emptySource = new DockTabGroupNode(
            sourceGroup.Id,
            Array.Empty<PanelInstanceId>(),
            activePanel: null);
        root = ReplaceNodeIn(root, sourceGroup.Id, _ => emptySource);
        return Commit(root);
    }

    internal DockOperationResult AppendPanels(
        DockNodeId targetGroupId,
        IReadOnlyList<PanelInstanceId> panels,
        IReadOnlyList<PanelInstanceId>? hiddenPanels = null)
    {
        ArgumentNullException.ThrowIfNull(panels);

        var targetGroup = FindGroup(targetGroupId);
        if (targetGroup is null)
            return Fail(DockError.TargetGone, $"Target group '{targetGroupId}' does not exist.");

        if (panels.Count == 0)
            return Fail(DockError.AlreadyInState, "No panels to append.");

        var local = new HashSet<string>(StringComparer.Ordinal);
        foreach (var panel in panels)
        {
            if (panel.IsEmpty)
                return Fail(DockError.InvalidModel, "PanelInstanceId is empty.");

            if (!local.Add(panel.ToString()))
                return Fail(DockError.DuplicatePanel, $"Panel instance '{panel}' appears twice.");

            if (TryFindPanel(panel, out _, out _))
            {
                return Fail(
                    DockError.DuplicatePanel,
                    $"Panel instance '{panel}' already exists in the tree.");
            }
        }

        var hidden = hiddenPanels ?? Array.Empty<PanelInstanceId>();
        var hiddenIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var panel in hidden)
        {
            if (panel.IsEmpty || !local.Contains(panel.ToString()))
            {
                return Fail(
                    DockError.InvalidModel,
                    $"Hidden panel '{panel}' is not among the appended panels.");
            }

            if (!hiddenIds.Add(panel.ToString()))
            {
                return Fail(
                    DockError.InvalidModel,
                    $"Hidden panel '{panel}' appears twice.");
            }
        }

        var combined = targetGroup.Panels.Concat(panels).ToArray();
        var combinedHidden = targetGroup.HiddenPanels.Concat(hidden).ToArray();
        var newGroup = new DockTabGroupNode(
            targetGroup.Id,
            combined,
            targetGroup.ActivePanel,
            combinedHidden);
        return Commit(ReplaceNode(targetGroup.Id, _ => newGroup));
    }

    public DockOperationResult ResizeSplit(
        DockNodeId splitId,
        double firstWeight,
        double secondWeight)
    {
        var split = FindSplit(splitId);
        if (split is null)
            return Fail(DockError.InvalidTarget, $"Split '{splitId}' does not exist.");

        if (!double.IsFinite(firstWeight) || !double.IsFinite(secondWeight)
            || firstWeight <= 0 || secondWeight <= 0)
        {
            return Fail(DockError.InvalidModel, "Split weights must be finite and positive.");
        }

        var sum = firstWeight + secondWeight;
        if (!double.IsFinite(sum))
            return Fail(DockError.InvalidModel, "Split weight sum is not finite.");

        var normalizedFirst = firstWeight / sum;
        var normalizedSecond = secondWeight / sum;
        if (Math.Abs(split.FirstWeight - normalizedFirst) <= WeightComparisonTolerance
            && Math.Abs(split.SecondWeight - normalizedSecond) <= WeightComparisonTolerance)
        {
            return Fail(DockError.AlreadyInState, "Split weights are unchanged.");
        }

        var resized = new DockSplitNode(
            split.Id,
            split.Orientation,
            split.First,
            split.Second,
            normalizedFirst,
            normalizedSecond);
        return Commit(ReplaceNode(split.Id, _ => resized));
    }

    private DockOperationResult Commit(DockNode? root, string diagnostic = "")
    {
        if (root is null)
        {
            return Fail(
                DockError.InvalidModel,
                "The operation would leave no dockable root.");
        }

        var normalization = DockNormalizer.Normalize(root);
        if (normalization.Root is null)
        {
            return Fail(
                DockError.InvalidModel,
                "The operation would leave no dockable root.");
        }

        var candidate = new DockTree(normalization.Root, _nodeIdPrefix);
        var validation = candidate.Validate();
        if (!validation.IsValid)
        {
            var error = validation.Has("empty-visible")
                ? DockError.WouldEmptyGroup
                : DockError.InvalidModel;
            return Fail(
                error,
                $"The operation produced an invalid tree: {validation.Summary}");
        }

        var diagnostics = new List<string>();
        if (!string.IsNullOrEmpty(diagnostic))
            diagnostics.Add(diagnostic);
        diagnostics.AddRange(normalization.Diagnostics);
        return DockOperationResult.Ok(candidate, string.Join("; ", diagnostics));
    }

    private DockNode ReplaceNode(DockNodeId nodeId, Func<DockNode, DockNode> replacement)
        => ReplaceNodeIn(Root, nodeId, replacement);

    private DockNode PlaceEdge(
        DockNode root,
        DockNodeId targetGroupId,
        DockEdge side,
        PanelInstanceId instance)
    {
        var newGroupId = AllocateNodeId(root, $"{_nodeIdPrefix}.tabs");
        var splitId = AllocateNodeId(root, $"{_nodeIdPrefix}.split");
        var newGroup = new DockTabGroupNode(newGroupId, new[] { instance }, instance);
        var orientation = side is DockEdge.Left or DockEdge.Right
            ? DockOrientation.Horizontal
            : DockOrientation.Vertical;
        var newGroupFirst = side is DockEdge.Left or DockEdge.Top;

        return ReplaceNodeIn(root, targetGroupId, existing =>
            newGroupFirst
                ? new DockSplitNode(splitId, orientation, newGroup, existing, 0.5d, 0.5d)
                : new DockSplitNode(splitId, orientation, existing, newGroup, 0.5d, 0.5d));
    }

    private static DockNode ReplaceNodeIn(
        DockNode root,
        DockNodeId nodeId,
        Func<DockNode, DockNode> replacement)
    {
        var replaced = false;
        var result = ReplaceNodeCore(root, nodeId, replacement, ref replaced);
        if (!replaced)
            throw new InvalidOperationException($"Dock node '{nodeId}' was not found in the tree.");

        return result;
    }

    private static DockNode ReplaceNodeCore(
        DockNode node,
        DockNodeId nodeId,
        Func<DockNode, DockNode> replacement,
        ref bool replaced)
    {
        if (node.Id == nodeId)
        {
            replaced = true;
            return replacement(node);
        }

        if (node is not DockSplitNode split)
            return node;

        var first = ReplaceNodeCore(split.First!, nodeId, replacement, ref replaced);
        if (replaced)
        {
            return new DockSplitNode(
                split.Id,
                split.Orientation,
                first,
                split.Second,
                split.FirstWeight,
                split.SecondWeight);
        }

        var second = ReplaceNodeCore(split.Second!, nodeId, replacement, ref replaced);
        if (replaced)
        {
            return new DockSplitNode(
                split.Id,
                split.Orientation,
                split.First,
                second,
                split.FirstWeight,
                split.SecondWeight);
        }

        return node;
    }

    private static DockNode RemovePanelCore(
        DockNode node,
        PanelInstanceId instance,
        out bool removed)
    {
        switch (node)
        {
            case DockTabGroupNode group:
                var index = IndexOf(group.Panels, instance);
                if (index < 0)
                {
                    removed = false;
                    return group;
                }

                removed = true;
                var remaining = new PanelInstanceId[group.Panels.Count - 1];
                var target = 0;
                for (var source = 0; source < group.Panels.Count; source++)
                {
                    if (source != index)
                        remaining[target++] = group.Panels[source];
                }

                var active = group.ActivePanel == instance ? null : group.ActivePanel;
                var hidden = group.Panels
                    .Where(panel => panel != instance && group.IsHidden(panel))
                    .ToArray();
                return new DockTabGroupNode(group.Id, remaining, active, hidden);

            case DockSplitNode split:
                var first = RemovePanelCore(split.First!, instance, out removed);
                if (removed)
                {
                    return new DockSplitNode(
                        split.Id,
                        split.Orientation,
                        first,
                        split.Second,
                        split.FirstWeight,
                        split.SecondWeight);
                }

                var second = RemovePanelCore(split.Second!, instance, out removed);
                if (removed)
                {
                    return new DockSplitNode(
                        split.Id,
                        split.Orientation,
                        split.First,
                        second,
                        split.FirstWeight,
                        split.SecondWeight);
                }

                return split;

            default:
                removed = false;
                return node;
        }
    }

    private static DockTabGroupNode? FindGroupIn(DockNode? root, DockNodeId groupId)
    {
        if (root is null)
            return null;

        return EnumerateNodesFrom(root)
            .OfType<DockTabGroupNode>()
            .FirstOrDefault(group => group.Id == groupId);
    }

    private static DockNodeId AllocateNodeId(DockNode root, string prefix)
    {
        var used = new HashSet<string>(
            EnumerateNodesFrom(root).Select(node => node.Id.ToString()),
            StringComparer.Ordinal);

        var ordinal = 1;
        while (!used.Add($"{prefix}.{ordinal}"))
            ordinal++;

        return new DockNodeId($"{prefix}.{ordinal}");
    }

    private static IEnumerable<DockNode> EnumerateNodesFrom(DockNode root)
    {
        var stack = new Stack<DockNode>();
        stack.Push(root);
        while (stack.Count > 0)
        {
            var node = stack.Pop();
            yield return node;

            if (node is DockSplitNode split)
            {
                if (split.Second is not null)
                    stack.Push(split.Second);
                if (split.First is not null)
                    stack.Push(split.First);
            }
        }
    }

    private static int IndexOf(IReadOnlyList<PanelInstanceId> panels, PanelInstanceId instance)
    {
        for (var index = 0; index < panels.Count; index++)
        {
            if (panels[index] == instance)
                return index;
        }

        return -1;
    }

    private static bool WouldLeaveNoVisiblePanel(DockTabGroupNode group, PanelInstanceId instance)
        => group.Panels.Count > 1
           && !group.IsHidden(instance)
           && group.VisiblePanels.Count == 1;

    private static PanelInstanceId? FindReplacementVisible(DockTabGroupNode group, int hiddenIndex)
    {
        for (var index = hiddenIndex + 1; index < group.Panels.Count; index++)
        {
            if (!group.IsHidden(group.Panels[index]))
                return group.Panels[index];
        }

        for (var index = hiddenIndex - 1; index >= 0; index--)
        {
            if (!group.IsHidden(group.Panels[index]))
                return group.Panels[index];
        }

        return null;
    }

    private static int ClampIndex(int index, int max)
        => max <= 0 ? 0 : Math.Clamp(index, 0, max);

    private static DockOperationResult Fail(DockError error, string diagnostic)
        => DockOperationResult.Fail(error, diagnostic);
}
