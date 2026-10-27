using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Avalonia.Controls;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// Commit A 回归：EngineHost 生产方式只允许**一个** runtime pump owner。
///
/// 为什么需要独立 gate：
///   - 双 pump 不会让帧/ tick 停止增长，因此既有的 "frames 在推进" 断言全都能通过；
///   - 它只改变**速度**（每 33ms 两次 tick）与时间步长，属于静默错误；
///   - 这里用固定时间窗口比较 `PumpInvocations` 与 native `frames` 增量，并给
///     频率设上界（约 30/s；第二个 driver 会把它推到 ~60/s）。
///
/// 本 gate 不新增 native ABI、不写 fixture，也**不**调用显式
/// <see cref="EditorHostService.RuntimeTick"/>：测量窗口内只允许 service timer 驱动。
/// </summary>
public static class PumpOwnershipGate
{
    private const string GateName = "[PUMP-OWNER GATE]";
    private const int MeasureWindowMs = 1500;

    /// <summary>33ms 名义间隔约 30.3/s；双 driver 约 60.6/s。50 足以区分且容忍调度抖动。</summary>
    private const double MaxAcceptedRate = 50.0;
    /// <summary>慢/被占用的 UI 线程也至少应能推进到这个下限，否则测量窗口无效。</summary>
    private const double MinAcceptedRate = 8.0;

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost,
                "pump ownership gate requires the EngineHost backend");

            // Open 之前不存在任何 pump：排除“构造即启动”的第二驱动。
            Check(host.PumpInvocations == 0,
                $"no pump may exist before the project is open (was {host.PumpInvocations})");

            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen,
                "the Open handler starts the runtime and reaches Ready");
            Check(await WaitUntilAsync(() => host.PumpInvocations >= 2, 4000),
                "the service-owned pump advances after Open");

            // dt clamp 的纯函数契约：停顿/负数/NaN 都不得灌进模拟。
            Check(EditorHostService.ClampPumpDeltaSeconds(10.0)
                  == EditorHostService.MaxPumpDeltaSeconds,
                "an oversized frame delta is clamped to MaxPumpDeltaSeconds");
            Check(EditorHostService.ClampPumpDeltaSeconds(-1.0) == 0.0,
                "a negative frame delta is clamped to zero");
            Check(EditorHostService.ClampPumpDeltaSeconds(double.NaN) == 0.0,
                "NaN is clamped to zero");
            Check(EditorHostService.ClampPumpDeltaSeconds(double.PositiveInfinity) == 0.0,
                "+Inf is clamped to zero");
            Check(EditorHostService.ClampPumpDeltaSeconds(double.NegativeInfinity) == 0.0,
                "-Inf is clamped to zero");
            Check(EditorHostService.ClampPumpDeltaSeconds(0.0) == 0.0,
                "zero keeps zero semantics");
            Check(EditorHostService.ClampPumpDeltaSeconds(0.05) == 0.05,
                "a normal positive delta is preserved");

            var idle = await MeasureAsync(host);
            log($"{GateName} idle: pumps={idle.Pumps} failures={idle.Failures} "
                + $"frames={idle.Frames} rate={idle.Rate:F1}/s "
                + $"lastDt={host.LastPumpDeltaSeconds:F3}s");
            AssertSingleDriver(idle, "idle");

            // Play / Stop 不得启动第二个 pump。
            var play = Control<Button>(window, "PlayBtn");
            var stop = Control<Button>(window, "StopBtn");
            Click(play);
            Check(vm.IsRunning, "Play reaches the running state");

            var playing = await MeasureAsync(host);
            log($"{GateName} playing: pumps={playing.Pumps} failures={playing.Failures} "
                + $"frames={playing.Frames} rate={playing.Rate:F1}/s "
                + $"lastDt={host.LastPumpDeltaSeconds:F3}s");
            AssertSingleDriver(playing, "playing");

            Click(stop);
            Check(!vm.IsRunning, "Stop leaves the running state");

            Check(host.LastPumpDeltaSeconds <= EditorHostService.MaxPumpDeltaSeconds,
                $"the last pump dt must stay clamped (was {host.LastPumpDeltaSeconds:F3}s)");

            log($"{GateName} ALL GREEN (one pump owner, rate<={MaxAcceptedRate:F0}/s, "
                + "attempts == frames + failures)");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    private readonly record struct Measurement(
        long Pumps, long Failures, ulong Frames, double Seconds, double Rate);

    /// <summary>
    /// 在一个时间窗口内同时采集 managed pump 次数与 native 帧数。
    /// 两者都在 UI 线程读取，窗口内没有其他泵调用者，因此增量可直接比较。
    /// </summary>
    private static async Task<Measurement> MeasureAsync(EditorHostService host)
    {
        // 三个计数器都在 UI 线程更新/读取：定时泵的 tick 不可能在本次采样
        // 与下一次采样之间只更新其中一部分。窗口内没有其他显式泵调用者。
        long pumpsBefore = host.PumpInvocations;
        long failuresBefore = host.PumpFailures;
        ulong framesBefore = host.RuntimeCounts.frames;

        var sw = Stopwatch.StartNew();
        await Task.Delay(MeasureWindowMs);
        sw.Stop();

        long pumps = host.PumpInvocations - pumpsBefore;
        long failures = host.PumpFailures - failuresBefore;
        ulong frames = host.RuntimeCounts.frames - framesBefore;
        double seconds = sw.Elapsed.TotalSeconds;
        return new Measurement(pumps, failures, frames, seconds, pumps / seconds);
    }

    private static void AssertSingleDriver(Measurement m, string phase)
    {
        Check(m.Seconds > 0.0, $"{phase}: measurement window must be non-empty");
        Check(m.Pumps > 0, $"{phase}: the service-owned pump must be running");
        Check(m.Failures == 0,
            $"{phase}: {m.Failures} pump attempt(s) failed — native frames cannot match attempts");
        // 语义：PumpInvocations = 成功推进的帧 + 失败调用。失败调用不推进 native frame，
        // 因此这里验证的是恒等式，而不是把“调用次数”无条件当作“成功帧数”。
        Check(m.Pumps >= m.Failures,
            $"{phase}: failure count {m.Failures} exceeds attempt count {m.Pumps}");
        Check(m.Frames == (ulong)(m.Pumps - m.Failures),
            $"{phase}: pump attempts must decompose into frame advances + failures "
            + $"(pumps={m.Pumps}, failures={m.Failures}, frames={m.Frames}) — "
            + "a second driver bypasses PumpInvocations");
        Check(m.Rate <= MaxAcceptedRate,
            $"{phase}: pump rate {m.Rate:F1}/s exceeds {MaxAcceptedRate:F0}/s — "
            + "two drivers are running (expected ~30/s)");
        Check(m.Rate >= MinAcceptedRate,
            $"{phase}: pump rate {m.Rate:F1}/s is below the measurement floor "
            + $"({MinAcceptedRate:F0}/s) — the window is not representative");
    }
}
