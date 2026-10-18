# Editor Avalonia HRC-2-0 — Runtime Bootstrap Source Audit + Contract

> **阶段**：UI-HRC-2-0（Source Audit + Runtime Contract）  
> **日期**：2026-09-24  
> **当前实现**：`NOT STARTED / BLOCKED`  
> **HRC-1 前置**：`HRC-G1 ALL GREEN`，HRC-1 Closure 已记录  
> **本阶段性质**：只读审计与契约冻结；不修改 runtime、GL、Program 或 bridge 实现

---

## 0. Executive decision

HRC-1 已经解决“谁拥有 Host”，但当前 Host 仍只是 `EditorSession` 的 managed/native facade，并不拥有 Engine、持久 GL context、render target 或 scheduler。

HRC-2-0 的结论：

```text
HRC-2-0 source audit:       COMPLETE
HRC-2 runtime contract:    PROPOSED / READY FOR REVIEW
HRC-2-A implementation:    NOT READY
HRC-G2a~g:                 NOT GREEN
Engine/GL bootstrap:       NOT IMPLEMENTED
RuntimeTick production:    NOT IMPLEMENTED
Avalonia UI:               PARTIAL / NOT FROZEN
```

本阶段冻结的核心原则：

> **唯一 Host 拥有 Engine、持久 GL runtime、render target、scheduler 和 active EditorSession；UI 只能发请求，不能创建或拥有 runtime。**

HRC-2 只验证 runtime 结构和生命周期，不验证 Viewport 像素正确性、交互或性能。

---

## 1. Baseline

### 1.1 Repository baseline

| 项目 | 值 |
|---|---|
| Branch | `avalonia` |
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| HRC-0 audit | `docs/Editor-Avalonia-HRC0-Audit.md` |
| HRC-1 closure | `docs/Editor-Avalonia-HRC1-Closure.md` |
| HRC-1 tag | 未创建，原因是 dirty Phase 7 overlap |
| HRC-G1 | `ALL GREEN` |
| HRC-2-0 code changes | 无 |

HRC-1 Closure 记录的 tracked diff 指纹：

```text
0254154b1d8b78dccc0a5f3646cc5c43841f3d91
```

该指纹不包含未跟踪文件；当前 spike、HRC 文档和 HRC-1 gate 文件仍需按 HRC-1 Closure 文档管理。

### 1.2 HRC-1 evidence inherited

HRC-2-0 不重复宣称 HRC-1 新证据，沿用 Closure 中已记录的：

- `dotnet build`：0 errors；
- `--hrc1`：ALL GREEN；
- selftest、Gate 1/2/3a/3b/3c/3d/3g：PASS；
- `EditorBridge` / `test_bridge`：PASS；
- `test_ecs`：独立 baseline，排除在 UI Gate 外。

---

## 2. Seven HRC-2 questions

| # | 问题 | 当前事实 | 当前判定 | HRC-2 必须达到 |
|---:|---|---|---|---|
| 1 | Host → Engine ownership | HRC-1 Host 只持有 `EditorSession`；Avalonia 没有 `Engine::Application` | FAIL | 一个 Host 独占 Application、GL、scheduler、Session |
| 2 | GL context/surface ownership | Session factory 只创建后销毁临时 GLAD window；没有持久 context | FAIL | Host 独占 persistent context/surface，Avalonia context 生命周期独立且可预测 |
| 3 | EditorSession lifetime | Session 在 OpenProject 时懒创建，早于 Engine/GL；CloseProject 销毁 Session | FAIL | Engine Ready 后创建 Session；Session 先销毁，Engine/GL 后销毁 |
| 4 | RuntimeTick owner/frequency | RuntimeTick production caller 为 0；ViewportTick 是 MainWindow 33ms timer | FAIL | 唯一 Host scheduler；每 frame 至多一次；真实 monotonic dt |
| 5 | Tick state machine | Host/VM/Session/Application 各自维护 bool/handle，没有统一状态机 | FAIL | 显式状态机和非法转换拒绝 |
| 6 | Failure paths | 没有 Engine/GL bootstrap；C ABI exception boundary、tick error、OpenProject rollback 不完整 | FAIL | 每个阶段可回滚，错误可观测，异常不穿 C ABI |
| 7 | Global/static state | Application singleton、GLFW、GameplayAPI、ScriptAPI 等存在重启污染风险 | FAIL | terminal teardown 后所有 global/resource counters 可验证归零 |

---

## 3. Current ownership graph

### 3.1 Actual graph

```text
AvaloniaEditor.Program.Main
  └─ using EditorHostService                 [唯一 managed Host]
      └─ App
          └─ MainWindow
              ├─ same Host reference
              └─ MainViewModel
                  └─ same Host reference
                      └─ raw EditorSession handle
                          ├─ Content/Scene/Script data
                          ├─ OpenGLGraphicsFactory
                          └─ Application::GetInstance() calls
```

证据：

- composition root：`editor_avalonia/Program.cs:10-22`；
- App injection：`editor_avalonia/App.axaml.cs:11-28`；
- Window/VM injection：`editor_avalonia/MainWindow.axaml.cs:21-35`、`editor_avalonia/ViewModels/MainViewModel.cs:15-18,434-438`；
- Host 当前只拥有 session handle/callback/state：`editor_avalonia/Services/EditorHostService.cs:11-25`；
- C ABI 没有 Engine host create/bootstrap ABI：`bridge/include/editor_bridge/capi.h:57-66`。

### 3.2 HRC-1 established boundary

已冻结并通过 HRC-G1：

- composition root 只有一个 Host；
- Window 与 VM 共享同一 Host；
- Session 由 Host facade 创建；
- `CloseProject` 与 terminal `Dispose` 分离；
- terminal disposed Host 拒绝重建。

这些边界在 HRC-2 中必须保持，不得重新引入第二 Host、第二 Application 或第二 scheduler。

### 3.3 HRC-2 missing ownership layer

当前没有：

```text
EditorHostService
  └─ native EngineHost
      ├─ Engine::Application
      ├─ persistent GL context carrier
      ├─ render target/FBO owner
      ├─ frame scheduler
      └─ active EditorSession
```

`EditorSession` 当前按值拥有 `OpenGLGraphicsFactory`：`bridge/src/EditorSession.h:198-200`。这使 project session 仍然拥有 graphics/GL factory 生命周期，违反 HRC-2 的 runtime ownership 目标。

---

## 4. A. Engine ownership

### 4.1 Current Engine path

`Engine::Application` 只在 legacy sandbox 中被构造：

```cpp
Engine::OpenGLGraphicsFactory factory;
{
    Engine::EditorDemoApp app(factory);
    app.Run();
}
```

证据：`sandbox/src/EditorDemo/main.cpp:37-46`。

Avalonia 进程没有等价的：

- Application constructor；
- `Application::Run()` 或可拆分 bootstrap/pump；
- Application readiness/status；
- Avalonia-facing runtime handle。

### 4.2 Application lifecycle facts

`Application` 构造时立即：

- 设置 `Application::s_Instance`；
- 注册 Window、Camera、UI、ResourceManager、FileWatcher、Shader、Texture、VertexData subsystem。

证据：`engine/src/Core/Application.cpp:38-88`。

`Application::Run()` 负责：

1. 初始化日志、allocator、CrashHandler；
2. 初始化 SubsystemManager、Time、Profiler、JobSystem、SceneManager；
3. 调用 `OnStartup()`；
4. 初始化 viewport FBO；
5. 进入 window-driven loop；
6. 退出时关闭 SceneManager、UIManager、JobSystem。

证据：`engine/src/Core/Application.cpp:521-819`。

`Application::Run()` 是阻塞式自有 GLFW window loop，不能直接放入 Avalonia UI thread。HRC-2 必须将其依赖拆成可由 Host 驱动的初始化、单帧 pump 和 shutdown 阶段，或提供等价的非阻塞 runtime service。

### 4.3 HRC-2 Engine contract

- `EditorHostService`/其 native runtime facade 是唯一 Engine owner；
- Window、VM、ViewportPanel 不得构造 Application；
- Host 必须在 GL/Engine Ready 后才允许创建 Session；
- Engine teardown 必须在 Session、scheduler、GPU resource 之后；
- `Application::GetInstance()` 不得作为未声明的 owner discovery；
- `Application::s_Instance` 在 terminal teardown 时必须清零。

---

## 5. B. GL context and surface ownership

### 5.1 Three independent rendering lifetimes

| 层 | 当前 owner/context | 当前状态 |
|---|---|---|
| Avalonia | desktop lifetime / platform backend | 自己的 native/rendering lifetime；没有 Engine context 接入 |
| legacy Engine | Application → GlfwWindow → OpenGLContext | 独立 GLFW window/default framebuffer |
| EditorSession | Session-owned OpenGLGraphicsFactory | 临时 GLAD loader window，创建后立即销毁 |

### 5.2 Session factory is not a persistent runtime

`EditorSession` 默认构造会构造 `OpenGLGraphicsFactory`：`bridge/src/EditorSession.h:198-200`。

factory constructor：

1. `glfwInit()`；
2. 创建隐藏 1×1 GLFW window；
3. make current；
4. 加载 GLAD；
5. 立即销毁 window。

证据：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:31-70`。

factory destructor 无条件 `glfwTerminate()`：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:72-75`。

这不是 persistent context，也没有明确的 current-context 生命周期。GLFW init/terminate 也没有 owner/refcount。

### 5.3 Avalonia rendering boundary

当前 Avalonia 只使用自己的 platform backend；Viewport 是 CPU `WriteableBitmap`：

- XAML：`editor_avalonia/MainWindow.axaml:71-96`；
- bitmap copy：`editor_avalonia/MainWindow.axaml.cs:359-385`。

当前没有：

- Avalonia native HWND 与 Engine GL context 共享；
- share list；
- NativeControlHost；
- GL texture interop；
- Avalonia/Engine context affinity 协议。

### 5.4 Proposed minimal HRC-2 GL strategy

HRC-2 推荐冻结为：

> **Host-owned offscreen persistent OpenGL context；Avalonia 保留自己的 native rendering context；HRC-2 不做 GL interop。**

结构：

```text
EditorHostService
  └─ native EngineHost
      ├─ persistent hidden GLFW context carrier
      ├─ Engine Application / render target
      └─ session/runtime registration
```

HRC-2 只要求 context/render-target identity 和生命周期正确，不要求像素显示、GPU texture sharing 或 viewport 交互。

如果未来选择 Avalonia context sharing，必须先新增 backend matrix、native handle 获取、share list、current-context 和 teardown 契约，不能在实现中隐式决定。

### 5.5 HRC-2 GL rules

- 只有 Host/graphics owner 可以 `glfwInit`/`glfwTerminate`；
- Session create/destroy 不得创建或终止 GLFW；
- CloseProject 不得销毁 persistent context；
- terminal Dispose 必须最后执行 `glfwTerminate`；
- GL resource delete 前 owning context 必须有效；
- `m_RenderContext` 等 raw pointer 在 context/window teardown 时清零；
- current-context loss 必须可检测并进入 Faulted；
- Session 不得再通过 `Application::GetInstance()` 获取 graphics owner。

---

## 6. C. EditorSession lifetime

### 6.1 Current order

```text
Program creates managed Host
  → App/Window/VM receive Host
  → OpenProject
      → Host.EnsureSession
          → EditorSession_Create
          → SetEventCallback
      → EditorSession_OpenProject
  → authoring/runtime operations
```

Session 当前在 OpenProject 时懒创建，而不是在 runtime bootstrap 完成后由 Host 创建。

### 6.2 Current states are split

| 状态 | 当前来源 |
|---|---|
| native session exists | Host `_session != 0` |
| native project open | Host `IsOpen` |
| VM project open | `Entities.Count > 0` |
| runtime playing | native `m_Playing` / VM `IsRunning` |
| Engine playing | `Application::m_IsPlaying` |

VM `IsOpen` 仅依据实体数量，空工程会产生状态分裂。HRC-2 必须以 Host state 为唯一事实源。

### 6.3 HRC-1 CloseProject semantics

`CloseProject()` 当前销毁整个 native Session，但保留 managed Host：

```text
Host.CloseProject
  → detach callback
  → EditorSession_Destroy
  → Stop
  → delete Session
  → Host remains reusable
```

证据：`editor_avalonia/Services/EditorHostService.cs:569-591`、`bridge/src/capi.cpp:29-34`。

这解决的是 project/session 级关闭，不是 Engine/GL/tick teardown。HRC-2 必须继承此语义：

- CloseProject：停止 project activity，保留 Engine/GL/Host；
- terminal Dispose：停止完整 runtime，销毁 Engine/GL/Session/Host。

### 6.4 HRC-2 Session contract

```text
Engine/GL Ready
  → Host creates one EditorSession
  → callback registration succeeds
  → OpenProject
  → Play
  → RuntimeTick
  → CloseProject
      → Stop runtime
      → reset GameplayAPI
      → destroy Session
      → retain Engine/GL/Host
```

- Engine 未 Ready 时 Session create 必须失败且不留下 handle；
- Session 不得自行创建 graphics factory；
- Session 不得通过 global singleton 查找 owner；
- failed OpenProject 不能留下悬垂 GameplayAPI scene；
- active session 每进程最多一个；
- session/runtime scene/render target 必须有同一 generation identity。

---

## 7. D. RuntimeTick and ViewportTick

### 7.1 RuntimeTick current chain

```text
EditorHostService.RuntimeTick
  → EditorBridgeApi.EditorSession_RuntimeTick
  → C ABI
  → EditorSession::RuntimeTick
  → ScriptInstance::OnUpdate
```

证据：

- wrapper：`editor_avalonia/Services/EditorHostService.cs:259-263`；
- P/Invoke：`editor_avalonia/Native/EditorBridgeApi.cs:144-145`；
- C ABI：`bridge/src/capi.cpp:269-272`；
- native：`bridge/src/EditorSession.cpp:746-751`。

Production caller 数为 **0**。唯一实际调用是测试中的单帧手动 tick：`tests/test_bridge/test_editor_capi.cpp:612-624`。

### 7.2 ViewportTick is not RuntimeTick

当前唯一 production ViewportTick caller 是 MainWindow 的 33ms timer：

- timer：`editor_avalonia/MainWindow.axaml.cs:42-48`；
- call：`editor_avalonia/MainWindow.axaml.cs:344-353`；
- native action：`bridge/src/EditorSession.cpp:856-873`。

它只 flush camera input/update，不 render，也不推进 `EditorSession::RuntimeTick`。不能把该 timer 改名为 RuntimeTick。

### 7.3 HRC-2 tick contract

- owner：唯一 Host scheduler；
- frequency：每 Host frame 最多一次；
- target：60Hz best effort，不承诺性能 SLA；
- dt：monotonic elapsed time；
- dt 必须 finite，建议 clamp 到 `0.1s`；
- 非 Playing 不调用 RuntimeTick；
- Stop/Close/Dispose 前先 stop/join scheduler；
- Viewport refresh/render/present 是 frame 的后续阶段，不能反向驱动 gameplay；
- tick 不进入通用 JobSystem worker；
- 记录 frame id、tick count、last dt、owner thread、session generation。

### 7.4 Tick error contract

- Lua error：记录 diagnostic，进入 `RuntimeFaulted`，停止继续 script tick，但允许诊断和 teardown；
- native/infrastructure exception：Host 进入 Faulted 并停止 scheduler；
- C++ exception 不得穿越 C ABI；
- C# callback exception 不得返回 native；
- fault 必须带 stage、frame id、session generation；
- 不得只记录日志后继续盲目 tick。

---

## 8. E. Runtime state machine

当前没有统一状态机；状态分散在 Host、Session、VM 和 Application。

HRC-2 建议冻结：

```text
Created
  → Bootstrapping
  → Ready
  → OpeningProject
  → ProjectOpen
  → Playing
  → RuntimeFaulted
  → Stopping
  → ProjectOpen
  → ClosingProject
  → Ready
  → Disposing
  → Disposed
```

规则：

- 所有转换只由 Host 执行；
- `RuntimeFaulted` 只允许诊断、Stop、Dispose；
- `Disposed` terminal；
- 非法转换返回稳定 status/diagnostic；
- UI state 从 Host state 派生，不再从 `Entities.Count` 推断；
- CloseProject 和 terminal Dispose 保持 HRC-1 已建立的语义区分。

---

## 9. F. Failure and teardown paths

### 9.1 Current managed strengths

HRC-1 已提供：

- ABI version check；
- null session check；
- callback registration rollback；
- terminal Dispose 幂等；
- disposed Host 拒绝重建。

证据：`editor_avalonia/Services/EditorHostService.cs:22-52,569-618`。

### 9.2 Current gaps

| Failure point | 当前风险 |
|---|---|
| Engine bootstrap | 不存在，无法回滚 |
| GLFW/context/GLAD | factory 只 log，Session 仍可能创建成功 |
| Session constructor | `new(nothrow)` 不能隔离成员/C++ 异常 |
| OpenProject instantiate | 可能留下 GameplayAPI 指向已销毁 local scene |
| RuntimeTick | Lua error 不进入 `m_RuntimeError`，Playing 继续 |
| C ABI | exception boundary 不存在 |
| callback | subscriber exception 可返回 native |
| CloseProject | 无 scheduler/GL stop/join |
| Window close | 依赖 OnClosed/using，不是 bootstrap cancellation gate |
| Host Dispose | 只终止 session，不终止 Engine/GL/scheduler |

### 9.3 HRC-2 teardown order

CloseProject：

```text
reject new commands
  → stop/join scheduler for project
  → detach callback
  → Stop runtime
  → OnDestroy exactly once
  → GameplayAPI::Reset
  → destroy runtime/edit session data
  → destroy EditorSession
  → retain Engine/GL/Host
  → Ready
```

Terminal Dispose：

```text
stop UI callbacks and commands
  → stop/join scheduler
  → detach callbacks
  → Stop runtime
  → destroy Session
  → reset Gameplay/script globals
  → destroy FBO/readback/GPU resources
  → shutdown Engine subsystems/jobs/UI
  → clear Application singleton/raw globals
  → release GL resources
  → destroy context carrier/window
  → glfwTerminate exactly once
  → Host Disposed
```

所有 GL resource 必须在 owning context 仍有效时销毁；`glfwTerminate` 必须是最后一步。

---

## 10. G. Global/static state contract

Terminal teardown 后必须可验证：

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

重点风险：

- `Application::s_Instance` 当前析构不清零：`engine/src/Core/Application.cpp:38-45,90-97`；
- `OpenGLGraphicsFactory` 每个实例都可能 `glfwTerminate`：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:72-75`；
- `GameplayAPI` 保存 raw `Scene*`：`engine/src/Scripting/GameplayAPI.cpp:35-49`；
- Session teardown 没有无条件最终 `GameplayAPI::Reset()`；
- `ScriptInstance::Stop` 当前可能双调用 `OnDestroy`：`bridge/src/EditorSession.cpp:731-739`、`engine/src/Scripting/ScriptInstance.cpp:74-79`；
- `Time`、fixed accumulator、ScriptAPI HUD/time 等 process-static 状态需要明确 reset 或 process-lifetime 策略。

HRC-2-G2g 必须区分：

1. 同一 Host CloseProject → Open；
2. terminal Host dispose → 新 Host；
3. 同一进程 H1 → H2；
4. failure → rollback → recovery。

不能只用冷进程重启掩盖 teardown 问题。

---

## 11. Proposed HRC-2 contract

以下为 HRC-2-0 冻结候选，需在 HRC-2-A 入口确认后生效。

### H2-01 — Single owner

- 每进程一个 `EditorHostService`；
- Host 独占 Engine、graphics/platform owner、persistent GL context、render target、scheduler、active Session；
- UI 不得创建或销毁上述对象。

### H2-02 — Minimal GL strategy

- HRC-2 使用 Host-owned offscreen persistent OpenGL context；
- Avalonia 保留自己的 rendering context；
- HRC-2 不做 GL interop/NativeControlHost/texture sharing；
- 所有 GL 操作有明确 current-context/affinity 规则。

### H2-03 — Single-thread minimum

- bootstrap、GL、Session、RuntimeTick 固定在 Host/UI 创建线程；
- 不在 UI 线程调用阻塞式 `Application::Run()`；
- runtime 必须可拆为 initialize/pump/shutdown；
- render thread 另立契约，不隐式引入。

### H2-04 — State machine

- 使用第 8 节状态机；
- Host 是唯一 transition owner；
- illegal transition 返回稳定 status；
- UI 状态从 Host 派生。

### H2-05 — Bootstrap order

```text
Host
  → Log/Crash/Time/global services
  → one graphics/platform owner
  → persistent GL context
  → Engine Application/subsystems
  → render target/FBO
  → scheduler idle
  → one EditorSession
  → Ready
```

Session 不得在 Engine/GL Ready 前存在。

### H2-06 — Session dependency

- Session 不再按值拥有 `OpenGLGraphicsFactory`；
- Session 通过 Host 获得 runtime/render registration；
- Session 不通过 `Application::GetInstance()` 查找 owner；
- session/runtime scene/tick/render target 共享同一 generation identity；
- 每进程 active session 最多一个。

### H2-07 — Tick

- Host scheduler 是唯一 RuntimeTick owner；
- 每 frame 最多一次；
- 仅 Playing 调用；
- monotonic dt，finite/clamp；
- Stop/Close/Dispose 先 stop/join；
- Viewport refresh 不驱动 gameplay。

### H2-08 — Error boundary

- C ABI 每个可能失败的入口隔离 C++ exception；
- tick/stop/bootstrap 使用明确 status/error stage；
- C# callback 不把 exception 返回 native；
- callback detach 后禁止 late callback；
- Lua error 和 native fault 有不同 policy。

### H2-09 — CloseProject

- 停止 project scheduler/tick；
- runtime teardown 与 GameplayAPI reset；
- Session 销毁；
- Engine/GL/Host 保留；
- 允许同一 Host 再次 Open。

### H2-10 — Terminal Dispose

- 停止所有 UI/tick/render；
- Session → Engine GPU resources → Engine subsystems → context → GLFW；
- singleton/global reset；
- Dispose 幂等；
- Dispose 后所有 runtime command 稳定拒绝。

### H2-11 — Global reset

- Application singleton、allocator、console panel 清零；
- GameplayAPI/ScriptAPI 状态按策略清零；
- 所有 worker/timer/callback 计数归零；
- factory raw context pointer 不保留；
- 同进程 H1→H2 无隐式继承。

### H2-12 — Viewport identity boundary

HRC-2 只冻结 identity，不要求像素正确：

```text
one runtime scene
  → one registered render target
  → one FBO/readback owner
  → one resize path
  → one frame sequence
```

必须消除当前 `Application::m_ViewportPanel` 与 `EngineEditor::m_Viewport` 的未登记 split。

---

## 12. HRC-G2a~g

### HRC-G2a — Engine initialized exactly once

必须证明：

- Program 仍只有一个 Host；
- Window/VM 共享同一 Host；
- native EngineHost/Application 只创建一次；
- Session 只能由该 EngineHost 创建；
- Window/VM/Gate 不创建第二 runtime；
- close/dispose 后重建请求按状态拒绝。

当前：**PARTIAL，HRC-1 managed ownership PASS，Engine ownership NOT CONNECTED。**

### HRC-G2b — Engine bootstrap transaction

覆盖 GLFW、persistent context/window、GLAD、Application subsystem、FBO、JobSystem/SceneManager、bootstrap 中 Window close。

通过条件：

- 有 `Bootstrapping → Ready` 或 `Faulted`；
- 失败无半初始化 context/window/timer/session；
- 已成功资源按 LIFO rollback；
- failure stage/error 可查询；
- 不以程序启动成功代替 bootstrap evidence。

当前：**NOT IMPLEMENTED。**

### HRC-G2c — Persistent GL ownership

必须证明：

- exactly one process graphics owner；
- exactly one persistent context carrier；
- Session create/destroy 不调用 glfwInit/glfwTerminate；
- CloseProject 后 context 仍有效；
- terminal 后 context/window/FBO 全部销毁；
- current-context rule 明确；
- raw context pointer 不悬空。

不要求像素正确。

当前：**FAIL，只有 temporary loader。**

### HRC-G2d — Session attached to same runtime

执行：

```text
Engine Ready
  → Session Create
  → callback register
  → OpenProject
  → Play
```

必须证明：

- Engine 未 Ready 时 Session create 失败且无 handle；
- callback failure 可回滚；
- Session 不通过 global singleton 获取 owner；
- runtime scene/tick/render registration identity 一致；
- OpenProject failure 不留下悬垂 GameplayAPI scene；
- active session 不超过一个。

当前：**FAIL。**

### HRC-G2e — One production RuntimeTick owner

使用 frame counter/last dt probe：

- 非 Playing tick count=0；
- Play 后每 Host frame 至多一次；
- Stop/Close/Dispose 后不再增长；
- dt 非固定伪值；
- Viewport timer 不改变 RuntimeTick 次数；
- Lua error 进入 Faulted 并停止继续 tick；
- native exception 不穿 ABI；
- 记录 frame id/tick count/owner thread。

当前：**FAIL，production caller=0。**

### HRC-G2f — State and teardown

分别执行：

1. Ready→CloseProject→Open；
2. ProjectOpen→CloseProject；
3. Playing→CloseProject；
4. RuntimeFaulted→CloseProject；
5. Window close；
6. Host Dispose；
7. repeated Dispose；
8. CloseProject/Window close race。

通过条件：

- CloseProject 保留 Engine/GL；
- terminal Dispose 销毁完整 runtime；
- callback/timer/tick/session/GL counters 归零；
- state trace 符合 H2-04/H2-09/H2-10；
- 不依赖 managed finalizer。

当前：**仅 HRC-1 session split PASS，其余 NOT REACHED。**

### HRC-G2g — Failure rollback and same-process restart

在同一进程连续运行：

```text
H1 Bootstrap → Open → Play → Tick → CloseProject
H1 Dispose
H2 Bootstrap → Open → Play → Tick → CloseProject
H2 Dispose
```

并覆盖 failure→rollback→recovery。

必须具备可观测计数：

- Host/Application/GLFW/GL/Session/Callback/Tick/Global。

通过条件：

- clean baseline；
- fault rollback；
- CloseProject recovery；
- Window close；
- Host Dispose；
- same-process H1→H2；
- all counters balanced；
- no late callback；
- no UAF/double-free/leak；
- no forced process exit。

当前：**NOT REACHED。**

---

## 13. HRC-2 explicit non-goals

本阶段不实现：

- 第二个 Host/Application/GL context/scheduler；
- 多 active Session、多 world、多 project runtime；
- Avalonia native GL interop、NativeControlHost、GPU texture sharing；
- Viewport 像素正确性、resize 视觉正确性；
- Picking、Gizmo、Viewport interaction；
- Render thread（除非另立线程契约）；
- Renderer/RenderGraph/RHI 重构；
- ECS/test_ecs；
- ImGui migration；
- Physics、ContentRegistry、SceneSerializerV1 格式重构；
- Script Reload 原子回滚、Lua sandbox 重设计；
- Inspector/Hierarchy/Asset 新功能；
- 60 FPS 性能 SLA。

---

## 14. HRC-2-A implementation entry criteria

HRC-2-A 当前状态：**NOT READY**。

开始实现前必须确认：

1. HRC-1 ownership boundary 保持不变，HRC-G1 继续 green；
2. 接受当前 dirty fingerprint，或建立独立可复现 baseline；
3. 确认 H2-02 offscreen persistent GL 策略；
4. 确认 H2-03 单线程非阻塞 frame pump 策略；
5. 冻结 tick owner、频率、dt、clamp、error policy；
6. 冻结 Host/Session state machine 与非法转换 status；
7. 设计 native EngineHost ABI/readiness；
8. 设计 callback register/detach、late callback 规则；
9. 设计 Engine/GL/Session/Tick failure injection seam；
10. 列出所有 global/static reset policy；
11. 选定唯一 canonical `Application.cpp` source path；
12. 为 G2a~g 定义 setup/action/observable/cleanup/evidence；
13. G2g 必须支持同进程 H1→H2，不得只用冷进程重启。

当前未满足项：

```text
GL ownership decision:    not frozen
thread model decision:    not frozen
tick/dt/error decision:   not frozen
ABI/readiness design:     not frozen
failure injection seam:   not designed
global reset policy:      not frozen
canonical CMake source:   not frozen
HRC-2-A entry:             NOT READY
```

---

## 15. Final HRC-2-0 decision

```text
HRC-2-0 audit:             COMPLETE
HRC-2 contract:           PROPOSED / READY FOR REVIEW
HRC-2-A implementation:   NOT STARTED
HRC-G2a~g:                NOT GREEN
HRC-1 regression:         MUST REMAIN GREEN
HRC-3 viewport reality:   NOT STARTED
Avalonia UI:              PARTIAL / NOT FROZEN
```

下一步不是修改 `Program.cs` 或 GL 初始化，而是由负责人确认 H2-02～H2-12 的 runtime 决策；确认后再进入 **HRC-2-A Runtime Bootstrap implementation**。
