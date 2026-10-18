using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using AvaloniaEditor.Native;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// 阶段 A / A3：UI-state contract 证据（D1 Host 派生 open 状态、D2 选择意图、D3 create 回执）。
///
/// 与 <see cref="ProductionPathGate"/>（A1，生产路径回归）分开：A1 证明"生产 handler 能跑通"，
/// 本 gate 证明"状态语义收敛"——特别是**顺序无关性**与**失败不产生假状态**。
///
/// 场景由**进程启动时**已存在的 native 注入环境变量 + backend 选择决定
/// （MSVC <c>getenv</c> 读 CRT 启动快照，运行期 <c>SetEnvironmentVariable</c> 对 native 不可见）。
///
/// <code>
/// 场景              启动条件                                            覆盖
/// enginehost        --uistate（EngineHost，无注入）                      N1 / N2a / N4 + Refused 负例
/// legacy-order      --uistate --uistatelegacy（legacy，无注入）            N2b（event-before-return）
/// legacy-failure    --uistate --uistatelegacy + FAIL_LEGACY_OPERATION     N3（host 侧 create 失败）
/// frame-fault       --uistate + EDITOR_BRIDGE_FAIL_FRAME                 N5（runtime fault → 投影清空）
/// </code>
///
/// N2 的"两种顺序最终状态一致"由 <see cref="OrderFact"/> 规范行 + runner 脚本的机械比较证明，
/// 不是叙述性结论。
/// </summary>
public static class UiStateGate
{
    private const string GateName = "[HRC-A3 UISTATE GATE]";
    private const string OrderFactTag = "A3-ORDER-FACT";
    private const int ProbeTimeoutMs = 6000;

    private enum Scenario
    {
        EngineHostHappy,
        LegacyOrder,
        FrameFault,
    }

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        var unhandled = new List<string>();
        InstallUnhandledProbe(kind => unhandled.Add(kind));
        var scenario = DetectScenario(host);
        try
        {
            log($"{GateName} scenario={scenario} backend={host.Backend}");
            switch (scenario)
            {
                case Scenario.LegacyOrder:
                    OpenAndAssertHostDerived(window, host, vm, log);
                    await OrderAsync(window, host, vm, log, expectOrder: "event-before-return");
                    break;
                case Scenario.FrameFault:
                    await FaultClearsProjectionAsync(window, host, vm, log);
                    break;
                default:
                    // 顺序：S0 负例 → 开放态 → N4（必须在**未编辑**的干净态关闭）→ 重开 → N1/N2。
                    // N4 之后再 create，因此 create 造成的 dirty 不会挡住关闭路径。
                    await RefusedWithoutProjectAsync(window, host, vm, log);
                    OpenAndAssertHostDerived(window, host, vm, log);
                    await StaleEventAfterCloseAsync(window, host, vm, log);
                    await OrderAsync(window, host, vm, log, expectOrder: "return-before-event");
                    // 生产 teardown 必须**按序**走完：Environment.Exit 与 GL/GLFW 析构并发时
                    // 会挂在退出期（NHI-1 同类：overlay interception + native teardown），
                    // 那样 gate 断言虽绿，进程却不退出 —— 不能作为可复现证据。
                    ProductionTeardown(window, host, vm, log);
                    break;
            }
            Check(unhandled.Count == 0,
                $"no exception escaped the UI thread (got: {string.Join(",", unhandled)})");
            log($"{GateName} unhandled UI exceptions = 0");
        }
        catch (Exception ex)
        {
            // 记录类型 + 栈：gate 的失败必须可定位（只留 Message 会把断言来源丢掉）。
            log($"{GateName} FAIL [{scenario}]: {ex.GetType().Name}: {ex.Message}");
            log($"{ex.StackTrace}");
            return 1;
        }
        log($"{GateName} ALL GREEN [{scenario}]");
        return 0;
    }

    // ── S0：无 project 的 create 必须被显式拒绝（N3 的一半，见 §AssertRefused）──

    private static async Task RefusedWithoutProjectAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(host.IsOpen == false && vm.IsOpen == false, "S0 Closed: no project, IsOpen is false");
        int before = host.HasSession ? host.GetCounts().entities : 0;

        int consoleBefore = vm.ConsoleLines.Count;
        Click(Control<Button>(window, "ToolbarCreateEntityBtn"));
        Check(LogSince(vm, consoleBefore).Contains("+ Entity ignored: no project"),
            "S0 the create handler refuses the edit with no project open");
        Check(vm.LastCreateReceipt.Outcome == CreateEntityOutcome.Refused,
            $"S0 the refusal is reported as a receipt (got {vm.LastCreateReceipt.Outcome})");
        Check(!host.HasSession, "S0 a refused create does not bootstrap a native host");
        AssertNoPhantomState(host, vm, "S0 closed state");
        log($"{GateName} S0/N3: create without a project is refused with a receipt and no phantom state");
        await Task.CompletedTask;
    }


    // ── S1：Open handler → host-derived IsOpen ─────────────────────

    private static void OpenAndAssertHostDerived(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready && host.IsOpen,
            "S1 the Open handler reaches Ready and the host reports an open project");
        Check(vm.IsOpen, "D1 IsOpen follows the host (not the local collection count)");
        Check(vm.Entities.Count == host.GetCounts().entities,
            "the collection projection matches the host after open");
        log("[HRC-A3 UISTATE GATE] S1: open state is host-derived");
    }

    // ── N1 / N2：create → 回执 + 选择意图（两种事件顺序）─────────────

    private static async Task OrderAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log,
        string expectOrder)
    {
        int before = host.GetCounts().entities;
        var existing = vm.Entities[0];
        vm.SelectByName(existing.Name);          // 预置一个**旧**选择，create 不得依赖它
        Check(vm.SelectedEntity?.Index == existing.Index, "a pre-existing selection is in place");

        int eventsBefore = vm.ConsumedEventCount;
        // 真实生产按钮 → MainWindow handler → VM → host（D3 单一路径）。
        Click(Control<Button>(window, "ToolbarCreateEntityBtn"));

        // D3：生产 handler 与 gate 断言同一份 receipt。
        var receipt = vm.LastCreateReceipt;
        Check(receipt.Outcome == CreateEntityOutcome.Created,
            $"the create produced a Created receipt (got {receipt})");
        Check(receipt.Index == before, $"the receipt carries the new index ({receipt})");
        Check(receipt.Succeeded && receipt.Name.Length > 0, "the receipt carries a real name");
        Check(host.GetCounts().entities == before + 1, "the session really holds the new entity");

        // D2：选择意图必须被消费，并把选择落到新实体上（与事件到达时机无关）。
        Check(await WaitUntilAsync(() => vm.SelectedEntity?.Index == receipt.Index, ProbeTimeoutMs),
            "the new entity ends up selected once the refresh lands");
        Check(vm.SelectionIntentResolved == receipt.Index,
            $"the selection intent resolved to the new index (got {vm.SelectionIntentResolved})");
        Check(vm.PendingSelectionIndex is null, "the intent is consumed exactly once");
        Check(vm.SelectedEntity?.Index != existing.Index,
            "create replaced the stale selection instead of leaving it behind");
        Check(vm.ScriptError.Length == 0, "a successful create reports no error");
        Check(vm.OffUiThreadEventCount == 0, "the create event was consumed on the UI thread");

        // N1 的最后一段：事件驱动的集合刷新本身仍然是 create 可见性的来源。
        Check(vm.ConsumedEventCount > eventsBefore || vm.LastCreateOrder == "event-before-return",
            "the create was observed either by the event or by the pre-existing synchronous order");
        Check(vm.LastCreateOrder == expectOrder,
            $"the observed event order is {expectOrder} (got {vm.LastCreateOrder})");

        // 规范行：runner 用它机械比较两种顺序的最终状态是否一致。
        log($"{OrderFactTag} order={vm.LastCreateOrder} outcome={receipt.Outcome} "
            + $"newIndex={receipt.Index} finalSelection={vm.SelectedEntity?.Index} "
            + $"intentResolved={vm.SelectionIntentResolved} intentPending={vm.PendingSelectionIndex?.ToString() ?? "none"}");
        log($"{GateName} order scenario passed ({expectOrder}): receipt + selection intent converged");
    }

    // ── N4：Close 之后的旧事件必须被 A7-2 generation guard 丢弃 ─────

    private static async Task StaleEventAfterCloseAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(!vm.IsDirty, "the close is exercised from a clean state (no modal prompt)");
        long captured = host.Generation;      // A7-2 现有 generation guard，不新造事件抑制
        Click(Control<MenuItem>(window, "FileCloseItem"));

        Check(!host.IsOpen && host.State == HostRuntimeState.Starting, "the Close handler closed the project");
        Check(!vm.IsOpen && vm.Entities.Count == 0 && vm.SelectedEntity is null,
            "S0 the projection is empty after close (IsOpen is host-derived)");
        Check(vm.PendingSelectionIndex is null, "close drops any pending selection intent");

        // 停泵证据放在这里（窗口仍开着）：teardown 段一旦 window.Close() 就不允许再 await。
        var framesAtClose = host.RuntimeCounts.frames;
        await Task.Delay(200);
        Check(host.RuntimeCounts.frames == framesAtClose,
            $"the pump stopped with the project (frames stayed at {framesAtClose})");

        int eventsBefore = vm.ConsumedEventCount;
        host.PostEventWithGeneration(captured, EditorBridgeApi.EvEntityCreated, "stale");
        await Task.Delay(150);
        Check(vm.ConsumedEventCount == eventsBefore,
            $"the stale event was dropped by the generation guard (consumed stayed at {eventsBefore})");
        Check(vm.Entities.Count == 0 && vm.SelectedEntity is null && !vm.IsOpen,
            "the stale event did not resurrect or pollute the closed projection");

        // 关闭后再 create：必须被拒绝，不得偷偷 bootstrap 或伪造选择。
        int consoleBefore = vm.ConsoleLines.Count;
        Click(Control<Button>(window, "ToolbarCreateEntityBtn"));
        Check(LogSince(vm, consoleBefore).Contains("+ Entity ignored: no project"),
            "a create after close is refused, not silently dropped");
        Check(vm.LastCreateReceipt.Outcome == CreateEntityOutcome.Refused,
            "the post-close refusal is a Refused receipt");
        Check(vm.SelectedEntity is null, "the post-close refusal fabricates no selection");

        // 重新打开：状态必须能恢复（不是一次性陷阱）。
        Click(Control<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready && vm.IsOpen, "the project reopens cleanly after a dropped event");
        Check(vm.Entities.Count == host.GetCounts().entities, "the projection is rebuilt on reopen");
        log("[HRC-A3 UISTATE GATE] N4: stale post-close event dropped; closed state stays closed");
    }

    // ── N5：runtime fault → 投影清空，但诊断必须保留 ────────────────

    private static async Task FaultClearsProjectionAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Click(Control<Button>(window, "OpenProjectBtn"));
        // 注入点在第一个被 pump 的帧生效：这里必须**同步**断言 fault 之前的开放态。
        Check(host.State == HostRuntimeState.Ready && vm.IsOpen, "the project is open before the injected fault");
        Check(vm.Entities.Count > 0 && vm.SelectedEntity is null,
            "the pre-fault projection holds entities and no selection");
        var keep = vm.Entities[0];
        vm.SelectByName(keep.Name);
        Check(vm.SelectedEntity?.Index == keep.Index, "a selection exists when the fault hits");

        Check(await WaitUntilAsync(() => host.State == HostRuntimeState.Faulted, ProbeTimeoutMs),
            $"the pump reports the injected fault (state={host.State})");

        // A3/D1：Fault 态 = IsOpen false + 投影清空 + 诊断保留。
        Check(!host.IsOpen, "the faulted host reports no open project");
        Check(!vm.IsOpen, "D1 IsOpen follows the host into the faulted state");
        Check(vm.Entities.Count == 0 && vm.Assets.Count == 0,
            $"D1 the collection projection is cleared ({vm.Entities.Count} entities left)");
        Check(vm.FilteredEntities.Count == 0 && vm.FilteredAssets.Count == 0,
            "D1 the filter views are cleared too");
        Check(vm.SelectedEntity is null && !vm.HasSelection, "D1 the selection is cleared");
        Check(vm.PendingSelectionIndex is null, "D2 the selection intent is dropped with the projection");
        Check(vm.ScriptError.Length > 0, $"D1 the fault diagnostic survives the projection clear ({vm.ScriptError})");
        Check(vm.StatusText.Contains("no project loaded"),
            $"the status bar reports the closed state ({vm.StatusText})");
        Check(vm.LastReconcileReason.Contains("faulted"),
            $"the reconcile recorded the fault reason (got '{vm.LastReconcileReason}')");

        // Faulted 之后不得复活，也不得产生假状态。
        int consoleBefore = vm.ConsoleLines.Count;
        Click(Control<Button>(window, "ToolbarCreateEntityBtn"));
        Check(LogSince(vm, consoleBefore).Contains("+ Entity ignored: no project"),
            "a create on a faulted host is refused and reported");
        AssertNoPhantomState(host, vm, "faulted state");
        Check(vm.Entities.Count == 0 && vm.SelectedEntity is null,
            "the post-fault create fabricated no entity and no selection");

        // UI 仍然可用（纯 UI 动作）：过滤器必须响应。
        int filteredBefore = vm.FilteredEntities.Count;
        vm.SearchText = "zzz";
        Check(vm.FilteredEntities.Count == 0, "the hierarchy filter still responds after a fault");
        vm.SearchText = "";
        Check(vm.FilteredEntities.Count == filteredBefore, "the filter restores after a fault");
        log("[HRC-A3 UISTATE GATE] N5: faulted state clears the projection but keeps the diagnostic");
    }

    // ── 生产 teardown 路径（窗口关闭 → Vm.Dispose → CloseProject → 终态 Dispose）──
    //
    // A3/D1 的状态不变量必须在**真实 teardown 路径**上成立：关闭后 host 回到 Starting
    // （不是终态），投影清空，IsOpen 随 host 变 false；终态 Dispose 之后不得复活。
    private static void ProductionTeardown(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(vm.IsDirty, "the create left the project dirty before teardown");
        var (frames, _) = host.RuntimeCounts;
        Check(frames > 0, $"the UI-driven pump advanced frames during the scenario (frames={frames})");

        // 同步断言：任何 await 都可能让进程在断言之前结束（静默通过 = 假证据）。
        log($"{GateName} teardown: closing the window");
        window.Close();
        log($"{GateName} teardown: window closed, asserting the closed state");

        Check(vm.Entities.Count == 0 && vm.Assets.Count == 0 && vm.FilteredEntities.Count == 0,
            "D1 closing the window clears the projection and its filter views");
        Check(vm.SelectedEntity is null && !vm.IsOpen && !vm.HasSelection,
            "D1 the selection and IsOpen follow the closed host");
        Check(!host.IsOpen && host.State == HostRuntimeState.Starting,
            "D1 window close performs CloseProject (Starting), not the terminal Dispose");
        Check(!host.IsDisposed, "the host survives the window close (Program owns the terminal Dispose)");

        // ⚠ 这里之后**不得有任何 await**：关闭最后一个窗口会让 Avalonia lifetime 开始 shutdown，
        // 进程会在挂起恢复前就结束（断言静默丢失 = 假证据）。停泵证据在 N4 里取，
        // 那时窗口还开着。同步走完 CloseProject → 终态 Dispose，再由调用方 Environment.Exit。
        log($"{GateName} teardown: closed state asserted, disposing the host");
        host.Dispose();
        log($"{GateName} teardown: terminal Dispose returned");
        Check(host.IsDisposed && host.State == HostRuntimeState.Stopping,
            "the terminal Dispose reaches Stopping");
        Check(Throws<ObjectDisposedException>(() => host.EnsureRuntime()),
            "a disposed host refuses resurrection");
        log($"{GateName} production teardown: window close -> CloseProject -> terminal Dispose "
            + $"(pump advanced {frames} frames)");
    }

    // ── N3（已裁决为 Refused 负例，见 A3 contract §9）────────────────
    //
    // 无 project 与 faulted host 是**两个真实可达的拒绝态**，两者共用同一组断言：
    // diagnostic 非空、无新增实体、无假选择、无选择意图、无意外 bootstrap。
    // 为什么不取"host 侧 create 失败（Failed）"：两个 backend 都没有 create 注入点
    // （`capi.cpp` 中 `LegacyFailureInjected()` 唯一作用于 `EditorSession_OpenProject`），
    // managed-only 约束下无法用真实 native 失败取证。`CreateEntityOutcome.Failed` 的映射
    // 保留在代码里，但**没有生产证据**（host 返回非法 index 或抛 typed 失败时才可达）。
    private static void AssertNoPhantomState(EditorHostService host, MainViewModel vm, string when)
    {
        var receipt = vm.LastCreateReceipt;
        Check(receipt.Outcome == CreateEntityOutcome.Refused,
            $"N3 {when}: the create is refused, not silently dropped (got {receipt})");
        Check(!receipt.Succeeded && receipt.Index == -1 && receipt.Name.Length == 0,
            $"N3 {when}: a refused receipt carries no index/name to act on (got {receipt})");
        Check(receipt.Diagnostic.Length > 0,
            $"N3 {when}: the refusal always carries a visible diagnostic (got '{receipt.Diagnostic}')");
        if (host.HasSession)
            Check(!vm.Entities.Any(e => e.Index == receipt.Index),
                $"N3 {when}: no entity was added to the projection");
        Check(vm.PendingSelectionIndex is null && vm.SelectionIntentResolved is null,
            $"N3 {when}: a refused create registers no selection intent");
    }

    // ── helpers ────────────────────────────────────────────

    private static string LogSince(MainViewModel vm, int count)
        => count >= vm.ConsoleLines.Count
            ? string.Empty
            : string.Join('\n', vm.ConsoleLines.Skip(count));

    /// <summary>
    /// 注入点只能来自进程启动时的环境快照（MSVC getenv 语义）。
    /// 注意：<c>EDITOR_BRIDGE_FAIL_LEGACY_OPERATION</c> 只作用于 <c>EditorSession_OpenProject</c>
    /// （见 bridge/src/capi.cpp 的唯一 LegacyFailureInjected 调用点），**无法**制造 create 失败。
    /// </summary>
    private static Scenario DetectScenario(EditorHostService host)
    {
        if (On("EDITOR_BRIDGE_FAIL_LEGACY_OPERATION"))
            throw new InvalidOperationException(
                "EDITOR_BRIDGE_FAIL_LEGACY_OPERATION only fails EditorSession_OpenProject; "
                + "it cannot produce a host-side create failure (no native create injection exists)");
        if (host.Backend == HostBackend.LegacySession) return Scenario.LegacyOrder;
        if (On("EDITOR_BRIDGE_FAIL_FRAME")) return Scenario.FrameFault;
        return Scenario.EngineHostHappy;

        static bool On(string name)
            => Environment.GetEnvironmentVariable(name) == "1";
    }
}
