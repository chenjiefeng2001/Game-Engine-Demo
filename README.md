# 🎮 Game Engine Demo

<p align="center">
  <h3 align="center">一个模块化游戏引擎演示项目 · 技术验证与原型开发</h3>
  <p align="center">
    <a href="#快速开始">快速开始</a>
    ·
    <a href="#引擎架构">引擎架构</a>
    ·
    <a href="#模块详解">模块详解</a>
    ·
    <a href="#沙盒示例">沙盒示例</a>
    ·
    <a href="#分支策略">分支策略</a>
    ·
    <a href="#构建说明">构建说明</a>
    ·
    <a href="#许可证">许可证</a>
    <br />
    <br />
    <img src="https://img.shields.io/badge/C%2B%2B-20-blue" alt="C++20"/>
    <img src="https://img.shields.io/badge/status-experimental-red" alt="status: experimental"/>
    <img src="https://img.shields.io/badge/license-MIT-blue" alt="license: MIT"/>
    <img src="https://img.shields.io/badge/OpenGL-4.6-green" alt="OpenGL 4.6"/>
    <img src="https://img.shields.io/badge/Vulkan-1.3-purple" alt="Vulkan 1.3"/>
    <img src="https://img.shields.io/badge/Box2D-3.0-orange" alt="Box2D 3.0"/>
    <img src="https://img.shields.io/badge/Jolt%20Physics-5.5-brightgreen" alt="Jolt Physics 5.5"/>
    <img src="https://img.shields.io/badge/OpenAL--Soft-1.25-lightgrey" alt="OpenAL Soft 1.25"/>
    <img src="https://img.shields.io/badge/Dear%20ImGui-1.91-cyan" alt="Dear ImGui 1.91"/>
  </p>
</p>

---

## 📖 项目简介

**Game Engine Demo** 是一个实验性的模块化游戏引擎演示项目，使用 **C++20** 标准开发。项目采用分层解耦架构，通过纯虚接口层（RHI 风格）将核心逻辑与具体实现分离，已集成：

- **OpenGL 4.6** 渲染（2D 精灵批处理 + 3D 光照管线 + 延迟渲染 / SSAO）
- **Vulkan 1.3** 渲染后端（95% 完成 — Dynamic Rendering / VMA / Bindless Descriptor，跨平台首选）
- **D3D12** 渲染后端（⏸️ 休眠状态 — 777 行完整 IRHIDevice + D3D12MA，基础架构已跑通但功能未对齐 Vulkan，暂不作为日常迭代重心）
- **Jolt Physics 5.5** 3D 物理引擎（多线程 JobSystem 适配，v4.0-v7.0 迭代）
- **Box2D 3.0** 2D 物理模拟（刚体、碰撞、关节）
- **Bare2D** 纯 CPU 2D 物理引擎（自制，1302 行完整物理管线）
- **动画系统**（骨骼蒙皮、混合树、IK、动画状态机、重定向）
- **OpenAL Soft** 3D 空间音频
- **Dear ImGui + ImGuizmo** 编辑器界面
- **nlohmann/json** 场景序列化
- **Tracy** 性能剖析
- **FreeType** 字体渲染

> ⚠️ 该项目仍处于快速迭代阶段，API 可能发生破坏性变化，不建议直接用于正式项目。

---

## 🚀 快速开始

### 环境要求

| 依赖 | 版本 |
|------|------|
| CMake | ≥ 3.20 |
| 编译器 | MSVC 2022 (Visual Studio 17) / GCC 12+ / Clang 16+ |
| C++ 标准 | C++20 或更高 |
| Vulkan SDK | 可选（构建 Vulkan 后端时需要） |

### Windows 构建

```powershell
# 克隆仓库（含子模块）
git clone --recursive https://github.com/chenjiefeng2001/game-engine-demo.git
cd game-engine-demo

# 如果已克隆但未拉取子模块：
# git submodule update --init --recursive

# 配置项目（使用 Visual Studio 2022）
cmake -B build -G "Visual Studio 17 2022"

# 构建引擎核心库
cmake --build build --target EngineCore --config Release

# 构建并运行沙盒示例
cmake --build build --target AudioPhysicsSandbox --config Release
./build/sandbox/AudioPhysicsSandbox/Release/AudioPhysicsSandbox.exe
```

### Linux / macOS 构建

```bash
git clone https://github.com/chenjiefeng2001/game-engine-demo.git
cd game-engine-demo
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target AudioPhysicsSandbox
./build/sandbox/AudioPhysicsSandbox/AudioPhysicsSandbox
```

---

## 🏗️ 引擎架构

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          Sandbox Layer                                   │
│  (沙盒示例, 直接使用引擎 API 构建场景和游戏逻辑)                          │
├──────────────────────────────────────────────────────────────────────────┤
│                           Engine API                                     │
│  IGraphicsFactory · IWindow · IRenderContext · IPhysicsWorld             │
│  IPhysicsWorld3D · IP hysicsDebugDraw · IAudioEngine · Scene            │
│  GameObject · Component · JobSystem · SubsystemManager · EventBus        │
├──────────────┬──────────────┬──────────────┬─────────────────────────────┤
│  OpenGL      │  Vulkan      │  Box2D       │  Jolt Physics v5.5          │
│  Backend 95% │  Backend 95% │  Backend     │  v4.0: 碰撞管道/四元数      │
│  GL46CmdList │  VkDevice    │  Box2DPhysWld│  v5.0: TLS無鎖/SetShape     │
│  GL46SwapChn │  VkCmdList   │  Box2DPhysBd │  v6.0: CharacterController  │
│  GL46Device  │  VkSwapChain │  Box2DJoint  │  v6.0: Joint3D 系统        │
│  OpenGLShader│  VkPSO       │  PhysicsDebug│  v7.0: 關節生命週期/Collision│
│  OpenGLTex   │  BindlessDesc│  Draw        │  Listener/ CCT 固定步進     │
│  GLResources │  VMA+VkPSOCch│  Bare2D      │  JoltDebugRenderer(TLS)    │
│              │              │  (自製物理)   │  LockFreeEventQueue(MPSC)  │
├──────────────┴──────────────┴──────────────┴─────────────────────────────┤
│  Animation                 │  Rendering / RHI                            │
│  Skeleton · SkinnedMesh    │  SceneRenderer · RenderQueue                │
│  AnimationController       │  AntiAliasing · BufferVisualization         │
│  AnimationBlend · BlendTree│  GeometryDebug · HelperToggles              │
│  IK · ConstraintSolver     │  PrimitiveBatch / IPrimitiveBatch           │
│  AnimationRetarget         │  PhysicsLayers · LockFreeEventQueue         │
│  AnimStateMachine          │  FixedTimestepAccumulator                   │
│  TransformSystem           │  PhysicsSyncSystem (三級同步)              │
├────────────────────────────┴─────────────────────────────────────────────┤
│  Physics PAL (Physics Abstraction Layer — v4.0-v7.0)                    │
│  2D: IPhysicsWorld · IPhysicsBody · IJoint · IForceGenerator           │
│  3D: IPhysicsWorld3D · IPhysicsBody3D · IJoint3D · ICharacterController│
│  Batch API: BatchSetKinematicTargets / BatchGetTransforms              │
│  ECS: Joint3DComponent · CollisionListenerComponent · PhysicsSyncSystem│
│  Data: PhysicsDefs · PhysicsDefs3D · PhysicsLayers · PhysicsMaterial   │
│  Events: LockFreeEventQueue (MPSC) · ContactListenerImpl (BodyID-only)│
│  Debug: IPhysicsDebugDraw · IPhysicsDebugDraw3D · JoltDebugRenderer   │
├──────────────────────────────────────────────────────────────────────────┤
│  Debug                       │  Editor                                  │
│  CrashHandler · StackTrace   │  EngineEditor                            │
│  ScreenshotCapture           │  MainMenuBar · Toolbar                   │
│  CrashContext                │  Viewport · ContentBrowser                │
│  Profiler · MemoryTracker    │  AssetBrowser · DepGraph                 │
│  ConsoleVariableRegistry     │  SceneHierarchy · Inspector              │
│                              │  ConsolePanel · PerformanceWindow        │
│                              │  MemoryPanel                             │
├──────────────────────────────┴──────────────────────────────────────────┤
│                       Third Party Libraries                              │
│  GLFW · glad · glm · Box2D · JoltPhysics · OpenAL Soft · stb           │
│  imgui · imguizmo · nlohmann/json · spdlog · freetype · tracy          │
│  yaml-cpp · VMA · SPIRV-Cross · shaderc                                 │
└──────────────────────────────────────────────────────────────────────────┘
```

### 设计原则

- **接口与实现分离** — 核心层只依赖纯虚接口（`IPhysicsWorld`、`IPhysicsWorld3D`、`IAudioEngine` 等），实现层在编译时注入
- **多后端架构** — 同时支持 OpenGL 4.6 和 Vulkan 1.3 渲染后端，物理层支持 Box2D (2D)、Bare2D (自製) 和 Jolt Physics (3D)
- **工厂模式** — 通过 `IGraphicsFactory` 统一创建窗口、上下文、着色器、精灵批处理等资源
- **组件化** — `GameObject` 通过 `TransformComponent`、`SpriteComponent`、`PhysicsComponent`、`AudioSourceComponent` 等组合行为
- **SubsystemManager** — 管理所有子系统初始化和关闭的生命周期，支持阶段式启动
- **混合驱动调度** — 每个子系统可独立声明更新策略（可变步长/固定步长/限频/事件驱动/手动），由 `Application` 统一调度
- **JobSystem** — 基于线程池的任务级并行调度，支持 `ParallelFor` 和 `Wait()` 工作窃取；Jolt Physics 通过 `JoltJobSystemAdapter` 接入
- **注册式序列化** — 组件在静态初始化期自动注册到 `JsonSerializer`，新增组件无需修改序列化器代码
- **RAII 资源管理** — 使用 `shared_ptr` / 智能指针管理 OpenGL 纹理、OpenAL 缓冲区和物理体的生命周期
- **ECS 架构（实验性）** — 在 `ECS` 分支上提供 Archetype-based ECS（EntityManager + Chunk + Archetype + ECB + Query）
- **Physics PAL** — 纯虚接口隔离物理引擎，不暴露 Box2D/Jolt/PhysX 类型，支持运行时后端切换

---

## 📦 模块详解

### 🖼️ 渲染系统 — OpenGL 4.6

| 组件 | 说明 |
|------|------|
| `OpenGLContext` | OpenGL 上下文封装，管理视口和清除状态 |
| `OpenGLGraphicsFactory` | 工厂实现，创建窗口/着色器/纹理/批处理器 |
| `OpenGLSpriteBatch` | 2D 精灵批处理渲染器 |
| `OpenGLShader` | GLSL 着色器编译与链接（支持多阶段：顶点/几何/细分/片元） |
| `OpenGLTexture` | OpenGL 纹理对象管理 |
| `GLVertexBuffer` / `GLIndexBuffer` / `GLVertexArray` | GPU 几何数据封装 |
| `OrthographicCamera` | 正交投影相机，支持缩放和移动 |
| `Shader` / `Texture` / `VertexBuffer` / `IndexBuffer` / `VertexArray` | 渲染资源抽象接口 |
| `ISpriteBatch` / `IPrimitiveBatch` | 批处理抽象接口（精灵 + 图元） |
| `TextureManager` | 纹理缓存管理器，自动去重 |

### 🖼️ 渲染系统 — Vulkan 1.3

| 组件 | 说明 |
|------|------|
| `VulkanDevice` | Vulkan 设备封装（VkInstance / VkPhysicalDevice / VkDevice / VMA / Volk） |
| `VulkanCommandList` | Vulkan 命令列表（Begin / End / Draw / DrawIndexed / Barrier / Viewport / Scissor） |
| `VulkanSwapChain` | 交换链（Present / Resize / FrameInFlight 三帧飞行） |
| `VulkanPipelineState` | 管线状态（SPIR-V 反射 + Dynamic Rendering + PSO Factory） |
| `VulkanPipelineLayoutCache` | PipelineLayout 缓存（SPIRV-Cross 反射 UBO / Sampler / SSBO / PushConstant） |
| `BindlessAllocator` | Bindless Descriptor Indexing（4096 binding，UpdateAfterBind，VariableDescriptorCount） |
| `VulkanBuffer` | VMA Buffer（Vertex / Index / Uniform / Storage） |
| `VulkanTexture` | VMA Image（支持各种格式和 mip levels） |
| `VulkanQueue` | 命令队列（Graphics / Compute） |
| `VulkanFrameResource` | 每帧资源（Fence / Semaphore / CommandPool / DynamicUBO） |
| `VulkanLoader` | Volk 加载器（Instance / Device 函数加载） |
| `PSOCache` | PSO 去重缓存（64-bit hash，线程安全 Find / Store） |
| `DescriptorRingBuffer` | 描述符环缓冲区（支持多帧飞行） |
| `GPUProfiler` | GPU 性能剖析（Timestamp Query） |

### 🧱 物理系统 — 2D (Box2D + Bare2D)

| 组件 | 说明 |
|------|------|
| `IPhysicsWorld` | 2D 物理世界抽象接口 |
| `IPhysicsBody` | 刚体抽象接口 |
| `IJoint` | 关节抽象接口（支持鼠标/距离/旋转/滑动/焊接/轮式/弹簧关节） |
| `IForceGenerator` | 力发生器抽象（重力/空气阻力） |
| `Box2DPhysicsWorld` | Box2D 3.0 物理世界封装 |
| `Box2DPhysicsBody` | Box2D 刚体封装 |
| `Bare2DPhysicsWorld` | 纯 CPU 2D 物理世界（完整 SAT + Sequential Impulse + CCD） |
| `Bare2DPhysicsBody` | 自制 2D 刚体（多 Fixture / 力累加器 / 休眠 / 插值） |
| `PhysicsComponent` | 可挂载到 GameObject 的物理组件 |
| `PhysicsDefs` | 纯数据结构（BodyDef/ShapeDef/JointDef/ContactManifold/CollisionLayers） |

### 🧱 物理系统 — 3D (Jolt Physics) — v4.0 至 v7.0 迭代

| 版本 | 核心改进 | 文件 |
|------|----------|------|
| **v4.0** | 四元数内部存储 + 碰撞安全管道 + Batch API + Collider Dirty | `TransformComponent` / `LockFreeEventQueue` / `IPhysicsWorld3D` |
| **v5.0** | TLS 无锁 DebugRenderer + SetShape 惯性张量重算 + 3 hotspot fix | `JoltDebugRenderer` (TLS) / `JoltPhysicsBody::SetShape` |
| **v6.0** | CharacterController + 6 种 3D 关节 + ECS 绑定 + CollisionListener | `JoltCharacterController3D` / `JoltJoint3D` / `Joint3DComponent` |
| **v7.0** | 关节生命周期（延迟创建+级联销毁）+ CCT 固定步进 + 碰撞路由 | `PhysicsSyncSystem` (v7.0) |

**当前接口与组件概览：**

| 组件 | 说明 |
|------|------|
| `IPhysicsWorld3D` | 3D 物理世界抽象接口（Init / Step / CreateBody / RayCast / Query） |
| `IPhysicsBody3D` | 3D 刚体抽象接口（变换/运动/力/冲量/SetShape/碰撞过滤/休眠） |
| `IJoint3D` | 3D 关节抽象接口（Hinge/Slider/Ball/Fixed/Distance/Spring/SixDOF） |
| `ICharacterController3D` | 虚拟角色控制器接口（基于 JPH::CharacterVirtual） |
| `IPhysicsDebugDraw3D` | 3D 调试绘制接口（Line/Sphere/Box/Capsule/Text/Axes） |
| `JoltPhysicsWorld` | Jolt Physics 5.5 实现（BodyLock 安全管道，6 种 Joint 创建） |
| `JoltPhysicsBody` | Jolt Body 封装（GetRotationQuat/SetShape/SetCollisionFilter） |
| `JoltJoint3D` | 6 种 JPH 约束封装（SetLimits/EnableMotor/GetCurrentAngle） |
| `JoltCharacterController3D` | CharacterVirtual::ExtendedUpdate 完整封装（爬坡/爬楼梯） |
| `JoltDebugRenderer` | TLS 无锁 DebugRenderer（16 线程独立 Buffer，无 hash 碰撞） |
| `JoltJobSystemAdapter` | JPH::JobSystem → Engine::JobSystem 适配 |
| `JoltContactListener` | Jolt → Engine 碰撞回调桥接（BodyID-only 安全管道） |
| `LockFreeEventQueue` | MPSC 无锁碰撞事件队列（CAS 原子对齐，只传 BodyID 不传指针） |
| `FixedTimestepAccumulator` | 固定步长累加器（防螺旋式死亡，渲染插值 alpha） |
| `PhysicsSyncSystem` | 三级同步管线（ECS→Physics→Step→Physics→ECS + 关节管理） |
| `PhysicsSystemManager` | 统一管理 2D + 3D 物理世界 + BackendType 枚举 |

**支持的回调：**
- **碰撞开始 / 结束 / 持续** `SetContactBegin/End/PersistCallback` — 带 BodyID 安全路由
- **碰撞滤波** `SetContactPreSolveCallback` — 运行时控制碰撞
- **ECS 碰撞监听** `CollisionListenerComponent` — OnCollisionEnter/Exit 回调

### 🔊 音频系统 (`Engine::OpenAL`)

| 组件 | 说明 |
|------|------|
| `IAudioEngine` | 音频引擎抽象接口 |
| `IAudioSource` | 3D 音源抽象接口 |
| `IAudioBuffer` | 音频缓冲区抽象接口 |
| `OpenALAudioEngine` | OpenAL Soft 引擎封装 |
| `OpenALAudioSource` | OpenAL 音源封装 |
| `OpenALAudioBuffer` | OpenAL 缓冲区封装 |
| `AudioClip` | 高层音频资源类（加载文件/内存 → 自动解码 → 上传 OpenAL） |
| `AudioClipManager` | 音频剪辑缓存管理器 |
| `AudioSourceComponent` | 可挂载到 GameObject 的音频源组件 |
| `AudioSystem` | 便捷工具 — `PlayOneShot` / `UpdateOneShots` 一次性音效播放 |
| `AudioLoader` | WAV/OGG 文件解码器 |
| `Listener` | OpenAL 听者参数管理 |

支持的音频格式：`.wav`、`.ogg`（通过 stb_vorbis 解码）

### 🎮 输入系统

| 组件 | 说明 |
|------|------|
| `IWindow` | 窗口抽象接口 |
| `Input` | 输入状态查询（键盘/鼠标） |
| `InputManager` | 输入动作系统（按键映射与事件绑定） |
| `GlfwWindow` | GLFW 窗口实现（支持事件回调、窗口缩放自适应、输入抢占） |
| `GlfwInput` | GLFW 输入轮询实现 |

### 🎬 场景与游戏对象

| 组件 | 说明 |
|------|------|
| `Scene` | 场景管理容器，维护游戏对象列表与更新/渲染遍历 |
| `GameObject` | 游戏对象基类，持有 Transform/Sprite/Physics 组件，支持 `ForEachComponent` |
| `Component` | 组件基类，提供 `Serialize`/`Deserialize` 虚方法供序列化 |
| `TransformComponent` | 位置/四元数旋转/缩放组件（v4.0: 内部 Quat，避免万向锁） |
| `TransformSystem` | 版本号驱动的线性批处理世界矩阵更新（Lazy Validation，O(N) 单次扫描） |
| `SpriteComponent` | 精灵渲染组件，支持 JSON 序列化 |
| `PhysicsComponent` | 物理组件（连接 GameObject 与 IPhysicsBody），支持 JSON 序列化 |
| `AudioSourceComponent` | 音频源组件（可挂载到 GameObject） |

### 🧩 Core 基础设施

| 组件 | 说明 |
|------|------|
| `Application` | 引擎应用基类，管理主循环、子系统生命周期、UI 集成 |
| `SubsystemManager` | 子系统管理器，支持阶段式启动/关闭 |
| `SubsystemConfig` | 子系统更新策略配置（可变/固定/限频/事件驱动/手动） |
| `JobSystem` | 线程池任务级并行调度，支持 `ParallelFor` 和 `Wait()` 工作窃取 |
| `FileSystem` | 跨平台文件系统抽象，支持 VFS 挂载点、异步 I/O、目录扫描 |
| `AsyncStream` | 异步文件流，支持分块读取、流式加载和后台预取 |
| `FileStream` | 文件流包装器，将 VFS 适配为 stb_image/stb_vorbis 等库的回调 |
| `Config` | JSON 配置系统，支持 section.key 组织、默认模板和恢复 |
| `EngineSettings` | 引擎全局设置 |
| `UserSettings` | 用户自定义设置 |
| `Log` | 日志系统（基于 spdlog） |
| `MenuManager` | 菜单管理器 |
| `Time` | 时间管理 |
| `IGraphicsFactory` | 图形工厂抽象接口 |
| `IUIManager` | UI 管理器接口 |
| `IRenderContext` | 渲染上下文接口 |
| `EventBus` | 类型安全的发布-订阅事件总线（支持 ECS 集成事件） |

### 📦 资源管理系统

| 组件 | 说明 |
|------|------|
| `ResourceManager` | 统一资源管理器，模板方法 `Load<T>()` 自动匹配，引用计数管理 |
| `Resource` | 资源基类 |
| `ResourceRegistry` | 资源类型注册表 |
| `ResourceGUID` | 资源全局唯一标识 |
| `ResourcePoolAllocator` | 资源池分配器 |
| `AssetDatabase` | 资产数据库 |
| `AssetRegistry` | 资产注册表（GUID 驱动，支持 .physmat 材质预留） |
| `AssetPipeline` | 资产处理管线 |
| `SceneSerializer` | 场景序列化器（支持 yaml-cpp） |
| `AsyncLoadData` | 异步加载数据结构 |
| `FileWatcher` | 文件变更监视器，后台轮询 mtime，支持资源热加载 |

### 🎭 动画系统 (`Engine::Animation`)

| 组件 | 说明 |
|------|------|
| `Skeleton` | 骨骼数据结构，维护骨骼层级和绑定姿势 |
| `SkinnedMesh` | 蒙皮网格，支持线性混合蒙皮（LBS） |
| `SkinningComponent` | 可挂载到 GameObject 的蒙皮组件 |
| `Bone` | 骨骼节点，支持本地/模型空间变换 |
| `AnimationController` | 动画控制器，管理动画播放与过渡 |
| `AnimationInstance` | 动画实例，管理单条动画的播放状态 |
| `AnimationClip` → `AnimationResource` | 动画资源（关键帧数据） |
| `AnimationTrack` | 动画轨道（位置/旋转/缩放通道） |
| `AnimationPose` | 动画姿势（骨骼局部变换集合） |
| `AnimationBlend` | 动画混合（线性/叠加混合） |
| `BlendTree` | 混合树节点系统 |
| `BlendSpace1D` / `BlendSpace2D` | 混合空间（1D/2D 参数化混合） |
| `AnimStateMachine` | 动画状态机（状态转换与条件） |
| `AnimationLayer` | 动画分层（基础层/覆盖层） |
| `AnimationBatch` | 动画批处理，GPU 蒙皮优化 |
| `AnimationCompression` | 动画压缩（关键帧降采样/量化） |
| `AnimationGlobalTimeline` | 全局动画时间线管理 |
| `AnimationLocalTimeline` | 局部动画时间线 |
| `AnimationManager` | 动画管理器，统一更新与调度 |
| `AnimationPipeline` | 动画管线（采样→混合→IK→提交） |
| `AnimationRetarget` | 动画重定向（不同骨骼间的动画映射） |
| `IK` | 反向动力学（Two-Bone IK / FABRIK） |
| `Constraint` | 约束基类 |
| `ConstraintSolver` | 约束求解器 |
| `ConstraintTarget` | 约束目标 |
| `Locator` | 定位器（用于挂载点 / 附着点） |
| `BlendMask` | 混合遮罩（控制每根骨骼的混合权重） |

### 🖥️ RHI / Rendering 层

| 组件 | 说明 |
|------|------|
| `IRHIDevice` | 渲染硬件设备抽象（CreateBuffer/CreateTexture/CreatePSO/CreateSwapChain） |
| `IRHICommandList` | 命令列表抽象（Begin/End/Draw/Barrier/Bind） |
| `IRHICommandQueue` | 命令队列抽象（ExecuteCommandLists/WaitIdle） |
| `IRHISwapChain` | 交换链抽象（Present/Resize/GetBackBuffer） |
| `IRHIBuffer` / `IRHITexture` / `IRHIPipelineState` | 渲染资源抽象 |
| `PSOCache` | 全局 PSO 去重缓存，64-bit hash，线程安全 |
| `SceneRenderer` | 场景渲染器 — 管理渲染队列和 pass 执行 |
| `RenderQueue` | 渲染队列 — 排序/剔除/提交 |
| `DynamicUBOAllocator` | 动态 Uniform Buffer 分配器 |
| `AntiAliasingConfig` / `AntiAliasingCaps` / `AntiAliasingTypes` | 抗锯齿配置与能力查询 |
| `BufferVisualization` | 缓冲区可视化调试工具 |
| `GeometryDebug` | 几何调试绘制 |
| `HelperToggles` | 辅助调试开关 |
| `ComputeCullingPass` | GPU Drivan 可见性剔除计算 Pass |
| `IDBufferPass` | 延迟渲染 ID Buffer Pass |

### 🐛 调试系统

| 组件 | 说明 |
|------|------|
| `CrashHandler` | 崩溃报告系统 — MiniDump + 截图 + 分配器状态转储 |
| `StackTrace` | 堆栈回溯（基于 DbgHelp） |
| `ScreenshotCapture` | 崩溃时自动截屏 |
| `CrashContext` | 崩溃上下文数据结构 |
| `Profiler` | Tracy 性能剖析集成 |
| `OpenGLPhysicsDebugDraw` | 2D 物理调试可视化（碰撞体轮廓/关节/质心） |
| `OpenGLPhysicsDebugDraw3D` | 3D 物理调试可视化（Sphere/Box/Capsule/Line/Axes） |
| `JoltDebugRenderer` | TLS 无锁 Jolt 原生调试渲染转发 |
| `ConsoleVariable` | 控制台变量系统，支持运行时修改 |
| `ConsoleCommandRegistry` | 控制台命令注册表 |

---

## 🌿 分支策略

本项目采用双分支策略，将实验性 ECS 架构与稳定主干分离：

### `master` 分支（当前）
**稳定的主分支** — 使用 OOP 风格的 `GameObject` + `Component` 模型。

- 渲染：OpenGL 4.6（生产级）+ Vulkan 1.3（~95%）
- 物理：Box2D 3.0（2D）+ Jolt Physics 5.5（3D，v4.0-v7.0 迭代完成）
- 实体：`GameObject` — 组件化 `shared_ptr<Component>` 容器
- 变换：`TransformComponent`（Quat）+ `TransformSystem`（版本号批处理）
- 事件：`EventBus` — 类型安全 Pub/Sub

### `ECS` 分支（实验性）
**ECS 架构实验分支** — 使用 Archetype-based 数据导向设计，同步 master 分支所有物理改进。

- 64-bit Generational EntityHandle（32-bit Index + 32-bit Generation）
- 16KB Cache-aligned Chunk 内存块（ComponentMeta 安全处理非 POD 类型）
- Archetype 组件签名分类 + 自动迁移
- EntityCommandBuffer（延迟结构性变更，解决遍历安全性）
- Query 引擎（Archetype 匹配缓存 + Chunk 迭代器）
- SparseSet 实体池（O(1) 分配/释放）
- ECSBridge（GameObject ↔ EntityHandle 双向映射，向后兼容）
- **ECS 物理同步**: `PhysicsSyncSystem` 三级同步管线、`Joint3DComponent`、`CollisionListenerComponent`

> ECS 分支的变更会定期合并到 master（仅合并非 ECS 部分，如渲染后端、物理引擎等通用改进）。

---

## 🧪 沙盒示例

| 目标 | 说明 |
|------|------|
| **`Sandbox`** | 基础应用入口，仅创建窗口和运行主循环 |
| **`InputTest`** | 输入系统测试 — WASD/鼠标/按键映射 |
| **`SpriteBatchTest`** | 精灵批处理渲染性能测试 |
| **`GameObjectTest`** | 游戏对象 / Transform / Sprite 组件测试 |
| **`TextureManagerTest`** | 纹理缓存与资源管理测试 |
| **`PhysicsTest`** | Box2D 物理集成测试 — 碰撞/堆叠/关节/鼠标拖拽/调试绘制 |
| **`BarePhysicsTest`** | 无渲染的裸物理测试，仅验证物理模拟正确性 |
| **`AudioTest`** | OpenAL 3D 空间音频测试 — WASD 移动音源/音高音量控制 |
| **`AudioSystemTest`** | 音频系统组件集成测试 |
| **`CollisionAudioTest`** | 碰撞音效测试 — 不同材质(Stone/Wood/Metal)碰撞发声 |
| **`ImGuiDemo`** | Dear ImGui 集成演示 — Docking 布局/基本控件/交互 |
| **`ImGuiTest`** | ImGui 功能测试 — 窗口管理/输入抢占/性能窗口 |
| **`SerializationTest`** | 场景 JSON 序列化/反序列化单元测试 |
| **`ComplexSceneTest`** | 复杂场景序列化与重建测试 |
| **`MarioDemo`** | 🎯 超级马里奥 Demo — 2D 平台游戏完整实现，含 BGM |
| **`AudioPhysicsSandbox`** | 🎯 综合演示 — 物理+音频+输入 |
| **`EditorDemo`** | 🎯 编辑器框架演示 — EngineEditor 全功能面板 |
| **`SystemTest`** | 🎯 系统综合测试 — JobSystem/FileSystem/Config/混合调度 |
| **`_3DTest`** | 🎯 3D 渲染测试 — 3D 光照 / 延迟渲染管线验证 |
| **`AnimationTest`** | 🎯 动画系统测试 — 骨骼/蒙皮/混合树基础功能 |
| **`AnimationDemo`** | 🎯 动画系统演示 — 骨骼动画播放与混合展示 |
| **`CrashTest`** | 崩溃报告测试 — 触发 SEH 异常验证 CrashHandler |

---

## 📁 项目结构

```
game-engine-demo/
├── assets/
│   ├── shaders/                      # GLSL / SPIR-V 着色器
│   ├── sounds/                       # 音频资源
│   ├── scenes/                       # 场景文件（.json / .yaml）
│   └── textures/                     # 纹理贴图
├── engine/                           # 引擎核心库 (EngineCore)
│   ├── include/Engine/
│   │   ├── Animation/                # 动画系统
│   │   ├── Audio/                    # 音频工具
│   │   ├── Box2D/                    # Box2D 物理实现
│   │   ├── Core/                     # 抽象接口层与核心工具
│   │   │   ├── ECS/                  # ECS 架构（PhysicsComponents/SyncSystem）
│   │   │   ├── GameObject/           # 游戏对象与组件（Transform v4.0 四元数）
│   │   │   ├── Physics/              # 物理接口（IPhysicsWorld/3D + 数据结构）
│   │   │   ├── RHI/                  # 渲染硬件接口（IRHIDevice/List/SwapChain）
│   │   │   └── Resources/            # 资源管理（AssetRegistry/GUID/SceneSerializer）
│   │   ├── Debug/                    # 调试系统
│   │   ├── Editor/                   # 编辑器框架
│   │   ├── Jolt/                     # Jolt Physics（World/Body/Joint3D/DebugRenderer/
│   │   │                               CharacterController + v4.0-v7.0 迭代）
│   │   ├── OpenAL/                   # OpenAL 音频实现
│   │   ├── OpenGL/                   # OpenGL 渲染实现
│   │   ├── Platform/                 # 平台层（GLFW 窗口/输入）
│   │   ├── Rendering/                # 渲染管线（SceneRenderer/ComputeCulling/IDBuffer）
│   │   ├── Vulkan/                   # Vulkan 渲染实现
│   │   └── Types.h / Config.h / EventBus.h / JobSystem.h / ...
│   └── src/                          # 引擎源码
│       ├── Jolt/                     # Jolt 实现（+JoltDebugRenderer/JoltJoint3D/
│       │                               JoltCharacterController3D）
│       ├── Core/ECS/                 # PhysicsSyncSystem v7.0（关节生命周期/CCT 步进）
│       └── ...
├── sandbox/                          # 沙盒测试可执行文件
├── docs/                             # 项目文档（Physics-Subsystem-Summary.md）
├── third_party/                      # 第三方库
└── CMakeLists.txt / LICENSE.txt / README.md
```

---

## 🧰 第三方依赖

| 库 | 版本 | 用途 |
|----|------|------|
| [Box2D](https://github.com/erincatto/box2d) | 3.0 | 2D 物理模拟 |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 5.5 | 3D 物理引擎（多线程 JobSystem） |
| [glfw](https://github.com/glfw/glfw) | 最新 | 跨平台窗口创建和输入处理 |
| [glad](https://github.com/Dav1dde/glad) | 2+ | OpenGL 4.6 函数加载 |
| [glm](https://github.com/g-truc/glm) | 1.1+ | 图形数学库 |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 1.25.2 | 3D 空间音频 |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91+ | 即时模式 GUI 框架 |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 最新 | 3D 变换操纵器 |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON 序列化 |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | 最新 | YAML 序列化（场景/材质） |
| [spdlog](https://github.com/gabime/spdlog) | 1.x | 高性能日志库 |
| [stb](https://github.com/nothings/stb) | 最新 | 纹理加载 + 音频解码 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 3.x | Vulkan 显存管理 |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | 最新 | SPIR-V 着色器反射 |
| [FreeType](https://github.com/freetype/freetype) | 最新 | 字体栅格化 |
| [Tracy](https://github.com/wolfpld/tracy) | 最新 | 实时性能剖析 |
| [Volk](https://github.com/zeux/volk) | 最新 | Vulkan 元加载器（内置） |
| [shaderc](https://github.com/google/shaderc) | 最新 | GLSL→SPIR-V 编译（可选） |

---

## ⚠️ 注意事项

- 项目处于**快速迭代阶段**，API 和架构可能随时变更
- 渲染后端支持 **OpenGL 4.6**（生产级）和 **Vulkan 1.3**（~95%，**主力后端**），**D3D12** 处于休眠状态
- **物理引擎 v4.0-v7.0 已全部完成**：四元数 → 碰撞管道 → Batch API → TLS DebugRenderer → SetShape → CharacterController → Joint3D → 碰撞路由
- 零 stub 承诺：CreateJoint/DestroyJoint/CharacterVirtual 全生产级实现
- ECS 架构在 `ECS` 分支上维护，`master` 分支使用 OOP `GameObject`+`Component` 模型
- Windows 构建需要安装 **Visual Studio 2022**（MSVC v14.4+）
- Vulkan 后端需要安装 **Vulkan SDK**，cmake 会自动检测
- ImGui / JoltPhysics 使用 git submodule 引入，克隆时需 `--recursive` 或执行 `git submodule update --init --recursive`
- Tracy Profiler 在 Debug 配置下默认启用，Release 配置下自动关闭

---

## 📝 许可证

本项目采用 **MIT License** 开源。详见 [LICENSE.txt](./LICENSE.txt)。

项目仍处于试验阶段，不具有投入生产的可能，请慎重考虑使用。