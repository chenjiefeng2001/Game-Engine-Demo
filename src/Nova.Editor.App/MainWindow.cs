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
    private bool _closing;

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
        if (_closeConfirmed)
            return;

        // The first close is always deferred; the actual close repeats after the
        // shutdown sequence, including when the save or initialization fails.
        e.Cancel = true;
        if (_closing)
            return;

        _closing = true;
        try
        {
            await _shell.InitializeAsync();

            // The snapshot must be immutable and captured while hosts are still
            // alive. BeginShutdown only runs after the capture succeeds.
            var snapshot = _workspace.CaptureShutdownSnapshot(out var reason);
            try
            {
                _shell.DockWorkspace?.BeginShutdown();
            }
            catch (Exception ex)
            {
                _workspace.Log.Warning($"Dock shutdown failed: {ex.Message}");
            }

            if (snapshot is null)
            {
                if (!string.IsNullOrWhiteSpace(reason))
                    _workspace.Log.Warning($"Final layout not saved: {reason}");
            }
            else
            {
                await _workspace.SaveShutdownSnapshotAsync(snapshot);
            }
        }
        catch (Exception ex)
        {
            _workspace.Log.Warning($"Editor shutdown failed before the final save: {ex.Message}");
        }
        finally
        {
            _closeConfirmed = true;
            Close();
        }
    }
}
