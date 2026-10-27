using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Nova.Editor.Services;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI;
using Nova.UI.Avalonia;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;
using Nova.UI.Theme;

namespace Nova.Editor.Views;

public partial class EditorShellView : UserControl
{
    private readonly List<IDisposable> _stateBindings = [];
    private readonly object _initializationGate = new();
    private readonly IDockNodeApplyInterceptor? _interceptor;
    private readonly IDockPanelViewFactory? _panelFactory;
    private Task? _initialization;
    private bool _subscribed;
    private bool _detached;

    public EditorShellView()
    {
        InitializeComponent();
    }

    public EditorShellView(EditorShellViewModel viewModel)
        : this(viewModel, interceptor: null, panelFactory: null)
    {
    }

    public EditorShellView(
        EditorShellViewModel viewModel,
        IDockNodeApplyInterceptor? interceptor,
        IDockPanelViewFactory? panelFactory) : this()
    {
        ArgumentNullException.ThrowIfNull(viewModel);
        ViewModel = viewModel;
        _interceptor = interceptor;
        _panelFactory = panelFactory;
        viewModel.Workspace.LayoutMeasurer = CollectLayout;
        DataContext = viewModel;

        Subscribe(viewModel.Workspace);
        BindState();
        Loaded += OnLoaded;
        DetachedFromVisualTree += OnDetached;
    }

    public EditorShellViewModel? ViewModel { get; }

    public EditorDockWorkspace? DockWorkspace { get; private set; }

    /// <summary>
    /// Loads and validates the layout before creating any dock host. Idempotent;
    /// repeat calls (Loaded, window close, tests) share the same task.
    /// </summary>
    public Task InitializeAsync()
    {
        if (ViewModel is null)
            return Task.CompletedTask;

        lock (_initializationGate)
            return _initialization ??= InitializeCoreAsync();
    }

    public WorkspaceLayout CollectLayout()
    {
        var workspace = ViewModel?.Workspace;
        var layout = workspace?.Layout ?? WorkspaceLayout.Default;
        var top = TopLevel.GetTopLevel(this);

        var width = top?.ClientSize.Width ?? Bounds.Width;
        var height = top?.ClientSize.Height ?? Bounds.Height;

        return LayoutValidator.Normalize(layout with
        {
            WindowWidth = width > 0 ? width : layout.WindowWidth,
            WindowHeight = height > 0 ? height : layout.WindowHeight,
            ThemeId = workspace?.Theme.Current.Id ?? layout.ThemeId,
        });
    }

    private async void OnLoaded(object? sender, RoutedEventArgs e)
    {
        try
        {
            await InitializeAsync();
        }
        catch (Exception ex)
        {
            if (ViewModel is not null)
                ViewModel.StatusText = $"Layout initialization failed: {ex.Message}";
        }
    }

    private async Task InitializeCoreAsync()
    {
        var viewModel = ViewModel!;
        var loaded = await viewModel.InitializeLayoutAsync();
        if (_detached)
            return;

        var candidate = loaded.DockWorkspace
                        ?? Nova.UI.Core.Docking.DockWorkspace.CreateDefault();
        var dock = new EditorDockWorkspace(
            viewModel,
            _interceptor,
            _panelFactory,
            floatHostFactory: null,
            restoreWorkspace: candidate);

        if (_detached)
        {
            dock.Dispose();
            return;
        }

        DockWorkspace = dock;
        DockRoot.Content = dock.Root;
        dock.StateChanged += OnDockStateChanged;

        var workspace = viewModel.Workspace;
        foreach (var diagnostic in dock.RestoreDiagnostics)
            workspace.Log.Warning(diagnostic);

        workspace.AttachDockPersistence(
            dock,
            dirty: dock.RestoreFailure is not null || dock.RestoreMergedFloatRoots.Count > 0);
        workspace.EnableAutoSave();
        workspace.LayoutSaveCompleted += OnLayoutSaveCompleted;

        if (dock.RestoreFailure is { } failure)
        {
            workspace.Log.Warning($"Dock restore failed; using defaults: {failure}");
            workspace.SuppressPersistence("the dock workspace could not be restored");
            viewModel.StatusText = $"Dock restore failed: {failure}";
        }
        else if (dock.RestoreMergedFloatRoots.Count > 0)
        {
            workspace.Log.Warning(
                $"{dock.RestoreMergedFloatRoots.Count} floating panel root(s) were merged into the main workspace during restore.");
        }

        UpdateDockState();
    }

    private void Subscribe(EditorWorkspace workspace)
    {
        if (_subscribed) return;
        _subscribed = true;
        workspace.Theme.Changed += OnThemeChanged;
    }

    private void BindState()
    {
        if (ViewModel is null) return;
        var store = ViewModel.Workspace.UiState;

        _stateBindings.Add(NovaUiBinding.Bind(
            StatusText, TextBlock.TextProperty, store, EditorUiIds.SelectionStatus, "text"));
        _stateBindings.Add(NovaUiBinding.Bind(
            HierarchyCountText, TextBlock.TextProperty, store, EditorUiIds.HierarchyCount, "text"));
    }

    private void OnThemeChanged(object? sender, ThemeChangedEventArgs e)
    {
        void Apply()
        {
            if (Application.Current is { } app)
                NovaThemeAdapter.Apply(app, e.Current);
        }

        if (Dispatcher.UIThread.CheckAccess()) Apply();
        else Dispatcher.UIThread.Post(Apply);
    }

    private void OnResetLayout(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is null) return;

        // Reset the dock first: the command's explicit save captures the
        // post-reset workspace, never the old float roots.
        if (DockWorkspace is { IsDisposed: false } dock)
            dock.ResetToDefault();

        ViewModel.ExecuteCommand(NovaCommandIds.ResetLayout);
    }

    private void OnRetryDockProjection(object? sender, RoutedEventArgs e)
    {
        if (DockWorkspace is not { IsDisposed: false } dock)
            return;

        DockRetryButton.IsEnabled = false;
        try
        {
            dock.Retry();
        }
        finally
        {
            UpdateDockState();
        }
    }

    private void OnDockStateChanged(object? sender, EventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
            UpdateDockState();
        else
            Dispatcher.UIThread.Post(UpdateDockState);
    }

    private void OnLayoutSaveCompleted(object? sender, LayoutSaveCompletedEventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
            UpdateDockState();
        else
            Dispatcher.UIThread.Post(UpdateDockState);
    }

    private void UpdateDockState()
    {
        var dock = DockWorkspace;
        var mainDegraded = dock is { IsDegraded: true };
        var anyDegraded = dock is { HasDegradedHosts: true };

        UpdateHiddenPanelsMenu(dock);
        UpdateDockDiagnostic(dock);

        DockDegradedBanner.IsVisible = anyDegraded;
        DockRetryButton.IsEnabled = anyDegraded && dock is { IsRetrying: false };

        if (!anyDegraded)
        {
            DockDegradedText.Text = string.Empty;
            return;
        }

        if (!mainDegraded)
        {
            var count = dock!.DegradedFloatCount;
            DockDegradedText.Text = count == 1
                ? "A floating panel could not be rendered. Retry to restore it."
                : $"{count} floating panels could not be rendered. Retry to restore them.";
            return;
        }

        var diagnostic = dock!.Current?.Diagnostic;
        DockDegradedText.Text = string.IsNullOrWhiteSpace(diagnostic)
            ? "The dock layout could not be rendered. Retry to restore the current workspace."
            : $"The dock layout could not be rendered. {diagnostic}";
    }

    private void UpdateHiddenPanelsMenu(EditorDockWorkspace? dock)
    {
        var items = HiddenPanelsMenu.Items;
        items.Clear();

        if (dock is null || dock.IsDisposed)
        {
            items.Add(new MenuItem { Header = "No workspace", IsEnabled = false });
            return;
        }

        var hidden = dock.HiddenPanels;
        if (hidden.Count == 0)
        {
            items.Add(new MenuItem { Header = "No hidden panels", IsEnabled = false });
            return;
        }

        foreach (var panel in hidden)
        {
            var item = new MenuItem { Header = panel.ToString() };
            item.Click += (_, _) => ShowHiddenPanel(panel);
            items.Add(item);
        }
    }

    private void ShowHiddenPanel(PanelInstanceId panel)
    {
        if (DockWorkspace is not { IsDisposed: false } dock)
            return;

        dock.ShowPanel(panel);
        UpdateDockState();
    }

    private void UpdateDockDiagnostic(EditorDockWorkspace? dock)
    {
        var unresolved = dock?.UnresolvedPanels ?? Array.Empty<PanelInstanceId>();
        if (unresolved.Count == 0)
        {
            DockDiagnosticText.Text = string.Empty;
            DockDiagnosticText.IsVisible = false;
            return;
        }

        var paused = ViewModel?.Workspace.IsPersistenceSuppressed == true;
        DockDiagnosticText.Text = paused
            ? $"{unresolved.Count} panel(s) unavailable in this build · "
              + "autosave paused, use Save Layout"
            : $"{unresolved.Count} panel(s) unavailable in this build";
        DockDiagnosticText.IsVisible = true;
    }

    private void OnExit(object? sender, RoutedEventArgs e)
    {
        // MainWindow owns the close-save-close sequence; do not write twice.
        if (TopLevel.GetTopLevel(this) is Window window)
            window.Close();
    }

    private void OnDetached(object? sender, VisualTreeAttachmentEventArgs e)
    {
        _detached = true;
        Loaded -= OnLoaded;
        foreach (var binding in _stateBindings)
            binding.Dispose();
        _stateBindings.Clear();

        if (ViewModel is not null)
            ViewModel.Workspace.Theme.Changed -= OnThemeChanged;
        _subscribed = false;

        if (ViewModel is not null)
        {
            ViewModel.Workspace.LayoutSaveCompleted -= OnLayoutSaveCompleted;
            ViewModel.Workspace.DetachDockPersistence();
        }

        if (DockWorkspace is not null)
        {
            DockWorkspace.StateChanged -= OnDockStateChanged;
            DockWorkspace.Dispose();
        }

        DockRoot.Content = null;
    }
}
