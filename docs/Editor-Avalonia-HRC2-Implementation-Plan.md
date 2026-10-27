# Editor Avalonia HRC-2-A — Runtime Host Implementation Plan

> **阶段**：UI-HRC-2-A（Implementation Plan）  
> **日期**：2026-09-24  
> **前置文档**：`docs/Editor-Avalonia-HRC2-Preflight.md`  
> **计划状态**：A4/A6/A7-0/A7-1/A7-2 CLOSED；Stage A/A1 CLOSED（F-1）；Stage A/A2 CLOSED（F-2，7-7 GREEN）；**Stage A/A3 CLOSED**（UI-state honesty）；NHI-1 CLOSED / Closure B；**HRC-2 CLOSED**（见 `docs/Editor-Avalonia-HRC2-Closure.md`）；A8 `DEFERRED / NON-BLOCKING`；**HRC-2-A READINESS `PASS`**（2026-09-28：F-1/F-2 两个 blocking finding 均已 CLOSED）  
> **API 冻结**：`ENGINE_HOST_ABI_VERSION = (1<<16)|1`（v1.1），single-source 于 `capi.h`  
> **实现状态**：A0–A6 native evidence 已建立，Stage A/A1~A3 evidence 已建立；**G2P-01~07 GREEN
> （Windows hidden-GLFW 范围），G2P-08 属 A8 DEFERRED**；HRC-2 已按 Windows 范围收口
> （`docs/Editor-Avalonia-HRC2-Closure.md`），HRC-2-A DoD 第 1/2/7 项仍为 A8 门控 OPEN  
> **提交策略**：不创建 commit/tag；保留现有 Phase 7/HRC-1 dirty baseline
> **A1 闭包**：`docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`

---

## 1. 已冻结决策

| 决策 | HRC-2-A 选择 |
|---|---|
| Headless GL | Hidden GLFW |
| Runtime owner | 一个 dedicated runtime thread |
| Frame cadence | monotonic variable dt |
| Tick clamp | effective `0.1s`，raw/effective 都可观测 |
| FBO owner | native `EngineHost`；UI `ViewportPanel` 不拥有 HRC-2 FBO |
| Reset | 显式 H1 Dispose → H2 Bootstrap |
| ABI result | 统一 typed `RuntimeResult` |
| CI | Windows Hidden GLFW；Linux Hidden GLFW + Xvfb |
| GL interop | 不做 NativeControlHost、GPU texture sharing 或 GL interop |

---

## 2. 不可违反的边界

1. 保留现有 `EditorSession_*` C ABI、v1.1 版本检查和 legacy/test 行为。
2. 新增独立、版本化的 `EngineHost_*` C ABI；不原地改变 `RuntimeTick`/`Stop` 的返回类型。
3. Avalonia production Host 不再直接持有或调用 `EditorSessionHandle`；新 Host 只持有 `EngineHostHandle`。
4. `EditorHostService` 仍是唯一 managed Host；Gate 不得创建第二个 production Host。
5. GLFW init/window/context create/destroy 只在 creation/UI main thread；Engine、GL resource、FBO、Session、tick、render 只在 runtime thread 执行。
6. UI 只提交 command、等待 result、消费 snapshot；不直接执行 GL/Session 操作。
7. `Application::Run()` 保持 legacy 行为，但内部改为调用可拆分的 `Initialize/PumpOneFrame/Shutdown`。
8. 生命周期路径只使用大写 `engine/src/Core` canonical source；不清理或重命名 `engine/src/core` 重复文件。
9. 不修改 HRC-1 注入链、Phase 7 readback ABI 名称和 `[width][height][RGBA8]` 布局。
10. 不在本阶段实现 Picking、Gizmo、ECS、ImGui migration、Renderer/RenderGraph/RHI 重构或 60 FPS SLA。

---

## 3. 目标 ownership graph

```text
EditorHostService (managed, one instance)
  └─ EngineHostHandle (opaque)
      └─ native EngineHost
          ├─ one std::jthread
          ├─ command queue / completion queue
          ├─ owner thread id
          ├─ one OpenGLGraphicsFactory (creation/main thread)
          ├─ one persistent hidden GlfwWindow/context
          │   └─ runtime thread only make/un-current
          ├─ one Engine::Application
          ├─ one Host-owned OpenGLFramebuffer
          ├─ CPU readback snapshot
          └─ one active runtime-bound EditorSession
```

```text
Avalonia UI thread
  → EngineHost command
  → runtime thread result/event
  → UI Dispatcher.Post
  → MainViewModel
```

runtime thread 不允许同步等待 UI；callback 只复制数据并投递，不在 runtime callback 中执行同步 native query。

---

## 4. Proposed ABI

### 4.1 Versioning

保留：

```text
EditorBridge_GetApiVersion() == v1.1
```

新增：

```text
EngineHost_GetApiVersion()
```

新 API 使用独立 minor version；旧 consumer 不因新导出而改变行为。

### 4.2 RuntimeResult

新 API 使用 caller-provided result buffer，不按值返回大型 struct：

```c
int32_t EngineHost_Operation(..., RuntimeResult* result);
```

建议固定宽度字段：

```text
struct_size
abi_version
status
stage
operation_id
frame_id
generation
owner_thread_id
state_before
state_after
error_code
diagnostic_length
diagnostic_truncated
raw_dt_seconds
effective_dt_seconds
```

所有字段使用 `int32_t`、`uint32_t`、`uint64_t`、`float`；C# 使用显式 sequential layout，并在 native 侧做 `sizeof/offsetof` 检查。

### 4.3 Status

```text
Success
SkippedNotPlaying
InvalidState
WrongThread
InitFailed
GLFailed
SessionFailed
TickFault
ShutdownComplete
ShutdownFailed
```

### 4.4 Stage

```text
Bootstrap
Platform
GLContext
GLAD
EngineSubsystem
FBO
Session
OpenProject
Play
Tick
Shutdown
```

### 4.5 New exports

```text
EngineHost_GetApiVersion
EngineHost_Create
EngineHost_OpenProject
EngineHost_Play
EngineHost_Reload
EngineHost_Stop
EngineHost_PumpOneFrame
EngineHost_CloseProject
EngineHost_Shutdown
EngineHost_Destroy
EngineHost_GetLastFrameResult
EngineHost_SetEventCallback
EngineHost_GetLastDiagnostic
```

`EngineHost_Create` 和 `EngineHost_Shutdown` 都必须返回 typed result；handle 创建失败时不得返回半初始化 Host。

---

## 5. Runtime command model

### 5.1 Bootstrap

```text
Created
  → register/verify creation main thread
  → factory/glfwInit
  → persistent hidden context + GLAD
  → creator un-current
  → runtime thread starts
  → runtime make-current
  → Application initialize
  → Host-owned FBO
  → runtime-bound Session
  → Ready
```

### 5.2 Single frame

```text
accept command
  → make context current
  → drain commands
  → poll hidden window
  → calculate monotonic raw dt
  → Engine update
  → Session RuntimeTick at most once
  → FBO render/readback snapshot
  → publish typed result/event
  → un-current at frame boundary
```

### 5.3 CloseProject

```text
reject new commands
  → stop/join project work
  → Stop once
  → detach callback
  → OnDestroy exactly once
  → GameplayAPI reset
  → destroy Session/project data
  → retain Engine/context/FBO/Host
  → Ready
```

### 5.4 Terminal shutdown

```text
reject new commands
  → runtime stop/join
  → detach callback
  → destroy Session
  → destroy FBO/readback/GPU resources
  → Application shutdown
  → clear globals
  → runtime un-current context
  → join complete
  → creator destroys hidden carrier
  → glfwTerminate exactly once
  → ShutdownComplete
  → Disposed
```

---

## 6. Atomic implementation slices

### A0 — Baseline and source lock

**修改范围**：

- 记录当前 HEAD、tracked diff fingerprint、untracked 文件；
- 固定 `engine/src/Core/Application.cpp` 为 canonical path；
- 不清理、不重置、不提交既有 dirty changes。

**Acceptance**：

- HRC-1 build/gates 仍绿；
- `git status` 与 A0 开始时一致，除明确新增 HRC-2 文件；
- 建立 A0 evidence 记录。

### A1 — ABI skeleton and typed result

**修改范围**：

- `bridge/include/editor_bridge/capi.h` 追加 `EngineHost_*` 和 `RuntimeResult`；
- `bridge/src/capi.cpp` 追加薄 wrapper、handle/result validation、exception guard；
- `editor_avalonia/Native/EditorBridgeApi.cs` 镜像 layout 和 imports；
- `bridge/CMakeLists.txt` 加入新 source（若 A2 创建）。

**Acceptance**：

- 旧 ABI 版本和符号不变；
- `sizeof/offsetof` 与 C# layout 一致；
- null handle、null result、重复 Destroy 不崩；
- C++ exception 不穿 C ABI；
- 尚不创建真实 GL/runtime。

### A2 — EngineHost thread and command queue

**修改范围**：

- 新增 `bridge/src/EngineHost.h/.cpp`；
- 一个 `std::jthread`；
- command/completion queue；
- operation/frame/generation/thread metadata；
- manual pump mode；
- stop/join barrier；
- `WrongThread`、`InvalidState`、shutdown acknowledgement。

**Acceptance**：

- test-only pump 可确定推进指定帧数；
- 非 owner thread 的直接 native operation 被拒绝或被明确排队；
- Runtime/queue 无 detached worker；
- 重复 Stop/Shutdown 幂等；
- 无 callback late-delivery。

### A3 — Persistent Hidden GLFW context

**修改范围**：

- `engine/include/Engine/OpenGL/OpenGLGraphicsFactory.h`；
- `engine/src/OpenGL/OpenGLGraphicsFactory.cpp`；
- `GlfwLifetime` shared owner；
- `GlfwWindow` current/un-current 与 fail-fast owner contract；
- persistent mode 下拒绝 legacy `CreateWindow`/RHI coexistence；
- creator/main thread 创建与销毁 carrier，runtime thread 只 make/un-current。

**Acceptance**：

- `glfwInit` 成功后 context carrier 持续存在；
- GLAD load 成功后才报告 Ready；
- creation/main thread 与 runtime thread 可观测；
- make-current/un-current 计数正确；
- `glfwTerminate` 只有 EngineHost owner 执行一次；
- persistent mode 与 legacy mode 不可混用；
- context teardown 前所有 GL resource 已释放。

### A4 — Runtime Seam Proof / Readiness Gate

A4 不再定义为 Runtime 完成；它只把 A3 之后的 native seam 变成可执行证据。目标是一个真实但最小的闭环：

```text
EngineHost
  → headless Application
  → persistent hidden GL
  → Host-owned fixed-size FBO
  → runtime-bound EditorSession
  → one frame pump
  → N frames
  → stop/close
  → FBO/Application teardown
  → context/GLFW teardown
  → same-process H1 → H2
```

**修改范围**：

- `engine/include/Engine/Application.h`；
- `engine/src/Core/Application.cpp`；
- `engine/include/Engine/OpenGL/OpenGLFramebuffer.h`；
- `engine/src/OpenGL/OpenGLFramebuffer.cpp`；
- `bridge/src/EngineHost.h/.cpp`；
- runtime-bound `EditorSession` binding；
- `tests/test_bridge` A4 proof tests；
- `tests/CMakeLists.txt` / `test_hrc`；
- headless CI configuration。

**Acceptance**：

- G2P-01～08 每项都有可执行 evidence，不以 A3 audit 或 skip 代替；
- `Application::InitializeHeadless/PumpOneFrame/Shutdown` 不创建 visible window、不启动第二个 loop；
- Host-owned FBO 有固定 identity、complete/clear/readback probe；
- Session 在 runtime thread attach，RuntimeTick 每 frame 最多一次；
- typed failure result 与 LIFO rollback 可查询；
- teardown 顺序为 Session → FBO/GL resource → Application → context → GLFW；
- H1 Dispose → H2 Bootstrap 在同一进程重复三次，global/resource counters 回到 baseline；
- Windows Hidden GLFW 与 Linux Xvfb/software GL 均实际运行，0 skipped；
- legacy `Application::Run()`、Viewport、Picking、Gizmo、ECS、ImGui、Asset Pipeline 不纳入 A4。

A4 完成后才评估 HRC-2-A readiness；A4 本身不等同于 Runtime Bootstrap Integration。

当前 native A4 evidence 已覆盖：typed context/tick/reload failure、Faulted emergency teardown、color/GL-error readback、callback detach、OpenProject failure transaction、H1→H2 三周期和 global snapshot。Windows 本地 `test_bridge`(41) / `test_hrc`(7) 及 HRC-1 bounded gates 已通过；Linux CI 仍只有配置没有实际 receipt。managed production Host 尚未切换到 `EngineHostHandle`，Viewport/global singleton 完整边界属于 A6/A7，因此 G2P-01~08 仍不能标为全 GREEN。

### A5 — Snapshot/Readback hardening

A4 已建立固定尺寸 Host-owned FBO 与 clear/readback proof；A5 只扩展 resize、snapshot identity 和可视诊断，不重新定义 FBO owner。

**修改范围**：

- A4 的 FBO owner/probe；
- `OpenGLFramebuffer` lifecycle/affinity；
- CPU snapshot contract；
- legacy `ViewportPanel` 保持不变。

**Acceptance**：

- runtime thread 创建/销毁 FBO；
- fixed initial size、resize、readback snapshot 有明确 identity；
- 所有 delete 在 current context 有效时执行；
- CPU snapshot 不跨线程暴露可变 GL 指针；
- HRC-2 path 不使用 `ViewportPanel` 的 FBO。

### A6 — Runtime Session transaction hardening

A4 已接入最小 runtime-bound Session；A6 扩展真实项目/脚本 transaction、错误映射和 teardown observability，不重新定义 Session owner。

**修改范围**：

- A4 的 runtime-bound `EditorSession`；
- `GameplayAPI` rollback；
- ScriptInstance failure mapping；
- Stop/CloseProject/Shutdown exactly-once trace。

**Acceptance**：

- production Session 不创建 GL factory、不查隐式 `Application::GetInstance()`；
- `OpenProject` 所有失败路径清空 raw scene；
- Play/Reload `OnCreate` failure 不返回 success；
- RuntimeTick 每 frame 最多一次；
- Stop/Shutdown 后无残留 callback/script/scene state；
- legacy test fixture 继续通过。

当前 A6 native proof 已覆盖：production Session 不再读取 `Application::GetInstance()`，OpenProject 临时 transaction 保留 active session，Reload/RuntimeTick failure 进入停止/typed fault，Session callback 使用互斥快照并在 Close/Shutdown 前 detach，`ScriptInstance` 统计 OnCreate/OnDestroy 且重复 Shutdown 不重复 OnDestroy，`GameplayAPI`/`ScriptAPI`/allocator 状态可通过 `EditorSessionRuntimeStats` 观测。`test_bridge`(41)、`test_hrc`(9) 与 Scripting lifecycle 回归已通过；A7/A8 仍未完成。

## A7-0 Managed Host Switch Contract Freeze

> **状态**：`FROZEN BEFORE IMPLEMENTATION`  
> **目的**：只把 A6 已验证的 native runtime seam 接到 Avalonia production Host；不重新打开 A6 native contract，不进入 Viewport Reality。

### Composition and backend mode

- `Program.Main` 继续只创建一个 `EditorHostService`，并通过 `App → MainWindow → MainViewModel` 传递同一引用。
- Host backend 在进程启动时只选择一次：`EngineHost`（production/A7 gate）或 `LegacySession`（HRC-1/既有 gate 注入链）。两者不并存、不互斥回退、不在同一进程同时持有 native runtime。
- `EngineHost` 模式由 service 持有唯一 `EngineHostHandle`；native `EngineHost` 继续拥有 Engine、GL、Session、scheduler，managed 不重建 native runtime。
- `LegacySession` 模式仅为 HRC-1 回归保留；A7 production gate 不得创建 legacy `EditorSession`。

### Frozen contracts

| Contract | A7 invariant | Evidence boundary |
|---|---|---|
| A7-C01 Single Host | Program 是唯一 Host factory；App/MainWindow/ViewModel 不得 new Host | managed composition + `Hrc1Gate` shared-Host check |
| A7-C02 Native ownership | managed 只持有 `EngineHostHandle`；不直接持有/创建 Engine、GL、Session | native `EngineHost` proof + managed handle gate |
| A7-C03 Tick owner | `EngineHost` 模式只有 service-owned single pump；UI timer 不得调用 runtime tick；legacy viewport timer 不计为 runtime tick | managed A7 gate + native `test_hrc` |
| A7-C04 Thread affinity | UI thread 只发命令/收 marshalled event；native runtime thread 独占 Engine/GL/Session pump | managed dispatcher assertion + native owner probes |
| A7-C05 Failure propagation | native `RuntimeResult` failure → managed observable error/state → stop pump → rollback/teardown；禁止静默继续 tick | managed failure gate + typed native result |
| A7-C06 CloseProject semantics | `CloseProject` 只关闭 project/runtime session；terminal `Dispose` 才 shutdown/destroy native Host；两者状态独立 | managed repeated lifecycle gate |

### Scope lock

- A7 只允许触及 `Program.cs`、`App.axaml.cs`、`EditorHostService.cs`、`MainWindow.axaml.cs`、`MainViewModel.cs` 与必要 managed/native ABI glue。
- 需要修改 Renderer、ViewportPanel、Gizmo、Picking、ECS、Asset Pipeline 时立即停止并重新划分 scope。
- A7 gate 只增加 managed lifecycle/runtime evidence；不新增 native test infrastructure，除非现有 `test_hrc`/`test_bridge` 无法证明既有 contract。
- A7 不宣称 UI Runtime 完成；Viewport/Gizmo/Picking 继续属于独立后续阶段。

### Permanent scope rules

1. A7 不改变 A6 已闭合的 native lifecycle contract。
2. A8 不等于 Linux Editor port。
3. 没有 Viewport evidence，就不能宣称 UI Runtime 完成。
4. 没有新的功能需求，不修改 Engine renderer。
5. 不修 `test_ecs` 作为 HRC-2 的一部分。
6. 不把 Phase 7 dirty changes 纳入 HRC-2。
7. 任何新增文件/接口必须回答：它关闭哪个 HRC-G2 gate？回答不了就不进入本阶段。
8. 任何新增行为必须同时提供 negative/failure evidence。
9. Cross-platform receipt 不得作为 Core Host Runtime correctness 的前置条件；平台能力与架构正确性分层验证。
10. 没有真实证据环境时，冻结该验证项（`DEFERRED`），不得制造或假设环境换取 PASS。

### A7-G2A evidence map

| Gate | Required managed evidence |
|---|---|
| HRC-G2A-01 | single service/handle; no second Host |
| HRC-G2A-02 | one active native owner; no legacy fallback in EngineHost mode |
| HRC-G2A-03 | one service-owned pump; frame/tick counters advance once |
| HRC-G2A-04 | UI-thread commands only; callback marshalled to UI |
| HRC-G2A-05 | failure injection becomes observable error and stops pump |
| HRC-G2A-06 | CloseProject leaves Host reusable |
| HRC-G2A-07 | terminal Dispose is exactly-once/idempotent |
| HRC-G2A-08 | cold start/reload/close/dispose repeated without residue |


### A7-G0 — 32-call production routing audit

> **状态**：`FROZEN / A7-0 不修改实现代码`  
> **Gate 要求**：`32/32 classified`，`0 × LEGACY BUT WORKS`。

#### 冻结的 runtime state machine

```text
Starting            → runtime 依赖操作返回 RuntimeNotReady
Ready               → runtime 依赖操作经唯一 EngineHost 执行
Stopping / Disposed → runtime 依赖操作返回 RuntimeUnavailable
```

禁止把不可用状态表达为 `false` / `null` / 空串 / 静默 no-op / 隐式 fallback 到 legacy。

#### Production routing 矩阵

`default prod` = 无 CLI 参数的 `AvaloniaEditor.exe` 是否会触发该调用。

| # | Call | managed call site | runtime dep | default prod | classification |
|---:|---|---|---|:---:|---|
| 1 | `CreateEntity` | `MainViewModel.cs:500`, `MainWindow.axaml.cs:152` | SESSION | 是 | MIGRATE |
| 2 | `DeleteEntity` | `MainViewModel.cs:533` | SESSION | 是 | MIGRATE |
| 3 | `RenameEntity` | `MainViewModel.cs:519` | SESSION | 是 | MIGRATE |
| 4 | `SetEntityPosition` | `MainViewModel.cs:720` | SESSION | 是 | MIGRATE |
| 5 | `GetEntityPosition` | `MainViewModel.cs:862` | SESSION | 是 | MIGRATE |
| 6 | `GetEntityName` | `MainViewModel.cs:507,801`, `MainWindow.axaml.cs:153` | SESSION | 是 | MIGRATE |
| 7 | `GetEntityCount` | `MainViewModel.cs:798` | SESSION | 是 | MIGRATE |
| 8 | `GetAssetCount` | `MainViewModel.cs:798` | SESSION | 是 | MIGRATE |
| 9 | `GetAssetPath` | `MainViewModel.cs:805` | SESSION | 是 | MIGRATE |
| 10 | `GetAssetType` | `MainViewModel.cs:806` | SESSION | 是 | MIGRATE |
| 11 | `GetAssetGuid` | `MainViewModel.cs:807` | SESSION | 是 | MIGRATE |
| 12 | `ImportAsset` | `MainViewModel.cs:564` | SESSION | 是 | MIGRATE |
| 13 | `RenameAsset` | `MainViewModel.cs:583` | SESSION | 是 | MIGRATE |
| 14 | `GetEntitySprite` | `MainViewModel.cs:864` | SESSION | 是 | MIGRATE |
| 15 | `GetEntityScript` | `MainViewModel.cs:866` | SESSION | 是 | MIGRATE |
| 16 | `AssignScript` | `MainViewModel.cs:598` | SESSION | 是 | MIGRATE |
| 17 | `ScriptRead` | `MainViewModel.cs:609` | NONE (FS) | 是 | MIGRATE |
| 18 | `ScriptSave` | `MainViewModel.cs:632` | NONE (FS) | 是 | MIGRATE |
| 19 | `SaveProject` | `MainViewModel.cs:464`, `MainWindow.axaml.cs:145` | SESSION | 是 | MIGRATE |
| 20 | `AddComponent` | `MainViewModel.cs:208` | SESSION | 是 | MIGRATE |
| 21 | `RemoveComponent` | `MainViewModel.cs:222` | SESSION | 是 | MIGRATE |
| 22 | `HasComponent` | `MainViewModel.cs:875` | SESSION | 是 | MIGRATE |
| 23 | `GetComponentProperty` | `MainViewModel.cs:881,885,887,889` | SESSION | 是 | MIGRATE |
| 24 | `SetComponentProperty` | `MainViewModel.cs:239-242` | SESSION | 是 | MIGRATE |
| 25 | `IsDirty` | `MainViewModel.cs:895` | NONE | 是 | MIGRATE |
| 26 | `GetLastError` | `MainViewModel.cs` ×16 | NONE | 是 | MIGRATE → `EngineHost_GetLastDiagnostic` |
| 27 | `GetWarnings` | `MainViewModel.cs:449` | SESSION | 是 | HOST SEAM ADDED |
| 28 | `GetRuntimeError` | `MainViewModel.cs:666,668,689,690` | RUNTIME | 是 | MIGRATE → `EngineHost_GetLastDiagnostic` |
| 29 | `AssignSprite` | `MainViewModel.cs:548` | **GL** | 是 | HOST SEAM ADDED — 受 GL-current 窗口约束，见下 |
| 30 | `IsPlaying` | 无调用点 | RUNTIME | 否 | RETIRE（删除生产引用，gate 改用 `EditorSessionRuntimeStats.Playing`） |
| 31 | `RuntimePersistInt` | 仅 `Phase3Gate.cs:712,721,728,777,812,861` | RUNTIME | 否 | DEFERRED（gate-only；A7 不补 Host seam） |
| 32 | `EditorBridge_GetVersion` | `EditorHostService.cs:28` | NONE | 是 | MIGRATE → `EngineHost_GetApiVersion` |

结论：`28 MIGRATE / 3 HOST SEAM ADDED / 1 RETIRE`，`0 LEGACY BUT WORKS`。

#### A7-0 关键发现：GL-current 窗口约束

`EngineHost::PumpOneFrame` 仅在帧内 `MakeRuntimeContextCurrent()` … `UncurrentPersistentContext()` 之间持有 GL context（`bridge/src/EngineHost.cpp:1087-1096`）。`Submit()` 执行的编辑命令落在该窗口之外。

因此 `AssignSprite`（`EditorSession.cpp:311-348`，经 `SpriteComponent` + `m_TexMgr` 触发 GL 上传）**不得**实现为普通 `Submit` 命令，否则会在无 current context 的 runtime 线程上发起 GL 调用。

冻结处理方式：EngineHost 内新增 **pending-edit queue**，在 pump 帧内、`MakeRuntimeContextCurrent()` 之后、Engine update 之前排空；command 完成前 managed 调用方阻塞等待该 op 的 `RuntimeResult`。这保持 A4 已验证的 "frame boundary `graphicsCurrent==0`" 不变量不变，且不改动 A6 任何 lifecycle contract。

#### 允许的最终状态

```text
MIGRATED | HOST SEAM ADDED | DEFERRED (explicitly justified) | RETIRED
```

A7-G1 通过条件：production call → Host → EngineHost → native/session，路径上不存在 legacy direct C ABI。

### A7-2 结果：Managed Host Switch

> **状态**：`COMPLETE / G2-01~G2-09 PASS（G2-10 见回归）`  
> **范围**：`Program.cs`、`MainWindow.axaml.cs`、`ViewModels/MainViewModel.cs`、`Native/EditorBridgeApi.cs`、`Services/EditorHostService.cs`、新增 `Services/Hrc2Gate.cs`。未改 A6 lifecycle、`PumpOneFrame` ownership、Renderer/Viewport/Gizmo/ECS。

#### 结构决定

- `EditorHostService` 启动时选定一次 backend（`EngineHost` / `LegacySession`），运行期不可切换、无回退。生产默认 `EngineHost`；HRC-1 gate flags 保留 legacy 注入链。
- 编辑面下沉到 `IEditorOps`，两个实现互不可见：`EngineHostChannel` 内**不存在任何 legacy `EditorSession_*` 调用**，因此 G2-03 是结构性保证而非约定。
- 三态 `Starting / Ready / Stopping`：`Starting` 抛 `HostRuntimeNotReadyException`，`Stopping`/disposed 抛 `HostRuntimeUnavailableException`，绝不返回 `false`/`null`/空串或回退。
- 唯一 pump 由 `EngineHostChannel` 的 `DispatcherTimer` 持有；legacy viewport timer 仅在 `LegacySession` backend 启动，EngineHost 模式不存在第二条 tick 路径。
- pending edit 只在 pump 之后用非阻塞 `EngineHost_PollPendingEdit` 取回，UI 线程不等待 completion。
- `GetLastError` / `GetRuntimeError` 统一读 `EngineHost_GetLastDiagnostic`。
- generation guard 完全在 managed：capture → `Dispatcher.UIThread.Post` → UI 侧比对，未依赖 native `m_Generation`。

#### A7-2 期间发现并修复的两个真实缺陷

1. **teardown 事件导致 UI 线程未处理异常**：`CloseProject` / `Dispose` 期间 native 仍会 emit，事件到达 UI 时 runtime 已 `Starting`，ViewModel 随即调用被三态 gate 正确拒绝的 `GetCounts()` → 崩溃。修复：channel 增加 `_suppressEvents`（`BeginTeardown` 置位、`BeginRuntime` 清零）+ ViewModel 对需要 project 的事件显式判 `IsOpen` 并记录，不静默 no-op。
2. **`_suppressEvents` 未在重新打开 project 时清零**（首版修复引入），导致后续所有事件被吞；由 G2-05 断言捕获。

#### Gate evidence（`--hrc2`，`Services/Hrc2Gate.cs`）

| Gate | 证据 |
|---|---|
| G2-01 | `ReferenceEquals(host, vm.Host)`；backend == EngineHost；无 flag → EngineHost；9 个 HRC-1 flag → LegacySession；lazy → `CreateSession` → 持有唯一 handle |
| G2-02 | 27 个 op 经 `EngineHost_Dispatch` / `EngineHost_Fetch` 逐一跑通（counts、name、guid、path、type、sprite、script、ScriptRead、position 往返、component 增删改查、IsDirty、warnings） |
| G2-03 | `SupportsViewport == false`；`ViewportTick` / `ViewportPick` / `SetGizmoMode` 抛 `HostCapabilityNotAvailableException`；`GetRuntimePersistInt` 抛 `NotSupportedException`（DEFERRED 强制） |
| G2-04 | 初始 `Starting`：`GetCounts` / `Stop` / `AddComponent` 全部 `HostRuntimeNotReadyException`；OpenProject → `Ready`；CloseProject → 回到 `Starting` 且 `!IsDisposed` |
| G2-05 | 端到端 27 op 全部成功；native 事件经 marshal 后 `OffUiThreadEventCount == 0` 且 `ConsumedEventCount` 严格增长 |
| G2-06 | CloseProject 前捕获 generation 的 late event 被丢弃（`DroppedLateEventCount` 严格增长）；重新打开后当前 generation 的事件被投递且不计入 drop |
| G2-07 | `AssignSprite` 返回 accepted 时 `GetEntitySprite` 仍为空且无 completion；`RuntimeTick` 后 completed/executed、sprite 已绑定、`LastPendingEditExecutedWithContextCurrent == true`；越界 edit 报 failed + 非空 diagnostic，`failures == 1` |
| G2-08 | 成功操作清空 diagnostic；`DeleteEntity(9999)` 失败后 `GetLastError` 非空且 `GetRuntimeError == GetLastError` |
| G2-09 | CloseProject 后 host 可复用并可重新 OpenProject 到 `Ready`；Dispose → `Stopping` + generation 增长；Dispose 幂等；disposed host 拒绝 `CreateSession` |
| G2-10 | 见下方回归（native A7-1 套件与 HRC-1 gates 全部不变） |

#### 回归结果

```text
--hrc2        exit 0  ALL GREEN
managed gates --hrc1 --selftest --gate1 --gate2 --gate3a --gate3b --gate3c --gate3d --gate3g 全 exit 0
dotnet build  0 errors
test_hrc      Debug 15/15   Release 15/15
test_bridge   Debug 41/41   Release 41/41
test_scripting Debug 27/27  Release 27/27
ctest Debug   10/10 in-scope（test_ecs 仍为既有 Not Run，范围外）
pure-C smoke  通过
fixtures      Main.scene 10 entities / 无 probe 实体 / mtime 未变；game.lua 未再改动
residual      targeted process = 0；git diff --check exit 0
```

#### 已知遗留

- `EditorHostService.cs` 现有 4 条编译告警（`MainViewModel._viewportCamX/Y/Z` 三个未被读取的字段 + `Phase2Gate.cs:90` CS8602）。这三条是 dirty baseline 的 Phase 7 viewport WIP 代码，A7-2 之前因增量编译未重新触发；属 Viewport 范围，A7-2 不处理。
- A7-2 gate 不调用 `SaveProject` / `ScriptSave`，因此 managed 侧这两个调用只有 routing 覆盖，没有落盘证据。

---

## HRC-2-A Readiness Audit

> **性质**：纯审计，不重新设计 lifecycle，不新增行为。  
> **日期**：2026-09-26  
> **结论**：`CONDITIONAL PASS` —— Core Host Runtime Contract 已闭合；HRC-2-A 实现前必须先关闭 **F-1（生产 UI 路径无证据）** 与 **F-2（bootstrap 由用户动作触发）**。

> **2026-09-28 复核**：两个 blocking finding 均已关闭 —— **F-1** 由 Stage A/A1 关闭
> （`docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`），**F-2** 由 Stage A/A2 关闭
> （7-7 GREEN）。A3 另行收净了 A1 记录的 UI-state 诚实性遗留（create receipt / auto-select
> 竞态 / `IsOpen` 单一事实源，见 `docs/Editor-Avalonia-HRC2-StageA-A3-Closure.md`）。
> → **readiness 判定由 `CONDITIONAL PASS` 升为 `PASS`**，HRC-2-A 的前置条件已满足。
> 附注：A3 的 N3 存在已记录的证据边界（`Failed` 分支无生产证据，见 A3 Closure §4），
> 该边界不构成 readiness blocker —— HRC-2-A 的判定项是 bootstrap/ownership/pump 语义，
> 而非 create 失败注入。


### 1. Engine 实例与所有权

```text
Program.Main
  └─ using EditorHostService(HostBackend.EngineHost)     [唯一 managed Host]
      └─ App → MainWindow → MainViewModel                  [同一引用]
          └─ EngineHostChannel                            [只持 EngineHostHandle]
              └─ native EngineHost
                  ├─ Engine::Application                  [Engine owner]
                  ├─ OpenGLGraphicsFactory(persistent)     [GL owner]
                  ├─ OpenGLContext / OpenGLFramebuffer     [Host-owned FBO]
                  └─ EditorSession                         [runtime-bound session]
```

判定：**PASS**。managed 侧不持有、不创建、不重建任何 native runtime；A7-2 的
`EngineHostChannel` 内不存在 legacy `EditorSession_*` 调用，legacy 与 persistent 互斥
（A3-0 `GlfwLifetime`）使"第二 owner"在物理上不可能。

### 2. GL 生命周期

```text
EngineHost_Create → InitializeGraphics → InitializeRuntimeResources
    → (PumpOneFrame: MakeCurrent … Uncurrent)*
    → Shutdown → Teardown
```

判定：**PASS**。没有引入第二个 context owner（managed 侧零 GL 调用）。A4 的
"frame boundary `graphicsCurrent == 0`" 由 A7-1 `Hrc2A7.PendingEditRunsInsideFrameWithContextCurrent`
在加入 pending-edit drain 之后仍然成立。

### 3. Runtime Tick

判定：**PASS（带 F-2）**。唯一 pump = `EngineHostChannel` 的 `DispatcherTimer`
（33ms），由 `BeginRuntime()` 启动；`MainWindow` 的 legacy viewport timer 只在
`LegacySession` backend 存在，`EngineHost` 模式下不启动。不存在第二条 tick 路径。

**F-2**：pump 与 `EngineHost` 都在首次 `OpenProject` 时才创建。应用启动后若用户未点击
"Open Project"，进程内不存在 `EngineHost`、也不存在 pump。这对 HRC-2-A 意味着：
"production RuntimeTick owner" 目前是**用户动作触发**而非**启动触发**。
是否改为启动即 bootstrap，属于 HRC-2-A 的实现决策，不在 readiness 阶段改动。

### 4. MainWindow 真实接入 —— 最大缺口

XAML → handler 链路已存在且完整：

```text
MainWindow.axaml  Click="OnOpenProject"
  → OnOpenProject → Directory.SetCurrentDirectory(repoRoot)
  → Vm.OpenProject → host.OpenProject → BeginRuntime()（pump 启动）
  → ReloadCollectionsFromSession()
```

判定：**CONDITIONAL**。

**F-1（阻塞 HRC-2-A 实现）**：目前所有 managed 证据都来自 gate harness
（`--hrc2` 直接调用 service；`--gate1/2/3*` 走 legacy backend）。**没有任何证据表明
真实的 UI handler 路径能驱动 Host**。两者并不等价：

- gate 直接调 service，绕过 `MainViewModel` 的全部派生状态；
- `MainViewModel.IsOpen` 的定义是 `Entities.Count > 0`，依赖事件驱动的集合刷新，
  而不是 host 的 `IsOpen`；`MainWindow.OnCreateEntity` 等 handler 以 `Vm.IsOpen` 作为前置判断；
- `MainViewModel` 的 refresh 路径（`ReloadCollectionsFromSession` /
  `RefreshInspectorFromSession` / `RefreshCamera`）在 EngineHost 模式下从未被执行过；
- `ScriptError` / `IsDirty` / `DerivedStateChanged` 等 UI 状态在 production 路径未验证。

因此当前状态是"**Host API 正确**"，而不是"**生产 UI 生命周期能真正驱动 Host**"。

### 5. Failure / Teardown

判定：**PASS**。A6 已提供 transaction、OnCreate/Reload failure、callback barrier、
OnDestroy exactly-once、global/session proof；A7-2 补齐 managed 三态 gate、diagnostic
统一与 generation guard。HRC-2-A 无需重新设计 lifecycle，只需复用。

### Readiness 结论

| 项 | 判定 |
|---|---|
| Engine 实例与所有权 | PASS |
| GL 生命周期 | PASS |
| Runtime Tick 唯一 owner | PASS（附 F-2） |
| MainWindow 真实接入 | **CONDITIONAL —— F-1 阻塞** |
| Failure / Teardown | PASS |
| A6 / A7 回归 | PASS（`test_hrc` 15、��`test_bridge` 41、`test_scripting` 27，Debug+Release） |
| 跨平台 receipt | DEFERRED（不阻塞） |

**HRC-2-A Readiness = CONDITIONAL PASS。** 允许进入 HRC-2-A 实现，但 F-1 必须作为
HRC-2-A 的第一个 slice 关闭；F-2 在同阶段内决策。

---

## API 冻结声明（A7-1 / A7-2）

`ENGINE_HOST_ABI_VERSION` 现为 **`(1 << 16) | 1`（v1.1）**，单一来源定义在
`bridge/include/editor_bridge/capi.h`，由 `EngineHost.cpp`（`RuntimeResult.abi_version`）、
`capi.cpp`（`EngineHost_GetApiVersion`）与 managed `EditorBridgeApi.EngineHostExpectedVersion`
共同引用。

冻结内容：

- 既有 20 个 `EngineHost_*` 导出；
- A7-1 新增 4 个导出：`EngineHost_Dispatch` / `EngineHost_Fetch` /
  `EngineHost_PollPendingEdit` / `EngineHost_GetPendingEditProofState`；
- `RuntimeResult`（76B）与 `EditorSessionRuntimeStats` 布局；
- `ENGINE_HOST_OP_*`（27 op）、`ENGINE_HOST_STAGE_EDIT=11`、`ENGINE_HOST_STATUS_EDIT_ACCEPTED=10`。

冻结期间新增导出的条件：必须同时给出（a）关闭哪个 HRC-G2 gate，（b）negative/failure
evidence，（c）ABI 版本递增。`EditorSession_*` legacy ABI 冻结为**只读兼容面**，不再新增。

> 冻结过程中发现并修复的真实缺陷：`EngineHost_GetApiVersion()` 此前持有独立硬编码
> 字面量，未引用 `kEngineHostAbiVersion`，两处版本号可独立漂移。现已 single-source。

---

## 下一阶段规划

### 阶段 A：HRC-2-A Runtime Bootstrap（进行中）

| # | Slice | 关闭 | 状态 | 说明 |
|---|---|---|---|---|
| A1 | 生产 UI 路径证据（production-path smoke） | F-1 | `COMPLETE` | 经真实 `MainWindow` handler 驱动 Host；不新增第二 Host，不绕过三态 gate。证据：`--prodpath`（PP-01~PP-12），闭包见 `docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md` |
| A2 | 启动即 bootstrap 决策与落地 | F-2 | `IMPLEMENTED / 7-7 GREEN / FINDING CLOSED` | contract 已 ratify；step 2 已实现 `EnsureRuntime()` + failure propagation seam，7/7 场景 GREEN。native 退出 hang 的根因已在 **NHI-1 Closure B** 定位为**测试环境交互**（NVIDIA overlay FLS 回调 + ASan 分配器，死锁在 `LdrShutdownProcess`，与 EngineHost/GLFW/teardown 无关），处置=runner 排除 overlay + 登记为基础设施约束 |
| A3 | `MainViewModel` refresh / create-state 收口 | A3-01/02/03 | `CLOSED` | step 1 审计+contract 冻结；§8 D1~D4 已 ratify；step 2 实现落地（`IsOpen` host 派生 + 唯一 reconcile、selection intent 顺序无关、create receipt、`--uistate` gate + runner，PP-05 由 KNOWN GAP 转硬断言）；N3 按 contract §9 方案 A 收敛为 Refused 负例（S0 + N5 共用 `AssertNoPhantomState`）；证据：`--uistate` 4-4 GREEN、`--prodpath` ALL GREEN、A2 `--boot` 7-7、I2=240 不变、native 零改动 |
| A4 | failure / teardown 复用验证 | — | `PENDING` | 只复用 A6/A7 contract，不重新设计 |
| A5 | A6/A7 全量回归 | — | `PENDING` | native + managed + HRC-1 gates |

### 阶段 B：HRC-2 Closure

- Core Runtime Contract 收口（Windows receipt 为唯一 readiness 依据）；
- 明确记录 Linux receipt 为 DEFERRED 而非缺失；
- Definition of Done 复核。

### 阶段 C：HRC-3 Viewport Reality（独立阶段）

- Viewport / Picking / Gizmo / Renderer 全部在此阶段，本阶段之前一律不碰。

### 阶段 D：A8 后置 Receipt

- 触发条件见 A8 章节的 4 条恢复条件。

### 明确不做（延续到后续阶段）

`Linux` · `Xvfb` · `跨设备验证` · `Viewport` · `Picking` · `Gizmo` · `Renderer 重构` ·
`test_ecs` · `Phase 7 warning cleanup` · `Content Pipeline` · `SaveProject/ScriptSave 落盘闭环`


### A7-1 结果：native op-dispatch seam + pending-edit queue

> **状态**：`COMPLETE / G1-01~G1-08 PASS`  
> **范围**：仅 native（`capi.h`、`EngineHost.h/.cpp`、`capi.cpp`、`test_hrc.cpp`）。未触碰 A6 lifecycle contract、`PumpOneFrame` ownership、Renderer/Viewport/Gizmo/ECS。

#### 新增 ABI（纯 additive，`RuntimeResult` 布局未变：仍 76B / offsetof 16/48/68）

| 符号 | 作用 | 关闭的 gate |
|---|---|---|
| `EngineHost_Dispatch` | 无输出缓冲的 session op，复用既有 `Submit()` executor | G1-01 / G1-06 |
| `EngineHost_Fetch` | 带输出缓冲的 session 查询 | G1-01 |
| `EngineHost_PollPendingEdit` | pending-edit completion seam（非阻塞） | G1-03 / G1-07 |
| `EngineHost_GetPendingEditProofState` | accepted/executed/failed/contextCurrent/frameId | G1-03 / G1-04 / G1-05 |

新增 `ENGINE_HOST_STAGE_EDIT = 11`、`ENGINE_HOST_STATUS_EDIT_ACCEPTED = 10`、`ENGINE_HOST_OP_*`（27 op）、`ENGINE_HOST_GL_OP_ASSIGN_SPRITE`。

#### 关键实现决定

- **不新增第二个 executor**：所有 SESSION op 走既有 `Submit()`，native runtime thread 仍是唯一执行者。
- **accepted ≠ executed**：GL op 的 `Dispatch` 返回 pending edit id 并置 `EDIT_ACCEPTED`；执行结果只能由下一次 `PumpOneFrame` 产生，经 `PollPendingEdit` 非阻塞取回。UI 线程不会被阻塞，因此不会与 UI 驱动的 pump 死锁。
- **GL 依赖 edit 只在 pump 窗口内执行**：`DrainPendingEdits()` 位于 `MakeRuntimeContextCurrent()` 之后、Engine update 之前；A4 的 "frame boundary `graphicsCurrent==0`" 不变量未变。
- **queued edit 失败不升级为 lifecycle fault**：记入 pending-edit result + Host diagnostic，host 继续可 pump（非静默 no-op）。
- **未排空的 edit 必须有确定结论**：`CloseProject` 与 runtime 线程退出都调用 `AbandonPendingEdits()`，标记为 failed 而非丢弃。
- **参数槽位固定、无序列化协议**：`arg0/arg1` 为 int32 索引，`strArg/strArg2/strArg3` 为 UTF-8 字符串，`floatArg` 为 3 float，`outBuffer` 为字符串或 float3。避免自造 pack/unpack 协议带来的错位风险。

#### Gate evidence（`tests/test_hrc`，A4/A6 9 + A7 6 = 15）

| Gate | Test | 关键断言 |
|---|---|---|
| G1-01 | `Hrc2A7.OpDispatchRoutesEverySessionCall` | 27 op 全部经 Host；未知 op / 跨通道 op 被拒；无 project 时场景依赖 op 返回 typed 失败；fetch 容量不足被拒 |
| G1-02 | `Hrc2A7.DiagnosticUnification` | 成功清空诊断；失败把 session 错误文本送到 `EngineHost_GetLastDiagnostic` |
| G1-03 | `Hrc2A7.PendingEditRunsInsideFrameWithContextCurrent` | enqueue→accepted；pump 前 `PollPendingEdit==1`；pump 后 `==0` 且 `frame_id==1`；执行前 sprite 未绑定、执行后已绑定 |
| G1-04 | 同上 | `lastExecutedWithContextCurrent == 1` |
| G1-05 | 同上 + `UndrainedEditIsAbandonedNotDropped` | 每帧边界 `graphicsCurrent==0`；`lastExecutedFrameId` 为真实帧号；未排空 edit 在 CloseProject 后为 failed + 非空 diagnostic |
| G1-06 | `Hrc2A7.SinglePumpOwnerAndLifecycleRegression` | 交错 dispatch/pump 的 `owner_thread_id` 恒等；persistent 模式下 `EditorSession_Create()==nullptr`（无第二 owner） |
| G1-07 | `Hrc2A7.PendingEditFailureIsObservable` | 失败计数、diagnostic 非空、`emergencyRequested==0`、失败后仍可 pump |
| G1-08 | `Hrc2A7.SinglePumpOwnerAndLifecycleRegression` + A6 回归 | Play/Reload/Stop/`on_create_count==2`/teardown proof 归零；glfw users 回基线 |

#### 回归结果

```text
test_hrc       Debug 15/15   Release 15/15   (A4 7 + A6 2 + A7 6)
test_bridge    Debug 41/41   Release 41/41
test_scripting Debug 27/27   Release 27/27
ctest Debug    10/10 in-scope（test_ecs 为既有 Not Run，按 HRC-2 范围排除）
dotnet build   0 errors / 0 warnings
HRC-1 gates    --hrc1 --selftest --gate1 --gate2 --gate3a --gate3b --gate3c --gate3d --gate3g 全 exit 0
pure-C smoke   gcc -std=c11 -fsyntax-only 通过
residual       targeted process = 0；git diff --check exit 0
```

#### 过程中的 fixture 事故与处置

A7-1 初版测试直接对 repo 的 `assets/gp01` 调用 `ScriptSave` / `SaveProject`，覆盖了 `assets/gp01/game.lua` 并向 `assets/gp01/Main.scene` 注入 3 个 `A7Probe*` 实体。已处置：

- `Main.scene`：移除注入实体，恢复为 10 entities / 合法 JSON / 原始 name 集合。
- `game.lua`：经确认后 `git checkout --` 从 HEAD 恢复 12939 字节真实 GP01 导演脚本；`test_bridge` 随之回到 41/41。
- 测试改为复用 `test_bridge` 既有的 scratch-copy 约定（`gp01_hrc_a7_scratch`，已加入 `.gitignore`），写操作只落副本；后续三次运行确认 repo fixture mtime 未再变化。

#### 已知遗留（不属于 A7-1）

- `test_ecs` 在 ctest 中为 `***Not Run`（`build/tests/Debug/Debug/test_ecs.exe` 双层路径），属既有注册问题，按 HRC-2 范围排除。
- `EngineHost::m_Generation` 仍只在 `Initialize` 写一次；managed 侧 late-event generation guard 需在 A7-2 处理。




**修改范围**：

- `editor_avalonia/Services/EditorHostService.cs`；
- `editor_avalonia/MainWindow.axaml.cs`；
- 必要的 `MainViewModel` event marshal；
- 不改变 HRC-1 注入链。

**Acceptance**：

- Host 只持有 `EngineHostHandle`；
- 所有 native command 走同一 executor；
- callback copy 后 `Dispatcher.UIThread.Post`；
- generation 丢弃 late event；
- delegate rooted 到 `EngineHost_Shutdown` 完成；
- UI timer 不再驱动 runtime tick；
- CloseProject 与 terminal Dispose 语义保持 HRC-1 兼容。

### A8 — Cross-platform Receipt

> **状态**：`DEFERRED / NON-BLOCKING`  
> **不阻塞 HRC-2-A Readiness、也不阻塞 HRC-2 Closure。**

原 Acceptance 中"Linux Xvfb + X11 + software GL 实际运行"与"0 skipped 才能 readiness"
已从 readiness 前置条件中移除。理由不是架构失败，而是**当前没有可用的真实 Linux/Xvfb
验证环境**；在没有环境的情况下冻结该项，而不是制造或假设环境来换取一个 PASS。

调整后的顺序：

```text
A7-2 → HRC-2-A READY → HRC-2-A Runtime → HRC-2 Closure → HRC-3 Viewport → A8（后置）
```

#### 平台能力与架构正确性分层（新增永久原则）

```text
Core Runtime Contract（架构正确性）
    ├── Windows receipt   ← 当前主开发环境，唯一 readiness 依据
    ├── Linux receipt     ← DEFERRED
    └── Other platform    ← Future
```

Cross-platform receipt 不得再作为 Core Host Runtime correctness 的前置条件。
即使数月后才有 Linux CI 或设备，也不应迫使 HRC-2 的核心架构重新等待。

#### A8 恢复条件（全部满足才重新开启）

1. 有可执行的 Linux 环境或可用 CI runner；
2. `LIBGL_ALWAYS_SOFTWARE=1` + Xvfb 下 `test_hrc` / `test_bridge` 实际通过；
3. managed `--hrc2` 在该平台 exit 0；
4. 重新评估是否需要 managed smoke 的 native 落盘闭环（`SaveProject` / `ScriptSave`）。

#### 已提前完成、原本归属 A8 的部分

- HRC target 显式构建（`EditorBridge test_bridge test_hrc Sandbox EditorDemo`），不依赖
  `ENABLE_HRC_TESTS`（该选项并不存在，`test_hrc` 在 `ENABLE_TESTS` 下无条件加入）；
- `tools/integrity_gate.ps1` + Evidence-Baseline 登记（I1=28 files、I2=240 tests）；
- `--hrc2` managed gate（G2-01~G2-10，`Services/Hrc2Gate.cs`）；
- CI workflow 的 Xvfb / software GL / `LastTest.log` 上传配置（仅配置，无 receipt）。

---

## 7. HRC-G2P evidence order

| Gate | 实现 slice | 主要 evidence |
|---|---|---|
| G2P-01 | A3/A5 | persistent context、FBO Ready、FBO complete、Session attach 顺序 |
| G2P-02 | A2/A3/A5 | owner thread、make/un-current、wrong-thread、GL delete trace |
| G2P-03 | A2/A4/A6/A7 | one pump、tick count、frame id、stop/join |
| G2P-04 | A1/A2/A6 | typed result、dt、status/stage、exception boundary |
| G2P-05 | A2/A3/A4/A5/A6 | stage failure injection、LIFO rollback、no half-init |
| G2P-06 | A2/A4/A5/A6/A7 | shutdown trace、ack、OnDestroy once、no late callback |
| G2P-07 | A2/A4/A5/A6 | H1 Dispose→H2 Bootstrap、global counters zero |
| G2P-08 | A8 | Windows/Linux actual Gate、Xvfb、no skip、artifacts |

---

## 8. Test protocol

### 8.1 Native `test_hrc`

同一进程按固定顺序执行：

```text
H1 Bootstrap
  → OpenProject
  → non-Playing frames
  → Play
  → Playing frames
  → failure injection
  → CloseProject
  → Dispose
  → assert all counters zero
H2 Bootstrap
  → repeat minimal Open/Play/Tick
  → Dispose
```

每帧至少记录：

```text
gate_id
status
stage
operation_id
frame_id
generation
owner_thread_id
raw_dt
effective_dt
context_id
fbo_id
tick_count
callback_count
scheduler_live_count
glfw_init_count
glfw_terminate_count
current_count
un_current_count
session_count
```

### 8.2 Failure matrix

必须覆盖：

```text
Platform init failure
GLContext failure
GLAD failure
EngineSubsystem failure
FBO failure
Session attach failure
OpenProject failure
Play OnCreate failure
Reload OnCreate failure
RuntimeTick script failure
Stop failure
Shutdown failure
wrong-thread operation
```

### 8.3 CI commands

Windows：

```powershell
cmake -S . -B build/hrc-win -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON -DENABLE_HRC_TESTS=ON -DENABLE_ASAN=OFF
cmake --build build/hrc-win --config Debug --target test_bridge test_hrc
ctest --test-dir build/hrc-win -C Debug -R "^(test_bridge|test_hrc)$" --output-on-failure
```

Linux：

```bash
cmake -S . -B build/hrc-linux -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_HRC_TESTS=ON -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF
cmake --build build/hrc-linux --target test_bridge test_hrc
LIBGL_ALWAYS_SOFTWARE=1 XDG_SESSION_TYPE=x11 xvfb-run -a -s "-screen 0 1280x800x24" ctest --test-dir build/hrc-linux -R "^(test_bridge|test_hrc)$" --output-on-failure
```

---

## 9. Verification matrix

| 检查 | 命令/入口 | 通过条件 |
|---|---|---|
| Managed compile | `dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore` | 0 errors；既有 warning 单独记录 |
| Legacy bridge | `cmake --build build --config Debug --target EditorBridge test_bridge` | PASS |
| Legacy bridge test | `ctest --test-dir build -C Debug -R '^test_bridge$' --output-on-failure` | PASS |
| HRC-1 | `--hrc1`、`--selftest`、`--gate1`、`--gate2`、`--gate3a/3b/3c/3d/3g` | 全部 exit 0 |
| HRC native | `test_hrc` | G2P-01~08 结构化 evidence 全绿 |
| HRC managed smoke | `--hrc2` | 不创建第二 Host、不跳过 teardown |
| Restart | native H1→H2 test | global/static/resource counters 清零 |
| Diff hygiene | `git diff --check`、status/fingerprint | 无意外清理/覆盖既有 dirty changes |

`test_ecs` 继续按 `docs/Evidence-Baseline.md` 的独立 EXCLUDED baseline 处理；不得为了让全量测试变绿而修改 ECS API。

---

## 10. Stop conditions

立即停止当前 slice 并回到审计/修复的条件：

- 任意 HRC-1 gate 回归失败；
- 出现第二个 runtime thread、tick source 或 `glfwTerminate` owner；
- UI thread 触碰 GL/Session raw handle；
- context teardown 时仍有 live GPU resource；
- `OnDestroy` 次数不等于一次；
- H1 Dispose 后 H2 无法 Bootstrap；
- failure path 留下 live handle/callback/timer；
- CI 通过依赖 skip；
- 需要 reset/clean/stash 才能继续；
- 实现必须覆盖 Phase 7 既有 dirty changes。

---

## 11. Definition of done

HRC-2-A 只有在以下全部满足时完成：

1. A0～A8 全部完成并有 evidence；
2. HRC-G2P-01~08 全部 GREEN；
3. production Host 不再直接持有 `EditorSessionHandle`；
4. runtime thread、GL context、FBO、Session ownership 唯一；
5. typed `RuntimeResult` 和 failure injection 可查询；
6. H1 Dispose→H2 Bootstrap 在同一进程通过；
7. Windows/Linux 实际 headless Gate 通过且无 skip；
8. HRC-1 全部回归继续通过；
9. 未覆盖或重置既有 Phase 7/HRC-1 dirty changes；
10. 未创建未经请求的 commit/tag。

> **2026-09-28 收口对照**：本 DoD **满足 7/10**；第 **1、2、7** 项为 **A8 门控 OPEN**
> （A8 DEFERRED —— 无真实 Linux/Xvfb 环境，G2P-08 与 Linux headless Gate 均无法执行，
> 且**不以 skip 充数**）。因此 HRC-2 的收口是**范围限定**结论（Windows hidden-GLFW），
> 逐项对照见 `docs/Editor-Avalonia-HRC2-Closure.md` §1.1。
> 完整 DoD 达成的唯一解锁条件是 A8 取得真实 headless GL 环境。

---

## 12. Current decision

```text
HRC-2-P:                 COMPLETE / CORE TOPOLOGY FROZEN
A0–A3 native:            COMPLETE（persistent carrier / owner / rollback / C ABI 边界）
A4/A6:                   COMPLETE（runtime seam proof / lifecycle hardening）
A7-0/A7-1/A7-2:          COMPLETE（32-call routing / native dispatch seam / managed host switch）
A8:                      DEFERRED / NON-BLOCKING（无真实 Linux/Xvfb 环境，见 A8 恢复条件）
Stage A/A1:              COMPLETE（F-1 关闭；2 个既有缺陷已修；A3 已把 PP-05 的竞态从 KNOWN GAP 转为硬断言并通过）
Stage A/A2:              IMPLEMENTED / FUNCTIONAL GATES 7-7 GREEN / NATIVE TEARDOWN FINDING 由 NHI-1 关闭
Stage A/A3:              CLOSED（D1/D2/D3 落地；`--uistate` 4-4 GREEN；N3 按 contract §9 方案 A 收敛为 Refused 负例；闭包文档 `docs/Editor-Avalonia-HRC2-StageA-A3-Closure.md`）
NHI-1:                   CLOSED / Closure B — 根因已识别（NVIDIA overlay FLS + ASan 分配器退出期死锁）；受控对比：ASan 37/12,225 vs Release 0/400
A3 runner flake:         独立记录为 runner / process-exit hygiene observation（**不重开 NHI-1**、非产品 workaround、不计入 A3 功能结果；见 A3 Closure §6）
HRC-2-A readiness:       PASS（F-1 由 A1 关闭、F-2 由 A2 关闭、F-3 UI-state honesty 由 A3 关闭；N3 证据边界不构成 readiness blocker）
HRC-2:                   CLOSED（`docs/Editor-Avalonia-HRC2-Closure.md`：证据链 A1→A2→A3→NHI-1 汇总；G2P-01~07 GREEN 逐项挂已执行用例；G2P-08 属 A8 DEFERRED）
HRC-G2P-01~08:           G2P-01~07 GREEN（Windows hidden-GLFW 范围）/ G2P-08 DEFERRED（A8）
HRC-3 Step 1:            AUDIT COMPLETE / CONTRACT PROPOSED（`docs/Editor-Avalonia-HRC3-Viewport-Reality-Audit.md`；零代码改动；核心结论：生产路径无 present 通道、FBO 为固定 64×64 清屏+单像素探针、pick/texture handle 为 stub、`PickingFBO` 实现完整但零接线）
HRC-3 D1~D5:             D1/D2/D5 RATIFIED；D3 RATIFIED as later slice（pick）；D4 RATIFIED，implementation deferred（resize）
HRC-3 Step 2:            PRE-FLIGHT GAP CLOSED / implementation blocked by identified prerequisites（`docs/Editor-Avalonia-HRC3-Step2-Preflight-Gap.md`；零代码）—— GAP-A 生产路径无场景绘制（`Application::InternalRender` headless 短路；唯一渲染驱动属 legacy imgui editor）/ GAP-B 冻结 ABI 无像素通道 / GAP-C 无「像素+frame_id」同调用返回
HRC-3 D1~D5:             D1 RATIFIED；**D2 修订 RATIFIED**（no new export function；允许 1 个 additive `EngineHost_Fetch` op，原子返回 frame identity + pixels；**明确是 ABI increment** v1.1→v1.2）；选项 A 采用 / B 不采用 / **C 明确禁止**；D3 后置独立 slice；D4 语义批准、实现后置；D5 RATIFIED
HRC-3 P1/P2 contract:    FROZEN（`docs/Editor-Avalonia-HRC3-P1P2-Contract.md`；零代码）—— P1-H1~H7（禁止接回 `ViewportPanel::OnUpdate`／legacy ViewportPanel 依赖／新增 tick 源）、P2-H1~H9 + 六项 ABI 交付物、ABI/version 增量规则六条（含 "27 op @ v1.1 保留为 historical baseline"）、历史 76B `RuntimeResult` 不得扩展
next action:             ratify **D2.1**（packet 尺寸协议；建议 D2.1-a 固定 v1 目标尺寸 + 既有静态容量模型，两阶段 size-query 留给 D4 slice）+ 确认 P1 实现归属（P1-a `Application` 拥有 headless 渲染步 / P1-b EngineHost 自有 render step）→ 才允许动 native；A8 继续 DEFERRED
A8:                      继续 DEFERRED（不因本 finding 转去跨平台验证）
```

下一步：**NHI-1**（A2 → 下一阶段之间的 blocker）。
- 定义与范围：`docs/Editor-Avalonia-HRC2-NHI-1-Charter.md`
- 实测数据与未决项：`docs/Editor-Avalonia-HRC2-NHI-1-Log.md`
- 调查工具：`tools/nhi1_stress.ps1`（批量压测 + hang 取证；不是门禁，不做 watchdog 绿化）

A1 证据见 `docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`；
A2 契约与实现证据见 `docs/Editor-Avalonia-HRC2-StageA-A2-Trigger-Contract.md` 与
`docs/Editor-Avalonia-HRC2-StageA-A2-Closure.md`。
