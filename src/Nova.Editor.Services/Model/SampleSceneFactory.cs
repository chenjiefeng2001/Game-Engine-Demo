using Nova.UI;

namespace Nova.Editor.Services.Model;

/// <summary>Deterministic offline sample used by the shell, gates, and playground.</summary>
public static class SampleSceneFactory
{
    public static SceneDocument Create()
    {
        var root = SceneNodeSchema.CreateNode(new UiId("scene.root"), "Sample Scene", "Scene");

        var arena = SceneNodeSchema.CreateNode(new UiId("entity.arena"), "Arena", "Entity");
        var player = SceneNodeSchema.CreateNode(new UiId("entity.player"), "Player", "Entity");
        var enemy = SceneNodeSchema.CreateNode(new UiId("entity.enemy"), "Enemy", "Entity");
        var light = SceneNodeSchema.CreateNode(new UiId("entity.key-light"), "Key Light", "Light");
        var camera = SceneNodeSchema.CreateNode(new UiId("entity.main-camera"), "Main Camera", "Camera");

        Apply(arena,
            ("position.x", 0d), ("position.y", 0d), ("position.z", 0d),
            ("sprite.asset", "sprites/arena.png"), ("sprite.layer", "Background"));
        Apply(player,
            ("position.x", 2.5d), ("position.y", 0d), ("position.z", 0d),
            ("sprite.asset", "sprites/player.png"));
        Apply(enemy,
            ("position.x", -3d), ("position.y", 1d), ("position.z", 0d),
            ("sprite.asset", "sprites/enemy.png"));
        Apply(light,
            ("position.x", 4d), ("position.y", 6d), ("position.z", 2d),
            ("light.intensity", 2.5d), ("light.color", "#FFE6B8FF"));
        Apply(camera,
            ("position.x", 0d), ("position.y", 0d), ("position.z", -10d),
            ("camera.fov", 60d));

        root.AddChild(arena);
        root.AddChild(player);
        root.AddChild(enemy);
        root.AddChild(light);
        root.AddChild(camera);

        return new SceneDocument(root);
    }

    private static void Apply(SceneNode node, params (string Key, object? Value)[] values)
    {
        foreach (var (key, value) in values)
            node.SetProperty(key, value);
    }
}
