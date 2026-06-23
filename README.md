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
    <a href="#构建说明">构建说明</a>
    ·
    <a href="#许可证">许可证</a>
    <br />
    <br />
    <img src="https://img.shields.io/badge/C%2B%2B-20-blue" alt="C++20"/>
    <img src="https://img.shields.io/badge/status-experimental-red" alt="status: experimental"/>
    <img src="https://img.shields.io/badge/license-MIT-blue" alt="license: MIT"/>
    <img src="https://img.shields.io/badge/OpenGL-4.6-green" alt="OpenGL 4.6"/>
    <img src="https://img.shields.io/badge/Box2D-3.0-orange" alt="Box2D 3.0"/>
    <img src="https://img.shields.io/badge/OpenAL--Soft-1.25-lightgrey" alt="OpenAL Soft 1.25"/>
    <img src="https://img.shields.io/badge/Dear%20ImGui-1.91-cyan" alt="Dear ImGui 1.91"/>
  </p>
</p>

---

## 📖 项目简介

**Game Engine Demo** 是一个实验性的模块化游戏引擎演示项目，使用 **C++20** 标准开发。项目采用分层解耦架构，通过纯虚接口层（RHI 风格）将核心逻辑与具体实现分离，已集成：

- **OpenGL 4.6** 渲染（2D 精灵批处理 + 3D 光照管线 + 延迟渲染 / SSAO）
- **动画系统**（骨骼蒙皮、混合树、IK、动画状态机、重定向）
- **Box2D 3.0** 物理模拟（刚体、碰撞、关节）
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
┌────────────────────────────────────────────────────────────────────┐
│                        Sandbox Layer                               │
│  (沙盒示例, 直接使用引擎 API 构建场景和游戏逻辑)                    │
├────────────────────────────────────────────────────────────────────┤
│                         Engine API                                 │
│  IGraphicsFactory · IWindow · IRenderContext · IPhysicsWorld       │
│  IAudioEngine · Scene · GameObject · Component · Serializer        │
│  JobSystem · SubsystemManager · ResourceManager · EngineEditor     │
├──────────────┬──────────────┬──────────────┬───────────────────────┤
│  OpenGL      │  Box2D       │  OpenAL      │  Core                 │
│  Backend     │  Backend     │  Backend     │  (抽象层 + 工具)      │
│  OpenGLContext│Box2DPhysWorld│OpenALAudioEng│  SubsystemMgr         │
│  OpenGLSprite │Box2DPhysBody │OpenALAudioSrc│  JobSystem            │
│  OpenGLShader │Box2DJoint    │OpenALAudioBuf│  FileSystem           │
│  OpenGLTexture│PhysicsDebug  │AudioClip     │  Config               │
│  GLResources  │Draw          │AudioLoader   │  ResourceMgr          │
│  (VAO/VBO/IBO)│              │Listener      │  StackAllocator       │
├──────────────┴──────────────┴──────────────┴───────────────────────┤
│  Animation                 │  Rendering / RHI                      │
│  Skeleton · SkinnedMesh    │  SceneRenderer · RenderQueue          │
│  AnimationController       │  AntiAliasing · BufferVisualization   │
│  AnimationBlend · BlendTree│  GeometryDebug · HelperToggles        │
│  IK · ConstraintSolver     │  PrimitiveBatch                       │
│  AnimationRetarget         │  IPrimitiveBatch                      │
│  AnimStateMachine          │                                       │
├────────────────────────────┴───────────────────────────────────────┤
│  Debug                       │  Editor                             │
│  CrashHandler · StackTrace   │  EngineEditor                       │
│  ScreenshotCapture           │  MainMenuBar · Toolbar              │
│  CrashContext                │  Viewport · ContentBrowser          │
│  Profiler · MemoryTracker    │  AssetBrowser · DepGraph            │
│  ConsoleVariableRegistry     │  SceneHierarchy · Inspector         │
│                              │  ConsolePanel · PerformanceWindow   │
│                              │  MemoryPanel                        │
├──────────────────────────────┴─────────────────────────────────────┤
│                     Third Party Libraries                           │
│  GLFW · glad · glm · Box2D · OpenAL Soft · stb · imgui · imguizmo  │
│  nlohmann/json · spdlog · freetype · tracy                          │
└────────────────────────────────────────────────────────────────────┘
```

### 设计原则

- **接口与实现分离** — 核心层只依赖纯虚接口（`IPhysicsWorld`、`IAudioEngine` 等），实现层在编译时注入
- **工厂模式** — 通过 `IGraphicsFactory` 统一创建窗口、上下文、着色器、精灵批处理等资源
- **组件化** — `GameObject` 通过 `TransformComponent`、`SpriteComponent`、`PhysicsComponent`、`AudioSourceComponent` 等组合行为
- **SubsystemManager** — 管理所有子系统初始化和关闭的生命周期，支持阶段式启动
- **StackAllocator** — 子系统内存线性分配器，256KB 连续内存池，O(1) 分配，关闭时整块回收
- **混合驱动调度** — 每个子系统可独立声明更新策略（可变步长/固定步长/限频/事件驱动/手动），由 `Application` 统一调度
- **JobSystem** — 基于线程池的任务级并行调度，支持 `ParallelFor` 和 `Wait()` 工作窃取
- **注册式序列化** — 组件在静态初始化期自动注册到 `JsonSerializer`，新增组件无需修改序列化器代码
- **RAII 资源管理** — 使用 `shared_ptr` / 智能指针管理 OpenGL 纹理、OpenAL 缓冲区和物理体的生命周期

---

## 📦 模块详解

### 🖼️ 渲染系统 (`Engine::OpenGL`)

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

### 🧱 物理系统 (`Engine::Box2D`)

| 组件 | 说明 |
|------|------|
| `IPhysicsWorld` | 物理世界抽象接口 |
| `IPhysicsBody` | 刚体抽象接口 |
| `IJoint` | 关节抽象接口（支持鼠标/距离/旋转/滑动/焊接/轮式关节） |
| `Box2DPhysicsWorld` | Box2D 物理世界封装 |
| `Box2DPhysicsBody` | Box2D 刚体封装 |
| `PhysicsComponent` | 可挂载到 GameObject 的物理组件 |
| `PhysicsDefs` | 纯数据结构（BodyDef/ShapeDef/JointDef/ContactInfo） |
| `OpenGLPhysicsDebugDraw` | 物理调试可视化（碰撞体轮廓/关节/质心） |

支持的回调：
- **碰撞开始** `SetContactBeginCallback` — 碰撞发生时触发
- **碰撞结束** `SetContactEndCallback` — 碰撞分离时触发
- **碰撞持续** `SetContactPersistCallback` — 碰撞接触中每帧触发（带冲量）
- **碰撞滤波** `SetContactPreSolveCallback` — 运行时控制是否产生碰撞

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
| `AudioTypes` | 音频相关类型定义 |

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
| `TransformComponent` | 位置/旋转/缩放组件，支持层级变换 |
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

### 🗄️ 内存与容器

| 组件 | 说明 |
|------|------|
| `StackAllocator` | 线性/栈分配器，O(1) 分配，预分配 256KB 连续内存池 |
| `StackAllocatorAdaptor` | STL 分配器适配器，支持 `std::allocate_shared` |
| `IntrusiveList` | 侵入式双向链表 |
| `SmallVector` | 小向量优化（SSO） |
| `StackList` | 栈上链表 |
| `MemoryTracker` / `MemoryPanel` | 内存追踪与调试面板 |

### 📦 资源管理系统

| 组件 | 说明 |
|------|------|
| `ResourceManager` | 统一资源管理器，模板方法 `Load<T>()` 自动匹配，引用计数管理 |
| `Resource` | 资源基类 |
| `ResourceRegistry` | 资源类型注册表 |
| `ResourceGUID` | 资源全局唯一标识 |
| `ResourcePoolAllocator` | 资源池分配器 |
| `AssetDatabase` | 资产数据库 |
| `AssetPipeline` | 资产处理管线 |
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
| `SceneRenderer` | 场景渲染器 — 管理渲染队列和 pass 执行 |
| `RenderQueue` | 渲染队列 — 排序/剔除/提交 |
| `AntiAliasingConfig` / `AntiAliasingCaps` / `AntiAliasingTypes` | 抗锯齿配置与能力查询 |
| `BufferVisualization` | 缓冲区可视化调试工具 |
| `GeometryDebug` | 几何调试绘制 |
| `HelperToggles` | 辅助调试开关 |

### 🐛 调试系统

| 组件 | 说明 |
|------|------|
| `CrashHandler` | 崩溃报告系统 — MiniDump + 截图 + 分配器状态转储 |
| `StackTrace` | 堆栈回溯（基于 DbgHelp） |
| `ScreenshotCapture` | 崩溃时自动截屏 |
| `CrashContext` | 崩溃上下文数据结构 |
| `Profiler` | Tracy 性能剖析集成 |
| `OpenGLPhysicsDebugDraw` | 物理调试可视化（碰撞体轮廓/关节/质心） |
| `ConsoleVariable` | 控制台变量系统，支持运行时修改 |
| `ConsoleCommandRegistry` | 控制台命令注册表 |

### 🎨 编辑器框架

| 组件 | 说明 |
|------|------|
| `EngineEditor` | 编辑器统一管理框架，ImGui Docking 布局 |
| `DockspaceBuilder` | Dockspace 布局构建器 |
| `MainMenuBar` | 主菜单栏 |
| `Toolbar` | 工具栏 |
| `ViewportPanel` | 视口面板 |
| `ContentBrowserPanel` | 内容浏览器面板 |
| `AssetBrowserPanel` | 资产浏览器面板 |
| `DependencyGraphPanel` | 依赖关系图面板 |
| `SceneHierarchyPanel` | 场景层级面板 |
| `InspectorPanel` | 检视面板 |
| `ConsolePanel` | 控制台面板 |
| `PerformanceWindow` | 性能监控窗口（FPS 折线图、帧时间柱状图、DrawCall 统计） |
| `MemoryPanel` | 内存使用监控面板 |

### 🔄 序列化系统

| 组件 | 说明 |
|------|------|
| `JsonSerializer` | 场景 JSON 序列化/反序列化，注册式组件工厂 |
| `Component::Serialize` | 组件基类虚方法，由各组件自行实现 JSON 序列化 |
| `GameObject::ForEachComponent` | 遍历所有已挂载组件，通用迭代 |

JSON 格式示例：
```json
{
    "scene": {
        "name": "MainScene",
        "properties": {
            "ambientColor": [0.02, 0.02, 0.03, 1.0],
            "gravity": [0.0, -9.81],
            "fogDensity": 0.0,
            "enableFog": false,
            "renderingOrder": 0
        },
        "objects": [{
            "name": "Player",
            "active": true,
            "transform": {"position": [0,0,0], "rotation": [0,0,0], "scale": [1,1,1]},
            "components": [{
                "type": "SpriteComponent",
                "data": {"texture": "tex.png", "color": [1,1,1,1], "visible": true}
            }],
            "children": [...]
        }]
    }
}
```

---

## 🧪 沙盒示例

项目包含多个可独立构建的沙盒可执行文件，每个测试一个核心子系统：

| 目标 | 源文件 | 说明 |
|------|--------|------|
| **`Sandbox`** | `sandbox/src/main.cpp` | 基础应用入口，仅创建窗口和运行主循环 |
| **`InputTest`** | `sandbox/src/InputTest/` | 输入系统测试 — WASD/鼠标/按键映射 |
| **`SpriteBatchTest`** | `sandbox/src/SpriteBatchTest/` | 精灵批处理渲染性能测试 |
| **`GameObjectTest`** | `sandbox/src/GameObjectTest/` | 游戏对象 / Transform / Sprite 组件测试 |
| **`TextureManagerTest`** | `sandbox/src/TextureManagerTest/` | 纹理缓存与资源管理测试 |
| **`PhysicsTest`** | `sandbox/src/PhysicsTest/` | Box2D 物理集成测试 — 碰撞/堆叠/关节/鼠标拖拽/调试绘制 |
| **`BarePhysicsTest`** | `sandbox/src/BarePhysicsTest/` | 无渲染的裸物理测试，仅验证物理模拟正确性 |
| **`AudioTest`** | `sandbox/src/AudioTest/` | OpenAL 3D 空间音频测试 — WASD 移动音源/音高音量控制 |
| **`AudioSystemTest`** | `sandbox/src/AudioSystemTest/` | 音频系统组件集成测试 |
| **`CollisionAudioTest`** | `sandbox/src/CollisionAudioTest/` | 碰撞音效测试 — 不同材质(Stone/Wood/Metal)碰撞发声 |
| **`ImGuiDemo`** | `sandbox/src/ImGuiDemo/` | Dear ImGui 集成演示 — Docking 布局/基本控件/交互 |
| **`ImGuiTest`** | `sandbox/src/ImGuiTest/` | ImGui 功能测试 — 窗口管理/输入抢占/性能窗口 |
| **`SerializationTest`** | `sandbox/src/SerializationTest/` | 场景 JSON 序列化/反序列化单元测试 |
| **`ComplexSceneTest`** | `sandbox/src/ComplexSceneTest/` | 复杂场景序列化与重建测试 |
| **`MarioDemo`** | `sandbox/src/MarioDemo/` | 🎯 **超级马里奥 Demo** — 2D 平台游戏完整实现，含 BGM |
| **`AudioPhysicsSandbox`** | `sandbox/src/AudioPhysicsSandbox/` | 🎯 **综合演示** — 见下方详细说明 |
| **`EditorDemo`** | `sandbox/src/EditorDemo/` | 🎯 **编辑器框架演示** — EngineEditor 全功能面板 |
| **`SystemTest`** | `sandbox/src/SystemTest/` | 🎯 **系统综合测试** — JobSystem/FileSystem/Config/混合调度 |
| **`_3DTest`** | `sandbox/src/_3DTest/` | 🎯 **3D 渲染测试** — 3D 光照 / 延迟渲染管线验证 |
| **`AnimationTest`** | `sandbox/src/AnimationTest/` | 🎯 **动画系统测试** — 骨骼/蒙皮/混合树基础功能 |
| **`AnimationDemo`** | `sandbox/src/AnimationDemo/` | 🎯 **动画系统演示** — 骨骼动画播放与混合展示 |
| **`CrashTest`** | `sandbox/src/CrashTest/` | 崩溃报告测试 — 触发 SEH 异常验证 CrashHandler |

### 🎯 AudioPhysicsSandbox — 综合功能演示

该沙盒集合了引擎的**物理 + 音频 + 输入**三大系统，展示完整的交互体验：

```
操作方式:
  1~5      — 播放音效（钢琴/鼓/贝斯/扫弦/镲）
  B        — 暂停/恢复背景音乐
  鼠标左键 — 拖动物体，碰撞时按材质发声
  Esc      — 退出

控制台输出（每秒刷新）:
  FPS | Collisions | Active sounds | AudioSources | PhysicsBodies | Joints | Clips

退出时打印:
  资源创建/销毁统计 → 自动检测内存泄漏
```

所有音效均由程序运行时实时生成（WAV 合成），无需外部音频文件。

### 🎯 MarioDemo — 超级马里奥平台游戏 Demo

一个完整的 2D 平台游戏实现，展示引擎的游戏逻辑能力：

```
关卡: 1-1 / 1-2（按 1/2 键切换）  旗杆通关后 3 秒自动进入下一关

操作方式:
  A/D      — 左右移动
  W/空格   — 跳跃
  R        — 重置当前关卡
  Esc      — 退出

功能特性:
  · 完整的平台游戏物理（重力/碰撞/平台判定）
  · 敌人 AI（Goomba 巡逻 + 可踩踏消灭）
  · 问号砖块与金币收集系统
  · 旗杆通关机制 + 通关音效（Fanfare）
  · 程序合成 BGM（方波+正弦波生成超级马里奥主题旋律）
  · 循环背景音乐，音量 0.15
  · 关卡重置与切换（R / 1 / 2 键）
```

### 🎯 EditorDemo — 编辑器框架演示

展示完整的 EngineEditor 集成，包含 ImGui Docking 布局和多个面板：

```
功能面板:
  · 主菜单栏（文件/编辑/视图/工具/帮助）
  · 工具栏（播放/暂停/步进/停止）
  · 视口面板（主渲染区域）
  · 场景层级面板（Hierarchy）
  · 检视面板（Inspector — 选中对象属性）
  · 内容浏览器面板（Content Browser）
  · 资产浏览器面板（Asset Browser）
  · 依赖关系图面板（Dependency Graph）
  · 控制台面板（日志输出）
  · 性能监控窗口（FPS 折线图/帧时间柱状图/DrawCall）
  · 内存面板（Memory Usage）
```

### 🎯 SystemTest — 系统综合测试

集成测试引擎基础设施组件的正确性和性能：

```
测试内容:
  · JobSystem — 多任务并行调度正确性验证
  · ParallelFor — 数据并行遍历正确性验证
  · Time — 时间精度验证
  · SubsystemConfig 混合调度 — 固定步长/限频/事件驱动
  · 物理/粒子/动画/AI/音频各子系统微基准（Timing）
  · 程序持续运行 10 秒后自动退出并打印汇总报告
```

### 🎯 _3DTest — 3D 渲染测试

验证引擎的 3D 渲染管线：

```
测试内容:
  · 3D 光照渲染（3D Lit Shader）
  · 延迟渲染管线（Deferred Geometry + Deferred Lighting Pass）
  · SSAO（屏幕空间环境光遮蔽）
  · 深度测试（Depth Only Pass）
  · 粒子系统渲染
  · UI 渲染
```

### 🎯 AnimationDemo — 动画系统演示

展示引擎的骨骼动画系统能力：

```
功能特性:
  · 骨骼蒙皮网格渲染
  · 动画混合（线性混合 / 叠加混合）
  · 动画状态机（状态转换与条件）
  · 混合树（Blend Tree）
  · 混合空间（Blend Space 1D / 2D）
  · 反向动力学（IK Two-Bone / FABRIK）
  · 动画重定向（跨骨骼动画映射）
```

---

## 🔧 构建说明

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | `Debug` | 调试/发布模式 |
| `BUILD_SHARED_LIBS` | `OFF` | 是否构建动态库 |

### 构建所有沙盒

```bash
cmake -B build
cmake --build build --config Release
```

构建完成后，每个沙盒可执行文件位于 `build/sandbox/<TargetName>/` 子目录下，资源文件（`assets/`）和 `OpenAL32.dll` 会自动复制到对应的输出目录。

### 可选构建目标

| 目标 | 说明 |
|------|------|
| `EngineCore` | 仅构建引擎核心静态库 |
| `Sandbox` | 基础空白窗口 |
| `AudioPhysicsSandbox` | 物理+音频综合演示 |
| `MarioDemo` | 超级马里奥平台游戏 Demo |
| `EditorDemo` | 引擎编辑器框架演示 |
| `SystemTest` | 系统基础设施综合测试 |
| `_3DTest` | 3D 渲染测试 |
| `AnimationDemo` | 动画系统演示 |
| ... | 其他目标见上方沙盒示例列表 |

### 清理构建

```bash
rm -rf build
cmake -B build
```

---

## 📁 项目结构

```
game-engine-demo/
├── assets/                          # 运行时资源
│   ├── shaders/
│   │   ├── vertex.glsl              # 通用顶点着色器
│   │   ├── fragment.glsl            # 通用片段着色器
│   │   ├── sprite_batch.vert        # 精灵批处理顶点着色器
│   │   ├── sprite_batch.frag        # 精灵批处理片段着色器
│   │   ├── 3d_lit.vert / .frag      # 3D 光照着色器
│   │   ├── deferred_geom.vert/.frag # 延迟渲染 — 几何 Pass
│   │   ├── deferred_light.frag      # 延迟渲染 — 光照 Pass
│   │   ├── depth_only.vert / .frag  # 深度预 Pass
│   │   ├── fullscreen_quad.vert     # 全屏四边形（后处理）
│   │   ├── particle.vert / .frag    # 粒子系统着色器
│   │   ├── ssao.frag                # SSAO 着色器
│   │   ├── ui.vert / .frag          # UI 渲染着色器
│   │   └── aa/                      # 抗锯齿着色器（预留）
│   ├── sounds/
│   │   ├── coin.wav                 # 马里奥金币音效
│   │   ├── death.wav                # 马里奥死亡音效
│   │   ├── jump.wav                 # 马里奥跳跃音效
│   │   ├── powerup.wav              # 马里奥道具音效
│   │   └── stomp.wav                # 马里奥踩踏音效
│   └── textures/
│       ├── test.png                 # 测试纹理
│       └── mario/                   # 马里奥 Demo 贴图（预留）
├── engine/                          # 引擎核心库 (EngineCore)
│   ├── CMakeLists.txt
│   ├── include/Engine/
│   │   ├── Animation/               # 动画系统（骨骼/蒙皮/混合树/IK/状态机）
│   │   ├── Audio/                   # 音频工具（AudioSystem）
│   │   ├── Box2D/                   # Box2D 物理实现
│   │   ├── Core/                    # 抽象接口层与核心工具
│   │   │   ├── Audio/               # 音频接口（IAudioEngine/Buffer/Source/Types）
│   │   │   ├── Containers/          # 容器（IntrusiveList/SmallVector/StackList）
│   │   │   ├── GameObject/          # 游戏对象与组件
│   │   │   ├── Memory/              # 内存分配器（StackAllocator/MemoryTracker）
│   │   │   ├── Physics/             # 物理接口（IPhysicsWorld/Body/Joint/DebugDraw）
│   │   │   ├── Renderer/            # 渲染器接口（OrthographicCamera/Renderer/SpriteBatch）
│   │   │   ├── RenderResources/     # 渲染资源接口（Shader/Texture/VAO/VBO/IBO）
│   │   │   ├── Resources/           # 资源管理（ResourceManager/FileWatcher/AssetDB）
│   │   │   ├── RHI/                 # 渲染硬件接口（AntiAliasing/BufferVisualization）
│   │   │   ├── Scene/               # 场景管理 + 序列化（Scene/Serializer）
│   │   │   ├── Config.h             # JSON 配置系统
│   │   │   ├── EngineSettings.h     # 引擎设置
│   │   │   ├── FileSystem.h         # 文件系统（VFS 挂载/异步 I/O）
│   │   │   ├── AsyncStream.h        # 异步文件流
│   │   │   ├── FileStream.h         # 文件流适配器（第三方库回调）
│   │   │   ├── IGraphicsFactory.h   # 图形工厂接口
│   │   │   ├── Input.h              # 输入状态查询
│   │   │   ├── InputAction.h        # 输入动作定义
│   │   │   ├── InputManager.h       # 输入管理器
│   │   │   ├── IRenderContext.h     # 渲染上下文接口
│   │   │   ├── IUIManager.h         # UI 管理器接口
│   │   │   ├── IWindow.h            # 窗口抽象接口
│   │   │   ├── JobSystem.h          # 任务并行系统
│   │   │   ├── Log.h                # 日志系统
│   │   │   ├── MenuManager.h        # 菜单管理器
│   │   │   ├── SubsystemConfig.h    # 子系统更新策略配置
│   │   │   ├── SubsystemManager.h   # 子系统生命周期管理器
│   │   │   └── UserSettings.h       # 用户设置
│   │   ├── Debug/                   # 调试系统（CrashHandler/StackTrace/Screenshot/Profiler）
│   │   ├── Editor/                  # 编辑器框架（EngineEditor + 面板）
│   │   ├── OpenAL/                  # OpenAL 音频实现
│   │   ├── OpenGL/                  # OpenGL 渲染实现
│   │   ├── Platform/                # 平台层（GLFW 窗口/输入）
│   │   ├── Rendering/               # 渲染管线（SceneRenderer/RenderQueue）
│   │   ├── Application.h            # 应用入口基类
│   │   ├── ConsoleCommandRegistry.h # 控制台命令注册表
│   │   ├── ConsoleLog.h             # 控制台日志
│   │   ├── ConsolePanel.h           # 控制台面板
│   │   ├── ConsoleVariable.h        # 控制台变量系统
│   │   ├── DockspaceBuilder.h       # Dockspace 布局构建器
│   │   ├── InspectorPanel.h         # 检视面板
│   │   ├── MemoryPanel.h            # 内存面板
│   │   ├── PCH.h                    # 预编译头文件
│   │   ├── PerformanceWindow.h      # 性能监控窗口
│   │   ├── Profiler.h               # Tracy 性能剖析
│   │   ├── SceneHierarchyPanel.h    # 场景层级面板
│   │   ├── Types.h                  # 基础类型定义
│   │   ├── UiHelpers.h              # UI 辅助工具
│   │   └── UIManager.h              # UI 管理器静态代理
│   └── src/                         # 引擎源码
│       ├── Animation/
│       ├── Audio/
│       ├── Box2D/
│       ├── Core/
│       │   ├── Memory/
│       │   ├── GameObject/
│       │   ├── Physics/
│       │   ├── Renderer/
│       │   ├── RenderResources/
│       │   ├── Resources/
│       │   ├── RHI/
│       │   ├── Scene/
│       │   └── ...
│       ├── Debug/
│       ├── Editor/
│       ├── OpenAL/
│       ├── OpenGL/
│       │   └── Resources/           # GL 渲染资源（VAO/VBO/IBO 实现）
│       ├── Platform/
│       │   └── glfw/
│       └── Rendering/               # 渲染管线实现
├── sandbox/                         # 沙盒测试可执行文件
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp                 # Sandbox 基础入口
│       ├── _3DTest/                 # ★ 3D 渲染测试
│       ├── AnimationDemo/           # ★ 动画系统演示
│       ├── AnimationTest/           # ★ 动画系统测试
│       ├── AudioPhysicsSandbox/     # ★ 综合演示
│       ├── AudioSystemTest/         # ★ 音频系统测试
│       ├── AudioTest/
│       ├── BarePhysicsTest/         # ★ 裸物理测试
│       ├── CollisionAudioTest/
│       ├── ComplexSceneTest/        # ★ 复杂场景测试
│       ├── CrashTest/               # 崩溃报告测试
│       ├── EditorDemo/              # 编辑器框架演示
│       ├── GameObjectTest/
│       ├── ImGuiDemo/               # ImGui 集成演示
│       ├── ImGuiTest/               # ImGui 功能测试
│       ├── InputTest/
│       ├── MarioDemo/               # 超级马里奥 Demo
│       ├── PhysicsTest/
│       ├── SerializationTest/       # 序列化单元测试
│       ├── SpriteBatchTest/
│       ├── SystemTest/              # 系统综合测试
│       └── TextureManagerTest/
├── docs/                            # 项目文档
│   └── 3rdparty/                    # 第三方库文档
├── third_party/                     # 第三方库
│   ├── CMakeLists.txt
│   ├── box2d/                       # Box2D 3.0 (物理引擎)
│   ├── freetype/                    # FreeType (字体渲染)
│   ├── glad/                        # OpenGL 4.6 函数加载
│   ├── glfw/                        # 窗口和输入管理
│   ├── glm/                         # 图形数学库
│   ├── imgui/                       # Dear ImGui 1.91 (git submodule)
│   ├── imgui_build/                 # ImGui 构建配置
│   ├── imguizmo/                    # ImGuizmo (3D 操纵器)
│   ├── imguizmo_tmp/                # ImGuizmo 附加副本
│   ├── nlohmann/                    # JSON 库 (json.hpp)
│   ├── openal-soft/                 # OpenAL Soft 1.25 (3D 空间音频)
│   ├── spdlog/                      # 日志库
│   ├── stb/                         # 图像和音频解码
│   └── tracy/                       # Tracy Profiler (性能剖析)
├── CMakeLists.txt                   # 根构建文件
├── LICENSE.txt
└── README.md
```

---

## 🧰 第三方依赖

| 库 | 版本 | 用途 |
|----|------|------|
| [Box2D](https://github.com/erincatto/box2d) | 3.0 | 2D 物理模拟（刚体、碰撞、关节） |
| [glfw](https://github.com/glfw/glfw) | 最新 | 跨平台窗口创建和输入处理 |
| [glad](https://github.com/Dav1dde/glad) | 2+ | OpenGL 4.6 函数加载 |
| [glm](https://github.com/g-truc/glm) | 1.1+ | 图形数学库（矩阵/向量运算） |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 1.25.2 | 3D 空间音频渲染 |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91+ | 即时模式 GUI 框架（编辑器面板/调试界面） |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | 最新 | 3D 变换操纵器（平移/旋转/缩放） |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON 序列化/反序列化（场景文件/配置） |
| [spdlog](https://github.com/gabime/spdlog) | 1.x | 高性能日志库 |
| [stb](https://github.com/nothings/stb) | 最新 | stb_image（纹理加载） + stb_vorbis（OGG 解码） |
| [FreeType](https://github.com/freetype/freetype) | 最新 | 字体栅格化与文本渲染 |
| [Tracy](https://github.com/wolfpld/tracy) | 最新 | 实时性能剖析（Debug 模式默认启用） |

---

## ⚠️ 注意事项

- 项目处于**快速迭代阶段**，API 和架构可能随时变更
- 当前仅支持 **OpenGL 4.6** 渲染后端（Vulkan 等尚未接入）
- 物理引擎使用 **Box2D 3.0**，API 与 2.x 有显著差异
- Windows 构建需要安装 **Visual Studio 2022**（MSVC v14.4+）
- 音频系统依赖 **OpenAL32.dll**，构建系统会自动复制到输出目录
- ImGui / ImGuizmo 使用 git submodule 引入，克隆时需 `--recursive` 或执行 `git submodule update --init --recursive`
- Tracy Profiler 在 Debug 配置下默认启用（`ENGINE_ENABLE_PROFILING` + `TRACY_ENABLE`），Release 配置下自动关闭；MSVC 下使用 `TRACY_DELAYED_INIT` 避免静态初始化期栈溢出
- 场景序列化使用 **nlohmann/json** 单头文件库，已预置于 `third_party/nlohmann/json.hpp`
- 动画系统仍处于积极开发阶段，API 可能发生较大变化

---

## 📝 许可证

本项目采用 **MIT License** 开源。详见 [LICENSE.txt](./LICENSE.txt)。

项目仍处于试验阶段，不具有投入生产的可能，请慎重考虑使用。