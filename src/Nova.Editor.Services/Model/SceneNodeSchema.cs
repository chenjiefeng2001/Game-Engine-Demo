using Nova.UI;

namespace Nova.Editor.Services.Model;

/// <summary>Explicit property schema per node type. Kept pure so command tests can use it.</summary>
public static class SceneNodeSchema
{
    private static readonly PropertyDefinition[] Transform =
    [
        new("position.x", "Position X", PropertyEditorKind.Number, 0d, -10000, 10000, 0.1, "0.###"),
        new("position.y", "Position Y", PropertyEditorKind.Number, 0d, -10000, 10000, 0.1, "0.###"),
        new("position.z", "Position Z", PropertyEditorKind.Number, 0d, -10000, 10000, 0.1, "0.###"),
        new("rotation.z", "Rotation Z", PropertyEditorKind.Number, 0d, -360, 360, 1, "0.###"),
        new("scale.x", "Scale X", PropertyEditorKind.Number, 1d, 0.001, 1000, 0.1, "0.###"),
        new("scale.y", "Scale Y", PropertyEditorKind.Number, 1d, 0.001, 1000, 0.1, "0.###"),
    ];

    private static readonly PropertyDefinition[] Sprite =
    [
        new("sprite.visible", "Visible", PropertyEditorKind.Boolean, true),
        new("sprite.asset", "Asset", PropertyEditorKind.Text, "sprites/placeholder.png"),
        new("sprite.layer", "Layer", PropertyEditorKind.Choice, "Default",
            Options: ["Background", "Default", "Foreground", "UI"]),
    ];

    private static readonly PropertyDefinition[] Camera =
    [
        new("camera.fov", "Field of view", PropertyEditorKind.Number, 60d, 10, 120, 1, "0.#"),
        new("camera.near", "Near plane", PropertyEditorKind.Number, 0.1, 0.001, 100, 0.01, "0.###"),
        new("camera.far", "Far plane", PropertyEditorKind.Number, 1000d, 1, 100000, 10, "0.#"),
    ];

    private static readonly PropertyDefinition[] Light =
    [
        new("light.intensity", "Intensity", PropertyEditorKind.Number, 1d, 0, 100, 0.05, "0.##"),
        new("light.color", "Color", PropertyEditorKind.Text, "#FFFFFFFF"),
    ];

    public static IReadOnlyList<PropertyDefinition> For(string type) => type switch
    {
        "Entity" => [.. Transform, .. Sprite],
        "Camera" => [.. Transform, .. Camera],
        "Light" => [.. Transform, .. Light],
        _ => [],
    };

    public static PropertyDefinition? Find(string type, string key)
        => For(type).FirstOrDefault(property => string.Equals(property.Key, key, StringComparison.Ordinal));

    public static SceneNode CreateNode(UiId id, string name, string type)
    {
        var node = new SceneNode(id, name, type);
        foreach (var property in For(type))
            node.SetProperty(property.Key, property.DefaultValue);

        return node;
    }
}
