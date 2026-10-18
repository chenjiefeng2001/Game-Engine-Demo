using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using AvaloniaEditor.Native;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// 阶段 A / A2：F-2 bootstrap trigger contract 证据（managed-only，native 零改动）。
///
/// 场景由**进程启动时**已存在的 native 注入环境变量决定（MSVC <c>getenv</c> 读的是 CRT 启动快照，
/// .NET 运行期 <c>SetEnvironmentVariable</c> 对 native 不可见，因此每个注入场景必须独立进程）。
///
/// <code>
/// 场景                     注入 env                                    期望
/// happy（无注入）          —                                          F2-01/02/03/04/06/07/08/09/10
/// bootstrap-app-init       EDITOR_BRIDGE_FAIL_APPLICATION_INIT         InitFailed / EngineSubsystem
/// bootstrap-fbo            EDITOR_BRIDGE_FAIL_FBO                      GLFailed / FBO
/// bootstrap-session        EDITOR_BRIDGE_FAIL_SESSION_ATTACH           SessionFailed / Session
/// frame-fault              EDITOR_BRIDGE_FAIL_FRAME                    TickFault / Tick
/// tick-fault               EDITOR_BRIDGE_FAIL_RUNTIME_TICK             TickFault / Tick（运行中）
/// play-bind-fault          EDITOR_BRIDGE_FAIL_PLAY_BIND                SessionFailed / Play
/// </code>
///
/// 每个场景都必须证明：typed 失败可观测、UI 不崩、runtime 不可用后显式拒绝、
/// 终态无复活、UI 线程零未处理异常、targeted process 归零。
/// </summary>
public static class BootstrapTriggerGate
{
    private const string GateName = "[HRC-F2 GATE]";
    private const int ProbeTimeoutMs = 6000;

    private enum Scenario
    {
        Happy,
        BootstrapAppInit,
        BootstrapFbo,
        BootstrapSession,
        FrameFault,
        TickFault,
        PlayBindFault,
    }

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        var unhandled = new List<string>();
        InstallUnhandledProbe(kind => unhandled.Add(kind));
        var scenario = DetectScenario();
        try
        {
            log($"{GateName} scenario={scenario}");
            switch (scenario)
            {
                case Scenario.BootstrapAppInit:
                    await BootstrapFailureAsync(window, host, vm, log,
                        EditorBridgeApi.EngineHostStatusInitFailed, EditorBridgeApi.EngineHostStageEngineSubsystem);
                    break;
                case Scenario.BootstrapFbo:
                    await BootstrapFailureAsync(window, host, vm, log,
                        EditorBridgeApi.EngineHostStatusGlFailed, EditorBridgeApi.EngineHostStageFbo);
                    break;
                case Scenario.BootstrapSession:
                    await BootstrapFailureAsync(window, host, vm, log,
                        EditorBridgeApi.EngineHostStatusSessionFailed, EditorBridgeApi.EngineHostStageSession);
                    break;
                case Scenario.FrameFault:
                    await PumpFaultAsync(window, host, vm, log, playFirst: false);
                    break;
                case Scenario.TickFault:
                    await PumpFaultAsync(window, host, vm, log, playFirst: true);
                    break;
                case Scenario.PlayBindFault:
                    await PlayFaultAsync(window, host, vm, log);
                    break;
                default:
                    await HappyPathAsync(window, host, vm, log);
                    break;
            }
            UiAlive(window, host, vm, log, "after scenario");
            Check(unhandled.Count == 0,
                $"no exception escaped the UI thread (got: {string.Join(",", unhandled)})");
            log($"{GateName} unhandled UI exceptions = 0");
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL [{scenario}]: {ex.Message}");
            return 1;
        }
        log($"{GateName} ALL GREEN [{scenario}]");
        return 0;
    }

    // ── 场景 1：无注入（F2-01/02/03/04/06/07/08/10）────────────────

    private static async Task HappyPathAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        // F2-03：窗口创建不 bootstrap。
        Check(host.State == HostRuntimeState.Starting, "host starts in Starting");
        Check(!host.HasSession, "no native host exists before the Open handler runs");
        Check(host.RuntimeCounts == (0, 0), "no frame was pumped before bootstrap");
        Check(host.GlfwLifetime.initCount == 0, "glfwInit has not run before bootstrap");
        Check(!vm.IsOpen, "the view model has no project");

        // F2-04：OpenProject → EnsureRuntime → Ready（唯一命名触发点）。
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.HasSession, "the Open handler bootstrapped the runtime through EnsureRuntime");
        Check(host.State == HostRuntimeState.Ready, "bootstrap + open reaches Ready");
        Check(host.IsOpen && vm.IsOpen, "project is open on both sides");
        Check(host.GlfwLifetime.initCount == 1, $"glfwInit ran exactly once (got {host.GlfwLifetime.initCount})");
        Check(host.GlfwLifetime.users == 1, $"exactly one glfw user (got {host.GlfwLifetime.users})");

        // F2-06：唯一 pump 由 service 持有并真的在推进。
        Check(await WaitFramesAsync(host, 3, ProbeTimeoutMs), "the service-owned pump advances frames");
        var frame = host.LastFrameProof;
        Check(frame is not null && frame.Value.Stage == EditorBridgeApi.EngineHostStageTick,
            "frames are published from the Tick stage");
        Check(frame!.Value.Status == EditorBridgeApi.EngineHostStatusSkippedNotPlaying,
            "not playing -> SkippedNotPlaying (not a fault)");

        // 传输链可重复：Play → Stop → Play → Stop（唯一 pump，tick 只在运行期增长）。
        var play = Control<Button>(window, "PlayBtn");
        var stop = Control<Button>(window, "StopBtn");
        ulong ticksMark = 0;
        for (int round = 1; round <= 2; round++)
        {
            Click(play);
            Check(vm.IsRunning, $"Play reaches the running state (round {round})");
            Check(await GateProbe.WaitUntilAsync(
                () => host.RuntimeCounts.ticks > ticksMark, ProbeTimeoutMs),
                $"the session ticks while playing (round {round})");
            ticksMark = host.RuntimeCounts.ticks;
            Click(stop);
            Check(!vm.IsRunning, $"Stop leaves the running state (round {round})");
            var ticks = host.RuntimeCounts.ticks;
            await Task.Delay(150);
            Check(host.RuntimeCounts.ticks == ticks, $"no tick after Stop (round {round})");
        }

        // F2-07：CloseProject 停止 pump，但**不**销毁 runtime / 不重建 GL。
        Check(!vm.IsDirty, "close is exercised from a clean state (no modal prompt)");
        var initBeforeClose = host.GlfwLifetime.initCount;
        Click(Control<MenuItem>(window, "FileCloseItem"));
        Check(host.State == HostRuntimeState.Starting, "CloseProject returns the host to Starting");
        Check(!host.IsOpen && !vm.IsOpen, "the project is closed on both sides");
        Check(host.HasSession, "F2-07: the EngineHost handle is retained across CloseProject");
        Check(host.GlfwLifetime.initCount == initBeforeClose,
            $"F2-07: CloseProject did not re-run glfwInit ({host.GlfwLifetime.initCount})");
        var framesAtClose = host.RuntimeCounts.frames;
        await Task.Delay(200);
        Check(host.RuntimeCounts.frames == framesAtClose, "the pump stopped with the project");

        // F2-10：重新打开复用同一 runtime（不重新 bootstrap）。
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready, "reopening reaches Ready again");
        Check(host.GlfwLifetime.initCount == initBeforeClose,
            "F2-07/F2-10: reopening reuses the existing runtime (no second bootstrap)");
        Check(await WaitFramesAsync(host, 2, ProbeTimeoutMs), "the pump restarts on reopen");

        // F2-08：终态之后不得隐式复活，且失败必须可观测而不是抛异常。
        host.Dispose();
        Check(host.IsDisposed && host.State == HostRuntimeState.Stopping, "terminal Dispose is reached");
        int consoleBefore = vm.ConsoleLines.Count;
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(vm.ConsoleLines.Count > consoleBefore, "the post-dispose Open attempt is reported");
        Check(LastConsoleText(vm).Contains("failed"),
            $"the post-dispose refusal is explicit ({LastConsoleText(vm)})");
        Check(host.IsDisposed, "the disposed host was not resurrected");
        Check(Throws<ObjectDisposedException>(host.EnsureRuntime),
            "EnsureRuntime on a disposed host throws ObjectDisposedException");

        // 生产 teardown 路径：终态 Dispose 之后窗口仍必须能安全关闭
        // （Vm.Dispose 会跳过 ResetSession，绝不回头触碰已释放的 host）。
        window.Close();
        Check(host.IsDisposed, "the host is still disposed after window close");
        Check(host.GlfwLifetime.users == 0, $"the glfw user was released with the host (users={host.GlfwLifetime.users})");
        Check(vm.Entities.Count > 0,
            "the view model kept its last state instead of re-driving a disposed host");
        log($"{GateName} happy path: lazy bootstrap, single trigger, retention across close, no resurrection");
    }

    // ── 场景 2~4：bootstrap 失败（F2-05 / F2-10 失败行）──────────

    private static async Task BootstrapFailureAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log,
        int expectedStatus, int expectedStage)
    {
        Check(host.State == HostRuntimeState.Starting, "host starts in Starting");
        Check(!host.HasSession, "no runtime before the Open handler runs");

        // 关键回归：bootstrap 失败**不得**以异常形式逃出 XAML handler
        // （旧行为：TargetInvocationException 包住 status/stage，诊断全丢）。
        Click(Control<Button>(window, "OpenProjectBtn"));

        Check(!host.HasSession, "a failed bootstrap leaves no native handle");
        Check(host.State == HostRuntimeState.Starting, "a failed bootstrap stays in Starting");
        Check(!host.IsOpen && !vm.IsOpen, "no project is open after a failed bootstrap");
        Check(host.RuntimeCounts == (0, 0), "a failed bootstrap never pumps a frame");
        Check(host.LastRuntimeFailure is null, "a bootstrap failure is not a runtime (pump) fault");

        // 结构化诊断必须到达 UI（Console / 状态栏 / ScriptError）。
        string expected = $"{HostOperationException.StatusText(expectedStatus)}"
                        + $" / {HostOperationException.StageText(expectedStage)}";
        Check(LastConsoleText(vm).Contains(expected),
            $"the structured failure reached the console (expected '{expected}', got '{LastConsoleText(vm)}')");
        Check(vm.StatusText.Contains("failed"), $"the status bar shows the failure ({vm.StatusText})");
        Check(vm.ScriptError.Contains(expected), "the script error slot carries the same structured text");

        // 幂等/确定性：第二次点击得到同一个 typed 失败（没有卡死、没有半初始化）。
        int consoleBefore = vm.ConsoleLines.Count;
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(vm.ConsoleLines.Count > consoleBefore, "the retry attempt is reported, not swallowed");
        Check(LastConsoleText(vm).Contains(expected), "the retry fails with the same structured status/stage");
        Check(!host.HasSession && host.State == HostRuntimeState.Starting,
            "the retry still leaves the host in Starting with no handle");

        // F2-06 / F2-10：runtime 依赖操作被显式拒绝。
        Click(Control<Button>(window, "HierarchyCreateEntityBtn"));
        Check(LastConsoleText(vm).Contains("+ Entity ignored: no project"),
            "a refused edit is reported, not silently dropped");
        Check(Throws<HostRuntimeNotReadyException>(() => host.GetCounts()),
            "runtime-dependent calls are rejected with RuntimeNotReady");

        // native 侧回滚干净：glfw 计数回到基线。
        var glfw = host.GlfwLifetime;
        Check(glfw.users == 0, $"no glfw user survives a failed bootstrap (users={glfw.users})");
        Check(glfw.initCount == glfw.terminateCount,
            $"glfw init/terminate are balanced ({glfw.initCount}/{glfw.terminateCount})");
        log($"{GateName} bootstrap failure: structured diagnostic, no handle, no pump, glfw balanced");
        await Task.CompletedTask;
    }

    // ── 场景 5~6：pump 侧 fault（F2-05 / F2-10）────────────────────

    private static async Task PumpFaultAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log, bool playFirst)
    {
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready, "bootstrap + open reach Ready before the injected fault");
        var play = Control<Button>(window, "PlayBtn");
        if (playFirst)
        {
            // 注入点在**第一个**运行帧生效：Play 本身成功，随后第一帧 tick fault，
            // 因此"成功 tick 计数"永远不会增长（断言的是 fault 到达，不是 tick 增长）。
            Click(play);
            Check(vm.IsRunning, "Play succeeds before the injected tick fault");
        }

        // 注入的 frame / tick fault 必须被 pump 检出：停泵 + Faulted + 上报一次。
        Check(await GateProbe.WaitUntilAsync(
            () => host.State == HostRuntimeState.Faulted, ProbeTimeoutMs),
            $"the pump detects the injected fault and marks the host Faulted (state={host.State})");
        var failure = host.LastRuntimeFailure;
        Check(failure is not null, "the pump failure is recorded as a structured failure");
        Check(failure!.Status == EditorBridgeApi.EngineHostStatusTickFault
              && failure.Stage == EditorBridgeApi.EngineHostStageTick,
            $"the failure is TickFault / Tick (got {failure.Status}/{failure.Stage})");
        Check(!vm.IsRunning, "the UI does not keep claiming to be running after a runtime fault");
        Check(vm.ScriptError.Length > 0, "the fault text is visible in the script error slot");
        Check(!host.IsOpen, "a faulted host reports no open project");

        // 停泵：帧号不再增长。
        var frames = host.RuntimeCounts.frames;
        await Task.Delay(200);
        Check(host.RuntimeCounts.frames == frames, $"the pump stopped (frames stayed at {frames})");

        // F2-08 / F2-10：faulted 之后不得复活，必须显式拒绝。
        Check(Throws<HostRuntimeUnavailableException>(() => host.EnsureRuntime()),
            "EnsureRuntime refuses to resurrect a faulted runtime");
        Check(Throws<HostRuntimeUnavailableException>(() => host.OpenProject("a", "b")),
            "OpenProject refuses to resurrect a faulted runtime");
        Check(Throws<HostRuntimeUnavailableException>(() => host.GetCounts()),
            "runtime-dependent calls are rejected as unavailable");
        // UI 侧的编辑必须被拒绝且**显式上报**。注意：fault 后 ViewModel 的本地集合仍在，
        // 因此 `IsOpen` 与 host 的 IsOpen 出现分歧，点击会走到 host 再被三态 gate 拒绝
        // （`IsOpen` 单一事实源属 A3 范围，A2 只要求"拒绝 + 显式"，不静默丢弃）。
        Click(Control<Button>(window, "HierarchyCreateEntityBtn"));
        Check(LastConsoleText(vm).Contains("+ Entity ignored: no project")
              || LastConsoleText(vm).Contains("failed"),
            $"the edit is refused and reported on a faulted host ({LastConsoleText(vm)})");
        log($"{GateName} pump fault: stopped, Faulted, structured, no resurrection");
    }

    // ── 场景 7：Play 命令失败（native command exception）────────────

    private static async Task PlayFaultAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready, "bootstrap + open reach Ready before the injected fault");

        var play = Control<Button>(window, "PlayBtn");
        int consoleBefore = vm.ConsoleLines.Count;
        Click(play);   // 失败必须被 VM 转成可观测状态，不得逃出 handler

        string tail = string.Join('\n', vm.ConsoleLines.Skip(consoleBefore));
        Check(tail.Contains(HostOperationException.StatusText(EditorBridgeApi.EngineHostStatusSessionFailed))
              && tail.Contains(HostOperationException.StageText(EditorBridgeApi.EngineHostStagePlay)),
            $"the play failure is reported with SessionFailed / Play (got: {tail})");
        Check(tail.Contains("injected play binding failure"),
            "the native diagnostic text survives the C ABI and reaches the UI");
        Check(!vm.IsRunning, "the UI is not marked running after a failed Play");

        // 实测事实：Play 的失败在 EditorSession 内部被 rollback 并转成 typed 失败，
        // runtime **没有** fault —— 这是"可恢复失败"，UI 必须能继续用（重试 / 关闭都可用）。
        Check(host.State == HostRuntimeState.Ready,
            $"a failed Play leaves the runtime usable (state={host.State})");
        Check(await WaitFramesAsync(host, 2, ProbeTimeoutMs),
            "the pump keeps advancing after a failed Play");
        int retryBefore = vm.ConsoleLines.Count;
        Click(play);
        Check(vm.ConsoleLines.Skip(retryBefore).Any(line => line.Contains("SessionFailed")),
            "retrying Play reports the same structured failure (deterministic, not wedged)");
        Check(!vm.IsRunning && host.State == HostRuntimeState.Ready,
            "the retry left the host usable");

        // 关闭路径在可恢复失败之后必须照常工作。
        Check(!vm.IsDirty, "close is exercised from a clean state (no modal prompt)");
        Click(Control<MenuItem>(window, "FileCloseItem"));
        Check(host.State == HostRuntimeState.Starting && !host.IsOpen,
            "CloseProject still works after a failed Play");
        log($"{GateName} play fault: SessionFailed/Play with native text, runtime stays usable");
    }

    // ── 共用：UI 仍然可用（每个场景都要证明"失败后 UI 仍可继续"）──────

    private static void UiAlive(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log, string when)
    {
        // 纯 UI 侧动作（不触碰 runtime）：搜索框过滤必须仍然工作。
        int before = vm.FilteredEntities.Count;
        vm.SearchText = "zzz_no_such_entity";
        Check(vm.FilteredEntities.Count == 0, $"hierarchy filter still responds {when}");
        vm.SearchText = "";
        Check(vm.FilteredEntities.Count == before, $"hierarchy filter restores {when}");
        Check(vm.OffUiThreadEventCount == 0, $"no event was consumed off the UI thread {when}");
    }

    private static string LastConsoleText(MainViewModel vm)
        => vm.ConsoleLines.Count == 0 ? string.Empty : vm.ConsoleLines[^1];

    /// <summary>注入点只能来自进程启动时的环境快照（MSVC getenv 语义）。</summary>
    private static Scenario DetectScenario()
    {
        if (On("EDITOR_BRIDGE_FAIL_APPLICATION_INIT")) return Scenario.BootstrapAppInit;
        if (On("EDITOR_BRIDGE_FAIL_FBO")) return Scenario.BootstrapFbo;
        if (On("EDITOR_BRIDGE_FAIL_SESSION_ATTACH")) return Scenario.BootstrapSession;
        if (On("EDITOR_BRIDGE_FAIL_FRAME") || On("EDITOR_BRIDGE_FAIL_RUNTIME_COMMAND")) return Scenario.FrameFault;
        if (On("EDITOR_BRIDGE_FAIL_RUNTIME_TICK")) return Scenario.TickFault;
        if (On("EDITOR_BRIDGE_FAIL_PLAY_BIND")) return Scenario.PlayBindFault;
        return Scenario.Happy;

        static bool On(string name)
            => Environment.GetEnvironmentVariable(name) == "1";
    }
}
