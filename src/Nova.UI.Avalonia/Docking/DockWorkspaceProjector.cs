using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Projects a committed main-workspace <see cref="DockTree"/> onto Avalonia
/// controls. The tree stays authoritative; node containers and panel views are
/// derived caches and can be discarded on failure.
/// </summary>
public sealed class DockWorkspaceProjector : IDisposable
{
    private const int MaxBuildAttempts = 2;

    private readonly ContentControl _root = new() { Name = "DockWorkspaceRoot" };
    private readonly DockDragController _dragController;
    private readonly IDockPanelViewFactory? _panelFactory;
    private readonly DockPanelViewRegistry? _registry;
    private readonly DockRootHostId _hostId;
    private readonly IDockNodeApplyInterceptor? _interceptor;
    private readonly Dictionary<PanelInstanceId, PanelEntry> _panels = [];
    private readonly Dictionary<DockNodeId, Control> _mountedNodes = [];
    private readonly List<TabItem> _mountedTabItems = [];
    private readonly Dictionary<PanelInstanceId, TabItem> _tabItemsByPanel = [];
    private readonly List<TokenSlot> _mountedTokenSlots = [];

    private DockTree? _committedTree;
    private DockApplyResult? _current;
    private long _generation;
    private string? _appliedDigest;
    private bool _disposed;

    public DockWorkspaceProjector(
        IDockPanelViewFactory panelFactory,
        IDockNodeApplyInterceptor? interceptor = null)
    {
        _panelFactory = panelFactory ?? throw new ArgumentNullException(nameof(panelFactory));
        _hostId = DockRootHostId.Main;
        _interceptor = interceptor;
        _dragController = new DockDragController(_root, this);
        _dragController.DragEvent += (_, e) => DragEvent?.Invoke(this, e);
    }

    /// <summary>
    /// Creates a projector that mounts views owned by a workspace registry.
    /// Disposing this projector detaches visuals but never releases the views.
    /// </summary>
    public DockWorkspaceProjector(
        DockPanelViewRegistry registry,
        DockRootHostId hostId,
        IDockNodeApplyInterceptor? interceptor = null)
    {
        _registry = registry ?? throw new ArgumentNullException(nameof(registry));
        if (hostId.IsEmpty)
            throw new ArgumentException("A dock host id must be non-empty.", nameof(hostId));

        _hostId = hostId;
        _interceptor = interceptor;
        _dragController = new DockDragController(_root, this);
        _dragController.DragEvent += (_, e) => DragEvent?.Invoke(this, e);
    }

    private bool OwnsPanelViews => _registry is null;

    public Control Root => _root;

    public bool IsDisposed => _disposed;

    public DockApplyResult? Current => _current;

    internal DockTree? CommittedTree => _committedTree;

    internal bool TryGetCurrentToken(out DockApplyToken token)
    {
        var current = _current;
        if (!_disposed && current is { IsCurrent: true })
        {
            token = new DockApplyToken(_hostId, current.Generation, current.CommittedDigest);
            return true;
        }

        token = default;
        return false;
    }

    internal bool RequestPanelMove(
        PanelInstanceId panel,
        DockTarget target,
        DockApplyToken token)
    {
        if (_disposed)
            return false;

        var request = new DockPanelMoveRequestedEventArgs(panel, target, token);
        PanelMoveRequested?.Invoke(this, request);
        return request.Accepted;
    }

    internal bool TryHitTestDrop(
        Point point,
        DockNodeId? sourceGroupId,
        out DockDropTargetHit hit)
        => _dragController.TryHitTestDrop(point, sourceGroupId, out hit);

    internal void ShowExternalDropPreview(DockDropTargetHit hit)
        => _dragController.ShowExternalPreview(hit);

    internal void ClearExternalDropPreview()
        => _dragController.ClearExternalPreview();

    public event EventHandler<DockPanelActivatedEventArgs>? PanelActivated;

    public event EventHandler<DockSplitResizeRequestedEventArgs>? SplitResizeRequested;

    public event EventHandler<DockPanelMoveRequestedEventArgs>? PanelMoveRequested;

    public event EventHandler<DockPanelActionRequestedEventArgs>? PanelActionRequested;

    internal event EventHandler<DockDragEventArgs>? DragEvent;

    public DockApplyResult Apply(DockTree tree)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(tree);

        var digest = tree.Digest;
        if (_current is { } current
            && string.Equals(current.CommittedDigest, digest, StringComparison.Ordinal))
        {
            if (current.Status == DockApplyStatus.Degraded)
                return current;

            var unchanged = new DockApplyResult(
                DockApplyStatus.Unchanged,
                current.Generation,
                digest,
                current.AppliedDigest,
                current.PanelFailures,
                Diagnostic: null);
            _current = unchanged;
            return unchanged;
        }

        return Project(tree, digest);
    }

    public DockApplyResult Retry()
    {
        ThrowIfDisposed();
        if (_current is null)
            throw new InvalidOperationException("Retry requires a previously applied dock tree.");

        _dragController.CancelSession();

        if (_current.Status == DockApplyStatus.Degraded)
        {
            RetryFailedPanels();
            return Project(_committedTree!, _current.CommittedDigest);
        }

        if (_current.PanelFailures.Count == 0)
            return _current;

        _generation++;
        var refreshedToken = new DockApplyToken(
            _hostId,
            _generation,
            _current.CommittedDigest);
        foreach (var slot in _mountedTokenSlots)
            slot.Value = refreshedToken;

        foreach (var instance in _current.PanelFailures)
        {
            if (!_panels.TryGetValue(instance, out var entry) || !entry.Failed)
                continue;

            RetryPanel(entry);
        }

        var retried = new DockApplyResult(
            DockApplyStatus.Applied,
            _generation,
            _current.CommittedDigest,
            _appliedDigest,
            CollectFailures(_committedTree!),
            Diagnostic: null);
        _current = retried;
        return retried;
    }

    public Control? FindNode(DockNodeId id)
        => _mountedNodes.TryGetValue(id, out var control) ? control : null;

    public IDockPanelView? FindPanel(PanelInstanceId instance)
        => _panels.TryGetValue(instance, out var entry) ? entry.View : null;

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        _dragController.Dispose();
        DetachCurrentVisuals();

        if (OwnsPanelViews)
        {
            foreach (var entry in _panels.Values)
            {
                entry.Host.Content = null;
                entry.View?.Dispose();
                entry.View = null;
            }
        }
        else
        {
            foreach (var entry in _panels.Values)
                entry.View = null;
        }

        _panels.Clear();
        _current = null;
        _committedTree = null;
        _appliedDigest = null;
    }

    private DockApplyResult Project(DockTree tree, string digest)
    {
        _dragController.OnProjectionReplaced();
        // Hidden panels have no mounted view. With a registry the coordinator
        // releases the hidden view; a locally owned projector releases it here.
        var finalPanels = new HashSet<PanelInstanceId>(tree.EnumerateVisiblePanels());
        DetachCurrentVisuals();

        string? lastFailure = null;
        for (var attempt = 0; attempt < MaxBuildAttempts; attempt++)
        {
            _generation++;
            var token = new DockApplyToken(_hostId, _generation, digest);
            var nodes = new Dictionary<DockNodeId, Control>();
            var tabItems = new List<TabItem>();
            var tabItemsByPanel = new Dictionary<PanelInstanceId, TabItem>();
            var tokenSlots = new List<TokenSlot>();

            try
            {
                var built = BuildNode(
                    tree.Root, token, nodes, tabItems, tabItemsByPanel, tokenSlots);

                _root.Content = _dragController.CreateHost(built);
                _mountedNodes.Clear();
                foreach (var pair in nodes)
                    _mountedNodes[pair.Key] = pair.Value;
                _mountedTabItems.AddRange(tabItems);
                _tabItemsByPanel.Clear();
                foreach (var pair in tabItemsByPanel)
                    _tabItemsByPanel[pair.Key] = pair.Value;
                _mountedTokenSlots.AddRange(tokenSlots);

                ReleasePanelsNotIn(finalPanels);
                _committedTree = tree;
                _appliedDigest = digest;

                var applied = new DockApplyResult(
                    DockApplyStatus.Applied,
                    _generation,
                    digest,
                    digest,
                    CollectFailures(tree),
                    Diagnostic: null);
                _current = applied;
                return applied;
            }
            catch (Exception ex)
            {
                lastFailure = $"{ex.GetType().Name}: {ex.Message}";
                DiscardAttempt(tabItems);
                tokenSlots.Clear();
                _dragController.OnProjectionReplaced();
                _root.Content = null;
                _mountedNodes.Clear();
                _mountedTabItems.Clear();
                _tabItemsByPanel.Clear();
                _mountedTokenSlots.Clear();
            }
        }

        ReleasePanelsNotIn(finalPanels);
        _committedTree = tree;

        var degraded = new DockApplyResult(
            DockApplyStatus.Degraded,
            _generation,
            digest,
            _appliedDigest,
            CollectFailures(tree),
            lastFailure);
        _current = degraded;
        _root.Content = CreateDegradedPlaceholder(digest, lastFailure);
        return degraded;
    }

    private Control BuildNode(
        DockNode node,
        DockApplyToken token,
        Dictionary<DockNodeId, Control> nodes,
        List<TabItem> tabItems,
        Dictionary<PanelInstanceId, TabItem> tabItemsByPanel,
        List<TokenSlot> tokenSlots)
    {
        _interceptor?.OnApplying(node.Id, token);

        switch (node)
        {
            case DockSplitNode split:
                return BuildSplit(split, token, nodes, tabItems, tabItemsByPanel, tokenSlots);

            case DockTabGroupNode group:
                return BuildTabGroup(group, token, nodes, tabItems, tabItemsByPanel, tokenSlots);

            default:
                throw new InvalidOperationException(
                    $"Unsupported dock node type '{node.GetType().Name}'.");
        }
    }

    private Control BuildSplit(
        DockSplitNode split,
        DockApplyToken token,
        Dictionary<DockNodeId, Control> nodes,
        List<TabItem> tabItems,
        Dictionary<PanelInstanceId, TabItem> tabItemsByPanel,
        List<TokenSlot> tokenSlots)
    {
        if (split.First is null || split.Second is null)
            throw new InvalidOperationException($"Split '{split.Id}' has a null child.");

        var grid = new Grid();
        if (split.Orientation == DockOrientation.Horizontal)
        {
            grid.ColumnDefinitions.Add(
                new ColumnDefinition(new GridLength(split.FirstWeight, GridUnitType.Star)));
            grid.ColumnDefinitions.Add(
                new ColumnDefinition(new GridLength(split.SecondWeight, GridUnitType.Star)));
        }
        else
        {
            grid.RowDefinitions.Add(
                new RowDefinition(new GridLength(split.FirstWeight, GridUnitType.Star)));
            grid.RowDefinitions.Add(
                new RowDefinition(new GridLength(split.SecondWeight, GridUnitType.Star)));
        }

        var first = BuildNode(
            split.First, token, nodes, tabItems, tabItemsByPanel, tokenSlots);
        var second = BuildNode(
            split.Second, token, nodes, tabItems, tabItemsByPanel, tokenSlots);

        var slot = new TokenSlot(token);
        var splitter = new DockSplitterThumb(
            grid,
            split.Id,
            split.Orientation,
            split.FirstWeight,
            split.SecondWeight);
        splitter.CommitRequested += (_, e) =>
        {
            if (_disposed)
                return;

            var request = new DockSplitResizeRequestedEventArgs(
                split.Id, e.FirstWeight, e.SecondWeight, slot.Value);
            SplitResizeRequested?.Invoke(this, request);
            e.Accepted = request.Accepted;
        };

        if (split.Orientation == DockOrientation.Horizontal)
        {
            Grid.SetColumn(first, 0);
            Grid.SetColumn(second, 1);
            Grid.SetColumn(splitter, 0);
        }
        else
        {
            Grid.SetRow(first, 0);
            Grid.SetRow(second, 1);
            Grid.SetRow(splitter, 0);
        }

        splitter.ZIndex = 1;
        grid.Children.Add(first);
        grid.Children.Add(second);
        grid.Children.Add(splitter);
        tokenSlots.Add(slot);
        nodes[split.Id] = grid;
        return grid;
    }

    private Control BuildTabGroup(
        DockTabGroupNode group,
        DockApplyToken token,
        Dictionary<DockNodeId, Control> nodes,
        List<TabItem> tabItems,
        Dictionary<PanelInstanceId, TabItem> tabItemsByPanel,
        List<TokenSlot> tokenSlots)
    {
        var tabs = new TabControl();
        var slot = new TokenSlot(token);
        var selectingInitialTab = true;
        var resettingSelection = false;
        var selectedIndex = -1;

        tabs.SelectionChanged += (_, _) =>
        {
            if (selectingInitialTab || resettingSelection || _disposed)
                return;

            if (tabs.SelectedItem is not TabItem { Tag: PanelInstanceId panel })
                return;

            var args = new DockPanelActivatedEventArgs(group.Id, panel, slot.Value);
            PanelActivated?.Invoke(this, args);
            if (args.Accepted || selectedIndex < 0)
                return;

            resettingSelection = true;
            try
            {
                tabs.SelectedIndex = selectedIndex;
            }
            finally
            {
                resettingSelection = false;
            }
        };

        for (var index = 0; index < group.VisiblePanels.Count; index++)
        {
            var instance = group.VisiblePanels[index];
            var entry = GetOrCreateEntry(instance);
            var item = new TabItem
            {
                Header = entry.Title,
                Content = entry.Host,
                Tag = instance,
                ContextMenu = CreateTabMenu(instance, slot),
            };

            tabs.Items.Add(item);
            tabItems.Add(item);
            tabItemsByPanel[instance] = item;

            if (group.ActivePanel == instance)
                selectedIndex = index;
        }

        tabs.SelectedIndex = selectedIndex;
        selectingInitialTab = false;
        tokenSlots.Add(slot);
        nodes[group.Id] = tabs;
        return tabs;
    }

    private ContextMenu CreateTabMenu(PanelInstanceId instance, TokenSlot slot)
    {
        var menu = new ContextMenu();

        if (_hostId.IsMain)
        {
            var floatItem = new MenuItem { Header = "Float Panel" };
            floatItem.Click += (_, _) =>
            {
                if (_disposed)
                    return;

                var request = new DockPanelMoveRequestedEventArgs(
                    instance,
                    new DockTarget.Float(CreateFloatBounds()),
                    slot.Value);
                PanelMoveRequested?.Invoke(this, request);
            };
            menu.Items.Add(floatItem);
        }

        var hideItem = new MenuItem { Header = "Hide Panel" };
        hideItem.Click += (_, _) => RequestPanelAction(instance, DockPanelAction.Hide, slot);

        var closeItem = new MenuItem { Header = "Close Panel" };
        closeItem.Click += (_, _) => RequestPanelAction(instance, DockPanelAction.Close, slot);

        menu.Items.Add(hideItem);
        menu.Items.Add(closeItem);
        return menu;
    }

    private void RequestPanelAction(
        PanelInstanceId instance,
        DockPanelAction action,
        TokenSlot slot)
    {
        if (_disposed)
            return;

        var request = new DockPanelActionRequestedEventArgs(instance, action, slot.Value);
        PanelActionRequested?.Invoke(this, request);
    }

    private DockBounds CreateFloatBounds()
    {
        if (TopLevel.GetTopLevel(_root) is Window window)
        {
            var scaling = window.RenderScaling > 0 ? window.RenderScaling : 1d;
            return new DockBounds(
                window.Position.X / scaling + 48d,
                window.Position.Y / scaling + 48d,
                640d,
                480d);
        }

        return new DockBounds(120d, 80d, 640d, 480d);
    }

    private PanelEntry GetOrCreateEntry(PanelInstanceId instance)
    {
        if (_registry is { } registry)
        {
            var registration = registry.GetOrCreate(instance);
            if (_panels.TryGetValue(instance, out var existing)
                && ReferenceEquals(existing.Host, registration.Host))
            {
                ApplyRegistration(existing, registration);
                return existing;
            }

            _panels.Remove(instance);
            var registered = new PanelEntry(instance, registration.Host);
            ApplyRegistration(registered, registration);
            _panels.Add(instance, registered);
            return registered;
        }

        if (_panels.TryGetValue(instance, out var existingLocal))
            return existingLocal;

        var entry = new PanelEntry(instance, new ContentControl());
        _panels.Add(instance, entry);
        CreateOrReplaceView(entry);
        return entry;
    }

    private static void ApplyRegistration(
        PanelEntry entry,
        DockPanelViewRegistration registration)
    {
        entry.View = registration.View;
        entry.Title = registration.Title;
        entry.Failure = registration.Failure;
        entry.Version = registration.Version;
        entry.Unresolved = registration.Unresolved;
    }

    private void RetryPanel(PanelEntry entry)
    {
        if (_registry is { } registry)
            ApplyRegistration(entry, registry.Retry(entry.Instance));
        else
            CreateOrReplaceView(entry);

        UpdateMountedHeader(entry);
    }

    private void CreateOrReplaceView(PanelEntry entry)
    {
        if (_registry is not null)
        {
            throw new InvalidOperationException(
                "Registry-owned panel views cannot be replaced by the projector.");
        }

        var factory = _panelFactory
            ?? throw new InvalidOperationException("No panel view factory is configured.");

        if (!factory.CanCreate(entry.Instance))
        {
            SetPanelUnavailable(entry);
            return;
        }

        IDockPanelView? view = null;
        try
        {
            view = factory.Create(entry.Instance)
                ?? throw new InvalidOperationException("Panel view factory returned null.");

            var content = view.Content
                ?? throw new InvalidOperationException(
                    $"Panel view for '{entry.Instance}' has no content.");

            entry.Host.Content = null;
            entry.View = null;
            entry.Host.Content = content;

            entry.View = view;
            entry.Title = string.IsNullOrWhiteSpace(view.Title)
                ? entry.Instance.ToString()
                : view.Title;
            entry.Failure = null;
            view = null;
        }
        catch (Exception ex)
        {
            view?.Dispose();
            SetPanelFailure(entry, ex.Message);
        }
    }

    private void SetPanelFailure(PanelEntry entry, string message)
    {
        entry.Host.Content = null;
        entry.View = null;
        entry.Unresolved = false;
        entry.Failure = string.IsNullOrWhiteSpace(message)
            ? "Panel view creation failed."
            : message;
        entry.Title = entry.Instance.ToString();
        entry.Host.Content = CreatePanelPlaceholder(entry.Instance, entry.Failure);
    }

    private void SetPanelUnavailable(PanelEntry entry)
    {
        entry.Host.Content = null;
        entry.View = null;
        entry.Unresolved = true;
        entry.Failure = null;
        entry.Title = entry.Instance.ToString();
        entry.Host.Content = CreatePanelUnavailablePlaceholder(entry.Instance);
    }

    private void RetryFailedPanels()
    {
        var tree = _committedTree;
        if (tree is null)
            return;

        var finalPanels = new HashSet<PanelInstanceId>(tree.EnumerateVisiblePanels());
        var failed = _panels.Values
            .Where(entry => entry.Failed && finalPanels.Contains(entry.Instance))
            .ToArray();
        if (failed.Length == 0)
            return;

        _generation++;
        foreach (var entry in failed)
            RetryPanel(entry);
    }

    private void UpdateMountedHeader(PanelEntry entry)
    {
        if (_tabItemsByPanel.TryGetValue(entry.Instance, out var item))
            item.Header = entry.Title;
    }

    private IReadOnlyList<PanelInstanceId> CollectFailures(DockTree tree)
    {
        List<PanelInstanceId>? failures = null;
        foreach (var instance in tree.EnumerateVisiblePanels())
        {
            if (_panels.TryGetValue(instance, out var entry) && entry.Failed)
                (failures ??= []).Add(instance);
        }

        return failures is null ? Array.Empty<PanelInstanceId>() : failures.AsReadOnly();
    }

    private void ReleasePanelsNotIn(HashSet<PanelInstanceId> finalPanels)
    {
        List<PanelInstanceId>? removed = null;
        foreach (var instance in _panels.Keys)
        {
            if (!finalPanels.Contains(instance))
                (removed ??= []).Add(instance);
        }

        if (removed is null)
            return;

        foreach (var instance in removed)
        {
            var entry = _panels[instance];
            _panels.Remove(instance);
            if (!OwnsPanelViews)
                continue;

            entry.Host.Content = null;
            entry.View?.Dispose();
            entry.View = null;
        }
    }

    private void DetachCurrentVisuals()
    {
        foreach (var item in _mountedTabItems)
            item.Content = null;

        _mountedTabItems.Clear();
        _tabItemsByPanel.Clear();
        _mountedTokenSlots.Clear();
        _mountedNodes.Clear();
        _root.Content = null;
    }

    private static void DiscardAttempt(List<TabItem> tabItems)
    {
        foreach (var item in tabItems)
            item.Content = null;
        tabItems.Clear();
    }

    private static Control CreatePanelPlaceholder(
        PanelInstanceId instance,
        string failure)
    {
        return new TextBlock
        {
            Name = "DockPanelFailure",
            Text = $"{instance}\nPanel view failed: {failure}",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(12),
        };
    }

    private static Control CreatePanelUnavailablePlaceholder(PanelInstanceId instance)
    {
        return new TextBlock
        {
            Name = "DockPanelUnavailable",
            Text = $"{instance}\nThis panel is not available in this build.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(12),
        };
    }

    private static Control CreateDegradedPlaceholder(string digest, string? failure)
    {
        return new TextBlock
        {
            Name = "DockProjectionDegraded",
            Text = "Dock projection degraded\n"
                   + $"Committed digest: {digest}\n"
                   + $"{failure ?? "Visual application failed."}\n"
                   + "Call Retry() to rebuild from the committed layout.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(24),
        };
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(DockWorkspaceProjector));
    }

    private sealed class TokenSlot(DockApplyToken token)
    {
        public DockApplyToken Value { get; set; } = token;
    }

    private sealed class PanelEntry(PanelInstanceId instance, ContentControl host)
    {
        public PanelInstanceId Instance { get; } = instance;

        public ContentControl Host { get; } = host;

        public IDockPanelView? View { get; set; }

        public string Title { get; set; } = string.Empty;

        public string? Failure { get; set; }

        public long Version { get; set; }

        public bool Unresolved { get; set; }

        public bool Failed => View is null && !Unresolved;
    }
}
