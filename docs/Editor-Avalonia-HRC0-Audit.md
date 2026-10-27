# Editor Avalonia Host/Runtime Closure — HRC-0 Source Audit & Contract

> **阶段**：UI-HRC-0（Source Audit + Contract）  
> **日期**：2026-09-24  
> **状态**：`PARTIAL / NOT FROZEN`  
> **审计性质**：只读源码审计；本阶段不修改 C++/C# 实现，不提交代码。

---

## 0. Baseline

### 0.1 Repository identity

| 项目 | 值 |
|---|---|
| Branch | `avalonia` |
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| `git describe` | `engine-foundation-v1-f2-b-collider-dirty` |
| HEAD message | `docs(engine): F2-B closure — evidence frozen (Result A) + baseline 212 + Golden Gate` |
| Audit scope | 当前工作树，而不是仅 HEAD |
| Working tree | 已有未提交 Phase 7/Bridge/Editor 相关修改；本文件创建前未修改源码 |

HRC-0 的结论必须以当前工作树为准。Phase 7 改动尚未形成可复现的 clean-commit baseline，因此不能把 HEAD 的历史绿色证据当作当前 Viewport 证据。

### 0.2 Test baseline

`docs/Evidence-Baseline.md:10-29` 记录的冻结基线为 212 tests，且明确将 `test_ecs` 排除在基线之外。

当前工作区已取得的构建证据：

| 检查 | 结果 | 说明 |
|---|---|---|
| `dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore` | PASS | 0 warning / 0 error；只证明 C# 外壳可编译 |
| `cmake --build build --config Debug --target EditorBridge test_bridge` | PASS | Bridge 与 Bridge tests 编译成功 |
| Avalonia Gate 1/2/3a/3b/3c/3d/3g | PASS | 现有 authoring/runtime contract gates 全绿 |
| `ctest --test-dir build -C Debug --output-on-failure` | PARTIAL | 9/10 通过；`test_ecs` 未运行 |
| `cmake --build build --config Debug --target test_ecs` | FAIL | `ComponentLifecycleTest.cpp` 使用未定义的 `Position` / `Velocity`；与 UI-HRC-0 独立处理 |

现有 Gate 主要直接构造 `MainViewModel` 或调用 Bridge contract，不能替代 `Program → MainWindow → Host → Engine/GL → Viewport` 的真实运行证据。

### 0.3 Baseline verdict

- Avalonia authoring shell：主路径基本存在。
- Avalonia Host/Runtime graph：**未建立**。
- Avalonia Viewport：**不可验收**。
- UI 总体验收：`PARTIAL / NOT FROZEN`。
- `test_ecs`：独立的 ECS baseline/infrastructure issue，HRC-0 不触碰。

---

## 1. Audit scope and method

### 1.1 Included paths

```text
editor_avalonia/Program.cs
editor_avalonia/App.axaml.cs
editor_avalonia/MainWindow.axaml.cs
editor_avalonia/MainWindow.axaml
editor_avalonia/ViewModels/MainViewModel.cs
editor_avalonia/Services/EditorHostService.cs
editor_avalonia/Native/EditorBridgeApi.cs
bridge/include/editor_bridge/capi.h
bridge/src/EditorSession.h
bridge/src/EditorSession.cpp
bridge/src/capi.cpp
engine/include/Engine/Application.h
engine/src/Core/Application.cpp
engine/src/OpenGL/OpenGLGraphicsFactory.cpp
engine/src/OpenGL/OpenGLContext.cpp
engine/src/Platform/GlfwWindow.cpp
engine/src/Editor/EngineEditor.cpp
engine/src/Editor/ViewportPanel.cpp
engine/src/Scripting/GameplayAPI.cpp
engine/src/Scripting/ScriptInstance.cpp
sandbox/src/EditorDemo/main.cpp
```

### 1.2 Method

1. 追踪所有 `EditorHostService`、`EditorSession`、`Application`、`OpenGLGraphicsFactory` 和 `RuntimeTick` 的创建、调用与销毁点。
2. 区分 legacy sandbox 的 Engine/GL 生命周期与 Avalonia 进程的 Bridge 生命周期。
3. 对每个 ownership、lifecycle、tick、failure/disposal 问题给出当前事实和目标契约。
4. HRC-0 只记录事实和契约，不修改实现，不修复 `test_ecs`，不引入新的 Viewport 功能。

---

## 2. Executive answer: the eight HRC-0 questions

| # | 问题 | 当前答案 | 当前判定 | HRC-0 冻结目标 |
|---:|---|---|---|---|
| 1 | Host 的唯一 owner 是谁？ | 没有唯一 owner；`MainWindow` 与 `MainViewModel` 各自创建 `EditorHostService` | FAIL | 每进程唯一 `EngineHost` owner |
| 2 | Engine 创建/销毁由谁负责？ | Avalonia 进程没有创建或运行 `Engine::Application`；只有 sandbox `main` 创建 Application | FAIL | Host 负责 Engine 的完整 create/run/destroy |
| 3 | GL context / render surface 由谁负责？ | `EditorSession` 内的 factory 只创建临时 GLAD loader window；Avalonia 没有持久 context/surface | FAIL | Host 独占持久 context、surface、FBO 和 render loop |
| 4 | MainWindow 是否只消费 Host？ | 否；`MainWindow` 直接创建并调用 Host A | FAIL | MainWindow 只消费注入的 Host 接口 |
| 5 | MainViewModel 是否只持有同一 Host/Session？ | 否；VM 自己创建 Host B | FAIL | VM 与 Window 共享同一 runtime/session |
| 6 | EditorSession 是否严格包含于 Engine/GL 生命周期？ | 否；Session 懒创建，隐式读取全局 `Application`，没有生命周期包含关系 | FAIL | Session 不得活得比 Host/Engine/GL 更久 |
| 7 | RuntimeTick 的 owner、频率、停止条件？ | ABI 和 wrapper 存在，但生产 Avalonia 调用者为 0；只有测试手动 tick | FAIL | 单一 scheduler owner；每 engine frame 一次；Stop/Dispose 前停止 |
| 8 | close/reload/exception 是否安全？ | 没有 App `OnExit`/Window close teardown；存在全局 scene、singleton、callback 和双 OnDestroy 风险 | FAIL | 单一可回滚 teardown，所有异常有边界 |

---

## 3. A. Current Host graph

### 3.1 Actual Avalonia startup graph

```text
AvaloniaEditor.Program.Main
  └─ BuildAvaloniaApp().StartWithClassicDesktopLifetime(args)
      └─ App.OnFrameworkInitializationCompleted
          └─ desktop.MainWindow = new MainWindow()
              ├─ MainWindow._host = new EditorHostService       [Host A]
              ├─ MainWindow.Vm = new MainViewModel
              │   └─ MainViewModel._host = new EditorHostService [Host B]
              │       └─ subscribe NativeEvent
              └─ DispatcherTimer(33 ms)
                  └─ OnViewportTimerTick
                      └─ MainWindow._host.ViewportTick
```

证据：

- Avalonia 入口没有 Engine/Host bootstrap：`editor_avalonia/Program.cs:8-20`。
- App 只创建 Window：`editor_avalonia/App.axaml.cs:11-16`。
- Window 和 VM 各自创建 Host：`editor_avalonia/MainWindow.axaml.cs:19-22`、`editor_avalonia/ViewModels/MainViewModel.cs:11-15`。
- timer 在构造时直接启动：`editor_avalonia/MainWindow.axaml.cs:36-42`。

### 3.2 Actual project-open graph

```text
Open Project
  └─ MainWindow.OnOpenProject
      ├─ Vm.OpenProject(...)
      │   └─ VM Host B.EnsureSession()
      │       └─ EditorSession_Create()
      └─ MainWindow Host A.SetCameraExternalInput(true)
          └─ _session == 0，静默 no-op
```

证据：`editor_avalonia/MainWindow.axaml.cs:115-126`、`editor_avalonia/ViewModels/MainViewModel.cs:438-451`。

### 3.3 Actual viewport timer failure path

```text
VM Host B 成功打开项目
  → Vm.IsOpen == true
  → MainWindow timer tick
  → Host A.ViewportTick()
  → Host A._session == 0
  → ThrowIfNoSession()
  → InvalidOperationException
```

`ViewportTick` 的无 session 抛出证据：`editor_avalonia/Services/EditorHostService.cs:366-369,544-547`；调用方没有 try/catch：`editor_avalonia/MainWindow.axaml.cs:330-339`。

### 3.4 Native boundary graph

当前 C# P/Invoke 面全部指向 `EditorBridge`：

```text
C# UI
  └─ EditorBridgeApi (EditorBridge.dll)
      └─ C ABI
          └─ EditorSession
              ├─ ContentRegistry / Scene / bindings
              ├─ ScriptInstance / GameplayAPI globals
              └─ Application::GetInstance() for Phase 7 paths
```

没有 Application create/run/destroy ABI，也没有 Avalonia 可调用的 GL context/render surface ABI。证据：`editor_avalonia/Native/EditorBridgeApi.cs:37-297`、`bridge/include/editor_bridge/capi.h:57-66`、`bridge/CMakeLists.txt:6-14`。

### 3.5 Ownership table

| 对象 | 当前实际持有 | 当前终止路径 | 判定 |
|---|---|---|---|
| `App` | Avalonia lifetime / MainWindow | 无 `OnExit` teardown | 不完整 |
| `MainWindow` | Host A、VM、timer、bitmap | 无 Dispose/Close handler | 不完整 |
| `MainViewModel` | Host B | 有 `Dispose()`，但 Window 不调用 | 实际 session owner |
| `EditorHostService` | raw native handle + callback | `Dispose()` 可重复调用且可再次 Create | Reset/terminal 语义混合 |
| `EditorSession` | edit/runtime scene、assets、script、GL factory | C ABI Stop + delete | 依赖全局状态 |
| `Engine::Application` | sandbox 中存在，Avalonia 中不存在 | sandbox 作用域析构 | 未接入 UI |

---

## 4. B. Engine lifecycle

### 4.1 Current Engine lifecycle exists only in the legacy path

`Application` 构造时：

- 设置全局 `Application::s_Instance`；
- 注册 Window、Camera、UI、ResourceManager、FileWatcher、Shader、Texture、VertexData 子系统。

证据：`engine/src/Core/Application.cpp:38-88`。

`Application::Run()` 负责：

1. 初始化日志、allocator、CrashHandler；
2. 初始化 SubsystemManager、Time、Profiler、JobSystem、SceneManager；
3. 调用 `OnStartup()`；
4. 初始化 viewport FBO 与 resize callback；
5. 在 `!m_Window->ShouldClose()` 循环中执行 update/UI/render/swap；
6. 退出时关闭 SceneManager、UIManager、JobSystem。

证据：`engine/src/Core/Application.cpp:521-819`。

`SubsystemManager::Shutdown()` 按成功初始化顺序逆序清理：`engine/src/Core/SubsystemManager.cpp:98-116`。

### 4.2 Avalonia 没有 Engine lifecycle

当前 `Program.Main` 只启动 Avalonia lifetime；没有任何 C# 或 C ABI 路径创建 `Engine::Application` 并调用 `Run()`。因此以下对象在 Avalonia 进程中不存在：

```text
Application
  └─ persistent GLFW window
      └─ OpenGLContext
          └─ current GL context
              └─ render loop / frame pump
```

`EditorSession` 只是在实现文件中 include `Engine/Application.h` 并通过 `Application::GetInstance()` 查找全局对象；这不是 ownership。证据：`bridge/src/EditorSession.cpp:9-17,708-740,826-1059`。

### 4.3 Singleton lifetime defect

`Application::s_Instance` 在构造函数中赋值，但 `Application::~Application()` 没有清空它。证据：`engine/src/Core/Application.cpp:38-45,90-97`。

因此若未来把 Application 接入 Avalonia，必须保证：

```text
Application fully initialized
  → EditorSession created and used
  → EditorSession destroyed
  → Application destroyed
  → s_Instance = nullptr
```

当前没有这样的显式顺序，也没有防止 session 比 Application 活得更久的机制。

### 4.4 HRC-0 Engine contract

- `EngineHost` 是唯一 Application owner。
- `EditorSession` 不得自行创建第二 Application。
- `EditorSession` 只能使用已经完成初始化的 Host runtime。
- Application teardown 之前必须先停止所有 session callbacks、runtime tick 和 render loop。
- Application 析构时必须使全局 singleton 失效；任何晚到的 session 调用必须安全失败而不是解引用悬垂对象。

---

## 5. C. GL context and render-surface lifecycle

### 5.1 EditorSession 内的临时 GL loader context

`EditorSession` 按值拥有一个 `OpenGLGraphicsFactory` 和 `TextureManager`：`bridge/src/EditorSession.h:198-231`。

`OpenGLGraphicsFactory` 构造函数会：

1. `glfwInit()`；
2. 创建一个隐藏的 1×1 GLFW window；
3. `glfwMakeContextCurrent()`；
4. 加载 GLAD；
5. 立即 `glfwDestroyWindow(tempWindow)`。

证据：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:31-69`。

factory 析构时调用 `glfwTerminate()`：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:72-75`。

这不是持久 render context。当前 Avalonia 进程因此没有：

- persistent GLFW window；
- persistent OpenGLContext；
- current-context 保证；
- 默认 framebuffer render surface；
- editor FBO；
- Avalonia GL interop surface。

### 5.2 Engine 的正常持久 context 创建链

`Application::InitWindow()` 调用 factory 的 `CreateWindow()`：`engine/src/Core/Application.cpp:106-113`。

`OpenGLGraphicsFactory::CreateWindow()` 执行：

```text
glfwCreateWindow
  → CreateRenderContext
  → glfwMakeContextCurrent
  → gladLoadGLContext
  → OpenGLContext::Init
  → GlfwWindow owns native window + context
```

证据：`engine/src/OpenGL/OpenGLGraphicsFactory.cpp:78-125`。

该链只在 legacy sandbox `EditorDemoApp` 路径中由 `sandbox/src/EditorDemo/main.cpp:37-46` 创建 Application 并调用 `app.Run()` 时发生。

### 5.3 两个 ViewportPanel 造成对象身份断裂

当前存在两个不同的 `ViewportPanel`：

1. `Application` 自有的 `m_ViewportPanel`：`engine/include/Engine/Application.h:207-211`；
2. `EngineEditor` 自有的 `m_Viewport`：`engine/include/Engine/Editor/EngineEditor.h:110-115`。

Bridge 的 `GetViewportPanel()` 返回第一个：`engine/include/Engine/Application.h:85-86`。

但 readback 开启和真实 update 发生在第二个：

- `EngineEditor::Init()` 对 `m_Viewport.EnableReadback(true)`：`engine/src/Editor/EngineEditor.cpp:137-156`；
- `EngineEditor::OnUpdate()` 调用 `m_Viewport.OnUpdate(dt, true)`：`engine/src/Editor/EngineEditor.cpp:467-479`。

因此即使未来补建 Application，也不能假设 Bridge 读取的就是正在渲染的 panel。HRC-2 必须冻结“同一 session、同一 viewport object、同一 frame sequence”的身份关系。

### 5.4 当前 Phase 7 路径的实际状态

- `GetViewportTextureHandle()` 固定返回 `0`：`bridge/src/EditorSession.cpp:826-832`。
- `ViewportPick()` 固定返回 `-1`：`bridge/src/EditorSession.cpp:988-994`。
- `ViewportTick()` 只 flush camera input，没有调用 `ViewportPanel::OnUpdate()` 或 `Render3DScene()`：`bridge/src/EditorSession.cpp:856-873`。
- `SetViewportSize()` 只更新 Bridge 缓存和 base panel camera，不重建真实 FBO：`bridge/src/EditorSession.cpp:839-854`。
- Avalonia 显示路径是 CPU `WriteableBitmap`，不是 GL interop：`editor_avalonia/MainWindow.axaml.cs:345-371`。
- XAML 没有 `NativeControlHost` 或真实 GL viewport control：`editor_avalonia/MainWindow.axaml:71-96`。

### 5.5 GL teardown contract

HRC-0 冻结以下顺序，不规定具体实现类：

```text
停止 UI timers / 拒绝新命令
  → detach native callback
  → 停止 RuntimeTick / render loop
  → Stop runtime
  → Destroy EditorSession
  → 销毁 FBO/readback/render surface
  → 销毁 OpenGLContext
  → 销毁 GLFW window
  → Application singleton = nullptr
  → Engine/graphics factory teardown
```

所有 GL 资源必须在其 context 仍有效时销毁。当前 `GlfwWindow` 的析构顺序是 context reset 后销毁 native window：`engine/src/Platform/GlfwWindow.cpp:182-190`；`OpenGLContext` 析构会清理 GPU queries：`engine/src/OpenGL/OpenGLContext.cpp:24-27,296-301`。

---

## 6. D. EditorSession lifecycle and dependencies

### 6.1 C# lifecycle

`EditorHostService` 通过 `EnsureSession()` 懒创建 session：`editor_avalonia/Services/EditorHostService.cs:542-547`。

创建过程是：

```text
check ABI version
  → EditorSession_Create
  → pin callback
  → SetEventCallback
```

证据：`editor_avalonia/Services/EditorHostService.cs:19-31`。

`Dispose()` 会撤销 callback、Destroy native session、清零 handle：`editor_avalonia/Services/EditorHostService.cs:550-559`。

当前语义问题：

- `Dispose()` 没有 terminal disposed 状态；之后 `OpenProject()` 可以再次 `EnsureSession()`。
- `CloseProject/ResetSession` 与 terminal `DisposeApp` 复用同一方法。
- `IsOpen` 在 Dispose 时没有显式清零。
- `MainViewModel.Dispose()` 存在，但 `MainWindow` 没有调用它：`editor_avalonia/ViewModels/MainViewModel.cs:926-927`。

### 6.2 C++ lifecycle

C ABI 使用 `new EditorSession` 创建，`EditorSession_Destroy` 先 Stop 再 delete：`bridge/src/capi.cpp:25-34`。

`EditorSession` 析构只在仍处于 Playing 时调用 Stop：`bridge/src/EditorSession.cpp:31-33`。

C ABI 合同要求句柄在创建线程使用：`bridge/include/editor_bridge/capi.h:7-14`。当前 C# P/Invoke 没有额外的跨线程队列或 affinity 检查。

### 6.3 Dependencies

`EditorSession` 持有或依赖：

- edit scene / runtime scene；
- content registry / asset order / bindings；
- `ScriptInstance`；
- `OpenGLGraphicsFactory` / `TextureManager`；
- synchronous native event callback；
- viewport/gizmo/input 临时状态。

同时隐式依赖进程级全局：

- `Engine::Application::s_Instance`；
- `Engine::Scripting::GameplayAPI` 静态状态。

`GameplayAPI::BridgeState` 保存裸 `Scene*` 和句柄表：`engine/src/Scripting/GameplayAPI.cpp:35-49`。`Reset()` 会清空 scene/input/handles：`engine/src/Scripting/GameplayAPI.cpp:344-351`。

### 6.4 Global scene lifetime risks

OpenProject 在 scene instantiate 成功前就设置全局 scene：`bridge/src/EditorSession.cpp:100-105`。如果 instantiate 失败，局部 scene 销毁后全局裸指针可能悬垂。

Session 析构也没有最终调用 `GameplayAPI::Reset()`；而 Stop 只在运行态路径中 Reset 后重新指向 edit scene：`bridge/src/EditorSession.cpp:731-743`。

HRC-0 将此列为 teardown contract violation；不在本阶段修改。

### 6.5 Single-session rule

当前 native 实现和全局 `GameplayAPI` 语义只支持每进程一个 active session。两个 active session 会互相 Reset 全局 gameplay state，并分别触发 GLFW 全局初始化/终止。

HRC-0 contract：每进程最多一个 active `EditorSession`；多窗口/多 session 不在本阶段范围内。

---

## 7. E. MainWindow / MainViewModel ownership

### 7.1 Current violations

1. `MainWindow` 直接创建 Host A：`editor_avalonia/MainWindow.axaml.cs:19-22`。
2. `MainViewModel` 直接创建 Host B：`editor_avalonia/ViewModels/MainViewModel.cs:11-15`。
3. Window 的 `+ Entity` 路径直接调用 Host A，绕过 VM：`editor_avalonia/MainWindow.axaml.cs:135-140`。
4. Window 的 timer 使用 Host A，但以 VM 的 `IsOpen` 作为执行条件：`editor_avalonia/MainWindow.axaml.cs:330-339`。
5. `MainWindow` 订阅 VM 事件但不取消订阅：`editor_avalonia/MainWindow.axaml.cs:28-34`。
6. `MainWindow` 没有 Window `Closing`/`Closed` 或 App `OnExit` teardown。
7. VM 的 `Dispose()` 没有明确 owner 调用：`editor_avalonia/ViewModels/MainViewModel.cs:926-927`。

### 7.2 Proposed ownership

```text
Program / composition root
  └─ EngineHost                         [唯一 runtime owner]
      ├─ Engine::Application
      ├─ persistent GL context/surface
      ├─ render loop / scheduler
      └─ EditorSession
          └─ UI-facing session interface
              ├─ MainWindow             [只消费接口]
              └─ MainViewModel          [只消费接口]
```

HRC-0 contract：

- UI 可以持有 Host 的引用，但不能拥有或创建 Engine/GL。
- `MainWindow` 和 `MainViewModel` 必须由同一 composition root 注入同一个 Host。
- View 不得直接调用 native ABI；所有 native 操作经过 VM/Host facade。
- `MainWindow` 不再拥有 `EditorHostService` 字段。
- `MainViewModel` 不再隐式创建 session。
- Host 负责 terminal disposal；UI 只发出 Close/Stop 请求。

### 7.3 Two close operations

必须区分：

| 操作 | 语义 | 是否销毁 Engine/GL |
|---|---|---|
| `CloseProject` / `ResetSession` | 停止 runtime、销毁当前 project/session 数据；允许再次打开工程 | 否 |
| `CloseApp` / terminal `Dispose` | 停止所有 tick/render，销毁 session、context、surface、Application | 是 |

当前两种语义都落在 `EditorHostService.Dispose()`，不符合契约。

---

## 8. F. RuntimeTick ownership

### 8.1 Existing call chain

```text
EditorHostService.RuntimeTick(dt)
  → EditorBridgeApi.EditorSession_RuntimeTick
      → C ABI EditorSession_RuntimeTick
          → EditorSession::RuntimeTick
              → ScriptInstance::OnUpdate(dt)
```

证据：

- C# wrapper：`editor_avalonia/Services/EditorHostService.cs:238-242`；
- P/Invoke：`editor_avalonia/Native/EditorBridgeApi.cs:144-145`；
- C ABI：`bridge/src/capi.cpp:269-272`；
- native 实现：`bridge/src/EditorSession.cpp:746-751`。

### 8.2 Current production callers

- `editor_avalonia` 中没有 `RuntimeTick` 生产调用者。
- `MainWindow` 的 33 ms timer 只调用 `ViewportTick`，不调用 `RuntimeTick`：`editor_avalonia/MainWindow.axaml.cs:36-42,335-339`。
- `MainViewModel` 没有 runtime tick wrapper。
- 唯一实际调用来自 Bridge test 的单帧手动 tick：`tests/test_bridge/test_editor_capi.cpp:612-623`。

因此当前 Play 后 UI 可以显示 running，但 gameplay `OnUpdate` 不会持续推进。

### 8.3 Current stop condition

`EditorSession::RuntimeTick` 只在以下情况提前返回：

```text
!m_Playing || !m_Inst.IsValid()
```

证据：`bridge/src/EditorSession.cpp:746-748`。

它不会因为某次 OnUpdate 错误自动停止，也不会把该次错误复制到 `m_RuntimeError`；错误主要由 `ScriptInstance::OnUpdate` 记录日志：`engine/src/Scripting/ScriptInstance.cpp:88-93`。

### 8.4 HRC-0 RuntimeTick contract

- RuntimeTick owner：唯一 `EngineHost` scheduler；不是 MainWindow，也不是 Viewport timer。
- 频率：每个 engine frame 最多一次；不能以 33 ms UI refresh 冒充 runtime tick。
- 条件：只有 Host 已完成 bootstrap 且 runtime 处于 Playing 时调用。
- 停止：Close、Stop、Dispose、fatal init failure、render loop termination 前先停止 tick。
- 线程：HRC-0 先固定为 UI/host 创建线程；若 HRC-2 引入 render thread，必须另行冻结命令队列、thread affinity 和 join barrier。
- Viewport refresh 与 RuntimeTick 可以属于同一 engine frame，但必须是两个明确的阶段，不允许一个 UI timer 隐式驱动两个生命周期。

---

## 9. G. Failure and disposal paths

### 9.1 Normal Avalonia window close

当前路径：

```text
Window close
  → Avalonia lifetime exit
  → no App.OnExit
  → no Window.Closing/Closed handler
  → timer not explicitly stopped
  → Host A not disposed
  → VM Host B not disposed
  → EditorSession_Destroy not called
```

证据：`editor_avalonia/App.axaml.cs:11-16`、`editor_avalonia/Program.cs:9-15`、`editor_avalonia/MainWindow.axaml.cs:36-42`。

当前只能依赖进程退出回收，不能提供进程内有序 teardown 证据。

### 9.2 File → Close Project

`OnCloseProject → Vm.ResetSession → _host.Dispose`：`editor_avalonia/MainWindow.axaml.cs:303-313`、`editor_avalonia/ViewModels/MainViewModel.cs:467-487`。

正常路径会：

```text
detach callback
  → EditorSession_Destroy
      → Stop
      → delete EditorSession
  → C# handle = 0
```

但 MainWindow 的另一个 Host 仍存在，timer 仍运行；native `GameplayAPI` 也可能保留旧 scene 裸指针。

### 9.3 Script Reload

Reload 不销毁 Session：`editor_avalonia/ViewModels/MainViewModel.cs:670-689`、`bridge/src/EditorSession.cpp:715-729`。

`ScriptInstance::Reload()` 会先清空/重建 Lua global，再执行新文件；失败时不会恢复旧脚本的完整 global state：`engine/src/Scripting/ScriptInstance.cpp:114-145`。

HRC-0 要求：Reload 失败不得破坏 Host/session 生命周期；脚本状态原子回滚属于后续 Runtime contract，不在 HRC-0 实现。

### 9.4 Double teardown risks

安全部分：

- C# Host 第二次 Dispose 因 handle 为零不会再次调用 native Destroy：`editor_avalonia/Services/EditorHostService.cs:550-559`。
- C ABI Destroy 先 Stop，析构中的 `if (m_Playing)` 通常避免第二次 Stop：`bridge/src/capi.cpp:29-34`、`bridge/src/EditorSession.cpp:31-33`。

已识别风险：

1. `EditorSession::Stop()` 显式调用 `m_Inst.OnDestroy()`，随后 `m_Inst.Shutdown()` 在 `m_Loaded` 时再次调用 `OnDestroy()`：`bridge/src/EditorSession.cpp:731-739`、`engine/src/Scripting/ScriptInstance.cpp:74-79`。
2. `CreateSession()` 在 native Create 成功、callback 设置异常时没有 rollback：`editor_avalonia/Services/EditorHostService.cs:19-31`。
3. C# Dispose 如果 native Destroy 抛异常，handle 尚未清零，可能再次进入同一 raw handle：`editor_avalonia/Services/EditorHostService.cs:550-559`。
4. C++ C ABI wrappers 没有异常边界，C++ 异常可能穿越 C ABI：`bridge/src/capi.cpp:14-34`。
5. native callback 是同步调用，C# callback 直接 invoke，没有异常隔离：`bridge/include/editor_bridge/capi.h:10-14`、`editor_avalonia/Services/EditorHostService.cs:539-540`。
6. `GameplayAPI` 的静态 raw scene 在 session teardown 后没有最终 Reset。

### 9.5 Startup partial failure contract

HRC-0 要求每个启动阶段可回滚：

```text
no partially-created native handle
no partially-registered callback
no live timer without owner
no live GL resource without context
no live session without Host
```

失败注入至少覆盖：

- ABI load/version failure；
- `EditorSession_Create` 失败；
- callback registration 失败；
- Engine/Application init failure；
- GL context/surface creation failure；
- render loop startup failure；
- Window close during startup。

---

## 10. H. Proposed frozen HRC-0 contract

以下为规范性契约，不是本阶段实现。

### H0-01 — Single runtime owner

- 每进程恰好一个 `EngineHost`。
- `EngineHost` 是 Engine、GL context/surface、render loop、scheduler 和 active `EditorSession` 的唯一 owner。
- `Program`/composition root 创建 Host，并将同一 Host 注入 Window 与 VM。
- 禁止 Window、VM 或 View 自行创建第二 Host、Application、GL context 或 session。

### H0-02 — Single active session

- 每进程最多一个 active `EditorSession`。
- Session 只能在 Host bootstrap 成功后创建。
- Session 必须在 Host teardown 前销毁。
- CloseProject 后允许重新创建 project session；terminal Dispose 后禁止隐式重建。

### H0-03 — Ownership graph

```text
Program / composition root
  └─ EngineHost
      ├─ Engine::Application
      ├─ GL context / render surface
      ├─ render loop / scheduler
      └─ EditorSession
          └─ injected UI facade
              ├─ MainWindow
              └─ MainViewModel
```

### H0-04 — UI boundary

- View 只调用 VM/facade，不直接触碰 native handle。
- ViewModel 只调用注入的 Host/session facade，不自行创建 runtime。
- View 与 ViewModel 不得分别拥有可销毁的 native Host。
- 所有 session callback 必须经过单一事件入口，并具备异常隔离。

### H0-05 — Lifecycle state machine

```text
Created
  → Bootstrapping
  → Ready
  → ProjectOpen
  → Playing
  → Stopping
  → Ready
  → Disposing
  → Disposed
```

失败状态必须能回滚到上一个稳定状态；`Disposed` 是 terminal state。

### H0-06 — Teardown order

```text
stop UI timers and reject commands
  → detach native callback
  → stop RuntimeTick and render loop
  → Stop runtime
  → Destroy EditorSession
  → destroy FBO/readback/render surface
  → destroy GL context/window
  → clear Application singleton
  → reset GameplayAPI/global state
  → release Host
```

具体实现可以分阶段落地，但不可改变依赖方向。

### H0-07 — Tick ownership

- 唯一 scheduler 负责 RuntimeTick。
- RuntimeTick 每 engine frame 最多一次。
- UI refresh timer 不得隐式驱动 gameplay/runtime。
- Stop/Dispose 前必须先停止 tick。
- HRC-0 不承诺具体 30/60 FPS；HRC-2 必须给出明确频率和 dt policy。

### H0-08 — Thread affinity

- HRC-0 固定 session 创建线程、Host bootstrap 和 GL 操作的 affinity 为同一 host thread。
- 如果后续使用 render thread，必须在实现前增加显式 command queue、context make-current、停止屏障和 join 规则。
- 不允许把现有 `Application::Run()` 直接搬到后台线程而不定义同步契约。

### H0-09 — Error boundary

- C++ exception 不得穿越 C ABI。
- C# native callback 不得把 exception 抛回 native。
- callback detach 后不得再收到事件。
- 初始化失败不得留下半初始化 handle/context/timer。
- 重复 Dispose 必须是 no-op。

### H0-10 — Global state

- `Application::GetInstance()` 只在 Host bootstrap 完成后、销毁前有效。
- Application teardown 前必须使 singleton 失效。
- Session teardown 必须清除 `GameplayAPI` 的 scene/input/handle 状态。
- Session 不得活得比其依赖的 Application/GL context 更久。

### H0-11 — Viewport object identity

在 HRC-2 之前不要求像素显示，但必须先冻结：

- Bridge 操作的 viewport object；
- EngineEditor/Application 实际渲染的 viewport object；
- 读取 readback 的 object；
- resize 接收的 object；

四者必须是同一个对象或一个明确登记的 render target。当前 `Application::m_ViewportPanel` 与 `EngineEditor::m_Viewport` 的分裂是 H0 contract violation。

---

## 11. I. Explicit non-goals for HRC-0

本阶段明确不实现、不修改：

- Gizmo 算法或渲染实现；
- Picking 算法；
- Renderer/RenderGraph/RHI 重构；
- EditorSession 公共 API 重设计；
- ECS 或 `test_ecs` 修复；
- ImGui 清理；
- Inspector/Hierarchy 新功能；
- Asset Pipeline 修改；
- 物理、脚本、ContentRegistry、SceneSerializerV1 冻结 contract；
- 多 session、多 world、跨进程 IPC；
- GPU interop 性能优化；
- 30/60 FPS 性能承诺。

HRC-0 只冻结 ownership、lifecycle、thread affinity、tick、error boundary 和 teardown 规则。

---

## 12. J. HRC-1 implementation gates

### HRC-G1 — Single Host Ownership

HRC-1 目标：消灭 `MainWindow` / `MainViewModel` 双 Host。

必须证明：

1. composition root 创建一个 Host；
2. MainWindow 接收同一 Host 引用；
3. MainViewModel 接收同一 Host/session facade；
4. MainWindow 和 MainViewModel 不再 `new EditorHostService`；
5. View 不直接调用 native ABI；
6. 没有第二 Engine/GL/session 初始化；
7. Open → CloseProject → Open 使用同一 Host；
8. terminal Close/Dispose 后不能隐式重建 session；
9. 重复 Dispose 无 double Destroy；
10. callback 数量和 native handle 数量可观测且平衡。

当前状态：**BLOCKED，待 HRC-1 实现。**

### HRC-G2 — Runtime Bootstrap

HRC-2 目标：Host → Engine → GL → Session → UI frame。

必须证明：

1. 唯一 `Engine::Application` 被创建；
2. 唯一持久 GL context/surface 存在；
3. context current 规则明确；
4. `EditorSession` 使用同一 runtime；
5. UI tick 能驱动 runtime frame；
6. render loop 不阻塞 Avalonia UI，或有明确 render-thread affinity；
7. close 时 runtime 先停再销毁；
8. init failure 可回滚；
9. `Application::GetInstance()` 销毁后为 null；
10. FBO/context/window 创建销毁计数平衡。

当前状态：**BLOCKED；无 Application/context/render host bootstrap。**

### HRC-G3 — Viewport Reality Gates

| Gate | 最小证明 | 当前状态 |
|---|---|---|
| HRC-G3a | Engine frame 实际进入 Avalonia Viewport | NOT REACHED |
| HRC-G3b | Resize 更新同一 runtime viewport 并正确重建 FBO | NOT REACHED |
| HRC-G3c | Texture/readback 实际产生非零、尺寸一致的数据 | NOT REACHED |
| HRC-G3d | Picking 实际命中对象并回写 Hierarchy | NOT REACHED |
| HRC-G3e | Gizmo 修改实际回写 Transform | NOT REACHED |
| HRC-G3f | RuntimeTick 持续驱动并可停止 | NOT REACHED |
| HRC-G3g | Close/dispose 无崩溃、泄漏、UAF 或第二实例 | NOT REACHED |

### HRC-G4 — Candidate freeze

只有以下全部满足，才可重新评估 `Avalonia UI = FROZEN candidate`：

- HRC-G1、G2、G3a–g 全部通过；
- 现有 authoring gates 保持全绿；
- `test_bridge` 通过且新增 runtime/UI integration tests 通过；
- `test_ecs` 仍作为独立 baseline issue，不被 UI Gate 掩盖；
- ASan/handle/context 计数和 teardown 证据完整；
- Human Golden Run 真实完成一次。

---

## 13. Open decisions to freeze before HRC-1

以下不是本阶段实现项，但必须在 HRC-1 开始前明确记录：

1. **Host 的具体类型和创建位置**：由 `Program` 创建并传入 `App`，还是由 `App` 作为 composition root 创建；无论选择哪种，必须全进程唯一。
2. **HRC-1 的线程模型**：先采用单 host/UI thread，还是同时引入 render thread；后者必须先补齐 command queue/context affinity 契约。
3. **CloseProject 与 terminal Dispose 的 API 语义**：必须成为两个不可混淆的状态转换。
4. **Application singleton 的替代或清理策略**：不得继续依赖未声明生命周期的全局指针。
5. **viewport object identity**：Application panel 与 EngineEditor panel 的唯一归属。
6. **RuntimeTick 频率和 dt policy**：每 frame、fixed step 或 throttled，必须在 HRC-2 固化并可观测。
7. **callback/exception policy**：C ABI exception boundary 与 C# callback 隔离方式。
8. **重复路径基线处理**：`engine/src/Core/Application.cpp` 与 `engine/src/core/Application.cpp` 的大小写冲突必须在跨平台 freeze 前单独解决。

---

## 14. Final HRC-0 decision

```text
HRC-0 contract:       FROZEN AS PROPOSED
Current implementation: FAIL
Host uniqueness:      FAIL
Engine lifecycle:     NOT CONNECTED
GL lifecycle:         NOT CONNECTED
EditorSession scope:  NOT ENFORCED
RuntimeTick:          NO PRODUCTION OWNER
Dispose safety:       NOT PROVEN
UI status:            PARTIAL / NOT FROZEN
Next allowed phase:   HRC-1 Host Unification
```

HRC-0 的完成含义是：问题和 ownership contract 已经冻结，允许进入 HRC-1；不代表当前代码已经满足 contract。
