using Avalonia.Controls;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Views.Docking;

/// <summary>
/// Editor-facing facade over the multi-root dock workspace. The coordinator
/// owns the committed workspace and every root host; this class keeps the
/// single-main-root API used by the editor shell.
/// </summary>
public sealed class EditorDockWorkspace
    : IDisposable, IDockPersistenceSource, IDockUnresolvedPanelSource, IDockLayoutHistorySource
{
    private readonly DockPanelViewRegistry _registry;
    private readonly DockProjectorRootHost _mainHost;
    private readonly DockWorkspaceCoordinator _coordinator;
    private readonly IDockNodeApplyInterceptor? _interceptor;
    private bool _disposed;
    private bool _retrying;
    private int _suppressNotifications;

    public EditorDockWorkspace(
        EditorShellViewModel viewModel,
        IDockNodeApplyInterceptor? interceptor = null,
        IDockPanelViewFactory? panelFactory = null,
        DockRootHostFactory? floatHostFactory = null,
        DockWorkspace? restoreWorkspace = null)
    {
        ArgumentNullException.ThrowIfNull(viewModel);
        _interceptor = interceptor;
        _registry = new DockPanelViewRegistry(
            panelFactory ?? new EditorDockPanelFactory(viewModel));
        _mainHost = new DockProjectorRootHost(
            DockRootHostDescriptor.ForMain(),
            _registry,
            interceptor);

        var factory = floatHostFactory ?? CreateFloatHost;
        if (restoreWorkspace is null)
        {
            _coordinator = new DockWorkspaceCoordinator(
                DockWorkspace.CreateDefault(),
                _mainHost,
                _registry,
                factory);
        }
        else
        {
            var restore = DockWorkspaceCoordinator.Restore(
                restoreWorkspace,
                _mainHost,
                _registry,
                factory);
            RestoreMergedFloatRoots = restore.MergedFloatRoots;
            RestoreDiagnostics = restore.Diagnostics;
            RestoreFailure = restore.Failure;
            _coordinator = restore.Success
                ? restore.Coordinator!
                : new DockWorkspaceCoordinator(
                    DockWorkspace.CreateDefault(),
                    _mainHost,
                    _registry,
                    factory);
        }

        _coordinator.StateChanged += OnCoordinatorStateChanged;
        _coordinator.DragEnded += OnCoordinatorDragEnded;
    }

    public Control Root => _mainHost.Root;

    public DockWorkspace Workspace => _coordinator.Workspace;

    public DockTree Tree => _coordinator.Workspace.MainRoot;

    public DockApplyResult? Current => _mainHost.Current;

    /// <summary>Floating roots merged inside the candidate during startup restore.</summary>
    public IReadOnlyList<DockFloatRootId> RestoreMergedFloatRoots { get; } =
        Array.Empty<DockFloatRootId>();

    public IReadOnlyList<string> RestoreDiagnostics { get; } = Array.Empty<string>();

    /// <summary>Non-null when the candidate was discarded and the default dock installed.</summary>
    public string? RestoreFailure { get; }

    public long Revision => _coordinator.Workspace.Revision;

    public bool IsDisposed => _disposed;

    public bool IsDegraded => Current is { Status: DockApplyStatus.Degraded };

    public bool IsRetrying => _retrying;

    public bool IsDragging => _coordinator.IsDragging;

    public int FloatCount => _coordinator.Workspace.FloatingRoots.Count;

    public bool HasDegradedHosts => _coordinator.HasDegradedHosts;

    public int DegradedFloatCount => _coordinator.EnumerateHostIds()
        .Count(id => !id.IsMain
                     && _coordinator.FindHost(id) is { IsHealthy: false });

    public event EventHandler? StateChanged;

    /// <summary>
    /// Raised for every model commit, drag end, retry and host-health result.
    /// The editor uses it to debounce persistence, not to rebuild visuals.
    /// </summary>
    public event EventHandler? PersistenceStateChanged;

    public DockApplyResult Apply(DockTree tree)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        ArgumentNullException.ThrowIfNull(tree);
        _coordinator.ReplaceMainRoot(tree);
        return RequireCurrent();
    }

    public DockApplyResult Retry()
    {
        ThrowIfDisposed();
        if (_retrying)
            throw new InvalidOperationException("A dock retry is already in progress.");

        _retrying = true;
        _suppressNotifications++;
        try
        {
            _coordinator.Retry(DockRootHostId.Main);
            foreach (var hostId in _coordinator.EnumerateHostIds())
            {
                if (!hostId.IsMain
                    && _coordinator.FindHost(hostId) is { IsHealthy: false })
                {
                    _coordinator.Retry(hostId);
                }
            }
        }
        finally
        {
            _suppressNotifications--;
            _retrying = false;
        }

        // Every notification raised inside the retry was published while
        // IsRetrying still blocked layout replay, so the editor has to re-read
        // host health and command availability now that the flag is down.
        PersistenceStateChanged?.Invoke(this, EventArgs.Empty);
        NotifyStateChanged();
        return RequireCurrent();
    }

    public DockOperationResult ActivateTab(DockNodeId groupId, PanelInstanceId panel)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(
            _coordinator.ActivateTab(DockRootHostId.Main, groupId, panel));
    }

    public DockOperationResult MovePanel(PanelInstanceId panel, DockTarget target)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.MovePanel(panel, target));
    }

    public DockOperationResult RemovePanel(PanelInstanceId panel)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.ClosePanel(panel));
    }

    public DockOperationResult HidePanel(PanelInstanceId panel)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.HidePanel(panel));
    }

    public DockOperationResult ShowPanel(PanelInstanceId panel)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.ShowPanel(panel));
    }

    public DockOperationResult ResizeSplit(
        DockNodeId splitId,
        double firstWeight,
        double secondWeight)
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(
            _coordinator.ResizeSplit(
                DockRootHostId.Main, splitId, firstWeight, secondWeight));
    }

    public DockApplyResult ResetToDefault()
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        _coordinator.RestoreDefault();
        return RequireCurrent();
    }

    /// <summary>
    /// Layout history is this process only: it starts from the layout the
    /// session was built with and is never written to the layout file.
    /// </summary>
    public bool CanUndoLayout
        => !_disposed && !IsRetrying && _coordinator.CanUndoLayout;

    public bool CanRedoLayout
        => !_disposed && !IsRetrying && _coordinator.CanRedoLayout;

    public DockOperationResult UndoLayout()
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.UndoLayout());
    }

    public DockOperationResult RedoLayout()
    {
        ThrowIfDisposed();
        ThrowIfRetrying();
        return ToOperation(_coordinator.RedoLayout());
    }

    /// <summary>Closes every float host without re-docking, before the app exits.</summary>
    public DockHostResult BeginShutdown()
    {
        ThrowIfDisposed();
        return _coordinator.BeginShutdown();
    }

    public DockPersistenceCapture Capture()
    {
        var revision = Revision;
        if (_disposed)
            return DockPersistenceCapture.Blocked("dock workspace is disposed", revision);

        if (_coordinator.IsShuttingDown)
            return DockPersistenceCapture.Blocked("dock workspace is shutting down", revision);

        if (_retrying)
            return DockPersistenceCapture.Blocked("a dock retry is in progress", revision);

        if (_coordinator.IsDragging)
            return DockPersistenceCapture.Blocked("a dock drag is in progress", revision);

        var workspace = _coordinator.Workspace;
        if (_coordinator.HostCount != workspace.FloatingRoots.Count + 1)
        {
            return DockPersistenceCapture.Blocked(
                "dock hosts do not match the restored roots",
                revision);
        }

        if (!TryValidateHost(DockRootHostId.Main, workspace.MainRoot, out var reason))
            return DockPersistenceCapture.Blocked(reason!, revision);

        foreach (var floatRoot in workspace.FloatingRoots)
        {
            var hostId = DockRootHostId.ForFloat(floatRoot.Id);
            if (!TryValidateHost(hostId, floatRoot.Root, out reason))
                return DockPersistenceCapture.Blocked(reason!, revision);
        }

        return DockPersistenceCapture.Ready(
            revision,
            DockWorkspaceSnapshotCodec.FromWorkspace(workspace));
    }

    public void CancelPendingInteraction() => _coordinator.CancelDrag();

    public Control? FindNode(DockNodeId id) => _mainHost.FindNode(id);

    public IDockPanelView? FindPanel(PanelInstanceId panel)
    {
        if (_disposed)
            return null;

        return _registry.TryGet(panel, out var registration)
            ? registration.View
            : null;
    }

    /// <summary>Hidden panels remain in their groups but have no mounted view.</summary>
    public IReadOnlyList<PanelInstanceId> HiddenPanels
    {
        get
        {
            if (_disposed)
                return Array.Empty<PanelInstanceId>();

            return _coordinator.Workspace.EnumerateHiddenPanels()
                .Distinct()
                .ToArray();
        }
    }

    /// <summary>
    /// Panel identities this build cannot resolve. Diagnostic only: they are
    /// still owned by the workspace and are never mapped to built-in panels.
    /// </summary>
    public IReadOnlyList<PanelInstanceId> UnresolvedPanels
    {
        get
        {
            if (_disposed)
                return Array.Empty<PanelInstanceId>();

            return _coordinator.Workspace.EnumeratePanels()
                .Distinct()
                .Where(panel => !_registry.CanCreate(panel))
                .ToArray();
        }
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        _coordinator.StateChanged -= OnCoordinatorStateChanged;
        _coordinator.DragEnded -= OnCoordinatorDragEnded;
        _coordinator.Dispose();
        StateChanged = null;
        PersistenceStateChanged = null;
    }

    private IDockRootHost CreateFloatHost(DockRootHostDescriptor descriptor)
    {
        var owner = TopLevel.GetTopLevel(_mainHost.Root) as Window;
        return new DockWindowRootHost(
            descriptor,
            _registry,
            new AvaloniaFloatWindow(owner, "Nova — Floating Panel"),
            _interceptor);
    }

    private void OnCoordinatorStateChanged(object? sender, EventArgs e)
    {
        if (_disposed)
            return;

        // Persistence observes retries and health transitions even while the
        // shell banner update is suppressed.
        PersistenceStateChanged?.Invoke(this, EventArgs.Empty);
        if (_suppressNotifications > 0)
            return;

        NotifyStateChanged();
    }

    private void OnCoordinatorDragEnded(object? sender, EventArgs e)
    {
        if (!_disposed)
            PersistenceStateChanged?.Invoke(this, EventArgs.Empty);
    }

    private bool TryValidateHost(DockRootHostId hostId, DockTree root, out string? reason)
    {
        reason = null;
        var host = _coordinator.FindHost(hostId);
        if (host is null)
        {
            reason = $"dock host '{hostId}' is not registered";
            return false;
        }

        if (!host.IsVisible)
        {
            reason = $"dock host '{hostId}' is not visible";
            return false;
        }

        if (!host.IsHealthy)
        {
            reason = $"dock host '{hostId}' is degraded";
            return false;
        }

        if (host.Current is not { IsCurrent: true } current)
        {
            reason = $"dock host '{hostId}' has not applied its committed root";
            return false;
        }

        if (!host.TryGetCurrentToken(out var token)
            || token.HostId != hostId
            || !string.Equals(token.Digest, root.Digest, StringComparison.Ordinal))
        {
            reason = $"dock host '{hostId}' has a stale token";
            return false;
        }

        if (!string.Equals(current.CommittedDigest, root.Digest, StringComparison.Ordinal))
        {
            reason = $"dock host '{hostId}' is projecting a different root";
            return false;
        }

        return true;
    }

    private void NotifyStateChanged()
        => StateChanged?.Invoke(this, EventArgs.Empty);

    private DockApplyResult RequireCurrent()
        => Current
           ?? throw new InvalidOperationException(
               "The main dock host has no projection.");

    private DockOperationResult ToOperation(DockWorkspaceHostResult result)
    {
        if (result.ModelCommitted)
        {
            return DockOperationResult.Ok(
                _coordinator.Workspace.MainRoot,
                result.Diagnostic);
        }

        var error = result.ModelError != DockWorkspaceError.None
            ? MapError(result.ModelError)
            : DockError.InvalidModel;
        return DockOperationResult.Fail(error, result.Diagnostic);
    }

    private static DockError MapError(DockWorkspaceError error)
        => error switch
        {
            DockWorkspaceError.InvalidSource => DockError.InvalidSource,
            DockWorkspaceError.DuplicatePanel => DockError.DuplicatePanel,
            DockWorkspaceError.TargetGone => DockError.TargetGone,
            DockWorkspaceError.AlreadyInState => DockError.AlreadyInState,
            DockWorkspaceError.PanelHidden => DockError.PanelHidden,
            DockWorkspaceError.WouldEmptyGroup => DockError.WouldEmptyGroup,
            DockWorkspaceError.InvalidTarget
                or DockWorkspaceError.WouldEmptyMainRoot
                or DockWorkspaceError.LimitExceeded => DockError.InvalidTarget,
            _ => DockError.InvalidModel,
        };

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(EditorDockWorkspace));
    }

    private void ThrowIfRetrying()
    {
        if (_retrying)
            throw new InvalidOperationException(
                "Cannot change the dock workspace while a retry is in progress.");
    }
}
