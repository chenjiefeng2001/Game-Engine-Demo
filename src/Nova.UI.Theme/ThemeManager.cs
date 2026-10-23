namespace Nova.UI.Theme;

public sealed class ThemeChangedEventArgs : EventArgs
{
    public ThemeChangedEventArgs(ThemeDefinition previous, ThemeDefinition current)
    {
        Previous = previous;
        Current = current;
    }

    public ThemeDefinition Previous { get; }
    public ThemeDefinition Current { get; }
}

/// <summary>
/// Owns the active theme for one workspace. Not a global singleton: tests and
/// multiple editor windows can coexist without sharing visual state implicitly.
/// </summary>
public sealed class ThemeManager
{
    private readonly NovaThemeCatalog _catalog;
    private ThemeDefinition _current;

    public ThemeManager(NovaThemeCatalog catalog, string initialThemeId = "nova.dark")
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _current = catalog.GetOrDefault(initialThemeId);
    }

    public ThemeDefinition Current => _current;

    public event EventHandler<ThemeChangedEventArgs>? Changed;

    public bool TrySwitch(string themeId)
    {
        if (!_catalog.TryGet(themeId, out var next) || next.Id == _current.Id)
            return false;

        var previous = _current;
        _current = next;
        Changed?.Invoke(this, new ThemeChangedEventArgs(previous, next));
        return true;
    }

    public ThemeDefinition Toggle()
    {
        var nextId = _current.IsDark ? "nova.light" : "nova.dark";
        TrySwitch(nextId);
        return _current;
    }
}
