using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia;
using Nova.UI.Avalonia.Docking;

namespace Nova.Editor.Views.Docking;

public partial class HierarchyPanelView : UserControl, IDockPanelView
{
    private readonly List<IDisposable> _bindings = [];
    private bool _disposed;

    public HierarchyPanelView() => InitializeComponent();

    public HierarchyPanelView(EditorShellViewModel viewModel) : this()
    {
        ViewModel = viewModel;
        DataContext = viewModel;
        _bindings.Add(NovaUiBinding.Bind(
            HierarchyCountInline,
            TextBlock.TextProperty,
            viewModel.Workspace.UiState,
            EditorUiIds.HierarchyCount,
            "text"));
    }

    public EditorShellViewModel? ViewModel { get; }

    public string Title => "Hierarchy";

    Control IDockPanelView.Content => this;

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        foreach (var binding in _bindings)
            binding.Dispose();
        _bindings.Clear();
        DataContext = null;
    }
}
