using System.Text;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Services.Layout;

public sealed record LayoutLoadResult(
    WorkspaceLayout Layout,
    bool UsedFallback,
    string? Reason,
    DockWorkspace? DockWorkspace = null,
    LayoutFallbackKind FallbackKind = LayoutFallbackKind.None,
    IReadOnlyList<PanelInstanceId>? UnresolvedPanels = null)
{
    /// <summary>
    /// Unknown panels are restored in place, but automatic writes must not
    /// replace a file that contains identities this build cannot resolve. A
    /// file that could not be read was never interpreted, so an automatic
    /// write could destroy data the editor never saw.
    /// </summary>
    public bool PersistenceSuppressed
        => FallbackKind is LayoutFallbackKind.ReadFailed
            || (UnresolvedPanels?.Count ?? 0) > 0;
}

public enum LayoutSaveStatus
{
    Saved,
    Skipped,
    Failed,
}

public sealed record LayoutSaveResult(LayoutSaveStatus Status, string? Error)
{
    public bool Success => Status == LayoutSaveStatus.Saved;

    public bool Skipped => Status == LayoutSaveStatus.Skipped;

    public static LayoutSaveResult Ok => new(LayoutSaveStatus.Saved, null);

    public static LayoutSaveResult Skip(string reason)
        => new(LayoutSaveStatus.Skipped, reason);

    public static LayoutSaveResult Fail(string error)
        => new(LayoutSaveStatus.Failed, error);
}

/// <summary>
/// Async, atomic layout persistence. Reads never run on the UI thread's critical
/// path; writes go to a temporary file and are swapped into place.
/// </summary>
public sealed class LayoutPersistenceService
{
    public const long MaxFileBytes = 1024 * 1024;

    private readonly SemaphoreSlim _writeGate = new(1, 1);

    public LayoutPersistenceService(string? filePath = null)
    {
        FilePath = filePath ?? DefaultFilePath();
    }

    public string FilePath { get; }

    public static string DefaultFilePath()
    {
        var baseDir = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(baseDir))
            baseDir = AppContext.BaseDirectory;
        return Path.Combine(baseDir, "NovaEditor", "layout.json");
    }

    public async Task<LayoutLoadResult> LoadAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var file = new FileInfo(FilePath);
            if (!file.Exists)
            {
                return new LayoutLoadResult(
                    WorkspaceLayout.Default,
                    true,
                    "layout file does not exist",
                    DockWorkspace.CreateDefault(),
                    LayoutFallbackKind.Missing);
            }

            if (file.Length > MaxFileBytes)
            {
                await QuarantineAsync(cancellationToken).ConfigureAwait(false);
                return new LayoutLoadResult(
                    WorkspaceLayout.Default,
                    true,
                    $"layout file is {file.Length} bytes and exceeds the 1 MiB limit",
                    DockWorkspace.CreateDefault(),
                    LayoutFallbackKind.Corrupt);
            }

            var json = await File.ReadAllTextAsync(FilePath, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(false);
            var parsed = WorkspaceLayoutSerializer.Deserialize(json);
            if (parsed.ShouldQuarantine)
                await QuarantineAsync(cancellationToken).ConfigureAwait(false);

            return new LayoutLoadResult(
                parsed.Layout,
                parsed.UsedFallback,
                parsed.Reason,
                parsed.DockWorkspace,
                parsed.FallbackKind,
                parsed.UnresolvedPanels);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            await QuarantineAsync(cancellationToken).ConfigureAwait(false);
            return new LayoutLoadResult(
                WorkspaceLayout.Default,
                true,
                $"layout could not be read: {ex.Message}",
                DockWorkspace.CreateDefault(),
                LayoutFallbackKind.ReadFailed);
        }
    }

    public async Task<LayoutSaveResult> SaveAsync(
        WorkspaceLayout layout,
        CancellationToken cancellationToken = default)
    {
        await _writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        string? temp = null;
        try
        {
            // Serialization and snapshot validation run before any file is
            // created or replaced; a bad snapshot cannot destroy the last
            // valid layout.
            var normalized = LayoutValidator.Normalize(layout);
            var json = WorkspaceLayoutSerializer.Serialize(normalized);

            var directory = Path.GetDirectoryName(FilePath);
            if (!string.IsNullOrEmpty(directory))
                Directory.CreateDirectory(directory);

            temp = $"{FilePath}.{Guid.NewGuid():N}.tmp";
            await File.WriteAllTextAsync(temp, json, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
                    cancellationToken)
                .ConfigureAwait(false);

            File.Move(temp, FilePath, overwrite: true);
            temp = null;
            return LayoutSaveResult.Ok;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            return LayoutSaveResult.Fail(ex.Message);
        }
        finally
        {
            if (temp is not null)
            {
                try { File.Delete(temp); }
                catch { /* best-effort cleanup */ }
            }
            _writeGate.Release();
        }
    }

    private async Task QuarantineAsync(CancellationToken cancellationToken)
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            var target = $"{FilePath}.corrupt-{DateTimeOffset.UtcNow:yyyyMMddHHmmss}";
            await Task.Run(() => File.Move(FilePath, target, overwrite: false), cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            // Quarantine is diagnostic only. The caller already has defaults.
        }
    }
}
