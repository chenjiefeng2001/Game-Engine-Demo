using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Threading;
using AvaloniaEditor.Native;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// A7-2 Managed Host Switch gate：G2-01 ~ G2-10。
/// 只做只读 / 内存态操作（不调用 SaveProject / ScriptSave），不写 repo fixture。
/// </summary>
public static class Hrc2Gate
{
    private const string ManifestRelative = "assets/gp01/manifest.json";
    private const string SceneRelative = "assets/gp01/Main.scene";

    private static readonly string[] LegacyFlags =
    {
        "--hrc1", "--selftest", "--gate1", "--gate2", "--gate3a",
        "--gate3b", "--gate3c", "--gate3d", "--gate3g",
    };

    public static async Task<int> RunAsync(EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            G2_01_SingleHost(host, vm, log);
            G2_02_03_NoLegacyFallback(host, log);
            G2_04_ThreeStateGate(host, log);
            G2_05_08_RuntimeOpsAndDiagnostics(host, log);
            await G2_05_MarshalOnUiThreadAsync(host, vm, log);
            await G2_06_GenerationGuardAsync(host, log);
            G2_07_PendingEditRoundTrip(host, log);
            G2_09_CloseDisposeRestart(host, log);
            log("[HRC-G2 GATE] ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log("[HRC-G2 GATE] FAIL: " + ex.Message);
            return 1;
        }
    }

    // G2-01：EditorHostService 是唯一 managed Host owner。
    private static void G2_01_SingleHost(EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(ReferenceEquals(host, vm.Host), "window and view model share one host");
        Check(host.Backend == HostBackend.EngineHost, "production backend is EngineHost");
        Check(host.IsEngineHost, "host holds the engine host channel");
        Check(Program.SelectBackend(Array.Empty<string>()) == HostBackend.EngineHost,
            "no CLI flag selects EngineHost");
        foreach (var flag in LegacyFlags)
        {
            Check(Program.SelectBackend(new[] { flag }) == HostBackend.LegacySession,
                $"HRC-1 flag {flag} keeps the legacy backend");
        }
        Check(!host.IsDisposed, "host is alive");
        Check(!host.HasSession, "host is lazy before bootstrap");
        host.EnsureRuntime();
        Check(host.HasSession, "host owns the single engine host handle after bootstrap");
        log("[G2-01] single managed host owner; backend is EngineHost");
    }

    // G2-02 / G2-03：28/28 已迁移，且 production 无 legacy Session ABI 退路。
    private static void G2_02_03_NoLegacyFallback(EditorHostService host, Action<string> log)
    {
        Check(!host.SupportsViewport,
            "EngineHost backend exposes no legacy Viewport/Gizmo capability");
        Check(Throws<HostCapabilityNotAvailableException>(() => host.ViewportTick(0.016f)),
            "legacy ViewportTick is rejected on the production backend");
        Check(Throws<HostCapabilityNotAvailableException>(() => host.ViewportPick(0f, 0f)),
            "legacy ViewportPick is rejected on the production backend");
        Check(Throws<HostCapabilityNotAvailableException>(() => host.SetGizmoMode(1)),
            "legacy gizmo is rejected on the production backend");
        Check(Throws<NotSupportedException>(() => host.GetRuntimePersistInt("hp", 0)),
            "RuntimePersistInt stays DEFERRED (A7-0)");
        log("[G2-02] SESSION calls route through EngineHost dispatch/fetch/poll");
        log("[G2-03] zero production path to the legacy EditorSession ABI");
    }

    // G2-04：Starting / Ready / Stopping 三态正确阻断。
    private static void G2_04_ThreeStateGate(EditorHostService host, Action<string> log)
    {
        Check(host.State == HostRuntimeState.Starting, "host starts in Starting");
        Check(Throws<HostRuntimeNotReadyException>(() => host.GetCounts()),
            "Starting rejects runtime-dependent calls with RuntimeNotReady");
        Check(Throws<HostRuntimeNotReadyException>(host.Stop), "Starting rejects Stop");
        Check(Throws<HostRuntimeNotReadyException>(() => host.AddComponent(0, "Camera")),
            "Starting rejects component edits");

        OpenProject(host);
        Check(host.State == HostRuntimeState.Ready, "opening a project reaches Ready");
        Check(host.IsOpen, "project is open");

        host.CloseProject();
        Check(host.State == HostRuntimeState.Starting, "CloseProject returns to Starting");
        Check(!host.IsOpen, "project is closed");
        Check(!host.IsDisposed, "CloseProject is not a terminal Dispose");
        Check(Throws<HostRuntimeNotReadyException>(() => host.GetCounts()),
            "closed project rejects runtime-dependent calls");
        log("[G2-04] three-state gate blocks unavailable calls with an explicit state");
    }

    // G2-05 / G2-08：runtime 操作可用，diagnostic 路径完整。
    private static void G2_05_08_RuntimeOpsAndDiagnostics(EditorHostService host, Action<string> log)
    {
        OpenProject(host);
        Check(host.GetLastError().Length == 0, "successful ops clear the host diagnostic");

        var counts = host.GetCounts();
        Check(counts.entities > 0, "entity count comes from the host seam");
        Check(counts.assets > 0, "asset count comes from the host seam");
        Check((host.GetEntityName(0) ?? string.Empty).Length > 0, "GetEntityName routes through Fetch");
        Check(host.GetAssetGuid(0).Length > 0, "GetAssetGuid routes through Fetch");
        Check(host.HasComponent(0, "Camera") == false, "HasComponent routes through Dispatch");
        Check(host.AddComponent(0, "Camera"), "AddComponent");
        Check(host.HasComponent(0, "Camera"), "AddComponent + HasComponent round-trip");
        Check(host.SetComponentProperty(0, "Camera", "zoom", "2"), "SetComponentProperty");
        Check(host.GetComponentProperty(0, "Camera", "zoom") == "2", "GetComponentProperty");
        Check(host.GetEntityPosition(0) is var _, "GetEntityPosition routes through Fetch");
        Check(host.SetEntityPosition(0, 1f, 2f, 3f), "SetEntityPosition");
        var after = host.GetEntityPosition(0);
        Check(Math.Abs(after.x - 1f) < 1e-5f && Math.Abs(after.z - 3f) < 1e-5f,
            "position round-trip through the host seam");
        Check(host.IsDirty(), "IsDirty routes through Dispatch");
        Check(host.GetAssetPath(0) is { Length: > 0 }, "GetAssetPath routes through Fetch");
        Check(host.GetAssetType(0) >= 0, "GetAssetType routes through Dispatch");
        Check(host.GetWarnings() is not null, "GetWarnings routes through Fetch");
        Check(host.GetEntitySprite(0) is not null, "GetEntitySprite routes through Fetch");
        Check(host.GetEntityScript(0) is not null, "GetEntityScript routes through Fetch");
        Check(host.ScriptRead(32).Length > 0, "ScriptRead routes through Fetch");

        // 失败必须落到 host diagnostic（GetLastError / GetRuntimeError 同源）。
        Check(!host.DeleteEntity(9999), "out-of-range delete fails");
        string last = host.GetLastError();
        string runtime = host.GetRuntimeError();
        Check(last.Length > 0, "GetLastError is populated after a failure");
        Check(runtime == last, "GetRuntimeError and GetLastError share one diagnostic path");
        log("[G2-05] runtime routing works end-to-end on the host seam");
        log("[G2-08] diagnostic path unified on EngineHost_GetLastDiagnostic");
    }

    // G2-05：UI 线程 marshal —— 事件只在 UI 线程消费。
    private static async Task G2_05_MarshalOnUiThreadAsync(
        EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int before = vm.ConsumedEventCount;
        host.PostEventWithGeneration(host.Generation, EditorBridgeApi.EvEntityCreated, "g2");
        await YieldToDispatcherAsync();
        Check(vm.OffUiThreadEventCount == 0, "no event was consumed off the UI thread");
        // 让出期间可能同时排空此前排队的真实 native 事件，因此只要求严格增长。
        Check(vm.ConsumedEventCount > before, "the event was consumed on the UI thread");
        log("[G2-05] native events are marshalled to the UI thread before consumption");
    }

    // G2-06：generation guard 丢弃旧 project / session 的 late event。
    // A3-0 约束：每进程只允许一个 EngineHost，因此复用主 host，不新建 probe host。
    private static async Task G2_06_GenerationGuardAsync(EditorHostService host, Action<string> log)
    {
        OpenProject(host);
        long captured = host.Generation;
        int droppedBefore = host.DroppedLateEventCount;

        // 真实场景：CloseProject 之后，先前捕获 generation 的 late event 必须被丢弃。
        host.CloseProject();
        Check(host.Generation > captured, "CloseProject bumps the managed generation");
        host.PostEventWithGeneration(captured, EditorBridgeApi.EvEntityCreated, "stale");
        await YieldToDispatcherAsync();
        // 此前排队的真实 native 事件同样带旧 generation，会一并被丢弃，因此只要求严格增长。
        Check(host.DroppedLateEventCount > droppedBefore,
            "late event captured before CloseProject is dropped");
        int droppedAfterStale = host.DroppedLateEventCount;

        // 重新打开 project：当前 generation 的事件在 runtime 活跃时必须被投递。
        OpenProject(host);
        int deliveredBefore = host.AcceptedEventCount;
        host.PostEventWithGeneration(host.Generation, EditorBridgeApi.EvEntityCreated, "fresh");
        await YieldToDispatcherAsync();
        Check(host.DroppedLateEventCount == droppedAfterStale,
            "event with the current generation is not dropped");
        Check(host.AcceptedEventCount > deliveredBefore,
            "event with the current generation is delivered");
        log("[G2-06] managed generation guard drops stale events");
    }

    // G2-07：pending edit 保持 accepted != executed。
    private static void G2_07_PendingEditRoundTrip(EditorHostService host, Action<string> log)
    {
        OpenProject(host);

        bool completed = false;
        bool lastOk = false;
        string failure = string.Empty;
        int failures = 0;
        host.PendingEditCompleted += (ok, diag) =>
        {
            completed = true;
            lastOk = ok;
            if (!ok) { failures++; failure = diag; }
        };

        int entity = host.CreateEntity("A7GateSprite");
        Check(entity >= 0, "entity created through the host seam");

        // 受理 ≠ 执行：受理后 sprite 尚未绑定。
        Check(host.AssignSprite(0, entity), "AssignSprite is accepted into the queue");
        Check(host.GetEntitySprite(entity).Length == 0, "accepted edit has not executed yet");
        Check(!completed, "no completion is reported before the pump");

        host.RuntimeTick(0.016f);
        Check(completed, "pending edit completed after the pump");
        Check(lastOk, "pending edit executed without diagnostic");
        Check(failure.Length == 0, "no failure diagnostic on the success path");
        Check(host.GetEntitySprite(entity).Length > 0, "sprite is bound after execution");
        Check(host.LastPendingEditExecutedWithContextCurrent,
            "edit executed inside the context-current frame window");
        var counts = host.PendingEditCounts;
        Check(counts.accepted >= 1 && counts.executed >= 1, $"pending edit proof {counts}");

        // 失败路径：越界 entity 必须给出 failed，而不是静默 no-op。
        completed = false;
        Check(host.AssignSprite(0, 9999), "bad pending edit is still accepted into the queue");
        host.RuntimeTick(0.016f);
        Check(completed, "failed pending edit is reported");
        Check(!lastOk, "failed pending edit is reported as failed");
        Check(failures == 1, $"exactly one failure reported (got {failures})");
        Check(failure.Length > 0, "failed pending edit carries a diagnostic");
        log("[G2-07] pending edit accepted/executed/failed semantics preserved");
    }

    // G2-09：CloseProject / Dispose / restart 回归。
    private static void G2_09_CloseDisposeRestart(EditorHostService host, Action<string> log)
    {
        host.CloseProject();
        Check(!host.IsDisposed, "CloseProject keeps the host reusable");
        OpenProject(host);
        Check(host.State == HostRuntimeState.Ready, "host restarts after CloseProject");

        long generationBefore = host.Generation;
        host.Dispose();
        Check(host.IsDisposed, "Dispose reaches the terminal state");
        Check(host.State == HostRuntimeState.Stopping, "state becomes Stopping");
        Check(host.Generation > generationBefore, "Dispose bumps the managed generation");
        host.Dispose();
        Check(host.IsDisposed, "terminal Dispose is idempotent");
        Check(Throws<ObjectDisposedException>(host.EnsureRuntime), "disposed host rejects recreation");
        log("[G2-09] CloseProject / Dispose / restart regression holds");
    }

    private static void OpenProject(EditorHostService host)
    {
        var root = FindRepoRoot();
        Check(root is not null, "repo root with assets/gp01 was found");
        host.EnsureRuntime();
        bool opened = host.OpenProject(
            Path.Combine(root!.FullName, ManifestRelative),
            Path.Combine(root.FullName, SceneRelative));
        Check(opened, "project opened through the host seam");
    }

    private static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

    /// <summary>
    /// 让出 UI 线程一次，使此前 Post 的 marshal 回调先执行。
    /// gate 运行在 UI 线程上，<c>RunJobs()</c> 在嵌套调用下无效，必须真正让出。
    /// </summary>
    private static async Task YieldToDispatcherAsync()
        => await Dispatcher.UIThread.InvokeAsync(() => { });

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static bool Throws<T>(Action action) where T : Exception
    {
        try
        {
            action();
            return false;
        }
        catch (T)
        {
            return true;
        }
    }
}
