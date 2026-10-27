using Nova.Editor.Services;
using Nova.Editor.Services.Projects;
using Nova.Editor.Views.ViewModels;
using Nova.UI;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class ShellViewModelTests
{
    [TestMethod]
    public void Initial_selection_populates_hierarchy_and_inspector_without_rebuilding_on_edit()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);

        Assert.IsNotNull(vm.SelectedNode);
        Assert.AreEqual("entity.arena", vm.SelectedNode!.Id.Value);
        Assert.IsTrue(vm.Properties.Count > 0);
        var rowCount = vm.Properties.Count;

        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);

        Assert.AreEqual("Player", vm.SelectedNode!.Name);
        Assert.AreEqual(rowCount, vm.Properties.Count);
        Assert.IsTrue(vm.Properties.Any(p => p.Key == "position.x"));
    }

    [TestMethod]
    public void Inspector_row_commit_and_undo_go_through_command_history()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();

        var positionX = vm.Properties.Single(row => row.Key == "position.x");
        positionX.NumberValue = 9.5;

        Assert.AreEqual(9.5d, player.GetProperty("position.x"));
        Assert.AreEqual(1, workspace.History.UndoCount);

        // ViewModel refreshes Inspector rows from History.Undone; no manual rebuild.
        workspace.History.Undo();
        Assert.AreEqual(2.5d, player.GetProperty("position.x"));
        Assert.AreEqual(2.5d, positionX.NumberValue);
    }

    [TestMethod]
    public void Uncommitted_text_draft_is_discarded_on_selection_and_session_changes()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();
        var asset = vm.Properties.Single(row => row.Key == "sprite.asset");
        asset.TextValue = "sprites/draft.png";

        // Selection change discards the row/draft; nothing is committed.
        workspace.Selection.Select(new UiId("entity.enemy"));
        Assert.AreEqual("sprites/player.png", player.GetProperty("sprite.asset"));
        Assert.AreEqual(0, workspace.History.UndoCount);

        // Scene switch also discards any uncommitted draft on the old scene.
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();
        asset = vm.Properties.Single(row => row.Key == "sprite.asset");
        asset.TextValue = "sprites/scene-draft.png";
        Assert.IsTrue(workspace.OpenScene(new UiId("scene.arena")).Success);
        Assert.AreEqual("sprites/player.png", player.GetProperty("sprite.asset"));
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.IsTrue(vm.Properties.Any(row => row.Key == "sprite.asset"));

        // Project switch follows the same discard-on-session-change policy.
        vm.Properties.Single(row => row.Key == "sprite.asset").TextValue = "sprites/project-draft.png";
        Assert.IsTrue(workspace.OpenProject(SampleProjectFactory.Create(), new UiId("scene.main")).Success);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.AreEqual("Main Scene", workspace.Project.FindScene(workspace.SceneId)?.Name);
    }

    [TestMethod]
    public void Cancelled_close_keeps_inspector_draft_selection_and_history()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();
        var asset = vm.Properties.Single(row => row.Key == "sprite.asset");
        asset.TextValue = "sprites/cancel-close.png";

        var result = workspace.CloseProject(confirm: () => false, saveLayout: false);

        Assert.AreEqual(LifecycleOutcome.Cancelled, result.Outcome);
        Assert.AreSame(player, vm.SelectedNode?.Node);
        Assert.AreEqual("sprites/cancel-close.png", asset.TextValue);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.AreEqual("sprites/player.png", player.GetProperty("sprite.asset"));
    }

    [TestMethod]
    public void External_selection_delete_clears_inspector_projection()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();
        Assert.IsNotNull(vm.SelectedNode);

        workspace.Document.RemoveNode(player);
        var result = workspace.TryEditProperty(player.Id, "position.x", 9d);

        Assert.IsFalse(result.Success);
        Assert.IsNull(vm.SelectedNode);
        Assert.AreEqual(0, vm.Properties.Count);
        Assert.AreEqual(0, workspace.History.UndoCount);
    }

    [TestMethod]
    public void Shell_scene_and_close_commands_update_the_projection()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);

        vm.OpenArenaSceneCommand.Execute(null);

        Assert.AreEqual(new UiId("scene.arena"), workspace.SceneId);
        Assert.IsNotNull(vm.SelectedNode);
        StringAssert.Contains(vm.SceneLabel, "Arena Only");

        vm.CloseProjectCommand.Execute(null);

        Assert.IsFalse(workspace.IsOpen);
        Assert.IsNull(vm.SelectedNode);
        Assert.AreEqual(0, vm.Roots.Count);
        Assert.AreEqual(0, vm.Properties.Count);
        Assert.AreEqual("No project open", vm.SceneLabel);
    }

    [TestMethod]
    public void Text_row_commits_only_on_explicit_commit_signal()
    {
        var workspace = EditorWorkspace.CreateSample(Path.Combine(Path.GetTempPath(), $"nova-vm-{Guid.NewGuid():N}.json"));
        var vm = new EditorShellViewModel(workspace);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        vm.RefreshInspectorValues();

        var asset = vm.Properties.Single(row => row.Key == "sprite.asset");
        asset.TextValue = "sprites/player_alt.png";
        Assert.AreEqual("sprites/player.png", player.GetProperty("sprite.asset"));

        // The real TextCommitted event passes the TextBox text; the VM must not
        // depend on a TwoWay binding having already updated TextValue.
        asset.CommitText("sprites/player_alt_v2.png");
        Assert.AreEqual("sprites/player_alt_v2.png", player.GetProperty("sprite.asset"));
        Assert.AreEqual(1, workspace.History.UndoCount);
    }
}
