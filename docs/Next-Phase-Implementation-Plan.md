# 下一步实施计划方案

> **生成日期**: 2026-07-15  
> **依据**: 基于 Core-Infrastructure-Summary / Physics-Subsystem-Summary / Rendering-Subsystem-Summary / Editor-Subsystem-Summary / Audio-Subsystem-Summary / Animation-Subsystem-Summary / DebugRenderer-2.0-Analysis-Report / Scripting-Engine-Integration-Report / D3D12-Integration-Analysis 的综合分析

---

## 一、总体分析：项目当前状态

| 子系统 | 完成度 | 核心强项 | 最大缺口 |
|--------|--------|---------|---------|
| 核心基础设施 | ~85% | SubsystemManager / JobSystem / Config / EventBus / Console | 脚本系统 (20%) |
| 物理系统 | ~90% | Jolt 3D + Box2D 2D / ECS 集成 / 碰撞管道 / 关节 / 角色控制器 | GPU 物理 / .physmat 管线 |
| 渲染系统 | ~85% | Vulkan/OpenGL 双后端 / RenderGraph DAG / 延迟渲染 / CSM | GPUProfiler Stub / 动画GPU集成缺失 |
| 编辑器 | ~85% | 视口/PIE/层级/属性/撤销/资产浏览 | 子编辑器空占位 / 场景序列化未全覆盖 |
| 动画系统 | ~85% | 骨骼/BlendTree/BlendSpace/IK/状态机 | **GPU蒙皮未接入渲染管线** |
| 音频系统 | ~85% | WAV/OGG解码 / 3D空间化 / ECS组件 | IAudioEngine抽象未使用 / EFX未启用 |
| 调试绘制 | ~70% | 物理调试完备 / Jolt TLS | 通用DebugDraw API缺失 |
| 脚本系统 | ~20% | 已识别 C# 路径 / C-ABI 架构已设计 | 最严重短板 |

**核心判断**: 引擎已完成 `~85%` 的基础架构建设，进入"**补齐关键集成缺口 + 抛光硬化**"阶段。最大单项缺口是**脚本系统**，次大缺口是**动画→渲染GPU集成**。

---

## 二、战略优先级排序

```
P0 ─── 必须做 ─── 引擎完整性的关键缺失
├── 0.1 脚本引擎落地 (C-ABI + CoreCLR + ScriptComponent + 热重载)
└── 0.2 GPU蒙皮管线集成 (动画→渲染的架构鸿沟)

P1 ─── 应该做 ─── 显著提升生产力/性能/可用性
├── 1.1 通用 DebugDraw 系统 (全局API + TLS + RenderGraph Pass)
├── 1.2 GPUProfiler 真实实现 (替换 Stub)
├── 1.3 音频子系统重构 (IAudioEngine 抽象对齐 + EFX)
└── 1.4 控制台差距补齐 (autoexec.cfg + 引号解析 + CVar线程安全)

P2 ─── 可以做 ─── 锦上添花 / 探索性
├── 2.1 ComputeCullingPass 增强 (遮挡剔除)
├── 2.2 编辑器场景序列化全覆盖 + 导入预览
├── 2.3 AnimationBatch RenderGraph 集成
├── 2.4 物理 .physmat 资产管线
└── 2.5 XPBD GPU物理原型 (探索性)
```

---

## 三、各阶段详细计划

### 阶段 0: 脚本引擎落地 (P0.1) — 预计 27 人天（MVP 13 天）

#### 架构升级：v1 "语言绑定" → v2 "通用数据视图"

| 维度 | v1 方案（旧） | v2 方案（新） |
|------|-------------|-------------|
| **核心理念** | 选 C# 语言做深度绑定 | **数据协议先行**，多语言共享同一内存视图 |
| **C-ABI 层** | 80+ 语义函数（SetPosition/SetVelocity...） | **~15-20 个函数**（GetComponentPtr + QueryEntities + 工具） |
| **数据所有权** | 脚本实例持有部分数据 | **C++ ECS 池持有全部数据**，脚本通过指针直读 |
| **多语言支持** | C# 唯一 | **WASM/C#/Lua/Python 平等**，共享同一数据层 |
| **热重载策略** | 序列化→卸载→反序列化（有拷贝） | **数据不动，只换逻辑**（WASM 零拷贝热重载） |
| **绑定方式** | 手写 P/Invoke / FFI | **代码生成器自动生成**（Clang / YAML → 多语言 struct） |

#### 为什么要先做这个？

当前引擎最大的"功能鸿沟"是**游戏逻辑必须用 C++ 编写**，导致每次修改都需要全引擎重编译。

**脚本系统补齐后，引擎从"技术演示"升级为"可产品化游戏引擎"。**

#### 详细设计方案已更新

完整的 v2 架构设计、C-ABI 极小接口定义、WASM 内存映射方案、代码生成器设计、多语言热重载对比，详见 **`docs/Scripting-Engine-Integration-Report.md`**（已重写为 v2）。

#### 实施步骤

| 步骤 | 任务 | 工时 | 产出 |
|------|------|------|------|
| **0.1.1** | **基础设施**: ComponentRegistry 元数据系统 + 代码生成器 + 极小 C-ABI 层 (~15 函数) | 5天 | `engine_api.h` + 自动生成多语言 binding |
| **0.1.2** | **WASM 后端**: Wasmtime 集成 + Memory Import 映射 + 零拷贝热重载 | 5天 | Rust/Zig 脚本可读写 ECS 内存 |
| **0.1.3** | **C# 后端**: CoreCLR 宿主 + 自动 P/Invoke 生成 + unsafe 指针操作 | 5天 | C# 脚本可调用 `Engine_GetComponentPtr` |
| **0.1.4** | **ScriptSystem + ScriptComponent**: 语言无关调度器 + 异常隔离 + GC 步进 | 3天 | 多语言脚本每帧批量执行 |
| **0.1.5** | **LuaJIT + Python 后端**: FFI 集成 + ctypes 集成 + 热重载 | 4天 | Lua/Python 脚本可用 |
| **0.1.6** | **编辑器集成**: Inspector + 资产浏览 + Console 连接 + CMake 构建集成 | 5天 | 编辑器内脚本可编辑/可调试 |

**工作量细分**：

| 细分阶段 | 人天 | 说明 |
|---------|------|------|
| 阶段 0: 基础设施 | 5天 | 组件注册表 + 代码生成器 + 极小 C-ABI |
| 阶段 1: WASM 后端 | 5天 | WASM VM + 内存映射 + 零拷贝热重载 |
| 阶段 2: C# 后端 | 5天 | CoreCLR + P/Invoke 自动生成 |
| 阶段 3: ScriptSystem | 3天 | 语言无关调度器 + 异常隔离 |
| 阶段 4: LuaJIT + Python | 4天 | 额外语言后端 |
| 阶段 5: 编辑器集成 | 5天 | Inspector + 资产浏览 |
| **总计** | **~27 人天** | |
| **MVP（阶段 0+1+3）** | **~13 天** | WASM-only，但完整可用的脚本系统 |

#### 极小 C-ABI 核心接口（v2 设计）

```cpp
// engine_api.h — 约 15-20 个 extern "C" 函数
extern "C" {
    // ── 核心：内存访问（3 个函数覆盖 90% 场景）──
    void*    Engine_GetComponentPtr(uint64_t entityID, uint32_t componentType);
    // 返回 ECS 池中对应组件的指针，脚本按本地 struct 布局直接读写
    
    uint64_t Engine_QueryEntities(uint32_t* componentMask, uint32_t count, uint32_t* outCount);
    // 返回匹配所有指定组件的实体 ID 数组
    
    // ── 实体生命周期 ──
    uint64_t Engine_CreateEntity();
    void     Engine_DestroyEntity(uint64_t id);
    
    // ── 工具函数（约 10 个，无法通过内存访问表达的语义操作）──
    bool   Engine_Input_GetKey(uint32_t keyCode);
    void   Engine_Log(const char* msg);
    float  Engine_Time_GetDeltaTime();
    // ...
}
```

**核心设计思想**：不再暴露 `SetPosition` / `SetVelocity` 等语义操作，脚本通过 `GetComponentPtr` 拿到指针后直接按 struct 布局读写内存。新增组件类型时**不需要新增任何 C-ABI 函数**。

#### 风险与缓解

| 风险 | 概率 | 缓解 |
|------|------|------|
| Wasmtime C++ SDK 集成复杂度 | 中 | 备选 memcpy 同步窗口方案（性能可接受） |
| Windows 上无 mmap 可用 | 中 | 使用 `CreateFileMapping` + `MapViewOfFile` 或 memcpy 后备 |
| CoreCLR 宿主 API 不稳定 | 低 | .NET 8 LTS，微软官方支持 |
| 代码生成器维护成本 | 中 | 使用 Clang libTooling，组件定义变更时自动更新所有语言绑定 |
| AssemblyLoadContext 卸载不完全 | 中 | C-ABI 禁止反向引用 + 强制释放 |

---

### 阶段 0.2: GPU 蒙皮管线集成 — 预计 5 天

#### 为什么要并行做这个？

动画系统目前 CPU 端功能 **完整**（骨骼/BlendTree/IK/状态机），但 **GPU 蒙皮** 这个关键集成缺口使所有动画工作成果无法在渲染管线中呈现。这是**动画子系统与渲染子系统之间的架构鸿沟**。

#### 实施步骤

| 步骤 | 任务 | 工时 | 产出 |
|------|------|------|------|
| **0.2.1** | 定义蒙皮矩阵 SSBO 数据结构，扩展 `VulkanBuffer` 上传接口 | 1天 | `SkinningBuffer` 上传管线 |
| **0.2.2** | 将 SkinningComponent 的骨骼矩阵结果上传到 Vulkan SSBO | 1天 | CPU→GPU 蒙皮矩阵同步 |
| **0.2.3** | 编写 Shader 端蒙皮代码（Vertex Shader bone sampling） | 1天 | GLSL/HLSL 蒙皮 Shader |
| **0.2.4** | 实现 `AnimationBatch` RenderGraph Pass 集成 | 1天 | 合批 Draw Call |
| **0.2.5** | 实现 LOD 级别的 GPU 蒙皮切换（全精度/半精度/无蒙皮） | 1天 | LOD 管线 | 

---

### 阶段 1.1: 通用 DebugDraw 系统 — 预计 4 天

#### 为什么优先级高？

当前只有物理专用调试绘制接口，其他子系统（动画骨骼/音频位置/AI 导航/编辑器辅助）无法方便绘制调试信息。**这是一个横切关注点，影响所有子系统**。

#### 实施步骤

| 步骤 | 任务 | 工时 | 产出 |
|------|------|------|------|
| **1.1.1** | 创建 `DebugDraw.h` 全局 API（Line/Sphere/Box/Capsule/Frustum/Transform/Text） | 2h | 全局 API 声明 |
| **1.1.2** | 实现 TLS 收集器（16 线程独立 Bucket，50k 顶点预算） | 4h | 线程安全收集 |
| **1.1.3** | 实现持久化显示 Duration 生命周期管理 | 2h | 跨帧持续显示 |
| **1.1.4** | 实现 DebugDrawPass in RenderGraph（Depth On/Off 双 PSO） | 4h | 渲染管线集成 |
| **1.1.5** | 3D→2D 文本投影（ImGui 屏幕空间绘制） | 3h | 文本调试 |
| **1.1.6** | CVar 开关集成（`p.showColliders` / `r.showFrustum` / `r.debugDrawDistance`） | 2h | 运行时控制 |
| **1.1.7** | JoltDebugRenderer → DebugDraw 转发 + Frustum/Transform 辅助函数 | 3h | 向后兼容 |

---

### 阶段 1.2: GPUProfiler 真实实现 — 预计 2 天

| 步骤 | 任务 | 工时 |
|------|------|------|
| **1.2.1** | 实现 Vulkan `VK_QUERY_TYPE_TIMESTAMP` 查询池 | 1天 |
| **1.2.2** | 实现 OpenGL `GL_TIMESTAMP` 查询封装 | 0.5天 |
| **1.2.3** | Tracy GPU Profiling 数据提交（`TracyVkCollect`） | 0.5天 |

---

### 阶段 1.3: 音频子系统重构 — 预计 4 天

| 步骤 | 任务 | 工时 |
|------|------|------|
| **1.3.1** | 将 `Engine::Audio::AudioEngine` 重构为通过 `IAudioEngine` 接口调用 | 2天 |
| **1.3.2** | 添加 `std::mutex` 保护 `m_ActiveSources` | 0.5天 |
| **1.3.3** | OpenAL EFX 扩展初始化 + 混响/低通/回声效果 | 1天 |
| **1.3.4** | 音源优先级抢占（One-shot 池满时替换低优先级的） | 0.5天 |

---

### 阶段 1.4: 控制台差距补齐 — 预计 2 天

| 步骤 | 任务 | 工时 |
|------|------|------|
| **1.4.1** | `autoexec.cfg` 启动自动加载 | 2h |
| **1.4.2** | CVar 引号解析 Tokenizer（支持带空格的字符串值） | 1h |
| **1.4.3** | int/float/bool CVar 改用 `std::atomic` | 1h |
| **1.4.4** | string CVar 改用读写锁保护 | 0.5天 |
| **1.4.5** | 渲染/物理命令延迟执行到帧边界 | 1天 |

---

### 阶段 2.x: P2 增强项

| 任务 | 工时 | 说明 |
|------|------|------|
| ComputeCullingPass 遮挡剔除 | 3天 | GPU Occlusion Queries + Hi-Z |
| 编辑器场景序列化全覆盖 | 2天 | Joint3D/CollisionListener/Script 序列化补齐 |
| Asset 导入预览 | 2天 | 拖拽导入时缩略图生成 |
| AnimationBatch → RenderGraph | 1天 | 将动画批处理接入渲染管线 |
| .physmat 物理材质管线 | 2天 | yaml-cpp 反序列化 + JPH::PhysicsMaterial |
| XPBD GPU 物理原型 | 5天 | Compute Shader 约束求解器原型 |

---

## 四、工作量汇总

| 阶段 | 总人天 | 并行度 | 建议日历时间 |
|------|--------|--------|------------|
| **阶段 0.1**: 脚本引擎 | 23天 | 低（串行依赖多） | 4-5 周 |
| **阶段 0.2**: GPU 蒙皮 | 5天 | 中（可与 0.1 并行） | 1 周 |
| **阶段 1.1**: DebugDraw | 4天 | 高（可多人并行） | 3-5 天 |
| **阶段 1.2**: GPUProfiler | 2天 | 高 | 2 天 |
| **阶段 1.3**: 音频重构 | 4天 | 中 | 3-4 天 |
| **阶段 1.4**: 控制台补齐 | 2天 | 高 | 2 天 |
| **阶段 2.x**: P2 增强 | 各 1-5 天 | 高 | 按需 |

**总计核心工作量**: ~40 人天（不含 P2）  
**建议总工期**: 6-8 周（含并行执行）

---

## 五、推荐并行策略

```
Week 1-2           Week 3-4           Week 5-6           Week 7-8
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│ 0.1.1 C-ABI 层   │ │ 0.1.3 ScriptSys│ │ 0.1.5 GC控制    │ │ 0.1.7 编辑器集成│
│ 0.2 GPU蒙皮(上)   │ │ 0.1.4 热重载   │ │ 0.1.6 C#基类    │ │ 1.3 音频重构    │
│ 1.2 GPUProfiler   │ │ 1.1 DebugDraw  │ │ 1.4 控制台补齐   │ │ 2.x P2项       │
└─────────────────┘ └─────────────────┘ └─────────────────┘ └─────────────────┘
        ↑                    ↑                    ↑                    ↑
   脚本 + GPU 蒙皮    脚本 + 调试系统     脚本抛光 + 控制台     编辑器 + 音频
   可并行推进          调试系统可并行       音频/控制台可并行     多 P2 可并行
```

---

## 六、每个阶段的衡量标准（通过条件）

### 阶段 0.1 (脚本引擎) 完成条件

#### MVP 完成条件（阶段 0+1+3：WASM-only，13 天）

1. ✅ ComponentRegistry 元数据系统可运行时查询任意组件的 typeID、size、offset
2. ✅ 代码生成器可从 C++ 组件头文件生成 Rust `#[repr(C)]` struct 绑定
3. ✅ `Engine_GetComponentPtr` / `Engine_QueryEntities` / `Engine_CreateEntity` 等 ~15 个 C-ABI 函数实现并通过测试
4. ✅ WASM VM (Wasmtime) 可加载 `.wasm` 模块并执行 `on_update` 导出函数
5. ✅ WASM Memory Import 映射成功：Rust 脚本通过指针直接读写 ECS Transform/RigidBody 等组件
6. ✅ `Engine_Log` / `Engine_Input_GetKey` 等导入函数在 WASM 中可调用
7. ✅ ScriptSystem 按语言分组调度：所有 WASM ScriptComponent 每帧批量执行
8. ✅ ScriptSystem 异常隔离：WASM Trap 被捕获，单脚本崩溃不波及引擎
9. ✅ FileWatcher 检测 `.wasm` 文件变更 → 零拷贝热重载（数据不动，ECS 池持久化）
10. ✅ 沙盒场景 + Rust 编写的 PlayerController 脚本演示

#### 全功能完成条件（+ C#/Lua/Python 后端，额外 14 天）

11. ✅ CoreCLR 宿主集成：C# 脚本可通过 `Engine_GetComponentPtr` 的 P/Invoke 绑定读写 ECS 内存
12. ✅ C# `ScriptBehaviour` 基类可编译、可继承，`OnUpdate` 中 `Transform*` 直接赋值生效
13. ✅ C# AssemblyLoadContext 热重载：修改 `.dll` → 序列化脚本字段 → 替换 DLL → 恢复状态
14. ✅ LuaJIT FFI 绑定：Lua 脚本通过 `ffi.cast("Transform*", ptr)` 直接操作 ECS 内存
15. ✅ Python ctypes 绑定：Python 脚本可调用 `Engine_GetComponentPtr` 读取/写入组件
16. ✅ GC 预算控制生效（`profile.gc.ms` CVar 显示 < 1ms）
17. ✅ Editor 中可添加 `ScriptComponent` 到实体，指定语言和脚本文件名
18. ✅ 新增组件类型时，只需更新 C++ 头文件 + 重新运行代码生成器，所有语言绑定自动更新

### 阶段 0.2 (GPU蒙皮) 完成条件

1. ✅ SkinningComponent 的骨骼矩阵上传到 Vulkan SSBO
2. ✅ Vertex Shader 端蒙皮渲染正确（`#define USE_GPU_SKINNING 1`）
3. ✅ AnimationBatch 在 RenderGraph 中作为独立 Pass 执行
4. ✅ LOD 级别切换蒙皮精度
5. ✅ Tracy 显示 GPU 蒙皮 pass 耗时

### 阶段 1.1 (DebugDraw) 完成条件

1. ✅ `Engine::DebugDraw::Line`/`Sphere`/`Box`/`Capsule`/`Frustum`/`Transform` 全部可用
2. ✅ 任意线程调用不崩溃（TLS Bucket 隔离）
3. ✅ `duration` 参数使图元跨帧持续显示
4. ✅ 50k 顶点预算限制触发时 ConsoleLog 告警
5. ✅ `p.showColliders` / `r.showFrustum` / `r.debugDrawDistance` CVar 生效
6. ✅ DebugDrawPass 在 RenderGraph 中渲染 Depth On + Depth Off 两趟

---

## 七、长期架构展望

```
2026 Q3                   2026 Q4                    2027 Q1
┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐
│ 脚本系统 (P0)      │  │ 脚本编辑器集成      │  │ GPU 原生管线       │
│ GPU 蒙皮 (P0)      │  │ 动画蓝图编辑器      │  │ GPU 物理后端       │
│ DebugDraw (P1)     │  │ AI 导航系统        │  │ ECS→GPU 编译       │
│ GPUProfiler (P1)   │  │ 网络同步          │  │ 多平台发布         │
│ 音频重构 (P1)      │  │ VFX Graph 编辑器   │  │ ShaderGraph 编辑器 │
│ 控制台补齐 (P1)    │  │ 遮挡剔除增强       │  │ 光线追踪           │
└────────────────────┘  └────────────────────┘  └────────────────────┘
```

---

## 八、关键决策记录

### 决策 1: 脚本架构 — 通用数据视图（非"选语言"）

**背景**: v1 设计选择了 C# 做单一语言绑定（评分 113 vs WASM 108 vs Luau 102）。  
**决策**: 废弃"选一种语言"的思维，改为构建**语言无关的数据层**。  
**核心设计**:
- **极小 C-ABI**（~15 个函数 vs v1 的 80+）：只暴露 `GetComponentPtr` + `QueryEntities` + 极少量工具函数
- **数据在 ECS 池**：所有运行时数据强制在 C++ 组件池中，脚本只持有逻辑和缓存状态
- **WASM/C#/Lua/Python 平等**：共享同一内存视图，通过 `Engine_GetComponentPtr` 获取指针后直接按 struct 布局读写
- **代码生成器**：从统一组件定义自动生成各语言的 struct 定义和绑定代码
- **零拷贝热重载**：WASM 通过 Memory Import 映射 ECS 池物理内存，热重载时数据不动

**理由**:
- 新增组件类型不需要新增 C-ABI 函数（v1 需要加 5 个）
- 所有语言通过结构体内存布局直接读写，跨语言调用零边界开销
- WASM 提供沙盒安全（第三方 Mod），C# 提供 IDE 生态（核心逻辑），Lua 提供快速原型（策划），Python 提供 AI/ML 集成（工具链）
- 数据与逻辑分离的设计使热重载从"序列化→卸载→反序列化"降级为"数据不动，只换逻辑"

**代价**: ~27 人天全功能 / 13 天 MVP（WASM-only）

### 决策 2: D3D12 保持休眠

**理由**:
- Vulkan-first 是跨平台引擎的工业标准
- D3D12 777 行代码保持"休眠"状态，不主动维护
- 未来复活策略: HLSL 单一事实来源 + DXC 双端编译

### 决策 3: 物理后端保持 CPU Jolt 为主，GPU 为远期探索

**理由**:
- Gameplay 逻辑深度耦合 + PCIe Readback 瓶颈使纯 GPU 物理暂不适用于游戏引擎
- Jolt + JobSystemAdapter 已足够应对当前规模
- GPU 物理（XPBD 原型）作为 P2 探索项

---

## 九、附录：各 Summary 报告的优先行动项映射

| 报告 | 建议行动 | 映射到此计划的阶段 |
|------|---------|------------------|
| Core-Infrastructure-Summary | 脚本系统落地 / 控制台补齐 / Pak压缩 | 0.1 / 1.4 / 2.x |
| Rendering-Subsystem-Summary | GPUProfiler / ComputeCulling / AsyncCompute | 1.2 / 2.x |
| Physics-Subsystem-Summary | GPU布料粒子 / .physmat / XPBD原型 | 2.x |
| Editor-Subsystem-Summary | 场景序列化 / 导入预览 / 主题 | 2.x |
| Animation-Subsystem-Summary | **GPU蒙皮** / Root Motion / 层级状态机 | **0.2** / 2.x |
| Audio-Subsystem-Summary | **IAudioEngine重构** / EFX / 线程安全 | **1.3** |
| DebugRenderer-2.0-Report | **通用DebugDraw API** / TLS / RenderPass / CVar | **1.1** |
| Scripting-Engine-Report | **C#(.NET)路径 → C-ABI → ScriptSystem → 热重载** | **0.1** |
| D3D12-Integration-Analysis | 保持休眠，不投入 | 无 |