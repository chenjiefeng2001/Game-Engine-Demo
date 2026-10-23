using Nova.UI;

namespace Nova.Editor.Services.Model;

/// <summary>
/// Source of truth for the editor scene. The UI never mutates nodes directly;
/// undoable commands call these methods.
/// </summary>
public sealed class SceneDocument
{
    private readonly Dictionary<UiId, SceneNode> _index = new();
    private readonly List<SceneNode> _roots = new();

    public SceneDocument(SceneNode root, Guid? contextId = null)
    {
        Root = root ?? throw new ArgumentNullException(nameof(root));
        if (root.Parent is not null) throw new InvalidOperationException("Root already has a parent.");
        ContextId = contextId ?? Guid.NewGuid();
        _roots.Add(root);
        RegisterTree(root);
    }

    /// <summary>Opaque token used to reject commands from another document/workspace.</summary>
    public Guid ContextId { get; }

    public SceneNode Root { get; }
    public IReadOnlyList<SceneNode> Roots => _roots;

    public SceneNode? Find(UiId id) => _index.TryGetValue(id, out var node) ? node : null;

    public bool Contains(UiId id) => _index.ContainsKey(id);

    public void AddNode(SceneNode parent, SceneNode node, int? index = null)
    {
        if (!_index.ContainsKey(parent.Id))
            throw new InvalidOperationException($"Parent '{parent.Id}' is not in this document.");
        if (_index.ContainsKey(node.Id))
            throw new InvalidOperationException($"Node '{node.Id}' already exists in this document.");
        var duplicate = node.Descendants().FirstOrDefault(descendant => _index.ContainsKey(descendant.Id));
        if (duplicate is not null)
            throw new InvalidOperationException($"Node '{duplicate.Id}' already exists in this document.");

        parent.AddChild(node, index);
        RegisterTree(node);
    }

    public void RemoveNode(SceneNode node)
    {
        if (ReferenceEquals(node, Root))
            throw new InvalidOperationException("The document root cannot be removed.");
        if (node.Parent is null)
            throw new InvalidOperationException($"Node '{node.Id}' is not attached.");

        node.Parent.RemoveChild(node);
        UnregisterTree(node);
    }

    public IEnumerable<SceneNode> AllNodes()
    {
        foreach (var root in _roots)
        {
            yield return root;
            foreach (var descendant in root.Descendants())
                yield return descendant;
        }
    }

    private void RegisterTree(SceneNode node)
    {
        if (!_index.TryAdd(node.Id, node))
            throw new InvalidOperationException($"Duplicate node id '{node.Id}'.");
        foreach (var child in node.Children)
            RegisterTree(child);
    }

    private void UnregisterTree(SceneNode node)
    {
        _index.Remove(node.Id);
        foreach (var child in node.Children)
            UnregisterTree(child);
    }
}
