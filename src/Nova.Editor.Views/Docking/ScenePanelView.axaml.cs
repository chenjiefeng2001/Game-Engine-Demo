using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia;
using Nova.UI.Avalonia.Docking;

namespace Nova.Editor.Views.Docking;

public partial class ScenePanelView : UserControl, IDockPanelView
{
    private readonly List<IDisposable> _bindings = [];
    private bool _disposed;

    public ScenePanelView() => InitializeComponent();

    public ScenePanelView(EditorShellViewModel viewModel) : this()
    {
        ViewModel = viewModel;
        DataContext = viewModel;
        _bindings.Add(NovaUiBinding.Bind(
            ViewportStatusText,
            TextBlock.TextProperty,
            viewModel.Workspace.UiState,
            EditorUiIds.ViewportStatus,
            "text"));
    }

    public EditorShellViewModel? ViewModel { get; }

    public string Title => "Scene";

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
