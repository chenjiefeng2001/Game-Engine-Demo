# Editor Avalonia HRC-2-P — Runtime Seam Confirmation

> **阶段**：UI-HRC-2-P（Preflight / Seam Confirmation）  
> **日期**：2026-09-24  
> **审计状态**：`COMPLETE`  
> **HRC-2-A 状态**：`NOT READY`  
> **HRC-G2P-01~08**：`0 PASS / PARTIAL OR NOT READY`  
> **本阶段性质**：只读 seam 审计与核心拓扑决策冻结；不修改 Program、GL、Bridge 或 runtime 实现

---

## 1. Preflight decision

HRC-2-0 已经确认 HRC-1 的 managed Host ownership 成立，但 HRC-2-P 需要进一步确认三个实现 seam：

```text
P1  Offscreen / headless-capable GL
P2  Single runtime thread / frame pump
P3  Tick / ABI / failure boundary
```

当前结论：

```text
HRC-2-P source audit:       COMPLETE
HRC-2-P contract:           CORE TOPOLOGY FROZEN / READY FOR HRC-2-A
HRC-G2P-01~08:              NOT GREEN
HRC-2-A implementation:    NOT READY
HRC-1 regression:          MUST REMAIN GREEN
HRC-3 viewport reality:    NOT STARTED
Avalonia UI:                PARTIAL / NOT FROZEN
```

本阶段不把“存在某个可调用的 GL API”当作 seam 已确认；必须同时明确 owner、thread affinity、lifecycle、failure result 和可执行 evidence。

---

## 2. Baseline

| 项目 | 值 |
|---|---|
| HRC-2-0 audit | `docs/Editor-Avalonia-HRC2-Audit.md` |
| HRC-1 closure | `docs/Editor-Avalonia-HRC1-Closure.md` |
| Branch | `avalonia` |
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| HRC-1 tracked diff fingerprint | `0254154b1d8b78dccc0a5f3646cc5c43841f3d91` |
| HRC-1 immutable tag | 未创建，dirty Phase 7 overlap |
| HRC-G1 | `ALL GREEN`（继承 HRC-1 Closure 证据） |
| HRC-2-P implementation changes | 无 |

tracked diff fingerprint 不包含未跟踪文件；Phase 7、HRC 文档、spike 目录继续按 HRC-1 Closure 的 dirty-baseline 规则处理。

---

## 3. G2P readiness summary

| Gate | 要求 | 当前判定 | 核心原因 |
|---|---|---|---|
| HRC-G2P-01 | Offscreen GL seam identified | **PARTIAL** | 只有临时 GLAD loader；没有 Host-owned persistent context API |
| HRC-G2P-02 | GL thread affinity identified | **PARTIAL** | GLFW current context 是隐式线程行为；无 owner-thread enforcement/current-uncurrent contract |
| HRC-G2P-03 | Single frame-pump owner identified | **NOT READY** | `Application::Run()` 阻塞；RuntimeTick production caller=0；UI timer 只做 ViewportTick |
| HRC-G2P-04 | Tick/ABI contract identified | **NOT READY** | RuntimeTick 是 void；无 init/readiness/typed result/ack ABI |
| HRC-G2P-05 | Initialization rollback defined | **PARTIAL** | HRC-1 session rollback 局部存在；Engine/GL/Session bootstrap failure 无统一 result/LIFO evidence |
| HRC-G2P-06 | Terminal teardown ordering defined | **PARTIAL** | legacy 局部析构顺序存在；没有 Host-owned Engine/GL/scheduler teardown 或 shutdown acknowledgement |
| HRC-G2P-07 | Global/static reset boundary defined | **PARTIAL** | HRC-2-0 已列风险和候选顺序；Application/GLFW/GameplayAPI/Time 等没有实现证据 |
| HRC-G2P-08 | CI/headless strategy identified | **NOT READY** | CI 默认不启用 tests；没有固化 Linux virtual display/software GL，也没有 HRC-2 runtime gate |

**总判定：0 PASS；5 PARTIAL；3 NOT READY。**

PARTIAL 只表示存在可复用构件或候选契约，不表示 Gate 可升格为 GREEN。

---

## 4. Current runtime graph

```text
Program.Main
  └─ using EditorHostService
      └─ App
          └─ MainWindow
              ├─ same Host
              └─ MainViewModel
                  └─ same Host
                      └─ lazy EditorSession
                          ├─ Content/Scene/Script
                          ├─ OpenGLGraphicsFactory
                          └─ Application::GetInstance() paths
```

当前 Host 拥有的实际内容：

- raw `EditorSession` handle；
- callback；
- managed project/session state；
- `CloseProject` / terminal `Dispose` 语义。

当前 Host 不拥有：

- `Engine::Application`；
- persistent GL context；
- render target/FBO owner；
- frame scheduler；
- production RuntimeTick pump。

证据：`editor_avalonia/Program.cs:10-22`、`editor_avalonia/App.axaml.cs:17-28`、`editor_avalonia/Services/EditorHostService.cs:11-25`、`bridge/include/editor_bridge/capi.h:57-66`。

---

## 5. P1 — Offscreen / headless-capable GL seam

### 5.1 Context creation API and owner

当前 `OpenGLGraphicsFactory` 构造函数：

1. `glfwInit()`；
2. 设置 OpenGL 4.6 hints；
3. 创建 hidden 1×1 `"GLAD Loader"` window；
4. `glfwMakeContextCurrent`；
5. 加载 GLAD；
6. 立即 `glfwDestroyWindow`。

证据：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:31-70`。

该路径是 function-loader context，不是 persistent offscreen render context。构造结束后没有保存 carrier window/context，也没有 readiness 状态。

当前 owner 实际是：

```text
EditorSession
  └─ OpenGLGraphicsFactory
      ├─ glfwInit
      ├─ temporary GLAD window
      └─ glfwTerminate
```

证据：`bridge/src/EditorSession.h:198-200`、`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:72-75`。

### 5.2 Frozen P1 contract

HRC-2-P 推荐冻结的最小 seam：

```text
EditorHostService
  └─ native EngineHost
      ├─ platform/graphics owner
      ├─ persistent hidden GLFW context carrier
      ├─ GLAD/function loader
      ├─ Engine Application/subsystems
      ├─ offscreen render target/FBO
      └─ session/runtime registration
```

约束：

- HRC-2 使用 Host-owned offscreen persistent OpenGL context；GLFW init/carrier create/destroy 由 creation/UI main thread 负责，runtime thread 只 make/un-current context；
- Avalonia 保持自己的 rendering context；
- HRC-2 不做 NativeControlHost、GPU texture sharing 或 GL interop；
- runtime 必须能在没有 Avalonia Viewport surface 的情况下创建 context、FBO、Engine frame 和 Session；
- HRC-2-A backend 已冻结为 **Hidden GLFW**：复用现有 GLFW/GLAD 基础，不在 HRC-2-A 引入 EGL/OSMesa 或平台分支 backend。backend 不得改变 ownership/affinity/failure 语义。

A3 已建立 `EngineHost`/persistent context ABI 和 graphics-state probe；完整 failure injection、legacy isolation 和 HRC-G2P evidence 尚未完成，因此 G2P-01 仍为 PARTIAL。

### 5.3 Current-context and un-current boundary

A3 已建立 creator/runtime handoff：creation/UI thread 创建 hidden carrier，runtime thread make-current，shutdown 先 runtime un-current 再由 creator 销毁 carrier。`EngineHost_GetGraphicsState` 和 make/un-current counters 可查询，worker `EngineHost_Create` 返回 `WrongThread`。

仍缺少 failure injection、完整 transition trace 和 CI evidence，因此 G2P-02 尚未 PASS。

### 5.4 Offscreen FBO capability

已有可复用构件：

- `OpenGLFramebuffer` 可创建颜色/深度/ID attachment；
- `ViewportPanel::InitMRTFramebuffer()` 可创建 MRT FBO；
- `ViewportPanel::Render3DScene()` 可在没有真实 UI surface 的情况下向 FBO 渲染。

证据：`engine/src/OpenGL/OpenGLFramebuffer.cpp:47-138`、`engine/src/Editor/ViewportPanel.cpp:94-147,167-241`。

但当前存在两个未登记的 viewport owner：

- `Application::m_ViewportPanel`；
- `EngineEditor::m_Viewport`。

Bridge 读取前者，而 readback 开启和 `OnUpdate` 发生在后者。FBO primitive 存在，不代表 HRC-2 runtime seam 已确定。HRC-2-A 已确认由 **Host-owned native runtime** 创建和销毁 offscreen FBO；UI `ViewportPanel` 不拥有 HRC-2 render target。

### 5.5 P1 failure and teardown candidate

初始化失败必须返回可查询 stage：

```text
GLFW/platform init failure
persistent context/window failure
GLAD failure
FBO/render-target failure
Engine subsystem failure
```

任何失败都必须：

- 不创建 Session；
- 不留下 live context/window/FBO/timer；
- 已成功资源按 LIFO rollback；
- 允许 Host 进入 `Faulted` 并查询诊断。

Terminal teardown 顺序：

```text
stop commands/tick/render
  → destroy FBO/readback/GPU resources
  → destroy Session/runtime data
  → shutdown Engine subsystems
  → clear Application/global state
  → un-current context
  → destroy context carrier/window
  → glfwTerminate exactly once
```

`glfwTerminate` 必须是最后一步。

### 5.6 CI/headless current evidence

当前根 CMake 默认 `ENABLE_TESTS=OFF`：`CMakeLists.txt:66-80`。

workflow configure 没有 `-DENABLE_TESTS=ON`，但仍执行 ctest：`.github/workflows/cmake-multi-platform.yml:84-99`。

Linux workflow 只安装 X11/Wayland/OpenGL 开发库，没有明确 Xvfb、virtual display 或 software GL：`.github/workflows/cmake-multi-platform.yml:69-76`。

现有测试提供局部证据：

- `GL46DeviceTest` 创建 hidden GLFW window，GL 不可用时 skip：`tests/test_renderer/GL46DeviceTest.cpp:25-53`；
- `EngineBootTest` 对 `glfwInit` 使用硬断言，hidden window 不是可靠的 no-display headless 保证：`tests/test_e2e/EngineBootTest.cpp:52-76`；
- CI path filter 不包含 `.cs` / `.axaml` / `.csproj`：`.github/workflows/cmake-multi-platform.yml:5-25`。

因此 G2P-08 当前 NOT READY。

HRC-2-A 的 CI/headless 策略已确认：

- Windows：使用 Hidden GLFW；
- Linux：使用 Hidden GLFW + Xvfb；
- 两个平台均以 `ENABLE_TESTS=ON` 运行实际 HRC-2 Gate；
- `skip`、仅有 hidden-window 编译通过或未创建 context 不算 readiness evidence；
- Gate 必须记录 context/FBO/current/un-current/rollback 结果。

该策略解决实现路线选择，但尚未改变当前 G2P-08 的 NOT READY 状态；workflow 和测试实现仍需完成。

---

## 6. P2 — Single runtime thread / frame pump

### 6.1 Current production tick

当前 RuntimeTick 链存在，但 production caller=0：

```text
EditorHostService.RuntimeTick
  → EditorBridgeApi.EditorSession_RuntimeTick
  → C ABI
  → EditorSession::RuntimeTick
  → ScriptInstance::OnUpdate
```

证据：`editor_avalonia/Services/EditorHostService.cs:259-263`、`editor_avalonia/Native/EditorBridgeApi.cs:144-145`、`bridge/src/capi.cpp:269-272`、`bridge/src/EditorSession.cpp:746-751`。

唯一实际调用是测试中的单帧手动 tick：`tests/test_bridge/test_editor_capi.cpp:612-624`。

MainWindow 的 33ms timer 只调用 `ViewportTick`：`editor_avalonia/MainWindow.axaml.cs:42-48,344-353`。它不推进 RuntimeTick，也不 render。

### 6.2 Frozen P2 contract

HRC-2-A 已确认采用 **ONE dedicated runtime thread**：

```text
ONE runtime thread
  ├─ make GL current
  ├─ drain UI commands
  ├─ Engine frame
  ├─ EditorSession RuntimeTick
  ├─ render/submit
  ├─ un-current
  └─ publish result to UI
```

UI thread 只做：

```text
UI events
  → Host command boundary
  → result/event back to UI
```

规则：

- runtime thread 是唯一 Engine/GL/Session/tick owner；
- frame cadence 使用 monotonic variable dt；测试可手动 pump，不把 60Hz 作为 HRC-2-A SLA；
- UI DispatcherTimer、Host timer、Engine internal tick 不得形成多个 source；
- 不在 UI thread 调用阻塞式 `Application::Run()`；
- native runtime 必须拆为 initialize / pump-one-frame / shutdown；
- start 只在 Ready 后发生；
- Stop/Close/Dispose 先拒绝新 command，再 stop/join runtime thread；
- fixed simulation substep 与 RuntimeTick invocation 分开命名，不能隐式产生多次 gameplay tick；
- UI command 按 owner-thread 顺序执行；
- runtime result 明确标注 status、stage、frame id、generation、owner thread。

当前没有 dedicated runtime thread、command queue、join barrier 或 result envelope，因此 G2P-03 当前 NOT READY，P3 的 cross-thread seam 仍 PARTIAL。

### 6.3 Stop/close/dispose candidate

CloseProject：

```text
reject new commands
  → stop/join project runtime work
  → detach callback
  → Stop runtime
  → OnDestroy exactly once
  → GameplayAPI::Reset
  → destroy Session/runtime data
  → retain Engine/GL/Host
  → Ready
```

Terminal Dispose：

```text
stop UI/runtime commands
  → stop/join runtime thread
  → detach callbacks
  → destroy Session
  → destroy GPU resources
  → shutdown Engine
  → clear globals
  → un-current/destroy context
  → glfwTerminate
  → return shutdown acknowledgement
  → Host Disposed
```

重复 Dispose 必须 no-op；Dispose 后所有 runtime command 稳定拒绝。

### 6.4 Current partial foundations

HRC-1 已有：

- `CloseProject` 与 terminal `Dispose` 分离：`editor_avalonia/Services/EditorHostService.cs:569-618`；
- managed Dispose 幂等；
- callback registration rollback。

但当前没有：

- scheduler join；
- native exception boundary；
- tick result；
- shutdown acknowledgement；
- late callback generation；
- `OnDestroy` exactly-once evidence。

---

## 7. P3 — Tick / ABI / failure seam

### 7.1 Frozen tick time contract

当前 Engine Time 使用 steady clock，秒为 dt 单位；`Time::UpdateDeltaTime()` 有 0.25 秒 clamp：`engine/include/Engine/Platform/PlatformUtils.h:41-68`、`engine/src/Core/Time.cpp:117-131`。

C ABI 只声明 `float dt`，没有单位、finite、范围或 clamp 约定：`bridge/include/editor_bridge/capi.h:167-192`。

HRC-2-A 已确认：

- `dt` 单位为秒；
- runtime 使用 monotonic variable dt；
- raw dt 必须 finite、非负；
- 暴露 raw/effective dt，effective clamp 为 `0.1s`；
- 每 Host frame 最多一次 RuntimeTick；
- 非 Playing 返回 `SkippedNotPlaying`；
- frame id、generation、owner thread 随 result 可观测。

### 7.2 Current ABI result gaps

| 操作 | 当前 ABI | Preflight 缺口 |
|---|---|---|
| EngineHost create | 不存在 | 没有 runtime handle/readiness |
| GL init | 无独立 API/result | factory 只 log |
| Session create | handle/null | 不能表达 Engine/GL failure |
| OpenProject | int32 bool + error | 无 stage/generation/rollback result |
| Play/Reload | int32 bool + error | OnCreate 错误不改变成功结果 |
| RuntimeTick | void | 无 success/skip/fault result |
| Stop | void | 无 stop acknowledgement |
| Shutdown | void | 无 teardown completion/error |
| Event | synchronous callback | 无 late callback/generation token |

证据：`bridge/include/editor_bridge/capi.h:167-192`、`bridge/src/capi.cpp:256-317`、`editor_avalonia/Native/EditorBridgeApi.cs:133-160`。

### 7.3 Frozen status/error ABI

HRC-2-A 采用统一 typed result；所有 Tick/Stop/Shutdown 操作返回同一类可查询结果：

```text
status:
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

stage:
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

metadata:
  operation_id
  frame_id
  session/runtime_generation
  owner_thread_id
  error_code
  diagnostic_buffer
```

规范：

- native runtime 拥有 canonical error record；
- C# 只镜像/展示，不重新定义错误语义；
- error record 绑定 operation/frame/generation；
- Lua diagnostic 与 native/GL diagnostic 分开；
- 成功操作不能无条件继承旧错误；
- C++ exception 不得穿 C ABI；
- C# callback exception 不得返回 native。

### 7.4 Frozen rollback matrix

```text
Create Engine
  └─ fail → rollback Engine → Faulted

Create GL
  └─ fail → destroy GL → rollback Engine → Faulted

Create Session
  └─ fail → destroy Session → destroy GL → destroy Engine → Faulted

Start Tick
  └─ fail → stop/join pump → destroy Session → GL → Engine → Faulted
```

所有失败必须产生：

- no half-initialized Host；
- no live native handle；
- no timer/scheduler；
- no late callback；
- queryable failure stage；
- safe retry or terminal dispose policy。

当前只有局部 C# callback rollback 和 legacy `SubsystemManager` LIFO cleanup；没有统一 Engine/GL/Session/tick transaction，因此 G2P-05 PARTIAL、G2P-04 NOT READY。

---

## 8. Frozen global/static reset boundary

HRC-2-A 采用显式 **H1 Dispose → H2 Bootstrap** 策略。Terminal teardown 后必须精确验证：

```text
Application::s_Instance == nullptr
g_SubsystemAllocator == nullptr
Application::s_ConsolePanel == nullptr
GameplayAPI scene/input/handles == empty
callback/timer/scheduler count == 0
persistent GL context/carrier count == 0
FBO/texture/query count == 0
Session count == 0
Job/FileWatcher/ResourceManager/SceneManager live instance == 0
```

当前风险证据：

- Application singleton 析构不清零：`engine/src/Core/Application.cpp:37-45,90-97`；
- factory 每个实例都可能 `glfwTerminate`：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:72-75`；
- `GameplayAPI` 保存 raw scene：`engine/src/Scripting/GameplayAPI.cpp:35-49`；
- Session teardown 没有无条件 `GameplayAPI::Reset()`；
- fixed accumulator/Time/ScriptAPI process-static 状态没有完整 restart evidence。

G2P-07 当前 PARTIAL：风险边界已经识别，但 reset policy 尚未实现/验证。

---

## 9. HRC-G2P-01~08 readiness gate

### HRC-G2P-01 — Offscreen GL seam identified

**当前：PARTIAL。**

已识别可复用 FBO/GLAD/GLFW 构件和 HRC-2-0 推荐的 Host-owned offscreen context；尚无 Host-owned persistent context API、readiness result 或 CI evidence。

**升格条件：**

- 明确 native EngineHost/context owner；
- 明确 backend-independent offscreen contract；
- 明确 Session 不得创建 GL factory；
- 有 fixed-size offscreen FBO create/render/readback identity。

### HRC-G2P-02 — GL thread affinity identified

**当前：PARTIAL。**

已知 GLFW current context 依赖线程，当前代码没有 owner-thread enforcement、make/un-current protocol 或 context-loss result。

**升格条件：**

- runtime thread/affinity 冻结；
- current/un-current 边界写入 ABI/runtime contract；
- wrong-thread/cross-thread command result；
- FBO/resource delete 前 current-context assertion；
- teardown probe。

### HRC-G2P-03 — Single frame-pump owner identified

**当前：NOT READY。**

`Application::Run()` 是阻塞式 legacy loop；Avalonia 没有 production RuntimeTick pump；UI timer 不是 runtime tick。

**升格条件：**

- 一个 runtime frame owner；
- initialize/pump-one-frame/shutdown API；
- no duplicate tick source；
- start/stop/project-close/dispose join trace。

### HRC-G2P-04 — Tick/ABI contract identified

**当前：NOT READY。**

当前 RuntimeTick 是 void，EngineHost ABI 不存在，dt/status/ack/error ownership 未冻结。

**升格条件：**

- dt 单位/finite/clamp/raw-effective policy；
- typed status/stage/result；
- frame/generation/thread metadata；
- shutdown acknowledgement；
- C ABI exception boundary。

### HRC-G2P-05 — Initialization rollback defined

**当前：PARTIAL。**

HRC-1 session callback rollback 和 legacy subsystem LIFO 是局部基础；Engine/GL/Session/Tick failure 没有统一 transaction。

**升格条件：**

- 每个 init stage 有 failure result；
- no half-initialized Host；
- LIFO rollback probe；
- retry/terminal policy；
- OpenProject/GL/FBO failure injection。

### HRC-G2P-06 — Terminal teardown ordering defined

**当前：PARTIAL。**

HRC-2-0 已提出 Session→GPU→Engine→context→GLFW 顺序，legacy window/context 析构有局部顺序；没有 Host-owned teardown 或 ack。

**升格条件：**

- shutdown command/ack；
- scheduler/worker join；
- callback detach；
- GL resource/context destroy counters；
- repeated Dispose no-op；
- no late callback。

### HRC-G2P-07 — Global/static reset boundary defined

**当前：PARTIAL。**

Application、GLFW、GameplayAPI、Time、JobSystem 等风险已列出；尚无统一 reset policy 和 same-process evidence。

**升格条件：**

- global reset ownership table；
- H1 Dispose→H2 Bootstrap；
- all counters zero；
- no stale raw pointer；
- no inherited Lua/scene/callback state。

### HRC-G2P-08 — CI/headless test strategy identified

**当前：NOT READY。**

现有 hidden-window tests 是局部证据；CI 默认不启用 tests，没有 Xvfb/software GL/display matrix，也没有 HRC-2 runtime gate。

**升格条件：**

- `ENABLE_TESTS=ON`；
- Windows/Linux context backend matrix；
- Linux virtual display/software GL 或明确 platform backend；
- 不以 skip 作为 readiness 通过；
- headless context/FBO/current/un-current/rollback counters；
- same-process H1→H2 evidence。

---

## 10. Decision record

### 10.1 已确认的 HRC-2-A 决策

1. **Headless GL backend**：Hidden GLFW；不在 HRC-2-A 引入 EGL/OSMesa 或平台分支 backend。
2. **Runtime topology**：一个 dedicated runtime thread；UI 只发送 command、接收 result/event。
3. **Frame cadence**：monotonic variable dt；测试可手动 pump，不把 60Hz 作为 HRC-2-A SLA。
4. **Tick clamp**：raw dt finite/non-negative，effective clamp 固定为 `0.1s`，并暴露 raw/effective 值。
5. **FBO owner**：Host-owned native runtime；UI `ViewportPanel` 不拥有 HRC-2 render target。
6. **Reset policy**：显式 H1 Dispose → H2 Bootstrap，要求 global/static/resource counters 清零。
7. **ABI result**：统一 typed `RuntimeResult`，包含 status/stage/frame/generation/error buffer。
8. **CI/headless**：Windows 使用 Hidden GLFW；Linux 使用 Hidden GLFW + Xvfb；两平台均运行实际 Gate，不接受 skip。
9. **Implementation boundary**：HRC-2-P 只冻结上述拓扑，HRC-2-A 负责实现、测试和证据闭环。

### 10.2 HRC-2-A 前仍需确认

1. dedicated runtime thread 与 UI command queue 的具体 ABI；
2. native EngineHost handle/readiness ABI；
3. status/stage/error record 的具体 C layout；
4. tick/Stop/Shutdown acknowledgement 的时序和重复调用语义；
5. callback 是同步、排队还是强制回 UI thread；
6. late callback generation/token；
7. `glfwTerminate` 唯一 owner；
8. Application/allocator/console/GameplayAPI/Time/ScriptAPI reset policy 的实现细节；
9. CWD 是 Host 保存恢复，还是明确的 process lifetime；
10. canonical `Application.cpp` source path；
11. Phase 7/HRC-1 dirty baseline 的可复现处理方式。

---

## 11. HRC-2-P explicit non-goals

本阶段不实现：

- EngineHost/Application/native runtime ABI；
- persistent GL context；
- offscreen FBO service；
- runtime thread/frame pump；
- RuntimeTick；
- Viewport UI integration；
- readback correctness；
- resize；
- Picking；
- Gizmo；
- Renderer/RenderGraph/RHI 重构；
- ECS/test_ecs；
- ImGui migration；
- Script Reload 原子回滚；
- Inspector/Hierarchy/Asset 新功能；
- 60 FPS SLA。

---

## 12. HRC-2-A entry criteria

HRC-2-A 只有在以下条件满足后才能从 `NOT READY` 变为 `READY`：

1. HRC-G2P-01~08 全部达到 GREEN；
2. GL backend、thread affinity、current/un-current contract 明确；
3. single runtime thread/pump owner 明确；
4. tick time unit、status、error ownership、shutdown ack 明确；
5. Engine/GL/Session/Tick failure injection seam 可用；
6. global/static reset policy 完整；
7. CI/headless context strategy 可执行；
8. FBO/render target identity 唯一；
9. canonical Application source path 明确；
10. HRC-1 regression 继续 ALL GREEN；
11. Phase 7 dirty overlap 仍不被 HRC-2-A 意外清理或提交。

---

## 13. Final HRC-2-P decision

```text
HRC-2-P source audit:       COMPLETE
HRC-2-P contract:           CORE TOPOLOGY FROZEN / READY FOR HRC-2-A
HRC-G2P-01~08:              NOT GREEN
HRC-2-A implementation:    NOT READY
HRC-1 regression:          MUST REMAIN GREEN
HRC-3 viewport reality:    NOT STARTED
Avalonia UI:                PARTIAL / NOT FROZEN
```

下一步是按已确认决策进入 HRC-2-A 的原子实现与 evidence 验证；在 HRC-G2P 全部 GREEN 前，不宣称 HRC-2-A 完成。
