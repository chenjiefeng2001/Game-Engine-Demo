using Nova.UI.Avalonia;
using Nova.UI.Controls;
using Nova.UI.Core.Tokens;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class ControlContractTests
{
    [TestMethod]
    public void Button_variants_map_to_stable_style_classes()
    {
        Assert.AreEqual("nova-primary", NovaControlClasses.For(NovaButtonKind.Primary));
        Assert.AreEqual("nova-subtle", NovaControlClasses.For(NovaButtonKind.Subtle));
        Assert.AreEqual("nova-danger", NovaControlClasses.For(NovaButtonKind.Danger));
        Assert.AreEqual(string.Empty, NovaControlClasses.For(NovaButtonKind.Default));
    }

    [TestMethod]
    public void P0_control_surface_is_declared()
    {
        string[] expected =
        [
            "Button", "IconButton", "TextField", "NumberField", "Toggle", "Select",
            "PropertyRow", "InspectorSection", "TreeView", "ListView", "Splitter",
        ];

        foreach (var control in expected)
            CollectionAssert.Contains(NovaControlClasses.P0Controls.ToList(), control);
    }

    [TestMethod]
    public void Token_resource_keys_round_trip_for_every_token_type()
    {
        foreach (var token in NovaTokens.Dark.Tokens.Values)
        {
            var key = NovaResourceKeys.Of(token);
            StringAssert.StartsWith(key, "Nova.");
            StringAssert.Contains(key, token.Key);
        }
    }

    [TestMethod]
    public void Style_vocabulary_is_unique_and_non_empty()
    {
        var classes = new[]
        {
            NovaControlClasses.Button, NovaControlClasses.Primary, NovaControlClasses.Subtle,
            NovaControlClasses.Danger, NovaControlClasses.TextField, NovaControlClasses.NumberField,
            NovaControlClasses.Invalid, NovaControlClasses.PropertyRow, NovaControlClasses.InspectorSection,
            NovaControlClasses.Surface, NovaControlClasses.Elevated, NovaControlClasses.Toolbar,
            NovaControlClasses.Divider, NovaControlClasses.Mono, NovaControlClasses.Muted,
            NovaControlClasses.SectionTitle, NovaControlClasses.PropertyLabel,
        };

        Assert.AreEqual(classes.Length, classes.Distinct(StringComparer.Ordinal).Count());
        Assert.IsTrue(classes.All(c => !string.IsNullOrWhiteSpace(c)));
    }
}
