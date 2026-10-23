using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Nova.UI.Avalonia;
using Nova.UI.Theme;

namespace Nova.UI.Playground;

public partial class MainWindow : Window
{
    private readonly ThemeManager _themes;

    public MainWindow()
        : this(new ThemeManager(NovaThemeCatalog.BuiltIn))
    {
    }

    public MainWindow(ThemeManager themes)
    {
        _themes = themes ?? throw new ArgumentNullException(nameof(themes));
        InitializeComponent();
        ThemeLabel.Text = themes.Current.DisplayName;
    }

    private void OnToggleTheme(object? sender, RoutedEventArgs e)
    {
        var theme = _themes.Toggle();
        if (Application.Current is { } app)
            NovaThemeAdapter.Apply(app, theme);
        ThemeLabel.Text = theme.DisplayName;
    }
}
