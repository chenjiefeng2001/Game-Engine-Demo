using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Nova.Editor.Services;
using Nova.UI.Avalonia;

namespace Nova.Editor.App;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // Composition root: the workspace owns state; the window owns the
            // Avalonia view; no static workspace singleton is involved.
            var workspace = EditorWorkspace.CreateSample();
            NovaThemeAdapter.Apply(this, workspace.Theme.Current);
            desktop.MainWindow = new MainWindow(workspace);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
