using Nova.Editor.Services;
using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.Views.ViewModels;

/// <summary>
/// One inspector row. Commits are routed through <see cref="EditorWorkspace"/>,
/// which validates and records an undoable command. The row never writes model
/// properties directly.
/// </summary>
public sealed class PropertyRowViewModel : ObservableObject
{
    private readonly EditorWorkspace _workspace;
    private readonly SceneNode _node;
    private readonly PropertyDefinition _definition;

    private bool _suppress;
    private double? _numberValue;
    private string _textValue = string.Empty;
    private bool _boolValue;
    private string _choiceValue = string.Empty;
    private string? _error;

    public PropertyRowViewModel(EditorWorkspace workspace, SceneNode node, PropertyDefinition definition)
    {
        _workspace = workspace;
        _node = node;
        _definition = definition;
        RefreshFromModel();
    }

    public string Key => _definition.Key;
    public string Label => _definition.Label;
    public string Description => _definition.Kind switch
    {
        PropertyEditorKind.Number when _definition.Minimum.HasValue || _definition.Maximum.HasValue =>
            $"range {_definition.Minimum?.ToString() ?? "-∞"} .. {_definition.Maximum?.ToString() ?? "∞"}",
        PropertyEditorKind.Choice when _definition.Options is { Count: > 0 } =>
            string.Join(" / ", _definition.Options),
        _ => string.Empty,
    };

    public bool IsNumber => _definition.Kind == PropertyEditorKind.Number;
    public bool IsText => _definition.Kind == PropertyEditorKind.Text;
    public bool IsBool => _definition.Kind == PropertyEditorKind.Boolean;
    public bool IsChoice => _definition.Kind == PropertyEditorKind.Choice;

    public double? Minimum => _definition.Minimum;
    public double? Maximum => _definition.Maximum;
    public double Step => _definition.Step;
    public string Format => _definition.Format;
    public IReadOnlyList<string> Options => _definition.Options ?? [];

    public string? Error
    {
        get => _error;
        private set
        {
            if (Set(ref _error, value)) Raise(nameof(HasError));
        }
    }

    public bool HasError => !string.IsNullOrEmpty(Error);

    public double? NumberValue
    {
        get => _numberValue;
        set
        {
            if (_suppress || Nullable.Equals(_numberValue, value)) return;
            _numberValue = value;
            Raise();
            if (value.HasValue) Commit(value.Value);
        }
    }

    public string TextValue
    {
        get => _textValue;
        set
        {
            if (_suppress || _textValue == value) return;
            _textValue = value;
            Raise();
        }
    }

    public bool BoolValue
    {
        get => _boolValue;
        set
        {
            if (_suppress || _boolValue == value) return;
            _boolValue = value;
            Raise();
            Commit(value);
        }
    }

    public string ChoiceValue
    {
        get => _choiceValue;
        set
        {
            if (_suppress || _choiceValue == value) return;
            _choiceValue = value;
            Raise();
            Commit(value);
        }
    }

    /// <summary>
    /// Called by NovaTextField on Enter/blur. The field's current text is passed
    /// explicitly because the TextBox binding is one-way (typing must not write
    /// the model or open an undo entry on every keystroke).
    /// </summary>
    public void CommitText(string? text = null)
    {
        if (!IsText) return;

        var candidate = text ?? _textValue;
        if (!string.Equals(_textValue, candidate, StringComparison.Ordinal))
        {
            _textValue = candidate;
            Raise(nameof(TextValue));
        }

        if (Equals(_node.GetProperty(Key), candidate)) return;
        if (Commit(candidate))
            _workspace.PublishProperty(_node, Key, candidate);
    }

    public void RefreshFromModel()
    {
        var value = _node.GetProperty(Key);
        _suppress = true;
        try
        {
            _numberValue = value is double d ? d : null;
            _textValue = value?.ToString() ?? string.Empty;
            _boolValue = value is bool b && b;
            _choiceValue = value?.ToString() ?? string.Empty;
        }
        finally
        {
            _suppress = false;
        }

        Raise(nameof(NumberValue));
        Raise(nameof(TextValue));
        Raise(nameof(BoolValue));
        Raise(nameof(ChoiceValue));
        Error = null;
    }

    private bool Commit(object? value)
    {
        var result = _workspace.TryEditProperty(_node.Id, Key, value);
        if (!result.Success)
        {
            Error = result.Message;
            RefreshFromModel();
            return false;
        }

        Error = null;
        return true;
    }
}
