# Editor Avalonia HRC-2 阶段 A / A2 — F-2 Bootstrap Trigger Audit & Contract

> **阶段**：UI-HRC-2-A / Stage A / A2（**step 1：纯审计 + contract 冻结，本 step 零代码改动**）
> **日期**：2026-09-26
> **状态**：`CONTRACT FROZEN / IMPLEMENTATION PENDING`
> **前置**：`docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`（F-1 已 CLOSED）
> **不重新设计**：`EngineHost` native lifecycle（A6/A7 contract 不重开）、`PumpOneFrame` ownership、Renderer/Viewport
> **明确不吸收**：A3 的 `create + auto-select` 竞态、handler 缺 create result/failure row、4 条 Phase 7 WIP warning

---

## 0. 本 step 的结论摘要

1. 当前生产路径的 bootstrap 触发时机是**可描述但未被决策**的：`MainWindow` 创建**不** bootstrap，
   首个 `OpenProject` 调用**隐式** bootstrap。A1 把这个现状固化成了可执行断言，但"这是不是有意设计"没有结论。
2. 历史 `EnsureSession()` 隐式语义仍在生产路径上活着（`EditorHostService.cs:197`），
   隐藏副作用 + 无失败契约 = A2 要消除的核心风险。
3. **bootstrap 失败在生产 UI 上不可观测**：受控实验证明失败以
   `TargetInvocationException` 形态逃出 XAML handler，诊断身份（`status/stage/diagnostic`）全部丢失。
   native 侧回滚是干净的（LIFO，无半初始化句柄），缺口只在 managed 的失败传播。
4. 两个 backend 的 **CloseProject 语义不同**（EngineHost 保留 handle / legacy 销毁 session），
   这是 F2-07 / F2-09 必须写进契约、而不是靠默契维持的既有不对称。

---

## 1. 当前触发点全清单（代码事实）

| # | 触发点 | 位置 | 是否创建 native runtime | 备注 |
|---|---|---|---|---|
| T0 | `Program.Main` 构造 `EditorHostService` | `Program.cs:25` | 否 | 只选定 backend 一次 |
| T1 | `App.OnFrameworkInitializationCompleted` → `new MainWindow(_host)` | `App.axaml.cs:27` | 否 | 窗口创建不 bootstrap |
| T2 | gate 显式 `host.CreateSession()` | `Hrc1Gate.cs:16`、`Hrc2Gate.cs:62` | **是** | 仅 gate harness |
| T3 | `EditorHostService.OpenProject` 内的隐式 `CreateSession()` | `EditorHostService.cs:197` | **是** | 历史 `EnsureSession` 语义；生产路径唯一触发点 |
| T4 | `MainViewModel.OpenProject` → `_host.OpenProject` | `MainViewModel.cs:444` | 继承 T3 | **无 try/catch** |
| T5 | `MainWindow.OnOpenProject`（File 菜单 + Open 按钮） | `MainWindow.axaml.cs:155` | 继承 T3/T4 | 异常逃出 handler |
| T6 | `EditorHostService.CloseProject` | `EditorHostService.cs:208` | 否 | EngineHost 保留 handle；legacy 销毁 session |
| T7 | `MainViewModel.Dispose` → `ResetSession` → `CloseProject` | `MainViewModel.cs:474` | 否 | 窗口关闭路径 |
| T8 | `EditorHostService.Dispose` | `EditorHostService.cs:217` | 否（终态） | `Shutdown` + `Destroy`；`_disposed = true` |

Bootstrap 内部序列（native，不在本 slice 改动）：

```text
EngineHost_Create                       capi.cpp:791   （非 main thread → WrongThread）
  └─ EngineHost::Initialize             EngineHost.cpp:655
       ├─ state guard（必须 Created）                    → 否则 InvalidState
       ├─ InitializeGraphics（glfwInit / persistent hidden window / GLAD）
       ├─ UncurrentPersistentContext（creator 交棒）
       ├─ runtime thread start（5s bounded wait → 超时 = lifecycleTimedOut）
       └─ bootstrap command（runtime thread）
            ├─ MakePersistentContextCurrent
            ├─ InitializeRuntimeResources              EngineHost.cpp:489
            │    ├─ FBO（64x64）
            │    ├─ Application::InitializeHeadless
            │    └─ session attach                    → EnsureRuntimeSession:468
            └─ state = READY
  失败：TeardownRuntimeResources（LIFO）+ typed status/stage + delete host → nullptr（无半初始化）
```

---

## 2. 受控实验：bootstrap 失败的真实行为（已证）

用既有 native 注入点制造 session attach 失败（**不改任何代码**）：

```powershell
$env:EDITOR_BRIDGE_FAIL_SESSION_ATTACH="1"
AvaloniaEditor.exe --prodpath
```

观测结果：

```text
[PP-01] startup baseline + refused no-project edit (no accidental bootstrap)   ← 启动期仍无 bootstrap
... SubsystemManager: All 8 subsystems completed successfully
... SubsystemManager: --- Engine Subsystem Shutdown ---                        ← 同毫秒回滚
[HRC-PROD-PATH GATE] FAIL: Exception has been thrown by the target of an invocation.
prodpath exit=1        ← 无挂起、无崩溃弹窗阻塞、无残留进程
```

判定（证据等级：**已证**）：

| 事实 | 结论 |
|---|---|
| 启动期（含被拒绝的 no-project 编辑）不创建 runtime | F2-03 现状成立 |
| native 回滚干净：subsystem 全部 shutdown，无半初始化 handle | A6 LIFO rollback 在 bootstrap 失败路径同样成立 |
| 失败不挂起、不需要 watchdog | 负路径可控 |
| **异常以 `TargetInvocationException` 逃出 XAML handler** | 真正的 `InvalidOperationException("EngineHost_Create failed (status=6, stage=6)")` 被反射派发包裹，**诊断身份丢失** |
| 生产路径（无 gate 兜底）该异常将抵达 UI dispatcher | **待证**（需 step 2 的 unhandled-exception 探针） |

> 结论：**native 侧无需改动即可提供 A2 需要的全部负例证据**（见 §5 注入点表）。

---

## 3. F-2 决策（contract 冻结）

### F2-01 bootstrap 的唯一 owner

> **唯一 owner = `EditorHostService`（managed），且只在 UI 主线程执行。**

- native 侧维持既有约束：`EngineHost_Create` 要求已注册的 main thread（`capi.cpp:793`），
  `RegisterMainThread` 目前由 `EngineHostChannel.Create()` 首次调用时完成（`EditorHostService.cs:419`）。
- 禁止：View / ViewModel / timer / 事件回调直接触发 bootstrap；禁止第二个 bootstrap 入口。
- 后果：`EditorHostService` 必须是唯一的 native runtime 构造点——这一点在 A7-2 已是结构性保证
  （`EngineHostChannel` 内零 legacy 调用），A2 只把"构造点 = 生命周期 API"写进契约。

### F2-02 bootstrap 的唯一 trigger

> **唯一 trigger = 显式的 managed 生命周期操作（`OpenProject` 路径上的 `EnsureRuntime`）。**

- 历史上的"隐式"语义保留为**唯一一处**（`OpenProject` 内部），但必须被显式命名为一个可测操作，
  而不是藏在 `if (!HasSession) CreateSession();` 里。
- 任何新增入口（未来的"启动即 bootstrap"、工具栏按钮、命令行开关）必须复用同一个命名操作，
  不得绕过它直接调 channel。

### F2-03 MainWindow 创建是否自动 bootstrap

> **决定：`NO`。窗口创建不 bootstrap。**

- 理由：
  1. bootstrap 会分配 GL context、FBO 并拉起 runtime thread；放进窗口构造路径会让
     "启动失败"与"窗口构造失败"不可区分，且把 GL/线程工作塞进 UI 构造阶段；
  2. 会破坏 HRC-1 gate 注入链（legacy gate 假设 host 是 lazy 的，`Hrc1Gate.cs:14` 依赖这一点）；
  3. A1 已把 lazy 行为固化为可执行断言（PP-01 `!HasSession`、PP-02 `frames == 0`），行为本身是自洽的。
- 代价（显式接受）："production RuntimeTick owner 由用户动作触发" 是**被记录的属性**，不是缺陷。
  未来若要"启动即 bootstrap"，必须走 F2-02 的同一命名操作，并且**不得**顺手改动本决定之外的任何东西。

### F2-04 OpenProject 是否隐含 bootstrap

> **决定：`YES`，但收敛为单一命名操作 `EnsureRuntime()`，并带失败契约。**

- 兼容理由：HRC-1 九个 gate 与 `--prodpath` 全部依赖"只调 `OpenProject` 就能用"；
  去掉隐式 bootstrap 会让它们全部需要改（`--selftest/--gate1` 曾因 `EnsureSession` 语义丢失而挂住）。
- 收敛理由：隐式副作用必须可被独立测试、独立失败、独立重试。
- 失败契约（新增，见 F2-05）：`EnsureRuntime` 失败 → 抛出**可识别的 typed 异常**
  （`HostBootstrapException`，携带 `status/stage/diagnostic`），且 host 保持 `Starting`、无 session、无 pump。

### F2-05 bootstrap failure 如何传播到 UI

> **决定：失败必须成为可观测状态，禁止逃出 handler，禁止静默。**

- native 已提供 typed `RuntimeResult`（`status=SessionFailed(6)` / `stage=Session(6)` /
  diagnostic `"runtime session attach failed"`），managed 必须把它**保真**地搬到 UI：
  - 不允许 `TargetInvocationException` 包裹丢失诊断（现状违规，见 §2）；
  - `MainViewModel.OpenProject` 必须捕获 bootstrap 失败 → 写 Console + 状态栏 + 保持 `IsOpen == false`；
  - host 状态保持 `Starting`，**不**进入半就绪；
  - pump 不得启动。
- 目标状态：`Starting + 有诊断 + UI 可继续（用户可重试）`。

### F2-06 bootstrap 未完成时 UI 操作如何表现

> **决定：沿用 A7-2 三态 gate，语义不变。**

- `Starting` 下 runtime 依赖操作 → `HostRuntimeNotReadyException`（已在 `--hrc2` G2-04 覆盖）。
- 新增要求：**bootstrap 自身**在进行中（不是失败）时的 UI 表现也必须是显式状态，
  不得让用户在 `EnsureRuntime` 执行窗口内点击而得到未定义行为
  （当前 `EngineHost_Create` 是同步阻塞的，因此窗口内 UI 无法重入——A2 需把"同步阻塞"这一点写进契约：
  **bootstrap 在 UI 线程同步执行，期间不重入**）。
- A1 的负路径证据（无 project 点击 `+ Entity` 被拒且不 bootstrap）继续作为该契约的回归。

### F2-07 CloseProject 后是否重新 bootstrap

> **决定：`NO`（EngineHost backend）。CloseProject 不销毁 runtime，重新打开不重新 bootstrap。**

- 依据：`EngineHostChannel.CloseProject` 只做 `EngineHost_CloseProject`（`EditorHostService.cs:566`），
  handle 与 GL/FBO/Application 全部保留；A1 PP-03 已断言 pump 停止、状态回 `Starting`、host 仍可用。
- 明确区分两件事：**pump 停止 ≠ runtime 销毁**。重新 `OpenProject` 只重开 project session，不重建 GL。
- legacy backend 当前相反（`LegacySessionChannel.DestroySession` 销毁 session，`Hrc1Gate.cs:18-19` 依赖该行为），
  见 F2-09。

### F2-08 Dispose 后绝不允许隐式 resurrection

> **决定：`NO RESURRECTION`。终态即终点。**

- 现状证据：`CreateSession` 在 `Stopping`/disposed 下抛 `HostRuntimeUnavailableException`/
  `ObjectDisposedException`（`EditorHostService.cs:183-185` + `ThrowIfDisposed`），
  HRC-1 G2-09 / Hrc1Gate 已覆盖。
- A2 要求把这条**也**加到 production-path gate（`--prodpath` 的 PP-12 之后追加一次
  "dispose 后点击 Open"负例），因为生产路径此前没有这条证据。

### F2-09 legacy backend 是否共享同一 trigger contract

> **决定：共享 trigger，不共享 session retention；不对称必须被断言而不是靠默契。**

| 维度 | EngineHost（生产） | Legacy（HRC-1 gate 注入链） |
|---|---|---|
| trigger owner | `EditorHostService` 生命周期 API | 同 |
| bootstrap 时机 | 首个 `OpenProject`（懒） | 同 |
| 窗口创建是否 bootstrap | 否 | 否 |
| CloseProject 后 handle | **保留** | **销毁** |
| 重新 OpenProject | 复用 runtime | 重新 bootstrap |
| Dispose 后复活 | 禁止 | 禁止 |

- 理由：legacy 是冻结的测试注入链，其 destroy-on-close 行为被 `Hrc1Gate` 断言依赖，
  在 A2 改它等于改动 HRC-1 契约 → 违反 stop condition。
- 因此 A2 的动作是**把不对称写成断言**（两个 backend 各自一条），而不是抹平。

### F2-10 startup / reopen / failure / retry 状态矩阵

> **决定：以下矩阵为 managed 侧的冻结期望（native 行为已由 A3/A4/A6 固定，A2 不改）。**

| 事件 | `HasSession` | `State` | `IsOpen` | pump | UI 期望 | 失败时 |
|---|---|---|---|---|---|---|
| 进程启动 / 窗口 Opened | `false` | `Starting` | `false` | 无 | 编辑被显式拒绝 | — |
| 首次点击 Open（bootstrap 进行中） | `false` | `Starting` | `false` | 无（同步阻塞，不重入） | 事件处理中 | bootstrap 失败 → 见下一行 |
| 首次点击 Open（成功） | `true` | `Ready` | `true` | 启动（33ms） | 集合刷新 + Inspector 可用 | — |
| 再次点击 Open（已有 runtime） | `true` | `Ready` | `true` | 已启动 | 幂等重开 | open 失败 → 保持 `Ready`、诊断可见 |
| bootstrap / open 失败 | `false` | `Starting` | `false` | 无 | Console + 状态栏显示诊断，**不崩溃** | 可重试 |
| File>Close | `true`（EngineHost） | `Starting` | `false` | 停止 | 集合/过滤视图全清 | — |
| 关闭后重新 Open | `true` | `Ready` | `true` | 重启 | host 可复用 | — |
| 窗口关闭 | `true` | `Starting` | `false` | 停止 | `Vm.Dispose` → `ResetSession` | — |
| `Dispose` 之后 | `false` | `Stopping` | `false` | 无 | 任何操作显式拒绝 | 幂等 |

---

## 4. 与 stop conditions / 不做清单的边界

- A2 **不**重开 A6/A7 native contract：上表所有 native 侧行为都已由 A3/A4/A6 + `test_hrc` 固定。
- A2 **不**吸收 A3 项：`create + auto-select` 竞态、handler 缺 create result/failure row 保持原状，
  它们的修复会改 `MainWindow`/`MainViewModel` 的事件时序，与 A2 的生命周期决策正交。
- A2 **不**清理 4 条 Phase 7 WIP warning。
- A2 **不**做 SaveProject/ScriptSave 落盘闭环、Viewport/Gizmo、Linux receipt。
- 任何需要"顺手改 native"的冲动 → 立即停止并回到本审计。

---

## 5. Step 2（实现 + 证据）计划

| # | 实施项 | 关闭 | 说明 |
|---|---|---|---|
| S1 | `EditorHostService.EnsureRuntime()`：把 `OpenProject` 里的隐式 `CreateSession()` 收敛为命名操作，并加 bootstrap 失败回滚保证 | F2-02 / F2-04 | managed-only，不动 native |
| S2 | `HostBootstrapException`（携带 `status/stage/diagnostic`）+ `MainViewModel.OpenProject` 捕获并落 Console/状态栏 | F2-05 | 消灭 `TargetInvocationException` 诊断丢失 |
| S3 | production-path 负例：dispose 后点击 Open、bootstrap 失败注入、reopen 幂等 | F2-06 / F2-07 / F2-08 | 见下表 |
| S4 | legacy / EngineHost 不对称各一条断言 | F2-09 | 不改 legacy 行为 |
| S5 | 全量回归（native Debug+Release + 10 个 managed gate + `--prodpath` + integrity gate） | 全部 | 与 A1 同规格 |

A2 gate 结构（`--boot` 之类的新 flag，或作为 `--prodpath` 的第二段）：

```text
A2-F2
├── trigger contract        F2-01/02/03/04：懒启动 + 单一命名触发 + 幂等重开
├── ownership contract      F2-01：UI 线程唯一 owner；无第二 bootstrap 入口
├── startup negative        F2-03/06：启动期编辑被拒且不 bootstrap；bootstrap 期间不重入
├── failure propagation     F2-05：注入失败 → typed 诊断可见、无崩溃、可重试成功
├── reopen negative         F2-07：CloseProject 不销毁 runtime；重开不重建 GL
├── dispose/no-resurrection F2-08：终态后所有入口显式拒绝
├── legacy compatibility    F2-09：两 backend 的 retain/destroy 差异各有断言
└── A1 regression           `--prodpath` 全量重跑
```

失败注入点（**全部已存在，step 2 不需要动 native**）：

| env | 影响的 bootstrap 阶段 | typed 期望 |
|---|---|---|
| `EDITOR_BRIDGE_FAIL_APPLICATION_INIT=1` | Application init | `InitFailed` / `EngineSubsystem` |
| `EDITOR_BRIDGE_FAIL_FBO=1` | FBO | `GLFailed` / `FBO` |
| `EDITOR_BRIDGE_FAIL_SESSION_ATTACH=1` | session attach | `SessionFailed` / `Session` |
| `EDITOR_BRIDGE_FAIL_FRAME` / `_RUNTIME_COMMAND` | pump | `TickFault` |
| `EDITOR_BRIDGE_FAIL_RUNTIME_TICK=1` | playing tick | `TickFault` + emergency teardown |
| `EDITOR_BRIDGE_FAIL_PLAY_BIND=1` | Play OnCreate | `SessionFailed` / `Play` |

注入执行方式：bootstrap 发生在进程内首个 `OpenProject`，因此 gate 进程在点击 Open **之前**
设置环境变量即可（`Environment.SetEnvironmentVariable`），无需重启进程、无需新 native 导出。

---

## 6. 需要 ratify 的 3 条决策

其余条目证据充分、可直接冻结。以下三条是真正的判断题，step 2 开工前请确认：

1. **F2-03 `NO` 自动 bootstrap** —— 保持"用户动作触发"。若要"启动即 bootstrap"，
   它应作为独立 slice（且必须复用 F2-02 的命名操作），不应搭在 A2 里。
2. **F2-04 保留隐式 bootstrap 但收敛为 `EnsureRuntime()`** —— 不取消隐式语义（否则 10 个 gate 全要改），
   只是把它变成有名有姓、有失败契约的操作。
3. **F2-09 接受 backend 不对称并断言之** —— 不在 A2 抹平 legacy 的 destroy-on-close。

---

## 7. 当前判定

```text
A2 step 1 (audit + contract):   COMPLETE（本文件）
A2 step 2 (实现 + 负例证据):     PENDING（需 §6 三条 ratify）
A2 overall:                     IN PROGRESS
F-1:                            CLOSED（A1）
F-2:                            CONTRACT FROZEN / 待实现
HRC-G2P-01~08:                  NOT GREEN（G2P-08 属 A8 DEFERRED）
native 改动:                    0（本 step 与 step 2 均不需要）
Evidence-Baseline 计数:         不变（本 step 不新增可执行证据）
```
