using Avalonia.Controls;

namespace Nova.UI.Controls;

/// <summary>Inspector group. Expander provides keyboard/focus behavior for free.</summary>
public class NovaInspectorSection : Expander
{
    public NovaInspectorSection()
    {
        Classes.Add(NovaControlClasses.InspectorSection);
        IsExpanded = true;
    }

    protected override Type StyleKeyOverride => typeof(Expander);
}
