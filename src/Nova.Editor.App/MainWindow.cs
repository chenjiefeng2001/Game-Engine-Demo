using Avalonia.Controls;
using Avalonia.Threading;
using Nova.Editor.Services;
using Nova.Editor.Views;
using Nova.Editor.Views.ViewModels;

namespace Nova.Editor.App;

/// <summary>
/// Thin host window. It applies persisted window geometry and saves the measured
/// shell layout before closing; the shell itself owns the panel model.
/// </summary>
public sealed class MainWindow : Window
{
    private readonly EditorWorkspace _workspace;
    private readonly EditorShellView _shell;
    private bool _closeConfirmed;

    public MainWindow(EditorWorkspace workspace)
    {
        _workspace = workspace ?? throw new ArgumentNullException(nameof(workspace));
        Title = "Nova Editor — v0.2 Editor Foundation";
        MinWidth = 900;
        MinHeight = 620;
        Width = workspace.Layout.WindowWidth;
        Height = workspace.Layout.WindowHeight;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;

        _shell = new EditorShellView(new EditorShellViewModel(workspace));
        Content = _shell;

        workspace.LayoutChanged += OnLayoutChanged;
        Closing += OnClosing;
    }

    private void OnLayoutChanged(object? sender, LayoutChangedEventArgs e)
    {
        void Apply()
        {
            if (double.IsFinite(e.Layout.WindowWidth) && e.Layout.WindowWidth > 0)
                Width = e.Layout.WindowWidth;
            if (double.IsFinite(e.Layout.WindowHeight) && e.Layout.WindowHeight > 0)
                Height = e.Layout.WindowHeight;
        }

        if (Dispatcher.UIThread.CheckAccess()) Apply();
        else Dispatcher.UIThread.Post(Apply);
    }

    private async void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (_closeConfirmed) return;

        // Save asynchronously; cancel the first close and re-close when the write
        // completes. The UI thread is never blocked by file I/O.
        e.Cancel = true;
        await _shell.ViewModel!.SaveCurrentLayoutAsync(_shell.CollectLayout());
        _closeConfirmed = true;
        Close();
    }
}
