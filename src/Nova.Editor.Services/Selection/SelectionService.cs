using Nova.UI;

namespace Nova.Editor.Services.Selection;

public sealed class SelectionChangedEventArgs : EventArgs
{
    public SelectionChangedEventArgs(UiId? previous, UiId? current)
    {
        Previous = previous;
        Current = current;
    }

    public UiId? Previous { get; }
    public UiId? Current { get; }
}

/// <summary>Selection is workspace state, not a static global.</summary>
public sealed class SelectionService
{
    public UiId? SelectedId { get; private set; }

    public event EventHandler<SelectionChangedEventArgs>? Changed;

    public bool Select(UiId? id)
    {
        if (SelectedId == id) return false;
        var previous = SelectedId;
        SelectedId = id;
        Changed?.Invoke(this, new SelectionChangedEventArgs(previous, id));
        return true;
    }

    public bool Clear() => Select(null);
}
