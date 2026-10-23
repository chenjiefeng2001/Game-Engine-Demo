using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Styling;
using Avalonia.Layout;

namespace Nova.UI.Controls;

// Thin, tokenized building blocks. They do not replace Avalonia panels; they
// exist so editor pages compose a consistent vocabulary with stable class names.
public class NovaSurface : Border
{
    public NovaSurface() => Classes.Add(NovaControlClasses.Surface);
}

public class NovaElevatedSurface : Border
{
    public NovaElevatedSurface() => Classes.Add(NovaControlClasses.Elevated);
}

public class NovaBox : Border
{
    public NovaBox() => Padding = new Thickness(8);
}

public class NovaRow : StackPanel
{
    public NovaRow()
    {
        Orientation = Orientation.Horizontal;
        Spacing = 4;
    }
}

public class NovaColumn : StackPanel
{
    public NovaColumn() => Spacing = 4;
}

public class NovaGrid : Grid
{
}

public class NovaScrollArea : ScrollViewer
{
    public NovaScrollArea()
    {
        HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
    }
}

public class NovaSplitter : GridSplitter
{
    public NovaSplitter()
    {
        ShowsPreview = false;
    }
}

public class NovaText : TextBlock
{
}

public class NovaMutedText : TextBlock
{
    public NovaMutedText() => Classes.Add(NovaControlClasses.Muted);
}

public class NovaIcon : TextBlock
{
    public static readonly StyledProperty<string?> GlyphProperty =
        AvaloniaProperty.Register<NovaIcon, string?>(nameof(Glyph));

    public NovaIcon()
    {
        FontSize = 16;
        VerticalAlignment = VerticalAlignment.Center;
        HorizontalAlignment = HorizontalAlignment.Center;
    }

    public string? Glyph
    {
        get => GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == GlyphProperty)
            Text = Glyph;
    }
}

public class NovaDivider : Border
{
    public NovaDivider()
    {
        Classes.Add(NovaControlClasses.Divider);
        Height = 1;
    }
}

/// <summary>
/// Avalonia TreeView already virtualizes its item containers. We keep the
/// standard control and only provide a stable Nova class/identity.
/// </summary>
public class NovaTreeView : TreeView
{
    public NovaTreeView()
    {
        Classes.Add("nova-tree-view");
        Styles.Add(new Style(x => x.OfType<TreeViewItem>())
        {
            Setters =
            {
                // Optional expansion state is part of the item data contract.
                // Nodes without the property simply keep the container default.
                new Setter(TreeViewItem.IsExpandedProperty,
                    new Binding("IsExpanded") { Mode = BindingMode.TwoWay }),
            },
        });
    }

    protected override Type StyleKeyOverride => typeof(TreeView);
}

/// <summary>
/// ListBox uses a virtualizing StackPanel by default; this wrapper prevents
/// future editor pages from accidentally replacing it with a non-virtualizing panel.
/// </summary>
public class NovaListView : ListBox
{
    public NovaListView() => Classes.Add("nova-list-view");

    protected override Type StyleKeyOverride => typeof(ListBox);
}
