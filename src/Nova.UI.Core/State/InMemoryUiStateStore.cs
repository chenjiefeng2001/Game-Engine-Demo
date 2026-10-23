using Nova.UI;

namespace Nova.UI.Core.State;

/// <summary>
/// Thread-safe UI projection store. It is deliberately a projection, not a model:
/// the scene document remains the source of truth and commands publish the values
/// the UI should display.
///
/// The change log is bounded. If a consumer falls behind, it should re-read the
/// current value rather than replaying an unbounded history.
/// </summary>
public sealed class InMemoryUiStateStore : IUiStateStore
{
    private readonly object _gate = new();
    private readonly Dictionary<(UiId Id, string Key), object?> _values = new();
    private readonly Queue<UiStateChange> _changes = new();
    private readonly int _maxChanges;
    private long _revision;

    public InMemoryUiStateStore(int maxChanges = 4096)
    {
        if (maxChanges <= 0) throw new ArgumentOutOfRangeException(nameof(maxChanges));
        _maxChanges = maxChanges;
    }

    public long Revision
    {
        get { lock (_gate) return _revision; }
    }

    public event EventHandler<UiStateChangedEventArgs>? Changed;

    public bool TryGet<T>(UiId id, string key, out T? value)
    {
        Validate(id, key);
        lock (_gate)
        {
            if (_values.TryGetValue((id, key), out var raw))
            {
                if (raw is null)
                {
                    value = default;
                    return true;
                }

                if (raw is T typed)
                {
                    value = typed;
                    return true;
                }
            }
        }

        value = default;
        return false;
    }

    public object? Get(UiId id, string key)
    {
        Validate(id, key);
        lock (_gate)
            return _values.TryGetValue((id, key), out var value) ? value : null;
    }

    public IReadOnlyList<UiStateChange> GetChanges(long sinceRevision)
    {
        lock (_gate)
            return _changes.Where(change => change.Revision > sinceRevision).ToArray();
    }

    public bool Set<T>(UiId id, string key, T value)
    {
        Validate(id, key);

        UiStateChange change;
        lock (_gate)
        {
            var slot = (id, key);
            var exists = _values.TryGetValue(slot, out var old);
            if (exists && EqualityComparer<object?>.Default.Equals(old, value))
                return false;

            _values[slot] = value;
            _revision++;
            change = new UiStateChange(_revision, id, key, exists ? old : null, value);
            Append(change);
        }

        RaiseChanged([change]);
        return true;
    }

    public bool Remove(UiId id, string key)
    {
        Validate(id, key);

        UiStateChange change;
        lock (_gate)
        {
            var slot = (id, key);
            if (!_values.TryGetValue(slot, out var old))
                return false;

            _values.Remove(slot);
            _revision++;
            change = new UiStateChange(_revision, id, key, old, null);
            Append(change);
        }

        RaiseChanged([change]);
        return true;
    }

    public void Clear()
    {
        lock (_gate)
        {
            if (_values.Count == 0 && _changes.Count == 0) return;
            _values.Clear();
            _changes.Clear();
            _revision++;
        }

        // An empty change list means "re-read everything". It is not a per-key delta.
        RaiseChanged([]);
    }

    private void RaiseChanged(IReadOnlyList<UiStateChange> changes)
    {
        var handlers = Changed;
        if (handlers is null) return;

        var args = new UiStateChangedEventArgs(changes);
        foreach (var handler in handlers.GetInvocationList())
        {
            try
            {
                ((EventHandler<UiStateChangedEventArgs>)handler)(this, args);
            }
            catch
            {
                // State projection consumers are observers. A broken binder must
                // not roll back or block the model mutation that produced the state.
            }
        }
    }

    private void Append(UiStateChange change)
    {
        _changes.Enqueue(change);
        while (_changes.Count > _maxChanges)
            _changes.Dequeue();
    }

    private static void Validate(UiId id, string key)
    {
        if (id.IsEmpty) throw new ArgumentException("UiId is required.", nameof(id));
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
    }
}
