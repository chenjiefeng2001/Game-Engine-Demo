using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Nova.UI.Avalonia;
using Nova.UI.Theme;

namespace Nova.UI.Playground;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var requestedTheme = Environment.GetEnvironmentVariable("NOVA_THEME");
            var themes = new ThemeManager(
                NovaThemeCatalog.BuiltIn,
                string.IsNullOrWhiteSpace(requestedTheme) ? "nova.dark" : requestedTheme);
            NovaThemeAdapter.Apply(this, themes.Current);
            desktop.MainWindow = new MainWindow(themes);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
