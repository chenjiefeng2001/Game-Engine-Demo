using Nova.UI.Core.Tokens;

namespace Nova.UI.Theme;

/// <summary>Immutable catalog of built-in themes. Custom themes can be registered by the composition root.</summary>
public sealed class NovaThemeCatalog
{
    private readonly Dictionary<string, ThemeDefinition> _themes = new(StringComparer.Ordinal);

    public NovaThemeCatalog(IEnumerable<ThemeDefinition> themes)
    {
        foreach (var theme in themes)
            Register(theme);
    }

    public static NovaThemeCatalog BuiltIn { get; } = new(
    [
        new ThemeDefinition("nova.dark", "Nova Dark", isDark: true, NovaTokens.Dark),
        new ThemeDefinition("nova.light", "Nova Light", isDark: false, NovaTokens.Light),
    ]);

    public IReadOnlyCollection<ThemeDefinition> Themes => _themes.Values;

    public bool TryGet(string id, out ThemeDefinition theme)
        => _themes.TryGetValue(id, out theme!);

    public ThemeDefinition Get(string id)
        => _themes.TryGetValue(id, out var theme)
            ? theme
            : throw new KeyNotFoundException($"Theme '{id}' is not registered.");

    public ThemeDefinition GetOrDefault(string? id, string fallbackId = "nova.dark")
        => !string.IsNullOrWhiteSpace(id) && _themes.TryGetValue(id, out var theme)
            ? theme
            : Get(fallbackId);

    public bool Register(ThemeDefinition theme)
    {
        ArgumentNullException.ThrowIfNull(theme);
        return _themes.TryAdd(theme.Id, theme);
    }
}
