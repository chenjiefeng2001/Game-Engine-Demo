using Nova.Editor.Services.Commands;
using Nova.Editor.Services.Diagnostics;
using Nova.Editor.Services.Layout;
using Nova.Editor.Services.Model;
using Nova.Editor.Services.Selection;
using Nova.Editor.Services.Viewport;
using Nova.UI;
using Nova.UI.Core.Commands;
using Nova.UI.Core.Layout;
using Nova.UI.Core.State;
using Nova.UI.Theme;

namespace Nova.Editor.Services;

public sealed class LayoutChangedEventArgs : EventArgs
{
    public LayoutChangedEventArgs(WorkspaceLayout layout) => Layout = layout;
    public WorkspaceLayout Layout { get; }
}

/// <summary>
/// Composition root for one editor workspace. Owns the model, selection,
/// command history/router, layout persistence, theme, and UI projection.
/// Nothing here is static, and nothing depends on Avalonia.
/// </summary>
public sealed class EditorWorkspace
{
    private WorkspaceLayout _layout;
    private int _newNodeCounter = 1;
    private bool _applyingLayout;

    private EditorWorkspace(
        SceneDocument document,
        SelectionService selection,
        CommandHistory history,
        CommandRouter router,
        EditorLogService log,
        InMemoryUiStateStore uiState,
        ThemeManager theme,
        LayoutPersistenceService layoutStore,
        PlaceholderViewportHost viewport,
        WorkspaceLayout initialLayout)
    {
        Document = document;
        Selection = selection;
        History = history;
        Router = router;
        Log = log;
        UiState = uiState;
        Theme = theme;
        LayoutStore = layoutStore;
        Viewport = viewport;
        _layout = LayoutValidator.Normalize(initialLayout);

        Selection.Changed += (_, _) => PublishSelection();
        History.Undone += OnHistoryUndone;
        History.Redone += OnHistoryRedone;
        Theme.Changed += OnThemeChanged;
        Log.EntryAdded += (_, entry) => UiState.Set(EditorUiIds.ConsoleLast, "text", entry.Message);

        RegisterCommands();
        PublishSelection();
    }

    public SceneDocument Document { get; }
    public SelectionService Selection { get; }
    public CommandHistory History { get; }
    public CommandRouter Router { get; }
    public EditorLogService Log { get; }
    public InMemoryUiStateStore UiState { get; }
    public ThemeManager Theme { get; }
    public LayoutPersistenceService LayoutStore { get; }
    public PlaceholderViewportHost Viewport { get; }
    public WorkspaceLayout Layout => _layout;

    /// <summary>
    /// Set by the view so visibility toggles capture the user's current measured
    /// splitter sizes before a panel disappears.
    /// </summary>
    public Func<WorkspaceLayout?>? LayoutMeasurer { get; set; }

    /// <summary>Raised when the node tree changes shape (add/delete/undo of those).</summary>
    public event EventHandler? StructureChanged;

    /// <summary>Raised when panel visibility/size/theme layout values change.</summary>
    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

    public static EditorWorkspace CreateSample(string? layoutPath = null)
    {
        var document = SampleSceneFactory.Create();
        var workspace = new EditorWorkspace(
            document,
            new SelectionService(),
            new CommandHistory(contextId: document.ContextId),
            new CommandRouter(),
            new EditorLogService(),
            new InMemoryUiStateStore(),
            new ThemeManager(NovaThemeCatalog.BuiltIn),
            new LayoutPersistenceService(layoutPath),
            new PlaceholderViewportHost(new UiId("viewport.main")),
            WorkspaceLayout.Default);

        workspace.Selection.Select(document.Root.Children.FirstOrDefault()?.Id ?? document.Root.Id);
        workspace.Log.Info("Nova UI v0.2 offline workspace ready");
        workspace.UiState.Set(EditorUiIds.ViewportStatus, "text", "Viewport placeholder (no backend)");
        return workspace;
    }

    /// <summary>Loads layout off the UI critical path. Falls back to defaults on any failure.</summary>
    public async Task<LayoutLoadResult> InitializeLayoutAsync(CancellationToken cancellationToken = default)
    {
        // Resume on the caller's context: layout/theme events are observed by UI
        // subscribers. File I/O itself remains asynchronous inside the store.
        var result = await LayoutStore.LoadAsync(cancellationToken);
        ApplyLayout(result.Layout);

        if (result.UsedFallback)
            Log.Warning($"Layout fallback: {result.Reason}");
        else
            Log.Info($"Layout restored from {LayoutStore.FilePath}");

        return result;
    }

    public Task<LayoutSaveResult> SaveLayoutAsync(CancellationToken cancellationToken = default)
        => LayoutStore.SaveAsync(_layout, cancellationToken);

    public void ApplyLayout(WorkspaceLayout layout)
    {
        _layout = LayoutValidator.Normalize(layout);
        var requestedTheme = _layout.ThemeId;
        _applyingLayout = true;
        try
        {
            Theme.TrySwitch(requestedTheme);
        }
        finally
        {
            _applyingLayout = false;
        }

        if (!string.Equals(Theme.Current.Id, requestedTheme, StringComparison.Ordinal))
            _layout = _layout with { ThemeId = Theme.Current.Id };

        LayoutChanged?.Invoke(this, new LayoutChangedEventArgs(_layout));
    }

    public CommandResult TryEditProperty(UiId nodeId, string key, object? input)
    {
        var node = Document.Find(nodeId);
        if (node is null)
            return Fail($"edit {key}: node '{nodeId}' no longer exists");

        var definition = SceneNodeSchema.Find(node.Type, key);
        if (definition is null)
            return Fail($"edit {key}: property is not in the {node.Type} schema");

        if (!definition.TryCoerce(input, out var value, out var error))
            return Fail(error ?? "invalid value");

        var command = new EditPropertyCommand(
            Document,
            node,
            key,
            value,
            (id, propertyKey, applied) =>
                UiState.Set(id, $"property.{propertyKey}", applied),
            ex => LogProjectionFailure(ex));

        if (!command.CanExecute())
        {
            PublishProperty(node, key, node.GetProperty(key));
            return CommandResult.Ok("unchanged");
        }

        History.Execute(command);
        Log.Info($"{node.Name}.{key} = {Format(value)}");
        return CommandResult.Ok();
    }

    public CommandResult Execute(UiId commandId) => Router.Execute(commandId);

    public void PublishProperty(SceneNode node, string key, object? value)
        => UiState.Set(node.Id, $"property.{key}", value);

    public void PublishSelection()
    {
        var node = Selection.SelectedId is { } id ? Document.Find(id) : null;
        if (node is null)
        {
            UiState.Set(EditorUiIds.InspectorHeader, "text", "No selection");
            UiState.Set(EditorUiIds.InspectorType, "text", string.Empty);
            UiState.Set(EditorUiIds.SelectionStatus, "text", "No selection");
        }
        else
        {
            UiState.Set(EditorUiIds.InspectorHeader, "text", node.Name);
            UiState.Set(EditorUiIds.InspectorType, "text", node.Type);
            UiState.Set(EditorUiIds.SelectionStatus, "text", $"{node.Name} · {node.Type}");
        }

        var count = Document.AllNodes().Count();
        UiState.Set(EditorUiIds.HierarchyCount, "count", count);
        UiState.Set(EditorUiIds.HierarchyCount, "text", count.ToString(System.Globalization.CultureInfo.InvariantCulture));
    }

    private void RegisterCommands()
    {
        Router.Register(new CommandDescriptor(
            NovaCommandIds.Undo, "Undo", () => History.CanUndo,
            () =>
            {
                History.Undo();
                Log.Info("Undo");
            }, "Ctrl+Z"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.Redo, "Redo", () => History.CanRedo,
            () =>
            {
                History.Redo();
                Log.Info("Redo");
            }, "Ctrl+Y"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.AddNode, "Add Entity", () => true,
            () => AddEntity(), "Ctrl+Shift+N"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.DeleteNode, "Delete Entity",
            () => Selection.SelectedId is { } id
                  && Document.Find(id) is { } node
                  && !ReferenceEquals(node, Document.Root),
            () => DeleteSelected(), "Delete"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.SaveLayout, "Save Layout", () => true,
            () => _ = SaveLayoutAndReportAsync(), "Ctrl+Shift+S"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.ResetLayout, "Reset Layout", () => true,
            () =>
            {
                ApplyLayout(WorkspaceLayout.Default);
                _ = SaveLayoutAndReportAsync();
                Log.Info("Layout reset to defaults");
            }));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.ToggleHierarchy, "Toggle Hierarchy", () => true,
            () => ToggleLayout(layout => layout with { ShowHierarchy = !layout.ShowHierarchy })));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.ToggleInspector, "Toggle Inspector", () => true,
            () => ToggleLayout(layout => layout with { ShowInspector = !layout.ShowInspector })));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.ToggleConsole, "Toggle Console", () => true,
            () => ToggleLayout(layout => layout with { ShowConsole = !layout.ShowConsole })));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.ToggleTheme, "Toggle Theme", () => true,
            () =>
            {
                var theme = Theme.Toggle();
                Log.Info($"Theme: {theme.DisplayName}");
            }, "Ctrl+Shift+T"));
    }

    private void AddEntity()
    {
        var selected = Selection.SelectedId is { } id ? Document.Find(id) : null;
        var parent = selected ?? Document.Root;

        var name = $"Entity {_newNodeCounter++:D2}";
        var node = SceneNodeSchema.CreateNode(
            new UiId($"entity.{Guid.NewGuid():N}"),
            name,
            "Entity");

        var command = new AddNodeCommand(Document, parent, node);
        History.Execute(command);
        Selection.Select(node.Id);
        StructureChanged?.Invoke(this, EventArgs.Empty);
        Log.Info($"Added {name} under {parent.Name}");
    }

    private void DeleteSelected()
    {
        if (Selection.SelectedId is not { } id) return;
        var node = Document.Find(id);
        if (node is null || ReferenceEquals(node, Document.Root)) return;

        var parent = node.Parent ?? Document.Root;
        var command = new DeleteNodeCommand(Document, node);
        History.Execute(command);
        Selection.Select(parent.Id);
        StructureChanged?.Invoke(this, EventArgs.Empty);
        Log.Info($"Deleted {node.Name}");
    }

    private void ToggleLayout(Func<WorkspaceLayout, WorkspaceLayout> mutate)
    {
        var current = LayoutMeasurer?.Invoke() ?? _layout;
        ApplyLayout(mutate(current with { ThemeId = Theme.Current.Id }));
        _ = SaveLayoutAndReportAsync();
    }

    private async Task SaveLayoutAndReportAsync()
    {
        var result = await SaveLayoutAsync().ConfigureAwait(false);
        if (!result.Success)
            Log.Warning($"Layout save failed: {result.Error}");
    }

    private void LogProjectionFailure(Exception exception)
    {
        try { Log.Error($"UI projection failed: {exception.Message}"); }
        catch { /* diagnostics must never break the command path */ }
    }

    private void OnHistoryUndone(object? sender, UndoableCommandEventArgs e)
    {
        if (e.Command is AddNodeCommand or DeleteNodeCommand)
        {
            EnsureSelectionValid();
            StructureChanged?.Invoke(this, EventArgs.Empty);
            PublishSelection();
        }

        PublishFromCommand(e.Command);
    }

    private void OnHistoryRedone(object? sender, UndoableCommandEventArgs e)
    {
        if (e.Command is AddNodeCommand or DeleteNodeCommand)
        {
            StructureChanged?.Invoke(this, EventArgs.Empty);
            PublishSelection();
        }

        PublishFromCommand(e.Command);
    }

    private void PublishFromCommand(IUndoableCommand command)
    {
        if (command is EditPropertyCommand edit)
        {
            var node = Document.Find(edit.NodeId);
            if (node is not null)
                PublishProperty(node, edit.Key, node.GetProperty(edit.Key));
        }
    }

    private void OnThemeChanged(object? sender, ThemeChangedEventArgs e)
    {
        _layout = LayoutValidator.Normalize(_layout with { ThemeId = e.Current.Id });
        if (!_applyingLayout)
            LayoutChanged?.Invoke(this, new LayoutChangedEventArgs(_layout));
    }

    private void EnsureSelectionValid()
    {
        if (Selection.SelectedId is { } id && Document.Contains(id))
            return;
        Selection.Select(Document.Root.Id);
    }

    private CommandResult Fail(string message)
    {
        Log.Error(message);
        return CommandResult.Fail(message);
    }

    private static string Format(object? value) => value switch
    {
        null => "null",
        IFormattable formattable => formattable.ToString(null, System.Globalization.CultureInfo.InvariantCulture),
        _ => value.ToString() ?? string.Empty,
    };
}
