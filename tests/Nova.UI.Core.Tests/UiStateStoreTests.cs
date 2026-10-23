using Nova.UI.Core.State;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class UiStateStoreTests
{
    [TestMethod]
    public void Setting_same_value_does_not_raise_or_advance_revision()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("inspector.header");
        var raised = 0;
        store.Changed += (_, _) => raised++;

        Assert.IsTrue(store.Set(id, "text", "Player"));
        Assert.IsFalse(store.Set(id, "text", "Player"));

        Assert.AreEqual(1, raised);
        Assert.AreEqual(1, store.Revision);
        Assert.IsTrue(store.TryGet<string>(id, "text", out var value));
        Assert.AreEqual("Player", value);
    }

    [TestMethod]
    public void Changes_can_be_read_incrementally_by_revision()
    {
        var store = new InMemoryUiStateStore();
        var first = new UiId("hierarchy.count");
        var second = new UiId("status.selection");

        store.Set(first, "count", 1);
        var mark = store.Revision;
        store.Set(second, "text", "Player");

        var changes = store.GetChanges(mark);
        Assert.AreEqual(1, changes.Count);
        Assert.AreEqual(second, changes[0].Id);
        Assert.AreEqual("Player", changes[0].NewValue);
    }

    [TestMethod]
    public void Store_is_safe_under_parallel_writers()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("node.parallel");

        Parallel.For(0, 2000, i => store.Set(id, $"key.{i % 32}", i));

        Assert.IsTrue(store.Revision > 0);
        Assert.IsTrue(store.TryGet<int>(id, "key.0", out _));
    }

    [TestMethod]
    public void Removing_a_key_publishes_null_new_value()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("node.remove");
        store.Set(id, "value", 42);

        UiStateChange? seen = null;
        store.Changed += (_, e) => seen = e.Changes[0];
        Assert.IsTrue(store.Remove(id, "value"));

        Assert.IsNotNull(seen);
        Assert.AreEqual(42, seen.Value.OldValue);
        Assert.IsNull(seen.Value.NewValue);
    }
}
