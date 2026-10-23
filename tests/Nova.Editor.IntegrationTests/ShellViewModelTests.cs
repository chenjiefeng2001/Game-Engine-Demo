using Nova.Editor.Services;
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
