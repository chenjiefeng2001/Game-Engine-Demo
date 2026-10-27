using Nova.UI;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Services.Projects;

/// <summary>
/// A scene factory. The context/session token is supplied by the workspace so a
/// scene document can never inherit another session's command context.
/// </summary>
public sealed record SceneDescriptor(
    UiId Id,
    string Name,
    Func<Guid, Model.SceneDocument> CreateDocument);

/// <summary>
/// In-memory project descriptor for Lifecycle v0.1. It intentionally has no
/// filesystem/asset pipeline dependency yet; opening remains deterministic and
/// can be failure-injected by tests.
/// </summary>
public sealed class EditorProject
{
    private readonly List<SceneDescriptor> _scenes;

    public EditorProject(
        string name,
        IEnumerable<SceneDescriptor> scenes,
        WorkspaceLayout? defaultLayout = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ProjectId = Guid.NewGuid();
        Name = name;
        _scenes = scenes?.ToList() ?? throw new ArgumentNullException(nameof(scenes));
        if (_scenes.Count == 0)
            throw new ArgumentException("A project requires at least one scene.", nameof(scenes));
        if (_scenes.Select(scene => scene.Id).Distinct().Count() != _scenes.Count)
            throw new ArgumentException("Scene ids must be unique within a project.", nameof(scenes));
        DefaultLayout = defaultLayout;
    }

    public Guid ProjectId { get; }
    public string Name { get; }
    public WorkspaceLayout? DefaultLayout { get; }
    public IReadOnlyList<SceneDescriptor> Scenes => _scenes;

    public SceneDescriptor? FindScene(UiId id)
        => _scenes.FirstOrDefault(scene => scene.Id == id);
}
