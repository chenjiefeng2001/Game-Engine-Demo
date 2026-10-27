using Avalonia.Controls;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;

namespace Nova.Editor.Views.Docking;

public partial class ConsolePanelView : UserControl, IDockPanelView
{
    private bool _disposed;

    public ConsolePanelView() => InitializeComponent();

    public ConsolePanelView(EditorShellViewModel viewModel) : this()
    {
        ViewModel = viewModel;
        DataContext = viewModel;
    }

    public EditorShellViewModel? ViewModel { get; }

    public string Title => "Console";

    Control IDockPanelView.Content => this;

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        DataContext = null;
    }
}
