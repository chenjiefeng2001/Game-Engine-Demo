# 架构 / Runtime Integration Inventory（成熟度审计）

> **性质**：全仓审计产出的**事实性清单**，记录每个 subsystem 的 runtime 接入状态与证据。
> **不含**：架构建议、方案推荐、待决策项。决策与恢复条件见 `docs/Pending-Decisions-Brief.md`。
> **审计方式**：source reading only —— 未构建、未运行任何 target。
> **基线**：`HEAD 7d623ed`（local `avalonia`，已 push）；`origin/master` = `1a066ed`
> **工作区**：56 tracked modified + 70 untracked（HRC-3），全文逐处区分 COMMITTED / UNCOMMITTED
> **日期**：2026-10-06 建立；**2026-10-08 最小差异校准**（仅更新基线 SHA 与两处已变更的决策状态引用；判定本身未重审，`test_*` 用例数 699 仍以**实跑**为准，不按 grep 计数改写）

---

## 0. 阅读须知：本文档的状态词汇

**"代码存在"不等于"产品功能完成"。** 本文所有结论按五级标注，且每级都带可复核证据：

| 标记 | 含义 |
|---|---|
| **COMPLETE** | 有真实产品路径，且被驱动/被消费 |
| **PARTIAL** | 主路径可用，但存在已记录的缺口或仅部分接线 |
| **STUB** | 代码存在但返回占位值/空实现，链路在此断开 |
| **UNREACHABLE** | 代码存在，但**无 producer 或无 consumer**，当前不可达 |
| **HRC-ONLY** | 只存在于未提交的 HRC-3 工作区，committed baseline 无此能力 |

**UNREACHABLE 是本文档新增的一级。** 它与 STUB 的区别：STUB 在可达路径上返回假值；UNREACHABLE 则是整条链路两端都没有调用方，因此**其行为不会被任何人观察到**，也就不能按 defect 处理。

### 0.1 空函数体必须区分两类

空函数体（`{}`）本身不是状态标记。扫描中发现的 41 处空 override 分为两类，**记录时必须分开**：

| 类别 | 定义 | 处置 |
|---|---|---|
**可达的 no-op** | 被生产代码调用，但不做功 | **潜在缺陷面**，需 characterization 定性 |
**不可达的空实现** | 无外部调用方 | 死代码面，不按 defect 处理 |

这一区分来自 `PhysicsSyncSystem` 的教训：初判其为"真实运行时错误"，实为端到端不可达。**先核对可达性，再定性。**

> **可达的 no-op 目前为空集。** 唯一曾被怀疑的 `JobSystem::PollCompleted`（`Application.cpp:790` 每帧调用）经核查为良性，见 §6.1。

---

## 1. 总体成熟度判断

**仓库的成长模式是 breadth-first：subsystem 被独立实现并单元测试，然后从未接入产品 runtime。**

具体表现为一个反复出现的同构模式：

```
[完整实现 + 真实测试]  →  [无 producer / 无 consumer / 无 coordinator]  →  [不在 Application 帧内]
```

ECS、Animation、`Core/RHI`、Scripting 四者都命中这个模式，且各自都有可观的测试覆盖。这一模式本身是成熟度信号，**不是缺陷清单**：这些 surface 的处置属于产品/架构意图，不应自动转为 integration 任务。

**因此本文档不产生任何待办工程项。** 唯一例外已在 §5 完成定性（STUB / unreachable，无需修复）。

---

## 2. Test target 口径（严格区分两个状态）

> **13 committed targets + 3 HRC targets = 工作区 16 个。**
> 「16 targets」仅描述**当前 HRC 工作区事实**，不代表仓库 committed baseline。

| 类别 | 数量 | targets |
|---|---|---|
| **COMMITTED**（`HEAD`） | **13** | `test_core` `test_ecs` `test_physics` `test_job` `test_renderer` `test_scripting` `test_content` `test_gp01` `test_e2e` `test_bridge` `test_animation` `test_audio` `test_io` |
| **HRC-3 新增（uncommitted）** | **3** | `test_hrc` `test_pick` `test_pick_transport` |

用例数（`TEST`/`TEST_F` 出现次数）：

| target | 用例 | target | 用例 |
|---|---|---|---|
| `test_io` | 202 | `test_gp01` | 22 |
| `test_audio` | 112 | `test_ecs` | 21 |
| `test_animation` | 111 | `test_renderer` | 9 |
| `test_content` | 60 | `test_e2e` | 6 |
| `test_physics` | 54 | `test_job` | 4 |
| `test_bridge` | 50 | `test_core` | 43※ |
| `test_scripting` | 27 | `test_hrc`（HRC） | 15 |
| `test_pick`（HRC） | 8 | `test_pick_transport`（HRC） | — |

※ `test_core` 使用 `EXPECT_*`/`ASSERT_*` 而非 `CHECK`，按 TEST 宏统计为 43（`Vector3Test` 12 + `StackAllocatorTest` 8 + `TimeTest` 23）。

**`test_physics` 的覆盖面值得注意**：54 个用例覆盖 physics 库本身，但 **`PhysicsSyncSystem` / `RouteCollisionEvents` / `CollisionListener` 在 tests 下零引用**（已验证）。

---

## 3. Core subsystem

| Subsystem | 状态 | 决定性证据 |
|---|---|---|
| Application（windowed） | **COMPLETE** | 5 个子类覆盖全部 4 个 hook；EditorDemo 端到端启动 |
| Application（headless） | **HRC-ONLY / PARTIAL** | 平行生命周期；**跳过 Profiler / JobSystem / SceneManager** |
| ECS core | **COMPLETE** | 21 测试；B4 registration 契约 + swap-with-back 不变量已钉死 |
| ECS integration | **UNREACHABLE** | `ISystem` 实现数为 0；ECSBridge 文档承诺的转发不存在 |
| GameObject | **COMPLETE** | 生命周期、层级、listener、契约注册表均工作 |
| GameObject 序列化 | **PARTIAL** | 8 个 component 中 4 个无可用路径 |
| Time（committed） | **COMPLETE** | `steady_clock` + drift 校准 |
| Time（`Shutdown`/`IsInitialized`） | **HRC-ONLY** | 未提交；7 个测试被刻意排除 |
| Resources | **PARTIAL** | 不存在 `AssetManager` 类；`AssetPipeline`/`AssetMetaDb` 未接线 |
| JsonSerializer | **COMPLETE** | 27 测试；宽松语义为刻意设计且已钉死 |

### 3.1 COMMITTED `Application` 注册的 8 个 subsystem

`Application` 构造函数（committed）注册的**全部**内容：

| name | phase | init |
|---|---|---|
| Window | Platform | `InitWindow()` |
| Camera | Graphics | `InitCamera()` |
| UI | Graphics | `InitUI()` |
| ResourceManager | Resources | — |
| FileWatcher | Resources | — |
| Shader | Resources | `InitShader()` |
| Texture | Assets | — |
| VertexData | Assets | `InitVertexData()` |

**没有 audio、没有 physics、没有 scripting。** 这是本文档最重要的单一结构事实：三个 subsystem 的"已实现"状态与"已接入产品"状态完全脱钩。

### 3.2 windowed frame 链路（COMMITTED）

```
sandbox/src/EditorDemo/main.cpp:44   Engine::EditorDemoApp app(factory)
  → ctor 注册 8 subsystem
  → Run()
  → Log::Init / SetAllocator / CrashHandler::Init
  → SubsystemManager.Initialize()      (按 SubsystemPhase 排序)
  → InitWindow()                        CreateWindow → GLFW + GL context
  → Time::Init / Profiler::Init / JobSystem::Init / SceneManager::Init
  → OnStartup()
  → while (!ShouldClose())
      PollEvents → Time::UpdateDeltaTime
      → InternalUpdate(dt) → OnUpdate(dt)          ← 虚拟 hook ①（Render3DScene 在此驱动）
      → DispatchSubsystemUpdates(dt)
      → OnImGui()                                 ← 虚拟 hook ②（呈现已渲染好的纹理）
      → InternalRender() → OnRender()             ← 虚拟 hook ③（EditorDemo 下跳过）
      → SwapBuffers
```

**两处与直觉相反的事实：**

1. **EditorDemo 的 `OnRender()` 是空实现。** `m_RenderDefaultQuad=false` 使 `Application.cpp:558-582` 整段 FBO/clear/draw 被跳过。真实场景渲染由 **`OnUpdate` → `ViewportPanel::Render3DScene()`** 驱动（`ViewportPanel.cpp:161`）；`OnImGui` 不执行场景渲染，只用 `ImGui::Image` 把结果纹理贴出（`:296-301`）。`OnRender` hook 不在生产路径上。

   > **更正记录（2026-10-06）**：本文档早期版本称"真实渲染发生在 `OnImGui()` 内"。该描述不准确 —— `Render3DScene()` 由 `OnUpdate` 驱动，`OnImGui` 仅负责呈现。详见 §4.2。

2. **headless 下 `OnUpdate`/`OnRender` 双双失效。** `EngineHost.cpp:528` 实例化的是**基类** `Application` —— 而已验证：`engine/`、`bridge/`、`tests/` 下**不存在任何 `Application` 子类**（5 个全在 `sandbox/`）。因此 headless 渲染完全由 `EngineHost` 在 Application 帧之外编排。

### 3.3 headless frame 链路（HRC-ONLY，uncommitted）

```
capi: EngineHost_Create → InitializeRuntimeResources
  → make_unique<Engine::Application>(*factory, /*headless*/true)   ← 基类，非子类
  → InitializeHeadless()      Log/Allocator/CrashHandler/subsystems（全部短路）+ Time::Init
                              ⚠ 不初始化 Profiler / JobSystem / SceneManager
  → OnStartup()               基类空实现，无 override
每帧: EngineHost_PumpOneFrame
  → RuntimeLoop（runtime thread）
  → Application::PumpOneFrame(dt) → InternalUpdate(dt) → OnUpdate(dt)（空）
                                  → DispatchSubsystemUpdates（JobSystem 为 nullptr → 串行退化）
  → m_Session->RuntimeTick(dt)     ← 实际推进 gameplay 的不是 Application
  → probe FBO Clear + ReadColorPixel 断言
  → Application::RenderProductionFrame(...)   ← 渲染在帧外，由 EngineHost 编排
```

**结论：`Application` 的每帧契约在两条路径间不对称。** headless 不是 windowed 的子集，而是另一套更不完整的生命周期。该不对称已被测试固化为"预期行为"（`test_hrc` 断言 `sceneInitialized==0`、`jobSystemPresent==0`）。

---

## 4. Graphics subsystem

### 4.1 核心发现：两套互不连接的图形抽象

| | 生产抽象 | 形式 RHI 层 |
|---|---|---|
| 接口 | `IGraphicsFactory` / `IWindow` / `IRenderContext` | `IRHIDevice` / `IRHICommandList` / `IRHISwapChain` / `IRHICommandQueue` |
| 位置 | `engine/include/Engine/Core/` | `engine/include/Engine/Core/RHI/`（46 header） |
| 实现数 | **1**（仅 OpenGL，已 grep 验证 3 处命中全为 OpenGL） | **3**（GL46 / Vulkan / D3D12） |
| 是否驱动像素 | **是** | **否** |
| 调用方 | engine runtime | 仅 sandbox demo + unit test |

已验证：`engine/src/Core`、`bridge`、`engine/src/Editor` 中**无任何 `GL46Device`/`VulkanDevice`/`D3D12Device` 构造点**（仅 `GPUPhysicsEngine.cpp` 一处注释提及 stub）。

| Subsystem | 状态 | 决定性证据 |
|---|---|---|
| `IGraphicsFactory`/`IWindow`/`IRenderContext` | **PARTIAL** | 真接口，**仅 1 个实现** |
| `Core/RHI` device RHI | **UNREACHABLE** | 3 backend 齐备，**0 处接入 engine** |
| backend 选择 | **STUB** | `RHIBackend.cpp:87` `return nullptr;` |
| `RHIWindow` | **UNREACHABLE** | 唯一调用方 `sandbox/src/RHIDemo` |
| OpenGL（生产路径） | **COMPLETE** | factory / context / swapchain 均为实码 |
| OpenGL（`GL46*` RHI 子路径） | **STUB** | `Present(){}`、全部 draw 方法 `{}` |
| Vulkan | **PARTIAL** | device + Present 为实码；descriptor 绑定恒为 no-op |
| D3D12 | **PARTIAL** | 最完整的非 GL backend；SRV 从未写入 |
| `Rendering/` | **PARTIAL** | `AutoPipelineLayout` 返回 `0xDEADBEEF` |
| ShaderReflection（两份） | **STUB + UNREACHABLE** | 一份有声明无定义，一份有实现无调用方 |

### 4.2 windowed 渲染链路 —— COMPLETE

```
InitWindow → CreateWindow → glfwCreateWindow → CreateRenderContext
  → OpenGLContext → glfwMakeContextCurrent → gladLoadGLContext → Init() → GlfwWindow

每帧:
  OnUpdate → ViewportPanel::OnUpdate → Render3DScene()        ViewportPanel.cpp:161
      BindFramebuffer(m_FBO_ID) → ClearColor/Clear → ClearBufferiv(GL_COLOR,1,-1)   :177-189
      m_SceneRenderCallback(vp, camPos)                     :208
      BindFramebuffer(GL_FRAMEBUFFER, 0)                    :219
  OnImGui → ViewportPanel::OnImGui → ImGui::Image(m_ColorTexture)   :296-301
  InternalRender()                                          Application.cpp:545
  m_Window->OnUpdate() → SwapBuffers()                      GlfwWindow.cpp:231
      AA ResolveToDefault → GPU timestamp 收集 → glfwSwapBuffers   OpenGLContext.cpp:45-82
```

**该链路完整且产出像素。** 每一跳均有 committed 实现：MRT FBO 的 clear + pick-id 写入、场景回调、AA resolve、真实 `glQueryCounter` 计时、`glfwSwapBuffers`。resize 亦完整：`glfwSetFramebufferSizeCallback`（`GlfwWindow.cpp:40`）→ `GlfwWindow::OnResize:76` → `IRenderContext::OnResize`（`OpenGLContext.cpp:126`）→ `AntiAliasing::OnResize`；ViewPortPanel 另有独立的 FBO 重建（`m_NeedsFBOUpdate`，由 ImGui 显示尺寸驱动）。两套 resize 机制并存且各自连通。

**`OnRender` 不参与渲染。** EditorDemo 设 `m_RenderDefaultQuad=false`，`InternalRender()` 的 clear+draw 整块位于 `Application.cpp:558-582` 的该条件内，故被跳过；`OnRender()` 为空实现。

**fb0 的清理由 ImGui 后端承担，不是 `Application`。** `ImGuiUIManager.cpp:92-93` 在 `Renderer_RenderWindow` 回调中 clear color+depth。因此渲染链路的收尾环节隐式依赖 ImGui 回调 —— 这是职责转移，**不是缺陷**，但它意味着移除 ImGui 后端会同时移除默认帧缓冲的清理。

### 4.3 backend 选择 —— STUB

`CreateRHI(RHI::Backend)` 返回 `nullptr`（`RHIBackend.cpp:87`，注释："实际创建在引擎启动时由 GLFW 后端接管"），且无任何代码路径可路由到非 OpenGL factory。Vulkan / D3D12 路径**无法从生产 graphics factory 到达**（UNREACHABLE，详见 §6）。

### 4.4 headless → Avalonia 呈现 —— 断于 native 导出边界

| 环节 | 状态 |
|---|---|
native 渲染到 `m_PresentationFbo`（HRC-3，26 处引用） | 完整 |
**native C ABI 导出 presentation frame** | **不存在** —— `capi.h` / `capi.cpp` 中无 `PresentationFrame` |
managed 常量 `EngineHostOpGetPresentationFrame = 28` | 已声明（`EditorBridgeApi.cs:115`） |
managed `TryFetchPresentationFrame` | 调用 op 28，native 侧无对应 handler |
`ViewportFramePresenter.Present` | **代码完整** —— 逐行 `Marshal.Copy` 翻转、`WriteableBitmap`、`PixelFormat.Rgba8888` |
native `ReadPresentationPixels` | `EngineHost.cpp` 中仅 1 次出现 |

**定性：不是"未接线"，是"接线只完成一半，且另一半尚未进版本控制"。** managed 侧呈现代码真实且完整，但没有任何 native 导出能向它提供数据。整条链的 native 端 —— `bridge/src/EngineHost.cpp` / `.h` —— **仍为 untracked，不在 HEAD 中**。

管理端已自认此边界：`EditorHostService.cs:251` 抛 `"presentation frame fetch is an EngineHost capability; the legacy backend has no presentation target"`；`P2ManagedFrameGate.cs:205` 与 `D3ManagedPickGate.cs:81` 把 `op == 28` 当作**待验证契约断言**检查，而非既成事实。**因此不得把 P2 描述为"已完成"。**

### 4.5 已定性的断点

| 断点 | 位置 | 性质 |
|---|---|---|
| backend 选择为硬 null | `Core/RHI/RHIBackend.cpp:87` | **STUB** |
| GL46 swapchain 不呈现 | `GL46SwapChain.cpp:20` `Present(){}` | **STUB** |
| GL46 命令回放为空 | `GL46SwapChain.cpp:29` → `GL46CommandList.cpp:337` `ExecuteOnMainThread(){}`，且全部记录型 draw 方法为 `{}` | **STUB** |
| Vulkan descriptor 恒 no-op | `VulkanCommandList.cpp:308,324,348` 均 gate 于 `currentDescriptorSet != VK_NULL_HANDLE`，而该字段（`:39`）**从未被赋值** | **STUB** |
| 自动 descriptor layout 返回哨兵值 | `AutoPipelineLayout.cpp:185` `reinterpret_cast<void*>(0xDEADBEEF)` | **UNREACHABLE**（零调用方，详见 §6） |
| `ExtractShaderReflection` 无定义 | `include/Engine/Rendering/ShaderReflection.h:189` 声明，全仓无定义（自身注释 `:187` 即写明"空桩"） | **STUB** |
| `ReflectSPIRV` 有实现无调用方 | `src/Rendering/ShaderReflection.cpp:14`（实码），调用方为 0 | **UNREACHABLE** |
| allocator 层整体惰性 | `SetMemoryAllocator` 0 调用方；`CreateGPUMemoryAllocator` 0 调用方；`VmaAllocator` 无 `.cpp` | **UNREACHABLE** |
| `RenderGraph` 仅测试使用 | 唯一引用 `tests/test_renderer/RenderGraphSG6Test.cpp` | **UNREACHABLE** |

### 4.6 Vulkan build gating 的既有事实（已记入 Decision Brief §8，不在此重复处置）

`engine/src/Rendering/GPUParticleSystem.cpp:17` 由 `:31` 无条件 glob 编译，却 include `VulkanIRHIDevice.h` → `<vulkan/vulkan.h>`。因此 `if(Vulkan_FOUND)` 的"backend disabled"回退**并不能让 EngineCore 在无 SDK 时构建**。此项已在 `docs/Pending-Decisions-Brief.md` §8 记录为 OPEN，**本文档不重复给方案**。

### 4.7 Phase 5.2 动态验证 —— windowed path **已动态验证**

> **结论：Committed windowed path dynamically verified.**
> **这证明 committed windowed rendering path，不证明 renderer subsystem 整体完成。** PBR / shadow / formal RHI / backend switching 仍未接入产品路径（见 §4.5、§5.4、§6）。

§4.2 的 windowed 链路此前仅有 source reading 证据。Phase 5.2 在**干净 worktree、committed tree `16c029b`、零 HRC-3 污染**下实跑 `EditorDemo`（sandbox production path）取得动态证据。

**运行环境（真实，非模拟）**

| 项 | 值 |
|---|---|
commit / worktree | `16c029b`，tracked modified **0** |
会话 | Session 1，console 交互式，explorer 运行中 |
GPU | NVIDIA GeForce RTX 3070 Laptop |
窗口 | `Window created 800x600`；GL context 成功 |
AA | `OpenGLAntiAliasing created. Max samples: 32, CSAA: supported` |
场景 | `GP01 project loaded: 10 objects, 33 assets`；`player.png` / `pad.png` / `wall.png` 绑定成功 |

**① 完整 windowed path —— 通过**

真实截图中 viewport 面板打开，**5 个 sprite 实际渲染**（青色 Player + 灰色 Wall/Pad），hierarchy 列出全部 10 个对象，viewport 自身状态栏显示 `FPS: 111 | Frame: 8.7ms`。viewport 区域像素采样（1680×1050，152 色）：

| 颜色 | 数量 | 对应 |
|---|---|---|
| `31,31,38` | 70690 | viewport FBO clear `0.12,0.12,0.15`（`ViewportPanel.cpp:185`）|
| `80,200,255` | 740 | Player sprite |
| `127,140,141` | 406 | Wall / Pad sprite |

**② Resize contract —— 通过**

| 阶段 | 窗口 | 结果 |
|---|---|---|
A | 1680×1050 | viewport 全尺寸，5 sprite 正常 |
B | 1200×760 | viewport 压缩为窄条，**sprite 仍正确渲染**，纵横比正确，**无旧尺寸/旧 texture 残留、无黑区** |
C | 1680×1050 | **与 A 像素级一致** —— sprite 像素 `80,200,255`×740、`127,140,141`×406 完全相同，仅背景差 7 px |

A→B→A 往返幂等，**FBO backing texture 与实际 viewport image 在 resize 后保持一致**。两条 resize 链（GLFW 回调 → `IRenderContext::OnResize`；`m_NeedsFBOUpdate` → FBO 重建）均生效。

**③ Frame ownership —— 仅部分动态**

- **动态可得**：viewport 面板**自带状态栏并逐帧更新**（`FPS: 111 | Frame: 8.7ms`），证明 `ViewportPanel::OnUpdate` 在帧内运行；`SwapBuffers` 生效（窗口实时刷新）。
- **仍属 source reading**：`OnUpdate` → `Render3DScene` → `OnImGui` 的**严格阶段划分**（`OnImGui` 仅呈现、`InternalRender()` 不参与 EditorDemo 的 scene render）**无法在不改代码的前提下直接观测** —— 那需要插桩。本节不将其记为动态证据。

**④ 一处不得误读的读数**

状态栏 `DC: 0 | Tris: 0K` 在**存在可见像素时仍为 0**。像素是地面真相，因此**该读数不构成"未渲染"的否定证据** —— 它是 §4.5 已记录的未接线计数器（`GPUProfiler` 无调用方）。本文档既不据此推断 defect，也不把它当作渲染证据。

**⑤ 工作区影响**

EditorDemo 运行后 worktree **tracked modified 仍为 0**；仅新增 gitignored 产物：`logs/`、`content_scratch/`、`df08_scratch/`、`df09_scratch/`、`crashes/`（0 文件）。**未修改任何代码、未新增测试。**

---

## 5. Audio / Physics / Animation / Scripting

| Subsystem | 状态 | 决定性证据 |
|---|---|---|
| Audio | **PARTIAL** | 两套栈并存且不一致，**零 engine 帧内接线** |
| Physics | **PARTIAL** | Box2D 2D + Jolt 3D 均可用；coordinator 无调用方 |
| Animation | **PARTIAL** | ~55 文件、111 测试，Scene/ECS/Application 内零引用 |
| Scripting | **PARTIAL（engine）/ 已接入（editor bridge）** | engine 无接线；真实 host 是 uncommitted 的 `EngineHost.cpp` |

### 5.1 Audio：两套栈

| | Stack 1 | Stack 2 |
|---|---|---|
| 入口 | `AudioEngine`（`include/Engine/Audio/AudioEngine.h:34`），自持 ALC device/context | `IAudioEngine`（`include/Engine/Core/Audio/IAudioEngine.h:21`），header 无 OpenAL 类型 |
| source 句柄 | `int AudioSourceHandle`（内部 map） | `shared_ptr<IAudioSource>` |
| 特征 | 遮挡、空间 IR、aux send、cone | 显著更少 |

**绕过路径被写在 header 里**：`AudioClip.h:21-24` 直接示范 `alSourcei(source, AL_BUFFER, clip->GetBufferHandle())`，`GetBufferHandle()`（`:80`）返回裸 `ALuint`。两处 live site 照此实现：`AudioSystem.cpp:45-66`、`AudioSourceComponent.cpp:38-54`。

**`Application` 对 audio 的引用数为 0**（仅 `Application.h:181-182` doc-comment 中的示例代码）。真实逐帧驱动者全部在 sandbox。

`test_audio` 的 112 个用例**全部无设备依赖**（纯逻辑）。`AudioClip`、`AudioClipManager`、两套栈本体、`OpenALAudioEngine` 均无覆盖。

**A0 状态**：`239cc5c` 经 `git merge-base --is-ancestor` 验证为 **off-mainline**。它为 `Application` 注入 `shared_ptr<IAudioEngine>` 并在 Platform phase 注册 subsystem（失败软降级），**不改变任何音频行为**。A1 冻结条件见 Decision Brief。

### 5.2 Physics

2D/3D 分维度切分，**两者均不由 `Application` 驱动**。

| Subsystem | 状态 | 证据 |
|---|---|---|
| Box2D 2D | **PARTIAL** | sandbox 内 3 处 consumer，链路完整 |
| Jolt 3D | **PARTIAL** | 经 `PhysicsSyncSystem` 可达 |
| `PhysicsSystemManager` | **UNREACHABLE** | `CreateWorld3D`/`StepAll` 外部调用方 **0**（已 grep 验证） |
| `CreateWorld2D` | **STUB** | 打 warning 后 `return nullptr`，显式委托给 Box2D 旧路径 |
| debug draw | **PARTIAL** | 两个 renderer 均存在且接线，但仅在 sandbox |

实际 step 调用：2D `b2World_Step`（`Box2DPhysicsWorld.cpp:165`）；3D `m_PhysicsSystem.Update(...)`（`JoltPhysicsWorld.cpp:241-246`）。

### 5.3 PhysicsSync contact-normal —— 最终定性：**STUB / unreachable，不是 defect**

> 本项 characterization 已完成，**不另开任务，不需要生产 commit**。此处仅作证据留档。

`PhysicsSyncSystem.cpp:312,320` 向 `onCollisionEnter` 传 `Vec3(0,0,0)`。追踪完整 producer→consumer 链（全部为 **committed clean** 代码，`git status engine/src/Core/ECS/` 为空）后定性：

**1. 上游事件 schema 根本不携带法线。** `CollisionEvent`（`LockFreeEventQueue.h:28-45`）的 payload 仅 `type` / `bodyIDA` / `bodyIDB` / `totalImpulse`。**既无法线也无接触点字段**，而 callback 签名的参数名是 `const Vec3& point`（`PhysicsComponents.h:143`）。

**2. 丢弃是 worker 线程边界的刻意设计。** Jolt 回调确实拿到 manifold（`JoltPhysicsWorld.cpp:112`），但只提取 `mPenetrationDepth` 存入 `totalImpulse`（`:119`）。`:110` 注释明写 *"只取 BodyID，不存 touch 点"* —— 因 worker 线程不可安全访问 `Body`。

**3. 因此 `Vec3(0,0,0)` 是"上游有意丢弃 + 下游无值可转发"的诚实占位，而非错误传参。**

**4. 决定性证据：整条链路无 reachability。**

| 检查 | 结果 |
|---|---|
| 任何赋值 `onCollisionEnter` 的位置 | **0** |
| 任何实例化 `PhysicsSyncSystem` 的位置 | **0**（仅 doc comment，及 `ComponentRegistry.cpp:46` 一条注记） |
| `tests/` 下对 `PhysicsSyncSystem`/`RouteCollisionEvents`/`CollisionListener` 的引用 | **0** |
| `CollisionListenerComponent` 的注册方 | 无（仅其自身查询代码） |

**结论：`RouteCollisionEvents` 是端到端死代码。其行为不会被任何人观察到，因此按 STUB / unreachable 记录，不按 defect 处理。**

> **更正记录**：本文档的早期 source-reading 阶段曾将此点标为"real defect"。该判断错误 —— 未检查 reachability。按"先 characterization 再定性"的纪律重新追踪后定为 stub。

### 5.4 Animation

最大代码面（约 55 文件）、测试覆盖良好（111 用例），**runtime 接入度最低**。

存在且实现：skeleton、skinning、clip、blend tree、blend space 1D/2D、state machine、IK、constraint、retarget、compression。

| 项 | 状态 | 证据 |
|---|---|---|
| `SkinningComponent` | **UNREACHABLE** | 唯一 `Component` 子类，**全仓零实例化**（已 grep 验证，仅自身定义/实现） |
| `AnimationManager`/`Pipeline`/`Instance` | **UNREACHABLE** | 调用方仅自身文件与 `tests/test_animation` |
| `Scene`/`ECS` 接入 | **无** | `Scene.h`/`Scene.cpp` 零 animation 引用；无 animation ECS bridge |
| IK（`IK.h`） | **UNREACHABLE** | 实现完整（CCD 等），但**无 in-repo consumer**；demo 走 `ConstraintSolver` |
| 逐帧驱动 | sandbox only | `AnimationDemoApp` 是**独立类**，非 `Application` 子类，自持 `Run()` 循环 |

### 5.5 Scripting

Lua 5.4（vendored，`third_party/lua`），sandbox 已裁剪 `io`/`package`/`require` 及危险 `os` 函数。

| 项 | 状态 | 证据 |
|---|---|---|
| `Application` 接线 | **无** | `Application.h/.cpp` 零 scripting 引用 |
| `PluginSystem` | **UNREACHABLE** | committed，但零 consumer |
| editor bridge 集成 | **COMPLETE**（但依赖 uncommitted 文件） | Play/Reload/Stop 三态 + `_PERSIST` + 事件 + fault injection |

真实 host 是 editor bridge：`EditorSession::RuntimeTick(dt)` → `m_Inst.OnUpdate(dt)`，gated on `IsPlaying()`。C ABI 经 `capi.cpp`，Avalonia 侧由 `MainWindow.axaml.cs` pump。

**COMMITTED vs HRC-3 差异**：`ScriptInstance` 的 `OnCreate`/`OnUpdate`/`OnFixedUpdate` 由 `void` 改为 **`bool`**、`OnDestroy` 改为幂等、失败时拆除 VM 并记录 `m_LastError` —— 全部 **uncommitted**。`tests/test_scripting/ScriptingMVPTest.cpp` 已消费这些新 API，故该测试的通过依赖未提交代码。

---

## 6. UNREACHABLE surface 汇总

以下 surface 均**实现存在但当前不可达**。它们的共同点是：既非缺陷（无人观察其行为），也非缺功能（代码已写），而是**处置权属于产品/架构意图**。

| Surface | 状态 | 缺失的一端 |
|---|---|---|
| `CreateRHI(Backend)` | **STUB** | 返回 `nullptr`，无路由路径 |
| `RHIWindow` | **UNREACHABLE** | 仅 sandbox 调用方 |
| `Core/RHI` 三 backend device | **UNREACHABLE** | 无 engine 构造点 |
| `ReflectSPIRV` | **UNREACHABLE** | 有实码，零调用方 |
| `ExtractShaderReflection` | **STUB** | 有声明，无定义 |
| `AutoPipelineLayout::CreatePipelineLayout` | **UNREACHABLE** | 返回 `0xDEADBEEF` 哨兵；`AutoPipelineLayoutCache` 亦零外部引用。注：全仓 5 处 `CreatePipelineLayout` 命中均为 Vulkan API `vkCreatePipelineLayout`，**同名不同符号** |
| `GL46CommandList` 11 个空方法 | **UNREACHABLE** | `SetVertexBuffer`/`SetIndexBuffer`/`SetPrimitiveTopology`/`DrawIndexed`/`Draw`/`DrawIndexedIndirect`/`SetViewport`/`SetScissorRect`/`SetConstantBuffer`/`SetShaderResource`/`ExecuteOnMainThread` 全为 `{}`。见 §6.2 |
| `InputManager::SaveBindings` / `LoadBindings` | **UNREACHABLE** | 两者外部调用方均为 0。`LoadBindings` 读完文件即打印 `"Full JSON parser not implemented in this demo."`（`InputManager.cpp:193`） |
| `GPUMemoryAllocatorFallback::Defragment` | **UNREACHABLE** | `"not implemented"`；零外部引用 |
| `LuaEngine` 8 个空方法 | **UNREACHABLE** | `SetGlobal`×4 / `RegisterFunction` / `RegisterSimpleFunction` / `WatchScript` / `SetAllowedAPIs` / `SetupBaseAPI` / `CheckStack`；精化查询后零外部调用 |
| `PhysicsColliderAdapter::AttachWorld` | **UNREACHABLE** | no-op（`PhysicsColliderAdapter.h:109,113`）；零外部引用 |
| `PropertyDrawer::DrawMixedValuePlaceholder` | **UNREACHABLE** | 零外部引用 |
| `SetMemoryAllocator` / `CreateGPUMemoryAllocator` / `VmaAllocator` | **UNREACHABLE** | 无调用方 / 无 `.cpp` |
| `RenderGraph` | **UNREACHABLE** | 仅测试引用 |
| `PhysicsSystemManager` | **UNREACHABLE** | `CreateWorld3D`/`StepAll` 零调用方 |
| `CreateWorld2D` | **STUB** | 返回 `nullptr` |
| `PhysicsSyncSystem` / `RouteCollisionEvents` | **UNREACHABLE** | 零实例化，零 listener 注册 |
| `SkinningComponent` | **UNREACHABLE** | 零实例化 |
| `AnimationManager`/`Pipeline`/`Instance` | **UNREACHABLE** | 零调用方 |
| `IK` | **UNREACHABLE** | 零 consumer |
| `PluginSystem` | **UNREACHABLE** | 零 consumer |
| `ISystem` | **UNREACHABLE** | 零实现 |
| `EntityCommandBuffer` | **UNREACHABLE** | 仅 sandbox 使用 |
| `ECSBridge` 文档承诺的转发 | **UNREACHABLE** | header 描述的转发从未实现 |
| `AssetPipeline` / `AssetMetaDb` / Core `AssetDatabase` | **UNREACHABLE** | 无生产调用方 |
| native presentation frame 导出（op 28） | **HRC-ONLY / 断链** | managed 侧常量与 fetch 逻辑齐备，native C ABI 导出不在 HEAD；`EngineHost.cpp/.h` 仍 untracked。详见 §4.4 |
| `AudioSourceComponent` | **observation** | 从未挂到任何 GameObject（证据不足，不追） |
| `SystemTestApp` 的 `debug_draw` CVar | **observation** | 无对应 `SetDebugDraw` 调用（证据不足，不追） |

---

### 6.1 空实现的三项 characterization 结论（2026-10-06）

对三处最可疑的空实现做了可达性与契约核查，**结论均为良性或死代码，不构成应修复的缺陷**。记录在此以免重复调查。

### (a) `JobSystem::PollCompleted` —— 良性预留 no-op

初判曾列为"唯一可达的 no-op 缺陷候选"。核查后**该判断被推翻**：

- `OnJobCompleted:417` 在 job 完成时**同步** `m_JobMap.erase(it)` —— 不存在待回收的 job 引用
- `TryPopLocal:452` 出队即 `pop_front()` —— 工作队列不留残余
- `m_JobMap` 是 `JobSystem` 中**唯一**的 job 容器（`JobSystem.h:264`）

即头文件承诺的"每帧清理已完成 Job 的引用"已在完成路径上做完，每帧轮询无事可做。**空实现是正确的。**

`EndFrame` 同为空实现 + 不可达。其 doc 承诺回收 `FrameTransient` 分配（该类型真实存在于 `IGPUMemoryAllocator.h:255`），但 `JobSystem` 不涉及 transient 内存、不持有 allocator —— 属为未接线功能预留的接口。

### (b) `InputManager::SaveBindings` / `LoadBindings` —— 死代码对

初判曾表述为"`SaveBindings` 写出无人能读的 JSON"。**该表述不准确**：完整调用图显示**两者外部调用方均为 0**，只有声明与定义。不是"写得出读不回"，而是根本无人写、无人读。与既有结论一致：`InputManager` 从不被 `Application` 调用（§5 附近 Input 结论）。

### (c) `GL46CommandList` 11 个空方法 —— 未完成的纯虚实现

初判曾假设"可能是刻意 stub"。三后端对照推翻该假设：

| | `SetPrimitiveTopology` | `DrawIndexed` |
|---|---|---|
`IRHICommandList.h:102` | **纯虚 `= 0`** | 纯虚 |
**GL46** | `{}` | `{}` |
**Vulkan** | 计算后**有意丢弃**（拓扑固化于 PSO，`:172-173`） | `vkCmdDrawIndexed`（`:183`） |
**D3D12** | 有实现 | 有实现（`D3D12Device.cpp:179,189`） |

接口要求必须实现，Vulkan 与 D3D12 均已实现，**仅 GL46 为空**。且 `GL46AZDODevice.h:8-17` 明确声明完整 AZDO 设计（DSA / Persistent Mapping / MultiDrawIndirect / Bindless），说明这些空方法是**对既定设计的未完成实现**。

定性：UNREACHABLE + 未完成实现。接通 `IRHIDevice` 的 OpenGL 路径会得到一个静默不画的 command list（`GL46Queue::ExecuteCommandLists` 调 `ExecuteOnMainThread`，而它为空）。**风险定性成立，但处置属"是否接通"的架构决策，非缺陷修复。**

---

## 7. 测试夹具缺口（与 Decision Brief §0 一致，此处仅留证据）

`.gitignore:88-89` 排除 `*.scene` / `*.manifest.json`。实测：

- `assets/gp01/Main.scene` —— **存在但未入库**（`git ls-files --error-unmatch` 失败）
- `assets/gp01/manifest.json` —— **已入库**，且工作区 dirty（+169/−37）

即 fresh checkout 会拿到一份**指向它并不拥有的 scene 文件**的 manifest。`EditorSession::OpenProject` 在缺 scene 时于 `EditorSession.cpp:131-132` 报 "scene load failed"。

是否将夹具纳入版本控制**已正式立项并归 HRC owner**，见 Decision Brief **§8.10.3**（committed-tree fixture reproducibility defect）。

---

## 8. 与 Decision Brief 的分工

| 文档 | 职责 |
|---|---|
| `docs/Pending-Decisions-Brief.md` | 需要人决定的事项、选项空间、恢复条件、门控 |
| **本文档** | runtime integration inventory、已接入/未接入/未可达 surface、成熟度证据 |

**明确避免的误读**："实现了但未接入" **不是** "待决策缺陷"。

- 需要人决定的事项 → Decision Brief（例：§8 CI dependency/build-boundary 的 **overall verification** 仍 OPEN，但其 **provenance chain 已 CLOSED**；committed-tree fixture reproducibility 已归 **HRC owner**）
- 已实现但无接入方 → 本文档（例：§6 全部 UNREACHABLE surface）

---

## 9. 本文档不做的事

- 不推荐任何架构选项或 integration 方案
- 不把 UNREACHABLE surface 升级为 defect
- 不把"已实现"等同于"产品功能完成"
- 不判定任何 subsystem 是否**应当**接入 runtime（属产品意图）
- 不重复 Decision Brief 中已记录的 OPEN 项处置
- 不修改任何代码
- 不将 §4.4 的断链表述为"待恢复的任务"，也不重开 P2

---

## 10. 证据口径

- §4.2 / §4.3 / §4.4 的全部判定均来自 **source reading**：未构建、未运行、未截帧验证。链路的**存在性与接线位置**可由此确证，但**实际出图正确性未经动态验证**。
- **§4.7 为例外** —— 该节为 Phase 5.2 的**动态验证**结果，含真实窗口、真实 GL context、真实像素与 resize 往返证据。

与本文档其他章节的差异，已明确标注：

- §5.3（PhysicsSync）有**动态 reachability 证据**（零赋值、零实例化、零测试引用）
- §2 的 target 数量经 `git` 索引核对
- §4.2 / §4.3 / §4.4 渲染链路为纯静态阅读，**无运行证据**；§4.7 提供了其中 windowed path 的动态证据
- §6.1 三项 characterization 为**静态核查**（源码 + grep 可达性），未构建、未运行。其结论是"良性 / 死代码 / 未完成实现"，均**不含**动态验证成分

> **§6.1 记录的三次自我更正**均为**静态证据推翻静态初判**，不涉及运行验证。若日后有人复核，应注意这三处的"错误"是初判阶段未核对可达性与调用图所致，而非后续动态实验的结论。
- 不声称任何验证经过构建或运行