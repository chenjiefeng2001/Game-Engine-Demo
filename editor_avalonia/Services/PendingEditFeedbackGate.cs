using System;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using Avalonia.Controls;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// Commit B 回归：pending edit 的 accepted ≠ executed 状态机。
///
/// 覆盖：
///   B1. 受理后立即显示 Submitted，**不得**提前显示完成；
///   B2. 帧内执行成功 → Executed（且只回执一次）；
///   B3. 帧内执行失败（用 Script 资产触发 native “asset is not a texture”）
///       → Failed + ScriptError + StatusBar，不产生 sprite 绑定；
///   B4. 窗口关闭后 PendingEditCompleted 的订阅被解除，投影回到 Idle。
///
/// 全部动作经真实 XAML 控件事件驱动（Hierarchy / Asset List / Assign 按钮），
/// 不新增 native ABI，不写 fixture。
/// </summary>
public static class PendingEditFeedbackGate
{
    private const string GateName = "[PENDING-EDIT GATE]";
    private const int TimeoutMs = 5000;

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost,
                "pending edit feedback gate requires the EngineHost backend");

            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen,
                "the Open handler reaches Ready");

            var hierarchy = Control<ListBox>(window, "HierarchyList");
            var assetList = Control<ListBox>(window, "AssetList");
            var assign = Control<Button>(window, "AssignSpriteBtn");
            var statusBar = Control<TextBlock>(window, "StatusBar");

            // 新建一个空实体：保证“受理后尚未绑定 sprite”是可判定的。
            int entitiesBefore = vm.Entities.Count;
            Click(Control<Button>(window, "ToolbarCreateEntityBtn"));
            Check(await WaitUntilAsync(
                      () => vm.Entities.Count == entitiesBefore + 1, TimeoutMs),
                "the created entity reaches the hierarchy projection");
            var entity = vm.Entities.First(e => e.Index == entitiesBefore);
            hierarchy.SelectedItem = vm.FilteredEntities.First(e => e.Index == entity.Index);
            Check(vm.SelectedEntity?.Index == entity.Index,
                "hierarchy selection drives the view model entity selection");
            Check(host.GetEntitySprite(entity.Index).Length == 0,
                "the new entity starts without a sprite binding");
            Check(vm.PendingEditState == PendingEditStatus.Idle,
                "no pending edit exists before the first assign");

            // ── B1 / B2：受理 → 执行成功 ──
            var texture = vm.FilteredAssets.First(a => a.TypeName == "Texture");
            assetList.SelectedItem = texture;
            Check(vm.SelectedAsset?.Index == texture.Index,
                "asset selection drives the view model asset selection");

            int submittedBefore = CountLog(vm, "PENDING EDIT SUBMITTED");
            int executedBefore = CountLog(vm, "PENDING EDIT EXECUTED");
            Click(assign);

            // 同步块内不可能有 pump tick：此刻必须只有“已受理”。
            Check(vm.PendingEditState == PendingEditStatus.Submitted,
                $"accepted edit must be Submitted, got {vm.PendingEditState}");
            Check(CountLog(vm, "PENDING EDIT SUBMITTED") == submittedBefore + 1,
                "acceptance is logged exactly once");
            Check(CountLog(vm, "PENDING EDIT EXECUTED") == executedBefore,
                "no completion may be reported before the frame executes");
            Check(host.GetEntitySprite(entity.Index) != texture.Path,
                "accepted is not executed: the sprite binding has not landed yet");

            Check(await WaitUntilAsync(
                      () => vm.PendingEditState == PendingEditStatus.Executed, TimeoutMs),
                "the frame execution receipt reaches the UI");
            Check(CountLog(vm, "PENDING EDIT EXECUTED") == executedBefore + 1,
                "the executed receipt is delivered exactly once (no duplicate subscription)");
            Check(host.GetEntitySprite(entity.Index) == texture.Path,
                "the session bound the texture after execution");
            Check(await WaitUntilAsync(() => vm.SelectedSprite == texture.Path, 2000),
                "the inspector refreshed from EV_ENTITY_ASSIGNED");
            log($"{GateName} B1/B2: submitted -> executed, exactly one completion");

            // ── B3：受理 → 执行失败（Script 资产在 session 侧被拒绝）──
            var script = vm.FilteredAssets.First(a => a.TypeName == "Script");
            assetList.SelectedItem = script;
            Check(vm.SelectedAsset?.Index == script.Index,
                "script asset selection is applied");

            string spriteBeforeFailure = host.GetEntitySprite(entity.Index);
            int failedBefore = CountLog(vm, "PENDING EDIT FAILED");
            Click(assign);

            Check(vm.PendingEditState == PendingEditStatus.Submitted,
                $"failed edit is first accepted, got {vm.PendingEditState}");
            Check(await WaitUntilAsync(
                      () => vm.PendingEditState == PendingEditStatus.Failed, TimeoutMs),
                "the native execution failure reaches the UI state");
            Check(CountLog(vm, "PENDING EDIT FAILED") == failedBefore + 1,
                "the failure receipt is delivered exactly once");
            Check(vm.ScriptError.Length > 0,
                "the native failure diagnostic is surfaced in ScriptError");
            Check(host.GetEntitySprite(entity.Index) == spriteBeforeFailure,
                "a failed pending edit must not change the sprite binding");
            Check(statusBar.Text == "pending edit failed (see console)",
                $"the status bar reflects the failure (was '{statusBar.Text}')");
            log($"{GateName} B3: submitted -> failed with diagnostic '{vm.ScriptError}'");

            // ── B4：窗口关闭解除订阅，投影回到 Idle ──
            window.Close();

            var field = typeof(EditorHostService).GetField(
                nameof(EditorHostService.PendingEditCompleted),
                BindingFlags.Instance | BindingFlags.NonPublic);
            Check(field is not null,
                "PendingEditCompleted has a field-like event backing field");
            Check(field!.GetValue(host) is null,
                "window close unsubscribes PendingEditCompleted (no leaked handler)");
            Check(vm.PendingEditState == PendingEditStatus.Idle,
                "window close resets the pending edit projection");
            log($"{GateName} B4: close unsubscribes and resets the projection");

            log($"{GateName} ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    private static int CountLog(MainViewModel vm, string marker)
        => vm.ConsoleLines.Count(line => line.Contains(marker, StringComparison.Ordinal));
}
