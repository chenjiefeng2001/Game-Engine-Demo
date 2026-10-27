using System.Text.Json;
using System.Text.Json.Serialization;
using Nova.UI.Core.Docking;

namespace Nova.UI.Core.Layout;

public enum LayoutFallbackKind
{
    None,
    Corrupt,
    UnsupportedSchema,
    InvalidDock,
    Missing,
    ReadFailed,
}

public sealed record LayoutDeserializeResult(
    WorkspaceLayout Layout,
    DockWorkspace? DockWorkspace,
    LayoutFallbackKind FallbackKind,
    string? Reason,
    IReadOnlyList<PanelInstanceId>? UnresolvedPanels = null)
{
    public bool UsedFallback => FallbackKind != LayoutFallbackKind.None;

    public bool ShouldQuarantine => FallbackKind is LayoutFallbackKind.Corrupt
        or LayoutFallbackKind.UnsupportedSchema
        or LayoutFallbackKind.InvalidDock
        or LayoutFallbackKind.ReadFailed;

    public static LayoutDeserializeResult Ok(
        WorkspaceLayout layout,
        DockWorkspace dockWorkspace,
        IReadOnlyList<PanelInstanceId>? unresolvedPanels = null,
        string? reason = null)
        => new(layout, dockWorkspace, LayoutFallbackKind.None, reason, unresolvedPanels);

    public static LayoutDeserializeResult Fallback(
        WorkspaceLayout layout,
        LayoutFallbackKind kind,
        string reason,
        DockWorkspace? dockWorkspace = null,
        IReadOnlyList<PanelInstanceId>? unresolvedPanels = null)
        => new(layout, dockWorkspace ?? DockWorkspace.CreateDefault(), kind, reason, unresolvedPanels);
}

public static class WorkspaceLayoutSerializer
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        MaxDepth = 256,
    };

    public static string Serialize(WorkspaceLayout layout)
    {
        ArgumentNullException.ThrowIfNull(layout);

        var normalized = LayoutValidator.Normalize(layout);
        if (normalized.Dock is not null)
        {
            var candidate = DockWorkspaceSnapshotCodec.ToWorkspace(normalized.Dock);
            if (!candidate.IsValid || candidate.Workspace is null)
            {
                throw new DockModelException(
                    $"Cannot serialize an invalid dock snapshot: {candidate.Diagnostic}");
            }

            // Unknown panels are opaque identities: they are written back
            // verbatim instead of being rejected or replaced.
            normalized = normalized with
            {
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(candidate.Workspace),
            };
        }

        return JsonSerializer.Serialize(normalized, Options);
    }

    public static LayoutDeserializeResult Deserialize(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return LayoutDeserializeResult.Fallback(
                WorkspaceLayout.Default,
                LayoutFallbackKind.Corrupt,
                "layout file is empty");
        }

        try
        {
            var parsed = JsonSerializer.Deserialize<WorkspaceLayout>(json, Options);
            if (parsed is null)
            {
                return LayoutDeserializeResult.Fallback(
                    WorkspaceLayout.Default,
                    LayoutFallbackKind.Corrupt,
                    "layout JSON was null");
            }

            if (parsed.SchemaVersion == 1)
            {
                var migrated = LayoutValidator.Normalize(parsed with { Dock = null });
                return LayoutDeserializeResult.Ok(migrated, DockWorkspace.CreateDefault());
            }

            if (parsed.SchemaVersion != 2
                && parsed.SchemaVersion != WorkspaceLayout.CurrentSchemaVersion)
            {
                return LayoutDeserializeResult.Fallback(
                    WorkspaceLayout.Default,
                    LayoutFallbackKind.UnsupportedSchema,
                    $"unsupported layout schema {parsed.SchemaVersion}");
            }

            // Hidden declarations are a schema v3 concept. A v2 file that
            // carries them is ambiguous, so it is rejected instead of being
            // silently reinterpreted or repaired.
            if (parsed.SchemaVersion < WorkspaceLayout.CurrentSchemaVersion
                && parsed.Dock is not null
                && DockWorkspaceSnapshotCodec.ContainsHiddenDeclarations(parsed.Dock))
            {
                return LayoutDeserializeResult.Fallback(
                    WorkspaceLayout.Default,
                    LayoutFallbackKind.InvalidDock,
                    $"schema {parsed.SchemaVersion} layout declares hidden panels, "
                    + $"which require schema {WorkspaceLayout.CurrentSchemaVersion}");
            }

            var shell = LayoutValidator.Normalize(parsed);
            if (shell.Dock is null)
                return LayoutDeserializeResult.Ok(shell, DockWorkspace.CreateDefault());

            var dock = DockWorkspaceSnapshotCodec.ToWorkspace(shell.Dock);
            if (!dock.IsValid || dock.Workspace is null)
            {
                return LayoutDeserializeResult.Fallback(
                    WorkspaceLayout.Default,
                    LayoutFallbackKind.InvalidDock,
                    $"invalid dock layout: {dock.Diagnostic}");
            }

            // Unknown panel ids are opaque identities. They are restored in
            // place with their order and hidden state; only automatic writes
            // are suppressed until the user saves explicitly.
            var restored = shell with
            {
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(dock.Workspace),
            };

            if (dock.UnresolvedPanels.Count > 0)
            {
                return LayoutDeserializeResult.Ok(
                    restored,
                    dock.Workspace,
                    dock.UnresolvedPanels,
                    "dock layout contains panels this build cannot resolve: "
                    + string.Join(", ", dock.UnresolvedPanels.Select(panel => panel.ToString())));
            }

            return LayoutDeserializeResult.Ok(restored, dock.Workspace);
        }
        catch (JsonException ex)
        {
            return LayoutDeserializeResult.Fallback(
                WorkspaceLayout.Default,
                LayoutFallbackKind.Corrupt,
                $"invalid layout JSON: {ex.Message}");
        }
        catch (NotSupportedException ex)
        {
            return LayoutDeserializeResult.Fallback(
                WorkspaceLayout.Default,
                LayoutFallbackKind.Corrupt,
                $"unsupported layout data: {ex.Message}");
        }
    }
}
