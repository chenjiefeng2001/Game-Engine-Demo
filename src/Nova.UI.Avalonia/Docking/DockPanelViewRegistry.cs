using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Nova.UI.Core.Docking;

namespace Nova.UI.Avalonia.Docking;

public readonly record struct DockPanelViewRegistration(
    PanelInstanceId Instance,
    ContentControl Host,
    IDockPanelView? View,
    string Title,
    string? Failure,
    long Version,
    bool Unresolved = false)
{
    /// <summary>
    /// Unresolved is a normal, non-degraded state: the identity is owned by the
    /// workspace but this build has no implementation for it.
    /// </summary>
    public bool Failed => View is null && !Unresolved;
}

/// <summary>
/// Workspace-level owner of panel views. Root hosts mount the shared
/// <see cref="DockPanelViewRegistration.Host"/> control; they never create,
/// replace or dispose the view itself.
/// </summary>
public sealed class DockPanelViewRegistry : IDisposable
{
    private readonly IDockPanelViewFactory _factory;
    private readonly Dictionary<PanelInstanceId, Entry> _entries = [];
    private bool _disposed;

    public DockPanelViewRegistry(IDockPanelViewFactory factory)
    {
        _factory = factory ?? throw new ArgumentNullException(nameof(factory));
    }

    public int Count => _entries.Count;

    public int ReleaseFailureCount { get; private set; }

    public string? LastDiagnostic { get; private set; }

    public bool CanCreate(PanelInstanceId instance)
    {
        ThrowIfDisposed();
        return _factory.CanCreate(instance);
    }

    public DockPanelViewRegistration GetOrCreate(PanelInstanceId instance)
    {
        ThrowIfDisposed();
        if (instance.IsEmpty)
            throw new ArgumentException("A PanelInstanceId must be non-empty.", nameof(instance));

        if (_entries.TryGetValue(instance, out var existing))
            return existing.ToRegistration();

        var entry = new Entry(instance);
        _entries.Add(instance, entry);
        CreateView(entry);
        return entry.ToRegistration();
    }

    public bool TryGet(
        PanelInstanceId instance,
        out DockPanelViewRegistration registration)
    {
        ThrowIfDisposed();
        if (_entries.TryGetValue(instance, out var entry))
        {
            registration = entry.ToRegistration();
            return true;
        }

        registration = default;
        return false;
    }

    public DockPanelViewRegistration Retry(PanelInstanceId instance)
    {
        ThrowIfDisposed();
        if (!_entries.TryGetValue(instance, out var entry) || !entry.Failed)
            return entry?.ToRegistration() ?? default;

        CreateView(entry);
        return entry.ToRegistration();
    }

    public void Release(PanelInstanceId instance)
    {
        if (!_entries.Remove(instance, out var entry))
            return;

        entry.Host.Content = null;
        var view = entry.View;
        entry.View = null;
        if (view is null)
            return;

        try
        {
            view.Dispose();
        }
        catch (Exception ex)
        {
            ReleaseFailureCount++;
            LastDiagnostic =
                $"Panel view '{instance}' failed to dispose: {ex.GetType().Name}: {ex.Message}";
        }
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        foreach (var instance in _entries.Keys.ToArray())
            Release(instance);
        _entries.Clear();
    }

    private void CreateView(Entry entry)
    {
        if (!_factory.CanCreate(entry.Instance))
        {
            entry.Host.Content = null;
            entry.View = null;
            entry.Unresolved = true;
            entry.Failure = null;
            entry.Title = entry.Instance.ToString();
            entry.Host.Content = CreateUnavailablePlaceholder(entry.Instance);
            entry.Version++;
            return;
        }

        entry.Unresolved = false;
        IDockPanelView? view = null;
        try
        {
            view = _factory.Create(entry.Instance)
                ?? throw new InvalidOperationException("Panel view factory returned null.");

            var content = view.Content
                ?? throw new InvalidOperationException(
                    $"Panel view for '{entry.Instance}' has no content.");

            entry.Host.Content = null;
            entry.View = view;
            entry.Title = string.IsNullOrWhiteSpace(view.Title)
                ? entry.Instance.ToString()
                : view.Title;
            entry.Failure = null;
            entry.Host.Content = content;
            entry.Version++;
            view = null;
        }
        catch (Exception ex)
        {
            SafeDispose(view);
            entry.View = null;
            entry.Failure = string.IsNullOrWhiteSpace(ex.Message)
                ? "Panel view creation failed."
                : ex.Message;
            entry.Title = entry.Instance.ToString();
            entry.Host.Content = CreatePlaceholder(entry.Instance, entry.Failure);
            entry.Version++;
        }
    }

    private static void SafeDispose(IDockPanelView? view)
    {
        try
        {
            view?.Dispose();
        }
        catch
        {
            // A failing factory must not mask the original creation failure.
        }
    }

    private static Control CreatePlaceholder(PanelInstanceId instance, string failure)
    {
        return new TextBlock
        {
            Name = "DockPanelFailure",
            Text = $"{instance}\nPanel view failed: {failure}",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(12),
        };
    }

    private static Control CreateUnavailablePlaceholder(PanelInstanceId instance)
    {
        return new TextBlock
        {
            Name = "DockPanelUnavailable",
            Text = $"{instance}\nThis panel is not available in this build.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(12),
        };
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(DockPanelViewRegistry));
    }

    private sealed class Entry(PanelInstanceId instance)
    {
        public PanelInstanceId Instance { get; } = instance;

        public ContentControl Host { get; } = new();

        public IDockPanelView? View { get; set; }

        public string Title { get; set; } = instance.ToString();

        public string? Failure { get; set; }

        public long Version { get; set; }

        public bool Unresolved { get; set; }

        public bool Failed => View is null && !Unresolved;

        public DockPanelViewRegistration ToRegistration()
            => new(Instance, Host, View, Title, Failure, Version, Unresolved);
    }
}
