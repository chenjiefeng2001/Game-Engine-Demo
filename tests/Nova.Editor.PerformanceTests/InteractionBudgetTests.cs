using System.Diagnostics;
using Nova.Editor.Services.Commands;
using Nova.Editor.Services.Model;
using Nova.UI.Core.Layout;
using Nova.UI.Core.State;
using Nova.UI;

namespace Nova.Editor.PerformanceTests;

/// <summary>
/// Coarse CI smoke budgets. They are not a substitute for the documented
/// benchmark on target hardware; they catch catastrophic regressions only.
/// </summary>
[TestClass]
[TestCategory("Performance")]
public sealed class InteractionBudgetTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public void Local_state_update_p95_is_under_50ms()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("perf.node");
        var samples = new List<double>(2000);

        for (var i = 0; i < 2000; i++)
        {
            var sw = Stopwatch.StartNew();
            store.Set(id, "value", i);
            sw.Stop();
            samples.Add(sw.Elapsed.TotalMilliseconds);
        }

        var p95 = Percentile(samples, 0.95);
        TestContext.WriteLine($"UiStateStore.Set p95 = {p95:F3} ms, revision={store.Revision}");
        Assert.IsTrue(p95 < 50, $"p95 {p95:F3} ms exceeded 50 ms budget");
    }

    [TestMethod]
    public void Edit_command_on_10k_nodes_p95_is_under_50ms()
    {
        var root = SceneNodeSchema.CreateNode(new UiId("scene.root"), "Root", "Scene");
        var document = new SceneDocument(root);
        SceneNode? target = null;

        for (var i = 0; i < 10_000; i++)
        {
            var node = SceneNodeSchema.CreateNode(new UiId($"entity.{i}"), $"Entity {i}", "Entity");
            document.AddNode(root, node);
            if (i == 9_999) target = node;
        }

        Assert.IsNotNull(target);
        var samples = new List<double>(500);

        for (var i = 0; i < 500; i++)
        {
            var sw = Stopwatch.StartNew();
            var command = new EditPropertyCommand(
                document, target, "position.x", (double)i);
            command.Execute();
            command.Undo();
            sw.Stop();
            samples.Add(sw.Elapsed.TotalMilliseconds);
        }

        var p95 = Percentile(samples, 0.95);
        TestContext.WriteLine($"EditPropertyCommand p95 = {p95:F3} ms over 10k nodes");
        Assert.IsTrue(p95 < 50, $"p95 {p95:F3} ms exceeded 50 ms budget");
    }

    [TestMethod]
    public void State_updates_allocate_a_bounded_amount_for_10k_writes()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("perf.alloc");
        var before = GC.GetAllocatedBytesForCurrentThread();

        for (var i = 0; i < 10_000; i++)
            store.Set(id, "value", i);

        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        TestContext.WriteLine($"10k UiStateStore writes allocated {allocated / 1024d:F1} KiB");
        Assert.IsTrue(allocated < 10 * 1024 * 1024,
            $"10k writes allocated {allocated / 1024d:F1} KiB");
    }

    [TestMethod]
    public void Layout_serialization_p95_is_under_50ms()
    {
        var layout = WorkspaceLayout.Default with { HierarchyWidth = 280, ConsoleHeight = 210 };
        var samples = new List<double>(200);

        for (var i = 0; i < 200; i++)
        {
            var sw = Stopwatch.StartNew();
            var json = WorkspaceLayoutSerializer.Serialize(layout);
            _ = WorkspaceLayoutSerializer.Deserialize(json);
            sw.Stop();
            samples.Add(sw.Elapsed.TotalMilliseconds);
        }

        var p95 = Percentile(samples, 0.95);
        TestContext.WriteLine($"Layout serialize+parse p95 = {p95:F3} ms");
        Assert.IsTrue(p95 < 50, $"p95 {p95:F3} ms exceeded 50 ms budget");
    }

    [TestMethod]
    public void Scene_lookup_is_indexed_not_linear()
    {
        var root = SceneNodeSchema.CreateNode(new UiId("scene.root"), "Root", "Scene");
        var document = new SceneDocument(root);
        for (var i = 0; i < 10_000; i++)
            document.AddNode(root, SceneNodeSchema.CreateNode(new UiId($"entity.{i}"), $"E{i}", "Entity"));

        var sw = Stopwatch.StartNew();
        for (var i = 0; i < 10_000; i++)
            Assert.IsNotNull(document.Find(new UiId($"entity.{i}")));
        sw.Stop();

        var total = sw.Elapsed.TotalMilliseconds;
        TestContext.WriteLine($"10k indexed lookups = {total:F3} ms");
        Assert.IsTrue(total < 500, $"indexed lookup budget exceeded: {total:F3} ms");
    }

    private static double Percentile(List<double> samples, double percentile)
    {
        samples.Sort();
        var index = (int)Math.Ceiling(percentile * samples.Count) - 1;
        return samples[Math.Clamp(index, 0, samples.Count - 1)];
    }
}
