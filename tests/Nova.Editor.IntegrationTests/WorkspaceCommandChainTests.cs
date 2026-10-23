using Nova.Editor.Services;
using Nova.Editor.Services.Model;
using Nova.UI;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class WorkspaceCommandChainTests
{
    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(Path.GetTempPath(), "nova-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* test cleanup is best-effort */ }
    }

    [TestMethod]
    public void Edit_flows_through_command_history_and_state_projection()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;

        var result = workspace.TryEditProperty(player.Id, "position.x", 4.25d);
        Assert.IsTrue(result.Success, result.Message);
        Assert.AreEqual(4.25d, player.GetProperty("position.x"));
        Assert.AreEqual(1, workspace.History.UndoCount);
        Assert.AreEqual(4.25d, workspace.UiState.Get(player.Id, "property.position.x"));

        workspace.History.Undo();
        Assert.AreEqual(2.5d, player.GetProperty("position.x"));
        Assert.AreEqual(2.5d, workspace.UiState.Get(player.Id, "property.position.x"));

        workspace.History.Redo();
        Assert.AreEqual(4.25d, player.GetProperty("position.x"));
        Assert.AreEqual(4.25d, workspace.UiState.Get(player.Id, "property.position.x"));
    }

    [TestMethod]
    public void Invalid_property_is_rejected_without_touching_history_or_model()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;

        var result = workspace.TryEditProperty(player.Id, "position.x", "not-a-number");

        Assert.IsFalse(result.Success);
        Assert.AreEqual(2.5d, player.GetProperty("position.x"));
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.IsTrue(workspace.Log.Snapshot().Any(e => e.Message.Contains("expects a number")));
    }

    [TestMethod]
    public void No_op_property_submission_does_not_create_history_or_touch_projection()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(player.Id, "position.x", 4d);
        var revision = workspace.History.UndoCount;

        var result = workspace.TryEditProperty(player.Id, "position.x", 4d);

        Assert.IsTrue(result.Success);
        Assert.AreEqual(revision, workspace.History.UndoCount);
        Assert.AreEqual(4d, player.GetProperty("position.x"));
    }

    [TestMethod]
    public void Stale_property_command_cannot_undo_after_node_removal()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(player.Id, "position.x", 8d);

        // Simulate an external scene edit/removal while history still holds the edit.
        workspace.Document.RemoveNode(player);

        Assert.IsFalse(workspace.History.CanUndo);
        Assert.ThrowsException<InvalidOperationException>(() => workspace.History.Undo());
        Assert.AreEqual(1, workspace.History.UndoCount);
        Assert.IsNull(workspace.Document.Find(player.Id));
    }

    [TestMethod]
    public void Command_from_another_workspace_is_rejected()
    {
        var workspaceA = EditorWorkspace.CreateSample(_layoutPath);
        var workspaceB = EditorWorkspace.CreateSample(Path.Combine(_directory, "layout-b.json"));
        var nodeB = workspaceB.Document.Find(new UiId("entity.player"))!;
        var foreign = new Nova.Editor.Services.Commands.EditPropertyCommand(
            workspaceB.Document, nodeB, "position.x", 99d);

        Assert.ThrowsException<InvalidOperationException>(() => workspaceA.History.Execute(foreign));
        Assert.AreEqual(2.5d, nodeB.GetProperty("position.x"));
        Assert.AreEqual(0, workspaceA.History.UndoCount);
    }

    [TestMethod]
    public void Projection_failure_does_not_split_model_and_history()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        void ThrowOnChange(object? sender, UiStateChangedEventArgs e)
            => throw new InvalidOperationException("projection subscriber failed");

        workspace.UiState.Changed += ThrowOnChange;
        try
        {
            var result = workspace.TryEditProperty(player.Id, "position.x", 6.5d);

            Assert.IsTrue(result.Success);
            Assert.AreEqual(6.5d, player.GetProperty("position.x"));
            Assert.AreEqual(1, workspace.History.UndoCount);
            Assert.IsTrue(workspace.History.CanUndo);
        }
        finally
        {
            workspace.UiState.Changed -= ThrowOnChange;
        }
    }

    [TestMethod]
    public void Numeric_property_is_clamped_by_schema_before_the_command()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;

        var result = workspace.TryEditProperty(player.Id, "scale.x", 99999d);

        Assert.IsTrue(result.Success, result.Message);
        Assert.AreEqual(1000d, player.GetProperty("scale.x"));
    }

    [TestMethod]
    public void Add_node_is_undoable_and_selection_recovers_after_undo()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var before = workspace.Document.AllNodes().Count();

        var result = workspace.Execute(NovaCommandIds.AddNode);
        Assert.IsTrue(result.Success, result.Message);
        Assert.AreEqual(before + 1, workspace.Document.AllNodes().Count());
        var addedId = workspace.Selection.SelectedId;
        Assert.IsNotNull(addedId);

        workspace.History.Undo();
        Assert.AreEqual(before, workspace.Document.AllNodes().Count());
        Assert.IsNull(workspace.Document.Find(addedId!.Value));
        Assert.AreEqual(workspace.Document.Root.Id, workspace.Selection.SelectedId);

        workspace.History.Redo();
        Assert.IsNotNull(workspace.Document.Find(addedId!.Value));
    }

    [TestMethod]
    public void Delete_is_undoable_and_restores_properties()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var enemy = workspace.Document.Find(new UiId("entity.enemy"))!;
        workspace.Selection.Select(enemy.Id);
        var property = enemy.GetProperty("sprite.asset");

        var result = workspace.Execute(NovaCommandIds.DeleteNode);
        Assert.IsTrue(result.Success, result.Message);
        Assert.IsNull(workspace.Document.Find(enemy.Id));

        workspace.History.Undo();
        var restored = workspace.Document.Find(enemy.Id);
        Assert.IsNotNull(restored);
        Assert.AreEqual(property, restored!.GetProperty("sprite.asset"));
    }

    [TestMethod]
    public void Selection_projection_uses_stable_ids()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;

        workspace.Selection.Select(player.Id);

        Assert.AreEqual("Player", workspace.UiState.Get(EditorUiIds.InspectorHeader, "text"));
        Assert.AreEqual("Entity", workspace.UiState.Get(EditorUiIds.InspectorType, "text"));
        Assert.AreEqual("Player · Entity", workspace.UiState.Get(EditorUiIds.SelectionStatus, "text"));
        Assert.AreEqual("6", workspace.UiState.Get(EditorUiIds.HierarchyCount, "text"));
    }

    [TestMethod]
    public void Layout_toggle_commands_change_only_layout_state()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var layoutEvents = 0;
        workspace.LayoutChanged += (_, _) => layoutEvents++;

        Assert.IsTrue(workspace.Execute(NovaCommandIds.ToggleHierarchy).Success);
        Assert.IsFalse(workspace.Layout.ShowHierarchy);
        Assert.IsTrue(workspace.Execute(NovaCommandIds.ToggleConsole).Success);
        Assert.IsFalse(workspace.Layout.ShowConsole);

        Assert.AreEqual(2, layoutEvents);
        Assert.IsTrue(workspace.Document.AllNodes().Any());
    }

    [TestMethod]
    public void Unknown_theme_id_falls_back_without_breaking_layout()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        workspace.ApplyLayout(workspace.Layout with { ThemeId = "missing.theme" });

        Assert.AreEqual("nova.dark", workspace.Theme.Current.Id);
        Assert.AreEqual("nova.dark", workspace.Layout.ThemeId);
    }

    [TestMethod]
    public void Panel_toggle_measures_and_preserves_splitter_width()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 333 });
        workspace.LayoutMeasurer = () => workspace.Layout with { HierarchyWidth = 333 };

        Assert.IsTrue(workspace.Execute(NovaCommandIds.ToggleHierarchy).Success);
        Assert.IsFalse(workspace.Layout.ShowHierarchy);

        Assert.IsTrue(workspace.Execute(NovaCommandIds.ToggleHierarchy).Success);
        Assert.IsTrue(workspace.Layout.ShowHierarchy);
        Assert.AreEqual(333, workspace.Layout.HierarchyWidth, 0.001);
    }

    [TestMethod]
    public void Theme_toggle_updates_theme_and_layout_without_touching_document()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var nodesBefore = workspace.Document.AllNodes().ToArray();

        Assert.IsTrue(workspace.Execute(NovaCommandIds.ToggleTheme).Success);

        Assert.AreEqual("nova.light", workspace.Theme.Current.Id);
        Assert.AreEqual("nova.light", workspace.Layout.ThemeId);
        CollectionAssert.AreEqual(nodesBefore, workspace.Document.AllNodes().ToArray());
    }
}
