using System.Collections.ObjectModel;

namespace Nova.UI.Core.Docking;

/// <summary>
/// Immutable multi-root workspace. Operations build candidate roots, normalize
/// and validate them, then replace the whole workspace; failures return no
/// workspace and leave the receiver byte-for-byte unchanged.
/// </summary>
public sealed class DockWorkspace
{
    private readonly ReadOnlyCollection<DockFloatRoot> _floatingRoots;

    private DockWorkspace(
        DockTree mainRoot,
        IReadOnlyList<DockFloatRoot> floatingRoots,
        long revision)
    {
        MainRoot = mainRoot;
        _floatingRoots = Array.AsReadOnly(floatingRoots.ToArray());
        Revision = revision;
        Digest = DockWorkspaceDigest.Compute(MainRoot, _floatingRoots);
    }

    public long Revision { get; }

    public DockTree MainRoot { get; }

    /// <summary>Floating roots sorted by <see cref="DockFloatRootId"/>.</summary>
    public IReadOnlyList<DockFloatRoot> FloatingRoots => _floatingRoots;

    public string Digest { get; }

    public static DockWorkspace CreateDefault()
        => new(DockTree.CreateDefault(), Array.Empty<DockFloatRoot>(), 0);

    public static DockWorkspace Create(
        DockTree mainRoot,
        IEnumerable<DockFloatRoot>? floatingRoots = null,
        long revision = 0)
    {
        ArgumentNullException.ThrowIfNull(mainRoot);
        if (revision < 0)
            throw new ArgumentOutOfRangeException(nameof(revision), "Revision must be non-negative.");

        var floats = SortFloats(floatingRoots);
        var validation = DockWorkspaceValidator.Validate(mainRoot, floats);
        if (!validation.IsValid)
        {
            throw new DockModelException(
                $"Dock workspace is invalid: {validation.Summary}",
                validation.Issues);
        }

        return new DockWorkspace(mainRoot, floats, revision);
    }

    public static DockValidationResult Validate(
        DockTree? mainRoot,
        IEnumerable<DockFloatRoot>? floatingRoots = null)
    {
        var floats = floatingRoots?.ToArray() ?? Array.Empty<DockFloatRoot>();
        return DockWorkspaceValidator.Validate(mainRoot, floats);
    }

    public DockValidationResult Validate()
        => DockWorkspaceValidator.Validate(MainRoot, FloatingRoots);

    public IEnumerable<DockNode> EnumerateNodes()
        => MainRoot.EnumerateNodes()
            .Concat(FloatingRoots.SelectMany(root => root.Root.EnumerateNodes()));

    public IEnumerable<PanelInstanceId> EnumeratePanels()
        => MainRoot.EnumeratePanels()
            .Concat(FloatingRoots.SelectMany(root => root.Root.EnumeratePanels()));

    public IEnumerable<PanelInstanceId> EnumerateVisiblePanels()
        => MainRoot.EnumerateVisiblePanels()
            .Concat(FloatingRoots.SelectMany(root => root.Root.EnumerateVisiblePanels()));

    public IEnumerable<PanelInstanceId> EnumerateHiddenPanels()
        => MainRoot.EnumerateHiddenPanels()
            .Concat(FloatingRoots.SelectMany(root => root.Root.EnumerateHiddenPanels()));

    public DockTabGroupNode? FindGroup(DockNodeId groupId)
    {
        var group = MainRoot.FindGroup(groupId);
        if (group is not null)
            return group;

        foreach (var root in FloatingRoots)
        {
            group = root.Root.FindGroup(groupId);
            if (group is not null)
                return group;
        }

        return null;
    }

    public bool TryLocatePanel(PanelInstanceId panel, out DockWorkspaceLocation location)
    {
        if (MainRoot.TryFindPanel(panel, out var group, out var index))
        {
            location = new DockWorkspaceLocation(null, group!.Id, index);
            return true;
        }

        foreach (var root in FloatingRoots)
        {
            if (root.Root.TryFindPanel(panel, out group, out index))
            {
                location = new DockWorkspaceLocation(root.Id, group!.Id, index);
                return true;
            }
        }

        location = default;
        return false;
    }

    public DockWorkspaceResult FloatPanel(PanelInstanceId panel, DockBounds bounds)
    {
        if (panel.IsEmpty)
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidSource,
                "PanelInstanceId is empty.");

        if (!MainRoot.TryFindPanel(panel, out var sourceGroup, out var sourceIndex))
        {
            var diagnostic = TryLocatePanel(panel, out _)
                ? $"Panel '{panel}' is already floating; only main-root panels can be floated in D6-B."
                : $"Panel '{panel}' is not in the workspace.";
            return DockWorkspaceResult.Fail(DockWorkspaceError.InvalidSource, diagnostic);
        }

        if (sourceGroup!.IsHidden(panel))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.PanelHidden,
                $"Panel '{panel}' is hidden and cannot be floated.");
        }

        if (!bounds.IsValid)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidModel,
                "Floating bounds must be finite, within +/-32768, and have positive width and height.");
        }

        if (FloatingRoots.Count >= DockLimits.MaxFloatRoots)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.LimitExceeded,
                $"The workspace already has {DockLimits.MaxFloatRoots} floating roots.");
        }

        if (MainRoot.EnumeratePanels().Count() == 1)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.WouldEmptyMainRoot,
                "Floating the last main-root panel would leave the main root empty.");
        }

        var removal = MainRoot.RemovePanel(panel);
        if (!removal.Success || removal.Tree is null)
            return MapSingleRootFailure(removal);

        var floatId = AllocateFloatRootId();
        var floatGroup = new DockTabGroupNode(
            new DockNodeId($"{floatId}.tabs.1"),
            new[] { panel },
            panel);

        DockTree floatTree;
        try
        {
            floatTree = DockTree.CreateWithPrefix(floatGroup, floatId.ToString());
        }
        catch (DockModelException ex)
        {
            return DockWorkspaceResult.Fail(DockWorkspaceError.InvalidModel, ex.Message);
        }

        var floatRoot = new DockFloatRoot(
            floatId,
            floatTree,
            bounds,
            maximized: false,
            new DockReturnTarget(sourceGroup!.Id, sourceIndex));

        var floats = FloatingRoots.ToList();
        floats.Add(floatRoot);
        return CommitWorkspace(
            removal.Tree,
            floats,
            $"Floated panel '{panel}' into '{floatId}'.");
    }

    public DockWorkspaceResult MovePanel(PanelInstanceId panel, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);

        if (panel.IsEmpty || !TryLocatePanel(panel, out var source))
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidSource,
                $"Panel '{panel}' is not in the workspace.");

        if (FindGroup(source.GroupId) is { } sourceGroup && sourceGroup.IsHidden(panel))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.PanelHidden,
                $"Panel '{panel}' is hidden and cannot be moved.");
        }

        switch (target)
        {
            case DockTarget.Invalid invalid:
                return DockWorkspaceResult.Fail(DockWorkspaceError.InvalidTarget, invalid.Reason);

            case DockTarget.Float floatTarget:
                if (!source.IsMain)
                {
                    return DockWorkspaceResult.Fail(
                        DockWorkspaceError.InvalidSource,
                        "Only main-root panels can be floated in D6-B.");
                }

                return FloatPanel(panel, floatTarget.Bounds);

            case DockTarget.Center center:
                return MoveToGroup(panel, source, center.GroupId, center.Index, edgeSide: null);

            case DockTarget.Edge edge:
                return MoveToGroup(panel, source, edge.GroupId, index: null, edge.Side);

            default:
                return DockWorkspaceResult.Fail(
                    DockWorkspaceError.InvalidTarget,
                    "Unsupported dock target.");
        }
    }

    public DockWorkspaceResult ReDockFloatRoot(DockFloatRootId rootId)
    {
        var floatRoot = FindFloatRoot(rootId);
        if (floatRoot is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{rootId}' does not exist.");
        }

        var target = ResolveReturnGroup(floatRoot);
        return MergeFloatRoot(rootId, new DockTarget.Center(target.Id));
    }

    public DockWorkspaceResult MergeFloatRoot(DockFloatRootId rootId, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);

        var floatRoot = FindFloatRoot(rootId);
        if (floatRoot is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{rootId}' does not exist.");
        }

        if (target is DockTarget.Invalid invalid)
            return DockWorkspaceResult.Fail(DockWorkspaceError.InvalidTarget, invalid.Reason);

        if (target is not DockTarget.Center center)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidTarget,
                "Merging a floating root requires a center target in the main root.");
        }

        if (FloatingRoots.Any(root => root.Root.FindGroup(center.GroupId) is not null))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidTarget,
                $"Target group '{center.GroupId}' does not belong to the main root.");
        }

        if (MainRoot.FindGroup(center.GroupId) is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Target group '{center.GroupId}' does not exist in the main root.");
        }

        var panels = floatRoot.Root.EnumeratePanels().ToArray();
        var hidden = floatRoot.Root.EnumerateHiddenPanels().ToArray();
        var append = MainRoot.AppendPanels(center.GroupId, panels, hidden);
        if (!append.Success || append.Tree is null)
            return MapSingleRootFailure(append);

        var floats = FloatingRoots.Where(root => root.Id != rootId).ToArray();
        return CommitWorkspace(
            append.Tree,
            floats,
            $"Merged floating root '{rootId}' into '{center.GroupId}'.");
    }

    public DockWorkspaceResult CloseFloatRoot(DockFloatRootId rootId)
    {
        if (FindFloatRoot(rootId) is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{rootId}' does not exist.");
        }

        var floats = FloatingRoots.Where(root => root.Id != rootId).ToArray();
        return CommitWorkspace(
            MainRoot,
            floats,
            $"Closed floating root '{rootId}'.");
    }

    public DockWorkspaceResult UpdateFloatBounds(
        DockFloatRootId rootId,
        DockBounds bounds,
        bool maximized)
    {
        var floatRoot = FindFloatRoot(rootId);
        if (floatRoot is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{rootId}' does not exist.");
        }

        if (!bounds.IsValid)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidModel,
                "Floating bounds must be finite, within +/-32768, and have positive width and height.");
        }

        if (floatRoot.Bounds == bounds && floatRoot.Maximized == maximized)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.AlreadyInState,
                "Floating bounds and maximized state are unchanged.");
        }

        var updated = new DockFloatRoot(
            floatRoot.Id,
            floatRoot.Root,
            bounds,
            maximized,
            floatRoot.ReturnTarget,
            maximized ? floatRoot.RestoreBounds : bounds);
        var floats = FloatingRoots
            .Select(root => root.Id == rootId ? updated : root)
            .ToArray();
        return CommitWorkspace(
            MainRoot,
            floats,
            $"Updated floating bounds for '{rootId}'.");
    }

    public DockWorkspaceResult RemovePanel(PanelInstanceId panel)
    {
        if (panel.IsEmpty || !TryLocatePanel(panel, out var location))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidSource,
                $"Panel '{panel}' is not in the workspace.");
        }

        if (location.IsMain)
        {
            if (MainRoot.EnumeratePanels().Count() == 1)
            {
                return DockWorkspaceResult.Fail(
                    DockWorkspaceError.WouldEmptyMainRoot,
                    "Closing the last main-root panel would leave the main root empty.");
            }

            var removal = MainRoot.RemovePanel(panel);
            if (!removal.Success || removal.Tree is null)
                return MapSingleRootFailure(removal);

            return CommitWorkspace(
                removal.Tree,
                FloatingRoots,
                $"Removed panel '{panel}' from the main root.");
        }

        var floatId = location.FloatRootId!.Value;
        var floatRoot = FindFloatRoot(floatId);
        if (floatRoot is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{floatId}' no longer exists.");
        }

        var floats = FloatingRoots.ToList();
        if (floatRoot.Root.EnumeratePanels().Count() == 1)
        {
            floats.RemoveAll(root => root.Id == floatId);
        }
        else
        {
            var removal = floatRoot.Root.RemovePanel(panel);
            if (!removal.Success || removal.Tree is null)
                return MapSingleRootFailure(removal);

            var index = floats.FindIndex(root => root.Id == floatId);
            floats[index] = RebuildFloatRoot(floatRoot, removal.Tree);
        }

        return CommitWorkspace(
            MainRoot,
            floats,
            $"Removed panel '{panel}' from floating root '{floatId}'.");
    }

    public DockWorkspaceResult ActivateTab(DockNodeId groupId, PanelInstanceId panel)
    {
        if (!TryLocateGroup(groupId, out var floatRoot, out var root))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Target group '{groupId}' does not exist.");
        }

        var result = root.ActivateTab(groupId, panel);
        if (!result.Success || result.Tree is null)
            return MapSingleRootFailure(result);

        return CommitWorkspace(
            floatRoot is null ? result.Tree : MainRoot,
            floatRoot is null
                ? FloatingRoots
                : ReplaceFloatTree(floatRoot, result.Tree),
            result.Diagnostic);
    }

    public DockWorkspaceResult HidePanel(PanelInstanceId panel)
        => ChangePanelVisibility(panel, hide: true);

    public DockWorkspaceResult ShowPanel(PanelInstanceId panel)
        => ChangePanelVisibility(panel, hide: false);

    private DockWorkspaceResult ChangePanelVisibility(PanelInstanceId panel, bool hide)
    {
        if (panel.IsEmpty || !TryLocatePanel(panel, out var location))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidSource,
                $"Panel '{panel}' is not in the workspace.");
        }

        if (location.IsMain)
        {
            var result = hide ? MainRoot.HidePanel(panel) : MainRoot.ShowPanel(panel);
            if (!result.Success || result.Tree is null)
                return MapSingleRootFailure(result);

            return CommitWorkspace(result.Tree, FloatingRoots, result.Diagnostic);
        }

        var floatId = location.FloatRootId!.Value;
        var floatRoot = FindFloatRoot(floatId);
        if (floatRoot is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Floating root '{floatId}' no longer exists.");
        }

        var floatResult = hide
            ? floatRoot.Root.HidePanel(panel)
            : floatRoot.Root.ShowPanel(panel);
        if (!floatResult.Success || floatResult.Tree is null)
            return MapSingleRootFailure(floatResult);

        return CommitWorkspace(
            MainRoot,
            ReplaceFloatTree(floatRoot, floatResult.Tree),
            floatResult.Diagnostic);
    }

    public DockWorkspaceResult ResizeSplit(
        DockNodeId splitId,
        double firstWeight,
        double secondWeight)
    {
        if (!TryLocateSplit(splitId, out var floatRoot, out var root))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidTarget,
                $"Split '{splitId}' does not exist.");
        }

        var result = root.ResizeSplit(splitId, firstWeight, secondWeight);
        if (!result.Success || result.Tree is null)
            return MapSingleRootFailure(result);

        return CommitWorkspace(
            floatRoot is null ? result.Tree : MainRoot,
            floatRoot is null
                ? FloatingRoots
                : ReplaceFloatTree(floatRoot, result.Tree),
            result.Diagnostic);
    }

    public DockWorkspaceResult RestoreDefault()
    {
        var defaultMain = DockTree.CreateDefault();
        if (FloatingRoots.Count == 0
            && string.Equals(defaultMain.Digest, MainRoot.Digest, StringComparison.Ordinal))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.AlreadyInState,
                "The workspace already uses the default layout.");
        }

        return CommitWorkspace(
            defaultMain,
            Array.Empty<DockFloatRoot>(),
            "Restored the default workspace layout.");
    }

    private DockWorkspaceResult MoveToGroup(
        PanelInstanceId panel,
        DockWorkspaceLocation source,
        DockNodeId targetGroupId,
        int? index,
        DockEdge? edgeSide)
    {
        if (!TryLocateGroup(targetGroupId, out var targetFloatRoot, out _))
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.TargetGone,
                $"Target group '{targetGroupId}' does not exist.");
        }

        var sourceFloatRoot = source.FloatRootId is { } sourceFloatId
            ? FindFloatRoot(sourceFloatId)
            : null;
        var sourceTree = source.IsMain ? MainRoot : sourceFloatRoot?.Root;
        if (sourceTree is null)
        {
            return DockWorkspaceResult.Fail(
                DockWorkspaceError.InvalidSource,
                $"Panel '{panel}' is not in the workspace.");
        }

        var target = edgeSide is { } side
            ? (DockTarget)new DockTarget.Edge(targetGroupId, side)
            : new DockTarget.Center(targetGroupId, index);

        if (source.FloatRootId == targetFloatRoot?.Id)
        {
            var result = sourceTree.MovePanel(panel, target);
            if (!result.Success || result.Tree is null)
                return MapSingleRootFailure(result);

            return CommitWorkspace(
                source.IsMain ? result.Tree : MainRoot,
                source.IsMain
                    ? FloatingRoots
                    : ReplaceFloatTree(sourceFloatRoot!, result.Tree),
                result.Diagnostic);
        }

        var nextMain = MainRoot;
        DockTree? nextSourceTree = null;
        var dropSourceRoot = false;

        if (source.IsMain)
        {
            if (MainRoot.EnumeratePanels().Count() == 1)
            {
                return DockWorkspaceResult.Fail(
                    DockWorkspaceError.WouldEmptyMainRoot,
                    "Moving the last main-root panel would leave the main root empty.");
            }

            var removal = MainRoot.RemovePanel(panel);
            if (!removal.Success || removal.Tree is null)
                return MapSingleRootFailure(removal);

            nextMain = removal.Tree;
        }
        else if (sourceFloatRoot!.Root.EnumeratePanels().Count() == 1)
        {
            dropSourceRoot = true;
        }
        else
        {
            var removal = sourceFloatRoot.Root.RemovePanel(panel);
            if (!removal.Success || removal.Tree is null)
                return MapSingleRootFailure(removal);

            nextSourceTree = removal.Tree;
        }

        DockTree? nextTargetTree = null;
        if (targetFloatRoot is null)
        {
            var insertion = nextMain.AddPanel(panel, target);
            if (!insertion.Success || insertion.Tree is null)
                return MapSingleRootFailure(insertion);

            nextMain = insertion.Tree;
        }
        else
        {
            var insertion = targetFloatRoot.Root.AddPanel(panel, target);
            if (!insertion.Success || insertion.Tree is null)
                return MapSingleRootFailure(insertion);

            nextTargetTree = insertion.Tree;
        }

        var floats = new List<DockFloatRoot>();
        foreach (var root in FloatingRoots)
        {
            if (sourceFloatRoot is not null && root.Id == sourceFloatRoot.Id)
            {
                if (!dropSourceRoot)
                    floats.Add(RebuildFloatRoot(root, nextSourceTree!));

                continue;
            }

            if (targetFloatRoot is not null && root.Id == targetFloatRoot.Id)
            {
                floats.Add(RebuildFloatRoot(root, nextTargetTree!));
                continue;
            }

            floats.Add(root);
        }

        return CommitWorkspace(
            nextMain,
            floats,
            $"Moved panel '{panel}' across dock roots.");
    }

    private DockWorkspaceResult CommitWorkspace(
        DockTree mainRoot,
        IEnumerable<DockFloatRoot> floatingRoots,
        string diagnostic = "")
    {
        DockTree normalizedMain;
        try
        {
            var normalized = NormalizeRoot(mainRoot);
            if (normalized is null)
            {
                return DockWorkspaceResult.Fail(
                    DockWorkspaceError.WouldEmptyMainRoot,
                    "The main dock root cannot be empty.");
            }

            normalizedMain = normalized;
        }
        catch (DockModelException ex)
        {
            return DockWorkspaceResult.Fail(MapValidationFailure(ex.Issues), ex.Message);
        }

        var normalizedFloats = new List<DockFloatRoot>();
        foreach (var root in floatingRoots)
        {
            try
            {
                var normalizedRoot = NormalizeRoot(root.Root);
                if (normalizedRoot is null)
                    continue;

                normalizedFloats.Add(RebuildFloatRoot(root, normalizedRoot));
            }
            catch (DockModelException ex)
            {
                return DockWorkspaceResult.Fail(MapValidationFailure(ex.Issues), ex.Message);
            }
        }

        normalizedFloats.Sort(CompareFloatRoots);

        var validation = DockWorkspaceValidator.Validate(normalizedMain, normalizedFloats);
        if (!validation.IsValid)
        {
            if (validation.Has("empty-visible"))
            {
                return DockWorkspaceResult.Fail(
                    DockWorkspaceError.WouldEmptyGroup,
                    $"The operation produced an invalid workspace: {validation.Summary}");
            }

            var error = validation.Has("float-limit")
                        || validation.Has("node-limit")
                        || validation.Has("panel-limit")
                ? DockWorkspaceError.LimitExceeded
                : DockWorkspaceError.InvalidModel;
            return DockWorkspaceResult.Fail(
                error,
                $"The operation produced an invalid workspace: {validation.Summary}");
        }

        var workspace = new DockWorkspace(normalizedMain, normalizedFloats, Revision + 1);
        return DockWorkspaceResult.Ok(workspace, diagnostic);
    }

    private static DockTree? NormalizeRoot(DockTree root)
    {
        var normalization = DockNormalizer.Normalize(root.Root);
        if (normalization.Root is null)
            return null;

        return DockTree.CreateWithPrefix(normalization.Root, root.NodeIdPrefix);
    }

    private DockTabGroupNode ResolveReturnGroup(DockFloatRoot floatRoot)
    {
        // The return target is a group hint; re-docking appends panels and
        // preserves the target group's active panel rather than restoring the
        // old split structure.
        var hinted = MainRoot.FindGroup(floatRoot.ReturnTarget.GroupId);
        if (hinted is not null)
            return hinted;

        return MainRoot.EnumerateNodes().OfType<DockTabGroupNode>().First();
    }

    private DockFloatRootId AllocateFloatRootId()
    {
        var used = new HashSet<string>(
            FloatingRoots.Select(root => root.Id.ToString()),
            StringComparer.Ordinal);
        var ordinal = 1;
        while (used.Contains($"dock.float.{ordinal}"))
            ordinal++;

        return new DockFloatRootId($"dock.float.{ordinal}");
    }

    private DockFloatRoot? FindFloatRoot(DockFloatRootId rootId)
    {
        foreach (var root in FloatingRoots)
        {
            if (root.Id == rootId)
                return root;
        }

        return null;
    }

    private bool TryLocateGroup(
        DockNodeId groupId,
        out DockFloatRoot? floatRoot,
        out DockTree root)
    {
        if (MainRoot.FindGroup(groupId) is not null)
        {
            floatRoot = null;
            root = MainRoot;
            return true;
        }

        foreach (var candidate in FloatingRoots)
        {
            if (candidate.Root.FindGroup(groupId) is not null)
            {
                floatRoot = candidate;
                root = candidate.Root;
                return true;
            }
        }

        floatRoot = null;
        root = null!;
        return false;
    }

    private bool TryLocateSplit(
        DockNodeId splitId,
        out DockFloatRoot? floatRoot,
        out DockTree root)
    {
        if (MainRoot.FindSplit(splitId) is not null)
        {
            floatRoot = null;
            root = MainRoot;
            return true;
        }

        foreach (var candidate in FloatingRoots)
        {
            if (candidate.Root.FindSplit(splitId) is not null)
            {
                floatRoot = candidate;
                root = candidate.Root;
                return true;
            }
        }

        floatRoot = null;
        root = null!;
        return false;
    }

    private IReadOnlyList<DockFloatRoot> ReplaceFloatTree(
        DockFloatRoot floatRoot,
        DockTree tree)
        => FloatingRoots
            .Select(root => root.Id == floatRoot.Id
                ? RebuildFloatRoot(root, tree)
                : root)
            .ToArray();

    private static DockFloatRoot RebuildFloatRoot(DockFloatRoot source, DockTree tree)
        => new(
            source.Id,
            tree,
            source.Bounds,
            source.Maximized,
            source.ReturnTarget,
            source.RestoreBounds);

    private static List<DockFloatRoot> SortFloats(IEnumerable<DockFloatRoot>? roots)
    {
        var floats = roots?.ToList() ?? new List<DockFloatRoot>();
        if (floats.Any(root => root is null))
            throw new ArgumentException("Floating roots cannot contain null.", nameof(roots));

        floats.Sort(CompareFloatRoots);
        return floats;
    }

    private static int CompareFloatRoots(DockFloatRoot left, DockFloatRoot right)
        => string.CompareOrdinal(left.Id.ToString(), right.Id.ToString());

    private static DockWorkspaceError MapValidationFailure(IReadOnlyList<DockValidationIssue> issues)
        => issues.Any(issue => string.Equals(issue.Code, "empty-visible", StringComparison.Ordinal))
            ? DockWorkspaceError.WouldEmptyGroup
            : DockWorkspaceError.InvalidModel;

    private static DockWorkspaceResult MapSingleRootFailure(DockOperationResult result)
    {
        var error = result.Error switch
        {
            DockError.InvalidSource => DockWorkspaceError.InvalidSource,
            DockError.InvalidTarget => DockWorkspaceError.InvalidTarget,
            DockError.DuplicatePanel => DockWorkspaceError.DuplicatePanel,
            DockError.TargetGone => DockWorkspaceError.TargetGone,
            DockError.AlreadyInState => DockWorkspaceError.AlreadyInState,
            DockError.PanelHidden => DockWorkspaceError.PanelHidden,
            DockError.WouldEmptyGroup => DockWorkspaceError.WouldEmptyGroup,
            _ => DockWorkspaceError.InvalidModel,
        };

        return DockWorkspaceResult.Fail(error, result.Diagnostic);
    }
}
