using Avalonia.Controls;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

/// <summary>
/// Identity of one root host. The main root and each floating root have a
/// distinct host id; tokens are only meaningful within their own host.
/// </summary>
public readonly record struct DockRootHostId
{
    public const string MainValue = "main";

    private readonly string? _value;

    public DockRootHostId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new ArgumentException("A DockRootHostId must be non-empty.", nameof(value));

        _value = value.Trim();
    }

    public string Value => _value ?? string.Empty;

    public bool IsEmpty => string.IsNullOrEmpty(_value);

    public bool IsMain => string.Equals(_value, MainValue, StringComparison.Ordinal);

    public static DockRootHostId Main => new(MainValue);

    public static DockRootHostId ForFloat(DockFloatRootId id)
        => new($"float:{id}");

    public override string ToString() => Value;
}

public enum DockRootKind
{
    Main,
    Float,
}

public readonly record struct DockRootHostDescriptor(
    DockRootHostId Id,
    DockRootKind Kind,
    DockFloatRootId? FloatRootId)
{
    public static DockRootHostDescriptor ForMain()
        => new(DockRootHostId.Main, DockRootKind.Main, null);

    public static DockRootHostDescriptor ForFloat(DockFloatRootId id)
        => new(DockRootHostId.ForFloat(id), DockRootKind.Float, id);
}

public delegate IDockRootHost DockRootHostFactory(DockRootHostDescriptor descriptor);

public enum DockHostError
{
    None,
    CreationFailed,
    ProjectionFailed,
    ShowFailed,
    CloseFailed,
    ClosingCanceled,
    RetryFailed,
    HostUnavailable,
    StaleToken,
    ShuttingDown,
    NoTarget,
}

public readonly record struct DockHostResult(
    bool Success,
    DockHostError Error,
    string Diagnostic)
{
    public static DockHostResult Ok(string diagnostic = "")
        => new(true, DockHostError.None, diagnostic);

    public static DockHostResult Fail(DockHostError error, string diagnostic)
        => new(false, error, diagnostic);
}

/// <summary>
/// One root presentation host. The coordinator owns the host lifecycle and
/// never treats OS/window failures as model errors.
/// </summary>
public interface IDockRootHost : IDisposable
{
    DockRootHostId Id { get; }

    DockRootKind Kind { get; }

    bool IsVisible { get; }

    bool IsHealthy { get; }

    DockApplyResult? Current { get; }

    DockHostResult Prepare(DockTree root);

    DockHostResult Show();

    DockHostResult Apply(DockTree root);

    DockHostResult Retry();

    DockHostResult Close();

    bool TryGetCurrentToken(out DockApplyToken token);

    Control? FindNode(DockNodeId id);

    IDockPanelView? FindPanel(PanelInstanceId instance);
}

/// <summary>
/// Result of a coordinator transaction. Model and host outcomes stay separate:
/// <see cref="ModelCommitted"/> can be true while <see cref="Host"/> is failed,
/// in which case the model is retained and only the host is degraded.
/// </summary>
public readonly record struct DockWorkspaceHostResult(
    bool Success,
    bool ModelCommitted,
    DockWorkspace Workspace,
    DockWorkspaceError ModelError,
    DockHostResult Host,
    DockRootHostId HostId,
    string Diagnostic)
{
    public bool HostFailed => !Host.Success;
}

/// <summary>
/// Result of the load-stage restore transaction. <see cref="Success"/> only
/// means the model and initial host construction were adopted; host apply/show
/// failures are reported as diagnostics and remain D6-degraded, not merged.
/// </summary>
public sealed record DockRestoreResult(
    DockWorkspaceCoordinator? Coordinator,
    IReadOnlyList<DockFloatRootId> MergedFloatRoots,
    IReadOnlyList<string> Diagnostics,
    string? Failure)
{
    public bool Success => Coordinator is not null;

    public static DockRestoreResult Ok(
        DockWorkspaceCoordinator coordinator,
        IReadOnlyList<DockFloatRootId> mergedFloatRoots,
        IReadOnlyList<string> diagnostics)
        => new(coordinator, mergedFloatRoots, diagnostics, null);

    public static DockRestoreResult Fail(string diagnostic)
        => new(null, Array.Empty<DockFloatRootId>(), Array.Empty<string>(), diagnostic);
}
