using Nova.UI.Core.Layout;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class LayoutSerializerTests
{
    [TestMethod]
    public void Round_trip_preserves_normalized_values()
    {
        var layout = WorkspaceLayout.Default with
        {
            WindowWidth = 1440,
            WindowHeight = 900,
            HierarchyWidth = 280,
            InspectorWidth = 360,
            ConsoleHeight = 220,
            ShowConsole = false,
            ThemeId = "nova.light",
        };

        var json = WorkspaceLayoutSerializer.Serialize(layout);
        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback);
        Assert.AreEqual(280, parsed.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(360, parsed.Layout.InspectorWidth, 0.001);
        Assert.AreEqual(220, parsed.Layout.ConsoleHeight, 0.001);
        Assert.IsFalse(parsed.Layout.ShowConsole);
        Assert.AreEqual("nova.light", parsed.Layout.ThemeId);
    }

    [TestMethod]
    public void Corrupt_json_falls_back_to_defaults_without_throwing()
    {
        var parsed = WorkspaceLayoutSerializer.Deserialize("{ this is not json");

        Assert.IsTrue(parsed.UsedFallback);
        Assert.AreEqual(WorkspaceLayout.Default, parsed.Layout);
        Assert.IsNotNull(parsed.Reason);
    }

    [TestMethod]
    public void Unknown_schema_falls_back_to_defaults()
    {
        var json = WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default)
            .Replace("\"schemaVersion\": 1", "\"schemaVersion\": 99");

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);
        Assert.IsTrue(parsed.UsedFallback);
        StringAssert.Contains(parsed.Reason, "schema");
        Assert.AreEqual(WorkspaceLayout.Default, parsed.Layout);
    }

    [TestMethod]
    public void Normalization_clamps_greedy_and_invalid_layouts()
    {
        var normalized = LayoutValidator.Normalize(new WorkspaceLayout
        {
            WindowWidth = 1000,
            WindowHeight = 700,
            HierarchyWidth = 5000,
            InspectorWidth = double.NaN,
            ConsoleHeight = -4,
        });

        Assert.IsTrue(normalized.HierarchyWidth <= 640);
        Assert.IsTrue(normalized.InspectorWidth >= WorkspaceLayout.MinPanelWidth);
        Assert.IsTrue(normalized.InspectorWidth <= WorkspaceLayout.Default.InspectorWidth);
        Assert.AreEqual(WorkspaceLayout.MinConsoleHeight, normalized.ConsoleHeight, 0.001);
        Assert.IsTrue(normalized.HierarchyWidth + normalized.InspectorWidth <= 680);
    }
}
