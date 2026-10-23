using Avalonia;
using Avalonia.Controls;

namespace Nova.UI.Avalonia;

/// <summary>
/// Attached stable identity. This is identity only; it is not a global state
/// container and carries no workspace data.
/// </summary>
public sealed class NovaUi
{
    private NovaUi() { }

    public static readonly AttachedProperty<UiId?> IdProperty =
        AvaloniaProperty.RegisterAttached<NovaUi, Control, UiId?>("Id", null);

    public static void SetId(Control control, UiId? value) => control.SetValue(IdProperty, value);

    public static UiId? GetId(Control control) => control.GetValue(IdProperty);

    public static string GetIdText(Control control) => GetId(control)?.Value ?? string.Empty;
}
