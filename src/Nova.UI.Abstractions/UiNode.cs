using System.Collections.Immutable;

namespace Nova.UI;

/// <summary>
/// Minimal immutable projection of a UI subtree.
///
/// v0.2 intentionally does not turn this into a full declarative UI DSL. It exists
/// to prove three things against real Avalonia controls:
///   1. every node has a stable <see cref="UiId"/>;
///   2. state changes can be addressed by (id, key);
///   3. a subtree can be inspected/tested without a visual tree.
/// </summary>
public sealed record UiNode
{
    public UiNode(
        UiId id,
        string kind,
        string? text = null,
        IEnumerable<KeyValuePair<string, string>>? attributes = null,
        IEnumerable<UiNode>? children = null)
    {
        if (id.IsEmpty) throw new ArgumentException("UiNode requires a stable UiId.", nameof(id));
        ArgumentException.ThrowIfNullOrWhiteSpace(kind);

        Id = id;
        Kind = kind;
        Text = text;
        Attributes = (attributes ?? []).ToImmutableDictionary(StringComparer.Ordinal);
        Children = (children ?? []).ToImmutableArray();
    }

    public UiId Id { get; }
    public string Kind { get; }
    public string? Text { get; }
    public ImmutableDictionary<string, string> Attributes { get; }
    public ImmutableArray<UiNode> Children { get; }

    public IEnumerable<UiNode> Descendants()
    {
        foreach (var child in Children)
        {
            yield return child;
            foreach (var descendant in child.Descendants())
                yield return descendant;
        }
    }

    public UiNode? Find(UiId id)
    {
        if (Id == id) return this;
        foreach (var child in Children)
        {
            var found = child.Find(id);
            if (found is not null) return found;
        }
        return null;
    }
}
