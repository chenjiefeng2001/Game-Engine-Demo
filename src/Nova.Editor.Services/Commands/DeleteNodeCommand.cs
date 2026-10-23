using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.Services.Commands;

public sealed class DeleteNodeCommand : IUndoableCommand
{
    private readonly SceneDocument _document;
    private readonly SceneNode _node;
    private readonly SceneNode _parent;
    private readonly int _index;

    public DeleteNodeCommand(SceneDocument document, SceneNode node)
    {
        _document = document ?? throw new ArgumentNullException(nameof(document));
        _node = node ?? throw new ArgumentNullException(nameof(node));

        _parent = node.Parent ?? throw new InvalidOperationException("Cannot delete an unattached node.");
        _index = _parent.IndexOf(node);
    }

    public UiId Id => new("editor.delete-node");
    public string Label => $"Delete {_node.Name}";
    public SceneNode Node => _node;
    public SceneNode Parent => _parent;
    public Guid ContextId => _document.ContextId;

    public bool CanExecute()
        => !ReferenceEquals(_node, _document.Root)
           && _document.Contains(_node.Id)
           && _node.Parent is not null;

    public bool CanUndo()
        => !_document.Contains(_node.Id) && _node.Parent is null;

    public void Execute() => _document.RemoveNode(_node);

    public void Undo() => _document.AddNode(_parent, _node, _index);
}
