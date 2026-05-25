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
  </p>
</p>

---

## 📖 项目简介

**Game Engine Demo** 是一个实验性的模块化游戏引擎演示项目，使用 **C++20** 标准开发。项目采用分层解耦架构，通过纯虚接口层（RHI 风格）将核心逻辑与具体实现分离，已集成 **OpenGL 4.6** 渲染、**Box2D 3.0** 物理模拟和 **OpenAL Soft** 3D 空间音频。

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
# 克隆仓库
git clone https://github.com/chenjiefeng2001/game-engine-demo.git
cd game-engine-demo

# 配置项目（使用 Visual Studio 2022）
cmake -B build -G "Visual Studio 17 2022"

# 构建引擎核心库
cmake --build build --target EngineCore --config Release

# 构建并运行沙盒示例
cmake --build build --target AudioPhysicsSandbox --config Release
./build/sandbox/Release/AudioPhysicsSandbox.exe
```

### Linux / macOS 构建

```bash
git clone https://github.com/chenjiefeng2001/game-engine-demo.git
cd game-engine-demo
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target AudioPhysicsSandbox
./build/sandbox/AudioPhysicsSandbox
```

---

## 🏗️ 引擎架构

```
┌─────────────────────────────────────────────────────────┐
│                     Sandbox Layer                       │
│  (沙盒示例, 直接使用引擎 API 构建场景和游戏逻辑)            │
├─────────────────────────────────────────────────────────┤
│                     Engine API                          │
│  IGraphicsFactory · IWindow · IPhysicsWorld · IAudioEngine │
│  Scene · GameObject · SpriteBatch · OrthographicCamera   │
├────────────────────┬────────────────────┬───────────────┤
│  OpenGL Backend    │  Box2D Backend     │ OpenAL Backend│
│  OpenGLContext     │  Box2DPhysicsWorld │ OpenALAudioEng│
│  OpenGLSpriteBatch │  Box2DPhysicsBody  │ OpenALAudioSrc│
│  OpenGLShader ...  │  Box2DJoint ...    │ OpenALAudioBuf│
├────────────────────┴────────────────────┴───────────────┤
│                  Third Party Libraries                   │
│  GLFW · glad · glm · Box2D · OpenAL Soft · stb           │
└─────────────────────────────────────────────────────────┘
```

### 设计原则

- **接口与实现分离** — 核心层只依赖纯虚接口（`IPhysicsWorld`、`IAudioEngine` 等），实现层在编译时注入
- **工厂模式** — 通过 `IGraphicsFactory` 统一创建窗口、上下文、着色器、精灵批处理等资源
- **组件化** — `GameObject` 通过 `TransformComponent`、`SpriteComponent`、`PhysicsComponent` 等组合行为
- **RAII 资源管理** — 使用 `shared_ptr` / 智能指针管理 OpenGL 纹理、OpenAL 缓冲区和物理体的生命周期

---

## 📦 模块详解

### 🖼️ 渲染系统 (`Engine::OpenGL`)

| 组件 | 文件 | 说明 |
|------|------|------|
| `OpenGLContext` | `OpenGLContext.h/.cpp` | OpenGL 上下文封装，管理视口和清除状态 |
| `OpenGLGraphicsFactory` | `OpenGLGraphicsFactory.h/.cpp` | 工厂实现，创建窗口/着色器/纹理/批处理器 |
| `OpenGLSpriteBatch` | `OpenGLSpriteBatch.h/.cpp` | 2D 精灵批处理渲染器 |
| `OpenGLShader` | `OpenGLShader.h/.cpp` | GLSL 着色器编译与链接 |
| `OpenGLTexture` | `OpenGLTexture.h/.cpp` | OpenGL 纹理对象管理 |
| `OrthographicCamera` | `OrthographicCamera.h/.cpp` | 正交投影相机，支持缩放和移动 |
| `Shader` | `Shader.h` | 着色器抽象接口 |
| `TextureManager` | `TextureManager.h/.cpp` | 纹理缓存管理器，自动去重 |

### 🧱 物理系统 (`Engine::Box2D`)

| 组件 | 文件 | 说明 |
|------|------|------|
| `IPhysicsWorld` | `IPhysicsWorld.h` | 物理世界抽象接口 |
| `IPhysicsBody` | `IPhysicsBody.h` | 刚体抽象接口 |
| `IJoint` | `IJoint.h` | 关节抽象接口（支持鼠标/距离/旋转/滑动/焊接/轮式关节） |
| `Box2DPhysicsWorld` | `Box2DPhysicsWorld.h/.cpp` | Box2D 物理世界封装 |
| `Box2DPhysicsBody` | `Box2DPhysicsBody.h/.cpp` | Box2D 刚体封装 |
| `PhysicsComponent` | `PhysicsComponent.h` | 可挂载到 GameObject 的物理组件 |
| `PhysicsDefs` | `PhysicsDefs.h` | 纯数据结构（BodyDef/ShapeDef/JointDef/ContactInfo） |
| `OpenGLPhysicsDebugDraw` | `OpenGLPhysicsDebugDraw.h/.cpp` | 物理调试可视化（碰撞体轮廓/关节/质心） |

支持的回调：
- **碰撞开始** `SetContactBeginCallback` — 碰撞发生时触发
- **碰撞结束** `SetContactEndCallback` — 碰撞分离时触发
- **碰撞持续** `SetContactPersistCallback` — 碰撞接触中每帧触发（带冲量）
- **碰撞滤波** `SetContactPreSolveCallback` — 运行时控制是否产生碰撞

### 🔊 音频系统 (`Engine::OpenAL`)

| 组件 | 文件 | 说明 |
|------|------|------|
| `IAudioEngine` | `IAudioEngine.h` | 音频引擎抽象接口 |
| `IAudioSource` | `IAudioSource.h` | 3D 音源抽象接口 |
| `IAudioBuffer` | `IAudioBuffer.h` | 音频缓冲区抽象接口 |
| `OpenALAudioEngine` | `OpenALAudioEngine.h/.cpp` | OpenAL Soft 引擎封装 |
| `OpenALAudioSource` | `OpenALAudioSource.h/.cpp` | OpenAL 音源封装 |
| `OpenALAudioBuffer` | `OpenALAudioBuffer.h/.cpp` | OpenAL 缓冲区封装 |
| `AudioClip` | `AudioClip.h/.cpp` | 高层音频资源类（加载文件/内存 → 自动解码 → 上传 OpenAL） |
| `AudioClipManager` | `AudioClipManager.h/.cpp` | 音频剪辑缓存管理器 |
| `AudioSourceComponent` | `AudioSourceComponent.h` | 可挂载到 GameObject 的音频源组件 |
| `AudioSystem` | `AudioSystem.h` | 便捷工具 — `PlayOneShot` / `UpdateOneShots` 一次性音效播放 |
| `AudioLoader` | `AudioLoader.h/.cpp` | WAV/OGG 文件解码器 |
| `Listener` | `Listener.h` | OpenAL 听者参数管理 |

支持的音频格式：`.wav`、`.ogg`（通过 stb_vorbis 解码）

### 🎮 输入系统

| 组件 | 文件 | 说明 |
|------|------|------|
| `IWindow` | `IWindow.h` | 窗口抽象接口 |
| `Input` | `Input.h` | 输入状态查询（键盘/鼠标） |
| `InputManager` | `InputManager.h/.cpp` | 输入动作系统（按键映射与事件绑定） |
| `GlfwWindow` | `GlfwWindow.h/.cpp` | GLFW 窗口实现（支持事件回调、窗口缩放自适应） |
| `GlfwInput` | `GlfwInput.h/.cpp` | GLFW 输入轮询实现 |

### 🎬 场景与游戏对象

| 组件 | 文件 | 说明 |
|------|------|------|
| `Scene` | `Scene.h/.cpp` | 场景管理容器，维护游戏对象列表与更新/渲染遍历 |
| `GameObject` | `GameObject.h/.cpp` | 游戏对象基类，持有 Transform/Sprite/Physics 组件 |
| `TransformComponent` | `TransformComponent.h/.cpp` | 位置/旋转/缩放组件，支持层级变换 |
| `SpriteComponent` | `SpriteComponent.h/.cpp` | 精灵渲染组件 |
| `PhysicsComponent` | `PhysicsComponent.h/.cpp` | 物理组件（连接 GameObject 与 IPhysicsBody） |

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
| **`AudioTest`** | `sandbox/src/AudioTest/` | OpenAL 3D 空间音频测试 — WASD 移动音源/音高音量控制 |
| **`CollisionAudioTest`** | `sandbox/src/CollisionAudioTest/` | 碰撞音效测试 — 不同材质(Stone/Wood/Metal)碰撞发声 |
| **`AudioPhysicsSandbox`** | `sandbox/src/AudioPhysicsSandbox/` | 🎯 **综合演示** — 见下方详细说明 |

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

构建完成后，所有可执行文件位于 `build/sandbox/Release/` 目录下，资源文件（`assets/`）和 `OpenAL32.dll` 会自动复制到对应的输出目录。

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
│   │   └── sprite_batch.frag        # 精灵批处理片段着色器
│   └── textures/
│       └── test.png                 # 测试纹理
├── engine/                          # 引擎核心库 (EngineCore)
│   ├── CMakeLists.txt
│   ├── include/Engine/
│   │   ├── Core/                    # 抽象接口层（纯虚类）
│   │   │   ├── Audio/               # 音频接口
│   │   │   ├── GameObject/          # 游戏对象组件
│   │   │   ├── Physics/             # 物理接口
│   │   │   ├── RenderResources/     # 渲染资源接口
│   │   │   ├── Renderer/            # 渲染器接口
│   │   │   ├── RHI/                 # 数学类型与基础类型
│   │   │   └── Scene/               # 场景管理
│   │   ├── OpenAL/                  # OpenAL 音频实现
│   │   ├── OpenGL/                  # OpenGL 渲染实现
│   │   ├── Box2D/                   # Box2D 物理实现
│   │   ├── Platform/                # 平台层（GLFW 窗口/输入/时间）
│   │   ├── Audio/                   # 音频工具（AudioSystem）
│   │   ├── Types.h                  # 基础类型定义
│   │   └── Application.h            # 应用入口
│   └── src/                         # 引擎源码
│       ├── Audio/
│       ├── Box2D/
│       ├── Core/
│       ├── OpenAL/
│       ├── OpenGL/
│       └── Platform/
├── sandbox/                         # 沙盒测试可执行文件
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       ├── AudioTest/
│       ├── AudioPhysicsSandbox/     # ★ 综合演示
│       ├── CollisionAudioTest/
│       ├── GameObjectTest/
│       ├── InputTest/
│       ├── PhysicsTest/
│       ├── SpriteBatchTest/
│       └── TextureManagerTest/
├── third_party/                     # 第三方库
│   ├── CMakeLists.txt
│   ├── box2d/                       # Box2D 3.0 (物理引擎)
│   ├── glad/                        # OpenGL 函数加载
│   ├── glfw/                        # 窗口和输入管理
│   ├── glm/                         # 数学库
│   ├── openal-soft/                 # OpenAL Soft (3D 空间音频)
│   └── stb/                         # 图像和音频解码
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
| [stb](https://github.com/nothings/stb) | 最新 | stb_image（纹理加载） + stb_vorbis（OGG 解码） |

---

## ⚠️ 注意事项

- 项目处于**快速迭代阶段**，API 和架构可能随时变更
- 当前仅支持 **OpenGL 4.6** 渲染后端（Vulkan 等尚未接入）
- 物理引擎使用 **Box2D 3.0**（`v2.4.1` 分支），API 与 2.x 有显著差异
- Windows 构建需要安装 **Visual Studio 2022**（MSVC v14.4+）以支持 C++20 模块
- 音频系统依赖 **OpenAL32.dll**，构建系统会自动复制到输出目录

---

## 📝 许可证

本项目采用 **MIT License** 开源。详见 [LICENSE.txt](./LICENSE.txt)。

项目仍处于试验阶段，不具有投入生产的可能，请慎重考虑使用。


