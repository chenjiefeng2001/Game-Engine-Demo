using Nova.UI.Core.Tokens;

namespace Nova.UI.Avalonia;

/// <summary>
/// Names under which Nova tokens are published as Avalonia resources. Consumer
/// XAML should use these keys (or the helper methods) instead of literals.
/// </summary>
public static class NovaResourceKeys
{
    public const string Prefix = "Nova";

    public static string Brush(string tokenKey) => $"{Prefix}.Brush.{tokenKey}";
    public static string Color(string tokenKey) => $"{Prefix}.Color.{tokenKey}";
    public static string Number(string tokenKey) => $"{Prefix}.Number.{tokenKey}";
    public static string FontFamily(string tokenKey) => $"{Prefix}.FontFamily.{tokenKey}";
    public static string FontWeight(string tokenKey) => $"{Prefix}.FontWeight.{tokenKey}";
    public static string Duration(string tokenKey) => $"{Prefix}.Duration.{tokenKey}";

    public static string Of(DesignToken token) => token.Type switch
    {
        TokenType.Color => Brush(token.Key),
        TokenType.Dimension or TokenType.FontSize or TokenType.Number => Number(token.Key),
        TokenType.FontFamily => FontFamily(token.Key),
        TokenType.FontWeight => FontWeight(token.Key),
        TokenType.Duration => Duration(token.Key),
        _ => $"{Prefix}.String.{token.Key}",
    };
}
