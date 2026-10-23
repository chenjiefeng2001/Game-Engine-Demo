using Nova.UI;

namespace Nova.UI.Core.Commands;

/// <summary>Linear undo/redo history. Commands are applied at most once per stack entry.</summary>
public sealed class CommandHistory : ICommandHistory
{
    private readonly List<IUndoableCommand> _undo = new();
    private readonly List<IUndoableCommand> _redo = new();
    private readonly int _maxDepth;
    private readonly Guid _contextId;

    /// <param name="maxDepth">Maximum retained undo entries.</param>
    /// <param name="contextId">
    /// Document/workspace token. When non-empty, commands carrying a different
    /// non-empty token are rejected. Empty means unbound (tests/legacy history).
    /// </param>
    public CommandHistory(int maxDepth = 100, Guid contextId = default)
    {
        if (maxDepth <= 0) throw new ArgumentOutOfRangeException(nameof(maxDepth));
        _maxDepth = maxDepth;
        _contextId = contextId;
    }

    public bool CanUndo => _undo.Count > 0 && _undo[^1].CanUndo();
    public bool CanRedo => _redo.Count > 0 && _redo[^1].CanExecute();
    public int UndoCount => _undo.Count;
    public int RedoCount => _redo.Count;

    public event EventHandler? Changed;
    public event EventHandler<UndoableCommandEventArgs>? Executed;
    public event EventHandler<UndoableCommandEventArgs>? Undone;
    public event EventHandler<UndoableCommandEventArgs>? Redone;

    public void Execute(IUndoableCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        EnsureContext(command);
        if (!command.CanExecute())
            throw new InvalidOperationException($"Command '{command.Id}' cannot execute in the current state.");

        command.Execute();
        Record(command);

        Executed?.Invoke(this, new UndoableCommandEventArgs(command));
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Undo()
    {
        if (_undo.Count == 0) return;

        var command = _undo[^1];
        EnsureContext(command);
        if (!command.CanUndo())
            throw new InvalidOperationException(
                $"Command '{command.Id}' can no longer be undone in this context.");

        command.Undo();
        _undo.RemoveAt(_undo.Count - 1);
        _redo.Add(command);

        Undone?.Invoke(this, new UndoableCommandEventArgs(command));
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Redo()
    {
        if (_redo.Count == 0) return;

        var command = _redo[^1];
        EnsureContext(command);
        if (!command.CanExecute())
            throw new InvalidOperationException($"Command '{command.Id}' cannot redo in the current state.");

        command.Execute();
        _redo.RemoveAt(_redo.Count - 1);
        _undo.Add(command);

        Redone?.Invoke(this, new UndoableCommandEventArgs(command));
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Clear()
    {
        if (_undo.Count == 0 && _redo.Count == 0) return;
        _undo.Clear();
        _redo.Clear();
        Changed?.Invoke(this, EventArgs.Empty);
    }

    private void EnsureContext(IUndoableCommand command)
    {
        if (_contextId != Guid.Empty &&
            command.ContextId != Guid.Empty &&
            command.ContextId != _contextId)
        {
            throw new InvalidOperationException(
                $"Command '{command.Id}' belongs to another workspace context.");
        }
    }

    private void Record(IUndoableCommand command)
    {
        _undo.Add(command);
        if (_undo.Count > _maxDepth)
            _undo.RemoveAt(0);
        _redo.Clear();
    }
}
