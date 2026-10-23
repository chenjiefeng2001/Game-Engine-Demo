using Nova.UI;

namespace Nova.Editor.Services.Model;

/// <summary>
/// Mutable scene node owned by the editor document. Identity is stable for the
/// lifetime of the document; display indexes are never used as identity.
/// </summary>
public sealed class SceneNode
{
    private readonly Dictionary<string, object?> _properties = new(StringComparer.Ordinal);
    private readonly List<SceneNode> _children = new();

    public SceneNode(UiId id, string name, string type)
    {
        if (id.IsEmpty) throw new ArgumentException("SceneNode requires a stable id.", nameof(id));
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentException.ThrowIfNullOrWhiteSpace(type);

        Id = id;
        Name = name;
        Type = type;
    }

    public UiId Id { get; }
    public string Name { get; set; }
    public string Type { get; }
    public SceneNode? Parent { get; private set; }

    public IReadOnlyList<SceneNode> Children => _children;
    public IReadOnlyDictionary<string, object?> Properties => _properties;

    public bool IsAttached => Parent is not null;

    public void AddChild(SceneNode child, int? index = null)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (ReferenceEquals(child, this)) throw new InvalidOperationException("A node cannot parent itself.");
        if (child.Parent is not null)
            throw new InvalidOperationException($"Node '{child.Id}' already has a parent.");

        child.Parent = this;
        if (index.HasValue && index.Value >= 0 && index.Value < _children.Count)
            _children.Insert(index.Value, child);
        else
            _children.Add(child);
    }

    public bool RemoveChild(SceneNode child)
    {
        if (!_children.Remove(child)) return false;
        child.Parent = null;
        return true;
    }

    public int IndexOf(SceneNode child) => _children.IndexOf(child);

    public object? GetProperty(string key)
        => _properties.TryGetValue(key, out var value) ? value : null;

    public void SetProperty(string key, object? value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        _properties[key] = value;
    }

    public bool HasProperty(string key) => _properties.ContainsKey(key);

    public IEnumerable<SceneNode> Descendants()
    {
        foreach (var child in _children)
        {
            yield return child;
            foreach (var descendant in child.Descendants())
                yield return descendant;
        }
    }
}
