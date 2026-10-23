using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Nova.Editor.Services;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia;
using Nova.UI.Controls;
using Nova.UI.Core.Layout;
using Nova.UI.Core.Tokens;
using Nova.UI.Theme;

namespace Nova.Editor.Views;

public partial class EditorShellView : UserControl
{
    private readonly List<IDisposable> _stateBindings = new();
    private double _lastHierarchyWidth = 240;
    private double _lastInspectorWidth = 300;
    private double _lastConsoleHeight = 180;
    private bool _subscribed;

    public EditorShellView()
    {
        InitializeComponent();
    }

    public EditorShellView(EditorShellViewModel viewModel) : this()
    {
        ViewModel = viewModel;
        viewModel.Workspace.LayoutMeasurer = CollectLayout;
        DataContext = viewModel;
        Subscribe(viewModel.Workspace);
        ApplyLayout(viewModel.Workspace.Layout);
        BindState();
        Loaded += OnLoaded;
        DetachedFromVisualTree += OnDetached;
    }

    public EditorShellViewModel? ViewModel { get; }

    public WorkspaceLayout CollectLayout()
    {
        var workspace = ViewModel?.Workspace;
        var layout = workspace?.Layout ?? WorkspaceLayout.Default;
        var top = TopLevel.GetTopLevel(this);

        var hierarchyWidth = HierarchyPanel.IsVisible && MainGrid.ColumnDefinitions[0].ActualWidth > 0
            ? MainGrid.ColumnDefinitions[0].ActualWidth
            : _lastHierarchyWidth;
        var inspectorWidth = InspectorPanel.IsVisible && MainGrid.ColumnDefinitions[4].ActualWidth > 0
            ? MainGrid.ColumnDefinitions[4].ActualWidth
            : _lastInspectorWidth;
        var consoleHeight = ConsolePanel.IsVisible && RootGrid.RowDefinitions[2].ActualHeight > 0
            ? RootGrid.RowDefinitions[2].ActualHeight
            : _lastConsoleHeight;

        var width = top?.ClientSize.Width ?? Bounds.Width;
        var height = top?.ClientSize.Height ?? Bounds.Height;

        return LayoutValidator.Normalize(layout with
        {
            WindowWidth = width > 0 ? width : layout.WindowWidth,
            WindowHeight = height > 0 ? height : layout.WindowHeight,
            HierarchyWidth = hierarchyWidth,
            InspectorWidth = inspectorWidth,
            ConsoleHeight = consoleHeight,
            ShowHierarchy = HierarchyPanel.IsVisible,
            ShowInspector = InspectorPanel.IsVisible,
            ShowConsole = ConsolePanel.IsVisible,
            ThemeId = workspace?.Theme.Current.Id ?? layout.ThemeId,
        });
    }

    public void ApplyLayout(WorkspaceLayout layout)
    {
        // Capture the real splitter sizes before hiding a panel, so showing it
        // again (or saving later) does not snap back to a stale model value.
        if (HierarchyPanel.IsVisible && MainGrid.ColumnDefinitions[0].ActualWidth > 0)
            _lastHierarchyWidth = MainGrid.ColumnDefinitions[0].ActualWidth;
        if (InspectorPanel.IsVisible && MainGrid.ColumnDefinitions[4].ActualWidth > 0)
            _lastInspectorWidth = MainGrid.ColumnDefinitions[4].ActualWidth;
        if (ConsolePanel.IsVisible && RootGrid.RowDefinitions[2].ActualHeight > 0)
            _lastConsoleHeight = RootGrid.RowDefinitions[2].ActualHeight;

        _lastHierarchyWidth = layout.ShowHierarchy ? layout.HierarchyWidth : _lastHierarchyWidth;
        _lastInspectorWidth = layout.ShowInspector ? layout.InspectorWidth : _lastInspectorWidth;
        _lastConsoleHeight = layout.ShowConsole ? layout.ConsoleHeight : _lastConsoleHeight;

        HierarchyPanel.IsVisible = layout.ShowHierarchy;
        HierarchySplitter.IsVisible = layout.ShowHierarchy;
        InspectorPanel.IsVisible = layout.ShowInspector;
        InspectorSplitter.IsVisible = layout.ShowInspector;
        ConsolePanel.IsVisible = layout.ShowConsole;
        ConsoleSplitter.IsVisible = layout.ShowConsole;

        var splitter = ViewModel?.Workspace.Theme.Current.Tokens.GetNumber(TokenKeys.SplitterSize) ?? 6;

        MainGrid.ColumnDefinitions[0].Width = new GridLength(
            layout.ShowHierarchy ? layout.HierarchyWidth : 0, GridUnitType.Pixel);
        MainGrid.ColumnDefinitions[1].Width = new GridLength(
            layout.ShowHierarchy ? splitter : 0, GridUnitType.Pixel);
        MainGrid.ColumnDefinitions[4].Width = new GridLength(
            layout.ShowInspector ? layout.InspectorWidth : 0, GridUnitType.Pixel);
        MainGrid.ColumnDefinitions[3].Width = new GridLength(
            layout.ShowInspector ? splitter : 0, GridUnitType.Pixel);

        RootGrid.RowDefinitions[2].Height = new GridLength(
            layout.ShowConsole ? layout.ConsoleHeight : 0, GridUnitType.Pixel);
        RootGrid.RowDefinitions[1].Height = new GridLength(
            layout.ShowConsole ? splitter : 0, GridUnitType.Pixel);
    }

    private async void OnLoaded(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is null) return;

        var result = await ViewModel.InitializeLayoutAsync();
        ApplyLayout(ViewModel.Workspace.Layout);

        if (result.UsedFallback)
            ViewModel.StatusText = $"Default layout: {result.Reason}";
    }

    private void Subscribe(EditorWorkspace workspace)
    {
        if (_subscribed) return;
        _subscribed = true;
        workspace.LayoutChanged += OnLayoutChanged;
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
        _stateBindings.Add(NovaUiBinding.Bind(
            HierarchyCountInline, TextBlock.TextProperty, store, EditorUiIds.HierarchyCount, "text"));
        _stateBindings.Add(NovaUiBinding.Bind(
            InspectorHeaderText, TextBlock.TextProperty, store, EditorUiIds.InspectorHeader, "text"));
        _stateBindings.Add(NovaUiBinding.Bind(
            InspectorTypeText, TextBlock.TextProperty, store, EditorUiIds.InspectorType, "text"));
        _stateBindings.Add(NovaUiBinding.Bind(
            ViewportStatusText, TextBlock.TextProperty, store, EditorUiIds.ViewportStatus, "text"));
    }

    private void OnLayoutChanged(object? sender, LayoutChangedEventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
            ApplyLayout(e.Layout);
        else
            Dispatcher.UIThread.Post(() => ApplyLayout(e.Layout));
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

    private void OnTextCommitted(object? sender, string text)
    {
        if (sender is NovaTextField { DataContext: PropertyRowViewModel row })
            row.CommitText(text);
    }

    private void OnExit(object? sender, RoutedEventArgs e)
    {
        // MainWindow owns the close-save-close sequence; do not write twice.
        if (TopLevel.GetTopLevel(this) is Window window)
            window.Close();
    }

    private void OnDetached(object? sender, VisualTreeAttachmentEventArgs e)
    {
        Loaded -= OnLoaded;
        foreach (var binding in _stateBindings)
            binding.Dispose();
        _stateBindings.Clear();

        if (ViewModel is not null)
        {
            ViewModel.Workspace.LayoutChanged -= OnLayoutChanged;
            ViewModel.Workspace.Theme.Changed -= OnThemeChanged;
        }
        _subscribed = false;
    }
}
