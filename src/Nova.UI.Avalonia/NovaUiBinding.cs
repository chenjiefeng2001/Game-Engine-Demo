using Avalonia;
using Avalonia.Threading;

namespace Nova.UI.Avalonia;

/// <summary>
/// Binds one Avalonia property to one (UiId, key) state slot. Only the affected
/// property is written when that slot changes; no panel or subtree is rebuilt.
/// </summary>
public sealed class NovaStateBinding<T> : IDisposable
{
    private readonly AvaloniaObject _target;
    private readonly AvaloniaProperty<T> _property;
    private readonly IUiStateReader _store;
    private readonly UiId _id;
    private readonly string _key;
    private bool _disposed;

    internal NovaStateBinding(
        AvaloniaObject target,
        AvaloniaProperty<T> property,
        IUiStateReader store,
        UiId id,
        string key)
    {
        _target = target;
        _property = property;
        _store = store;
        _id = id;
        _key = key;
        _store.Changed += OnStoreChanged;
        Apply(_store.Get(_id, _key));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _store.Changed -= OnStoreChanged;
    }

    private void OnStoreChanged(object? sender, UiStateChangedEventArgs e)
    {
        if (e.Changes.Count == 0)
        {
            // Clear() is an explicit "re-read everything" signal.
            Apply(_store.Get(_id, _key));
            return;
        }

        foreach (var change in e.Changes)
        {
            if (change.Id != _id || !string.Equals(change.Key, _key, StringComparison.Ordinal))
                continue;

            Apply(change.NewValue);
            return;
        }
    }

    private void Apply(object? value)
    {
        var typed = value is T t ? t : default;

        if (Dispatcher.UIThread.CheckAccess())
        {
            _target.SetCurrentValue(_property, typed);
        }
        else
        {
            Dispatcher.UIThread.Post(() =>
            {
                if (!_disposed)
                    _target.SetCurrentValue(_property, typed);
            });
        }
    }
}

public static class NovaUiBinding
{
    public static IDisposable Bind<T>(
        AvaloniaObject target,
        AvaloniaProperty<T> property,
        IUiStateReader store,
        UiId id,
        string key)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(property);
        ArgumentNullException.ThrowIfNull(store);
        if (id.IsEmpty) throw new ArgumentException("UiId is required.", nameof(id));
        ArgumentException.ThrowIfNullOrWhiteSpace(key);

        return new NovaStateBinding<T>(target, property, store, id, key);
    }
}
