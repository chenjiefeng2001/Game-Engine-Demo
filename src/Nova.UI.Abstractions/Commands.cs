namespace Nova.UI;

/// <summary>
/// A command that can be applied, reverted, and re-applied. Commands own the
/// mutation; controls never write model objects directly.
/// </summary>
public interface IUndoableCommand
{
    UiId Id { get; }
    string Label { get; }

    /// <summary>
    /// Workspace/document token used by <see cref="ICommandHistory"/> to reject
    /// commands from another context. Empty means "unbound" (tests/legacy).
    /// </summary>
    Guid ContextId => Guid.Empty;

    bool CanExecute();

    /// <summary>
    /// Whether the current model state still allows reverting this command.
    /// This is not the same as <see cref="CanExecute"/> for add/delete commands.
    /// </summary>
    bool CanUndo() => true;

    void Execute();
    void Undo();
}

public interface ICommandHistory
{
    bool CanUndo { get; }
    bool CanRedo { get; }
    int UndoCount { get; }
    int RedoCount { get; }

    event EventHandler? Changed;
    event EventHandler<UndoableCommandEventArgs>? Executed;
    event EventHandler<UndoableCommandEventArgs>? Undone;
    event EventHandler<UndoableCommandEventArgs>? Redone;

    /// <summary>Executes and, on success, records the command. A throwing command is not recorded.</summary>
    void Execute(IUndoableCommand command);

    void Undo();
    void Redo();
    void Clear();
}

public sealed class UndoableCommandEventArgs : EventArgs
{
    public UndoableCommandEventArgs(IUndoableCommand command)
        => Command = command ?? throw new ArgumentNullException(nameof(command));

    public IUndoableCommand Command { get; }
}

public readonly record struct CommandResult(bool Success, string? Message = null)
{
    public static CommandResult Ok(string? message = null) => new(true, message);

    public static CommandResult Fail(string message) => new(false, message);
}

/// <summary>
/// Named entry point for keyboard, menu, toolbar, and automation dispatch.
/// A gesture is metadata; actual key binding is installed by the host.
/// </summary>
public sealed record CommandDescriptor(
    UiId Id,
    string Label,
    Func<bool> CanExecute,
    Action Execute,
    string? Gesture = null);

public sealed class CommandRoutedEventArgs : EventArgs
{
    public CommandRoutedEventArgs(CommandDescriptor descriptor, CommandResult result)
    {
        Descriptor = descriptor;
        Result = result;
    }

    public CommandDescriptor Descriptor { get; }
    public CommandResult Result { get; }
}

public interface ICommandRouter
{
    IReadOnlyList<CommandDescriptor> Commands { get; }

    bool Register(CommandDescriptor descriptor);

    bool CanExecute(UiId id);

    CommandResult Execute(UiId id);

    event EventHandler<CommandRoutedEventArgs>? Executed;
}

public static class NovaCommandIds
{
    public static readonly UiId Undo = new("editor.undo");
    public static readonly UiId Redo = new("editor.redo");
    public static readonly UiId AddNode = new("scene.add-node");
    public static readonly UiId DeleteNode = new("scene.delete-node");
    public static readonly UiId SaveLayout = new("layout.save");
    public static readonly UiId ResetLayout = new("layout.reset");
    public static readonly UiId ToggleTheme = new("theme.toggle");
    public static readonly UiId ToggleHierarchy = new("view.toggle-hierarchy");
    public static readonly UiId ToggleInspector = new("view.toggle-inspector");
    public static readonly UiId ToggleConsole = new("view.toggle-console");
}
