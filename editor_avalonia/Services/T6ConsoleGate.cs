using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using AvaloniaEditor.Native;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// T-6-A Console 的真实窗口门禁：D-5 状态表、容量、清空、过滤与工程边界行。
/// 使用临时 scratch 工程，不写 repo fixture，不使用测试专用旁路。
/// </summary>
public static class T6ConsoleGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[T6A] start");
            RunTrackerStateTable();

            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_t6a_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));

            long generationBeforeFirstOpen = host.Generation;
            int droppedBeforeFirstOpen = host.DroppedLateEventCount;
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "managed editor opened the scratch project");
            await SettleAsync();
            Check(host.LastOpenOutcome is { NativeCommitted: true, CallSucceeded: true },
                  "successful open reports native committed + call success");
            Check(host.Generation == generationBeforeFirstOpen + 1,
                  "successful open commits exactly one candidate generation");
            Check(host.DroppedLateEventCount == droppedBeforeFirstOpen,
                  "EV_PROJECT_LOADED is not counted as dropped");
            Check(CountLines(vm, "EV ProjectLoaded:") == 1,
                  "EV_PROJECT_LOADED is displayed exactly once");
            Check(CountLines(vm, "[project] opened:") == 1,
                  "success boundary is appended exactly once");
            log("[T6A-05] successful open commits generation and shows one boundary + one ProjectLoaded");

            long generationAfterFirstOpen = host.Generation;
            int droppedBeforeStale = host.DroppedLateEventCount;
            host.PostEventWithGeneration(generationBeforeFirstOpen,
                                         EditorBridgeApi.EvEntityCreated, "t6-stale-event");
            await SettleAsync();
            Check(host.DroppedLateEventCount > droppedBeforeStale,
                  "old generation event is dropped after a successful switch");
            Check(!vm.ConsoleLines.Any(l => l.Contains("t6-stale-event", StringComparison.Ordinal)),
                  "dropped stale event is not displayed");
            log("[T6A-06] stale generation is rejected and not displayed");

            host.PostEventWithGeneration(generationAfterFirstOpen,
                                         EditorBridgeApi.EvEntityCreated, "t6-seq-1");
            host.PostEventWithGeneration(generationAfterFirstOpen,
                                         EditorBridgeApi.EvEntityCreated, "t6-seq-2");
            await SettleAsync();
            int seq1 = IndexOfLine(vm, "t6-seq-1");
            int seq2 = IndexOfLine(vm, "t6-seq-2");
            Check(seq1 >= 0 && seq2 > seq1, "marshalled event order is preserved");
            log("[T6A-09] marshalled event order is preserved");

            long generationBeforeFailure = host.Generation;
            int boundaryBeforeFailure = CountLines(vm, "[project] opened:");
            int loadedBeforeFailure = CountLines(vm, "EV ProjectLoaded:");
            Check(!vm.OpenProjectRooted(scratch,
                                        "assets/gp01/does-not-exist.scene",
                                        "assets/gp01/Main.scene"),
                  "pre-load failure is reported as failure");
            await SettleAsync();
            Check(host.Generation == generationBeforeFailure,
                  "pre-load failure keeps the old generation");
            Check(host.LastOpenOutcome is { NativeCommitted: false },
                  "pre-load failure reports native not committed");
            Check(vm.IsOpen, "old project remains open after pre-load failure");
            Check(CountLines(vm, "[project] opened:") == boundaryBeforeFailure,
                  "pre-load failure adds no success boundary");
            Check(CountLines(vm, "EV ProjectLoaded:") == loadedBeforeFailure,
                  "pre-load failure adds no ProjectLoaded event");
            log("[T6A-07] pre-load failure preserves generation, project and history");

            long generationBeforeClose = host.Generation;
            int droppedBeforeTeardown = host.DroppedLateEventCount;
            vm.ResetSession();
            Check(host.Generation > generationBeforeClose,
                  "close/teardown bumps the managed generation");
            host.PostEventWithGeneration(generationBeforeClose,
                                         EditorBridgeApi.EvEntityCreated, "t6-teardown-event");
            await SettleAsync();
            Check(host.DroppedLateEventCount == droppedBeforeTeardown,
                  "teardown suppression is not counted as a generation drop");
            Check(!vm.ConsoleLines.Any(l => l.Contains("t6-teardown-event", StringComparison.Ordinal)),
                  "teardown event is not displayed");
            log("[T6A-10] teardown suppresses events without changing dropped counter");

            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "managed editor reopened the scratch project");
            await SettleAsync();
            Check(CountLines(vm, "[project] opened:") == 2,
                  "reopen appends exactly one more boundary");
            Check(CountLines(vm, "EV ProjectLoaded:") == 2,
                  "reopen displays exactly one more ProjectLoaded event");
            log("[T6A-08] repeated open/close delivers each event once");

            // D-2 / T6-A-01：原始缓存与视图都受 10,000 行上限约束。
            bool dirtyBeforeConsoleWork = host.IsDirty();
            var countsBeforeConsoleWork = host.GetCounts();
            vm.ClearConsole();
            Check(vm.ConsoleLines.Count == 0 && vm.ConsoleView.Count == 0,
                  "clear empties raw cache and view");
            Check(host.IsDirty() == dirtyBeforeConsoleWork,
                  "clear does not change host dirty state");
            Check(host.GetCounts() == countsBeforeConsoleWork,
                  "clear does not change host project data");
            for (int i = 0; i < MainViewModel.ConsoleCapacity + 5; i++)
                vm.LogToConsole("cap-" + i);
            Check(vm.ConsoleLines.Count == MainViewModel.ConsoleCapacity,
                  "raw console cache is capped at 10,000 lines");
            Check(vm.ConsoleView.Count == MainViewModel.ConsoleCapacity,
                  "unfiltered view reflects the capped cache");
            Check(vm.ConsoleLines[0].Contains("cap-5", StringComparison.Ordinal),
                  "oldest lines are trimmed first");
            Check(vm.ConsoleLines[^1].Contains("cap-10004", StringComparison.Ordinal),
                  "newest line is retained");
            Check(vm.ConsoleTrimmedCount == 5, "trim counter records dropped lines");
            var consoleList = window.FindControl<ListBox>("ConsoleList");
            Check(consoleList is not null, "ConsoleList control exists");
            Check(ReferenceEquals(consoleList!.ItemsSource, vm.ConsoleView),
                  "ConsoleList binds the filtered view");
            log("[T6A-01] capacity trims oldest lines and keeps raw/view bounded");

            // D-4 / T6-A-03/04/12：真实 TextBox 过滤 + 真实按钮清空。
            vm.ClearConsole();
            vm.ConsoleFilter = string.Empty;
            vm.LogToConsole("alpha one");
            vm.LogToConsole("beta two");
            vm.LogToConsole("ALPHA three");
            Check(vm.ConsoleView.Count == 3, "unfiltered view contains all lines");

            var filterBox = window.FindControl<TextBox>("ConsoleFilterBox");
            Check(filterBox is not null, "ConsoleFilterBox control exists");
            filterBox!.Text = "alpha";
            await SettleAsync();
            Check(vm.ConsoleFilter == "alpha", "real TextBox updated ConsoleFilter");
            Check(vm.ConsoleLines.Count == 3, "filter does not modify raw cache");
            Check(vm.ConsoleView.Count == 2, "filter view is case-insensitive substring");
            Check(vm.ConsoleView.All(l => l.Contains("alpha", StringComparison.OrdinalIgnoreCase)),
                  "filter view contains only matching lines");

            filterBox.Text = string.Empty;
            await SettleAsync();
            Check(vm.ConsoleView.Count == 3, "clearing the filter restores the full view");

            // 匹配对象是消息文本，不是时间戳前缀。
            filterBox.Text = ":";
            await SettleAsync();
            Check(vm.ConsoleLines.Count == 3 && vm.ConsoleView.Count == 0,
                  "filter does not match the timestamp prefix");

            filterBox.Text = "ALPHA";
            await SettleAsync();
            Check(vm.ConsoleView.Count == 2, "filter remains case-insensitive");

            Click(window, "ConsoleClearBtn");
            Check(vm.ConsoleLines.Count == 0 && vm.ConsoleView.Count == 0,
                  "real Clear button empties raw cache and view");
            Check(vm.ConsoleFilter == "ALPHA", "clear retains the filter text");

            vm.LogToConsole("alpha after clear");
            vm.LogToConsole("beta after clear");
            vm.LogToConsole("ALPHA again");
            Check(vm.ConsoleLines.Count == 3, "post-clear lines are appended to raw cache");
            Check(vm.ConsoleView.Count == 2, "post-clear view still applies the retained filter");
            Check(vm.ConsoleView.All(l => l.Contains("alpha", StringComparison.OrdinalIgnoreCase)),
                  "post-clear view contains only retained-filter matches");
            log("[T6A-02/03/04/12] real filter and clear controls behave per frozen contract");

            log("[T6A] all real-window checks passed");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[T6A] FAIL: " + ex);
            log("[T6A] FAIL: " + ex.Message);
            return 1;
        }
        finally
        {
            try { vm.ResetSession(); } catch { }
            if (scratch is not null)
            {
                try { Directory.Delete(scratch, recursive: true); } catch { }
            }
        }
    }

    private static void RunTrackerStateTable()
    {
        // 成功：pre-commit 事件属于旧代次；marker 及其后事件属于候选代次。
        var success = new OpenAttemptTracker(7);
        var successAttempt = success.Begin();
        var pre = success.Admit(EditorBridgeApi.EvEntityCreated, "old");
        Check(!pre.IsQueued && pre.Generation == 7, "pre-commit event belongs to active generation");
        var marker = success.Admit(EditorBridgeApi.EvProjectLoaded, "objects=1;assets=1");
        Check(marker.IsQueued && marker.Generation == 8, "ProjectLoaded is queued as candidate");
        var post = success.Admit(EditorBridgeApi.EvEntityCreated, "new");
        Check(post.IsQueued && post.Generation == 8, "post-commit event is queued as candidate");
        var successResult = success.Resolve(successAttempt, callSucceeded: true);
        Check(successResult.Committed && !successResult.ProtocolViolated,
              "success commits the candidate generation");
        Check(successResult.PendingEvents.Count == 2, "candidate events are released on commit");
        Check(success.ActiveGeneration == 8, "active generation advances exactly once");

        // 加载前失败：abort，旧代次保持不变。
        var abort = new OpenAttemptTracker(7);
        var abortAttempt = abort.Begin();
        abort.Admit(EditorBridgeApi.EvEntityCreated, "old");
        var abortResult = abort.Resolve(abortAttempt, callSucceeded: false);
        Check(!abortResult.Committed && !abortResult.ProtocolViolated,
              "pre-load failure aborts without protocol violation");
        Check(abort.ActiveGeneration == 7, "pre-load failure keeps the old generation");

        // 加载后 GL 失败：marker 已到，仍提交候选代次。
        var glFail = new OpenAttemptTracker(7);
        var glAttempt = glFail.Begin();
        glFail.Admit(EditorBridgeApi.EvProjectLoaded, "objects=1;assets=1");
        var glResult = glFail.Resolve(glAttempt, callSucceeded: false);
        Check(glResult.Committed, "post-load failure still commits the native switch");
        Check(glFail.ActiveGeneration == 8, "post-load failure advances the generation");

        // 成功但无 marker：协议违约，不提交。
        var violation = new OpenAttemptTracker(7);
        var violationAttempt = violation.Begin();
        var violationResult = violation.Resolve(violationAttempt, callSucceeded: true);
        Check(violationResult.ProtocolViolated && !violationResult.Committed,
              "success without ProjectLoaded is a protocol violation");
        Check(violation.ActiveGeneration == 7, "protocol violation does not commit");

        // 防重复打开。
        var doubleOpen = new OpenAttemptTracker(7);
        var first = doubleOpen.Begin();
        bool rejected = false;
        try { doubleOpen.Begin(); }
        catch (InvalidOperationException) { rejected = true; }
        Check(rejected, "a second concurrent open attempt is rejected");
        doubleOpen.Resolve(first, callSucceeded: false);

        // teardown 丢弃候选待决事件。
        var teardown = new OpenAttemptTracker(7);
        teardown.Begin();
        teardown.Admit(EditorBridgeApi.EvProjectLoaded, "objects=1;assets=1");
        var teardownResult = teardown.BeginTeardown();
        Check(teardownResult.DroppedPending == 1, "teardown drops pending candidate events");
        Check(teardown.ActiveGeneration == 8, "teardown bumps the active generation");

        // 无尝试时的 marker 是协议异常，但不得崩溃。
        var unexpected = new OpenAttemptTracker(7);
        var unexpectedAdmission = unexpected.Admit(EditorBridgeApi.EvProjectLoaded, "");
        Check(!unexpectedAdmission.IsQueued && unexpected.UnexpectedProjectLoadedCount == 1,
              "unexpected ProjectLoaded is diagnosed, not queued");

        Console.WriteLine("[T6A] tracker state table PASS");
    }

    private static async Task SettleAsync() => await Task.Delay(350);

    private static int CountLines(MainViewModel vm, string substring)
        => vm.ConsoleLines.Count(l => l.Contains(substring, StringComparison.Ordinal));

    private static int IndexOfLine(MainViewModel vm, string substring)
    {
        for (int i = 0; i < vm.ConsoleLines.Count; i++)
            if (vm.ConsoleLines[i].Contains(substring, StringComparison.Ordinal)) return i;
        return -1;
    }

    private static void Click(MainWindow window, string name)
    {
        var button = window.FindControl<Button>(name);
        Check(button is not null, $"button '{name}' was found");
        button!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
    }

    private static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source))
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), overwrite: true);
        foreach (var dir in Directory.GetDirectories(source))
            CopyDirectory(dir, Path.Combine(target, Path.GetFileName(dir)));
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}

/// <summary>
/// T-6 UIA 测试的 host harness：只打开 scratch 工程并设置可识别的标题；
/// 过滤/清空仍由真实控件驱动，不提供任何测试旁路。
/// </summary>
public static class T6ConsoleHostBootstrap
{
    public const string Flag = "--t6ui-host";
    public const string ReadyTitle = "Avalonia Editor — T6UI-HOST";

    public static string? FindRoot(string[] args)
    {
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], Flag, StringComparison.OrdinalIgnoreCase))
                return args[i + 1];
        }
        return null;
    }

    /// <returns>0 = ready (keep running); non-zero = startup failure.</returns>
    public static int Run(MainWindow window, MainViewModel vm, Action<string> log, string? root)
    {
        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
        {
            log($"[T6UI-HOST] invalid scratch root: {root ?? "<missing>"}");
            return 2;
        }

        if (!vm.OpenProjectRooted(root, "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        {
            log($"[T6UI-HOST] open failed: {root}");
            return 3;
        }

        window.Title = ReadyTitle;
        vm.LogToConsole($"[T6UI-HOST] ready: {root}");
        log($"[T6UI-HOST] ready: {root}");
        return 0;
    }
}
