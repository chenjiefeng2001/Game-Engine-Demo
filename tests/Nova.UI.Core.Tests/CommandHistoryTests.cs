using Nova.UI;
using Nova.UI.Core.Commands;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class CommandHistoryTests
{
    private sealed class CounterCommand : IUndoableCommand
    {
        private readonly int[] _value;
        private readonly int _delta;

        public CounterCommand(int[] value, int delta)
        {
            _value = value;
            _delta = delta;
        }

        public UiId Id => new("test.counter");
        public string Label => $"Add {_delta}";
        public bool CanExecute() => true;
        public void Execute() => _value[0] += _delta;
        public void Undo() => _value[0] -= _delta;
    }

    [TestMethod]
    public void Execute_undo_redo_restores_values_and_stack_shape()
    {
        var value = new[] { 0 };
        var history = new CommandHistory();
        history.Execute(new CounterCommand(value, 2));
        history.Execute(new CounterCommand(value, 3));
        Assert.AreEqual(5, value[0]);
        Assert.AreEqual(2, history.UndoCount);

        history.Undo();
        Assert.AreEqual(2, value[0]);
        Assert.AreEqual(1, history.UndoCount);
        Assert.AreEqual(1, history.RedoCount);

        history.Redo();
        Assert.AreEqual(5, value[0]);
        Assert.AreEqual(2, history.UndoCount);
        Assert.AreEqual(0, history.RedoCount);
    }

    [TestMethod]
    public void New_command_after_undo_clears_redo()
    {
        var value = new[] { 0 };
        var history = new CommandHistory();
        history.Execute(new CounterCommand(value, 1));
        history.Undo();
        history.Execute(new CounterCommand(value, 10));

        Assert.AreEqual(10, value[0]);
        Assert.IsFalse(history.CanRedo);
    }

    [TestMethod]
    public void History_depth_is_bounded()
    {
        var value = new[] { 0 };
        var history = new CommandHistory(maxDepth: 3);

        for (var i = 0; i < 10; i++)
            history.Execute(new CounterCommand(value, 1));

        Assert.AreEqual(3, history.UndoCount);
        while (history.CanUndo) history.Undo();
        Assert.AreEqual(7, value[0]);
    }

    [TestMethod]
    public void Command_that_throws_is_not_recorded()
    {
        var history = new CommandHistory();
        Assert.ThrowsException<InvalidOperationException>(
            () => history.Execute(new ThrowingCommand()));
        Assert.AreEqual(0, history.UndoCount);
    }

    [TestMethod]
    public void History_rejects_a_command_from_another_context_before_mutating()
    {
        var value = new[] { 0 };
        var contextA = Guid.NewGuid();
        var contextB = Guid.NewGuid();
        var history = new CommandHistory(contextId: contextA);
        var foreign = new ContextCommand(value, contextB, 7);

        Assert.ThrowsException<InvalidOperationException>(() => history.Execute(foreign));
        Assert.AreEqual(0, value[0]);
        Assert.AreEqual(0, history.UndoCount);
    }

    [TestMethod]
    public void CanUndo_reflects_the_top_command_revertibility()
    {
        var value = new[] { 0 };
        var history = new CommandHistory();
        var command = new ContextCommand(value, default, 3);
        history.Execute(command);

        Assert.IsTrue(history.CanUndo);
        command.AllowUndo = false;
        Assert.IsFalse(history.CanUndo);
        Assert.ThrowsException<InvalidOperationException>(() => history.Undo());
        Assert.AreEqual(3, value[0]);
        Assert.AreEqual(1, history.UndoCount);
    }

    private sealed class ContextCommand : IUndoableCommand
    {
        private readonly int[] _value;
        private readonly int _delta;

        public ContextCommand(int[] value, Guid contextId, int delta)
        {
            _value = value;
            ContextId = contextId;
            _delta = delta;
        }

        public UiId Id => new("test.context");
        public string Label => "Context";
        public Guid ContextId { get; }
        public bool AllowUndo { get; set; } = true;
        public bool CanExecute() => true;
        public bool CanUndo() => AllowUndo;
        public void Execute() => _value[0] += _delta;
        public void Undo() => _value[0] -= _delta;
    }

    private sealed class ThrowingCommand : IUndoableCommand
    {
        public UiId Id => new("test.throw");
        public string Label => "Throw";
        public bool CanExecute() => true;
        public void Execute() => throw new InvalidOperationException("boom");
        public void Undo() { }
    }
}
