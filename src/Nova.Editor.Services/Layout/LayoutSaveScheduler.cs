namespace Nova.Editor.Services.Layout;

/// <summary>
/// Latest-wins debounce timer. Repeated requests replace the pending snapshot;
/// only the most recent elapsed timer is raised, on the synchronization context
/// that requested it.
/// </summary>
public sealed class LayoutSaveScheduler : IDisposable
{
    private readonly TimeSpan _debounce;
    private readonly Func<TimeSpan, CancellationToken, Task> _delay;
    private readonly object _gate = new();
    private CancellationTokenSource? _pending;
    private SynchronizationContext? _context;
    private long _generation;
    private bool _disposed;

    public LayoutSaveScheduler(
        Func<TimeSpan, CancellationToken, Task>? delay = null,
        TimeSpan? debounce = null)
    {
        _delay = delay ?? ((duration, token) => Task.Delay(duration, token));
        _debounce = debounce ?? TimeSpan.FromMilliseconds(500);
        if (_debounce <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(debounce), "The debounce must be positive.");
    }

    public TimeSpan Debounce => _debounce;

    public bool IsPending
    {
        get
        {
            lock (_gate)
                return _pending is not null;
        }
    }

    public event EventHandler? Elapsed;

    public void Schedule()
    {
        CancellationTokenSource source;
        long generation;
        SynchronizationContext? context;

        lock (_gate)
        {
            if (_disposed)
                return;

            _pending?.Cancel();
            source = new CancellationTokenSource();
            _pending = source;
            generation = ++_generation;
            context = SynchronizationContext.Current ?? _context;
            _context = context;
        }

        _ = RunAsync(generation, source, context);
    }

    public void Cancel()
    {
        lock (_gate)
        {
            _generation++;
            _pending?.Cancel();
            _pending = null;
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed)
                return;

            _disposed = true;
            _generation++;
            _pending?.Cancel();
            _pending = null;
            Elapsed = null;
        }
    }

    private async Task RunAsync(
        long generation,
        CancellationTokenSource source,
        SynchronizationContext? context)
    {
        try
        {
            await _delay(_debounce, source.Token).ConfigureAwait(false);
            lock (_gate)
            {
                if (_disposed || generation != _generation || source.IsCancellationRequested)
                    return;

                _pending = null;
            }

            if (context is null)
                RaiseElapsed(generation);
            else
                context.Post(_ => RaiseElapsed(generation), null);
        }
        catch (OperationCanceledException)
        {
        }
        catch
        {
            // The timer is a scheduling aid; a failed delay must not crash the
            // editor. The next layout change will request another attempt.
        }
    }

    private void RaiseElapsed(long generation)
    {
        lock (_gate)
        {
            if (_disposed || generation != _generation)
                return;
        }

        Elapsed?.Invoke(this, EventArgs.Empty);
    }
}
