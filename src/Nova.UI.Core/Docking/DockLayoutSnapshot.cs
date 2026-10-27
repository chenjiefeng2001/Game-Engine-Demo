namespace Nova.UI.Core.Docking;

/// <summary>
/// One committed workspace layout captured for the layout history. The payload
/// is pure model content: the main root, every floating root, panel identity
/// and tab order, hidden membership, split weights, floating bounds, restore
/// bounds, maximized state and return targets. Revision, window handles, views,
/// host generations and projection tokens never enter a snapshot, so a snapshot
/// stays replayable after its hosts and views are gone.
/// </summary>
public sealed class DockLayoutSnapshot
{
    private DockLayoutSnapshot(
        DockTree mainRoot,
        IReadOnlyList<DockFloatRoot> floatingRoots,
        string digest)
    {
        MainRoot = mainRoot;
        FloatingRoots = floatingRoots;
        Digest = digest;
    }

    public DockTree MainRoot { get; }

    public IReadOnlyList<DockFloatRoot> FloatingRoots { get; }

    /// <summary>
    /// W3 content identity of the captured layout. It is derived from this
    /// snapshot's own content when the snapshot is built; because that content
    /// never changes afterwards, the value cannot go stale.
    /// </summary>
    public string Digest { get; }

    /// <summary>
    /// Captures the content of a committed workspace. The revision is dropped:
    /// replaying history must adopt the revision of the commit sequence it
    /// joins, never rewind to the one the layout originally carried.
    /// </summary>
    public static DockLayoutSnapshot Capture(DockWorkspace workspace)
    {
        ArgumentNullException.ThrowIfNull(workspace);

        var content = DockWorkspace.Create(
            workspace.MainRoot,
            workspace.FloatingRoots,
            revision: 0);

        return new DockLayoutSnapshot(
            content.MainRoot,
            content.FloatingRoots,
            content.Digest);
    }

    /// <summary>
    /// Rebuilds a committed workspace from the captured content at the revision
    /// the caller is about to commit. Throws <see cref="DockModelException"/>
    /// when the content would not validate, which keeps a corrupt snapshot from
    /// reaching a host.
    /// </summary>
    public DockWorkspace RestoreWorkspace(long revision)
        => DockWorkspace.Create(MainRoot, FloatingRoots, revision);

    public bool SameContent(DockLayoutSnapshot? other)
        => other is not null
           && string.Equals(Digest, other.Digest, StringComparison.Ordinal);
}
