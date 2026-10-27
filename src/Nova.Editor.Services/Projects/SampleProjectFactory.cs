using Nova.UI;
using Nova.UI.Core.Layout;

namespace Nova.Editor.Services.Projects;

public static class SampleProjectFactory
{
    public static EditorProject Create()
        => new(
            "Sample Project",
            [
                new SceneDescriptor(
                    new UiId("scene.main"),
                    "Main Scene",
                    contextId => Model.SampleSceneFactory.Create(contextId)),
                new SceneDescriptor(
                    new UiId("scene.arena"),
                    "Arena Only",
                    contextId => Model.SampleSceneFactory.CreateArena(contextId)),
            ],
            WorkspaceLayout.Default);
}
