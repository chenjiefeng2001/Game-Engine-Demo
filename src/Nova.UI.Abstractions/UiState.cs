namespace Nova.UI;

public readonly record struct UiStateChange(
    long Revision,
    UiId Id,
    string Key,
    object? OldValue,
    object? NewValue);

public sealed class UiStateChangedEventArgs : EventArgs
{
    public UiStateChangedEventArgs(IReadOnlyList<UiStateChange> changes)
        => Changes = changes ?? throw new ArgumentNullException(nameof(changes));

    public IReadOnlyList<UiStateChange> Changes { get; }
}

/// <summary>
/// Read-only view over the UI state projection. Controls/binders depend on this
/// interface, not on the workspace that produced the state.
/// </summary>
public interface IUiStateReader
{
    long Revision { get; }

    bool TryGet<T>(UiId id, string key, out T? value);

    object? Get(UiId id, string key);

    /// <summary>Changes with <see cref="UiStateChange.Revision"/> greater than <paramref name="sinceRevision"/>.</summary>
    IReadOnlyList<UiStateChange> GetChanges(long sinceRevision);

    event EventHandler<UiStateChangedEventArgs>? Changed;
}

/// <summary>
/// Mutable state projection. It is intentionally not a global singleton: each
/// workspace owns one store and passes it to the UI layer explicitly.
/// </summary>
public interface IUiStateStore : IUiStateReader
{
    bool Set<T>(UiId id, string key, T value);

    bool Remove(UiId id, string key);

    void Clear();
}
