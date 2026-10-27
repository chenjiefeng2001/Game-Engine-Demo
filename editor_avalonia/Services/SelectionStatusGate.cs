using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// Commit C 回归：**选择通知**与**状态栏同步**的单一事实源。
///
/// 为什么需要独立 gate：
///   - 两处缺陷都不会让任何既有断言变红 —— ViewModel 的 <c>SelectedEntity</c> /
///     <c>StatusText</c> 本身是对的，错的只是**控件**没有被通知；
///   - 也就是说，测试一直在断言模型，用户看到的是控件，二者之间断了一根线；
///   - 因此本 gate 的每条断言都**同时**读模型与控件，并要求二者一致。
///
/// 覆盖：
///   C1. 选择通知：VM 自动选中（create 的选择意图）后，Hierarchy 的
///       <c>SelectedItem</c> 必须跟着走 —— 直接写字段而不发 PropertyChanged
///       会让 OneWay 绑定停在旧选中项上（"模型选中了新实体、列表还高亮旧的"）。
///   C2. 选择通知不重复：一次选择变更只发一次 <c>SelectedEntity</c>；
///       事件驱动的刷新不得把通知放大成风暴（否则 UI 会反复重建选中项）。
///   C3. 状态栏同步：<c>StatusBar.Text</c> 必须等于 <c>Vm.StatusText</c>，
///       包括**没有经过 handler** 的路径（native 事件驱动的集合刷新）。
///   C4. 清空：Close 之后列表不得残留选中项，状态栏回到空投影文案。
///
/// 不新增 native ABI、不新增 pump、不写 fixture；全部经真实 XAML 控件驱动。
/// </summary>
public static class SelectionStatusGate
{
    private const string GateName = "[SELECTION-STATUS GATE]";
    private const int TimeoutMs = 5000;

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost,
                "selection/status gate requires the EngineHost backend");

            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen,
                "the Open handler reaches Ready");

            var hierarchy = Control<ListBox>(window, "HierarchyList");
            var statusBar = Control<TextBlock>(window, "StatusBar");

            // ── C3 起点：Open 之后状态栏与模型一致 ──
            AssertStatusConsistent(vm, statusBar, "after open");

            // ── C1：先建立一个**旧**选中项，再让 VM 自动选中新实体 ──
            var previous = vm.FilteredEntities.First();
            hierarchy.SelectedItem = previous;
            Check(vm.SelectedEntity?.Index == previous.Index,
                "the hierarchy drives the initial selection");
            Check(ReferenceEquals(hierarchy.SelectedItem, previous)
                  || (hierarchy.SelectedItem as EntityVm)?.Index == previous.Index,
                "the list reflects the initial selection");

            int entitiesBefore = vm.Entities.Count;
            int selectedNotifications = 0;
            void CountSelection(object? _, System.ComponentModel.PropertyChangedEventArgs e)
            {
                if (e.PropertyName == nameof(MainViewModel.SelectedEntity)) selectedNotifications++;
            }

            vm.PropertyChanged += CountSelection;
            try
            {
                Click(Control<Button>(window, "ToolbarCreateEntityBtn"));

                Check(await WaitUntilAsync(
                          () => vm.Entities.Count == entitiesBefore + 1, TimeoutMs),
                    "the created entity reaches the hierarchy projection");
                Check(await WaitUntilAsync(
                          () => vm.SelectedEntity?.Index == entitiesBefore, TimeoutMs),
                    "the selection intent resolves to the new entity");

                // C3 的核心断言：native 事件驱动的刷新没有 handler 收尾，
                // 状态栏也必须跟上模型。先验 C3，避免 C1 的失败把它遮掉。
                AssertStatusConsistent(vm, statusBar, "after the event-driven create refresh");

                // C1 的核心断言：**控件**必须跟模型一致，而不是只有模型对。
                var listSelection = hierarchy.SelectedItem as EntityVm;
                Check(listSelection is not null,
                    "C1: the hierarchy must have a selected item after the auto-select "
                    + $"(model selected #{vm.SelectedEntity?.Index}, list has none)");
                Check(listSelection!.Index == entitiesBefore,
                    "C1: the hierarchy SelectedItem must follow the view model selection "
                    + $"(list=#{listSelection.Index}, model=#{vm.SelectedEntity?.Index})");

                log($"{GateName} C1/C3: auto-select reaches the list; status bar tracks the "
                    + $"event-driven refresh ('{statusBar.Text}')");
            }
            finally
            {
                vm.PropertyChanged -= CountSelection;
            }

            // ── C2：一次选择变更 = 一次通知 ──
            var other = vm.FilteredEntities.First(e => e.Index != entitiesBefore);
            int before2 = 0;
            void Count2(object? _, System.ComponentModel.PropertyChangedEventArgs e)
            {
                if (e.PropertyName == nameof(MainViewModel.SelectedEntity)) before2++;
            }

            vm.PropertyChanged += Count2;
            try
            {
                hierarchy.SelectedItem = other;
            }
            finally
            {
                vm.PropertyChanged -= Count2;
            }
            Check(vm.SelectedEntity?.Index == other.Index,
                "the user selection reaches the view model");
            Check(before2 == 1,
                $"C2: one selection change must raise exactly one SelectedEntity notification "
                + $"(got {before2})");
            AssertStatusConsistent(vm, statusBar, "after a user selection");
            log($"{GateName} C2: one selection change -> one notification ({before2})");

            // ── C2 续：VM 侧发起的选择同样只通知一次 ──
            // 必须选一个**不同于当前选中**的实体，否则"0 次通知"才是对的（值相等不发通知）。
            var third = vm.FilteredEntities.First(
                e => e.Index != other.Index && e.Index != entitiesBefore);
            int before3 = 0;
            void Count3(object? _, System.ComponentModel.PropertyChangedEventArgs e)
            {
                if (e.PropertyName == nameof(MainViewModel.SelectedEntity)) before3++;
            }

            vm.PropertyChanged += Count3;
            try
            {
                Check(vm.SelectByName(third.Name),
                    "the view-model selection entry point resolves the entity");
            }
            finally
            {
                vm.PropertyChanged -= Count3;
            }
            Check(before3 == 1,
                $"C2: a view-model driven selection must raise exactly one notification "
                + $"(got {before3})");
            Check((hierarchy.SelectedItem as EntityVm)?.Index == vm.SelectedEntity?.Index,
                "C2: the list follows a view-model driven selection");

            // 重复选择同一实体：值相等 → 不得再发通知（否则控件会反复重建选中项）。
            int before4 = 0;
            void Count4(object? _, System.ComponentModel.PropertyChangedEventArgs e)
            {
                if (e.PropertyName == nameof(MainViewModel.SelectedEntity)) before4++;
            }

            vm.PropertyChanged += Count4;
            try
            {
                vm.SelectByName(third.Name);
            }
            finally
            {
                vm.PropertyChanged -= Count4;
            }
            Check(before4 == 0,
                $"C2: re-selecting the same entity must not raise a notification (got {before4})");
            log($"{GateName} C2: view-model driven selection -> one notification ({before3}), "
                + "same-value reselect -> none");

            // ── C4：Close 之后不得残留选中项，状态栏回到空投影文案 ──
            // create 会让 project 变脏，而“Close Project”菜单项在脏状态下会弹模态确认框；
            // 模态框在 gate 里无法应答（await 永不返回 = 挂死）。
            //
            // ⚠ 也不用 Save 来“变干净”：Save 会写回 assets/gp01/Main.scene ——
            // gate 不允许污染共享 fixture（test_bridge 的 10-entity 基线就靠它）。
            // 因此走窗口关闭（与 B4 / UiStateGate 的 teardown 路径一致，同样经
            // Vm.Dispose → ResetSession → ClearProjectProjection）。
            var fresh = vm.FilteredEntities.First();
            hierarchy.SelectedItem = fresh;
            Check(vm.SelectedEntity?.Index == fresh.Index,
                "a selection exists before the close (C4 needs a non-empty starting point)");
            Check((hierarchy.SelectedItem as EntityVm)?.Index == fresh.Index,
                "the list shows that selection before the close");

            // ⚠ 这里之后**不得有任何 await**：关闭最后一个窗口会让 Avalonia lifetime
            // 开始 shutdown，进程会在挂起恢复前结束（断言静默丢失 = 假证据）。
            window.Close();

            Check(!vm.IsOpen && vm.SelectedEntity is null,
                "the close clears the projection and the selection");
            Check(hierarchy.SelectedItem is null,
                "C4: the hierarchy must not keep a stale selected item after close "
                + $"(list has #{(hierarchy.SelectedItem as EntityVm)?.Index})");
            AssertStatusConsistent(vm, statusBar, "after close");
            Check(statusBar.Text == "no project loaded",
                $"C4: the status bar returns to the empty-projection text (was '{statusBar.Text}')");
            log($"{GateName} C4: close clears the list selection and the status bar");

            log($"{GateName} ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    /// <summary>状态栏是模型的投影：两者必须逐字相等（不比较"看起来差不多"）。</summary>
    private static void AssertStatusConsistent(
        MainViewModel vm, TextBlock statusBar, string phase)
        => Check(statusBar.Text == vm.StatusText,
            $"C3: StatusBar.Text must equal Vm.StatusText {phase} "
            + $"(control='{statusBar.Text}', model='{vm.StatusText}')");
}
