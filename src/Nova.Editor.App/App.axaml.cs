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
            var workspace = EditorWorkspace.CreateSample(FindLayoutPath(desktop.Args));
            NovaThemeAdapter.Apply(this, workspace.Theme.Current);
            desktop.MainWindow = new MainWindow(workspace);
        }

        base.OnFrameworkInitializationCompleted();
    }

    /// <summary>
    /// Production override for the layout file location. The default remains
    /// %LOCALAPPDATA%\NovaEditor\layout.json.
    /// </summary>
    private static string? FindLayoutPath(string[]? args)
    {
        if (args is null)
            return null;

        for (var index = 0; index < args.Length - 1; index++)
        {
            if (string.Equals(args[index], "--layout-file", StringComparison.Ordinal))
                return Path.GetFullPath(args[index + 1]);
        }

        return null;
    }
}
