using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.Editor.Views.Docking;

/// <summary>
/// Creates the real editor panels for the dock projector. Titles are display
/// only; identity always comes from <see cref="PanelInstanceId"/>.
/// </summary>
public sealed class EditorDockPanelFactory : IDockPanelViewFactory
{
    private readonly EditorShellViewModel _viewModel;

    public EditorDockPanelFactory(EditorShellViewModel viewModel)
        => _viewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));

    public bool CanCreate(PanelInstanceId instance)
        => instance == DockPanelIds.Hierarchy
           || instance == DockPanelIds.Scene
           || instance == DockPanelIds.Inspector
           || instance == DockPanelIds.Console;

    public IDockPanelView Create(PanelInstanceId instance)
    {
        if (instance == DockPanelIds.Hierarchy)
            return new HierarchyPanelView(_viewModel);
        if (instance == DockPanelIds.Scene)
            return new ScenePanelView(_viewModel);
        if (instance == DockPanelIds.Inspector)
            return new InspectorPanelView(_viewModel);
        if (instance == DockPanelIds.Console)
            return new ConsolePanelView(_viewModel);

        throw new InvalidOperationException(
            $"No editor panel is registered for '{instance}'.");
    }
}
