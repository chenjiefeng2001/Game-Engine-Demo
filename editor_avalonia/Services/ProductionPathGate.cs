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
/// 阶段 A / A1：生产 UI 路径证据（关闭 readiness 审计 F-1）。
///
/// 与 <see cref="Hrc2Gate"/> 的区别是本 gate 不直接调用 service API：
/// 每一个动作都通过 **真实 XAML 绑定的控件事件** 驱动（Button.Click / MenuItem.Click /
/// Hierarchy 与 Asset ListBox 的选择变化），从而证明 `MainWindow` handler →
/// `MainViewModel` → `EditorHostService` → native `EngineHost` 的整条生产链路，
/// 以及 EngineHost 模式下事件驱动的集合刷新 / Inspector 回填真的成立。
///
/// 约束：
/// - 不新建 Host，不绕过三态 gate，不新增 native ABI；
/// - 不写 repo fixture（本 slice 不点 Save Project / Save Script，理由见闭包文档）；
/// - 每个新增断言都配负路径（无 project 编辑 / 无选择 assign / 无选择 Inspector 应用）。
/// </summary>
public static class ProductionPathGate
{
    private const string ProbeName = "A1ProdPathProbe";
    private const float ProbeX = 7.5f;
    private const float ProbeY = 1.25f;
    private const float ProbeZ = -2.5f;

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            PP_01_BaselineAndNoProjectEdit(window, host, vm, log);
            PP_02_OpenHandlerDrivesHost(window, host, vm, log);
            await PP_03_CloseHandlerAsync(window, host, vm, log);
            PP_04_ReopenAndHierarchySelection(window, host, vm, log);
            await PP_05_CreateEntityHandlerAsync(window, host, vm, log);
            PP_06_RenameHandler(window, host, vm, log);
            PP_07_TransformHandler(window, host, vm, log);
            PP_08_CameraComponentHandlers(window, host, vm, log);
            await PP_09_AssignSpritePendingEditAsync(window, host, vm, log);
            await PP_10_AssignScriptHandlerAsync(window, host, vm, log);
            await PP_11_PlayReloadStopTransportAsync(window, host, vm, log);
            log("[HRC-PROD-PATH GATE] ALL GREEN");
        }
        catch (Exception ex)
        {
            log("[HRC-PROD-PATH GATE] FAIL: " + ex.Message);
            return 1;
        }

        // 全部断言通过后才走生产 teardown 路径（失败时直接 exit 1，不做 teardown 掩盖）。
        try
        {
            PP_12_ProductionTeardown(window, host, vm, log);
        }
        catch (Exception ex)
        {
            log("[HRC-PROD-PATH GATE] FAIL (teardown): " + ex.Message);
            return 1;
        }
        log("[HRC-PROD-PATH GATE] ALL GREEN (incl. production teardown)");
        return 0;
    }

    // PP-01：进程启动态 + 无 project 编辑的负路径。
    private static void PP_01_BaselineAndNoProjectEdit(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(host.Backend == HostBackend.EngineHost, "production backend is EngineHost");
        Check(host.IsEngineHost, "the single host owns an EngineHost channel");
        Check(ReferenceEquals(host, vm.Host), "window and view model share one host instance");
        Check(host.State == HostRuntimeState.Starting, "host starts in Starting");
        // F-2 现状证据：用户未点击 Open Project 之前，进程内不存在 EngineHost/pump。
        Check(!host.HasSession, "no native host exists before the Open Project handler runs");
        Check(vm.Entities.Count == 0 && !vm.IsOpen, "the view model starts with no project");
        Check(vm.StatusText == "no project loaded", "status bar starts empty");
        Check(host.RuntimeCounts == (0, 0), "no frames were pumped before the project is open");
        Check(vm.OffUiThreadEventCount == 0, "no event was consumed off the UI thread");

        // 负路径：无 project 时点击 Hierarchy "+ Entity" 必须显式拒绝，且不得顺手 bootstrap。
        int consoleBefore = vm.ConsoleLines.Count;
        Click(N<Button>(window, "HierarchyCreateEntityBtn"));
        Check(LogSince(vm, consoleBefore).Contains("+ Entity ignored: no project"),
            "the create handler refuses the edit when no project is open");
        Check(!host.HasSession, "a refused edit must not bootstrap a native host");
        Check(!vm.IsOpen, "a refused edit leaves the UI closed");
        log("[PP-01] startup baseline + refused no-project edit (no accidental bootstrap)");
    }

    // PP-02：Open Project 按钮 → host Ready + 集合刷新 + pump 起点。
    private static void PP_02_OpenHandlerDrivesHost(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        var root = FindRepoRoot();
        Check(root is not null, "repo root with assets/gp01 was found");

        Click(N<Button>(window, "OpenProjectBtn"));

        Check(host.HasSession, "the Open handler bootstrapped the single native host");
        Check(host.State == HostRuntimeState.Ready, "the Open handler reaches the Ready state");
        Check(host.IsOpen, "the host reports an open project");
        Check(Directory.GetCurrentDirectory() == root!.FullName,
            "the Open handler points the process CWD at the repo root");
        Check(File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "assets", "gp01", "manifest.json")),
            "repo-relative asset paths resolve against the new CWD (GP01 runtime semantics)");

        var (entities, assets) = host.GetCounts();
        Check(entities > 0 && assets > 0, $"session has content ({entities} entities / {assets} assets)");
        Check(vm.Entities.Count == entities, "the entity collection was refreshed from the host");
        Check(vm.Assets.Count == assets, "the asset collection was refreshed from the host");
        Check(vm.FilteredEntities.Count == entities, "the hierarchy filter view is in sync");
        Check(vm.FilteredAssets.Count == assets, "the asset filter view is in sync");
        // F-1 核心：IsOpen 的事实源是 host 驱动的集合刷新，不是 gate 直接赋值。
        Check(vm.IsOpen, "MainViewModel.IsOpen is derived from the refreshed collection");
        Check(vm.StatusText.Contains($"{entities} objects") && vm.StatusText.Contains($"{assets} assets"),
            $"status bar reports the loaded project ({vm.StatusText})");
        // F-2 证据：pump 只由 UI timer 驱动，Open 本身不产生帧。
        Check(host.RuntimeCounts.frames == 0, "the Open handler itself does not pump a frame");
        log("[PP-02] Open Project handler drives host bootstrap, collections and status bar");
    }

    // PP-03：File > Close 菜单项（MenuItem.Click）→ CloseProject 而非 Dispose。
    private static async Task PP_03_CloseHandlerAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(!vm.IsDirty, "the close handler must be exercised from a clean state (no modal prompt)");
        Click(N<MenuItem>(window, "FileCloseItem"));

        Check(!host.IsOpen, "the Close handler closed the project");
        Check(host.State == HostRuntimeState.Starting, "the host returns to Starting, not Stopping");
        Check(!host.IsDisposed, "CloseProject is not the terminal Dispose (A7-C06)");
        Check(vm.Entities.Count == 0 && vm.Assets.Count == 0, "the view model collections are cleared");
        Check(vm.FilteredEntities.Count == 0 && vm.FilteredAssets.Count == 0, "filter views are cleared too");
        Check(!vm.IsOpen && vm.SelectedEntity is null, "IsOpen and the selection are cleared");
        Check(vm.StatusText == "no project loaded", "the status bar returns to the empty state");

        // pump 必须随 CloseProject 停止（唯一 pump，A7-C03）。
        await Task.Delay(150);
        var frames = host.RuntimeCounts.frames;
        await Task.Delay(150);
        Check(host.RuntimeCounts.frames == frames,
            $"the service-owned pump stopped with the project (frames stayed at {frames})");
        log("[PP-03] File>Close handler closes the project and stops the pump");
    }

    // PP-04：重新打开 + Hierarchy 列表点击驱动选择与 Inspector 回填。
    private static void PP_04_ReopenAndHierarchySelection(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Click(N<Button>(window, "OpenProjectBtn"));
        Check(host.State == HostRuntimeState.Ready, "the host is reusable after CloseProject");
        var (entities, _) = host.GetCounts();
        Check(vm.Entities.Count == entities, "collections are rebuilt on reopen");

        var list = N<ListBox>(window, "HierarchyList");
        Check(vm.SelectedEntity is null, "no selection before the list interaction");
        Check(!vm.HasSelection, "the inspector reports no selection before the click");
        var first = vm.FilteredEntities[0];
        list.SelectedItem = first;
        Check(vm.SelectedEntity?.Index == first.Index,
            "the Hierarchy SelectionChanged handler drives the view model selection");
        Check(vm.HasSelection, "HasSelection follows the handler");
        Check(vm.SelectedName == first.Name, $"inspector name is back-filled ({vm.SelectedName})");

        var pos = host.GetEntityPosition(first.Index);
        Check(vm.PosX == pos.x.ToString("0.0###") && vm.PosY == pos.y.ToString("0.0###")
              && vm.PosZ == pos.z.ToString("0.0###"),
            $"inspector transform is back-filled from the session ({vm.PosX}, {vm.PosY}, {vm.PosZ})");
        Check(vm.SelectedSprite.Length > 0 && vm.SelectedScript.Length > 0,
            "sprite/script fields are back-filled (path or the explicit '(none)' marker)");
        Check(vm.HasCamera == host.HasComponent(first.Index, "Camera"),
            "camera back-fill matches the session component state");
        log("[PP-04] hierarchy click path drives selection, inspector and camera back-fill");
    }

    // PP-05：工具栏 "+ Entity" → 事件驱动的集合刷新（生产路径）。
    private static async Task PP_05_CreateEntityHandlerAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int before = vm.Entities.Count;
        int eventsBefore = vm.ConsumedEventCount;
        int consoleBefore = vm.ConsoleLines.Count;

        Click(N<Button>(window, "ToolbarCreateEntityBtn"));

        Check(await WaitUntilAsync(() => vm.Entities.Count == before + 1, 3000),
            "the created entity reaches the hierarchy through the marshalled native event");
        Check(host.GetCounts().entities == before + 1, "the session really holds the new entity");
        string name = host.GetEntityName(before) ?? "";
        Check(name.Length > 0, "the new entity has a generated name");
        Check(vm.Entities.Any(e => e.Index == before && e.Name == name),
            "the view model shows the same name as the session");
        Check(vm.ConsumedEventCount > eventsBefore, "the create event was consumed on the UI thread");
        Check(vm.OffUiThreadEventCount == 0, "no create event was consumed off the UI thread");
        Check(vm.IsDirty, "the session reports dirty state through the production path");
        Check(LogSince(vm, consoleBefore).Contains("EV EntityCreated"),
            "the console shows the marshalled create event");

        // A3/D2：EngineHost 事件经 Dispatcher.Post 异步到达（return-before-event：命令先返回、
        // 事件后到）。旧行为是 handler 内 create-then-select 抢在集合刷新前执行，因此新实体不会
        // 自动选中；A3 改为登记 selection intent 并由刷新解析，此处成为**硬断言**。
        Check(vm.LastCreateReceipt.Outcome == CreateEntityOutcome.Created
              && vm.LastCreateReceipt.Index == before,
            $"A3/D3 the production handler produced a Created receipt ({vm.LastCreateReceipt})");
        Check(vm.SelectionIntentResolved == before && vm.PendingSelectionIndex is null,
            "A3/D2 the selection intent resolved to the new index and was consumed exactly once");
        Check(vm.SelectedEntity?.Index == before,
            "A3/D2 the new entity is auto-selected once the marshalled refresh lands");
        log($"[PP-05] create+auto-select resolved through the selection intent "
            + $"(observed order: {vm.LastCreateOrder})");

        // 用户真实后续动作：在 Hierarchy 里点中新实体。
        var list = N<ListBox>(window, "HierarchyList");
        list.SelectedItem = vm.FilteredEntities.First(e => e.Index == before);
        Check(vm.SelectedEntity?.Index == before, "the new entity can be selected from the hierarchy");
        log("[PP-05] create handler reaches the session; event-driven refresh confirmed");
    }

    // PP-06：RenameBox 双向绑定 + Rename 按钮。
    private static void PP_06_RenameHandler(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int idx = vm.SelectedEntity!.Index;
        var box = N<TextBox>(window, "RenameBox");
        box.Text = ProbeName;
        Check(vm.RenameInput == ProbeName,
            "the two-way binding feeds the rename precondition the handler reads");
        int consoleBefore = vm.ConsoleLines.Count;

        Click(N<Button>(window, "RenameEntityBtn"));

        Check(host.GetEntityName(idx) == ProbeName, "the session entity was renamed");
        Check(vm.SelectedEntity?.Name == ProbeName, "the selection survives the refresh by index");
        Check(vm.SelectedName == ProbeName, "the inspector shows the new name");
        Check(vm.FilteredEntities.Any(e => e.Index == idx && e.Name == ProbeName),
            "the hierarchy filter view was rebuilt with the new name");
        Check(vm.RenameInput == "", "the handler clears the rename input");
        Check(LogSince(vm, consoleBefore).Contains("renamed:"), "the handler logged the rename result");
        log("[PP-06] rename box binding + Rename handler round-trip through the host");
    }

    // PP-07：Inspector 数值应用（Apply Transform）。
    private static void PP_07_TransformHandler(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int idx = vm.SelectedEntity!.Index;
        vm.PosX = ProbeX.ToString("0.0###");
        vm.PosY = ProbeY.ToString("0.0###");
        vm.PosZ = ProbeZ.ToString("0.0###");
        int consoleBefore = vm.ConsoleLines.Count;

        Click(N<Button>(window, "ApplyTransformBtn"));

        var pos = host.GetEntityPosition(idx);
        Check(Math.Abs(pos.x - ProbeX) < 1e-4f && Math.Abs(pos.y - ProbeY) < 1e-4f
              && Math.Abs(pos.z - ProbeZ) < 1e-4f,
            $"the transform reached the session ({pos.x}, {pos.y}, {pos.z})");
        Check(LogSince(vm, consoleBefore).Contains("transform applied"),
            "the handler logged the applied transform");
        log("[PP-07] Apply Transform handler writes through the host seam");
    }

    // PP-08：Camera 组件三按钮 + 无选择时的静默 no-op（已知缺口）。
    private static void PP_08_CameraComponentHandlers(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int idx = vm.SelectedEntity!.Index;
        Click(N<Button>(window, "AddCameraBtn"));
        Check(vm.HasCamera, "the inspector reveals the camera block after Add Camera");
        Check(host.HasComponent(idx, "Camera"), "the session really holds the Camera component");

        vm.CameraZoom = "2.5";
        Click(N<Button>(window, "ApplyCameraBtn"));
        Check(host.GetComponentProperty(idx, "Camera", "zoom") == "2.5",
            "Apply Camera wrote the component property through the host");
        Check(vm.CameraZoom == "2.5", "the camera fields are back-filled from the session");

        Click(N<Button>(window, "RemoveCameraBtn"));
        Check(!vm.HasCamera && !host.HasComponent(idx, "Camera"),
            "Remove Camera removed the component and the inspector agrees");

        // P3-A 设计：Hierarchy 失选不得回写 null 覆盖 VM 选择（MainWindow 有明确注释），
        // 因此清空选择不是生产动作；清除只发生在 delete / close。
        // 同理 "apply 被忽略" 分支在生产 UI 不可达：Inspector 编辑块绑定 HasSelection。
        var list = N<ListBox>(window, "HierarchyList");
        list.SelectedItem = null;
        Check(vm.SelectedEntity?.Index == idx,
            "a deselect in the list does not clobber the view model selection (P3-A contract)");
        Check(N<Button>(window, "AddCameraBtn").IsEnabled,
            "the camera buttons follow HasCamera after the component was removed");
        log("[PP-08] camera component handlers work; deselect cannot clobber the VM selection");
    }

    // PP-09：Asset Browser 选择 + Assign Sprite 的 accepted ≠ executed 生产证据。
    private static async Task PP_09_AssignSpritePendingEditAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        // PP-09 继续使用 PP-08 保留的选择（Hierarchy 失选不会清空 VM 选择）。
        int idx = vm.SelectedEntity!.Index;

        var tabs = N<TabControl>(window, "MainTabs");
        tabs.SelectedIndex = 1;   // Asset Browser：与用户点击标签一致
        var assetList = N<ListBox>(window, "AssetList");

        // 负路径：没有资产选择时 Assign 必须显式拒绝，且不进入 pending-edit queue。
        int consoleBefore = vm.ConsoleLines.Count;
        var baseline = host.PendingEditCounts;
        Click(N<Button>(window, "AssignSpriteBtn"));
        Check(LogSince(vm, consoleBefore).Contains("assign ignored: need entity + asset selection"),
            "Assign Sprite refuses the edit when no asset is selected");
        await Task.Delay(120);
        Check(host.PendingEditCounts == baseline,
            "the refused assign did not enqueue a pending edit");

        var texture = vm.FilteredAssets.First(a => a.TypeName == "Texture");
        assetList.SelectedItem = texture;
        Check(vm.SelectedAsset?.Index == texture.Index,
            "the asset list selection drives the view model selection");

        var countsBefore = host.PendingEditCounts;
        Click(N<Button>(window, "AssignSpriteBtn"));
        Check(host.GetEntitySprite(idx).Length == 0, "accepted is not executed: no sprite bound yet");
        Check(vm.SelectedSprite == "(none)", "the inspector still shows '(none)' before the frame");

        Check(await WaitFramesAsync(host, 2, 4000), "the UI-driven pump advanced frames");
        var countsAfter = host.PendingEditCounts;
        Check(countsAfter.executed == countsBefore.executed + 1
              && countsAfter.accepted == countsBefore.accepted + 1
              && countsAfter.failed == countsBefore.failed,
            $"exactly one pending edit was accepted and executed (accepted={countsAfter.accepted} "
            + $"executed={countsAfter.executed} failed={countsAfter.failed})");
        Check(host.LastPendingEditExecutedWithContextCurrent,
            "the edit executed inside the context-current frame window");
        Check(host.GetEntitySprite(idx) == texture.Path, "the session bound the sprite path");
        Check(await WaitUntilAsync(() => vm.SelectedSprite == texture.Path, 2000),
            "the inspector was refreshed by the marshalled EV_ENTITY_ASSIGNED event");
        log("[PP-09] assign sprite keeps accepted != executed and refreshes the inspector by event");
    }

    // PP-10：Assign Script（SESSION op，同步）+ 事件回填。
    private static async Task PP_10_AssignScriptHandlerAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        int idx = vm.SelectedEntity!.Index;
        var script = vm.FilteredAssets.First(a => a.TypeName == "Script");
        N<ListBox>(window, "AssetList").SelectedItem = script;
        Check(vm.SelectedAsset?.Index == script.Index, "a script asset is selected");

        Click(N<Button>(window, "AssignScriptBtn"));
        Check(host.GetEntityScript(idx) == script.Path, "the script binding reached the session");
        Check(await WaitUntilAsync(() => vm.SelectedScript == script.Path, 2000),
            "the inspector was refreshed by the marshalled EV_ENTITY_SCRIPT_ASSIGNED event");
        log("[PP-10] assign script handler + event-driven inspector refresh");
    }

    // PP-11：Play / Reload / Stop 传输链（唯一 pump + 每帧至多一次 tick）。
    private static async Task PP_11_PlayReloadStopTransportAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        var play = N<Button>(window, "PlayBtn");
        var reload = N<Button>(window, "ReloadBtn");
        var stop = N<Button>(window, "StopBtn");
        var (_, ticksBeforePlay) = host.RuntimeCounts;

        Click(play);
        Check(vm.IsRunning, "the Play handler enters the running state");
        Check(play.Content as string == "Stop", "the play button flips to Stop");
        Check(reload.IsEnabled && stop.IsEnabled, "the transport buttons enable from the IsRunning binding");
        Check(vm.ScriptError.Length == 0, $"no runtime error surfaced on play ({vm.ScriptError})");

        Check(await WaitUntilAsync(() => host.RuntimeCounts.ticks >= ticksBeforePlay + 3, 4000),
            "the UI-driven pump ticks the running session");
        var (_, ticksPlaying) = host.RuntimeCounts;
        var frame = host.LastFrameProof;
        Check(frame is not null && frame.Value.Status == EditorBridgeApi.EngineHostStatusSuccess,
            "the published frame result reports Success while playing");
        Check(frame is not null && frame.Value.Stage == EditorBridgeApi.EngineHostStageTick,
            "the published frame result comes from the Tick stage");

        Click(reload);
        Check(vm.IsRunning, "Reload keeps the runtime alive");
        Check(vm.ScriptError.Length == 0, $"no runtime error surfaced on reload ({vm.ScriptError})");
        Check(await WaitUntilAsync(() => host.RuntimeCounts.ticks > ticksPlaying, 4000),
            "the runtime keeps ticking after Reload");

        Click(stop);
        Check(!vm.IsRunning, "the Stop handler leaves the running state");
        Check(play.Content as string == "Play", "the play button flips back to Play");
        Check(!reload.IsEnabled && !stop.IsEnabled, "the transport buttons disable again");

        // 停止后仍会 pump 帧（Application 活着），但 session tick 必须归零增长。
        var (_, ticksAfterStop) = host.RuntimeCounts;
        Check(await WaitFramesAsync(host, 2, 4000), "the pump keeps running after Stop");
        Check(host.RuntimeCounts.ticks == ticksAfterStop,
            $"no session tick happens while stopped (ticks stayed at {ticksAfterStop}, "
            + $"was {ticksPlaying} while playing)");
        log("[PP-11] Play/Reload/Stop transport: one pump, tick only while playing");
    }

    // PP-12：生产 teardown 路径（窗口关闭 → Vm.Dispose → ResetSession → CloseProject → Dispose）。
    private static void PP_12_ProductionTeardown(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        Check(vm.IsDirty, "unsaved edits left the project dirty before teardown");
        var (frames, ticks) = host.RuntimeCounts;
        Check(frames > 0, $"the UI-driven pump really advanced frames (frames={frames})");

        // 窗口关闭会触发 lifetime shutdown，因此这里必须同步取证据：
        // 任何 await 都会让进程在断言之前结束（静默通过 = 假证据）。
        window.Close();

        Check(vm.Entities.Count == 0 && vm.Assets.Count == 0 && !vm.IsOpen,
            "closing the window disposes the view model and clears the collections");
        Check(vm.SelectedEntity is null && vm.StatusText == "no project loaded",
            "the view model returned to its empty state");
        Check(!host.IsOpen && host.State == HostRuntimeState.Starting,
            "window close performs CloseProject, not the terminal Dispose");
        Check(!host.IsDisposed, "the host survives the window close (Program owns the terminal Dispose)");
        Check(vm.OffUiThreadEventCount == 0, "no event was consumed off the UI thread during the session");

        host.Dispose();
        Check(host.IsDisposed && host.State == HostRuntimeState.Stopping,
            "the terminal Dispose reaches the Stopping state");
        host.Dispose();
        Check(host.IsDisposed, "the terminal Dispose is idempotent");
        Check(Throws<ObjectDisposedException>(host.EnsureRuntime), "a disposed host refuses recreation");
        log($"[PP-12] production teardown: window close -> CloseProject -> terminal Dispose "
            + $"(ui-driven pump: {frames} frames / {ticks} session ticks)");
    }

    // ── helpers ────────────────────────────────────────────

    private static T N<T>(MainWindow window, string name) where T : Control
        => window.FindControl<T>(name) ?? throw new InvalidOperationException($"control '{name}' not found");

    /// <summary>通过真实 routed Click 事件驱动 XAML 绑定的 handler。</summary>
    private static void Click(Button button)
        => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent, button));

    /// <summary>菜单项与按钮同路径：raise 其 Click routed event，触发 XAML 绑定的 handler。</summary>
    private static void Click(MenuItem item)
        => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent, item));

    private static string LogSince(MainViewModel vm, int count)
        => count >= vm.ConsoleLines.Count
            ? string.Empty
            : string.Join('\n', vm.ConsoleLines.Skip(count));

    private static async Task<bool> WaitUntilAsync(Func<bool> predicate, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return true;
            await Task.Delay(20);
        }
        return predicate();
    }

    /// <summary>等待 UI 驱动的 pump 至少再推进 n 帧（唯一 pump 的直接证据）。</summary>
    private static async Task<bool> WaitFramesAsync(EditorHostService host, int frames, int timeoutMs)
    {
        var start = host.RuntimeCounts.frames;
        return await WaitUntilAsync(() => host.RuntimeCounts.frames >= start + (ulong)frames, timeoutMs);
    }

    private static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

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
