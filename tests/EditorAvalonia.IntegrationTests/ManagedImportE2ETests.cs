using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Windows.Automation;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EditorAvalonia.IntegrationTests;

/// <summary>
/// RF-4 后续（#141）：真实 editor_avalonia 进程中的导入 E2E。
/// 真实窗口 + 真实 Import 按钮 + 真实 Windows 文件选择器；不使用 ViewModel 旁路。
/// </summary>
[TestClass]
public sealed class ManagedImportE2ETests
{
    private const string ReadyTitle = "Avalonia Editor — M1IMPORT-HOST";
    private static readonly TimeSpan UiTimeout = TimeSpan.FromSeconds(40);

    [TestMethod]
    public async Task Managed_import_via_real_button_and_picker_commits_and_survives_restart()
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
            "engine_m1import_" + Guid.NewGuid().ToString("N"));
        string sourceDir = Path.Combine(Path.GetTempPath(),
            "engine_m1import_src_" + Guid.NewGuid().ToString("N"));
        string shortId = Guid.NewGuid().ToString("N").Substring(0, 8);
        string sourceName = "m1import_" + shortId + ".png";
        string conflictName = "conflict_" + shortId + ".png";
        string sourcePath = Path.Combine(sourceDir, sourceName);
        string conflictSourcePath = Path.Combine(sourceDir, conflictName);
        string importedRel = "assets/imported/tex/" + sourceName;
        string conflictTargetRel = "assets/imported/tex/" + conflictName;

        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(scratch);
            CopyDirectory(Path.Combine(repo, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Directory.CreateDirectory(sourceDir);
            File.Copy(Path.Combine(repo, "assets", "gp01", "tex", "pad.png"), sourcePath);
            File.Copy(Path.Combine(repo, "assets", "gp01", "tex", "pad.png"), conflictSourcePath);

            // 预置目标冲突：同名目标已存在，导入服务应拒绝且不提交。
            string conflictTarget = Path.Combine(scratch,
                conflictTargetRel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(conflictTarget)!);
            File.Copy(Path.Combine(repo, "assets", "gp01", "tex", "pad.png"),
                      conflictTarget, overwrite: true);

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectTab(main, "Asset Browser");
            Assert.AreEqual("33", ReadAssetCount(main), "initial asset count");

            ClickImport(main);
            await CompleteFileDialog(process, main, sourcePath);
            main = WaitForMainWindow(process, UiTimeout);
            SelectTab(main, "Console");
            WaitForText(main, "asset imported:", UiTimeout);
            SelectTab(main, "Asset Browser");
            Assert.AreEqual("34", ReadAssetCount(main), "asset count after import");

            string currentPath = Path.Combine(scratch, ".engine-content", "current");
            Assert.IsTrue(File.Exists(currentPath), "import must publish a current revision");
            string manifest = ReadCommittedManifest(scratch);
            Assert.IsTrue(manifest.Contains(importedRel, StringComparison.Ordinal),
                "committed manifest must contain " + importedRel + "\n" + manifest);
            string? guid = ExtractAssetGuid(manifest, importedRel);
            Assert.IsFalse(string.IsNullOrWhiteSpace(guid), "imported asset GUID must be assigned");

            SetSearch(main, sourceName);
            var importedTexts = ReadAssetListTexts(main);
            Assert.IsTrue(importedTexts.Any(t => t.Contains(importedRel, StringComparison.Ordinal)),
                "Asset Browser must show the imported path. Texts: " + Describe(importedTexts));
            Assert.IsTrue(importedTexts.Any(t => t.Contains(guid!, StringComparison.Ordinal)),
                "Asset Browser must show the imported GUID " + guid + ". Texts: " + Describe(importedTexts));
            int assetCountBeforeFailure = CountManifestAssets(manifest);
            SetSearch(main, "");
            Assert.AreEqual("34", ReadAssetCount(main), "asset count before failure path");

            byte[] currentBeforeFailure = File.ReadAllBytes(currentPath);
            ClickImport(main);
            await CompleteFileDialog(process, main, conflictSourcePath);
            main = WaitForMainWindow(process, UiTimeout);
            SelectTab(main, "Console");
            WaitForText(main, "IMPORT FAILED:", UiTimeout);
            SelectTab(main, "Asset Browser");
            CollectionAssert.AreEqual(currentBeforeFailure, File.ReadAllBytes(currentPath),
                "a rejected import must not change current");
            string manifestAfterFailure = ReadCommittedManifest(scratch);
            Assert.AreEqual(assetCountBeforeFailure, CountManifestAssets(manifestAfterFailure),
                "a rejected import must not add an asset");
            Assert.IsFalse(manifestAfterFailure.Contains(conflictTargetRel, StringComparison.Ordinal),
                "a rejected import must not appear in the committed manifest");
            Assert.AreEqual("34", ReadAssetCount(main), "asset count after rejected import");

            CloseProcess(ref process);
            process = Launch(editor, repo, scratch);
            main = WaitForMainWindow(process, UiTimeout);
            SelectTab(main, "Asset Browser");
            SetSearch(main, sourceName);
            var reopenedTexts = ReadAssetListTexts(main);
            Assert.IsTrue(reopenedTexts.Any(t => t.Contains(importedRel, StringComparison.Ordinal)),
                "imported asset must survive a process restart. Texts: " + Describe(reopenedTexts));
            Assert.IsTrue(reopenedTexts.Any(t => t.Contains(guid!, StringComparison.Ordinal)),
                "imported GUID must survive a process restart. Texts: " + Describe(reopenedTexts));

            Console.WriteLine("[M1IMPORT] PASS scratch=" + scratch);
            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed)
            {
                TryDelete(scratch);
                TryDelete(sourceDir);
            }
            else
            {
                Console.WriteLine("[M1IMPORT] SCRATCH PRESERVED: " + scratch);
                Console.WriteLine("[M1IMPORT] SOURCE PRESERVED: " + sourceDir);
            }
        }
    }

    // ── process / repo ─────────────────────────────────────────────

    private static string LocateRepoRoot()
    {
        string? env = Environment.GetEnvironmentVariable("M1IMPORT_REPO_ROOT");
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
        string? env = Environment.GetEnvironmentVariable("M1IMPORT_EDITOR_EXE");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env))
            return env;

        string config = Environment.GetEnvironmentVariable("M1IMPORT_CONFIG") ?? "Debug";
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
        psi.ArgumentList.Add("--m1import-host");
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

    private static string SafeAutomationId(AutomationElement element)
    {
        try { return element.Current.AutomationId ?? ""; }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { return ""; }
    }

    private static void SelectTab(AutomationElement root, string name)
    {
        AutomationElement tab = WaitFor(
            () => Descendants(root, ControlType.TabItem)
                .FirstOrDefault(e => SafeName(e).Trim() == name),
            UiTimeout, "tab '" + name + "'");
        if (tab.TryGetCurrentPattern(SelectionItemPattern.Pattern, out object? pattern) &&
            pattern is SelectionItemPattern selection)
            selection.Select();
        else
            ClickCenter(tab);
        Thread.Sleep(350);
    }

    private static void ClickImport(AutomationElement root)
    {
        AutomationElement button = WaitFor(
            () => FindByAutomationId(root, "ImportAssetBtn") ??
                  Descendants(root, ControlType.Button)
                      .FirstOrDefault(e => SafeName(e).Contains("Import", StringComparison.OrdinalIgnoreCase)),
            UiTimeout, "Import button");
        Invoke(button);
    }

    private static void SetSearch(AutomationElement root, string text)
    {
        AutomationElement? edit = FindByAutomationId(root, "AssetSearchBox");
        if (edit is null)
        {
            edit = Descendants(root, ControlType.Edit)
                .FirstOrDefault(e => SafeName(e).Contains("search assets", StringComparison.OrdinalIgnoreCase));
        }
        Assert.IsNotNull(edit, "asset search box was not found through UIA");
        if (!SetValue(edit!, text))
            throw new InvalidOperationException("asset search box does not expose ValuePattern");
        Thread.Sleep(350);
    }

    private static string ReadAssetCount(AutomationElement root)
    {
        AutomationElement? count = FindByAutomationId(root, "AssetCountText");
        if (count is not null)
        {
            var value = SafeName(count);
            if (!string.IsNullOrWhiteSpace(value)) return value.Trim();
        }
        var texts = ReadTexts(root);
        return texts.FirstOrDefault(t => t.Trim() == "33" || t.Trim() == "34")?.Trim() ?? "";
    }

    private static IReadOnlyList<string> ReadAssetListTexts(AutomationElement root)
    {
        AutomationElement? list = FindByAutomationId(root, "AssetList");
        if (list is null)
            throw new InvalidOperationException("AssetList was not found through UIA");
        var items = Descendants(list!, ControlType.ListItem);
        var texts = items.SelectMany(ReadTexts).Distinct().ToList();
        if (texts.Count == 0) texts = ReadTexts(list!).ToList();
        return texts;
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

    private static void WaitForText(AutomationElement root, string substring, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            ScrollConsoleToBottom(root);
            var texts = ReadTexts(root);
            if (texts.Any(t => t.Contains(substring, StringComparison.Ordinal)))
                return;
            Thread.Sleep(200);
        }
        throw new TimeoutException(
            "UI text '" + substring + "' did not appear. Visible texts: " + Describe(ReadTexts(root)));
    }

    private static void ScrollConsoleToBottom(AutomationElement root)
    {
        AutomationElement? list = FindByAutomationId(root, "ConsoleList");
        if (list is null) return;
        try
        {
            var items = Descendants(list!, ControlType.ListItem);
            if (items.Count > 0 &&
                items[^1].TryGetCurrentPattern(ScrollItemPattern.Pattern, out object? itemPattern) &&
                itemPattern is ScrollItemPattern scrollItem)
            {
                scrollItem.ScrollIntoView();
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        try
        {
            if (list!.TryGetCurrentPattern(ScrollPattern.Pattern, out object? scrollPattern) &&
                scrollPattern is ScrollPattern scroll)
            {
                if (scroll.Current.VerticallyScrollable)
                    scroll.SetScrollPercent(ScrollPattern.NoScroll, 100);
            }
            var rect = list.Current.BoundingRectangle;
            if (!rect.IsEmpty)
            {
                ClickPoint((int)(rect.Left + rect.Width / 2), (int)(rect.Top + rect.Height / 2));
                Thread.Sleep(120);
                SendCtrlEnd();
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        Thread.Sleep(250);
    }

    // ── real file dialog ───────────────────────────────────────────

    private static async Task CompleteFileDialog(Process process, AutomationElement main, string path)
    {
        AutomationElement dialog = WaitFor(
            () => FindFileDialog(process.Id, main),
            UiTimeout, "Windows file dialog for Import");
        Console.WriteLine("[M1IMPORT] dialog: " + DescribeElement(dialog));
        Console.WriteLine("[M1IMPORT] dialog tree:\n" + DescribeTree(dialog));
        bool set = SetFileName(dialog, path);
        Console.WriteLine("[M1IMPORT] SetFileName=" + set + " path=" + path);
        if (!set)
        {
            TypePathAndEnter(process, path);
            WaitForDialogClosed(process, main, dialog);
            return;
        }

        AutomationElement? open = Descendants(dialog, ControlType.Button)
            .FirstOrDefault(e => SafeAutomationId(e) == "1") ??
            Descendants(dialog, ControlType.Button)
                .FirstOrDefault(e => SafeName(e).Replace("&", "").Trim()
                    .Equals("Open", StringComparison.OrdinalIgnoreCase));
        Console.WriteLine("[M1IMPORT] buttons: " +
            string.Join(" || ", Descendants(dialog, ControlType.Button).Select(DescribeElement)));
        Console.WriteLine("[M1IMPORT] Open button: " +
            (open is null ? "<null>" : DescribeElement(open)));
        if (open is not null) Invoke(open);
        else AcceptFileDialog(dialog);
        WaitForDialogClosed(process, main, dialog);
        await Task.CompletedTask;
    }

    private static void AcceptFileDialog(AutomationElement dialog)
    {
        var buttons = Descendants(dialog, ControlType.Button);
        AutomationElement? confirm = buttons.FirstOrDefault(e =>
            SafeName(e).Contains("Open", StringComparison.OrdinalIgnoreCase) ||
            SafeName(e).Contains("打开", StringComparison.Ordinal) ||
            SafeName(e).Contains("OK", StringComparison.OrdinalIgnoreCase) ||
            SafeName(e).Contains("确定", StringComparison.Ordinal) ||
            SafeAutomationId(e) == "1");
        if (confirm is not null)
        {
            Console.WriteLine("[M1IMPORT] confirm button: " + DescribeElement(confirm));
            Invoke(confirm);
            return;
        }

        AutomationElement? edit = Descendants(dialog, ControlType.Edit)
            .FirstOrDefault(e => SafeAutomationId(e) == "1148");
        if (edit is not null)
        {
            try { edit.SetFocus(); } catch { }
            Thread.Sleep(200);
            SendEnter();
            return;
        }

        try
        {
            int hwnd = dialog.Current.NativeWindowHandle;
            if (hwnd != 0)
                PostMessage(new IntPtr(hwnd), WmCommand, new IntPtr(IdOk), IntPtr.Zero);
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
    }

    private static void WaitForDialogClosed(Process process, AutomationElement main, AutomationElement dialog)
    {
        if (WaitUntilClosed(process, main, TimeSpan.FromSeconds(10))) return;
        ClickOpenByLayout(dialog);
        if (WaitUntilClosed(process, main, TimeSpan.FromSeconds(10))) return;
        Console.WriteLine("[M1IMPORT] dialog did not close; current tree:\n" +
            DescribeTree(FindFileDialog(process.Id, main) ?? dialog));
        throw new TimeoutException("Timed out waiting for file dialog to close");
    }

    private static bool WaitUntilClosed(Process process, AutomationElement main, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (FindFileDialog(process.Id, main) is null) return true;
            Thread.Sleep(250);
        }
        return false;
    }

    private static void PostIdOk(AutomationElement dialog)
    {
        try
        {
            int hwnd = dialog.Current.NativeWindowHandle;
            if (hwnd != 0)
                PostMessage(new IntPtr(hwnd), WmCommand, new IntPtr(IdOk), IntPtr.Zero);
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
    }

    private static bool ClickOpenById(AutomationElement dialog)
    {
        try
        {
            int hwnd = dialog.Current.NativeWindowHandle;
            if (hwnd == 0) return false;
            IntPtr button = GetDlgItem(new IntPtr(hwnd), IdOk);
            if (button == IntPtr.Zero) return false;
            Console.WriteLine($"[M1IMPORT] GetDlgItem(IDOK)={button} enabled={IsWindowEnabled(button)}");
            SendMessage(button, BmClick, IntPtr.Zero, IntPtr.Zero);
            return true;
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
            return false;
        }
    }

    private static void ClickOpenByLayout(AutomationElement dialog)
    {
        var cancel = Descendants(dialog, ControlType.Button)
            .FirstOrDefault(e => SafeAutomationId(e) == "2" ||
                SafeName(e).Contains("Cancel", StringComparison.OrdinalIgnoreCase) ||
                SafeName(e).Contains("取消", StringComparison.Ordinal));
        if (cancel is null) return;
        try
        {
            var rect = cancel.Current.BoundingRectangle;
            if (rect.IsEmpty) return;
            // Standard file dialog: Open is the default button immediately left of Cancel.
            ClickPoint((int)(rect.Left - 52), (int)(rect.Top + rect.Height / 2));
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
    }

    private static void ClickPoint(int x, int y)
    {
        SetCursorPos(x, y);
        Thread.Sleep(120);
        mouse_event(MouseLeftDown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
    }

    private static string DescribeElement(AutomationElement element)
    {
        try
        {
            AutomationElement.AutomationElementInformation c = element.Current;
            return $"{c.ControlType.ProgrammaticName} name='{c.Name}' id='{c.AutomationId}' " +
                   $"class='{c.ClassName}' enabled={c.IsEnabled} rect={c.BoundingRectangle}";
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
            return "<element unavailable>";
        }
    }

    private static string DescribeTree(AutomationElement root)
    {
        var lines = new List<string>();
        try
        {
            foreach (AutomationElement element in
                     root.FindAll(TreeScope.Descendants, Condition.TrueCondition))
            {
                lines.Add("  " + DescribeElement(element));
                if (lines.Count >= 120) break;
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
            lines.Add("  <tree unavailable: " + ex.Message + ">");
        }
        return string.Join("\n", lines);
    }

    private static AutomationElement? FindFileDialog(int pid, AutomationElement main)
    {
        foreach (AutomationElement window in TopLevelWindows())
        {
            try
            {
                string name = window.Current.Name ?? "";
                string cls = window.Current.ClassName ?? "";
                if ((window.Current.ProcessId == pid || name == "Import asset") &&
                    (cls == "#32770" || name == "Import asset" || name == "Open"))
                    return window;
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }

        foreach (AutomationElement window in Descendants(main, ControlType.Window))
        {
            try
            {
                string name = window.Current.Name ?? "";
                string cls = window.Current.ClassName ?? "";
                if (cls == "#32770" || name == "Import asset" || name == "Open")
                    return window;
            }
            catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
            {
            }
        }
        return null;
    }

    private static bool SetFileName(AutomationElement dialog, string path)
    {
        var edits = Descendants(dialog, ControlType.Edit);
        Console.WriteLine("[M1IMPORT] edits: " +
            (edits.Count == 0 ? "<none>" : string.Join(" || ", edits.Select(DescribeElement))));
        AutomationElement? edit = edits.FirstOrDefault(e => SafeAutomationId(e) == "1148") ??
            edits.FirstOrDefault(e => SafeName(e).Contains("File name", StringComparison.OrdinalIgnoreCase)) ??
            edits.LastOrDefault();
        if (edit is null) return false;
        Console.WriteLine("[M1IMPORT] chosen edit: " + DescribeElement(edit));
        ActivateDialog(dialog);
        try
        {
            var rect = edit.Current.BoundingRectangle;
            if (!rect.IsEmpty)
            {
                int x = (int)(rect.Left + rect.Width / 2);
                int y = (int)(rect.Top + rect.Height / 2);
                ClickPoint(x, y);
                Thread.Sleep(250);
                ClickPoint(x, y);
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        Thread.Sleep(250);
        SendCtrlA();
        SendUnicode(path);
        Thread.Sleep(400);
        Console.WriteLine("[M1IMPORT] edit value after typing: " + ReadValue(edit));
        SendEnter();
        Thread.Sleep(250);
        return true;
    }

    private static void ActivateDialog(AutomationElement dialog)
    {
        try
        {
            int hwnd = dialog.Current.NativeWindowHandle;
            if (hwnd == 0) return;
            var handle = new IntPtr(hwnd);
            var before = GetForegroundWindow();
            var rect = dialog.Current.BoundingRectangle;
            if (!rect.IsEmpty)
                ClickPoint((int)(rect.Left + 100), (int)(rect.Top + 15));
            ShowWindow(handle, SwRestore);
            // A neutral ALT keystroke unlocks SetForegroundWindow for a background test host.
            keybd_event(VkMenu, 0, 0, UIntPtr.Zero);
            keybd_event(VkMenu, 0, KeyEventKeyUp, UIntPtr.Zero);
            bool set = SetForegroundWindow(handle);
            Thread.Sleep(300);
            Console.WriteLine($"[M1IMPORT] fg before={before} dialog={handle} set={set} " +
                              $"after={GetForegroundWindow()} visible={IsWindowVisible(handle)} " +
                              $"enabled={IsWindowEnabled(handle)}");
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
    }

    private static string ReadValue(AutomationElement element)
    {
        try
        {
            if (element.TryGetCurrentPattern(ValuePattern.Pattern, out object? pattern) &&
                pattern is ValuePattern valuePattern)
                return valuePattern.Current.Value;
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException or InvalidOperationException)
        {
        }
        return "<no value pattern>";
    }

    private static bool SetValue(AutomationElement element, string value)
    {
        try
        {
            if (element.TryGetCurrentPattern(ValuePattern.Pattern, out object? pattern) &&
                pattern is ValuePattern valuePattern)
            {
                valuePattern.SetValue(value);
                return true;
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException or InvalidOperationException)
        {
        }
        return false;
    }

    private static void Invoke(AutomationElement element)
    {
        try
        {
            if (element.TryGetCurrentPattern(InvokePattern.Pattern, out object? pattern) &&
                pattern is InvokePattern invoke)
            {
                invoke.Invoke();
                return;
            }
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException or InvalidOperationException)
        {
        }
        ClickCenter(element);
    }

    private static void ClickCenter(AutomationElement element)
    {
        var rect = element.Current.BoundingRectangle;
        if (rect.IsEmpty) throw new InvalidOperationException("element has no screen rectangle");
        int x = (int)(rect.Left + rect.Width / 2);
        int y = (int)(rect.Top + rect.Height / 2);
        SetCursorPos(x, y);
        Thread.Sleep(120);
        mouse_event(MouseLeftDown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
    }

    private static void TypePathAndEnter(Process process, string path)
    {
        IntPtr hwnd = process.MainWindowHandle;
        if (hwnd != IntPtr.Zero) SetForegroundWindow(hwnd);
        Thread.Sleep(250);
        SendCtrlA();
        SendUnicode(path);
        Thread.Sleep(250);
        SendEnter();
    }

    private static void TypeEnter(AutomationElement dialog)
    {
        try
        {
            int handle = dialog.Current.NativeWindowHandle;
            if (handle != 0) SetForegroundWindow(new IntPtr(handle));
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException)
        {
        }
        SendEnter();
    }

    private static void SendUnicode(string text)
    {
        var inputs = new List<Input>();
        foreach (char c in text)
        {
            inputs.Add(KeyInput(c, KeyEventUnicode));
            inputs.Add(KeyInput(c, KeyEventUnicode | KeyEventKeyUp));
        }
        if (inputs.Count > 0)
            SendInput((uint)inputs.Count, inputs.ToArray(), Marshal.SizeOf<Input>());
    }

    private static void SendCtrlA()
    {
        var inputs = new[]
        {
            KeyInputVk(0x11, 0),
            KeyInputVk(0x41, 0),
            KeyInputVk(0x41, KeyEventKeyUp),
            KeyInputVk(0x11, KeyEventKeyUp),
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
        Thread.Sleep(150);
    }

    private static void SendCtrlEnd()
    {
        var inputs = new[]
        {
            KeyInputVk(0x11, 0),
            KeyInputVk(0x23, 0),
            KeyInputVk(0x23, KeyEventKeyUp),
            KeyInputVk(0x11, KeyEventKeyUp),
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
    }

    private static Input KeyInputVk(ushort vk, uint flags) => new()
    {
        type = InputKeyboard,
        u = new InputUnion
        {
            ki = new KeyboardInput
            {
                wVk = vk,
                wScan = 0,
                dwFlags = flags,
                time = 0,
                dwExtraInfo = IntPtr.Zero,
            }
        }
    };

    private static void SendEnter()
    {
        var inputs = new[]
        {
            KeyInputVk(0x0D, 0),
            KeyInputVk(0x0D, KeyEventKeyUp),
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
    }

    private static void SendAltO()
    {
        var inputs = new[]
        {
            KeyInputVk(0x12, 0),
            KeyInputVk(0x4F, 0),
            KeyInputVk(0x4F, KeyEventKeyUp),
            KeyInputVk(0x12, KeyEventKeyUp),
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
    }

    private static Input KeyInput(ushort scan, uint flags) => new()
    {
        type = InputKeyboard,
        u = new InputUnion
        {
            ki = new KeyboardInput
            {
                wVk = 0,
                wScan = scan,
                dwFlags = flags,
                time = 0,
                dwExtraInfo = IntPtr.Zero,
            }
        }
    };

    // ── committed state ────────────────────────────────────────────

    private static string ReadCommittedManifest(string scratch)
    {
        string currentPath = Path.Combine(scratch, ".engine-content", "current");
        using var current = JsonDocument.Parse(File.ReadAllText(currentPath));
        string relative = current.RootElement.GetProperty("manifest").GetString()
                          ?? throw new InvalidOperationException("current.manifest missing");
        string manifestPath = Path.Combine(scratch, ".engine-content",
            relative.Replace('/', Path.DirectorySeparatorChar));
        Assert.IsTrue(File.Exists(manifestPath), "committed manifest missing: " + manifestPath);
        return File.ReadAllText(manifestPath);
    }

    private static string? ExtractAssetGuid(string manifest, string path)
    {
        using var doc = JsonDocument.Parse(manifest);
        foreach (JsonElement asset in doc.RootElement.GetProperty("assets").EnumerateArray())
        {
            if (asset.GetProperty("path").GetString() == path)
                return asset.GetProperty("guid").GetString();
        }
        return null;
    }

    private static int CountManifestAssets(string manifest)
    {
        using var doc = JsonDocument.Parse(manifest);
        return doc.RootElement.GetProperty("assets").GetArrayLength();
    }

    // ── helpers ────────────────────────────────────────────────────

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
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); }
        catch (Exception ex) { Console.WriteLine("[M1IMPORT] cleanup skipped " + path + ": " + ex.Message); }
    }

    // ── Win32 ──────────────────────────────────────────────────────

    private const uint MouseLeftDown = 0x0002;
    private const uint MouseLeftUp = 0x0004;
    private const uint InputKeyboard = 1;
    private const uint KeyEventKeyUp = 0x0002;
    private const uint KeyEventUnicode = 0x0004;
    private const uint WmCommand = 0x0111;
    private const int IdOk = 1;
    private const uint BmClick = 0x00F5;
    private const int SwRestore = 9;
    private const byte VkMenu = 0x12;

    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    private static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, Input[] pInputs, int cbSize);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint type;
        public InputUnion u;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MouseInput mi;
        [FieldOffset(0)] public KeyboardInput ki;
        [FieldOffset(0)] public HardwareInput hi;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HardwareInput
    {
        public uint uMsg;
        public ushort wParamL;
        public ushort wParamH;
    }
}
