using Nova.Editor.Services.Commands;
using Nova.Editor.Services.Diagnostics;
using Nova.Editor.Services.Layout;
using Nova.Editor.Services.Model;
using Nova.Editor.Services.Projects;
using Nova.Editor.Services.Selection;
using Nova.Editor.Services.Viewport;
using Nova.UI;
using Nova.UI.Core.Commands;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;
using Nova.UI.Core.State;
using Nova.UI.Theme;

namespace Nova.Editor.Services;

public sealed class LayoutChangedEventArgs : EventArgs
{
    public LayoutChangedEventArgs(WorkspaceLayout layout) => Layout = layout;
    public WorkspaceLayout Layout { get; }
}

public sealed class LayoutSaveCompletedEventArgs(
    LayoutSaveResult result,
    bool userInitiated,
    bool shutdown) : EventArgs
{
    public LayoutSaveResult Result { get; } = result;

    public bool UserInitiated { get; } = userInitiated;

    public bool Shutdown { get; } = shutdown;
}

public enum LifecycleOutcome
{
    Success,
    Cancelled,
    Failed,
}

public sealed record LifecycleResult(LifecycleOutcome Outcome, string? Message = null)
{
    public bool Success => Outcome == LifecycleOutcome.Success;

    public static LifecycleResult Ok(string? message = null)
        => new(LifecycleOutcome.Success, message);

    public static LifecycleResult Cancelled(string? message = null)
        => new(LifecycleOutcome.Cancelled, message);

    public static LifecycleResult Failed(string message)
        => new(LifecycleOutcome.Failed, message);
}

public sealed class SessionChangedEventArgs : EventArgs
{
    public SessionChangedEventArgs(
        bool isOpen,
        Guid previousSessionId,
        Guid sessionId,
        Guid previousProjectId,
        Guid projectId,
        UiId previousSceneId,
        UiId sceneId)
    {
        IsOpen = isOpen;
        PreviousSessionId = previousSessionId;
        SessionId = sessionId;
        PreviousProjectId = previousProjectId;
        ProjectId = projectId;
        PreviousSceneId = previousSceneId;
        SceneId = sceneId;
    }

    public bool IsOpen { get; }
    public Guid PreviousSessionId { get; }
    public Guid SessionId { get; }
    public Guid PreviousProjectId { get; }
    public Guid ProjectId { get; }
    public UiId PreviousSceneId { get; }
    public UiId SceneId { get; }
}

/// <summary>
/// Composition root for one editor workspace. Owns the active project/scene
/// session, selection, command history/router, layout persistence, theme, and
/// UI projection. Nothing here is static, and nothing depends on Avalonia.
///
/// Lifecycle rule: a new scene document is created successfully *before* the
/// current session is invalidated. A failed load therefore leaves the previous
/// model, history, selection and draft context untouched.
/// </summary>
public sealed class EditorWorkspace
{
    private readonly SemaphoreSlim _saveGate = new(1, 1);
    private readonly LayoutSaveScheduler _saveScheduler;

    private WorkspaceLayout _layout;
    private int _newNodeCounter = 1;
    private bool _applyingLayout;
    private IDockPersistenceSource? _dockPersistence;
    private string? _lastSavedContent;
    private long _lastDockRevision;
    private long _layoutVersion;
    private bool _dockDirty;
    private bool _autoSaveEnabled;
    private bool _shuttingDown;
    private bool _persistenceSuppressed;
    private string? _persistenceSuppressionReason;
    private HashSet<PanelInstanceId> _acceptedUnresolvedPanels = new();

    private EditorWorkspace(
        EditorProject project,
        UiId sceneId,
        SceneDocument document,
        SelectionService selection,
        CommandHistory history,
        CommandRouter router,
        EditorLogService log,
        InMemoryUiStateStore uiState,
        ThemeManager theme,
        LayoutPersistenceService layoutStore,
        PlaceholderViewportHost viewport,
        WorkspaceLayout initialLayout,
        Func<TimeSpan, CancellationToken, Task>? autoSaveDelay = null)
    {
        Project = project;
        SceneId = sceneId;
        Document = document;
        SessionId = document.ContextId;
        IsOpen = true;
        Selection = selection;
        History = history;
        Router = router;
        Log = log;
        UiState = uiState;
        Theme = theme;
        LayoutStore = layoutStore;
        Viewport = viewport;
        _layout = LayoutValidator.Normalize(initialLayout);
        _saveScheduler = new LayoutSaveScheduler(autoSaveDelay);
        _saveScheduler.Elapsed += OnAutoSaveElapsed;

        Selection.Changed += (_, _) => PublishSelection();
        History.Undone += OnHistoryUndone;
        History.Redone += OnHistoryRedone;
        Theme.Changed += OnThemeChanged;
        Log.EntryAdded += (_, entry) => UiState.Set(EditorUiIds.ConsoleLast, "text", entry.Message);

        RegisterCommands();
        PublishSelection();
    }

    public SceneDocument Document { get; private set; }
    public EditorProject Project { get; private set; }
    public Guid ProjectId => Project.ProjectId;
    public UiId SceneId { get; private set; }
    public Guid SessionId { get; private set; }
    public bool IsOpen { get; private set; }

    public SelectionService Selection { get; }
    public CommandHistory History { get; }
    public CommandRouter Router { get; }
    public EditorLogService Log { get; }
    public InMemoryUiStateStore UiState { get; }
    public ThemeManager Theme { get; }
    public LayoutPersistenceService LayoutStore { get; }
    public PlaceholderViewportHost Viewport { get; }
    public WorkspaceLayout Layout => _layout;

    public bool IsPersistenceSuppressed => _persistenceSuppressed;

    public string? PersistenceSuppressionReason => _persistenceSuppressionReason;

    public bool HasUnsavedDockLayout => _dockDirty;

    /// <summary>
    /// Set by the view so visibility toggles capture the user's current measured
    /// splitter sizes before a panel disappears.
    /// </summary>
    public Func<WorkspaceLayout?>? LayoutMeasurer { get; set; }

    /// <summary>Raised when the node tree changes shape (add/delete/undo of those).</summary>
    public event EventHandler? StructureChanged;

    /// <summary>Raised after a project/scene/session transition, including close.</summary>
    public event EventHandler<SessionChangedEventArgs>? SessionChanged;

    /// <summary>Raised when panel visibility/size/theme layout values change.</summary>
    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

    /// <summary>Raised after an explicit, automatic or shutdown save attempt.</summary>
    public event EventHandler<LayoutSaveCompletedEventArgs>? LayoutSaveCompleted;

    /// <summary>Raised when a layout undo/redo becomes possible or impossible.</summary>
    public event EventHandler? LayoutHistoryStateChanged;

    /// <summary>Raised once per Undo/Redo Layout command, with its outcome.</summary>
    public event EventHandler<DockLayoutHistoryEventArgs>? LayoutHistoryExecuted;

    public static EditorWorkspace CreateSample(
        string? layoutPath = null,
        Func<TimeSpan, CancellationToken, Task>? autoSaveDelay = null)
    {
        var project = SampleProjectFactory.Create();
        var scene = project.Scenes[0];
        var sessionId = Guid.NewGuid();
        var document = scene.CreateDocument(sessionId);

        var workspace = new EditorWorkspace(
            project,
            scene.Id,
            document,
            new SelectionService(),
            new CommandHistory(contextId: document.ContextId),
            new CommandRouter(),
            new EditorLogService(),
            new InMemoryUiStateStore(),
            new ThemeManager(NovaThemeCatalog.BuiltIn),
            new LayoutPersistenceService(layoutPath),
            new PlaceholderViewportHost(new UiId("viewport.main")),
            WorkspaceLayout.Default,
            autoSaveDelay);

        workspace.Selection.Select(document.Root.Children.FirstOrDefault()?.Id ?? document.Root.Id);
        workspace.Log.Info("Nova UI v0.2 offline workspace ready");
        workspace.UiState.Set(EditorUiIds.ViewportStatus, "text", "Viewport placeholder (no backend)");
        return workspace;
    }

    public LifecycleResult OpenProject(EditorProject project, UiId? sceneId = null)
    {
        ArgumentNullException.ThrowIfNull(project);
        var scene = sceneId is { } requested
            ? project.FindScene(requested)
            : project.Scenes[0];

        if (scene is null)
            return LifecycleResult.Failed($"scene '{sceneId}' is not part of project '{project.Name}'");

        return Transition(project, scene, project.DefaultLayout ?? WorkspaceLayout.Default);
    }

    public LifecycleResult OpenScene(UiId sceneId)
    {
        var scene = Project.FindScene(sceneId);
        if (scene is null)
            return LifecycleResult.Failed($"scene '{sceneId}' is not part of project '{Project.Name}'");

        // Scene switch keeps the current workspace layout, but creates a new
        // session/context so old commands cannot touch the new scene.
        return Transition(Project, scene, layoutToApply: null);
    }

    /// <summary>
    /// Closes the active project. The confirmation callback runs before any
    /// destructive change, so a cancelled close preserves history/selection.
    /// </summary>
    public LifecycleResult CloseProject(Func<bool>? confirm = null, bool saveLayout = true)
    {
        if (!IsOpen)
            return LifecycleResult.Ok("project is already closed");

        if (confirm is not null && !confirm())
            return LifecycleResult.Cancelled("close cancelled by user");

        var previousSession = SessionId;
        var previousProject = ProjectId;
        var previousScene = SceneId;

        IsOpen = false;
        SessionId = Guid.NewGuid();
        History.Reset(SessionId);
        Selection.Clear();
        UiState.Clear();

        if (saveLayout)
            _ = SaveLayoutAndReportAsync();

        RaiseSessionChanged(new SessionChangedEventArgs(
            isOpen: false,
            previousSession,
            SessionId,
            previousProject,
            ProjectId,
            previousScene,
            SceneId));

        Log.Info($"Project closed: {Project.Name}");
        return LifecycleResult.Ok();
    }

    /// <summary>Loads layout off the UI critical path. Falls back to defaults on any failure.</summary>
    public async Task<LayoutLoadResult> InitializeLayoutAsync(CancellationToken cancellationToken = default)
    {
        // Resume on the caller's context: layout/theme events are observed by UI
        // subscribers. File I/O itself remains asynchronous inside the store.
        var result = await LayoutStore.LoadAsync(cancellationToken);
        ApplyLayout(result.Layout);

        _persistenceSuppressed = result.PersistenceSuppressed;
        _persistenceSuppressionReason = result.PersistenceSuppressed
            ? DescribeSuppression(result)
            : null;
        _dockDirty = false;

        // A fallback layout was not read from a matching file, and a suppressed
        // load (unresolved panels or unreadable file) must not be treated as
        // already saved: either case would make the dedupe path skip the first
        // explicit save.
        _lastSavedContent = result.UsedFallback || result.PersistenceSuppressed
            ? null
            : TrySerializeLayout(result.Layout);

        if (result.UsedFallback)
            Log.Warning($"Layout fallback: {result.Reason}");
        else
            Log.Info($"Layout restored from {LayoutStore.FilePath}");

        if (result.PersistenceSuppressed)
            Log.Warning($"Layout autosave paused: {_persistenceSuppressionReason}");

        return result;
    }

    public Task<LayoutSaveResult> SaveLayoutAsync(CancellationToken cancellationToken = default)
        => SaveLifecycleAsync(userInitiated: true, cancellationToken);

    /// <summary>
    /// Explicit save used by the shell. The measured layout is applied first so
    /// window geometry is part of the snapshot; the health gate still applies.
    /// </summary>
    public async Task<LayoutSaveResult> SaveCurrentLayoutAsync(
        WorkspaceLayout? measured = null,
        bool userInitiated = true,
        CancellationToken cancellationToken = default)
    {
        if (measured is not null && !_shuttingDown)
            ApplyLayout(measured);

        return await SaveLifecycleAsync(userInitiated, cancellationToken);
    }

    /// <summary>
    /// Connects the live dock coordinator. Auto-save is deliberately not
    /// scheduled here: load-stage merges are dirty but must not be written
    /// immediately on startup.
    /// </summary>
    public void AttachDockPersistence(IDockPersistenceSource source, bool dirty = false)
    {
        ArgumentNullException.ThrowIfNull(source);
        DetachDockPersistence();

        _dockPersistence = source;
        _lastDockRevision = source.Revision;
        _dockDirty = dirty;
        source.PersistenceStateChanged += OnDockPersistenceStateChanged;
        RaiseLayoutHistoryStateChanged();
    }

    public void DetachDockPersistence()
    {
        if (_dockPersistence is { } source)
            source.PersistenceStateChanged -= OnDockPersistenceStateChanged;

        _dockPersistence = null;
        _autoSaveEnabled = false;
        _saveScheduler.Cancel();
        RaiseLayoutHistoryStateChanged();
    }

    public void EnableAutoSave() => _autoSaveEnabled = true;

    /// <summary>
    /// Blocks ordinary automatic writes until an explicit save/reset succeeds.
    /// Used when the dock candidate cannot be restored at all, so a fallback
    /// default must not silently replace a structurally valid file.
    /// </summary>
    public void SuppressPersistence(string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);
        _persistenceSuppressed = true;
        _persistenceSuppressionReason = reason;
        _saveScheduler.Cancel();
    }

    /// <summary>
    /// Cancels pending timers/interactions, stops normal scheduling and returns
    /// the immutable snapshot to write after BeginShutdown. Null means the
    /// current state must not replace the previous valid file.
    /// </summary>
    public WorkspaceLayout? CaptureShutdownSnapshot(out string? reason)
    {
        if (_shuttingDown)
        {
            reason = "the layout shutdown snapshot was already captured";
            return null;
        }

        _shuttingDown = true;
        _saveScheduler.Cancel();
        _dockPersistence?.CancelPendingInteraction();

        if (LayoutMeasurer?.Invoke() is { } measured)
        {
            _layout = LayoutValidator.Normalize(
                measured with { ThemeId = Theme.Current.Id });
        }

        if (_persistenceSuppressed)
        {
            reason = $"layout autosave is paused: {_persistenceSuppressionReason}";
            return null;
        }

        if (_dockPersistence is { } source)
        {
            var capture = source.Capture();
            _lastDockRevision = capture.Revision;
            if (!capture.CanPersist)
            {
                reason = capture.Reason ?? "the dock workspace is not persistable";
                return null;
            }

            reason = null;
            return LayoutValidator.Normalize(
                _layout with
                {
                    Dock = capture.Snapshot,
                    ThemeId = Theme.Current.Id,
                });
        }

        reason = null;
        return LayoutValidator.Normalize(_layout);
    }

    /// <summary>
    /// Writes a previously captured shutdown snapshot. Hosts are already shut
    /// down; this does not re-check health, re-dock floats, or retry.
    /// </summary>
    public async Task<LayoutSaveResult> SaveShutdownSnapshotAsync(
        WorkspaceLayout snapshot,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        await _saveGate.WaitAsync(cancellationToken);
        try
        {
            var result = await LayoutStore.SaveAsync(snapshot, cancellationToken);
            if (result.Success)
            {
                _lastSavedContent = null;
                _dockDirty = false;
                _persistenceSuppressed = false;
                _persistenceSuppressionReason = null;
            }
            else
            {
                Log.Warning($"Final layout save failed: {result.Error}");
            }

            RaiseLayoutSaveCompleted(result, userInitiated: false, shutdown: true);
            return result;
        }
        finally
        {
            _saveGate.Release();
        }
    }

    private async Task<LayoutSaveResult> SaveLifecycleAsync(
        bool userInitiated,
        CancellationToken cancellationToken)
    {
        if (_shuttingDown)
            return LayoutSaveResult.Skip("the shutdown snapshot is already captured");

        if (userInitiated)
            _saveScheduler.Cancel();

        // Capture the visible geometry before waiting behind another write so the
        // snapshot is the one the user is looking at, not a later animation frame.
        if (LayoutMeasurer?.Invoke() is { } measured)
            _layout = LayoutValidator.Normalize(measured with { ThemeId = Theme.Current.Id });

        var source = _dockPersistence;
        var layoutVersion = _layoutVersion;
        var capturedRevision = source?.Revision ?? _lastDockRevision;

        await _saveGate.WaitAsync(cancellationToken);
        try
        {
            if (_shuttingDown)
                return LayoutSaveResult.Skip("the shutdown snapshot is already captured");

            if (_persistenceSuppressed && !userInitiated)
            {
                var suppressed = LayoutSaveResult.Skip(
                    _persistenceSuppressionReason ?? "layout persistence is suppressed");
                RaiseLayoutSaveCompleted(suppressed, userInitiated, shutdown: false);
                return suppressed;
            }

            DockWorkspaceSnapshot? dock = null;
            if (source is not null)
            {
                var capture = source.Capture();
                capturedRevision = capture.Revision;
                if (!capture.CanPersist)
                {
                    var unhealthy = LayoutSaveResult.Skip(
                        capture.Reason ?? "the dock workspace is not persistable");
                    RaiseLayoutSaveCompleted(unhealthy, userInitiated, shutdown: false);
                    return unhealthy;
                }

                dock = capture.Snapshot;
            }

            var candidate = LayoutValidator.Normalize(
                _layout with { ThemeId = Theme.Current.Id, Dock = dock ?? _layout.Dock });

            string json;
            try
            {
                // Canonical bytes are the dedupe key and are produced before any
                // file is created or replaced.
                json = WorkspaceLayoutSerializer.Serialize(candidate);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                var failure = LayoutSaveResult.Fail($"layout could not be serialized: {ex.Message}");
                RaiseLayoutSaveCompleted(failure, userInitiated, shutdown: false);
                return failure;
            }

            if (string.Equals(json, _lastSavedContent, StringComparison.Ordinal))
            {
                if (source is null || source.Revision == capturedRevision)
                {
                    _lastDockRevision = capturedRevision;
                    _dockDirty = false;
                }

                var unchanged = LayoutSaveResult.Skip("the layout is unchanged");
                RaiseLayoutSaveCompleted(unchanged, userInitiated, shutdown: false);
                return unchanged;
            }

            LayoutSaveResult result;
            try
            {
                result = await LayoutStore.SaveAsync(candidate, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                result = LayoutSaveResult.Fail(ex.Message);
            }

            if (result.Success)
            {
                _lastSavedContent = json;
                // The file now holds exactly the unresolved identities this write
                // saw, so their continued presence is no longer a reappearance.
                AcceptUnresolvedPanels();
                if (userInitiated)
                {
                    _persistenceSuppressed = false;
                    _persistenceSuppressionReason = null;
                }

                var revisionUnchanged = source is null || source.Revision == capturedRevision;
                if (revisionUnchanged)
                {
                    _lastDockRevision = capturedRevision;
                    _dockDirty = false;
                }
                else
                {
                    _dockDirty = true;
                }

                if (_layoutVersion != layoutVersion || !revisionUnchanged)
                    ScheduleAutoSave();
            }

            RaiseLayoutSaveCompleted(result, userInitiated, shutdown: false);
            return result;
        }
        finally
        {
            _saveGate.Release();
        }
    }

    private void OnDockPersistenceStateChanged(object? sender, EventArgs e)
    {
        if (_shuttingDown || _dockPersistence is not { } source)
            return;

        if (source.Revision != _lastDockRevision)
            _dockDirty = true;

        PausePersistenceForUnresolvedPanels(source);
        RaiseLayoutHistoryStateChanged();

        if (_dockDirty)
            ScheduleAutoSave();
    }

    /// <summary>
    /// Re-arms the unresolved-panel protection when an identity this build never
    /// wrote back into the live workspace — undoing a layout change can bring a
    /// panel back that an explicit reset had already cleared. An identity an
    /// explicit save already accepted stays accepted, so ordinary interaction
    /// with a placeholder panel cannot pause persistence again.
    /// </summary>
    private void PausePersistenceForUnresolvedPanels(IDockPersistenceSource source)
    {
        if (_persistenceSuppressed
            || source is not IDockUnresolvedPanelSource unresolved)
        {
            return;
        }

        var reappeared = unresolved.UnresolvedPanels
            .Where(panel => !_acceptedUnresolvedPanels.Contains(panel))
            .ToArray();
        if (reappeared.Length == 0)
            return;

        _persistenceSuppressed = true;
        _persistenceSuppressionReason = DescribeUnresolvedPanels(reappeared);
        _saveScheduler.Cancel();
        Log.Warning($"Layout autosave paused: {_persistenceSuppressionReason}");
    }

    /// <summary>
    /// Records that the current write accepted the unresolved identities it just
    /// saw, so their continued presence is no longer treated as a reappearance.
    /// </summary>
    private void AcceptUnresolvedPanels()
        => _acceptedUnresolvedPanels = _dockPersistence is IDockUnresolvedPanelSource source
            ? new HashSet<PanelInstanceId>(source.UnresolvedPanels)
            : new HashSet<PanelInstanceId>();

    private IDockLayoutHistorySource? LayoutHistory
        => _dockPersistence as IDockLayoutHistorySource;

    /// <summary>
    /// Runs one layout replay through the attached dock source and reports the
    /// outcome, so the shell can state a rejection instead of swallowing it.
    /// </summary>
    private void ReplayLayoutHistory(bool undo)
    {
        var result = LayoutHistory is { } history
            ? (undo ? history.UndoLayout() : history.RedoLayout())
            : DockOperationResult.Fail(DockError.InvalidModel, "no dock workspace is attached");

        LayoutHistoryExecuted?.Invoke(this, new DockLayoutHistoryEventArgs(undo, result));
        Log.Info(undo
            ? $"Layout undo: {result.Diagnostic}"
            : $"Layout redo: {result.Diagnostic}");
        RaiseLayoutHistoryStateChanged();
    }

    private void RaiseLayoutHistoryStateChanged()
        => LayoutHistoryStateChanged?.Invoke(this, EventArgs.Empty);

    private async void OnAutoSaveElapsed(object? sender, EventArgs e)
    {
        try
        {
            var result = await SaveLifecycleAsync(userInitiated: false, CancellationToken.None);
            if (result.Status == LayoutSaveStatus.Failed)
                Log.Warning($"Automatic layout save failed: {result.Error}");
        }
        catch (Exception ex)
        {
            Log.Warning($"Automatic layout save failed: {ex.Message}");
        }
    }

    private void ScheduleAutoSave()
    {
        if (!_autoSaveEnabled || _shuttingDown || _persistenceSuppressed)
            return;

        _saveScheduler.Schedule();
    }

    private void RaiseLayoutSaveCompleted(
        LayoutSaveResult result,
        bool userInitiated,
        bool shutdown)
    {
        var handlers = LayoutSaveCompleted;
        if (handlers is null) return;

        var args = new LayoutSaveCompletedEventArgs(result, userInitiated, shutdown);
        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler<LayoutSaveCompletedEventArgs>)handler)(this, args);
            }
            catch
            {
                // Status observers cannot roll back or block a completed write.
            }
        }
    }

    private static string? TrySerializeLayout(WorkspaceLayout layout)
    {
        try
        {
            return WorkspaceLayoutSerializer.Serialize(layout);
        }
        catch
        {
            return null;
        }
    }

    private static string DescribeSuppression(LayoutLoadResult result)
    {
        if (result.FallbackKind == LayoutFallbackKind.ReadFailed)
            return $"the layout file could not be read and was left untouched ({result.Reason})";

        if (result.UnresolvedPanels is { Count: > 0 } unresolved)
            return DescribeUnresolvedPanels(unresolved);

        return result.Reason ?? "layout persistence is suppressed";
    }

    private static string DescribeUnresolvedPanels(
        IReadOnlyList<PanelInstanceId> panels)
        => "the layout contains panels this build cannot resolve ("
           + string.Join(", ", panels.Select(panel => panel.ToString()))
           + "); automatic writes are paused until an explicit Save Layout";

    public void ApplyLayout(WorkspaceLayout layout)
    {
        _layout = LayoutValidator.Normalize(layout);
        _layoutVersion++;
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

        RaiseLayoutChanged(new LayoutChangedEventArgs(_layout));
    }

    public CommandResult TryEditProperty(UiId nodeId, string key, object? input)
    {
        if (!IsOpen)
            return Fail("edit rejected: no project is open");

        var node = Document.Find(nodeId);
        if (node is null)
        {
            // External deletion: never let the Inspector keep writing to a dead
            // target. Clear selection so the projection also drops the stale row.
            if (Selection.SelectedId == nodeId)
                Selection.Clear();

            return Fail($"edit {key}: node '{nodeId}' no longer exists");
        }

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
        if (!IsOpen)
        {
            UiState.Set(EditorUiIds.InspectorHeader, "text", "No project");
            UiState.Set(EditorUiIds.InspectorType, "text", string.Empty);
            UiState.Set(EditorUiIds.SelectionStatus, "text", "No project open");
            UiState.Set(EditorUiIds.HierarchyCount, "count", 0);
            UiState.Set(EditorUiIds.HierarchyCount, "text", "0");
            return;
        }

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

    private LifecycleResult Transition(
        EditorProject project,
        SceneDescriptor scene,
        WorkspaceLayout? layoutToApply)
    {
        var candidateSession = Guid.NewGuid();
        SceneDocument candidate;
        try
        {
            candidate = scene.CreateDocument(candidateSession);
        }
        catch (Exception ex)
        {
            // Fail before invalidating the current session.
            Log.Warning($"Scene load failed for '{scene.Name}': {ex.Message}");
            return LifecycleResult.Failed($"scene load failed: {ex.Message}");
        }

        var previousSession = SessionId;
        var previousProject = ProjectId;
        var previousScene = SceneId;

        Project = project;
        SceneId = scene.Id;
        Document = candidate;
        SessionId = candidate.ContextId;
        IsOpen = true;

        History.Reset(SessionId);
        Selection.Clear();
        UiState.Clear();

        if (layoutToApply is not null)
            ApplyLayout(layoutToApply);

        var defaultSelection = candidate.Root.Children.FirstOrDefault()?.Id ?? candidate.Root.Id;
        Selection.Select(defaultSelection);

        RaiseSessionChanged(new SessionChangedEventArgs(
            isOpen: true,
            previousSession,
            SessionId,
            previousProject,
            ProjectId,
            previousScene,
            SceneId));

        Log.Info($"Opened scene '{scene.Name}' in project '{project.Name}'");
        return LifecycleResult.Ok();
    }

    private void RegisterCommands()
    {
        Router.Register(new CommandDescriptor(
            NovaCommandIds.Undo, "Undo", () => IsOpen && History.CanUndo,
            () =>
            {
                History.Undo();
                Log.Info("Undo");
            }, "Ctrl+Z"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.Redo, "Redo", () => IsOpen && History.CanRedo,
            () =>
            {
                History.Redo();
                Log.Info("Redo");
            }, "Ctrl+Y"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.AddNode, "Add Entity", () => IsOpen,
            () => AddEntity(), "Ctrl+Shift+N"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.DeleteNode, "Delete Entity",
            () => IsOpen
                  && Selection.SelectedId is { } id
                  && Document.Find(id) is { } node
                  && !ReferenceEquals(node, Document.Root),
            () => DeleteSelected(), "Delete"));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.SaveLayout, "Save Layout", () => true,
            () => _ = SaveLayoutAndReportAsync(userInitiated: true), "Ctrl+Shift+S"));

        // The paired save is contractual: discarding identities writes nothing,
        // and only a user-requested write ends the persistence pause.
        Router.Register(new CommandDescriptor(
            NovaCommandIds.ResetLayout, "Reset Layout", () => true,
            () =>
            {
                ApplyLayout(WorkspaceLayout.Default);
                _ = SaveLayoutAndReportAsync(userInitiated: true);
                Log.Info("Layout reset to defaults");
            }));

        // No gesture: Ctrl+Z and Ctrl+Y stay the scene history. Layout replay is
        // a menu command because its undo stack is a different one.
        Router.Register(new CommandDescriptor(
            NovaCommandIds.UndoLayout, "Undo Layout",
            () => LayoutHistory is { CanUndoLayout: true },
            () => ReplayLayoutHistory(undo: true)));

        Router.Register(new CommandDescriptor(
            NovaCommandIds.RedoLayout, "Redo Layout",
            () => LayoutHistory is { CanRedoLayout: true },
            () => ReplayLayoutHistory(undo: false)));

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
        if (!IsOpen) return;
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
        RaiseStructureChanged();
        Log.Info($"Added {name} under {parent.Name}");
    }

    private void DeleteSelected()
    {
        if (!IsOpen || Selection.SelectedId is not { } id) return;
        var node = Document.Find(id);
        if (node is null || ReferenceEquals(node, Document.Root)) return;

        var parent = node.Parent ?? Document.Root;
        var command = new DeleteNodeCommand(Document, node);
        History.Execute(command);
        Selection.Select(parent.Id);
        RaiseStructureChanged();
        Log.Info($"Deleted {node.Name}");
    }

    private void ToggleLayout(Func<WorkspaceLayout, WorkspaceLayout> mutate)
    {
        var current = LayoutMeasurer?.Invoke() ?? _layout;
        ApplyLayout(mutate(current with { ThemeId = Theme.Current.Id }));
        ScheduleAutoSave();
    }

    private async Task SaveLayoutAndReportAsync(bool userInitiated = false)
    {
        try
        {
            var result = await SaveLifecycleAsync(userInitiated, CancellationToken.None);
            if (result.Status == LayoutSaveStatus.Failed)
                Log.Warning($"Layout save failed: {result.Error}");
        }
        catch (Exception ex)
        {
            Log.Warning($"Layout save failed: {ex.Message}");
        }
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
            RaiseStructureChanged();
            PublishSelection();
        }

        PublishFromCommand(e.Command);
    }

    private void OnHistoryRedone(object? sender, UndoableCommandEventArgs e)
    {
        if (e.Command is AddNodeCommand or DeleteNodeCommand)
        {
            RaiseStructureChanged();
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
        _layoutVersion++;
        if (!_applyingLayout)
        {
            RaiseLayoutChanged(new LayoutChangedEventArgs(_layout));
            ScheduleAutoSave();
        }
    }

    private void EnsureSelectionValid()
    {
        if (!IsOpen)
        {
            Selection.Clear();
            return;
        }

        if (Selection.SelectedId is { } id && Document.Contains(id))
            return;
        Selection.Select(Document.Root.Id);
    }

    private void RaiseStructureChanged()
    {
        var handlers = StructureChanged;
        if (handlers is null) return;

        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler)handler)(this, EventArgs.Empty);
            }
            catch
            {
                // A projection subscriber must not break a recorded command.
            }
        }
    }

    private void RaiseLayoutChanged(LayoutChangedEventArgs args)
    {
        var handlers = LayoutChanged;
        if (handlers is null) return;

        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler<LayoutChangedEventArgs>)handler)(this, args);
            }
            catch
            {
                // Layout observers are projections; they cannot abort a transition.
            }
        }
    }

    private void RaiseSessionChanged(SessionChangedEventArgs args)
    {
        var handlers = SessionChanged;
        if (handlers is null) return;

        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler<SessionChangedEventArgs>)handler)(this, args);
            }
            catch
            {
                // Lifecycle observers cannot roll back a completed transition.
            }
        }
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
