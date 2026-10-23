using Avalonia;
using Avalonia.Styling;
using Nova.UI.Avalonia;
using Nova.UI.Theme;

namespace Nova.UI.Controls.Tests;

[TestClass]
public sealed class ThemeAdapterTests
{
    [TestMethod]
    public void Applying_both_themes_publishes_resources_and_replaces_styles()
    {
        var app = new Application();
        var catalog = NovaThemeCatalog.BuiltIn;

        var dark = NovaThemeAdapter.Apply(app, catalog.Get("nova.dark"));
        var light = NovaThemeAdapter.Apply(app, catalog.Get("nova.light"));

        Assert.AreEqual("nova.light", light.Theme.Id);
        Assert.AreEqual(ThemeVariant.Light, app.RequestedThemeVariant);
        Assert.IsTrue(light.ResourceKeys.Count > 0);
        Assert.IsTrue(light.Resources.ContainsKey(NovaResourceKeys.Brush("color.bg.canvas")));
        Assert.IsFalse(app.Styles.Contains(dark.Styles));
        Assert.IsTrue(app.Styles.Contains(light.Styles));
    }
}
