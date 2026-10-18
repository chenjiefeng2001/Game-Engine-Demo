# Editor Avalonia HRC-2 阶段 A / A1 — 生产 UI 路径证据（关闭 F-1）

> **阶段**：UI-HRC-2-A / Stage A / A1
> **日期**：2026-09-26
> **状态**：`COMPLETE`
> **前置**：`docs/Editor-Avalonia-HRC2-Implementation-Plan.md`（HRC-2-A Readiness Audit → F-1 / F-2）
> **证据入口**：`AvaloniaEditor.exe --prodpath` → `[HRC-PROD-PATH GATE] ALL GREEN`
> **性质**：只加证据 + 修证据当场暴露的两个局部缺陷。未改 native 契约、未新增 ABI、未改 pump ownership、未改 `Application`/Renderer/Viewport。

---

## 1. 为什么 gate 必须驱动真实控件事件

Readiness Audit 的 F-1 结论：此前所有 managed 证据都来自 gate harness **直接调用 service**，
`MainWindow` 的 XAML → handler 链、`MainViewModel` 的派生状态在 EngineHost backend 下
从未被执行过一次。因此本 slice 的唯一合法动作是「用户动作」：

| 用户动作 | 生产链路 | gate 的驱动方式 |
|---|---|---|
| 点击 Open | XAML `Click=OnOpenProject` | `RaiseEvent(Button.ClickEvent)` |
| 点击 File>Close | XAML `Click=OnCloseProject` | `RaiseEvent(MenuItem.ClickEvent)` |
| 点击 + Entity / Rename | XAML `Click=OnCreateEntity/OnRenameEntity` | `RaiseEvent(Button.ClickEvent)` |
| 在 Hierarchy 点选实体 | `SelectionChanged=OnHierarchySelectionChanged` | `HierarchyList.SelectedItem = item` |
| 在 RenameBox 输入 | `Text="{Binding RenameInput, Mode=TwoWay}"` | 写 `RenameBox.Text` 后**断言绑定已回填**再点 Rename |
| 在 Asset Browser 点选 / Assign | `SelectedItem` 双向绑定 + 面板按钮 | `AssetList.SelectedItem` + `RaiseEvent` |
| 点击 Play / Reload / Stop | `Click=OnPlay/OnReloadScript/OnStop` | `RaiseEvent(Button.ClickEvent)` |
| 关闭窗口 | `OnClosed` → `Vm.Dispose` → `ResetSession` → `CloseProject` | `window.Close()` |

约定：

- `RaiseEvent` 触发的就是 XAML 中注册的那个 handler，不新增旁路入口、不新增 Host；
- 三态 gate 不绕过——无 project 的编辑必须被**显式拒绝**而不是静默通过；
- 新增诊断只读面（`EditorHostService.LastFrameProof` / `RuntimeCounts`）用于直接证明
  「UI timer 驱动的唯一 pump 真的在推进帧」，不受三态 gate 约束（evidence probe 不得成为
  runtime 依赖操作），未 bootstrap 时返回 `null` / `(0,0)`。

---

## 2. Gate 覆盖（PP-01 ~ PP-12）

| Gate | 断言要点 | 关闭的 F-1 条目 |
|---|---|---|
| PP-01 | 单一 host、backend=EngineHost、启动态 `Starting` / 无 native host / `frames==0`；负路径：无 project 点 `+ Entity` 被显式拒绝且**不 bootstrap** | F-2 现状证据 |
| PP-02 | Open handler → `HasSession` + `Ready` + CWD 指向 repo root + 两个集合与过滤视图同步 + `IsOpen` 由集合刷新导出 + StatusBar 文本 + `frames==0`（Open 本身不打帧） | MainWindow 真实接入 / `IsOpen` 事实源 |
| PP-03 | File>Close（MenuItem）→ `Starting`、非 Dispose（A7-C06）、集合/过滤视图/选择/状态栏全清、**pump 随之停止**（A7-C03） | MainWindow 真实接入 / 集合刷新 |
| PP-04 | 重新打开后 host 可复用；Hierarchy 点选 → handler 驱动 VM 选择；`SelectedName` / `Pos*` / sprite / script / Camera 全部从会话回填 | Inspector 回填 / `RefreshCamera` |
| PP-05 | 工具栏 `+ Entity` → 会话实体 + 事件在 UI 线程消费 + `IsDirty` + 过滤视图出现新实体 | 事件驱动刷新 / `IsDirty` |
| PP-06 | RenameBox 双向绑定回填 → Rename handler → 会话改名 + 选择按 index 保持 + Inspector 改名 + 过滤视图重建 + 输入框清空 | Inspector 回填 |
| PP-07 | Apply Transform → 会话坐标 `(7.5, 1.25, -2.5)` + Console 结果行 | MainWindow 真实接入 |
| PP-08 | `+ Camera` / `Apply Camera`（zoom=2.5）/ `Remove Camera` 往返；Hierarchy 失选不得回写 null（P3-A 契约） | Inspector 回填 |
| PP-09 | 无资产选择时 Assign Sprite 显式拒绝且 **pending-edit 计数不变**；受理后 sprite 仍未绑定（accepted ≠ executed）；pump 帧内执行且 `lastExecutedWithContextCurrent==true`、`failed` 不增；Inspector 由 marshal 后的 `EV_ENTITY_ASSIGNED` 回填 | A7-C03 / A7-C04 生产路径 |
| PP-10 | Assign Script（同步 SESSION op）→ 会话绑定 + `EV_ENTITY_SCRIPT_ASSIGNED` 回填 Inspector | 事件驱动刷新 |
| PP-11 | Play → `IsRunning` + 按钮翻转 + 传输按钮启用；`tick` 只在运行期增长（≥3 帧）；`LastFrameProof` = `Success` / `stage=Tick`；Reload 保持运行且继续 tick；Stop 后按钮复位且 **tick 不再增长** | 唯一 pump / 每帧至多一次 tick |
| PP-12 | 生产 teardown：脏状态成立 → `window.Close()` → `Vm.Dispose` → `ResetSession`（集合清空、状态栏复位）→ `CloseProject`（非 Dispose）→ 终态 `Dispose` 幂等 + 拒绝重建 | 生命周期 / 无 late callback |

关键量化证据（连续两次运行完全一致）：

```text
[PP-12] production teardown: window close -> CloseProject -> terminal Dispose
        (ui-driven pump: 14 frames / 4 session ticks)
```

---

## 3. 过程中发现并当场修复的 2 个真实缺陷

两者都命中 F-1 描述的「`MainViewModel` refresh 路径在 EngineHost 模式下从未被执行」，
并且在 legacy backend 下同样存在（与 A7 无关的既有缺陷），此前**没有任何 gate 覆盖**。

### 3.1 关闭工程后过滤视图残留

- 现象：PP-03 断言 `FilteredEntities` / `FilteredAssets` 已清空失败。
- 根因：`MainViewModel.ResetSession()` 只清 `Entities` / `Assets`，而 Hierarchy 与 Asset Browser
  绑定的是 `Filtered*` 视图；`Filtered*` 仅在 `RebuildFiltered*()` 时重建 →
  关闭工程后 `IsOpen == false` 但两个列表仍显示旧条目。
- 修复：`ResetSession()` 在清空选择之后调用 `RebuildFilteredEntities()` + `RebuildFilteredAssets()`。
- 影响面：仅 UI 派生视图，无 native / ABI 影响。

### 3.2 `+ Entity` 之后 DIRTY 徽标不亮

- 现象：PP-05 断言 `vm.IsDirty` 失败。
- 根因：`EV_ENTITY_CREATED` 是唯一缺少 `RefreshDirtyState()` 的编辑类事件；而
  `MainWindow.OnCreateEntity` 直接调 `_host.CreateEntity`（不经过会刷新脏状态的
  `Vm.CreateEntityFromVm`），于是这条生产路径上脏状态没有任何刷新点。
- 修复：事件分支补 `RefreshDirtyState()`。
- 其余编辑类事件（moved / deleted / renamed / assigned / script-assigned / asset-imported /
  asset-renamed / component-changed）本就带刷新，行为不变。

---

## 4. 记录但本 slice 不修改的 2 项

1. **create + auto-select 竞态**（`[PP-05][KNOWN GAP]`，每次运行都会打印）：
   `OnCreateEntity` 在 `_host.CreateEntity` 之后立刻 `Vm.SelectByName(...)`，而 EngineHost 的
   事件经 `Dispatcher.UIThread.Post` 异步到达，集合尚未刷新，因此**新实体不会被自动选中**。
   legacy backend 的事件是同步回调，所以这个缺陷从未暴露。修复需要「pending selection」
   概念（属于 A3 的集合刷新契约），A1 只测量与记录，不改行为。
2. **handler 路径与 VM 方法的 Console 输出不对称**：`OnCreateEntity` 不打印 `entity created:`
   行（该行来自 `Vm.CreateEntityFromVm`），且 create 失败时没有错误行。信息没有丢失
   （`EV EntityCreated` 事件行在 Console 中），但失败路径不诚实——留给 UI 诚实性后续项。

---

## 5. 负路径证据

| 场景 | 期望 | 证据 |
|---|---|---|
| 无 project 点击 `+ Entity` | 显式拒绝 + **不**创建 native host | PP-01 |
| 无资产选择点击 Assign Sprite | 显式拒绝 + pending-edit 三元组完全不变 | PP-09 |
| 无选择时 Apply Transform / Apply Camera | 生产 UI 不可达（Inspector 编辑块绑定 `HasSelection`），VM 内为防御性分支 | PP-04 / PP-08 |
| Hierarchy 失选 | 不得回写 null 覆盖 VM 选择（P3-A 契约） | PP-08 |
| 关闭后再次点击编辑 | 回到 `Starting`，runtime 依赖操作被三态 gate 拒绝 | PP-03 → PP-04 |

---

## 6. 本 slice 明确不做

- `SaveProject` / `ScriptSave` 落盘闭环：会写 repo fixture（延续 A7-2 的不做清单）。
  因此 DIRTY 只断言「变脏」，不验证「保存后变干净」。
- F-2 决策（bootstrap 由启动触发还是由用户动作触发）：属 Stage A / A2。
  A1 只把现状固化为断言（PP-01 `!host.HasSession`、PP-02 `frames == 0`、PP-03 pump 停止）。
- Viewport / Picking / Gizmo / Renderer / ECS / test_ecs / Linux receipt（A8 仍 DEFERRED）。

---

## 7. 回归结果

```text
--prodpath      ALL GREEN（含 production teardown）两次连续运行一致，exit 0
--hrc2          [HRC-G2 GATE] ALL GREEN     exit 0
--hrc1          [HRC-G1 GATE] ALL GREEN     exit 0
--selftest      [PHASE0 GATE] ALL GREEN     exit 0
--gate1/2/3a/3b/3c/3d/3g  全部 ALL GREEN    exit 0
dotnet build    0 errors / 4 warnings（4 条均为既有 Phase 7 viewport WIP baseline，未新增）
test_hrc        Debug 15/15   Release 15/15
test_bridge     Debug 41/41   Release 41/41
test_scripting  Debug PASS    （I2 计数校验通过）
ctest Debug     10/11 in-scope 全绿（test_ecs 为既有 `Not Run`，按 EXCLUDED baseline 处理）
integrity_gate  I1 + I2 + I3 ALL GREEN，in-scope total = 240（未变）
fixtures        assets/gp01/{manifest.json,Main.scene,game.lua} mtime 与 hash 未变
                （assets/vs01/arena_director.lua 被 test_content 的 VS01 用例重写为相同字节，
                  git 无 diff，属既有行为）
residual        targeted process = 0；git diff --check exit 0
```

native 侧本 slice **零改动**：`capi.h` / `EngineHost.*` / `test_hrc` / `test_bridge` 未触碰，
`ENGINE_HOST_ABI_VERSION` 仍为 `(1 << 16) | 1`，Evidence-Baseline 的 native 计数不变。

---

## 8. 决定

```text
F-1 生产 UI 路径无证据:            CLOSED（本 slice 关闭，2 个缺陷已修）
F-2 bootstrap 由用户动作触发:      OPEN → Stage A / A2 决策
HRC-2-A readiness:                CONDITIONAL PASS → F-1 已清，允许进入 A2
HRC-G2P-01~08:                     NOT GREEN（G2P-08 仍属 A8 DEFERRED）
next slice:                       Stage A / A2（启动即 bootstrap 决策与落地）
```
