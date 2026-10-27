using Avalonia;
using Avalonia.Input;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Owns a multi-root <see cref="DockWorkspace"/>, its one shared view registry
/// and one host per root. Model transactions commit first; host apply/show
/// failures degrade only the affected host and never roll the model back.
/// </summary>
public sealed class DockWorkspaceCoordinator : IDisposable
{
    private readonly DockPanelViewRegistry _viewRegistry;
    private readonly DockRootHostFactory _hostFactory;
    private readonly IDockRootHost _mainHost;
    private readonly Dictionary<DockRootHostId, IDockRootHost> _hosts = [];

    private DockWorkspace _workspace;
    private readonly DockLayoutHistory _layoutHistory = new();
    private DockDragSession? _drag;
    private bool _shuttingDown;
    private bool _disposed;

    public DockWorkspaceCoordinator(
        DockWorkspace workspace,
        IDockRootHost mainHost,
        DockPanelViewRegistry viewRegistry,
        DockRootHostFactory floatHostFactory)
        : this(workspace, mainHost, viewRegistry, floatHostFactory, restoredFloatHosts: null)
    {
        InitializeMainHost();
    }

    private DockWorkspaceCoordinator(
        DockWorkspace workspace,
        IDockRootHost mainHost,
        DockPanelViewRegistry viewRegistry,
        DockRootHostFactory floatHostFactory,
        IReadOnlyDictionary<DockRootHostId, IDockRootHost>? restoredFloatHosts)
    {
        _workspace = workspace ?? throw new ArgumentNullException(nameof(workspace));
        _mainHost = mainHost ?? throw new ArgumentNullException(nameof(mainHost));
        _viewRegistry = viewRegistry ?? throw new ArgumentNullException(nameof(viewRegistry));
        _hostFactory = floatHostFactory ?? throw new ArgumentNullException(nameof(floatHostFactory));

        // The layout history is in-process only: a restored workspace becomes
        // the single baseline entry, so the first undo returns to exactly the
        // layout this session started from.
        _layoutHistory.Reset(DockLayoutSnapshot.Capture(_workspace));

        if (!mainHost.Id.IsMain || mainHost.Kind != DockRootKind.Main)
            throw new ArgumentException("The main host must use the main host id.", nameof(mainHost));

        if (restoredFloatHosts is null && _workspace.FloatingRoots.Count != 0)
        {
            throw new ArgumentException(
                "The constructor starts from a workspace without floating roots; use Restore.",
                nameof(workspace));
        }

        _hosts.Add(mainHost.Id, mainHost);
        AttachHost(mainHost);

        if (restoredFloatHosts is null)
            return;

        foreach (var pair in restoredFloatHosts)
        {
            _hosts.Add(pair.Key, pair.Value);
            AttachHost(pair.Value);
        }
    }

    /// <summary>
    /// Restores a validated multi-root candidate. Floating hosts are prepared
    /// hidden; a creation/prepare failure is merged inside the candidate and
    /// never uses the runtime re-dock transaction. Apply/show failures after
    /// that point degrade the host under D6 rules and are not merged.
    /// </summary>
    public static DockRestoreResult Restore(
        DockWorkspace candidate,
        IDockRootHost mainHost,
        DockPanelViewRegistry viewRegistry,
        DockRootHostFactory floatHostFactory)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        ArgumentNullException.ThrowIfNull(mainHost);
        ArgumentNullException.ThrowIfNull(viewRegistry);
        ArgumentNullException.ThrowIfNull(floatHostFactory);

        if (!mainHost.Id.IsMain || mainHost.Kind != DockRootKind.Main)
            return DockRestoreResult.Fail("The main host must use the main host id.");

        var validation = candidate.Validate();
        if (!validation.IsValid)
            return DockRestoreResult.Fail($"The restore candidate is invalid: {validation.Summary}");

        var originalRevision = candidate.Revision;
        var workspace = candidate;
        var preparedHosts = new Dictionary<DockRootHostId, IDockRootHost>();
        var merged = new List<DockFloatRootId>();
        var diagnostics = new List<string>();

        foreach (var floatRoot in candidate.FloatingRoots.ToArray())
        {
            var descriptor = DockRootHostDescriptor.ForFloat(floatRoot.Id);
            IDockRootHost? host = null;
            string? failure = null;

            try
            {
                host = floatHostFactory(descriptor);
            }
            catch (Exception ex)
            {
                failure = Describe(ex);
            }

            if (host is not null && (host.Id != descriptor.Id || host.Kind != DockRootKind.Float))
            {
                DisposeHost(host);
                host = null;
                failure ??= "The host factory returned an invalid floating host.";
            }

            if (host is not null)
            {
                var prepare = PrepareHostCore(host, floatRoot.Root);
                if (!prepare.Success)
                {
                    failure = $"{prepare.Error}: {prepare.Diagnostic}";
                    DisposeHost(host);
                    host = null;
                }
            }

            if (host is null)
            {
                diagnostics.Add(
                    $"Floating root '{floatRoot.Id}' could not be restored: {failure ?? "unknown failure"}");
                var merge = workspace.ReDockFloatRoot(floatRoot.Id);
                if (!merge.Success || merge.Workspace is null)
                {
                    foreach (var prepared in preparedHosts.Values)
                        DisposeHost(prepared);

                    return DockRestoreResult.Fail(
                        $"Floating root '{floatRoot.Id}' could not be merged during restore: "
                        + merge.Diagnostic);
                }

                workspace = merge.Workspace;
                merged.Add(floatRoot.Id);
                continue;
            }

            preparedHosts.Add(host.Id, host);
        }

        // Load-stage merges build candidates; they do not advance the runtime
        // revision of the workspace that the coordinator adopts.
        if (merged.Count > 0)
        {
            try
            {
                workspace = DockWorkspace.Create(
                    workspace.MainRoot,
                    workspace.FloatingRoots,
                    revision: originalRevision);
            }
            catch (DockModelException ex)
            {
                foreach (var prepared in preparedHosts.Values)
                    DisposeHost(prepared);
                return DockRestoreResult.Fail($"The restored workspace is invalid: {ex.Message}");
            }
        }

        var finalValidation = workspace.Validate();
        if (!finalValidation.IsValid)
        {
            foreach (var prepared in preparedHosts.Values)
                DisposeHost(prepared);
            return DockRestoreResult.Fail(
                $"The restored workspace is invalid: {finalValidation.Summary}");
        }

        var coordinator = new DockWorkspaceCoordinator(
            workspace,
            mainHost,
            viewRegistry,
            floatHostFactory,
            preparedHosts);
        diagnostics.AddRange(coordinator.InitializeRestoredHosts(out var normalized));

        // Placement may have pulled restored float geometry back inside the
        // current work areas; that settled geometry is the session baseline,
        // so the first undo must not resurrect the pre-clamp rectangles.
        if (normalized)
            coordinator._layoutHistory.Reset(DockLayoutSnapshot.Capture(coordinator._workspace));

        return DockRestoreResult.Ok(coordinator, merged, diagnostics);
    }

    public event EventHandler? StateChanged;

    /// <summary>Raised when an active drag session ends without a model commit.</summary>
    public event EventHandler? DragEnded;

    public DockWorkspace Workspace => _workspace;

    public DockPanelViewRegistry ViewRegistry => _viewRegistry;

    public bool IsShuttingDown => _shuttingDown;

    public bool IsDisposed => _disposed;

    public int HostCount => _hosts.Count;

    public bool HasDegradedHosts
        => _hosts.Values.Any(host => !host.IsHealthy);

    public IReadOnlyList<DockRootHostId> EnumerateHostIds()
        => _hosts.Keys.ToArray();

    public IDockRootHost? FindHost(DockRootHostId hostId)
        => _hosts.TryGetValue(hostId, out var host) ? host : null;

    public DockWorkspaceHostResult FloatPanel(PanelInstanceId panel, DockBounds bounds)
        => FloatPanel(panel, bounds, preferredScreen: null);

    private DockWorkspaceHostResult FloatPanel(
        PanelInstanceId panel,
        DockBounds bounds,
        DockScreenArea? preferredScreen)
    {
        var rejected = RejectIfUnavailable(DockRootHostId.Main)
            ?? RejectIfDegraded(DockRootHostId.Main);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var model = _workspace.FloatPanel(panel, bounds);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, DockRootHostId.Main);

        var candidate = model.Workspace;
        var floatRoot = candidate.FloatingRoots.First(root => FindFloatRoot(root.Id) is null);
        var hostId = DockRootHostId.ForFloat(floatRoot.Id);

        IDockRootHost? host = null;
        try
        {
            host = _hostFactory(DockRootHostDescriptor.ForFloat(floatRoot.Id));
        }
        catch (Exception ex)
        {
            return Reject(hostId, DockHostError.CreationFailed, Describe(ex));
        }

        if (host is null || host.Id != hostId || host.Kind != DockRootKind.Float)
        {
            DisposeHost(host);
            return Reject(
                hostId,
                DockHostError.CreationFailed,
                "The host factory returned an invalid floating host.");
        }

        var prepare = PrepareHost(host, floatRoot.Root);
        if (!prepare.Success)
        {
            DisposeHost(host);
            return Reject(hostId, prepare.Error, prepare.Diagnostic);
        }

        _workspace = candidate;
        _hosts.Add(hostId, host);
        AttachHost(host);
        SetFloatHostBounds(host, floatRoot, preferredScreen);

        var mainApply = ApplyHost(_mainHost, _workspace.MainRoot);
        var floatApply = ApplyHost(host, floatRoot.Root);
        var show = floatApply.Success
            ? ShowHost(host)
            : floatApply;

        if (show.Success)
            NormalizeFloatGeometry(host);

        if (!mainApply.Success)
            return CommittedHostFailure(_mainHost.Id, mainApply);
        if (!floatApply.Success)
            return CommittedHostFailure(hostId, floatApply);
        if (!show.Success)
            return CommittedHostFailure(hostId, show);

        return CommittedSuccess(hostId, model.Diagnostic);
    }

    public DockWorkspaceHostResult MovePanel(PanelInstanceId panel, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        if (target is DockTarget.Float floatTarget)
            return FloatPanel(panel, floatTarget.Bounds);

        var rejected = RejectIfUnavailable(DockRootHostId.Main)
            ?? RejectIfDegraded(DockRootHostId.Main);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var hasSource = _workspace.TryLocatePanel(panel, out var source);
        var sourceHostId = hasSource ? HostIdFor(source) : DockRootHostId.Main;
        var targetHostId = ResolveGroupHost(target);
        if (targetHostId is { } expectedTarget)
        {
            var targetHost = FindHost(expectedTarget);
            if (targetHost is null || !targetHost.IsHealthy)
            {
                return Reject(
                    expectedTarget,
                    DockHostError.HostUnavailable,
                    $"Target host '{expectedTarget}' is not available.");
            }
        }

        var previousFloats = _workspace.FloatingRoots.ToArray();
        var model = _workspace.MovePanel(panel, target);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, sourceHostId);

        _workspace = model.Workspace;

        DockRootHostId failedHost = sourceHostId;
        DockHostResult? failure = null;

        foreach (var removed in previousFloats)
        {
            if (FindFloatRoot(removed.Id) is not null)
                continue;

            var removedHostId = DockRootHostId.ForFloat(removed.Id);
            if (!_hosts.Remove(removedHostId, out var removedHost))
                continue;

            DetachHost(removedHost);
            var close = CloseHost(removedHost);
            DisposeHost(removedHost);
            if (!close.Success && failure is null)
            {
                failure = close;
                failedHost = removedHostId;
            }
        }

        var applyFailure = ApplyInOrder(
            new[] { sourceHostId, targetHostId ?? sourceHostId },
            out var applyFailedHost);
        if (failure is null && applyFailure is { } applied)
        {
            failure = applied;
            failedHost = applyFailedHost;
        }

        return failure is { } hostFailure
            ? CommittedHostFailure(failedHost, hostFailure)
            : CommittedSuccess(sourceHostId, model.Diagnostic);
    }

    /// <summary>
    /// Replaces the main root while keeping floating roots and their hosts.
    /// Panels that leave the whole workspace release their views once.
    /// </summary>
    public DockWorkspaceHostResult ReplaceMainRoot(DockTree mainRoot)
    {
        ArgumentNullException.ThrowIfNull(mainRoot);
        var rejected = RejectIfUnavailable(DockRootHostId.Main)
            ?? RejectIfDegraded(DockRootHostId.Main);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var previous = _workspace;
        if (string.Equals(
                mainRoot.Digest, previous.MainRoot.Digest, StringComparison.Ordinal))
        {
            var unchanged = ApplyHost(_mainHost, mainRoot);
            return unchanged.Success
                ? CommittedSuccess(_mainHost.Id, "The main root is unchanged.")
                : CommittedHostFailure(_mainHost.Id, unchanged);
        }

        DockWorkspace candidate;
        try
        {
            candidate = DockWorkspace.Create(
                mainRoot,
                previous.FloatingRoots,
                previous.Revision + 1);
        }
        catch (DockModelException ex)
        {
            return new DockWorkspaceHostResult(
                false,
                ModelCommitted: false,
                previous,
                DockWorkspaceError.InvalidModel,
                DockHostResult.Ok(),
                DockRootHostId.Main,
                ex.Message);
        }

        CancelDrag();
        _workspace = candidate;

        var apply = ApplyHost(_mainHost, candidate.MainRoot);
        ReleasePanelsRemoved(previous, candidate);

        return apply.Success
            ? CommittedSuccess(_mainHost.Id, "The main root was replaced.")
            : CommittedHostFailure(_mainHost.Id, apply);
    }

    /// <summary>
    /// Restores the default main root and removes every floating root. The
    /// caller confirms this destructive operation before calling.
    /// </summary>
    public DockWorkspaceHostResult RestoreDefault()
    {
        var rejected = RejectIfUnavailable(DockRootHostId.Main)
            ?? RejectIfDegraded(DockRootHostId.Main);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var previous = _workspace;
        var model = previous.RestoreDefault();
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, DockRootHostId.Main);

        var candidate = model.Workspace;
        var prepare = PrepareHost(_mainHost, candidate.MainRoot);
        if (!prepare.Success)
        {
            return Reject(
                _mainHost.Id,
                prepare.Error,
                $"The main host could not prepare the default root. {prepare.Diagnostic}");
        }

        CancelDrag();
        _workspace = candidate;

        DockHostResult? failure = null;
        var failedHost = _mainHost.Id;
        foreach (var root in previous.FloatingRoots)
        {
            var removedHostId = DockRootHostId.ForFloat(root.Id);
            if (!_hosts.Remove(removedHostId, out var host))
                continue;

            DetachHost(host);
            var close = CloseHost(host);
            DisposeHost(host);
            if (!close.Success && failure is null)
            {
                failure = close;
                failedHost = removedHostId;
            }
        }

        var mainApply = ApplyHost(_mainHost, _workspace.MainRoot);
        var mainShow = mainApply.Success && !_mainHost.IsVisible
            ? ShowHost(_mainHost)
            : mainApply;
        if (!mainShow.Success)
        {
            failure = mainShow;
            failedHost = _mainHost.Id;
        }

        ReleasePanelsRemoved(previous, candidate);

        return failure is { } hostFailure
            ? CommittedHostFailure(failedHost, hostFailure)
            : CommittedSuccess(_mainHost.Id, model.Diagnostic);
    }

    /// <summary>Handles a floating-window system close (X) by re-docking.</summary>
    public DockWorkspaceHostResult RequestCloseFloatRoot(DockFloatRootId rootId)
        => MergeAndClose(rootId, target: null, closingRequest: true);

    public DockWorkspaceHostResult ReDockFloatRoot(DockFloatRootId rootId)
        => MergeAndClose(rootId, target: null, closingRequest: false);

    public DockWorkspaceHostResult MergeFloatRoot(DockFloatRootId rootId, DockTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        return MergeAndClose(rootId, target, closingRequest: false);
    }

    public DockWorkspaceHostResult CloseFloatRoot(DockFloatRootId rootId)
    {
        var hostId = DockRootHostId.ForFloat(rootId);
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var floatRoot = FindFloatRoot(rootId);
        var panels = floatRoot?.Root.EnumeratePanels().ToArray();

        var model = _workspace.CloseFloatRoot(rootId);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;

        DockHostResult? failure = null;
        if (_hosts.Remove(hostId, out var host))
        {
            DetachHost(host);
            var close = CloseHost(host);
            DisposeHost(host);
            if (!close.Success)
                failure = close;
        }

        var release = ReleasePanels(panels ?? Array.Empty<PanelInstanceId>());
        if (!release.Success && failure is null)
            failure = release;

        return failure is { } hostFailure
            ? CommittedHostFailure(hostId, hostFailure)
            : CommittedSuccess(hostId, model.Diagnostic);
    }

    /// <summary>
    /// Hides a known panel: the model keeps its identity, order and group, the
    /// host re-projects the visible set, and the released view is disposed
    /// exactly once after the commit.
    /// </summary>
    public DockWorkspaceHostResult HidePanel(PanelInstanceId panel)
        => ChangePanelVisibility(panel, hide: true);

    /// <summary>
    /// Shows a hidden panel: the model keeps its order and does not auto
    /// activate it; the host projection recreates the view (or placeholder).
    /// </summary>
    public DockWorkspaceHostResult ShowPanel(PanelInstanceId panel)
        => ChangePanelVisibility(panel, hide: false);

    private DockWorkspaceHostResult ChangePanelVisibility(PanelInstanceId panel, bool hide)
    {
        var hasLocation = _workspace.TryLocatePanel(panel, out var location);
        var hostId = hasLocation ? HostIdFor(location) : DockRootHostId.Main;
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var model = hide ? _workspace.HidePanel(panel) : _workspace.ShowPanel(panel);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;

        DockHostResult? failure = null;
        var host = FindHost(hostId);
        var root = RootForHost(hostId);
        if (host is null || root is null)
        {
            failure = DockHostResult.Fail(
                DockHostError.HostUnavailable,
                $"Host '{hostId}' has no root in the committed workspace.");
        }
        else
        {
            var apply = ApplyHost(host, root);
            if (!apply.Success)
                failure = apply;
        }

        // A hidden panel owns no view. The registry entry is released after the
        // projection dropped it, so the release runs exactly once.
        if (hide)
        {
            var release = ReleasePanels(new[] { panel });
            if (!release.Success && failure is null)
                failure = release;
        }

        return failure is { } hostFailure
            ? CommittedHostFailure(hostId, hostFailure)
            : CommittedSuccess(hostId, model.Diagnostic);
    }

    public DockWorkspaceHostResult ClosePanel(PanelInstanceId panel)
    {
        var rejected = RejectIfUnavailable(DockRootHostId.Main)
            ?? RejectIfDegraded(DockRootHostId.Main);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        if (!_workspace.TryLocatePanel(panel, out var location))
        {
            var missing = _workspace.RemovePanel(panel);
            return FromModelFailure(missing, DockRootHostId.Main);
        }

        var hostId = HostIdFor(location);
        var floatId = location.FloatRootId;
        var model = _workspace.RemovePanel(panel);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;

        DockHostResult? failure = null;
        if (floatId is { } rootId && FindFloatRoot(rootId) is null)
        {
            if (_hosts.Remove(hostId, out var floatHost))
            {
                DetachHost(floatHost);
                var close = CloseHost(floatHost);
                DisposeHost(floatHost);
                if (!close.Success)
                    failure = close;
            }
        }
        else
        {
            var host = FindHost(hostId);
            if (host is null)
            {
                failure = DockHostResult.Fail(
                    DockHostError.HostUnavailable,
                    $"Host '{hostId}' is not available.");
            }
            else
            {
                var root = RootForHost(hostId);
                if (root is not null)
                {
                    var apply = ApplyHost(host, root);
                    if (!apply.Success)
                        failure = apply;
                }
            }
        }

        var release = ReleasePanels(new[] { panel });
        if (!release.Success && failure is null)
            failure = release;

        return failure is { } hostFailure
            ? CommittedHostFailure(hostId, hostFailure)
            : CommittedSuccess(hostId, model.Diagnostic);
    }

    public DockWorkspaceHostResult UpdateFloatBounds(
        DockFloatRootId rootId,
        DockBounds bounds,
        bool maximized)
    {
        var hostId = DockRootHostId.ForFloat(rootId);
        var rejected = RejectIfUnavailable(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        if (HasDegradedHosts)
        {
            // The refusal must be visible: the window snaps back to the
            // committed geometry instead of keeping a size the model rejected.
            RestoreCommittedFloatBounds(rootId, hostId);
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                "A dock host is degraded. Retry it before changing the layout.");
        }

        var model = _workspace.UpdateFloatBounds(rootId, bounds, maximized);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;
        return CommittedSuccess(hostId, model.Diagnostic);
    }

    public DockWorkspaceHostResult ActivateTab(
        DockRootHostId hostId,
        DockNodeId groupId,
        PanelInstanceId panel,
        DockApplyToken token)
    {
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var invalid = ValidateCallback(hostId, token, FindGroupHost(groupId));
        if (invalid is { } invalidResult)
            return invalidResult;

        var model = _workspace.ActivateTab(groupId, panel);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;
        return ApplyHostAfterCommit(hostId, model.Diagnostic, recordHistory: false);
    }

    public DockWorkspaceHostResult ResizeSplit(
        DockRootHostId hostId,
        DockNodeId splitId,
        double firstWeight,
        double secondWeight,
        DockApplyToken token)
    {
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var invalid = ValidateCallback(hostId, token, FindSplitHost(splitId));
        if (invalid is { } invalidResult)
            return invalidResult;

        var model = _workspace.ResizeSplit(splitId, firstWeight, secondWeight);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;
        return ApplyHostAfterCommit(hostId, model.Diagnostic);
    }

    /// <summary>
    /// Programmatic tab activation. Unlike the visual callback overload this
    /// does not depend on a projection token, but it still changes the model,
    /// so a degraded workspace rejects it before any change.
    /// </summary>
    public DockWorkspaceHostResult ActivateTab(
        DockRootHostId hostId,
        DockNodeId groupId,
        PanelInstanceId panel)
    {
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        if (FindHost(hostId) is null)
        {
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                $"Host '{hostId}' is not available.");
        }

        if (FindGroupHost(groupId) != hostId)
        {
            return Reject(
                hostId,
                DockHostError.StaleToken,
                $"Group '{groupId}' does not belong to host '{hostId}'.");
        }

        var model = _workspace.ActivateTab(groupId, panel);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;
        return ApplyHostAfterCommit(hostId, model.Diagnostic, recordHistory: false);
    }

    /// <summary>
    /// Programmatic split resize. Unlike the visual callback overload this
    /// does not depend on a projection token.
    /// </summary>
    public DockWorkspaceHostResult ResizeSplit(
        DockRootHostId hostId,
        DockNodeId splitId,
        double firstWeight,
        double secondWeight)
    {
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        if (FindHost(hostId) is null)
        {
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                $"Host '{hostId}' is not available.");
        }

        if (FindSplitHost(splitId) != hostId)
        {
            return Reject(
                hostId,
                DockHostError.StaleToken,
                $"Split '{splitId}' does not belong to host '{hostId}'.");
        }

        var model = _workspace.ResizeSplit(splitId, firstWeight, secondWeight);
        if (!model.Success || model.Workspace is null)
            return FromModelFailure(model, hostId);

        _workspace = model.Workspace;
        return ApplyHostAfterCommit(hostId, model.Diagnostic);
    }

    public DockWorkspaceHostResult Retry(DockRootHostId hostId)
    {
        var rejected = RejectIfUnavailable(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var host = FindHost(hostId);
        if (host is null)
        {
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                $"Host '{hostId}' is not available.");
        }

        var result = RetryHost(host);
        var settledShow = false;
        if (result.Success && !host.IsVisible)
        {
            result = ShowHost(host);
            settledShow = result.Success;
        }

        // Settlement calibration runs only for a floating host this Retry
        // actually showed: the window's final placement is the geometry the
        // current history entry must replay from now on. A main-host retry and
        // a visible-host retry never calibrate.
        if (result.Success && settledShow && !hostId.IsMain)
            result = CalibrateSettledGeometry(host, result);

        NotifyStateChanged();
        return new DockWorkspaceHostResult(
            result.Success,
            ModelCommitted: false,
            _workspace,
            DockWorkspaceError.None,
            result,
            hostId,
            result.Diagnostic);
    }

    private DockHostResult CalibrateSettledGeometry(
        IDockRootHost host,
        DockHostResult shown)
    {
        var expectedCurrent = DockLayoutSnapshot.Capture(_workspace);
        var current = _layoutHistory.Current;
        if (current is null || !expectedCurrent.SameContent(current))
        {
            return AbandonCalibration(
                host,
                modelChanged: false,
                expectedCurrent,
                "The layout history no longer holds the commit that opened the floating window.");
        }

        var normalized = NormalizeFloatGeometry(host);
        var settled = DockLayoutSnapshot.Capture(_workspace);
        var reconciled = _layoutHistory.ReconcileCurrent(expectedCurrent, settled);
        if (reconciled.Outcome != DockLayoutHistoryReconcileOutcome.Rejected)
            return shown;

        return AbandonCalibration(host, normalized, expectedCurrent, reconciled.Diagnostic);
    }

    /// <summary>
    /// Calibration could not be proven: the host is closed again so the
    /// degraded protections stay up, the model returns to the expected content
    /// when normalization already wrote it, and the failure is reported instead
    /// of being mistaken for a healthy retry.
    /// </summary>
    private DockHostResult AbandonCalibration(
        IDockRootHost host,
        bool modelChanged,
        DockLayoutSnapshot expectedCurrent,
        string diagnostic)
    {
        if (modelChanged)
        {
            try
            {
                _workspace = expectedCurrent.RestoreWorkspace(_workspace.Revision);
            }
            catch (DockModelException)
            {
                // The closed host keeps the workspace blocked either way.
            }
        }

        CloseHost(host);
        return DockHostResult.Fail(DockHostError.RetryFailed, diagnostic);
    }

    /// <summary>
    /// Layout history lives in this coordinator only: a replayed layout is a
    /// fresh commit, never a rewind of the revision, and nothing here is
    /// persisted. A new session starts from the restored layout as baseline.
    /// </summary>
    public bool CanUndoLayout
        => MayReplayLayout && _layoutHistory.CanUndo;

    public bool CanRedoLayout
        => MayReplayLayout && _layoutHistory.CanRedo;

    public DockWorkspaceHostResult UndoLayout()
        => ReplayLayout(backwards: true);

    public DockWorkspaceHostResult RedoLayout()
        => ReplayLayout(backwards: false);

    private bool MayReplayLayout
        => !_disposed && !_shuttingDown && _drag is null && !HasDegradedHosts;

    /// <summary>
    /// Replays one history entry through the same commit funnel as a direct
    /// operation: the candidate is rebuilt at the next revision, every host the
    /// replay needs is prepared before the model is adopted, and the cursor
    /// advances only because the commit itself succeeded. A rejected or
    /// unprepared replay leaves both the model and the cursor untouched.
    /// </summary>
    private DockWorkspaceHostResult ReplayLayout(bool backwards)
    {
        var hostId = DockRootHostId.Main;
        var rejected = RejectIfUnavailable(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        if (_drag is not null)
        {
            return Reject(
                hostId,
                DockHostError.NoTarget,
                "A dock drag session is in progress.");
        }

        if (HasDegradedHosts)
        {
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                "A dock host is degraded. Retry it before undoing or redoing the layout.");
        }

        DockLayoutSnapshot? target = null;
        if (backwards)
        {
            if (_layoutHistory.TryGetUndoTarget(out var undoTarget))
                target = undoTarget;
        }
        else if (_layoutHistory.TryGetRedoTarget(out var redoTarget))
        {
            target = redoTarget;
        }

        if (target is null)
        {
            return new DockWorkspaceHostResult(
                false,
                ModelCommitted: false,
                _workspace,
                DockWorkspaceError.AlreadyInState,
                DockHostResult.Ok(),
                hostId,
                backwards
                    ? "There is no layout left to undo."
                    : "There is no layout left to redo.");
        }

        var previous = _workspace;
        DockWorkspace candidate;
        try
        {
            candidate = target.RestoreWorkspace(previous.Revision + 1);
        }
        catch (DockModelException ex)
        {
            return new DockWorkspaceHostResult(
                false,
                ModelCommitted: false,
                previous,
                DockWorkspaceError.InvalidModel,
                DockHostResult.Ok(),
                hostId,
                ex.Message);
        }

        var prepareMain = PrepareHost(_mainHost, candidate.MainRoot);
        if (!prepareMain.Success)
        {
            return Reject(
                hostId,
                prepareMain.Error,
                $"The main host could not prepare the replayed root. {prepareMain.Diagnostic}");
        }

        var creationFailure = CreateReplayFloatHosts(candidate, out var newHosts);
        if (creationFailure is { } creationProblem)
        {
            foreach (var (_, host) in newHosts)
                DisposeHost(host);

            return creationProblem;
        }

        _workspace = candidate;

        foreach (var (newHostId, newHost) in newHosts)
        {
            _hosts.Add(newHostId, newHost);
            AttachHost(newHost);
        }

        DockHostResult? failure = null;
        var failedHost = hostId;

        var keptFloatHostIds = new HashSet<DockRootHostId>(
            candidate.FloatingRoots.Select(root => DockRootHostId.ForFloat(root.Id)));
        foreach (var removedId in _hosts.Keys
                     .Where(id => !id.IsMain && !keptFloatHostIds.Contains(id))
                     .ToArray())
        {
            if (!_hosts.Remove(removedId, out var removed))
                continue;

            DetachHost(removed);
            var close = CloseHost(removed);
            DisposeHost(removed);
            if (!close.Success && failure is null)
            {
                failure = close;
                failedHost = removedId;
            }
        }

        var mainApply = ApplyHost(_mainHost, candidate.MainRoot);
        var mainShow = mainApply.Success && !_mainHost.IsVisible
            ? ShowHost(_mainHost)
            : mainApply;
        if (!mainShow.Success && failure is null)
        {
            failure = mainShow;
            failedHost = hostId;
        }

        foreach (var floatRoot in candidate.FloatingRoots)
        {
            var floatHostId = DockRootHostId.ForFloat(floatRoot.Id);
            if (FindHost(floatHostId) is not { } floatHost)
                continue;

            SetFloatHostBounds(floatHost, floatRoot);
            var apply = ApplyHost(floatHost, floatRoot.Root);
            var show = apply.Success && !floatHost.IsVisible
                ? ShowHost(floatHost)
                : apply;
            if (!show.Success && failure is null)
            {
                failure = show;
                failedHost = floatHostId;
            }
        }

        ReleasePanelsRemoved(previous, candidate);

        return failure is { } hostFailure
            ? CommittedHostFailure(
                failedHost,
                hostFailure,
                backwards ? DockLayoutOperationSource.Undo : DockLayoutOperationSource.Redo)
            : CommittedSuccess(
                hostId,
                backwards
                    ? "Undone one layout change."
                    : "Redone one layout change.",
                backwards ? DockLayoutOperationSource.Undo : DockLayoutOperationSource.Redo);
    }

    /// <summary>
    /// Creates and prepares the floating hosts a replay needs, before the model
    /// is adopted. Hosts that already exist are reused and re-projected. On the
    /// first failure the rejection is returned and the host that failed is
    /// disposed, so a rejected replay leaves no orphan window behind; the caller
    /// disposes the hosts that were created successfully before the failure.
    /// </summary>
    private DockWorkspaceHostResult? CreateReplayFloatHosts(
        DockWorkspace candidate,
        out List<(DockRootHostId HostId, IDockRootHost Host)> newHosts)
    {
        newHosts = [];

        foreach (var floatRoot in candidate.FloatingRoots)
        {
            var hostId = DockRootHostId.ForFloat(floatRoot.Id);
            if (_hosts.ContainsKey(hostId))
                continue;

            IDockRootHost? host = null;
            try
            {
                host = _hostFactory(DockRootHostDescriptor.ForFloat(floatRoot.Id));
            }
            catch (Exception ex)
            {
                return Reject(hostId, DockHostError.CreationFailed, Describe(ex));
            }

            if (host is null || host.Id != hostId || host.Kind != DockRootKind.Float)
            {
                DisposeHost(host);
                return Reject(
                    hostId,
                    DockHostError.CreationFailed,
                    "The host factory returned an invalid floating host.");
            }

            var prepare = PrepareHost(host, floatRoot.Root);
            if (!prepare.Success)
            {
                DisposeHost(host);
                return Reject(hostId, prepare.Error, prepare.Diagnostic);
            }

            newHosts.Add((hostId, host));
        }

        return null;
    }

    public bool IsDragging => _drag is not null;

    /// <summary>
    /// Starts a coordinator-owned cross-window session. The source host and
    /// token are snapshotted here; nothing is committed until
    /// <see cref="CommitDrag"/> or <see cref="CommitTearOff"/>.
    /// </summary>
    public DockHostResult BeginDrag(
        DockRootHostId sourceHostId,
        PanelInstanceId panel,
        DockApplyToken sourceToken)
    {
        ThrowIfDisposed();
        CancelDrag();

        if (_shuttingDown)
            return DockHostResult.Fail(DockHostError.ShuttingDown, "The workspace is shutting down.");

        var host = FindHost(sourceHostId);
        if (host is null || !host.IsHealthy)
        {
            return DockHostResult.Fail(
                DockHostError.HostUnavailable,
                $"Source host '{sourceHostId}' is not available.");
        }

        if (!host.TryGetCurrentToken(out var current) || !TokenMatches(current, sourceToken))
        {
            return DockHostResult.Fail(
                DockHostError.StaleToken,
                $"Source host '{sourceHostId}' no longer matches the drag token.");
        }

        if (panel.IsEmpty
            || !_workspace.TryLocatePanel(panel, out var location)
            || HostIdFor(location) != sourceHostId)
        {
            return DockHostResult.Fail(
                DockHostError.StaleToken,
                $"Panel '{panel}' is not in source host '{sourceHostId}'.");
        }

        _drag = new DockDragSession(
            sourceHostId,
            panel,
            sourceToken,
            location.GroupId);
        return DockHostResult.Ok();
    }

    /// <summary>Resolves an external drop target from host-local coordinates.</summary>
    public DockDropTargetHit? UpdateDrag(DockRootHostId targetHostId, Point localPoint)
    {
        var drag = _drag;
        if (drag is null)
            return null;

        if (!ValidateDragSource(drag, out _))
        {
            CancelDrag();
            return null;
        }

        if (targetHostId == drag.SourceHostId)
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return null;
        }

        var rootHost = FindHost(targetHostId);
        if (rootHost is null
            || !rootHost.IsHealthy
            || rootHost is not IDockDropTargetHost target)
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return null;
        }

        DockDropTargetHit hit;
        try
        {
            if (!target.TryHitTestLocal(localPoint, drag.SourceGroupId, out hit))
            {
                ClearExternalPreviews();
                ResetExternalTarget(drag);
                return null;
            }
        }
        catch
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return null;
        }

        return ApplyExternalHit(drag, target, hit);
    }

    /// <summary>
    /// Resolves an external drop target from a screen-space point. The source
    /// host is excluded; floating windows are tested before the main host.
    /// </summary>
    public DockDropTargetHit? UpdateDragExternal(PixelPoint screenPoint)
    {
        var drag = _drag;
        if (drag is null)
            return null;

        if (!ValidateDragSource(drag, out _))
        {
            CancelDrag();
            return null;
        }

        foreach (var hostId in ExternalDropTargetOrder(drag.SourceHostId))
        {
            var rootHost = FindHost(hostId);
            if (rootHost is null
                || !rootHost.IsHealthy
                || rootHost is not IDockDropTargetHost target)
            {
                continue;
            }

            DockDropTargetHit hit;
            try
            {
                if (!target.TryHitTestScreen(screenPoint, drag.SourceGroupId, out hit))
                    continue;
            }
            catch
            {
                continue;
            }

            return ApplyExternalHit(drag, target, hit);
        }

        ClearExternalPreviews();
        ResetExternalTarget(drag);
        return null;
    }

    /// <summary>
    /// Revalidates the source, target host and target token snapshot, then
    /// commits exactly one <see cref="DockWorkspace.MovePanel"/>.
    /// </summary>
    public DockWorkspaceHostResult CommitDrag()
    {
        ThrowIfDisposed();

        var drag = _drag;
        if (drag is null)
        {
            return Reject(
                DockRootHostId.Main,
                DockHostError.NoTarget,
                "No cross-window drag session is active.");
        }

        if (_shuttingDown)
        {
            var shutdownSource = drag.SourceHostId;
            CancelDrag();
            return Reject(shutdownSource, DockHostError.ShuttingDown, "The workspace is shutting down.");
        }

        if (!ValidateDragSource(drag, out var sourceFailure))
        {
            CancelDrag();
            return Reject(drag.SourceHostId, sourceFailure.Error, sourceFailure.Diagnostic);
        }

        if (drag.Target is not { } target
            || drag.TargetHostId is not { } targetHostId
            || drag.TargetToken is not { } targetToken)
        {
            var source = drag.SourceHostId;
            CancelDrag();
            return Reject(
                source,
                DockHostError.NoTarget,
                "No external drop target was resolved.");
        }

        var targetHost = FindHost(targetHostId);
        if (targetHost is null || !targetHost.IsHealthy)
        {
            CancelDrag();
            return Reject(
                targetHostId,
                DockHostError.HostUnavailable,
                $"Target host '{targetHostId}' is not available.");
        }

        if (!targetHost.TryGetCurrentToken(out var current)
            || !TokenMatches(current, targetToken))
        {
            CancelDrag();
            return Reject(
                targetHostId,
                DockHostError.StaleToken,
                $"Target host '{targetHostId}' changed after the drop target was resolved.");
        }

        if (GroupOf(target) is { } targetGroupId
            && FindGroupHost(targetGroupId) != targetHostId)
        {
            var goneTarget = targetHostId;
            CancelDrag();
            return new DockWorkspaceHostResult(
                false,
                ModelCommitted: false,
                _workspace,
                DockWorkspaceError.TargetGone,
                DockHostResult.Ok(),
                goneTarget,
                $"Target group '{targetGroupId}' no longer exists in '{targetHostId}'.");
        }

        var panel = drag.Panel;
        CancelDrag();
        return MovePanel(panel, target);
    }

    /// <summary>
    /// Main-root tear-off. If a valid target was already resolved, the target
    /// wins and the operation behaves as <see cref="CommitDrag"/>.
    /// </summary>
    public DockWorkspaceHostResult CommitTearOff(DockBounds bounds)
        => CommitTearOff(bounds, targetScreen: null);

    /// <summary>
    /// <paramref name="targetScreen"/> is the monitor under the pointer at
    /// release; the host places the new window on that work area (ADR-011 O-3).
    /// </summary>
    public DockWorkspaceHostResult CommitTearOff(DockBounds bounds, DockScreenArea? targetScreen)
    {
        // The summary reports target-wins semantics; this must stay above the
        // source/bounds checks so a late target is never discarded.
        if (_drag?.Target is not null)
            return CommitDrag();

        ThrowIfDisposed();

        var drag = _drag;
        if (drag is null)
        {
            return Reject(
                DockRootHostId.Main,
                DockHostError.NoTarget,
                "No cross-window drag session is active.");
        }

        if (!drag.SourceHostId.IsMain)
        {
            var source = drag.SourceHostId;
            CancelDrag();
            return new DockWorkspaceHostResult(
                false,
                ModelCommitted: false,
                _workspace,
                DockWorkspaceError.InvalidSource,
                DockHostResult.Ok(),
                source,
                "Only main-root panels can be torn off into a floating root.");
        }

        if (_shuttingDown)
        {
            var source = drag.SourceHostId;
            CancelDrag();
            return Reject(source, DockHostError.ShuttingDown, "The workspace is shutting down.");
        }

        if (!ValidateDragSource(drag, out var sourceFailure))
        {
            CancelDrag();
            return Reject(drag.SourceHostId, sourceFailure.Error, sourceFailure.Diagnostic);
        }

        var panel = drag.Panel;
        var clamped = ClampTearOffBounds(bounds);
        CancelDrag();
        return FloatPanel(panel, clamped, targetScreen);
    }

    /// <summary>Cancels the session and clears every external preview.</summary>
    public void CancelDrag()
    {
        if (_drag is null)
            return;

        _drag = null;
        ClearExternalPreviews();
        if (!_disposed && !_shuttingDown)
            DragEnded?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>
    /// Enters application shutdown. Floating hosts are closed without
    /// re-docking, so window-close callbacks cannot mutate the layout.
    /// </summary>
    public DockHostResult BeginShutdown()
    {
        ThrowIfDisposed();
        if (_shuttingDown)
            return DockHostResult.Ok();

        CancelDrag();
        _shuttingDown = true;
        var result = CloseFloatHosts();
        NotifyStateChanged();
        return result;
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        CancelDrag();
        _disposed = true;
        _shuttingDown = true;

        foreach (var hostId in _hosts.Keys.ToArray())
        {
            var host = _hosts[hostId];
            DetachHost(host);
            CloseHost(host);
            DisposeHost(host);
        }

        _hosts.Clear();
        _viewRegistry.Dispose();
    }

    private void AttachHost(IDockRootHost host)
    {
        if (host is IDockDragSourceHost dragSource)
            dragSource.DragEvent += OnHostDragEvent;

        if (host is IDockRootHostCallbacks callbacks)
        {
            callbacks.PanelActivated += OnHostPanelActivated;
            callbacks.SplitResizeRequested += OnHostSplitResizeRequested;
            callbacks.PanelMoveRequested += OnHostPanelMoveRequested;
            callbacks.PanelActionRequested += OnHostPanelActionRequested;
        }

        if (host is DockWindowRootHost windowHost)
        {
            windowHost.CloseRequested += OnFloatCloseRequested;
            windowHost.BoundsChanged += OnFloatBoundsChanged;
        }
    }

    private void DetachHost(IDockRootHost host)
    {
        if (host is IDockDragSourceHost dragSource)
            dragSource.DragEvent -= OnHostDragEvent;

        if (host is IDockRootHostCallbacks callbacks)
        {
            callbacks.PanelActivated -= OnHostPanelActivated;
            callbacks.SplitResizeRequested -= OnHostSplitResizeRequested;
            callbacks.PanelMoveRequested -= OnHostPanelMoveRequested;
            callbacks.PanelActionRequested -= OnHostPanelActionRequested;
        }

        if (host is DockWindowRootHost windowHost)
        {
            windowHost.CloseRequested -= OnFloatCloseRequested;
            windowHost.BoundsChanged -= OnFloatBoundsChanged;
        }
    }

    private void OnHostPanelActivated(object? sender, DockPanelActivatedEventArgs e)
    {
        if (_disposed || _shuttingDown || sender is not IDockRootHost host)
            return;

        e.Accepted = ActivateTab(host.Id, e.GroupId, e.Panel, e.Token).Success;
    }

    private void OnHostSplitResizeRequested(object? sender, DockSplitResizeRequestedEventArgs e)
    {
        if (_disposed || _shuttingDown || sender is not IDockRootHost host)
            return;

        e.Accepted = ResizeSplit(
            host.Id,
            e.SplitId,
            e.FirstWeight,
            e.SecondWeight,
            e.Token).Success;
    }

    private void OnHostPanelMoveRequested(object? sender, DockPanelMoveRequestedEventArgs e)
    {
        if (_disposed || _shuttingDown || sender is not IDockRootHost host)
            return;

        var invalid = ValidateCallback(host.Id, e.Token, nodeHost: null);
        if (invalid is not null)
            return;

        if (e.Panel.IsEmpty
            || !_workspace.TryLocatePanel(e.Panel, out var location)
            || HostIdFor(location) != host.Id)
        {
            return;
        }

        e.Accepted = MovePanel(e.Panel, e.Target).Success;
    }

    private void OnHostPanelActionRequested(object? sender, DockPanelActionRequestedEventArgs e)
    {
        if (_disposed || _shuttingDown || sender is not IDockRootHost host)
            return;

        var invalid = ValidateCallback(host.Id, e.Token, nodeHost: null);
        if (invalid is not null)
            return;

        if (e.Panel.IsEmpty
            || !_workspace.TryLocatePanel(e.Panel, out var location)
            || HostIdFor(location) != host.Id)
        {
            return;
        }

        e.Accepted = e.Action switch
        {
            DockPanelAction.Hide => HidePanel(e.Panel).Success,
            DockPanelAction.Close => ClosePanel(e.Panel).Success,
            _ => false,
        };
    }

    private void OnFloatCloseRequested(object? sender, EventArgs e)
    {
        if (_disposed || _shuttingDown || sender is not DockWindowRootHost host)
            return;

        if (_drag is { } drag && drag.SourceHostId == host.Id)
            CancelDrag();

        RequestCloseFloatRoot(host.FloatRootId);
    }

    private void OnFloatBoundsChanged(object? sender, DockFloatBoundsChangedEventArgs e)
    {
        if (_disposed || _shuttingDown || _drag is not null)
            return;

        if (sender is not DockWindowRootHost host)
            return;

        UpdateFloatBounds(host.FloatRootId, e.Bounds, e.Maximized);
    }

    private void SetFloatHostBounds(
        IDockRootHost host,
        DockFloatRoot floatRoot,
        DockScreenArea? preferredScreen = null)
    {
        if (host is not IDockFloatHostBounds boundsHost)
            return;

        var normal = floatRoot.Maximized ? floatRoot.RestoreBounds : floatRoot.Bounds;
        boundsHost.SetWindowBounds(normal, floatRoot.Maximized, preferredScreen);
    }

    /// <summary>
    /// Writes the geometry the platform actually settled on back into the model
    /// so persistence and history follow the visible window (ADR-011 O-2/O-3,
    /// §十一·二). Only client DIP numbers cross this line; screens stay in the
    /// host layer and the model never learns about them.
    /// </summary>
    private bool NormalizeFloatGeometry(IDockRootHost host)
    {
        if (host is not DockWindowRootHost windowHost)
            return false;

        var root = FindFloatRoot(windowHost.FloatRootId);
        if (root is null)
            return false;

        DockBounds normal;
        if (!root.Maximized && windowHost.TryGetWindowBounds(out var observed))
        {
            normal = observed;
        }
        else if (windowHost.LastPlacement is { } placement)
        {
            // A maximized window's observed rectangle is the maximized frame;
            // the persisted restore geometry is the placement of the normal
            // bounds that the host applied before maximizing.
            normal = placement.Bounds;
        }
        else
        {
            return false;
        }

        return ApplyFloatGeometry(root, normal);
    }

    private bool ApplyFloatGeometry(DockFloatRoot root, DockBounds normal)
    {
        normal = ClampTearOffBounds(normal);
        var bounds = root.Maximized ? root.Bounds : normal;
        if (bounds == root.Bounds && normal == root.RestoreBounds)
            return false;

        var floats = _workspace.FloatingRoots
            .Select(candidate => candidate.Id == root.Id
                ? new DockFloatRoot(
                    candidate.Id,
                    candidate.Root,
                    bounds,
                    candidate.Maximized,
                    candidate.ReturnTarget,
                    normal)
                : candidate)
            .ToArray();

        DockWorkspace normalized;
        try
        {
            normalized = DockWorkspace.Create(
                _workspace.MainRoot,
                floats,
                _workspace.Revision);
        }
        catch (DockModelException)
        {
            // The visible window already carries the placement; a model write
            // that cannot be expressed is not allowed to degrade the host.
            return false;
        }

        _workspace = normalized;
        return true;
    }

    private void OnHostDragEvent(object? sender, DockDragEventArgs e)
    {
        if (_disposed || sender is not IDockDragSourceHost source)
            return;

        switch (e.Kind)
        {
            case DockDragEventKind.Begin:
                BeginDrag(source.Id, e.Panel, e.Token);
                break;
            case DockDragEventKind.Move:
                HandleDragMove(source.Id, e);
                break;
            case DockDragEventKind.Release:
                HandleDragRelease(source.Id, e);
                break;
            case DockDragEventKind.Cancel:
                if (_drag?.SourceHostId == source.Id)
                    CancelDrag();
                break;
        }
    }

    private void HandleDragMove(DockRootHostId sourceHostId, DockDragEventArgs e)
    {
        var drag = _drag;
        if (drag is null || drag.SourceHostId != sourceHostId)
            return;

        if (e.LocalTargetResolved)
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return;
        }

        if (e.HasScreenPoint)
            UpdateDragExternal(e.ScreenPoint);
    }

    private void HandleDragRelease(DockRootHostId sourceHostId, DockDragEventArgs e)
    {
        var drag = _drag;
        if (drag is null || drag.SourceHostId != sourceHostId)
            return;

        if (e.LocalTargetResolved)
        {
            CancelDrag();
            return;
        }

        if (e.HasScreenPoint)
        {
            UpdateDragExternal(e.ScreenPoint);
            drag = _drag;
            if (drag is null)
                return;
        }

        if (drag.Target is not null)
        {
            CommitDrag();
            return;
        }

        if (e.HasScreenPoint
            && !e.InsideSource
            && (e.Modifiers & KeyModifiers.Control) != 0
            && drag.SourceHostId.IsMain)
        {
            // The pointer's own monitor decides both the DIP conversion and the
            // work area (ADR-011 O-3.1/O-3.2). The source window's scaling is
            // only a deterministic fallback when the platform cannot name one.
            var targetScreen = e.Screen;
            var scaling = targetScreen?.EffectiveScaling
                ?? (double.IsFinite(e.RenderScaling) && e.RenderScaling > 0d
                    ? e.RenderScaling
                    : 1d);
            CommitTearOff(
                new DockBounds(
                    e.ScreenPoint.X / scaling,
                    e.ScreenPoint.Y / scaling,
                    DockScreenGeometry.DefaultTearOffWidth,
                    DockScreenGeometry.DefaultTearOffHeight),
                targetScreen);
            return;
        }

        CancelDrag();
    }

    private bool ValidateDragSource(DockDragSession drag, out DockHostResult failure)
    {
        var host = FindHost(drag.SourceHostId);
        if (host is null || !host.IsHealthy)
        {
            failure = DockHostResult.Fail(
                DockHostError.HostUnavailable,
                $"Source host '{drag.SourceHostId}' is not available.");
            return false;
        }

        if (!host.TryGetCurrentToken(out var current) || !TokenMatches(current, drag.SourceToken))
        {
            failure = DockHostResult.Fail(
                DockHostError.StaleToken,
                $"Source host '{drag.SourceHostId}' no longer matches the drag token.");
            return false;
        }

        if (!_workspace.TryLocatePanel(drag.Panel, out var location)
            || HostIdFor(location) != drag.SourceHostId
            || location.GroupId != drag.SourceGroupId)
        {
            failure = DockHostResult.Fail(
                DockHostError.StaleToken,
                $"Panel '{drag.Panel}' is no longer in source group '{drag.SourceGroupId}'.");
            return false;
        }

        failure = DockHostResult.Ok();
        return true;
    }

    private DockDropTargetHit? ApplyExternalHit(
        DockDragSession drag,
        IDockDropTargetHost host,
        DockDropTargetHit hit)
    {
        var groupId = GroupOf(hit.Target);
        if (groupId is null)
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return null;
        }

        // The token is snapshotted when the target host/group is first hit;
        // re-entering the same target keeps the original snapshot.
        if (drag.TargetHostId != host.Id || drag.TargetGroupId != groupId)
        {
            var rootHost = FindHost(host.Id);
            if (rootHost is null
                || !rootHost.IsHealthy
                || !rootHost.TryGetCurrentToken(out var token))
            {
                ClearExternalPreviews();
                ResetExternalTarget(drag);
                return null;
            }

            drag.TargetHostId = host.Id;
            drag.TargetGroupId = groupId;
            drag.TargetToken = token;
        }

        drag.Target = hit.Target;
        ClearExternalPreviews(except: host.Id);
        try
        {
            host.ShowExternalDropPreview(hit);
        }
        catch
        {
            ClearExternalPreviews();
            ResetExternalTarget(drag);
            return null;
        }

        return hit;
    }

    private IEnumerable<DockRootHostId> ExternalDropTargetOrder(DockRootHostId sourceHostId)
    {
        foreach (var root in _workspace.FloatingRoots.Reverse())
        {
            var hostId = DockRootHostId.ForFloat(root.Id);
            if (hostId != sourceHostId)
                yield return hostId;
        }

        if (!sourceHostId.IsMain)
            yield return DockRootHostId.Main;
    }

    private void ClearExternalPreviews(DockRootHostId? except = null)
    {
        foreach (var host in _hosts.Values)
        {
            if (host is not IDockDropTargetHost target || target.Id == except)
                continue;

            try
            {
                target.ClearExternalDropPreview();
            }
            catch
            {
                // Previews are visual-only; a disposed host cannot break the session.
            }
        }
    }

    private static void ResetExternalTarget(DockDragSession drag)
    {
        drag.Target = null;
        drag.TargetHostId = null;
        drag.TargetToken = null;
        drag.TargetGroupId = null;
    }

    private static DockNodeId? GroupOf(DockTarget target)
        => target switch
        {
            DockTarget.Center center => center.GroupId,
            DockTarget.Edge edge => edge.GroupId,
            _ => null,
        };

    private static bool TokenMatches(DockApplyToken current, DockApplyToken expected)
        => current.HostId == expected.HostId
           && current.Generation == expected.Generation
           && string.Equals(current.Digest, expected.Digest, StringComparison.Ordinal);

    private static DockBounds ClampTearOffBounds(DockBounds bounds)
    {
        var width = double.IsFinite(bounds.Width)
            ? Math.Clamp(bounds.Width, 1d, DockBounds.MaxExtent)
            : 640d;
        var height = double.IsFinite(bounds.Height)
            ? Math.Clamp(bounds.Height, 1d, DockBounds.MaxExtent)
            : 480d;
        var x = double.IsFinite(bounds.X)
            ? Math.Clamp(bounds.X, -DockBounds.MaxCoordinate, DockBounds.MaxCoordinate)
            : 0d;
        var y = double.IsFinite(bounds.Y)
            ? Math.Clamp(bounds.Y, -DockBounds.MaxCoordinate, DockBounds.MaxCoordinate)
            : 0d;
        return new DockBounds(x, y, width, height);
    }

    private void InitializeMainHost()
    {
        var prepare = PrepareHost(_mainHost, _workspace.MainRoot);
        if (!prepare.Success)
            return;

        var apply = ApplyHost(_mainHost, _workspace.MainRoot);
        if (apply.Success)
            ShowHost(_mainHost);
    }

    private IReadOnlyList<string> InitializeRestoredHosts(out bool geometryNormalized)
    {
        var diagnostics = new List<string>();
        geometryNormalized = false;

        var mainPrepare = PrepareHost(_mainHost, _workspace.MainRoot);
        if (!mainPrepare.Success)
        {
            diagnostics.Add(
                $"Main host could not prepare the restored root: {mainPrepare.Diagnostic}");
        }
        else
        {
            var mainApply = ApplyHost(_mainHost, _workspace.MainRoot);
            if (!mainApply.Success)
            {
                diagnostics.Add(
                    $"Main host could not apply the restored root: {mainApply.Diagnostic}");
            }
            else
            {
                var mainShow = ShowHost(_mainHost);
                if (!mainShow.Success)
                {
                    diagnostics.Add(
                        $"Main host could not show the restored root: {mainShow.Diagnostic}");
                }
            }
        }

        foreach (var floatRoot in _workspace.FloatingRoots)
        {
            var hostId = DockRootHostId.ForFloat(floatRoot.Id);
            if (!_hosts.TryGetValue(hostId, out var host))
                continue;

            if (host is IDockFloatHostBounds boundsHost)
            {
                boundsHost.SetWindowBounds(
                    floatRoot.RestoreBounds,
                    floatRoot.Maximized,
                    preferredScreen: null);
            }

            var apply = ApplyHost(host, floatRoot.Root);
            if (!apply.Success)
            {
                diagnostics.Add(
                    $"Floating host '{hostId}' could not apply the restored root: {apply.Diagnostic}");
                continue;
            }

            var show = ShowHost(host);
            if (!show.Success)
            {
                diagnostics.Add(
                    $"Floating host '{hostId}' could not show the restored root: {show.Diagnostic}");
                continue;
            }

            geometryNormalized |= NormalizeFloatGeometry(host);
        }

        return diagnostics;
    }

    private DockWorkspaceHostResult MergeAndClose(
        DockFloatRootId rootId,
        DockTarget? target,
        bool closingRequest)
    {
        var hostId = DockRootHostId.ForFloat(rootId);
        var rejected = RejectIfUnavailable(hostId) ?? RejectIfDegraded(hostId);
        if (rejected is { } rejectedResult)
            return rejectedResult;

        var model = target is null
            ? _workspace.ReDockFloatRoot(rootId)
            : _workspace.MergeFloatRoot(rootId, target);
        if (!model.Success || model.Workspace is null)
        {
            return FromModelFailure(
                model,
                hostId,
                closingRequest ? DockHostError.ClosingCanceled : DockHostError.None);
        }

        var candidate = model.Workspace;
        var prepare = PrepareHost(_mainHost, candidate.MainRoot);
        if (!prepare.Success)
        {
            return Reject(
                _mainHost.Id,
                closingRequest ? DockHostError.ClosingCanceled : prepare.Error,
                $"Target host '{_mainHost.Id}' could not prepare the merged root; "
                + $"the floating root was not closed. {prepare.Diagnostic}");
        }

        _workspace = candidate;

        DockHostResult? failure = null;
        if (_hosts.Remove(hostId, out var host))
        {
            DetachHost(host);
            var close = CloseHost(host);
            DisposeHost(host);
            if (!close.Success)
                failure = close;
        }

        var mainApply = ApplyHost(_mainHost, _workspace.MainRoot);
        var mainShow = mainApply.Success && !_mainHost.IsVisible
            ? ShowHost(_mainHost)
            : mainApply;
        var mainFailed = !mainShow.Success;
        if (mainFailed && failure is null)
            failure = mainShow;

        if (failure is not { } hostFailure)
            return CommittedSuccess(hostId, model.Diagnostic);

        return CommittedHostFailure(mainFailed ? _mainHost.Id : hostId, hostFailure);
    }

    private DockWorkspaceHostResult ApplyHostAfterCommit(
        DockRootHostId hostId,
        string diagnostic,
        DockLayoutOperationSource source = DockLayoutOperationSource.Direct,
        bool recordHistory = true)
    {
        var host = FindHost(hostId);
        if (host is null)
        {
            return CommittedHostFailure(
                hostId,
                DockHostResult.Fail(
                    DockHostError.HostUnavailable,
                    $"Host '{hostId}' is not available."),
                source,
                recordHistory);
        }

        var root = RootForHost(hostId);
        if (root is null)
        {
            return CommittedHostFailure(
                hostId,
                DockHostResult.Fail(
                    DockHostError.HostUnavailable,
                    $"Host '{hostId}' has no root in the committed workspace."),
                source,
                recordHistory);
        }

        var apply = ApplyHost(host, root);
        return apply.Success
            ? CommittedSuccess(hostId, diagnostic, source, recordHistory)
            : CommittedHostFailure(hostId, apply, source, recordHistory);
    }

    private DockHostResult? ApplyInOrder(
        IReadOnlyList<DockRootHostId> hostIds,
        out DockRootHostId failedHost)
    {
        failedHost = DockRootHostId.Main;
        DockHostResult? failure = null;
        var seen = new HashSet<DockRootHostId>();

        foreach (var hostId in hostIds)
        {
            if (!seen.Add(hostId))
                continue;

            var host = FindHost(hostId);
            var root = RootForHost(hostId);
            if (host is null || root is null)
                continue;

            var apply = ApplyHost(host, root);
            if (!apply.Success && failure is null)
            {
                failure = apply;
                failedHost = hostId;
            }
        }

        return failure;
    }

    private DockHostResult CloseFloatHosts()
    {
        DockHostResult? failure = null;
        foreach (var hostId in _hosts.Keys.Where(id => !id.IsMain).ToArray())
        {
            if (!_hosts.Remove(hostId, out var host))
                continue;

            DetachHost(host);
            var close = CloseHost(host);
            DisposeHost(host);
            if (!close.Success && failure is null)
                failure = close;
        }

        return failure ?? DockHostResult.Ok();
    }

    private void ReleasePanelsRemoved(DockWorkspace previous, DockWorkspace current)
    {
        // Hidden panels also own no view. Release is idempotent, so a panel
        // that was already hidden and released is unaffected.
        var kept = new HashSet<PanelInstanceId>(current.EnumerateVisiblePanels());
        var removed = previous.EnumeratePanels()
            .Where(panel => !kept.Contains(panel))
            .Distinct()
            .ToArray();
        if (removed.Length > 0)
            ReleasePanels(removed);
    }

    private DockHostResult ReleasePanels(IReadOnlyList<PanelInstanceId> panels)
    {
        var failuresBefore = _viewRegistry.ReleaseFailureCount;
        foreach (var panel in panels)
            _viewRegistry.Release(panel);

        return _viewRegistry.ReleaseFailureCount == failuresBefore
            ? DockHostResult.Ok()
            : DockHostResult.Fail(
                DockHostError.CloseFailed,
                _viewRegistry.LastDiagnostic ?? "A panel view failed to dispose.");
    }

    private DockWorkspaceHostResult? ValidateCallback(
        DockRootHostId hostId,
        DockApplyToken token,
        DockRootHostId? nodeHost)
    {
        var host = FindHost(hostId);
        if (host is null || !host.IsHealthy)
        {
            return Reject(
                hostId,
                DockHostError.HostUnavailable,
                $"Host '{hostId}' is not available.");
        }

        if (token.HostId != hostId
            || !host.TryGetCurrentToken(out var current)
            || current.HostId != hostId
            || current.Generation != token.Generation
            || !string.Equals(current.Digest, token.Digest, StringComparison.Ordinal))
        {
            return Reject(
                hostId,
                DockHostError.StaleToken,
                $"Callback token for host '{hostId}' is stale.");
        }

        if (nodeHost is { } owner && owner != hostId)
        {
            return Reject(
                hostId,
                DockHostError.StaleToken,
                $"The callback node belongs to host '{owner}', not '{hostId}'.");
        }

        return null;
    }

    private DockWorkspaceHostResult FromModelFailure(
        DockWorkspaceResult model,
        DockRootHostId hostId,
        DockHostError hostError = DockHostError.None)
    {
        var host = hostError == DockHostError.None
            ? DockHostResult.Ok()
            : DockHostResult.Fail(hostError, model.Diagnostic);

        return new DockWorkspaceHostResult(
            false,
            ModelCommitted: false,
            _workspace,
            model.Error,
            host,
            hostId,
            model.Diagnostic);
    }

    private DockWorkspaceHostResult CommittedSuccess(
        DockRootHostId hostId,
        string diagnostic,
        DockLayoutOperationSource source = DockLayoutOperationSource.Direct,
        bool recordHistory = true)
    {
        RecordLayoutCommit(source, recordHistory);
        NotifyStateChanged();
        return new DockWorkspaceHostResult(
            true,
            true,
            _workspace,
            DockWorkspaceError.None,
            DockHostResult.Ok(),
            hostId,
            diagnostic);
    }

    private DockWorkspaceHostResult CommittedHostFailure(
        DockRootHostId hostId,
        DockHostResult host,
        DockLayoutOperationSource source = DockLayoutOperationSource.Direct,
        bool recordHistory = true)
    {
        RecordLayoutCommit(source, recordHistory);
        NotifyStateChanged();
        return new DockWorkspaceHostResult(
            false,
            ModelCommitted: true,
            _workspace,
            DockWorkspaceError.None,
            host,
            hostId,
            host.Diagnostic);
    }

    /// <summary>
    /// The only place a layout commit is written to the history. A replay
    /// carries its own source so the cursor moves instead of appending, and a
    /// navigation commit opts out. A duplicated notification of one commit is
    /// absorbed by the history's digest-equal check: adjacent entries never
    /// hold the same content, so that check can never swallow a legitimate
    /// replay. Recording runs before the notification because observers read
    /// the new cursor. Settlement calibration is not a commit: it replaces the
    /// current entry separately and never reaches this funnel (D9-A-10).
    /// </summary>
    private void RecordLayoutCommit(
        DockLayoutOperationSource source,
        bool recordHistory)
    {
        if (recordHistory)
            _layoutHistory.Record(DockLayoutSnapshot.Capture(_workspace), source);
    }

    private void NotifyStateChanged()
        => StateChanged?.Invoke(this, EventArgs.Empty);

    private DockWorkspaceHostResult Reject(
        DockRootHostId hostId,
        DockHostError error,
        string diagnostic)
        => new(
            false,
            ModelCommitted: false,
            _workspace,
            DockWorkspaceError.None,
            DockHostResult.Fail(error, diagnostic),
            hostId,
            diagnostic);

    private DockWorkspaceHostResult? RejectIfUnavailable(DockRootHostId hostId)
    {
        ThrowIfDisposed();
        return _shuttingDown
            ? Reject(hostId, DockHostError.ShuttingDown, "The workspace is shutting down.")
            : null;
    }

    /// <summary>
    /// D9-A-6 gate: while any host is degraded every entry that would change
    /// the workspace or its persistable geometry is refused before the model
    /// moves. Retry, shutdown and read-only queries stay available.
    /// </summary>
    private DockWorkspaceHostResult? RejectIfDegraded(DockRootHostId hostId)
    {
        if (!HasDegradedHosts)
            return null;

        return Reject(
            hostId,
            DockHostError.HostUnavailable,
            "A dock host is degraded. Retry it before changing the layout.");
    }

    private void RestoreCommittedFloatBounds(
        DockFloatRootId rootId,
        DockRootHostId hostId)
    {
        var root = FindFloatRoot(rootId);
        var host = FindHost(hostId);
        if (root is not null && host is not null)
            SetFloatHostBounds(host, root);
    }

    private DockHostResult PrepareHost(IDockRootHost host, DockTree root)
        => PrepareHostCore(host, root);

    private static DockHostResult PrepareHostCore(IDockRootHost host, DockTree root)
    {
        try
        {
            return host.Prepare(root);
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(DockHostError.ProjectionFailed, Describe(ex));
        }
    }

    private DockHostResult ApplyHost(IDockRootHost host, DockTree root)
    {
        try
        {
            return host.Apply(root);
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(DockHostError.ProjectionFailed, Describe(ex));
        }
    }

    private DockHostResult ShowHost(IDockRootHost host)
    {
        try
        {
            return host.Show();
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(DockHostError.ShowFailed, Describe(ex));
        }
    }

    private DockHostResult RetryHost(IDockRootHost host)
    {
        try
        {
            return host.Retry();
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(DockHostError.RetryFailed, Describe(ex));
        }
    }

    private DockHostResult CloseHost(IDockRootHost host)
    {
        try
        {
            return host.Close();
        }
        catch (Exception ex)
        {
            return DockHostResult.Fail(DockHostError.CloseFailed, Describe(ex));
        }
    }

    private static void DisposeHost(IDockRootHost? host)
    {
        try
        {
            host?.Dispose();
        }
        catch
        {
            // Model state is already authoritative; disposal only releases visuals.
        }
    }

    private DockRootHostId? ResolveGroupHost(DockTarget target)
    {
        var groupId = target switch
        {
            DockTarget.Center center => center.GroupId,
            DockTarget.Edge edge => edge.GroupId,
            _ => (DockNodeId?)null,
        };

        return groupId is { } id ? FindGroupHost(id) : null;
    }

    private DockRootHostId? FindGroupHost(DockNodeId groupId)
    {
        if (_workspace.MainRoot.FindGroup(groupId) is not null)
            return DockRootHostId.Main;

        foreach (var root in _workspace.FloatingRoots)
        {
            if (root.Root.FindGroup(groupId) is not null)
                return DockRootHostId.ForFloat(root.Id);
        }

        return null;
    }

    private DockRootHostId? FindSplitHost(DockNodeId splitId)
    {
        if (_workspace.MainRoot.FindSplit(splitId) is not null)
            return DockRootHostId.Main;

        foreach (var root in _workspace.FloatingRoots)
        {
            if (root.Root.FindSplit(splitId) is not null)
                return DockRootHostId.ForFloat(root.Id);
        }

        return null;
    }

    private DockTree? RootForHost(DockRootHostId hostId)
    {
        if (hostId.IsMain)
            return _workspace.MainRoot;

        foreach (var root in _workspace.FloatingRoots)
        {
            if (DockRootHostId.ForFloat(root.Id) == hostId)
                return root.Root;
        }

        return null;
    }

    private DockFloatRoot? FindFloatRoot(DockFloatRootId rootId)
    {
        foreach (var root in _workspace.FloatingRoots)
        {
            if (root.Id == rootId)
                return root;
        }

        return null;
    }

    private static DockRootHostId HostIdFor(DockWorkspaceLocation location)
        => location.IsMain
            ? DockRootHostId.Main
            : DockRootHostId.ForFloat(location.FloatRootId!.Value);

    private static string Describe(Exception ex) => $"{ex.GetType().Name}: {ex.Message}";

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(DockWorkspaceCoordinator));
    }

    private sealed class DockDragSession(
        DockRootHostId sourceHostId,
        PanelInstanceId panel,
        DockApplyToken sourceToken,
        DockNodeId sourceGroupId)
    {
        public DockRootHostId SourceHostId { get; } = sourceHostId;

        public PanelInstanceId Panel { get; } = panel;

        public DockApplyToken SourceToken { get; } = sourceToken;

        public DockNodeId SourceGroupId { get; } = sourceGroupId;

        public DockRootHostId? TargetHostId { get; set; }

        public DockApplyToken? TargetToken { get; set; }

        public DockNodeId? TargetGroupId { get; set; }

        public DockTarget? Target { get; set; }
    }
}
