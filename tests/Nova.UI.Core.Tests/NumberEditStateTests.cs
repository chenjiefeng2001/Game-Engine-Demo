using Nova.UI.Core.State;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class NumberEditStateTests
{
    [TestMethod]
    public void Intermediate_text_is_preserved_until_commit()
    {
        var edit = new NumberEditState();
        edit.SetValue(1);
        edit.SetText("1.");

        Assert.AreEqual("1.", edit.Text);
        Assert.IsTrue(edit.HasValidValue);
        Assert.IsTrue(edit.IsDirty);

        Assert.IsTrue(edit.TryCommit(null, null, out var value));
        Assert.AreEqual(1d, value);
        Assert.AreEqual("1", edit.Text);
        Assert.IsFalse(edit.IsDirty);
    }

    [TestMethod]
    public void Invalid_text_does_not_commit_and_is_preserved()
    {
        var edit = new NumberEditState();
        edit.SetValue(5);
        edit.SetText("not a number");

        Assert.IsFalse(edit.HasValidValue);
        Assert.IsFalse(edit.TryCommit(null, null, out _));
        Assert.AreEqual("not a number", edit.Text);

        edit.Revert();
        Assert.AreEqual("5", edit.Text);
        Assert.AreEqual(5d, edit.Value);
    }

    [TestMethod]
    public void Commit_clamps_to_range()
    {
        var edit = new NumberEditState();
        edit.SetText("250");

        Assert.IsTrue(edit.TryCommit(0, 100, out var value));
        Assert.AreEqual(100d, value);
        Assert.AreEqual("100", edit.Text);
    }

    [TestMethod]
    public void Step_clamps_and_rejects_non_finite()
    {
        var edit = new NumberEditState();
        edit.SetValue(1);

        Assert.IsTrue(edit.TryStep(5, 0, 3, out var stepped));
        Assert.AreEqual(3d, stepped);

        Assert.IsFalse(edit.TryStep(double.PositiveInfinity, 0, 10, out _));
    }
}
