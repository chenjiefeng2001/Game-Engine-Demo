namespace Nova.UI.Core.Layout;

/// <summary>
/// Normalizes untrusted layout data (old config, hand-edited JSON, failed restore).
/// Never throws: invalid fields fall back to defaults and the layout remains usable.
/// </summary>
public static class LayoutValidator
{
    public static WorkspaceLayout Normalize(WorkspaceLayout? candidate, WorkspaceLayout? defaults = null)
    {
        var fallback = defaults ?? WorkspaceLayout.Default;
        if (candidate is null) return fallback;

        var windowWidth = Finite(candidate.WindowWidth, fallback.WindowWidth, WorkspaceLayout.MinWindowWidth, 10000);
        var windowHeight = Finite(candidate.WindowHeight, fallback.WindowHeight, WorkspaceLayout.MinWindowHeight, 10000);

        var left = Finite(candidate.HierarchyWidth, fallback.HierarchyWidth,
            WorkspaceLayout.MinPanelWidth, Math.Max(WorkspaceLayout.MinPanelWidth, windowWidth - 360));
        var right = Finite(candidate.InspectorWidth, fallback.InspectorWidth,
            WorkspaceLayout.MinPanelWidth, Math.Max(WorkspaceLayout.MinPanelWidth, windowWidth - 360));

        // Preserve a usable center column even if a hand-edited layout is greedy.
        const double centerMinimum = 320;
        var available = Math.Max(2 * WorkspaceLayout.MinPanelWidth, windowWidth - centerMinimum);
        if (left + right > available && left + right > 0)
        {
            var scale = available / (left + right);
            left = Math.Max(WorkspaceLayout.MinPanelWidth, Math.Floor(left * scale));
            right = Math.Max(WorkspaceLayout.MinPanelWidth, Math.Floor(right * scale));
        }

        var console = Finite(candidate.ConsoleHeight, fallback.ConsoleHeight,
            WorkspaceLayout.MinConsoleHeight, WorkspaceLayout.MaxConsoleHeight);
        console = Math.Min(console, Math.Max(WorkspaceLayout.MinConsoleHeight, windowHeight - 220));

        var theme = string.IsNullOrWhiteSpace(candidate.ThemeId) ? fallback.ThemeId : candidate.ThemeId.Trim();

        return new WorkspaceLayout
        {
            SchemaVersion = WorkspaceLayout.CurrentSchemaVersion,
            WindowWidth = windowWidth,
            WindowHeight = windowHeight,
            HierarchyWidth = left,
            InspectorWidth = right,
            ConsoleHeight = console,
            ShowHierarchy = candidate.ShowHierarchy,
            ShowInspector = candidate.ShowInspector,
            ShowConsole = candidate.ShowConsole,
            ThemeId = theme,
        };
    }

    private static double Finite(double value, double fallback, double minimum, double maximum)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
            value = fallback;
        return Math.Clamp(value, minimum, Math.Max(minimum, maximum));
    }
}
