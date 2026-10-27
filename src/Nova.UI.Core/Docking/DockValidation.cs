namespace Nova.UI.Core.Docking;

public sealed record DockValidationIssue(string Code, string Message);

public sealed record DockValidationResult(IReadOnlyList<DockValidationIssue> Issues)
{
    public bool IsValid => Issues.Count == 0;

    public string Summary => IsValid
        ? "valid"
        : string.Join("; ", Issues.Select(issue => $"{issue.Code}: {issue.Message}"));

    public bool Has(string code)
        => Issues.Any(issue => string.Equals(issue.Code, code, StringComparison.Ordinal));
}

internal static class DockLimits
{
    public const int MaxDepth = 64;
    public const int MaxNodes = 512;
    public const int MaxPanels = 256;
    public const int MaxFloatRoots = 16;
    public const double WeightTolerance = 1e-6;
}
