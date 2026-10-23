using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.Services.Commands;

/// <summary>
/// The single model-edit path used by the inspector. Validation happens before
/// the command is created; the command itself only applies/reverts the value and
/// publishes the projection callback.
/// </summary>
public sealed class EditPropertyCommand : IUndoableCommand
{
    private readonly SceneDocument _document;
    private readonly SceneNode _node;
    private readonly string _key;
    private readonly object? _oldValue;
    private readonly object? _newValue;
    private readonly Action<UiId, string, object?>? _published;
    private readonly Action<Exception>? _projectionFailure;

    public EditPropertyCommand(
        SceneDocument document,
        SceneNode node,
        string key,
        object? newValue,
        Action<UiId, string, object?>? published = null,
        Action<Exception>? projectionFailure = null)
    {
        _document = document ?? throw new ArgumentNullException(nameof(document));
        _node = node ?? throw new ArgumentNullException(nameof(node));
        ArgumentException.ThrowIfNullOrWhiteSpace(key);

        _key = key;
        _oldValue = node.GetProperty(key);
        _newValue = newValue;
        _published = published;
        _projectionFailure = projectionFailure;
    }

    public UiId Id => new("editor.edit-property");
    public string Label => $"Set {_key}";
    public Guid ContextId => _document.ContextId;
    public UiId NodeId => _node.Id;
    public string Key => _key;
    public object? OldValue => _oldValue;
    public object? NewValue => _newValue;

    public bool CanExecute()
        => _document.Contains(_node.Id)
           && _node.HasProperty(_key)
           && !Equals(_oldValue, _newValue);

    public bool CanUndo()
        => _document.Contains(_node.Id) && _node.HasProperty(_key);

    public void Execute() => Apply(_newValue);

    public void Undo() => Apply(_oldValue);

    private void Apply(object? value)
    {
        _node.SetProperty(_key, value);

        if (_published is null) return;
        try
        {
            _published(_node.Id, _key, value);
        }
        catch (Exception ex)
        {
            // Projection is an observer. A broken UI subscriber must not turn a
            // successfully applied model change into an unrecorded command.
            _projectionFailure?.Invoke(ex);
        }
    }
}
