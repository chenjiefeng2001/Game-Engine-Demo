namespace Nova.Editor.Services.Diagnostics;

public enum EditorLogLevel
{
    Info,
    Warning,
    Error,
}

public sealed record EditorLogEntry(DateTimeOffset Timestamp, EditorLogLevel Level, string Message);

/// <summary>Small in-memory log used by the shell console. Thread-safe and bounded.</summary>
public sealed class EditorLogService
{
    private readonly object _gate = new();
    private readonly Queue<EditorLogEntry> _entries = new();
    private readonly int _capacity;

    public EditorLogService(int capacity = 2000)
    {
        if (capacity <= 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        _capacity = capacity;
    }

    public event EventHandler<EditorLogEntry>? EntryAdded;

    public void Info(string message) => Write(EditorLogLevel.Info, message);
    public void Warning(string message) => Write(EditorLogLevel.Warning, message);
    public void Error(string message) => Write(EditorLogLevel.Error, message);

    public IReadOnlyList<EditorLogEntry> Snapshot()
    {
        lock (_gate) return _entries.ToArray();
    }

    private void Write(EditorLogLevel level, string message)
    {
        var entry = new EditorLogEntry(DateTimeOffset.Now, level, message);
        lock (_gate)
        {
            _entries.Enqueue(entry);
            while (_entries.Count > _capacity)
                _entries.Dequeue();
        }

        RaiseEntryAdded(entry);
    }

    private void RaiseEntryAdded(EditorLogEntry entry)
    {
        var handlers = EntryAdded;
        if (handlers is null) return;

        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler<EditorLogEntry>)handler)(this, entry);
            }
            catch
            {
                // Diagnostics sinks must not be able to break a command or the
                // editor shell. A broken console subscriber is logged nowhere,
                // but the model/history invariant remains intact.
            }
        }
    }
}
