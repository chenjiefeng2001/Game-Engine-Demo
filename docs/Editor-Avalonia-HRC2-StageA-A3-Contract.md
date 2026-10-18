# Stage A / A3 — MainViewModel Refresh & Create-State Closure（Contract Freeze）

> **阶段**：UI-HRC-2-A / Stage A / A3（step 1：审计 + contract 冻结，零代码改动 → step 2 已实现并收口）
> **日期**：2026-09-28
> **状态**：`RATIFIED`（§8 D1~D4 已批准）→ 实施结果见 §11，**A3 已 CLOSED**
> **闭包文档**：`docs/Editor-Avalonia-HRC2-StageA-A3-Closure.md`
> （**A3 CLOSED — MainViewModel Host-State Reconciliation, Selection-Intent Ordering, and Create Receipt**）
> **前置**：`docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`（F-1）·
> `docs/Editor-Avalonia-HRC2-StageA-A2-Closure.md`（F-2，7-7 GREEN）·
> `docs/Editor-Avalonia-HRC2-NHI-1-Charter.md`（Closure B，已关闭）
> **性质**：**纯 managed / UI-state 收口 slice**

---

## 1. Scope

| ID | 目标 | 类型 | 必须证明 |
|---|---|---|---|
| **A3-01** | `IsOpen` 单一事实源 | **行为变更** | UI 的 open/closed 状态只由 Host/服务状态派生；**不允许本地状态与 Host 分叉**，也不允许把已失效的 project/entity 继续呈现为"当前" |
| **A3-02** | create → auto-select 异步竞态 | **行为变更** | 创建成功后，**无论事件先后/调度顺序**，最终 selection 与真实 entity 一致；失败**不能**伪装成成功 |
| **A3-03** | handler create 结果 / 失败行 | **可观测性变更** | 用户能看到 create 的成功/失败结果；失败保留 host/native diagnostic，不被吞掉 |

## 2. 明确不是 A3

- 不重新设计 A6 生命周期 / callback barrier / transaction；
- 不重开 A1 的两个修复（`ResetSession` 重建 `Filtered*`、`EvEntityCreated → RefreshDirtyState`）与
  A2 的三个修复（`HostOperationException` 传播、pump fault 检测、`Shutdown` 完成才 `Destroy`）
  ——它们已有证据闭环；
- 不碰 Viewport / Picking / Gizmo / Renderer / ECS / Content Pipeline / `test_ecs`；
- 不做 A8（跨平台 receipt 仍 DEFERRED）；
- **不重造事件抑制机制**：generation guard 与 `_suppressEvents` 属 A7-2 已冻结契约，A3 只复用。

---

## 3. 现状审计（代码事实，行号可验证）

### 3.1 `IsOpen` 的定义与全部消费点

```text
MainViewModel.cs:67    public bool IsOpen => Entities.Count > 0;     ← 事实源是"本地集合"
```

| 消费点 | 位置 | 分叉时的后果 |
|---|---|---|
| `SaveProject` | `MainViewModel.cs:467` | 分叉时可能对已关闭的 project 发起保存 |
| `CreateEntityFromVm` | `:507` | 同上 |
| `ImportAssetFromPath` | `:571` | 同上 |
| `RenameAssetFromVm` | `:590` | 同上 |
| `PlayScript` | `:658` | 同上 |
| `OnCreateEntity` | `MainWindow.axaml.cs:213` | 落到 host 后被三态 gate 拒绝（typed 错误行），而非友好的 "no project" |
| `OnImportAsset` | `MainWindow.axaml.cs:281` | 同上 |
| `OnViewportTimerTick` | `MainWindow.axaml.cs:410` | 仅 legacy backend 会启动该 timer |

而 Host 侧的事实源是 `EditorHostService.IsOpen` + `HostRuntimeState`
（`Starting / Ready / Faulted / Stopping`）。**两套事实源没有任何强制同步**。

### 3.2 已确认的分叉点（4 个）

| # | 分叉 | 触发路径 | 现状 | 证据 |
|---|---|---|---|---|
| D1 | **Fault 后集合不清理** | A2 `RaiseRuntimeFault` 把 host 置 `IsOpen=false` + `State=Faulted`，但 VM 的 `Entities` 原样保留；`OnRuntimeFaulted`（`MainViewModel.cs:822`）只改 ScriptError/StatusText/IsRunning | `Vm.IsOpen == true` 而 host 已关闭 | A2 `FrameFault` 场景：`+ Entity` 点击后拿到的是 host typed 拒绝，而不是 "no project" |
| D2 | **open 失败不重载集合** | `MainViewModel.OpenProject:448-455` 失败即 `return false` | 若此前有 project，旧集合继续显示，host 已 `IsOpen=false` | 读码 |
| D3 | **事件守卫只"忽略"不"清理"** | `OnNativeEvent:850` 在非 Ready 时 `return`（A2 引入） | 保证不被污染，代价是旧状态继续呈现 | 读码 |
| D4 | **Close 后 VM 与 host 不同步来源** | `ResetSession:479` 清集合并置状态，但 `IsOpen` 仍由集合派生 | 当前恰好一致（集合空 ⇒ IsOpen false），属**巧合一致**而非契约 | 读码 |

### 3.3 create 路径与事件时序（A3-02 的根因）

```text
MainWindow.OnCreateEntity            MainWindow.axaml.cs:211-219
  if (!Vm.IsOpen) { 拒绝 }
  int idx = _host.CreateEntity(null);            ← 命令（同步阻塞到 runtime 返回）
  Vm.SelectByName(_host.GetEntityName(idx));    ← 在"此刻"的 Entities 里找
```

两个 backend 的**事件送达时机相反**（关键事实）：

| backend | 事件路径 | 相对命令返回的时机 |
|---|---|---|
| Legacy（HRC-1 注入链） | `LegacySessionChannel` callback → `RaiseEvent` 直连（`EditorHostService.cs:964`） | **event-before-return**（在命令内同步送达 UI 线程） |
| EngineHost（生产） | `EngineHostChannel.MarshalEvent` → `Dispatcher.UIThread.Post`（`EditorHostService.cs:691`） | **return-before-event**（UI 线程被命令阻塞，Post 只能在其返回后处理） |

因此：

- legacy 下 `SelectByName` 能找到新实体 → 自动选中成立；
- EngineHost 下 `SelectByName` 必然落空 → **新实体创建成功但不被选中**（A1 记录为 KNOWN GAP）；
- 事件随后到达 → `ReloadCollectionsFromSession:920` 以 `keepIdx = 当前选中索引` 保持**旧**选择
  （`:945-953`），新实体进入列表但未被选中。

**更深一层**：`ReloadCollectionsFromSession:937` 每次都**重建 `EntityVm` record**，
因此任何跨刷新持有旧引用的选择都会失效 —— 选择意图必须以**索引**为载体，不能持引用。

### 3.4 create 结果行的现状（A3-03）

| 入口 | 成功行 | 失败行 |
|---|---|---|
| `MainViewModel.CreateEntityFromVm:505` | `entity created: <name>` | `CREATE FAILED: <diagnostic>` |
| `MainWindow.OnCreateEntity:211`（**生产路径**） | 无（只有事件行 `EV EntityCreated: <name>`） | **无**；`idx < 0` 被忽略，`GetEntityName(-1)` → `""`，`SelectByName("")` 返回 false 且不记录 |

⇒ 两条 create 路径语义不一致，且生产路径的失败是**静默**的。

### 3.5 可复用（不得重造）

| 机制 | 位置 | A3 用法 |
|---|---|---|
| generation guard + `_suppressEvents` | `EngineHostChannel`（A7-2） | 关闭后的旧 event 抑制，原样复用 |
| `RequiresOpenProject` 事件守卫 | `MainViewModel.cs:831` | 扩展为"host 不可用 ⇒ 不刷新"，并与新的 reconcile 衔接 |
| `TryHost` / `ReportHostFailure` | `MainViewModel.cs:~750-820`（A2） | create 失败的结构化诊断出口 |
| `HostRuntimeState.Faulted` | `EditorHostService.cs`（A2） | Fault 态的 UI 呈现依据 |
| `ResetSession` + `RebuildFiltered*` | `MainViewModel.cs:479`（A1） | Closed 态清理的既有实现 |
| `DerivedStateChanged` / 状态栏同步 | `MainWindow.SyncStatusBar` | UI 呈现通道 |

---

## 4. 冻结的状态模型

```text
Closed
  事实源            host: State ∈ {Starting(未打开), Stopping} 且 IsOpen == false
  IsOpen            false
  集合              Entities / Assets / FilteredEntities / FilteredAssets 均为空
  选择              SelectedEntity == null && SelectedAsset == null
  Inspector         "(none)" / 空
  呈现              不得显示任何"当前 project/entity"

Opening
  事实源            host: bootstrap 或 open 命令进行中
  IsOpen            false
  集合              保持 Closed 态（**不得**预先填充任何实体）
  选择              无
  禁止              不得出现"假 entity / 假 selection"

Open/Ready
  事实源            host: State == Ready && IsOpen == true
  IsOpen            true
  集合              反映 host 状态（数量与名称一致）
  选择              要么无，要么指向集合中真实存在的 entity

Fault
  事实源            host: State == Faulted（runtime 不可用）
  IsOpen            false
  集合              **必须清空**（不得把已失效 project 呈现为当前）
  选择              必须清空
  呈现              状态栏/Console 保留 fault 诊断（不得静默）
```

**唯一事实源规则（冻结）**：

> `IsOpen` 与所有"当前 project"派生量，**只能**由 `EditorHostService.IsOpen` + `HostRuntimeState`
> 派生；本地集合是 host 状态的**投影**，不是独立事实源。
> 任何 host 状态变化都必须经过**唯一**的 reconcile 例程把投影拉齐，不允许各 handler 各自判断。

---

## 5. 三条 contract（冻结提案）

### A3-01 `IsOpen` 单一事实源

- `IsOpen` 改为由 host 派生（`State == Ready && host.IsOpen`），**不再**由 `Entities.Count` 派生；
- 新增**唯一** reconcile 例程（建议名 `ReconcileWithHostState()`），在下列时机被调用：
  host 状态变化（Ready / Faulted / Stopping）、`OpenProject` 成功或失败、`ResetSession`、
  以及任何刷新类事件被消费之前；
- Fault ⇒ **清空集合 + 选择**（不只是改标志位），并保留 fault 诊断；
- open 失败 ⇒ 同样 reconcile 到 Closed（消除 D2）；
- 8 个 `IsOpen` 消费点的**语义不变**（仍是"没有 project 就拒绝"），但拒绝理由与日志来自同一个事实源。

### A3-02 create → auto-select 与顺序无关

- VM 内引入**选择意图**载体：以 **entity index** 记录（不持 `EntityVm` 引用），
  在下一次 `ReloadCollectionsFromSession` 时解析；解析不到 ⇒ 丢弃意图（**不伪造选择**）；
- create 失败 ⇒ 不登记意图，并记录失败行；
- 契约要求 **event-before-return 与 return-before-event 两种顺序都得到同一最终 selection**；
- 意图在 `ResetSession` / reconcile 到 Closed / 实体被删除时必须清空。

### A3-03 create receipt（成功/失败都可见）

- 生产 handler 与 VM 入口**收敛到同一个** create 入口（建议 VM 方法返回结构化 receipt：
  `Created(index, name)` / `Refused(reason)` / `Failed(diagnostic)`）；
- `MainWindow.OnCreateEntity` 不再直接调用 `_host.CreateEntity`；
- 成功行与失败行都进 Console；失败必须携带 host/native diagnostic（A2 的 `HostOperationException`
  或 `GetLastError`），禁止空诊断、禁止静默。

---

## 6. 负例矩阵（冻结）

| # | 场景 | 必须成立 | 归属 contract |
|---|---|---|---|
| N1 | **正常 Create** | entity 创建；事件在命令返回后到达；auto-select 最终落到该 entity | A3-02 / A3-03 |
| N2 | **两种事件顺序** | event-before-return 与 return-before-event 都不产生错误 selection（最终 selection 一致） | A3-02 |
| N3 | **Create 失败** | host 失败；diagnostic 可见；**不新增 entity**；**不产生假 selection** | A3-02 / A3-03 |
| N4 | **CloseProject 后的旧 event** | 不重新污染 UI（沿用 A7-2 generation guard，**不重造机制**） | 复用契约 |
| N5 | **Bootstrap / runtime fault 后 UI 状态** | `IsOpen` 不再显示旧 project；集合/选择被清空；诊断可见 | A3-01 |

> N2 需要能人为控制事件顺序。当前架构下 EngineHost 只会出现 return-before-event
> （UI 线程被命令阻塞），因此**顺序无关性必须由"意图 + 解析"设计保证**，而不是靠时序假设。

---

## 7. 证据计划（closure 门槛）

| 证据 | 内容 | 门槛 |
|---|---|---|
| **A3 contract gate**（新增，建议 `--uistate`） | N1~N5 全部负例 + 状态模型四态不变量 | 全绿 |
| **`--prodpath`（A1 生产路径）** | 现有 12 个 PP 断言**不得回归** | ALL GREEN |
| **`--hrc2` / `--hrc1` / `--selftest` / `--gate1..3g`** | managed 回归 | 全 exit 0 |
| **native** | `test_hrc` 15/15 · `test_bridge` 41/41 · `test_scripting` 27/27（Debug + Release） | 全绿 |
| **integrity gate** | I1 + I2 + I3，in-scope 合计 **240 不变**（A3 不新增 native 测试） | ALL GREEN |
| **build** | `dotnet build` 0 errors；warning 不新增 | 通过 |
| **hygiene** | fixture hash/mtime 未变 · residual process = 0 · `git diff --check` exit 0 | 通过 |

**closure 定义**：A3-01/02/03 的 contract tests + `--prodpath` + HRC-2 回归 + build + hygiene **全绿**，
且 create 路径具备**明确的 success/failure receipt**（否则 A3-03 只是"没崩"，不是诚实性闭环）。

**不作为 closure 依据**：按钮能点、界面看起来对、单个手工 smoke 通过。

---

## 8. 需要 ratify 的 4 条决策

| # | 决策 | 建议 | 状态 |
|---|---|---|---|
| **D1** | `IsOpen` 的实现方式 | 改为 **host 派生**（`State == Ready && host.IsOpen`），并配唯一 reconcile 例程；**不接受**继续用 `Entities.Count > 0` | **RATIFIED** |
| **D2** | auto-select 的载体 | **选择意图（index）+ 刷新时解析**；不接受"解析事件 payload 名字"或"依赖事件时序" | **RATIFIED** |
| **D3** | create 入口 | 生产 handler 与 VM 入口**收敛为一个**返回结构化 receipt 的入口；`MainWindow` 不再直连 `_host.CreateEntity` | **RATIFIED** |
| **D4** | A3 证据形态 | **新增 `--uistate` gate**（需要 fault 注入与顺序控制，`--prodpath` 表达不了），`--prodpath` 保持为回归项；不接受把 A3 断言塞进 `--prodpath` | **RATIFIED** |

### 8.1 D2 的实现细化（实现期发现，已落地）

选择意图的**解析时机**必须区分两种事件顺序，否则顺序无关性不成立：

| 调用点 | 集合新鲜度 | 目标不在集合里时 |
|---|---|---|
| `CreateEntity` 返回后（`strict: false`） | **可能仍是旧快照**（EngineHost 的常态：命令先返回、事件后到） | **保留**意图，等真实刷新 |
| `ReloadCollectionsFromSession`（`strict: true`） | **已是最新** | **丢弃**意图（不可满足，绝不落到无关实体） |

早期实现缺这个区分：create 返回后立刻"尝试解析并消费"，导致 EngineHost 顺序下意图被提前丢弃、
新实体永远不被自动选中（`legacy-order` 通过而 `enginehost` 失败）。这正是 N2 必须跨两种顺序
机械比较的原因。

---

## 9. OPEN 项：N3（host 侧 create 失败）缺触发点

N3 要求"create 失败：无新增实体、无假 selection、diagnostic 可见"。审计结论：

- `EditorBridge` 的**唯一** legacy 注入点是 `EditorSession_OpenProject`
  （`bridge/src/capi.cpp` 中 `LegacyFailureInjected()` 的唯一调用处），
  `EditorSession_CreateEntity` **没有**注入保护。
- EngineHost 侧 `EngineHostOpCreateEntity` 同样没有注入点。
- `EditorSession::CreateEntity` 只在"无 project"时失败（返回 -1 + `create entity: no project loaded`），
  而 managed 层在更早的 D1 事实源判断处就 `Refuse` 了 —— 到不了 native。

**结论：managed-only 约束下，host 侧 create 失败无法用真实 native 失败来取证。**

**裁决（2026-09-28，用户批准）：选方案 A。** 记录如下，理由与代价一并留档：

| 方案 | 内容 | 代价 | 状态 |
|---|---|---|---|
| **A** | N3 收敛为 **Refused 负例**：无 project + faulted host 两个**真实可达**的拒绝态共用同一组断言（diagnostic 非空 / 无新增实体 / 无假选择 / 无选择意图 / 无意外 bootstrap） | create 失败分支缺一条真实 native 失败证据 | **已采纳** |
| B | 批准新增 `EDITOR_BRIDGE_FAIL_CREATE` 注入点 | 违反 A3"零 native 改动"，需独立 native 证据与 A1/A2 式 closure | 未采纳 |
| C | 加 managed-only 测试 seam（下次 create 强制返回 -1） | 属测试替身，不是生产证据；且在生产类型里留测试钩子 | 未采纳 |

**方案 A 的落地形态**：

- `UiStateGate.AssertNoPhantomState(host, vm, when)` 是 N3 的唯一断言体，被 S0（无 project）与
  N5（faulted host）两个场景共用 —— 两处都是真实可达的拒绝路径，不是构造出来的假失败。
- `CreateEntityOutcome.Failed` 的映射**保留在代码里**（host 返回非法 index 或抛 typed 失败时可达），
  但**明确标注无生产证据**：注释写在 `UiStateGate` 与 `CreateEntityReceipt` 旁，不让读者误以为已验证。
- 原计划的 `CreateFailureAsync` 断言体**已删除**（方案 A 下无触发点，留着就是死代码）。

**遗留风险（记入 closure）**：若将来 native 侧新增 create 注入点，必须回头把 `Failed` 分支
补上真实证据，否则 A3-03 的"失败语义"始终只有代码、无证据。

---

## 10. 当前判定

```text
A3-01/02/03  contract         RATIFIED（§8 D1~D4 全部批准）
A3 step 1（审计 + 冻结）      COMPLETE（零代码改动）
A3 step 2（实现）             COMPLETE（D1/D2/D3 落地；N3 按 §9 方案 A 收敛为 Refused 负例）
A3 closure                   CLOSED（`--uistate` 4-4 GREEN / `--prodpath` ALL GREEN /
                                       A2 `--boot` 7-7 / I2=240 不变 / native 零改动）
A2                            IMPLEMENTED / 7-7 GREEN / finding CLOSED
NHI-1                         CLOSED / Closure B
HRC-2-A readiness             CONDITIONAL PASS（F-1 已清、F-2 已实现、F-3 见 A3 §9 方案 A）
A8                            DEFERRED
```

---

## 11. Step 2 实施结果（2026-09-28）

### 11.1 代码改动（managed-only，零 native 改动）

| 文件 | 改动 |
|---|---|
| `ViewModels/CreateEntityReceipt.cs`（新增） | `CreateEntityOutcome{Created,Refused,Failed}` + `CreateEntityReceipt`（index/name/diagnostic；空 diagnostic 被工厂拒绝） |
| `ViewModels/MainViewModel.cs` | **D1** `IsOpen => _host.IsOpen && State == Ready`；新增唯一 reconcile 例程 `ReconcileWithHostState` / `ClearProjectProjection`；open 成功/失败、runtime fault、事件忽略分支、close/reset 全部走它。**D2** `_pendingSelectionIndex` + `ResolvePendingSelectionIntent(strict)`（两个解析点，见 §8.1）+ `SelectionIntentResolved` / `LastCreateOrder` / `LastReconcileReason` 证据属性。**D3** `CreateEntity(string?) -> CreateEntityReceipt`；`CreateEntityFromVm` 收敛为 bool 兼容包装 |
| `MainWindow.axaml.cs` | `OnCreateEntity` 不再直连 `_host.CreateEntity`，改走 VM receipt 入口；handler 不再自己选一次（选择由意图解析）。新增 `--uistate` 挂载点 |
| `Services/UiStateGate.cs`（新增） | `--uistate` gate：S0/S1 + N1/N2/N4/N5，产出 `A3-ORDER-FACT` 规范行 |
| `Program.cs` | 新增 `--uistatelegacy`（仅用于 N2 的 event-before-return 顺序对照） |
| `Services/ProductionPathGate.cs` | **PP-05 由 KNOWN GAP 改为硬断言**（receipt + 意图解析 + 自动选中） |
| `tools/run_a3_uistate_gate.ps1`（新增） | 三场景 runner + N2 跨运行**机械比较**；UTF-8 BOM |

### 11.2 与既有契约的冲突及处理

| 冲突 | 处理 |
|---|---|
| A2/F2-08 要求终态 Dispose 后 VM **保留最后状态** | `ReconcileWithHostState` / 事件忽略分支在 `_host.IsDisposed` 时**不动投影**。D1 四态只适用于 host 存活期（Closed/Opening/Fault）。修复前 `--boot Happy` 失败（"the view model kept its last state…"），修复后 7-7 恢复 |
| A1/A2 冻结了日志文案 `"+ Entity ignored: no project"` | `Refused` 的 diagnostic 固定为 `"no project"`，receipt 结构化但文案不动 |
| N4 复用 A7-2 generation guard | 通过既有 `host.PostEventWithGeneration(captured, EvEntityCreated, "stale")` 注入旧事件，未新造事件抑制 |

### 11.3 证据结果

| 证据 | 结果 |
|---|---|
| `--uistate` enginehost（N1/N2a/N4 + Refused + 生产 teardown） | GREEN |
| `--uistate` frame-fault（N5，`EDITOR_BRIDGE_FAIL_FRAME`） | GREEN |
| `--uistate` legacy-order（N2b，`--uistatelegacy`） | GREEN |
| N2 跨运行机械比较 | GREEN：`order=return-before-event` vs `event-before-event`（两种顺序都真实执行过），而 `outcome/newIndex=10/finalSelection=10/intentResolved=10/intentPending=none` **逐字段相同** |
| `--prodpath`（A1，含 PP-05 新硬断言） | ALL GREEN（含生产 teardown）；`observed order: return-before-event` |
| `--boot` × 7（A2 全场景） | 7-7 GREEN |
| `--hrc2` / `--hrc1` / `--selftest` / `--gate1` / `--gate2` / `--gate3a..3g` | 全部 exit 0 + ALL GREEN |
| integrity gate（I1+I2+I3） | ALL GREEN，in-scope 合计 **240 不变** |
| Release native（`EngineCore` / `EditorBridge`） | exit 0 |
| `dotnet build -c Debug` | 0 errors，4 warnings（全部 pre-existing） |
| N3 | **CLOSED（方案 A）**：收敛为 Refused 负例，由 S0（无 project）+ N5（faulted host）两处真实拒绝共用 AssertNoPhantomState 取证；Failed 映射保留但**无生产证据**（见 §9） |

### 11.4 环境注记（NHI-1 同类，非产品缺陷）

`tools/run_a3_uistate_gate.ps1` 偶发遇到"gate 已 `ALL GREEN` 且已打印 `exit=0`，但进程不退出"
（stderr 伴随 `interception_win: unhandled instruction`），单独复跑同一 gate 4~5 s 正常退出。
这与 NHI-1 Closure B 记录的"overlay interception × native 析构"退出期问题同类，发生在
`Environment.Exit` 之后（managed 代码已全部执行完），因此不是 A3 的行为回归。runner 对此：
① 每个场景之间显式等待进程表清空 + settle；② 最多重试 1 次；③ 汇总表**显式标注 `(1 retry)`**，
不静默掩盖；④ 门禁判定以 `selftest.log` 为准（`window.Close()` 与 Avalonia 自身 shutdown 竞争时
stdout 可能整段丢失）。
