using Nova.UI;

namespace Nova.UI.Core.Commands;

/// <summary>
/// Single dispatch point for menu, toolbar, shortcuts, and automation. Handlers
/// are registered by the composition root, not by controls.
/// </summary>
public sealed class CommandRouter : ICommandRouter
{
    private readonly Dictionary<UiId, CommandDescriptor> _commands = new();

    public IReadOnlyList<CommandDescriptor> Commands => _commands.Values.ToArray();

    public event EventHandler<CommandRoutedEventArgs>? Executed;

    public bool Register(CommandDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        if (descriptor.Id.IsEmpty) throw new ArgumentException("Command id is required.", nameof(descriptor));
        if (string.IsNullOrWhiteSpace(descriptor.Label))
            throw new ArgumentException("Command label is required.", nameof(descriptor));
        ArgumentNullException.ThrowIfNull(descriptor.CanExecute);
        ArgumentNullException.ThrowIfNull(descriptor.Execute);

        return _commands.TryAdd(descriptor.Id, descriptor);
    }

    public bool CanExecute(UiId id)
        => _commands.TryGetValue(id, out var descriptor) && descriptor.CanExecute();

    public CommandResult Execute(UiId id)
    {
        if (!_commands.TryGetValue(id, out var descriptor))
            return Fail(id, "unknown command");

        try
        {
            if (!descriptor.CanExecute())
                return Fail(id, "command is currently unavailable");

            descriptor.Execute();
            var result = CommandResult.Ok();
            Executed?.Invoke(this, new CommandRoutedEventArgs(descriptor, result));
            return result;
        }
        catch (Exception ex)
        {
            // Routing is a boundary: a failing command must not take down the shell.
            var result = Fail(id, ex.Message);
            Executed?.Invoke(this, new CommandRoutedEventArgs(descriptor, result));
            return result;
        }
    }

    private static CommandResult Fail(UiId id, string message) => CommandResult.Fail($"{id}: {message}");
}
