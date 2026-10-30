using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Automation;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EditorAvalonia.IntegrationTests;

/// <summary>
/// T-6-A：真实 editor_avalonia 进程中的 Console 过滤与清空。
/// 真实 ConsoleFilterBox / ConsoleClearBtn / ConsoleList；不使用 ViewModel 旁路。
/// </summary>
[TestClass]
public sealed class T6ConsoleUITests
{
    private const string ReadyTitle = "Avalonia Editor — T6UI-HOST";
    private static readonly TimeSpan UiTimeout = TimeSpan.FromSeconds(40);

    [TestMethod]
    public void Managed_console_filter_and_clear_use_real_controls()
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
            "engine_t6ui_" + Guid.NewGuid().ToString("N"));

        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(scratch);
            CopyDirectory(Path.Combine(repo, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectTab(main, "Console");
            main = WaitForMainWindow(process, UiTimeout);
            _ = WaitFor(() => FindByAutomationId(main, "ConsoleList"),
                UiTimeout, "ConsoleList");

            WaitForConsoleText(main, "[project] opened:", UiTimeout);
            WaitForConsoleText(main, "T6UI-HOST] ready", UiTimeout);

            ClickButton(main, "ToolbarCreateEntityBtn");
            WaitForConsoleText(main, "entity created:", UiTimeout);

            SetFilter(main, "entity created");
            WaitUntil(() =>
            {
                var texts = ReadConsoleTexts(main);
                return texts.Count > 0 &&
                       texts.All(t => t.Contains("entity created", StringComparison.OrdinalIgnoreCase));
            }, UiTimeout, "console filtered to matching lines only");

            var filtered = ReadConsoleTexts(main);
            Assert.IsFalse(filtered.Any(t => t.Contains("[project] opened:", StringComparison.Ordinal)),
                "the filter must hide non-matching boundary lines: " + Describe(filtered));
            Assert.IsTrue(filtered.Any(t => t.Contains("entity created:", StringComparison.OrdinalIgnoreCase)),
                "the filtered view must keep matching lines: " + Describe(filtered));

            ClickButton(main, "ConsoleClearBtn");
            WaitUntil(() => ReadConsoleTexts(main).Count == 0,
                UiTimeout, "real clear button empties the console list");
            Assert.AreEqual("entity created", ReadFilterText(main),
                "clear must retain the current filter text");

            ClickButton(main, "ToolbarCreateEntityBtn");
            WaitForConsoleText(main, "entity created:", UiTimeout);
            var afterClear = ReadConsoleTexts(main);
            Assert.IsTrue(afterClear.Any(t => t.Contains("entity created:", StringComparison.OrdinalIgnoreCase)),
                "post-clear events must pass the retained filter: " + Describe(afterClear));
            Assert.IsFalse(afterClear.Any(t => t.Contains("[project] opened:", StringComparison.Ordinal)),
                "the retained filter must still hide non-matching lines: " + Describe(afterClear));

            SetFilter(main, "");
            WaitForConsoleText(main, "EV EntityCreated:", UiTimeout);
            var restored = ReadConsoleTexts(main);
            Assert.IsTrue(restored.Any(t => t.Contains("EV EntityCreated:", StringComparison.Ordinal)),
                "clearing the filter must restore the full view: " + Describe(restored));

            Console.WriteLine("[T6UI] PASS scratch=" + scratch);
            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[T6UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    // ── process / repo ─────────────────────────────────────────────

    private static string LocateRepoRoot()
    {
        string? env = Environment.GetEnvironmentVariable("T6UI_REPO_ROOT");
        if (!string.IsNullOrWhiteSpace(env) &&
            File.Exists(Path.Combine(env, "assets", "gp01", "manifest.json")))
            return env;

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
        {
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir.FullName;
        }
        throw new InvalidOperationException("repo root with assets/gp01 was not found");
    }

    private static string LocateEditorExe(string repo)
    {
        string? env = Environment.GetEnvironmentVariable("T6UI_EDITOR_EXE");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env))
            return env;

        string config = Environment.GetEnvironmentVariable("T6UI_CONFIG") ?? "Debug";
        string path = Path.Combine(repo, "editor_avalonia", "bin", config,
            "net9.0", "AvaloniaEditor.exe");
        if (!File.Exists(path))
            throw new InvalidOperationException(
                "AvaloniaEditor.exe not found: " + path +
                ". Build it first: dotnet build editor_avalonia/AvaloniaEditor.csproj -c " + config);
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
        return Process.Start(psi)
               ?? throw new InvalidOperationException("AvaloniaEditor process did not start");
    }

    private static AutomationElement WaitForMainWindow(Process process, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (process.HasExited)
                throw new InvalidOperationException(
                    "AvaloniaEditor exited before ready, exit=" + process.ExitCode);
            foreach (AutomationElement window in TopLevelWindows())
            {
                try
                {
                    if (window.Current.ProcessId == process.Id &&
                        window.Current.Name == ReadyTitle)
                        return window;
                }
                catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
                {
                }
            }
            Thread.Sleep(200);
        }
        throw new TimeoutException("AvaloniaEditor main window did not reach '" + ReadyTitle + "'");
    }

    private static void CloseProcess(ref Process? process)
    {
        if (process is null) return;
        try
        {
            if (!process.HasExited)
            {
                process.CloseMainWindow();
                if (!process.WaitForExit(5000))
                    process.Kill(entireProcessTree: true);
            }
        }
        catch
        {
            try { process.Kill(entireProcessTree: true); } catch { }
        }
        finally
        {
            process.Dispose();
            process = null;
        }
    }

    // ── UI automation ──────────────────────────────────────────────

    private static IEnumerable<AutomationElement> TopLevelWindows()
    {
        AutomationElementCollection? windows = null;
        try
        {
            windows = AutomationElement.RootElement.FindAll(TreeScope.Children, Condition.TrueCondition);
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        if (windows is null) yield break;
        foreach (AutomationElement element in windows) yield return element;
    }

    private static IReadOnlyList<AutomationElement> Descendants(AutomationElement root, ControlType type)
    {
        var result = new List<AutomationElement>();
        try
        {
            var all = root.FindAll(TreeScope.Descendants,
                new PropertyCondition(AutomationElement.ControlTypeProperty, type));
            foreach (AutomationElement element in all) result.Add(element);
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        return result;
    }

    private static AutomationElement? FindByAutomationId(AutomationElement root, string id)
    {
        try
        {
            return root.FindFirst(TreeScope.Descendants,
                new PropertyCondition(AutomationElement.AutomationIdProperty, id));
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
            return null;
        }
    }

    private static string SafeName(AutomationElement element)
    {
        try { return element.Current.Name ?? ""; }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { return ""; }
    }

    private static void SelectTab(AutomationElement root, string name)
    {
        var deadline = DateTime.UtcNow + UiTimeout;
        while (DateTime.UtcNow < deadline)
        {
            AutomationElement? tab = Descendants(root, ControlType.TabItem)
                .FirstOrDefault(e => SafeName(e).Trim() == name);
            if (tab is not null &&
                tab.TryGetCurrentPattern(SelectionItemPattern.Pattern, out object? pattern) &&
                pattern is SelectionItemPattern selection)
            {
                selection.Select();
                Thread.Sleep(350);
                try
                {
                    if (selection.Current.IsSelected) return;
                }
                catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
                {
                }
            }
            Thread.Sleep(200);
        }
        throw new TimeoutException("tab '" + name + "' could not be selected");
    }

    private static void ClickButton(AutomationElement root, string automationId)
    {
        AutomationElement button = WaitFor(
            () => FindByAutomationId(root, automationId),
            UiTimeout, "button " + automationId);
        if (button.TryGetCurrentPattern(InvokePattern.Pattern, out object? pattern) &&
            pattern is InvokePattern invoke)
        {
            invoke.Invoke();
        }
        else
        {
            throw new InvalidOperationException("button does not expose InvokePattern: " + automationId);
        }
        Thread.Sleep(350);
    }

    private static void SetFilter(AutomationElement root, string text)
    {
        AutomationElement edit = WaitFor(
            () => FindByAutomationId(root, "ConsoleFilterBox"),
            UiTimeout, "ConsoleFilterBox");
        if (!edit.TryGetCurrentPattern(ValuePattern.Pattern, out object? pattern) ||
            pattern is not ValuePattern valuePattern)
            throw new InvalidOperationException("ConsoleFilterBox does not expose ValuePattern");
        valuePattern.SetValue(text);
        Thread.Sleep(350);
    }

    private static string ReadFilterText(AutomationElement root)
    {
        AutomationElement? edit = FindByAutomationId(root, "ConsoleFilterBox");
        if (edit is null) return "";
        try
        {
            if (edit.TryGetCurrentPattern(ValuePattern.Pattern, out object? pattern) &&
                pattern is ValuePattern valuePattern)
                return valuePattern.Current.Value;
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException or InvalidOperationException)
        {
        }
        return "";
    }

    private static IReadOnlyList<string> ReadConsoleTexts(AutomationElement root)
    {
        AutomationElement? list = FindByAutomationId(root, "ConsoleList");
        if (list is null) return Array.Empty<string>();
        try
        {
            // UIA only exposes realized items in a virtualized ListBox. Read both
            // ends so a bottom scroll does not hide an earlier lifecycle line.
            var itemNames = new HashSet<string>(StringComparer.Ordinal);
            var textNames = new HashSet<string>(StringComparer.Ordinal);
            ScrollConsoleTo(list, 0);
            CollectConsoleTexts(list, itemNames, textNames);
            ScrollConsoleTo(list, 100);
            CollectConsoleTexts(list, itemNames, textNames);
            return (itemNames.Count > 0 ? itemNames : textNames).ToList();
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException
                                       or OperationCanceledException)
        {
            return Array.Empty<string>();
        }
    }

    private static void CollectConsoleTexts(
        AutomationElement list, HashSet<string> itemNames, HashSet<string> textNames)
    {
        // The Avalonia provider can return an empty set for a ControlType
        // PropertyCondition on a virtualized ListBox; TrueCondition works.
        foreach (AutomationElement element in
                 list.FindAll(TreeScope.Descendants, Condition.TrueCondition))
        {
            try
            {
                string name = element.Current.Name ?? "";
                if (string.IsNullOrWhiteSpace(name)) continue;
                if (element.Current.ControlType == ControlType.ListItem) itemNames.Add(name);
                else if (element.Current.ControlType == ControlType.Text) textNames.Add(name);
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }
    }

    private static IReadOnlyList<string> ReadTexts(AutomationElement root)
    {
        var result = new List<string>();
        try
        {
            foreach (AutomationElement element in
                     root.FindAll(TreeScope.Descendants, Condition.TrueCondition))
            {
                try
                {
                    string name = element.Current.Name ?? "";
                    if (!string.IsNullOrWhiteSpace(name)) result.Add(name);
                }
                catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
                {
                }
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        return result.Distinct().ToList();
    }

    private static void ScrollConsoleTo(AutomationElement list, double percent)
    {
        try
        {
            if (list.TryGetCurrentPattern(ScrollPattern.Pattern, out object? pattern) &&
                pattern is ScrollPattern scroll &&
                scroll.Current.VerticallyScrollable)
            {
                scroll.SetScrollPercent(ScrollPattern.NoScroll, percent);
            }
            else
            {
                AutomationElement? bar = FindByAutomationId(list, "PART_VerticalScrollBar");
                if (bar is not null &&
                    bar.TryGetCurrentPattern(RangeValuePattern.Pattern, out object? barPattern) &&
                    barPattern is RangeValuePattern range)
                {
                    range.SetValue(percent <= 0 ? range.Current.Minimum : range.Current.Maximum);
                }
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException
                                       or InvalidOperationException)
        {
        }
        Thread.Sleep(120);
    }

    private static void WaitForConsoleText(AutomationElement root, string substring, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        IReadOnlyList<string> last = Array.Empty<string>();
        while (DateTime.UtcNow < deadline)
        {
            last = ReadConsoleTexts(root);
            if (last.Any(t => t.Contains(substring, StringComparison.Ordinal))) return;
            Thread.Sleep(200);
        }
        throw new TimeoutException("Timed out waiting for console text '" + substring +
            "'. Last console texts: " + Describe(last));
    }

    private static void WaitUntil(Func<bool> probe, TimeSpan timeout, string description)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (probe()) return;
            Thread.Sleep(200);
        }
        throw new TimeoutException("Timed out waiting for " + description);
    }

    private static T WaitFor<T>(Func<T?> probe, TimeSpan timeout, string description)
        where T : class
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            T? value = probe();
            if (value is not null) return value;
            Thread.Sleep(200);
        }
        throw new TimeoutException("Timed out waiting for " + description);
    }

    private static string Describe(IEnumerable<string> texts)
        => "[" + string.Join(" | ", texts.Take(80)) + "]";

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (string file in Directory.GetFiles(source))
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), overwrite: true);
        foreach (string dir in Directory.GetDirectories(source))
            CopyDirectory(dir, Path.Combine(target, Path.GetFileName(dir)));
    }

    private static void TryDelete(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); } catch { }
    }
}
