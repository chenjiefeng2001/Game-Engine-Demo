namespace Nova.UI;

/// <summary>
/// Boundary between the UI shell and a concrete graphics backend. UI Core and
/// UI Controls must never reference Vulkan/D3D/OpenGL types. v0.2 ships a
/// placeholder implementation; v0.5 replaces it with the real engine host.
/// </summary>
public interface IEngineViewportHost
{
    UiId Id { get; }
    string BackendName { get; }
    bool IsAvailable { get; }

    event EventHandler? AvailabilityChanged;
}
