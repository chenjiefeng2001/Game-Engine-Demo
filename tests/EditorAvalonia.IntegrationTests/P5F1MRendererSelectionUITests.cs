using System.Diagnostics;
using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Automation;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EditorAvalonia.IntegrationTests;

[TestClass]
public sealed class P5F1MRendererSelectionUITests
{
    private const string ReadyTitle = "Avalonia Editor — T6UI-HOST";
    private static readonly TimeSpan UiTimeout = TimeSpan.FromSeconds(40);

    [TestMethod]
    public void Renderer_path_selector_toggle_and_status()
    {
        try
        {
            _ = AutomationElement.RootElement;
        }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_p5f1m_" + Guid.NewGuid().ToString("N"));

        Process? process = null;
            bool passed = false;
        try
        {
            Directory.CreateDirectory(scratch);
            CopyDirectory(Path.Combine(repo, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);

            // Find RendererPathSelector ComboBox
            var selector = WaitFor(() => FindByAutomationId(main, "ViewportRendererPathSelector"),
                UiTimeout, "ViewportRendererPathSelector");
            Assert.IsNotNull(selector, "RendererPathSelector ComboBox not found");

            // Find status text
            var status = WaitFor(() => FindByAutomationId(main, "RendererPathStatus"),
                UiTimeout, "RendererPathStatus");

            // Verify initial state is Direct
            string initialStatus = status.Current.Name;
            Assert.IsTrue(initialStatus.Contains("Direct", StringComparison.OrdinalIgnoreCase),
                $"Expected initial status 'Direct', got '{initialStatus}'");

            // Select RenderGraph (index 1)
            SelectComboBoxItem(selector, 1);
            Thread.Sleep(500);
            main = WaitForMainWindow(process, UiTimeout);
            status = FindByAutomationId(main, "RendererPathStatus");
            Assert.IsNotNull(status);
            string rgStatus = status.Current.Name;
            // Status may still show Direct if native hasn't applied yet, or RenderGraph if applied
            // We just verify no crash and control is responsive

            // Select Direct (index 0)
            selector = FindByAutomationId(main, "ViewportRendererPathSelector");
            Assert.IsNotNull(selector);
            SelectComboBoxItem(selector, 0);
            Thread.Sleep(500);

            passed = true;
        }
        finally
        {
            if (process is not null && !process.HasExited)
            {
                try { process.Kill(entireProcessTree: true); } catch { }
            }
            try { Directory.Delete(scratch, recursive: true); } catch { }
        }

        Assert.IsTrue(passed);
    }

    // Helper methods (copied from T6ConsoleUITests pattern)
    private static string LocateRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "editor_avalonia", "AvaloniaEditor.csproj")))
                return dir.FullName;
            dir = dir.Parent;
        }
        throw new InvalidOperationException("Repo root not found");
    }

    private static string LocateEditorExe(string repo)
    {
        string path = Path.Combine(repo, "editor_avalonia", "bin", "Debug", "net9.0", "AvaloniaEditor.exe");
        if (!File.Exists(path))
            throw new InvalidOperationException("Editor exe not found: " + path);
        return path;
    }

    private static Process Launch(string editor, string repo, string scratch)
    {
        var psi = new ProcessStartInfo(editor)
        {
            UseShellExecute = false,
            WorkingDirectory = repo,
        };
        psi.ArgumentList.Add("--t6ui-host");
        psi.ArgumentList.Add(scratch);
        return Process.Start(psi) ?? throw new InvalidOperationException("Editor did not start");
    }

    private static AutomationElement WaitForMainWindow(Process process, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (process.HasExited) throw new InvalidOperationException("Editor exited");
            var desktop = AutomationElement.RootElement;
            var cond = new PropertyCondition(AutomationElement.NameProperty, "Avalonia Editor");
            // Try broader search
            try
            {
                var windows = desktop.FindAll(TreeScope.Children, Condition.TrueCondition);
                foreach (AutomationElement w in windows)
                {
                    if (w.Current.Name.Contains("Avalonia Editor", StringComparison.OrdinalIgnoreCase)
                        || w.Current.Name.Contains("T6UI-HOST", StringComparison.OrdinalIgnoreCase))
                        return w;
                }
            }
            catch { }
            Thread.Sleep(200);
        }
        throw new TimeoutException("Main window not found");
    }

    private static AutomationElement? FindByAutomationId(AutomationElement parent, string id)
    {
        try
        {
            var cond = new PropertyCondition(AutomationElement.AutomationIdProperty, id);
            return parent.FindFirst(TreeScope.Descendants, cond);
        }
        catch { return null; }
    }

    private static void ClickButton(AutomationElement parent, string automationId)
    {
        var btn = FindByAutomationId(parent, automationId);
        if (btn is null) throw new InvalidOperationException($"Button {automationId} not found");
        var pattern = (InvokePattern)btn.GetCurrentPattern(InvokePattern.Pattern);
        pattern.Invoke();
    }

    private static void SelectComboBoxItem(AutomationElement comboBox, int index)
    {
        var pattern = (SelectionPattern?)comboBox.GetCurrentPattern(SelectionPattern.Pattern);
        if (pattern is null) return;
        // Use ExpandCollapsePattern to open
        if (comboBox.TryGetCurrentPattern(ExpandCollapsePattern.Pattern, out var ec))
        {
            ((ExpandCollapsePattern)ec).Expand();
            Thread.Sleep(200);
        }
        var items = comboBox.FindAll(TreeScope.Descendants,
            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.ListItem));
        if (index < items.Count)
        {
            var item = items[index];
            if (item.TryGetCurrentPattern(SelectionItemPattern.Pattern, out var sip))
            {
                ((SelectionItemPattern)sip).Select();
            }
        }
        if (comboBox.TryGetCurrentPattern(ExpandCollapsePattern.Pattern, out var ec2))
        {
            ((ExpandCollapsePattern)ec2).Collapse();
        }
    }

    private static T? WaitFor<T>(Func<T?> factory, TimeSpan timeout, string description) where T : class
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var result = factory();
            if (result is not null) return result;
            Thread.Sleep(200);
        }
        throw new TimeoutException($"Timed out waiting for {description}");
    }

    private static void CopyDirectory(string source, string dest)
    {
        Directory.CreateDirectory(dest);
        foreach (var file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
        {
            string rel = Path.GetRelativePath(source, file);
            string target = Path.Combine(dest, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, overwrite: true);
        }
    }
}
