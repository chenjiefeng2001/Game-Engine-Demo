# Editor Avalonia — Commit A / B / C 收尾

> **日期**：2026-10-21
> **状态**：A / B / C 三个 slice 均已实现并**端到端实测 GREEN**
> **前置**：P2 UI Proof CLOSED（D1 达成）；D3 Pick / D4 Resize / A8 仍 Deferred
> **纪律**：三个 slice 各自独立提交，不互相混入

本文记录三个 slice 的**实现事实**与**可执行证据**。三个 slice 都在同一条生产路径上
（`MainWindow` → `MainViewModel` → `EditorHostService` → `EngineHost`），但解决的问题
彼此独立：A 是驱动源所有权，B 是异步回执语义，C 是模型→控件的通知。

---

## 0. 环境前提（本次实测建立的事实）

本次首次在本机建立了可运行的 native + managed pair，因此 A / B / C 的端到端结论
不再是"待验证"：

| 项 | 值 |
|---|---|
| native 构建 | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON -DENABLE_ASAN=OFF` |
| bridge 产物 | `build/bridge/Debug/EditorBridge.dll`（10,689,536 B）/ `build/bridge/Release/EditorBridge.dll`（2,174,464 B） |
| managed 构建 | `dotnet build editor_avalonia -c Debug` / `-c Release` → 两者均 **0 errors** |
| staged pair | `editor_avalonia/bin/<Config>/net9.0/EditorBridge.dll` 与上述产物**同字节**（csproj `CopyToOutputDirectory=Always`） |

### 0.1 恢复构建时修复的三个仓库状态问题（非本次 slice 引入）

这三点是**仓库自身**的既有破损，不修则 native 构建无法完成。它们与 A/B/C 的语义无关，
但必须在同一工作树中修好才能取得证据：

1. `third_party/imgui_build/CMakeLists.txt` 在工作树中**被删除**，而
   `third_party/CMakeLists.txt:79` 无条件 `add_subdirectory(imgui_build)` ⇒ configure 直接失败。
   已 `git checkout --` 恢复。
2. `third_party/JoltPhysics/Build/` 目录（48 个文件）在工作树中**被删除**，而
   `third_party/CMakeLists.txt:157` 以 `EXISTS .../Build/CMakeLists.txt` 为条件
   ⇒ 物理引擎被静默跳过（**不是**构建失败，而是证据面缩水）。已恢复。
3. `engine/{include/Engine/Debug,src/Debug}`（CrashHandler / StackTrace / ProfilerCore /
   ScreenshotCapture / CrashContext）在工作树中**被删除**，而 `Application.cpp`、
   `MemoryPanel.cpp`、`ProfilerPanel.h` 仍 `#include` 它们 ⇒ `EngineCore` 编译失败
   （`error C1083`）。已恢复。

> 三者均为 submodule / tracked 文件在工作树中被删除，**不是**本 session 的改动。
> 恢复后 `git status` 对这三处重新干净。

### 0.2 两个运行时 staging 缺口（**未修**，仅记录）

| 缺口 | 现象 | 影响 |
|---|---|---|
| `OpenAL32.dll` 未被 csproj 正确 staging | csproj 硬编码指向 `..\build\sandbox\EditorDemo\Debug\OpenAL32.dll`，但 CMake 实际把它放在 `build/third_party/openal-soft/<Config>/OpenAL32.dll` ⇒ `EditorBridge.dll` 的 PE 导入表无法解析，宿主报 `Unable to load DLL 'EditorBridge' (0x8007007E)` | 需要**手工** `cp build/third_party/openal-soft/<Config>/OpenAL32.dll editor_avalonia/bin/<Config>/net9.0/` 才能运行任何 managed gate |
| `assets/gp01/Main.scene` 被 `.gitignore:88` 排除且无 committed generator | 该 fixture 在本次工作树中**缺失**，所有 gate 的 `OpenProject` 都失败（`cannot open file`） | 需从 `editor_avalonia/spike_gp07_e3/assets/gp01/Main.scene`（md5 `faabc37448f44197d6966708532c1ef9`，10 entities）恢复 |

两者都属 build/deployment hygiene，**刻意不在本次 slice 中修改**（避免一个 slice 同时动两件事），
但**必须**记录：否则"gate 跑不起来"会被误读成"代码有问题"。`docs/Pending-Decisions-Brief.md`
已就 12 个 gitignored fixture 记录同类问题。

---

## 1. Commit A — 唯一 pump owner

### 1.1 问题

P2 UI Proof 之前，production `EngineHost` 后端**没有任何 pump owner**：全仓 `RuntimeTick(`
的调用者只有两个 gate。因此 production UI 从不产生帧。P2 UI 建立了第一个 pump，
但它的形态是"gate 显式逐帧 pump"，不能作为产品驱动。

### 1.2 实现

`EditorHostService.EngineHostChannel.BeginRuntime()` 持有**唯一** `DispatcherTimer`
（33 ms），tick 内 `PumpOnce()` → `EngineHost_PumpOneFrame` → 成功后 `FrameAdvanced`。
`MainWindow` 只订阅 `FrameAdvanced` 呈现，不持有任何周期性 pump。

关键实现点：

| 点 | 实现 |
|---|---|
| 幂等启动 | `BeginRuntime` 在 `_pump is not null` 时直接 return，**不重置时钟**（否则会把运行中的泵重新锚定） |
| 时钟锚定 | `_clock.Restart()` + `_lastPumpSeconds = 0`，首帧只含启动后的间隔，不含启动前的墙钟 |
| 定时器归属 | 每个 timer 只驱动自己：`if (ReferenceEquals(_pump, timer)) PumpOnce();`，Close/Reopen 后残留的旧 tick 不得泵新 runtime |
| 停止 | `StopPump()` 置 `_pumpActive=false`、`_pump=null`、换新 `Stopwatch` |
| dt 收敛 | `ClampPumpDeltaSeconds`：非有限（NaN/±Inf）与负数 → `0.0`；`> 0.1` → `0.1`；其余原样 |

### 1.3 计数语义（审阅要求的第一类问题）

**`PumpInvocations` 在 native 调用前递增**，因此失败调用会计入它。这是刻意的，
并由 `PumpFailures` 显式区分，gate 断言的是**恒等式**而不是"调用次数 = 帧数"：

```text
frames == PumpInvocations - PumpFailures
```

`PumpOnce()` 失败分支：`Interlocked.Increment(ref _pumpFailures)` → `RaiseRuntimeFault` → **return**
（失败的帧不作为新帧发布给 UI）。`RuntimeTick()`（gate/诊断入口）保持同一语义。

### 1.4 四项审阅结论

| 检查项 | 结论 | 机械核验 |
|---|---|---|
| 唯一产品 pump 源 | ✅ | `EngineHost_PumpOneFrame` 的调用者只有 `PumpOnce`（L706）与 `RuntimeTick`（L1144）；`RuntimeTick` 的调用者**全部**是 gate（`Hrc2Gate` / `P2ManagedFrameGate` / `P2UiProofGate` / `D3ManagedPickGate`） |
| UI 消费帧事件 | ✅ | `FrameAdvanced` 只有一个订阅者 `OnFrameAdvanced`，其函数体只调 `_viewportPresenter.PresentLatestFrame(_host)`；presenter 内 `RuntimeTick` 命中 **0** |
| 计数语义 | ✅ | 见 §1.3；`PumpInvocations` 只在 `PumpOnce` / `RuntimeTick` 的 native 调用处递增，不由 timer tick 计数 |
| Gate 计数逻辑 | ✅ | 固定 1.5 s 窗口，同时采样 `PumpInvocations` / `PumpFailures` / native `frames`；断言 `frames == pumps - failures`，并对**频率设上界**（`<= 50/s`，第二驱动会推到 ~60/s）与下界（`>= 8/s`，防止测量窗口无效） |

`dt` 收敛的七条纯函数断言（负/NaN/+Inf/-Inf/0/正常正数/超限）全部在
`PumpOwnershipGate` 内以**纯函数**方式验证，不依赖 runtime。

### 1.5 实测

**Debug**

```text
[PUMP-OWNER GATE] idle:    pumps=28 failures=0 frames=28 rate=18.5/s lastDt=0.048s
[PUMP-OWNER GATE] playing: pumps=27 failures=0 frames=27 rate=17.7/s lastDt=0.048s
[PUMP-OWNER GATE] ALL GREEN (one pump owner, rate<=50/s, attempts == frames + failures)
pumpowner exit=0
```

**Release**

```text
[PUMP-OWNER GATE] idle:    pumps=32 failures=0 frames=32 rate=21.1/s lastDt=0.048s
[PUMP-OWNER GATE] playing: pumps=32 failures=0 frames=32 rate=21.3/s lastDt=0.047s
[PUMP-OWNER GATE] ALL GREEN (one pump owner, rate<=50/s, attempts == frames + failures)
pumpowner exit=0
```

- Play / Stop 前后**没有**出现第二个 driver（频率未翻倍，`attempts == frames + failures` 恒等）。
- 频测值 18.5~21.3/s 低于名义 30.3/s，原因是本机 UI 线程被 gate 自身的 `Task.Delay` 窗口与
  首次 GL 初始化占用；关键是它**远低于**双驱动的 ~60/s 上界，判据有效。

### 1.6 文档 superseded

`docs/Editor-Avalonia-HRC3-P2-UI-Proof.md` 已加入 superseded note：旧文档中的 pump ownership
描述反映的是 P2 UI Proof 当时的实现事实；自 Commit A 起所有权集中到
`EditorHostService`，`RuntimeTick()` 保留给 gate / 诊断，`PumpOwnershipGate` 已进入回归面。

---

## 2. Commit B — Pending Edit 完成反馈

### 2.1 问题

`AssignSprite` 是 GL 依赖操作，native 侧只返回 `accepted`（已入 pending-edit queue），
真实结果在下一帧 `DrainPendingEdits` 之后才产生。旧 UI 把 `accepted` 直接呈现为"完成"，
而 `PendingEditCompleted` 事件**没有任何订阅者**——事件链是断的。

### 2.2 实现

**订阅生命周期**：`MainWindow` 构造期订阅一次 `_host.PendingEditCompleted += OnPendingEditCompleted`，
`OnClosed` 对称 `-=`。不重复订阅、不泄漏。

**状态机**（`PendingEditStatus`）：

| 状态 | 含义 | 触发 |
|---|---|---|
| `Idle` | 无在途 edit | 初始 / 清投影 / 窗口关闭 |
| `Submitted` | native 已受理，等下一帧 | `AssignSprite` 返回 accepted 后 |
| `Executed` | 帧内执行成功 | `PendingEditCompleted(executed: true)` |
| `Failed` | 帧内执行失败 | `PendingEditCompleted(executed: false)` + 诊断 |

**硬约束**：
- `AssignSpriteToSelected` 在 `PendingEditState == Submitted` 时**拒绝**再次排队
  （managed 侧只跟踪单笔回执，第二笔会丢掉第一笔的结果）；
- 成功后**不清空** `ScriptError`（它可能属于无关的 Lua / Play 诊断）；
- 失败时诊断进 `ScriptError` + Console + `StatusBar`，且**不得**改变 sprite 绑定；
- 事件契约是 UI 线程；越界时记录 `A7-C04 violation` 并**不**跨线程改控件。

### 2.3 实测

```text
[PENDING-EDIT GATE] B1/B2: submitted -> executed, exactly one completion
[PENDING-EDIT GATE] B3: submitted -> failed with diagnostic
                     'assign sprite assets/gp01/game.lua -> Entity: assign sprite: asset is not a texture'
[PENDING-EDIT GATE] B4: close unsubscribes and resets the projection
[PENDING-EDIT GATE] ALL GREEN
pendingedit exit=0
```

- **B1**：受理后同步块内 `PendingEditState == Submitted`，且 `SUBMITTED` 日志恰好 +1、
  `EXECUTED` 日志**未**增加、sprite 绑定**尚未**落地 —— "accepted ≠ executed" 可判定。
- **B2**：帧执行后 `Executed`，`EXECUTED` 恰好 +1（证明没有重复订阅），session 侧绑定已落地，
  Inspector 经 `EV_ENTITY_ASSIGNED` 刷新。
- **B3**：用 Script 资产触发 native 拒绝（"asset is not a texture"），`Failed` 恰好一次，
  诊断进 `ScriptError`，状态栏 `pending edit failed (see console)`，sprite 绑定**未变**。
- **B4**：`window.Close()` 后反射核验 `PendingEditCompleted` 的 backing field **为 null**
  （无泄漏 handler），且投影回到 `Idle`。

---

## 3. Commit C — 选择通知与状态栏同步

### 3.1 问题（两个都属"模型对、控件错"）

**C1 选择通知缺失**：`MainViewModel` 有三处**直接写 `_selectedEntity` 字段**的路径
（选择意图解析、刷新后保持选择、过滤视图重建），理由都是"避免 `SelectedEntity` setter
重入刷新 Inspector"。代价是**不发 `PropertyChanged`**：模型选中了新实体，而 OneWay 绑定的
`HierarchyList.SelectedItem` 还停在旧项（或直接变成"无选中"）。既有测试断言的都是模型，
所以一直是绿的。

**C3 状态栏同步缺失**：`StatusBar.Text` 只在 View handler 末尾手工 `SyncStatusBar()`。
而 `ReloadCollectionsFromSession` 是由 **native 事件**驱动的（`EV_ENTITY_CREATED` 等），
没有 handler 收尾 ⇒ 模型 `StatusText` 已改、控件未改。

### 3.2 实现

**C1**：新增 `SetSelectedEntityCore(hit, refreshInspector, forceNotify = false)` 作为 VM 内部
改写选择的**唯一**入口。把两件事分开：刷新由调用方决定，通知由本方法统一负责。
值相等（record 值语义）时不发通知 ⇒ 一次选择变更恰好一次通知。

**顺序修正**（实测发现）：通知必须发生在**最终** `FilteredEntities` 重建**之后**。
控件绑定的是 `FilteredEntities` 里的实例，而刷新会重建这些实例；先解析意图再重建列表，
控件会收到一个已不在 `ItemsSource` 里的旧实例，OneWay 绑定随即回退到"无选中"。
因此 `ReloadCollectionsFromSession` 改为：先 `RebuildFilteredEntities(preserveSelection: 意图待解析 ? false : true)`
→ 再 `ResolvePendingSelectionIntent(strict: true)`。

`RebuildFilteredEntities` 在重新指向新实例时使用 `forceNotify: true`：值相等但**实例**已换，
不发通知控件就仍指着旧实例。

**C3**：`OnVmPropertyChanged` 增加 `StatusText` 分支 → `SyncStatusBar()`。
`SyncStatusBar` 改为幂等（`sb.Text != Vm.StatusText` 才写），因此既有的显式调用点保留无害。

### 3.3 实测

```text
[SELECTION-STATUS GATE] C1/C3: auto-select reaches the list; status bar tracks the event-driven refresh ('[Avalonia] project loaded: 11 objects, 33 assets')
[SELECTION-STATUS GATE] C2: one selection change -> one notification (1)
[SELECTION-STATUS GATE] C2: view-model driven selection -> one notification (1), same-value reselect -> none
[SELECTION-STATUS GATE] C4: close clears the list selection and the status bar
[SELECTION-STATUS GATE] ALL GREEN
selstatus exit=0
```

**修复前**该 gate 的第一条断言即失败：

```text
[SELECTION-STATUS GATE] FAIL: C1: the hierarchy must have a selected item after the auto-select
                                  (model selected #10, list has none)
```

这证明 gate 确实在测缺陷，而不是在测一个本来就不会坏的路径。

**gate 自身的两条纪律**（过程中修正）：
- **不得写共享 fixture**：最初 C4 用 `Save` 按钮把项目"变干净"，结果把
  `assets/gp01/Main.scene` 写成 12 entities，直接打红 `test_bridge` 的
  `OpenProjectRoundTrip`（期望 10）。改为 `window.Close()`（与 B4 / UiStateGate 同一 teardown 路径）。
  修复后 `Main.scene` md5 仍为 `faabc37448f44197d6966708532c1ef9`（10 entities）。
- **关闭后不得 await**：关闭最后一个窗口会让 Avalonia lifetime 开始 shutdown，
  进程可能在挂起恢复前结束 ⇒ 断言静默丢失 = 假证据。C4 段全同步。

---

## 4. 回归面（全部实测）

### 4.1 native

| target | Debug | Release |
|---|---|---|
| `test_bridge` | **44/44** | **44/44** |
| `test_hrc` | **15/15** | **15/15** |
| `test_scripting` | **27/27** | **27/27** |

I2 合计仍 **243**（本 slice **0** native 增量、0 新 ABI、0 新 op/status）。

### 4.2 managed gates

| gate | flag | Debug | Release |
|---|---|---|---|
| pump owner | `--pumpowner` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| pending edit | `--pendingedit` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| selection/status | `--selstatus` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| production path | `--prodpath` | ALL GREEN, exit=0 | — |
| HRC-2 | `--hrc2` | ALL GREEN, exit=0 | — |
| UI state | `--uistate` | ALL GREEN, exit=0 | — |
| UI state (legacy order) | `--uistatelegacy` | ALL GREEN, self-reported exit=0 | — |
| P2 frame | `--p2frame` | ALL GREEN, self-reported exit=0 | — |
| P2 UI | `--p2ui` | ALL GREEN, self-reported exit=0 | — |
| D3 managed | `--d3managed` | ALL GREEN, self-reported exit=0 | — |

A / B / C 三个 slice 各自新增的 gate 在 **Debug 与 Release 双配置**下均 ALL GREEN + exit=0。
其余 gate 本 slice 只在 Debug 复跑（未受 C 的 `MainViewModel` 改动影响的部分不重复取证）。

> `--uistatelegacy` / `--p2frame` / `--p2ui` / `--d3managed` 在本机出现**已记录**的
> process-exit linger（`docs/Evidence-Baseline.md` §"已知的既有现象"）：断言层 `ALL GREEN`
> 且自报 `exit=0`，但进程不退出，被外层 timeout 杀掉。本 slice **未**引入该现象，
> 也**未**用 retry/delay 掩盖它。

### 4.3 新增 standing runner

`tools/run_selection_status_gate.ps1`（`--selstatus`）：与 `run_pump_owner_gate.ps1` /
`run_pending_edit_gate.ps1` 同形——独立进程、单次尝试、无 retry、GREEN 判据为
exit 0 **且** stdout 含 marker。**必须用 `pwsh`**（`$psi.ArgumentList` 在 PS 5.1 不存在）。

---

## 5. 关闭陈述

| slice | 达成 |
|---|---|
| **A** | 产品 UI 的周期性 runtime pump 所有权**唯一**在 `EditorHostService`；UI 只消费 `FrameAdvanced` 呈现；`frames == pumps - failures` 恒等；频率上界证明无第二驱动 |
| **B** | `accepted` 与 `executed/failed` 是两个独立可观测状态；成功/失败各恰好一次回执；失败带 native 诊断且不改变绑定；关闭时订阅被解除 |
| **C** | 模型的选择变更**通知**控件（一次变更一次通知，同值不通知）；`StatusBar.Text == Vm.StatusText` 在 handler 路径与事件驱动路径上都成立；关闭后无残留选中项 |

三个 slice 互不越界：A 未动回执语义，B 未动 pump 所有权，C 未动 pick / resize / 运行时行为。

---

## 6. 之后（各自独立、需单独批准）

- **D3 Pick**、**D4 Resize**、**A8**：继续 Deferred。
- **build/deployment hygiene**：`OpenAL32.dll` staging 路径与 gitignored fixture 的
  committed generator —— 两者都是"clean checkout 不能自洽复现"的根因，应作为独立 slice 处理。
- **process-exit linger**：已知、已记录、本 slice 未触碰；Framework A 仍 OPEN。
