using Avalonia.Controls;
using Nova.UI;
using Nova.UI.Avalonia;
using Nova.UI.Core.State;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class StateBindingTests
{
    [TestMethod]
    public void Binding_updates_only_the_matching_state_slot()
    {
        var store = new InMemoryUiStateStore();
        var id = new UiId("inspector.header");
        var other = new UiId("status.selection");
        var text = new TextBlock();

        using var binding = NovaUiBinding.Bind(text, TextBlock.TextProperty, store, id, "text");
        store.Set(id, "text", "Player");
        Assert.AreEqual("Player", text.Text);

        store.Set(other, "text", "Enemy");
        Assert.AreEqual("Player", text.Text);

        store.Set(id, "text", "Camera");
        Assert.AreEqual("Camera", text.Text);

        store.Clear();
        Assert.IsNull(text.Text);
    }
}
