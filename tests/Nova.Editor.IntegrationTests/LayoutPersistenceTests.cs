using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.UI;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class LayoutPersistenceTests
{
    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(Path.GetTempPath(), "nova-layout-tests", Guid.NewGuid().ToString("N"));
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
    public async Task Saved_layout_round_trips_through_disk()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var layout = workspace.Layout with
        {
            WindowWidth = 1500,
            WindowHeight = 950,
            HierarchyWidth = 320,
            InspectorWidth = 380,
            ConsoleHeight = 210,
            ShowInspector = false,
            ThemeId = "nova.light",
        };

        workspace.ApplyLayout(layout);
        var save = await workspace.SaveLayoutAsync();
        Assert.IsTrue(save.Success, save.Error);

        var loaded = await workspace.InitializeLayoutAsync();
        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.AreEqual(320, loaded.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(380, loaded.Layout.InspectorWidth, 0.001);
        Assert.AreEqual(210, loaded.Layout.ConsoleHeight, 0.001);
        Assert.IsFalse(loaded.Layout.ShowInspector);
        Assert.AreEqual("nova.light", workspace.Theme.Current.Id);
    }

    [TestMethod]
    public async Task Missing_layout_uses_defaults_and_does_not_throw()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        StringAssert.Contains(loaded.Reason, "does not exist");
        Assert.AreEqual(WorkspaceLayout.Default.HierarchyWidth, workspace.Layout.HierarchyWidth, 0.001);
    }

    [TestMethod]
    public async Task Missing_layout_is_created_by_the_first_save()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        await workspace.InitializeLayoutAsync();
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 275 });

        var saved = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(275, parsed.Layout.HierarchyWidth, 0.001);
    }

    [TestMethod]
    public async Task Save_command_failure_is_logged_and_preserves_the_previous_layout()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("Cross-process share locks are only enforced on Windows.");

        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 280 });
        Assert.IsTrue((await workspace.SaveLayoutAsync()).Success);
        var before = await File.ReadAllTextAsync(_layoutPath);

        using (var lockStream = new FileStream(
                   _layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 340 });
            Assert.IsTrue(workspace.Execute(NovaCommandIds.SaveLayout).Success);

            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            while (DateTime.UtcNow < deadline
                   && !workspace.Log.Snapshot().Any(
                       entry => entry.Message.Contains(
                           "Layout save failed",
                           StringComparison.Ordinal)))
            {
                await Task.Delay(20);
            }

            Assert.IsTrue(
                workspace.Log.Snapshot().Any(
                    entry => entry.Message.Contains(
                        "Layout save failed",
                        StringComparison.Ordinal)),
                "A failed save through the command path must be logged.");
        }

        Assert.AreEqual(before, await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "*.tmp").Length);
    }

    [TestMethod]
    public async Task Corrupt_layout_falls_back_and_is_quarantined()
    {
        await File.WriteAllTextAsync(_layoutPath, "{ broken json");
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        Assert.AreEqual(WorkspaceLayout.Default, loaded.Layout);
        Assert.IsTrue(Directory.GetFiles(_directory, "layout.json.corrupt-*").Length == 1);
        Assert.IsTrue(workspace.Log.Snapshot().Any(e => e.Message.Contains("Layout fallback")));
    }

    [TestMethod]
    public async Task Failed_atomic_replace_keeps_the_previous_layout_and_cleans_temp()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("Cross-process share locks are only enforced on Windows.");

        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 280 });
        var initial = await workspace.SaveLayoutAsync();
        Assert.IsTrue(initial.Success, initial.Error);
        var before = await File.ReadAllTextAsync(_layoutPath);

        using (var lockStream = new FileStream(
                   _layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 340 });
            var result = await workspace.SaveLayoutAsync();
            Assert.IsFalse(result.Success);
        }

        Assert.AreEqual(before, await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "*.tmp").Length);
    }

    [TestMethod]
    public async Task Unknown_schema_is_quarantined_instead_of_silently_migrated()
    {
        var future = WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default)
            .Replace("\"schemaVersion\": 3", "\"schemaVersion\": 99");
        await File.WriteAllTextAsync(_layoutPath, future);
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        StringAssert.Contains(loaded.Reason, "schema");
        Assert.IsFalse(File.Exists(_layoutPath));
        Assert.AreEqual(1, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);
    }

    [TestMethod]
    public async Task Concurrent_saves_are_serialized_and_leave_no_temp_files()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var tasks = Enumerable.Range(0, 8).Select(i =>
            workspace.SaveLayoutAsync().ContinueWith(_ => i));
        await Task.WhenAll(tasks);

        Assert.IsTrue(File.Exists(_layoutPath));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "*.tmp").Length);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.IsFalse(parsed.UsedFallback);
    }
}
