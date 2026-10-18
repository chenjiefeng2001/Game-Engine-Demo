using Avalonia;
using System;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using AvaloniaEditor.Services;

namespace AvaloniaEditor;

public partial class App : Application
{
    private readonly EditorHostService _host;

    public App() : this(RequireInjectedHost())
    {
    }

    public App(EditorHostService host)
    {
        _host = host ?? throw new ArgumentNullException(nameof(host));
    }

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.MainWindow = new MainWindow(_host);
        base.OnFrameworkInitializationCompleted();
    }

    private static EditorHostService RequireInjectedHost()
        => throw new InvalidOperationException("App requires the composition host");
}
