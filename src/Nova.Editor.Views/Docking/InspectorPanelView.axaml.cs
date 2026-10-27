using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Controls;

namespace Nova.Editor.Views.Docking;

public partial class InspectorPanelView : UserControl, IDockPanelView
{
    private readonly List<IDisposable> _bindings = [];
    private bool _disposed;

    public InspectorPanelView() => InitializeComponent();

    public InspectorPanelView(EditorShellViewModel viewModel) : this()
    {
        ViewModel = viewModel;
        DataContext = viewModel;
        _bindings.Add(NovaUiBinding.Bind(
            InspectorHeaderText,
            TextBlock.TextProperty,
            viewModel.Workspace.UiState,
            EditorUiIds.InspectorHeader,
            "text"));
        _bindings.Add(NovaUiBinding.Bind(
            InspectorTypeText,
            TextBlock.TextProperty,
            viewModel.Workspace.UiState,
            EditorUiIds.InspectorType,
            "text"));
    }

    public EditorShellViewModel? ViewModel { get; }

    public string Title => "Inspector";

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

    private void OnTextCommitted(object? sender, string text)
    {
        if (sender is NovaTextField { DataContext: PropertyRowViewModel row })
            row.CommitText(text);
    }
}
