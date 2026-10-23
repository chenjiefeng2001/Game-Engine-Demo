using Nova.UI;

namespace Nova.Editor.Services.Viewport;

/// <summary>v0.2 viewport boundary. It has no graphics dependency by design.</summary>
public sealed class PlaceholderViewportHost : IEngineViewportHost
{
    public PlaceholderViewportHost(UiId id)
    {
        if (id.IsEmpty) throw new ArgumentException("Viewport id is required.", nameof(id));
        Id = id;
    }

    public UiId Id { get; }
    public string BackendName => "placeholder";
    public bool IsAvailable => false;

    public event EventHandler? AvailabilityChanged;

    public void SetAvailable(bool available)
    {
        _ = available;
        AvailabilityChanged?.Invoke(this, EventArgs.Empty);
    }
}
