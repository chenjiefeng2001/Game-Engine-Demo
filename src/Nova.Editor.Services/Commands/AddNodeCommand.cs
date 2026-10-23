using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.Services.Commands;

public sealed class AddNodeCommand : IUndoableCommand
{
    private readonly SceneDocument _document;
    private readonly SceneNode _parent;
    private readonly SceneNode _node;
    private readonly int? _index;

    public AddNodeCommand(SceneDocument document, SceneNode parent, SceneNode node, int? index = null)
    {
        _document = document ?? throw new ArgumentNullException(nameof(document));
        _parent = parent ?? throw new ArgumentNullException(nameof(parent));
        _node = node ?? throw new ArgumentNullException(nameof(node));
        _index = index;
    }

    public UiId Id => new("editor.add-node");
    public string Label => $"Add {_node.Name}";
    public SceneNode Node => _node;
    public Guid ContextId => _document.ContextId;

    public bool CanExecute()
        => _document.Contains(_parent.Id)
           && !_document.Contains(_node.Id)
           && _node.Parent is null;

    public bool CanUndo()
        => _document.Contains(_node.Id) && _node.Parent is not null;

    public void Execute() => _document.AddNode(_parent, _node, _index);

    public void Undo() => _document.RemoveNode(_node);
}
