using Nova.UI;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class UiNodeTests
{
    [TestMethod]
    public void Nodes_are_found_by_stable_id_not_by_position()
    {
        var player = new UiNode(new UiId("entity.player"), "node", "Player");
        var enemy = new UiNode(new UiId("entity.enemy"), "node", "Enemy");
        var root = new UiNode(new UiId("scene.root"), "root", children: [player, enemy]);

        Assert.AreEqual(enemy, root.Find(new UiId("entity.enemy")));
        Assert.AreEqual(2, root.Descendants().Count());
    }

    [TestMethod]
    public void Empty_id_is_rejected()
    {
        Assert.ThrowsException<ArgumentException>(() => new UiNode(default, "node"));
        Assert.IsFalse(UiId.TryParse(null, out var empty));
        Assert.IsTrue(empty.IsEmpty);
    }
}
