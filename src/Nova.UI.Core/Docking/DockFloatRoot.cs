using Nova.UI;

namespace Nova.UI.Core.Docking;

/// <summary>Stable identity for one floating root in a workspace.</summary>
public readonly record struct DockFloatRootId(UiId Value)
{
    public DockFloatRootId(string value) : this(new UiId(value)) { }

    public bool IsEmpty => Value.IsEmpty;

    public override string ToString() => Value.Value ?? string.Empty;
}

/// <summary>
/// Hint for re-docking a floating root. The target group may disappear while
/// the root floats; the coordinator then resolves a deterministic fallback.
/// </summary>
public readonly record struct DockReturnTarget(DockNodeId GroupId, int Index);

/// <summary>
/// One floating root. Its dock tree uses the root id as the node-id namespace
/// so newly allocated split/tab nodes stay globally unique.
/// </summary>
public sealed class DockFloatRoot
{
    public DockFloatRoot(
        DockFloatRootId id,
        DockTree root,
        DockBounds bounds,
        bool maximized,
        DockReturnTarget returnTarget,
        DockBounds? restoreBounds = null)
    {
        if (id.IsEmpty)
            throw new ArgumentException("A DockFloatRootId must be non-empty.", nameof(id));
        ArgumentNullException.ThrowIfNull(root);

        Id = id;
        Root = root.WithNodeIdPrefix(id.ToString());
        Bounds = bounds;
        RestoreBounds = restoreBounds ?? bounds;
        Maximized = maximized;
        ReturnTarget = returnTarget;
    }

    public DockFloatRootId Id { get; }

    public DockTree Root { get; }

    /// <summary>Last reported window geometry; transient while maximized.</summary>
    public DockBounds Bounds { get; }

    /// <summary>Normal (restored) geometry used for persistence and W3 digest.</summary>
    public DockBounds RestoreBounds { get; }

    public bool Maximized { get; }

    public DockReturnTarget ReturnTarget { get; }
}
