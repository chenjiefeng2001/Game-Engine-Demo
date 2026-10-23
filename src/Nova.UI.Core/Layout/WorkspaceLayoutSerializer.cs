using System.Text.Json;

namespace Nova.UI.Core.Layout;

public sealed record LayoutDeserializeResult(WorkspaceLayout Layout, bool UsedFallback, string? Reason)
{
    public static LayoutDeserializeResult Ok(WorkspaceLayout layout) => new(layout, false, null);

    public static LayoutDeserializeResult Fallback(WorkspaceLayout layout, string reason)
        => new(layout, true, reason);
}

public static class WorkspaceLayoutSerializer
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
    };

    public static string Serialize(WorkspaceLayout layout)
        => JsonSerializer.Serialize(LayoutValidator.Normalize(layout), Options);

    public static LayoutDeserializeResult Deserialize(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return LayoutDeserializeResult.Fallback(WorkspaceLayout.Default, "layout file is empty");

        try
        {
            var parsed = JsonSerializer.Deserialize<WorkspaceLayout>(json, Options);
            if (parsed is null)
                return LayoutDeserializeResult.Fallback(WorkspaceLayout.Default, "layout JSON was null");

            if (parsed.SchemaVersion != WorkspaceLayout.CurrentSchemaVersion)
            {
                return LayoutDeserializeResult.Fallback(
                    WorkspaceLayout.Default,
                    $"unsupported layout schema {parsed.SchemaVersion}");
            }

            return LayoutDeserializeResult.Ok(LayoutValidator.Normalize(parsed));
        }
        catch (JsonException ex)
        {
            return LayoutDeserializeResult.Fallback(WorkspaceLayout.Default, $"invalid layout JSON: {ex.Message}");
        }
        catch (NotSupportedException ex)
        {
            return LayoutDeserializeResult.Fallback(WorkspaceLayout.Default, $"unsupported layout data: {ex.Message}");
        }
    }
}
