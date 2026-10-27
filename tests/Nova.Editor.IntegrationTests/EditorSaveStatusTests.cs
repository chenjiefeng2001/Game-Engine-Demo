using System.Text;
using Avalonia.Controls;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Controls;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// D8-E addendum gates: the save result must be visible in the real status
/// bar, not only on the view-model. The selection-status binding stays
/// independent, and the unresolved-panel suppression must never be reported
/// as a saved layout.
/// </summary>
[TestClass]
public sealed class EditorSaveStatusTests
{
    private static readonly PanelInstanceId UnknownChat = new("third.party.chat");

    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(
            Path.GetTempPath(), "nova-d8e-save-status", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    [TestMethod]
    public async Task Save_layout_reports_saved_without_replacing_the_selection_status()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var shell = new EditorShellView(viewModel);
        try
        {
            await shell.InitializeAsync();

            var saveButton = Require<NovaButton>(shell.FindControl<NovaButton>("SaveLayoutButton"));
            Assert.AreSame(
                viewModel.SaveLayoutCommand,
                saveButton.Command,
                "The toolbar button must stay wired to the production save command.");

            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));
            var selection = Require<TextBlock>(shell.FindControl<TextBlock>("StatusText"));

            saveButton.Command!.Execute(null);

            Assert.AreEqual(
                "Layout saved",
                await WaitForStatusAsync(status, text => text == "Layout saved", "a saved report"));
            Assert.AreNotEqual(
                "Layout saved",
                selection.Text,
                "The selection status slot must keep its own store binding.");
            Assert.IsTrue(File.Exists(_layoutPath));
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task A_locked_layout_file_shows_the_failure_and_the_retry_shows_success()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var shell = new EditorShellView(viewModel);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var saveButton = Require<NovaButton>(shell.FindControl<NovaButton>("SaveLayoutButton"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));

            saveButton.Command!.Execute(null);
            await WaitForStatusAsync(status, text => text == "Layout saved", "the first save");
            var bytesAfterSave = await File.ReadAllBytesAsync(_layoutPath);

            // Hold the target with FileShare.None: the atomic replace fails and
            // the status bar must say so instead of staying silent.
            using (var gate = File.Open(
                       _layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            {
                Assert.IsTrue(dock.HidePanel(DockPanelIds.Console).Success);

                saveButton.Command!.Execute(null);

                await WaitForStatusAsync(
                    status,
                    text => text.StartsWith("Layout save failed:", StringComparison.Ordinal),
                    "a failed save report");
            }

            // Nothing was written while the file was held: the previous layout
            // is byte-identical until the retry succeeds.
            CollectionAssert.AreEqual(
                bytesAfterSave,
                await File.ReadAllBytesAsync(_layoutPath),
                "A failed save must leave the previous layout byte-identical.");

            // The same production button, now unblocked, reports success.
            saveButton.Command!.Execute(null);

            Assert.AreEqual(
                "Layout saved",
                await WaitForStatusAsync(status, text => text == "Layout saved", "the retried save"));

            var raw = await File.ReadAllTextAsync(_layoutPath, Encoding.UTF8);
            var parsed = WorkspaceLayoutSerializer.Deserialize(raw);
            Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
            CollectionAssert.AreEqual(
                new[] { DockPanelIds.Console },
                parsed.DockWorkspace!.EnumerateHiddenPanels().ToArray());
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    [TestMethod]
    public async Task Unresolved_suppression_reports_paused_and_only_an_explicit_save_reports_saved()
    {
        await File.WriteAllTextAsync(_layoutPath, CreateUnknownPanelJson());
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        var viewModel = new EditorShellViewModel(workspace);
        var shell = new EditorShellView(viewModel);
        try
        {
            await shell.InitializeAsync();
            var dock = Require<EditorDockWorkspace>(shell.DockWorkspace);
            var saveButton = Require<NovaButton>(shell.FindControl<NovaButton>("SaveLayoutButton"));
            var status = Require<TextBlock>(shell.FindControl<TextBlock>("SaveStatusText"));

            // Startup with unresolved panels must not claim a saved layout.
            StringAssert.StartsWith(status.Text ?? string.Empty, "Offline · autosave paused (");
            StringAssert.Contains(status.Text!, "cannot resolve");
            Assert.IsFalse(
                status.Text!.Contains("Layout saved", StringComparison.Ordinal),
                "A suppressed startup must not report a saved layout.");
            Assert.IsTrue(workspace.IsPersistenceSuppressed);

            // A real model change schedules nothing: suppression holds and the
            // status bar keeps reporting the pause.
            Assert.IsTrue(dock.ShowPanel(UnknownChat).Success);
            await Task.Delay(800);

            Assert.IsTrue(workspace.IsPersistenceSuppressed);
            Assert.IsFalse(
                status.Text!.Contains("Layout saved", StringComparison.Ordinal),
                "A suppressed automatic save must not report a saved layout.");

            saveButton.Command!.Execute(null);

            Assert.AreEqual(
                "Layout saved",
                await WaitForStatusAsync(status, text => text == "Layout saved", "the explicit save"));
            Assert.IsFalse(workspace.IsPersistenceSuppressed);

            var raw = await File.ReadAllTextAsync(_layoutPath, Encoding.UTF8);
            StringAssert.Contains(raw, "third.party.scene");
            StringAssert.Contains(raw, "third.party.chat");
        }
        finally
        {
            shell.DockWorkspace?.Dispose();
        }
    }

    private static string CreateUnknownPanelJson()
    {
        var hidden = DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        var json = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 333,
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(hidden.Workspace!),
            });
        return json
            .Replace("\"editor.scene\"", "\"third.party.scene\"", StringComparison.Ordinal)
            .Replace("\"editor.console\"", "\"third.party.chat\"", StringComparison.Ordinal);
    }

    private static async Task<string> WaitForStatusAsync(
        TextBlock status,
        Func<string, bool> match,
        string because)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            var text = status.Text ?? string.Empty;
            if (match(text))
                return text;

            await Task.Delay(25);
        }

        var last = status.Text ?? string.Empty;
        Assert.Fail(
            $"Timed out waiting for the status bar to show {because}. Last value: '{last}'.");
        return last;
    }

    private static T Require<T>(object? value) where T : class
    {
        Assert.IsNotNull(value);
        Assert.IsInstanceOfType(value, typeof(T));
        return (T)value;
    }
}
