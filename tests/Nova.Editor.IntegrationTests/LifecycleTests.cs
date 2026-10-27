using Nova.Editor.Services;
using Nova.Editor.Services.Commands;
using Nova.Editor.Services.Projects;
using Nova.UI;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class LifecycleTests
{
    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(Path.GetTempPath(), "nova-lifecycle-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    [TestMethod]
    public void Project_switch_invalidates_old_history_and_session()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var oldDocument = workspace.Document;
        var oldSession = workspace.SessionId;
        var oldProject = workspace.ProjectId;
        var player = oldDocument.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(player.Id, "position.x", 4d);

        var foreignCommand = new EditPropertyCommand(oldDocument, player, "position.x", 9d);
        var newProject = SampleProjectFactory.Create();

        var result = workspace.OpenProject(newProject, new UiId("scene.main"));

        Assert.IsTrue(result.Success, result.Message);
        Assert.AreNotEqual(oldProject, workspace.ProjectId);
        Assert.AreNotEqual(oldSession, workspace.SessionId);
        Assert.AreNotSame(oldDocument, workspace.Document);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.IsFalse(workspace.History.CanUndo);
        Assert.ThrowsException<InvalidOperationException>(
            () => workspace.History.Execute(foreignCommand));
        Assert.AreEqual(4d, player.GetProperty("position.x"));
    }

    [TestMethod]
    public void Scene_switch_keeps_project_and_layout_but_resets_session()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var projectId = workspace.ProjectId;
        var oldSession = workspace.SessionId;
        var oldScene = workspace.SceneId;
        var oldDocument = workspace.Document;
        var oldPlayer = oldDocument.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(oldPlayer.Id, "position.x", 5d);
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 333 });

        var oldCommand = new EditPropertyCommand(oldDocument, oldPlayer, "position.x", 8d);
        var result = workspace.OpenScene(new UiId("scene.arena"));

        Assert.IsTrue(result.Success, result.Message);
        Assert.AreEqual(projectId, workspace.ProjectId);
        Assert.AreNotEqual(oldScene, workspace.SceneId);
        Assert.AreNotEqual(oldSession, workspace.SessionId);
        Assert.AreNotSame(oldDocument, workspace.Document);
        Assert.AreEqual(333, workspace.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.ThrowsException<InvalidOperationException>(
            () => workspace.History.Execute(oldCommand));
    }

    [TestMethod]
    public void Scene_load_failure_preserves_the_current_session()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        workspace.TryEditProperty(player.Id, "position.x", 7d);
        var document = workspace.Document;
        var project = workspace.ProjectId;
        var scene = workspace.SceneId;
        var session = workspace.SessionId;
        var historyCount = workspace.History.UndoCount;

        var broken = new EditorProject("Broken", [
            new SceneDescriptor(
                new UiId("scene.broken"),
                "Broken",
                _ => throw new InvalidOperationException("scene factory failed")),
        ]);

        var result = workspace.OpenProject(broken);

        Assert.IsFalse(result.Success);
        Assert.IsTrue(workspace.IsOpen);
        Assert.AreSame(document, workspace.Document);
        Assert.AreEqual(project, workspace.ProjectId);
        Assert.AreEqual(scene, workspace.SceneId);
        Assert.AreEqual(session, workspace.SessionId);
        Assert.AreEqual(historyCount, workspace.History.UndoCount);
        Assert.AreEqual(player.Id, workspace.Selection.SelectedId);
        Assert.AreEqual(7d, player.GetProperty("position.x"));
    }

    [TestMethod]
    public void Cancelled_close_preserves_document_history_and_selection()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        workspace.TryEditProperty(player.Id, "position.x", 3d);
        var document = workspace.Document;
        var session = workspace.SessionId;
        var history = workspace.History.UndoCount;

        var result = workspace.CloseProject(confirm: () => false, saveLayout: false);

        Assert.AreEqual(LifecycleOutcome.Cancelled, result.Outcome);
        Assert.IsTrue(workspace.IsOpen);
        Assert.AreSame(document, workspace.Document);
        Assert.AreEqual(session, workspace.SessionId);
        Assert.AreEqual(history, workspace.History.UndoCount);
        Assert.AreEqual(player.Id, workspace.Selection.SelectedId);
    }

    [TestMethod]
    public void Close_invalidates_commands_and_clears_projection()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        workspace.TryEditProperty(player.Id, "position.x", 3.5d);
        var oldSession = workspace.SessionId;
        var oldDocument = workspace.Document;
        var oldCommand = new EditPropertyCommand(oldDocument, player, "position.x", 6d);
        SessionChangedEventArgs? changed = null;
        workspace.SessionChanged += (_, args) => changed = args;

        var result = workspace.CloseProject(saveLayout: false);

        Assert.IsTrue(result.Success);
        Assert.IsFalse(workspace.IsOpen);
        Assert.AreNotEqual(oldSession, workspace.SessionId);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.IsNull(workspace.Selection.SelectedId);
        Assert.IsFalse(workspace.TryEditProperty(player.Id, "position.x", 9d).Success);
        Assert.IsNotNull(changed);
        Assert.IsFalse(changed.IsOpen);
        Assert.ThrowsException<InvalidOperationException>(
            () => workspace.History.Execute(oldCommand));
    }

    [TestMethod]
    public void Reopen_creates_a_new_session_without_reviving_old_commands()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var oldSession = workspace.SessionId;
        var oldDocument = workspace.Document;
        var oldNode = oldDocument.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(oldNode.Id, "position.x", 4.5d);
        var oldCommand = new EditPropertyCommand(oldDocument, oldNode, "position.x", 7d);

        workspace.CloseProject(saveLayout: false);
        var result = workspace.OpenProject(workspace.Project, workspace.SceneId);

        Assert.IsTrue(result.Success, result.Message);
        Assert.IsTrue(workspace.IsOpen);
        Assert.AreNotEqual(oldSession, workspace.SessionId);
        Assert.AreNotSame(oldDocument, workspace.Document);
        Assert.AreEqual(0, workspace.History.UndoCount);
        Assert.ThrowsException<InvalidOperationException>(
            () => workspace.History.Execute(oldCommand));
    }

    [TestMethod]
    public void Project_default_layout_is_applied_on_project_open()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var customLayout = WorkspaceLayout.Default with
        {
            HierarchyWidth = 321,
            ThemeId = "nova.light",
        };
        var project = new EditorProject("Layout Project", [
            new SceneDescriptor(
                new UiId("scene.layout"),
                "Layout Scene",
                contextId => Nova.Editor.Services.Model.SampleSceneFactory.Create(contextId)),
        ], customLayout);

        var result = workspace.OpenProject(project, new UiId("scene.layout"));

        Assert.IsTrue(result.Success, result.Message);
        Assert.AreEqual(321, workspace.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual("nova.light", workspace.Theme.Current.Id);
    }

    [TestMethod]
    public void External_selected_node_delete_does_not_write_or_keep_selection()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.Selection.Select(player.Id);
        workspace.TryEditProperty(player.Id, "position.x", 4d);
        var history = workspace.History.UndoCount;

        workspace.Document.RemoveNode(player);
        var result = workspace.TryEditProperty(player.Id, "position.x", 8d);

        Assert.IsFalse(result.Success);
        Assert.IsNull(workspace.Selection.SelectedId);
        Assert.AreEqual(history, workspace.History.UndoCount);
        Assert.AreEqual(4d, player.GetProperty("position.x"));
    }

    [TestMethod]
    public void Repeated_scene_switches_do_not_duplicate_session_events()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var events = 0;
        workspace.SessionChanged += (_, _) => events++;

        for (var i = 0; i < 20; i++)
        {
            Assert.IsTrue(workspace.OpenScene(new UiId("scene.arena")).Success);
            Assert.IsTrue(workspace.OpenScene(new UiId("scene.main")).Success);
        }

        Assert.AreEqual(40, events);
    }

    [TestMethod]
    public void Repeated_close_and_reopen_cycles_do_not_duplicate_events()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var project = workspace.Project;
        var scene = workspace.SceneId;
        var events = 0;
        workspace.SessionChanged += (_, _) => events++;

        for (var i = 0; i < 10; i++)
        {
            var closed = workspace.CloseProject(saveLayout: false);

            Assert.AreEqual(LifecycleOutcome.Success, closed.Outcome);
            Assert.IsFalse(workspace.IsOpen);
            Assert.IsNull(workspace.Selection.SelectedId);
            Assert.AreEqual(0, workspace.History.UndoCount);

            var opened = workspace.OpenProject(project, scene);

            Assert.IsTrue(opened.Success, opened.Message);
            Assert.IsTrue(workspace.IsOpen);
            Assert.IsNotNull(workspace.Selection.SelectedId);
            Assert.AreEqual(0, workspace.History.UndoCount);
        }

        Assert.AreEqual(20, events);
    }

    [TestMethod]
    public async Task Reopened_workspace_restores_layout_but_not_old_history()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 307, ConsoleHeight = 201 });
        Assert.IsTrue((await workspace.SaveLayoutAsync()).Success);
        var player = workspace.Document.Find(new UiId("entity.player"))!;
        workspace.TryEditProperty(player.Id, "position.x", 4d);

        var reopened = EditorWorkspace.CreateSample(_layoutPath);
        var loaded = await reopened.InitializeLayoutAsync();

        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.AreEqual(307, reopened.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(201, reopened.Layout.ConsoleHeight, 0.001);
        Assert.AreEqual(0, reopened.History.UndoCount);
        Assert.IsTrue(reopened.Selection.SelectedId.HasValue);
        Assert.IsNull(reopened.UiState.Get(new UiId("entity.player"), "property.position.x"));
    }
}
