using System.Collections.ObjectModel;
using Avalonia.Threading;
using Nova.Editor.Services;
using Nova.Editor.Services.Commands;
using Nova.Editor.Services.Diagnostics;
using Nova.Editor.Services.Layout;
using Nova.Editor.Services.Model;
using Nova.UI;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Views.ViewModels;

public sealed class LogEntryViewModel
{
    public LogEntryViewModel(EditorLogEntry entry)
    {
        Level = entry.Level;
        Text = $"{entry.Timestamp:HH:mm:ss}  {entry.Level.ToString().ToUpperInvariant(),-7}  {entry.Message}";
    }

    public EditorLogLevel Level { get; }
    public string Text { get; }
}

/// <summary>
/// Shell projection. Structural changes rebuild the hierarchy; property edits
/// update only the affected rows and state slots.
/// </summary>
public sealed class EditorShellViewModel : ObservableObject
{
    private readonly EditorWorkspace _workspace;
    private NodeViewModel? _selectedNode;
    private bool _syncingSelection;
    private string _statusText = "Starting offline workspace...";
    private string _activeTheme;
    private int _consoleCount;

    public EditorShellViewModel(EditorWorkspace workspace)
    {
        _workspace = workspace ?? throw new ArgumentNullException(nameof(workspace));
        _activeTheme = workspace.Theme.Current.DisplayName;

        RebuildHierarchy();
        SyncSelectionFromWorkspace();
        RebuildInspector();
        foreach (var entry in workspace.Log.Snapshot())
            AppendLog(entry);

        UndoCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.Undo),
            () => workspace.Router.CanExecute(NovaCommandIds.Undo));
        RedoCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.Redo),
            () => workspace.Router.CanExecute(NovaCommandIds.Redo));
        AddNodeCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.AddNode));
        DeleteNodeCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.DeleteNode),
            () => workspace.Router.CanExecute(NovaCommandIds.DeleteNode));
        SaveLayoutCommand = new RelayCommand(
            () => _ = SaveCurrentLayoutAsync(_workspace.LayoutMeasurer?.Invoke()));
        ResetLayoutCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.ResetLayout));
        ToggleThemeCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.ToggleTheme));
        ToggleHierarchyCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.ToggleHierarchy));
        ToggleInspectorCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.ToggleInspector));
        ToggleConsoleCommand = new RelayCommand(
            () => ExecuteCommand(NovaCommandIds.ToggleConsole));
        ClearConsoleCommand = new RelayCommand(() =>
        {
            Logs.Clear();
            ConsoleCount = 0;
        });

        workspace.Selection.Changed += OnSelectionChanged;
        workspace.StructureChanged += OnStructureChanged;
        workspace.History.Changed += OnHistoryChanged;
        workspace.History.Undone += OnHistoryUndone;
        workspace.History.Redone += OnHistoryRedone;
        workspace.Log.EntryAdded += OnLogEntryAdded;
        workspace.LayoutChanged += OnLayoutChanged;
        workspace.Theme.Changed += OnThemeChanged;
    }

    public EditorWorkspace Workspace => _workspace;
    public ObservableCollection<NodeViewModel> Roots { get; } = new();
    public ObservableCollection<PropertyRowViewModel> Properties { get; } = new();
    public ObservableCollection<LogEntryViewModel> Logs { get; } = new();

    public NodeViewModel? SelectedNode
    {
        get => _selectedNode;
        set
        {
            if (!Set(ref _selectedNode, value)) return;
            if (!_syncingSelection)
                _workspace.Selection.Select(value?.Id);

            RebuildInspector();
            Raise(nameof(InspectorTitle));
            Raise(nameof(InspectorSubtitle));
            RaiseCommandStates();
        }
    }

    public string InspectorTitle => _selectedNode?.Name ?? "No selection";
    public string InspectorSubtitle => _selectedNode?.Type ?? string.Empty;

    public string StatusText
    {
        get => _statusText;
        internal set => Set(ref _statusText, value);
    }

    public string ActiveTheme
    {
        get => _activeTheme;
        private set => Set(ref _activeTheme, value);
    }

    public int ConsoleCount
    {
        get => _consoleCount;
        private set => Set(ref _consoleCount, value);
    }

    public WorkspaceLayout Layout => _workspace.Layout;

    public RelayCommand UndoCommand { get; }
    public RelayCommand RedoCommand { get; }
    public RelayCommand AddNodeCommand { get; }
    public RelayCommand DeleteNodeCommand { get; }
    public RelayCommand SaveLayoutCommand { get; }
    public RelayCommand ResetLayoutCommand { get; }
    public RelayCommand ToggleThemeCommand { get; }
    public RelayCommand ToggleHierarchyCommand { get; }
    public RelayCommand ToggleInspectorCommand { get; }
    public RelayCommand ToggleConsoleCommand { get; }
    public RelayCommand ClearConsoleCommand { get; }

    public async Task<LayoutLoadResult> InitializeLayoutAsync(CancellationToken cancellationToken = default)
    {
        var result = await _workspace.InitializeLayoutAsync(cancellationToken);
        ActiveTheme = _workspace.Theme.Current.DisplayName;
        StatusText = result.UsedFallback
            ? $"Offline · default layout ({result.Reason})"
            : "Offline · layout restored";
        return result;
    }

    public async Task<LayoutSaveResult> SaveCurrentLayoutAsync(WorkspaceLayout? measured = null)
    {
        if (measured is not null)
            _workspace.ApplyLayout(measured);

        var result = await _workspace.SaveLayoutAsync();
        StatusText = result.Success ? "Layout saved" : $"Layout save failed: {result.Error}";
        return result;
    }

    public CommandResult ExecuteCommand(UiId id)
    {
        var result = _workspace.Execute(id);
        if (!result.Success)
            StatusText = result.Message ?? "Command failed";
        RaiseCommandStates();
        return result;
    }

    public void RefreshInspectorValues()
    {
        foreach (var row in Properties)
            row.RefreshFromModel();
        Raise(nameof(InspectorTitle));
        Raise(nameof(InspectorSubtitle));
    }

    private void RebuildHierarchy()
    {
        var selectedId = _selectedNode?.Id ?? _workspace.Selection.SelectedId;
        Roots.Clear();
        var root = new NodeViewModel(_workspace.Document.Root, expanded: true);
        foreach (var child in root.Children)
            child.IsExpanded = true;
        Roots.Add(root);
        _selectedNode = selectedId is { } id ? Find(root, id) : null;
        Raise(nameof(SelectedNode));
    }

    private void SyncSelectionFromWorkspace()
    {
        var id = _workspace.Selection.SelectedId;
        var node = id is { } selected ? FindInRoots(selected) : null;
        _syncingSelection = true;
        try
        {
            SelectedNode = node;
        }
        finally
        {
            _syncingSelection = false;
        }

        StatusText = node is null
            ? "No selection"
            : $"{node.Name} · {node.Type}";
    }

    private void RebuildInspector()
    {
        Properties.Clear();
        if (_selectedNode is null)
        {
            Raise(nameof(InspectorTitle));
            Raise(nameof(InspectorSubtitle));
            return;
        }

        foreach (var definition in SceneNodeSchema.For(_selectedNode.Type))
            Properties.Add(new PropertyRowViewModel(_workspace, _selectedNode.Node, definition));

        Raise(nameof(InspectorTitle));
        Raise(nameof(InspectorSubtitle));
    }

    private NodeViewModel? Find(NodeViewModel root, UiId id)
    {
        if (root.Id == id) return root;
        foreach (var child in root.Children)
        {
            var found = Find(child, id);
            if (found is not null) return found;
        }
        return null;
    }

    private NodeViewModel? FindInRoots(UiId id)
    {
        foreach (var root in Roots)
        {
            var found = Find(root, id);
            if (found is not null) return found;
        }
        return null;
    }

    private void AppendLog(EditorLogEntry entry)
    {
        Logs.Add(new LogEntryViewModel(entry));
        while (Logs.Count > 400)
            Logs.RemoveAt(0);
        ConsoleCount = Logs.Count;
    }

    private void OnSelectionChanged(object? sender, Nova.Editor.Services.Selection.SelectionChangedEventArgs e)
        => SyncSelectionFromWorkspace();

    private void OnStructureChanged(object? sender, EventArgs e)
    {
        RebuildHierarchy();
        SyncSelectionFromWorkspace();
        RebuildInspector();
    }

    private void OnHistoryChanged(object? sender, EventArgs e) => RaiseCommandStates();

    private void OnHistoryUndone(object? sender, UndoableCommandEventArgs e)
    {
        if (e.Command is EditPropertyCommand)
            RefreshInspectorValues();
    }

    private void OnHistoryRedone(object? sender, UndoableCommandEventArgs e)
    {
        if (e.Command is EditPropertyCommand)
            RefreshInspectorValues();
    }

    private void OnLogEntryAdded(object? sender, EditorLogEntry entry)
    {
        if (Dispatcher.UIThread.CheckAccess())
            AppendLog(entry);
        else
            Dispatcher.UIThread.Post(() => AppendLog(entry));
    }

    private void OnLayoutChanged(object? sender, LayoutChangedEventArgs e)
    {
        Raise(nameof(Layout));
        RaiseCommandStates();
    }

    private void OnThemeChanged(object? sender, Nova.UI.Theme.ThemeChangedEventArgs e)
        => ActiveTheme = e.Current.DisplayName;

    private void RaiseCommandStates()
    {
        UndoCommand.RaiseCanExecuteChanged();
        RedoCommand.RaiseCanExecuteChanged();
        DeleteNodeCommand.RaiseCanExecuteChanged();
    }
}
