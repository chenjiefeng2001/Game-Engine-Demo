# A3 CLOSED — MainViewModel Host-State Reconciliation, Selection-Intent Ordering, and Create Receipt

> **阶段**：UI-HRC-2-A / Stage A / A3（**step 2：实现 + 负例证据 → CLOSED**）
> **日期**：2026-09-28
> **状态**：`CLOSED` —— D1/D2/D3 全部落地并取证；N3 按方案 A 收敛为 Refused 负例（证据边界已写明）
> **前置**：`docs/Editor-Avalonia-HRC2-StageA-A3-Contract.md`（§8 D1~D4 已 ratify）
> **native 改动**：**0**（`capi.h` / `EditorSession.*` / `EngineHost.*` / `tests/**` 未触碰）
> **不吸收**：A6 lifecycle 重新设计、A1/A2 既有修复、Viewport/Renderer、A8
> **证据入口**：`AvaloniaEditor.exe --uistate`（+ `--uistatelegacy` 做时序对照），
> runner：`tools/run_a3_uistate_gate.ps1`

> **命名消歧**：本 slice 是 **Stage A / A3（UI-state 收口）**。计划里另有一个更早的
> **A3 = Persistent Hidden GLFW Context**（`docs/Editor-Avalonia-HRC2-A3-Audit.md`，2026-09-24，
> native A3 已 COMPLETE）。两者无关，引用时请带 `Stage A/` 前缀。

---

## 1. 门禁结果

| # | 门禁 | 关闭的 contract | 场景 / 机制 | 结果 |
|---|---|---|---|---|
| **S0** | 无 project 的 create | N3（方案 A 的一半） | 真实 `ToolbarCreateEntityBtn` 点击 → `Refused` receipt | PASS |
| **S1** | host-derived `IsOpen` | A3-01 / D1 | 真实 `OpenProjectBtn` → `State==Ready && IsOpen` | PASS |
| **N1** | create → 回执 + 自动选中 | A3-02 / A3-03 / D2 / D3 | 预置旧选择 → create → 意图解析到新 index | PASS |
| **N2** | **双时序一致性** | A3-02 | enginehost（`return-before-event`）vs legacy（`event-before-return`）两次运行**机械比较** | PASS |
| **N3** | create 失败的负例 | A3-03（方案 A） | S0（无 project）+ N5（faulted host）共用 `AssertNoPhantomState` | PASS（边界见 §4） |
| **N4** | Close 后旧事件 | A3-01 / D1 | 复用 A7-2 `PostEventWithGeneration(captured, …)` | PASS |
| **N5** | runtime fault → 投影清空 | A3-01 / D1 | `EDITOR_BRIDGE_FAIL_FRAME` | PASS |
| **—** | 生产 teardown | A3-01 | `window.Close()` → CloseProject → 终态 `Dispose` | PASS |

`--uistate` 汇总（`tools/run_a3_uistate_gate.ps1`）：

```text
[GREEN] legacy-order         N2b
[GREEN] frame-fault          N5
[GREEN] enginehost           N1/N2a/N4 + Refused
[GREEN] n2-order-equivalence N2
A3 UISTATE GATE: 4/4 GREEN
```

### 1.1 N2 的证明形态（不是叙述性结论）

runner 比对两次运行的 `A3-ORDER-FACT` 规范行：

```text
enginehost : order=return-before-event  outcome=Created newIndex=10 finalSelection=10 intentResolved=10 intentPending=none
legacy     : order=event-before-return  outcome=Created newIndex=10 finalSelection=10 intentResolved=10 intentPending=none
```

判据是两条同时成立：
1. `order` **必须不同** —— 证明两种事件顺序都被真实执行过（不是同一顺序跑两次）；
2. `outcome` / `newIndex` / `finalSelection` / `intentResolved` / `intentPending` **必须逐字段相同**。

任一不成立即 FAIL。顺序无关性因此是**可执行断言**，不是文字声明。

---

## 2. 实现清单（managed-only，零 native）

| # | 改动 | 位置 | 关闭 |
|---|---|---|---|
| 1 | `IsOpen` 由 `Entities.Count > 0` 改为 host 派生（`host.IsOpen && State == Ready`） | `ViewModels/MainViewModel.cs` | A3-01 / D1 |
| 2 | 唯一 reconcile 例程 `ReconcileWithHostState` / `ClearProjectProjection`；open 成功/失败、runtime fault、事件忽略分支、close/reset 全部走它 | 同上 | A3-01 / D1 |
| 3 | selection intent（`_pendingSelectionIndex`）+ `ResolvePendingSelectionIntent(strict)`；证据属性 `SelectionIntentResolved` / `LastCreateOrder` / `LastReconcileReason` | 同上 | A3-02 / D2 |
| 4 | `CreateEntity(string?) -> CreateEntityReceipt`（`Created`/`Refused`/`Failed` + index/name/diagnostic，空 diagnostic 被工厂拒绝）；`CreateEntityFromVm` 收敛为兼容包装 | 同上 + `ViewModels/CreateEntityReceipt.cs`（新增） | A3-03 / D3 |
| 5 | `MainWindow.OnCreateEntity` 不再直连 `_host.CreateEntity`，改走 VM receipt 入口；handler 不再自己选一次 | `MainWindow.axaml.cs` | D3 |
| 6 | 新增 managed gate `--uistate` 与场景编排 | `Services/UiStateGate.cs`（新增） | D4 |
| 7 | 新增 `--uistatelegacy`（**仅**用于 N2 的 event-before-return 顺序对照） | `Program.cs` | D2 证据 |
| 8 | PP-05 由 **KNOWN GAP 转为硬断言**（receipt + 意图解析 + 自动选中） | `Services/ProductionPathGate.cs` | A1 遗留竞态闭环 |
| 9 | 三场景 runner + N2 跨运行机械比较 + 进程表清空等待 | `tools/run_a3_uistate_gate.ps1`（新增，UTF-8 BOM） | D4 |

**未做（刻意）**：native 改动、A6 lifecycle 重设计、任何 delay/watchdog/retry 式时序修补、
第二套状态机（`--uistate` 是证据 gate，不是生产路径分支）。

---

## 3. 过程中发现并修掉的 3 个真实缺陷

### 3.1 D2 实现自身：意图被提前消费（自发现，gate 直接抓到）

第一版在 create 返回后"尝试解析并消费"选择意图。EngineHost 的顺序是
**命令先返回、事件后到**（`Dispatcher.Post`），此时集合还是旧快照 → 目标不在集合里 →
意图被当作"不可满足"丢弃 → **新实体永远不会被自动选中**。
证据表现：`legacy-order`（event-before-return）绿、`enginehost`（return-before-event）红。

修法不是加 delay，而是把两个解析点的语义分开（contract §8.1）：

| 调用点 | 集合新鲜度 | 目标不在集合里时 |
|---|---|---|
| `CreateEntity` 返回后（`strict: false`） | 可能仍是旧快照 | **保留**意图，等真实刷新 |
| `ReloadCollectionsFromSession`（`strict: true`） | 已是最新 | **丢弃**意图（不落到无关实体） |

这正是"真正证明 order independence"的分界：顺序无关性来自**解析时机**，
不是来自"等一会儿看看事件到没到"。

### 3.2 A2/F2-08 与 D1 的语义冲突（回归，由 A2 抓到）

D1 落地后 `--boot Happy` 立即失败：`the view model kept its last state instead of
re-driving a disposed host`。原因是 open 失败路径新增的 reconcile 在**终态 `Dispose` 之后**
清空了投影，而这与 A2/F2-08 的契约直接冲突：host 已不存在时，view model 必须**保留最后一帧状态**
快照，不得再驱动或清空。

处置：**两种语义明确分开** —— `_host.IsDisposed` 时 reconcile 与事件忽略分支都**不动投影**；
D1 的四态不变量（Closed/Opening/Open/Fault）只适用于 host 存活期。修复后 A2 恢复 7-7。

> 记录此条是因为它揭示了一处此前**没有写下来**的契约边界：
> "关闭/故障 → 清投影" 与 "终态 Dispose → 保留快照" 是两件事，代码里必须能被区分。

### 3.3 gate 自身的 teardown 缺陷（假证据风险）

`window.Close()` 之后接着 `await`，进程会在挂起恢复前就结束（关闭最后一个窗口触发 Avalonia
lifetime shutdown）—— 断言静默丢失 = **假证据**，比红更危险。
处置：teardown 段改为全同步（`window.Close()` 之后**禁止任何 await**），停泵证据移到 N4
（那时窗口还开着）。同时 runner 的门禁判定改以 `selftest.log` 为准，而不是 stdout。

---

## 4. N3 的证据边界（方案 A，已裁决）

**事实**：`EditorBridge` 的唯一 legacy 注入点是 `EditorSession_OpenProject`
（`bridge/src/capi.cpp` 中 `LegacyFailureInjected()` 的唯一调用处）；
`EditorSession_CreateEntity` 与 `EngineHostOpCreateEntity` **都没有注入点**；
`EditorSession::CreateEntity` 只在"无 project"时失败，而 managed 层在 D1 判断处已 `Refuse`。
→ **managed-only 约束下，host 侧 create 失败无法用真实 native 失败取证。**

**裁决（方案 A）**：N3 收敛为 **Refused 负例**，由两个**真实可达**的拒绝态共用同一组断言
（`UiStateGate.AssertNoPhantomState`）：

| 断言 | 覆盖 |
|---|---|
| `Outcome == Refused`，非静默丢弃 | 拒绝显式可见 |
| `Index == -1` 且 `Name.Length == 0` | 没有可被误用的目标 |
| `Diagnostic.Length > 0` | 诊断必须可见 |
| 投影无新增实体 | 无幽灵实体 |
| 无 `PendingSelectionIndex` / `SelectionIntentResolved` | 无假选择、无残留意图 |
| 无意外 bootstrap（无 project 场景 `!host.HasSession`） | 拒绝不产生副作用 |

**保留但无证据的部分**：`CreateEntityOutcome.Failed` 的映射仍在代码里（host 返回非法 index
或抛 typed 失败时可达），并在 `CreateEntityReceipt` 旁**明确标注"没有生产证据"**。
原计划的 `CreateFailureAsync` 断言体已删除 —— 无触发点即死代码。

**遗留风险（记入 closure）**：若将来 native 侧新增 create 注入点，必须回头为 `Failed`
分支补真实证据，否则 A3-03 的失败语义始终只有代码、没有证据。

---

## 5. 回归证据

| 证据 | 结果 |
|---|---|
| `--uistate`（3 场景 + N2 机械比较） | **4/4 GREEN** |
| `--prodpath`（A1，含 PP-05 新硬断言） | **ALL GREEN**（含生产 teardown）；`observed order: return-before-event` |
| `--boot`（A2 全 7 场景） | **7-7 GREEN** |
| `--hrc2` / `--hrc1` | ALL GREEN / exit 0 |
| `--selftest` / `--gate1` / `--gate2` / `--gate3a..3g` | 全部 exit 0 + ALL GREEN |
| integrity gate（I1 + I2 + I3） | **ALL GREEN，in-scope 合计 240 不变** |
| Release native（`EngineCore` / `EditorBridge`） | exit 0 |
| `dotnet build -c Debug` | 0 errors；4 warnings（全部 pre-existing：3× CS0414 viewport 字段 + 1× CS8602 `Phase2Gate`） |
| native 文件改动 | **0** |
| fixture（`assets/gp01/*`） | 未被 A3 写入（mtime 早于本 slice） |
| residual process | **0** |
| `git diff --check` | exit 0 |

已知无关项：Release 全量构建在 `tests/test_ecs/ComponentLifecycleTest.cpp` 失败 —— 该文件
**自 2026-07-16（commit `4d0db67`）未改动**，与 A3 无关；A3 相关的 native target
（`EngineCore` / `EditorBridge`）Release 构建 exit 0。

---

## 6. Runner / process-exit hygiene observation（独立记录）

现象：偶发"gate 已打印 `ALL GREEN`、`exit=0` 已打印，但进程在 `Environment.Exit(0)` 之后不退出"，
stderr 伴随 `interception_win: unhandled instruction`。单独复跑同一 gate 4~5 s 正常退出；
连续启动时更容易出现（尤其前一个进程刚做完 GL/GLFW 析构）。

**定性（必须保持）**：

- 这是 **runner / process-exit hygiene observation**，**不是** A3 的功能结果；
- **不重开 NHI-1** —— NHI-1 已证明的是特定 NVIDIA ShadowPlay + ASan 的退出期死锁（Closure B）；
  本条**没有新的 native stack、没有新的根因证据**，两者不合并；
- runner 的重试是 **runner hygiene**，**不是产品 workaround**（产品代码里没有任何 retry/delay）；
- 如果将来出现**稳定复现**或**新的 live native stack**，则**独立开调查 slice**，不回头污染 A3。

runner 侧已做的四件事（都在 harness，不在产品）：

1. 每个场景之间显式**等待进程表清空** + settle（不靠 `Start-Process -PassThru` 的 `HasExited`）；
2. 每场景最多重试 1 次；
3. 汇总表把功能结果与 hygiene 观察**分栏**（`(1 retry)` 归 hygiene 栏，不进功能结果）；
4. 门禁判定以 `selftest.log` 为准（`window.Close()` 与 Avalonia shutdown 竞争时 stdout 可能整段丢失）。

---

## 7. A3 判定

```text
A3-01  IsOpen 单一事实源（host 派生 + 唯一 reconcile）        CLOSED
A3-02  create → auto-select 顺序无关（selection intent）      CLOSED
A3-03  create receipt（成功/拒绝/失败 + 诊断）                 CLOSED（Failed 分支无生产证据，见 §4）
N1/N2/N4/N5 负例                                              4/4 GREEN
A1 / A2 / HRC-1 / HRC-2 回归                                 GREEN
I1 / I2 / I3                                                  GREEN（I2 = 240 不变）
native 改动                                                    0
runner process-exit 观察                                      独立记录（§6），不重开 NHI-1
A8                                                              DEFERRED
```

**下一步**：HRC-2-A Runtime Bootstrap → HRC-2 Closure。A3 已收净此前明确的三个
managed/UI-state gap，继续在 A3 加压测试收益很低；Viewport/Renderer 不由 A3 顺带打开。
