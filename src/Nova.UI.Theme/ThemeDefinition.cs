using Nova.UI.Core.Tokens;

namespace Nova.UI.Theme;

/// <summary>A theme is data: a token set plus presentation metadata. Controls contain no theme branches.</summary>
public sealed class ThemeDefinition
{
    public ThemeDefinition(string id, string displayName, bool isDark, DesignTokenSet tokens)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayName);
        Tokens = tokens ?? throw new ArgumentNullException(nameof(tokens));

        Id = id;
        DisplayName = displayName;
        IsDark = isDark;
    }

    public string Id { get; }
    public string DisplayName { get; }
    public bool IsDark { get; }
    public DesignTokenSet Tokens { get; }

    public IReadOnlyList<string> Validate()
        => Tokens.Validate(TokenKeys.Required);
}
