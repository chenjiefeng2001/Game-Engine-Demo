using System.Collections.ObjectModel;
using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.Views.ViewModels;

public sealed class NodeViewModel
{
    public NodeViewModel(SceneNode node, bool expanded)
    {
        Node = node;
        Id = node.Id;
        Name = node.Name;
        Type = node.Type;
        IsExpanded = expanded;
        TypeGlyph = node.Type switch
        {
            "Camera" => "C",
            "Light" => "L",
            "Scene" => "S",
            _ => "E",
        };

        foreach (var child in node.Children)
            Children.Add(new NodeViewModel(child, expanded: false));
    }

    public SceneNode Node { get; }
    public UiId Id { get; }
    public string Name { get; }
    public string Type { get; }
    public string TypeGlyph { get; }
    public bool IsExpanded { get; set; }
    public ObservableCollection<NodeViewModel> Children { get; } = new();

    public IEnumerable<NodeViewModel> Descendants()
    {
        foreach (var child in Children)
        {
            yield return child;
            foreach (var descendant in child.Descendants())
                yield return descendant;
        }
    }
}
