using System.Text;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Services.Layout;

public sealed record LayoutLoadResult(WorkspaceLayout Layout, bool UsedFallback, string? Reason);

public sealed record LayoutSaveResult(bool Success, string? Error)
{
    public static LayoutSaveResult Ok => new(true, null);
}

/// <summary>
/// Async, atomic layout persistence. Reads never run on the UI thread's critical
/// path; writes go to a temporary file and are swapped into place.
/// </summary>
public sealed class LayoutPersistenceService
{
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
            if (!File.Exists(FilePath))
                return new LayoutLoadResult(WorkspaceLayout.Default, true, "layout file does not exist");

            var json = await File.ReadAllTextAsync(FilePath, Encoding.UTF8, cancellationToken)
                .ConfigureAwait(false);
            var parsed = WorkspaceLayoutSerializer.Deserialize(json);
            if (parsed.UsedFallback)
                await QuarantineAsync(cancellationToken).ConfigureAwait(false);

            return new LayoutLoadResult(parsed.Layout, parsed.UsedFallback, parsed.Reason);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            // Missing/unreadable storage must never prevent the editor from starting.
            return new LayoutLoadResult(
                WorkspaceLayout.Default,
                true,
                $"layout could not be read: {ex.Message}");
        }
    }

    public async Task<LayoutSaveResult> SaveAsync(
        WorkspaceLayout layout,
        CancellationToken cancellationToken = default)
    {
        var normalized = LayoutValidator.Normalize(layout);
        var json = WorkspaceLayoutSerializer.Serialize(normalized);

        await _writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        string? temp = null;
        try
        {
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
            return new LayoutSaveResult(false, ex.Message);
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
