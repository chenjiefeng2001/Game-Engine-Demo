using System.Text.Json.Serialization;
using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Layout;

/// <summary>
/// Persistence-only projection of a <see cref="DockWorkspace"/>. Runtime state
/// such as revision, host generations, tokens and window handles is excluded.
/// </summary>
public sealed record DockWorkspaceSnapshot
{
    [JsonPropertyOrder(0)]
    public DockNodeSnapshot? Main { get; init; }

    [JsonPropertyOrder(1)]
    public DockFloatRootSnapshot[] Floats { get; init; } = Array.Empty<DockFloatRootSnapshot>();
}

public sealed record DockFloatRootSnapshot
{
    [JsonPropertyOrder(0)]
    public string Id { get; init; } = string.Empty;

    [JsonPropertyOrder(1)]
    public DockBoundsSnapshot Bounds { get; init; } = new();

    [JsonPropertyOrder(2)]
    public bool Maximized { get; init; }

    [JsonPropertyOrder(3)]
    public DockReturnTargetSnapshot? ReturnTarget { get; init; }

    [JsonPropertyOrder(4)]
    public DockNodeSnapshot? Root { get; init; }
}

public sealed record DockBoundsSnapshot
{
    [JsonPropertyOrder(0)]
    public double X { get; init; }

    [JsonPropertyOrder(1)]
    public double Y { get; init; }

    [JsonPropertyOrder(2)]
    public double Width { get; init; }

    [JsonPropertyOrder(3)]
    public double Height { get; init; }
}

public sealed record DockReturnTargetSnapshot
{
    [JsonPropertyOrder(0)]
    public string GroupId { get; init; } = string.Empty;

    [JsonPropertyOrder(1)]
    public int Index { get; init; }
}

/// <summary>
/// Flat node encoding shared by split and tab nodes. Fields that do not belong
/// to a node kind are omitted on write and rejected on read.
/// </summary>
public sealed record DockNodeSnapshot
{
    [JsonPropertyOrder(0)]
    public string Kind { get; init; } = string.Empty;

    [JsonPropertyOrder(1)]
    public string Id { get; init; } = string.Empty;

    [JsonPropertyOrder(2)]
    public string? Orientation { get; init; }

    [JsonPropertyOrder(3)]
    public double? FirstWeight { get; init; }

    [JsonPropertyOrder(4)]
    public double? SecondWeight { get; init; }

    [JsonPropertyOrder(5)]
    public DockNodeSnapshot? First { get; init; }

    [JsonPropertyOrder(6)]
    public DockNodeSnapshot? Second { get; init; }

    [JsonPropertyOrder(7)]
    public string[]? Panels { get; init; }

    [JsonPropertyOrder(8)]
    public string[]? Hidden { get; init; }

    [JsonPropertyOrder(9)]
    public string? ActivePanel { get; init; }
}

public readonly record struct DockSnapshotResult(
    DockWorkspace? Workspace,
    IReadOnlyList<PanelInstanceId> UnresolvedPanels,
    string Diagnostic)
{
    public bool IsValid => Workspace is not null;

    public static DockSnapshotResult Ok(
        DockWorkspace workspace,
        IReadOnlyList<PanelInstanceId> unresolvedPanels)
        => new(workspace, unresolvedPanels, string.Empty);

    public static DockSnapshotResult Fail(string diagnostic)
        => new(null, Array.Empty<PanelInstanceId>(), diagnostic);
}

public static class DockWorkspaceSnapshotCodec
{
    private static readonly HashSet<PanelInstanceId> SupportedPanels =
    [
        DockPanelIds.Hierarchy,
        DockPanelIds.Scene,
        DockPanelIds.Inspector,
        DockPanelIds.Console,
    ];

    public static DockWorkspaceSnapshot FromWorkspace(DockWorkspace workspace)
    {
        ArgumentNullException.ThrowIfNull(workspace);

        return new DockWorkspaceSnapshot
        {
            Main = FromNode(workspace.MainRoot.Root),
            Floats = workspace.FloatingRoots.Select(FromFloatRoot).ToArray(),
        };
    }

    /// <summary>
    /// True when any node declares <see cref="DockNodeSnapshot.Hidden"/>.
    /// Schema v2 has no hidden concept, so such a file is inconsistent.
    /// </summary>
    public static bool ContainsHiddenDeclarations(DockWorkspaceSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        if (ContainsHidden(snapshot.Main))
            return true;

        foreach (var root in snapshot.Floats ?? Array.Empty<DockFloatRootSnapshot>())
        {
            if (root is not null && ContainsHidden(root.Root))
                return true;
        }

        return false;
    }

    private static bool ContainsHidden(DockNodeSnapshot? node)
        => node is not null
           && (node.Hidden is not null
               || ContainsHidden(node.First)
               || ContainsHidden(node.Second));

    public static DockSnapshotResult ToWorkspace(DockWorkspaceSnapshot? snapshot)
    {
        if (snapshot is null)
            return DockSnapshotResult.Fail("dock snapshot is missing.");

        if (snapshot.Main is null)
            return DockSnapshotResult.Fail("dock snapshot has no main root.");

        try
        {
            var main = BuildTree(snapshot.Main);
            var floats = new List<DockFloatRoot>();
            var floatSnapshots = snapshot.Floats ?? Array.Empty<DockFloatRootSnapshot>();

            foreach (var floatSnapshot in floatSnapshots)
            {
                if (floatSnapshot is null)
                    return DockSnapshotResult.Fail("dock snapshot contains a null floating root.");

                floats.Add(BuildFloatRoot(floatSnapshot));
            }

            var workspace = DockWorkspace.Create(main, floats, revision: 0);
            var unresolved = workspace.EnumeratePanels()
                .Where(panel => !SupportedPanels.Contains(panel))
                .ToArray();
            return DockSnapshotResult.Ok(workspace, unresolved);
        }
        catch (DockModelException ex)
        {
            return DockSnapshotResult.Fail(ex.Message);
        }
        catch (ArgumentException ex)
        {
            return DockSnapshotResult.Fail(ex.Message);
        }
    }

    private static DockNodeSnapshot? FromNode(DockNode? node)
    {
        switch (node)
        {
            case null:
                return null;

            case DockTabGroupNode group:
                return new DockNodeSnapshot
                {
                    Kind = "tabs",
                    Id = group.Id.ToString(),
                    Panels = group.Panels.Select(panel => panel.ToString()).ToArray(),
                    Hidden = group.HiddenPanels.Count == 0
                        ? null
                        : group.HiddenPanels.Select(panel => panel.ToString()).ToArray(),
                    ActivePanel = group.ActivePanel?.ToString(),
                };

            case DockSplitNode split:
                return new DockNodeSnapshot
                {
                    Kind = "split",
                    Id = split.Id.ToString(),
                    Orientation = split.Orientation == DockOrientation.Horizontal
                        ? "horizontal"
                        : "vertical",
                    FirstWeight = NormalizeZero(split.FirstWeight),
                    SecondWeight = NormalizeZero(split.SecondWeight),
                    First = FromNode(split.First),
                    Second = FromNode(split.Second),
                };

            default:
                throw new DockModelException(
                    $"Unsupported dock node type '{node.GetType().Name}'.");
        }
    }

    private static DockFloatRootSnapshot FromFloatRoot(DockFloatRoot root)
        => new()
        {
            Id = root.Id.ToString(),
            Bounds = new DockBoundsSnapshot
            {
                X = NormalizeZero(root.RestoreBounds.X),
                Y = NormalizeZero(root.RestoreBounds.Y),
                Width = NormalizeZero(root.RestoreBounds.Width),
                Height = NormalizeZero(root.RestoreBounds.Height),
            },
            Maximized = root.Maximized,
            ReturnTarget = new DockReturnTargetSnapshot
            {
                GroupId = root.ReturnTarget.GroupId.ToString(),
                Index = root.ReturnTarget.Index,
            },
            Root = FromNode(root.Root.Root),
        };

    private static DockFloatRoot BuildFloatRoot(DockFloatRootSnapshot snapshot)
    {
        if (string.IsNullOrWhiteSpace(snapshot.Id))
            throw new DockModelException("A floating root has an empty id.");

        if (snapshot.Bounds is null)
            throw new DockModelException($"Floating root '{snapshot.Id}' has no bounds.");

        var bounds = new DockBounds(
            snapshot.Bounds.X,
            snapshot.Bounds.Y,
            snapshot.Bounds.Width,
            snapshot.Bounds.Height);
        if (!bounds.IsValid)
            throw new DockModelException($"Floating root '{snapshot.Id}' has invalid bounds.");

        if (snapshot.ReturnTarget is null)
            throw new DockModelException($"Floating root '{snapshot.Id}' has no return target.");

        if (snapshot.ReturnTarget.Index < 0)
        {
            throw new DockModelException(
                $"Floating root '{snapshot.Id}' has a negative return-target index.");
        }

        if (snapshot.Root is null)
            throw new DockModelException($"Floating root '{snapshot.Id}' has no dock tree.");

        var returnTarget = new DockReturnTarget(
            BuildNodeId(snapshot.ReturnTarget.GroupId),
            snapshot.ReturnTarget.Index);
        var tree = BuildTree(snapshot.Root);
        return new DockFloatRoot(
            new DockFloatRootId(snapshot.Id),
            tree,
            bounds,
            snapshot.Maximized,
            returnTarget,
            bounds);
    }

    private static DockTree BuildTree(DockNodeSnapshot snapshot)
        => DockTree.Create(BuildNode(snapshot));

    private static DockNode BuildNode(DockNodeSnapshot snapshot)
    {
        if (string.Equals(snapshot.Kind, "tabs", StringComparison.Ordinal))
            return BuildTabGroup(snapshot);

        if (string.Equals(snapshot.Kind, "split", StringComparison.Ordinal))
            return BuildSplit(snapshot);

        throw new DockModelException($"Unsupported dock node kind '{snapshot.Kind}'.");
    }

    private static DockTabGroupNode BuildTabGroup(DockNodeSnapshot snapshot)
    {
        if (HasSplitFields(snapshot))
            throw new DockModelException($"Tab group '{snapshot.Id}' contains split fields.");

        if (snapshot.Panels is null || snapshot.Panels.Length == 0)
            throw new DockModelException($"Tab group '{snapshot.Id}' has no panels.");

        if (snapshot.ActivePanel is null)
            throw new DockModelException($"Tab group '{snapshot.Id}' has no active panel.");

        var panels = new PanelInstanceId[snapshot.Panels.Length];
        for (var index = 0; index < snapshot.Panels.Length; index++)
            panels[index] = new PanelInstanceId(snapshot.Panels[index]);

        var hidden = Array.Empty<PanelInstanceId>();
        if (snapshot.Hidden is not null)
        {
            hidden = new PanelInstanceId[snapshot.Hidden.Length];
            for (var index = 0; index < snapshot.Hidden.Length; index++)
                hidden[index] = new PanelInstanceId(snapshot.Hidden[index]);
        }

        return new DockTabGroupNode(
            BuildNodeId(snapshot.Id),
            panels,
            new PanelInstanceId(snapshot.ActivePanel),
            hidden);
    }

    private static DockSplitNode BuildSplit(DockNodeSnapshot snapshot)
    {
        if (HasTabFields(snapshot))
            throw new DockModelException($"Split '{snapshot.Id}' contains tab fields.");

        var orientation = snapshot.Orientation switch
        {
            "horizontal" => DockOrientation.Horizontal,
            "vertical" => DockOrientation.Vertical,
            null => throw new DockModelException($"Split '{snapshot.Id}' has no orientation."),
            _ => throw new DockModelException(
                $"Split '{snapshot.Id}' has unknown orientation '{snapshot.Orientation}'."),
        };

        if (snapshot.FirstWeight is null || snapshot.SecondWeight is null)
            throw new DockModelException($"Split '{snapshot.Id}' has no weights.");

        if (snapshot.First is null || snapshot.Second is null)
            throw new DockModelException($"Split '{snapshot.Id}' does not have two children.");

        return new DockSplitNode(
            BuildNodeId(snapshot.Id),
            orientation,
            BuildNode(snapshot.First),
            BuildNode(snapshot.Second),
            snapshot.FirstWeight.Value,
            snapshot.SecondWeight.Value);
    }

    private static DockNodeId BuildNodeId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new DockModelException("A dock node has an empty id.");

        return new DockNodeId(value);
    }

    private static bool HasSplitFields(DockNodeSnapshot snapshot)
        => snapshot.Orientation is not null
           || snapshot.FirstWeight is not null
           || snapshot.SecondWeight is not null
           || snapshot.First is not null
           || snapshot.Second is not null;

    private static bool HasTabFields(DockNodeSnapshot snapshot)
        => snapshot.Panels is not null
           || snapshot.Hidden is not null
           || snapshot.ActivePanel is not null;

    private static double NormalizeZero(double value)
        => value == 0d ? 0d : value;
}
