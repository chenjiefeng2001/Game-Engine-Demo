# 工业级脚本引擎集成方案 — 可行性报告与实施蓝图

> **生成日期**: 2026-07-08  
> **分析范围**: 语言选型、C-ABI 桥接设计、ECS 深度解耦、热重载架构、落地实施路线  
> **当前项目状态**: C++20 ECS 架构 + Jolt Physics v4.0-v7.0 完成 + JobSystem + EventBus + FileWatcher

---

## 一、当前项目脚本能力评估

### 1.1 已有脚本基础设施

| 组件 | 状态 | 与脚本引擎的关联 |
|------|------|----------------|
| **ECS EntityManager** | v7.0 完整 | 脚本层操作的底层对象模型 |
| **ComponentRemovedCallback** | 已实现 | 脚本实体的生命周期通知 |
| **EventBus** | 已实现 | 脚本事件的 C++ 端路由 |
| **FileWatcher** | 已实现 | 热重载的文件变更检测 |
| **JobSystem** | 已实现 | 脚本 Update 的并行调度 |
| **JsonSerializer** | 已实现 | 脚本状态的序列化底座 |
| **PhysicsSyncSystem** | v7.0 完整 | 脚本层物理操作的三级同步 |
| **TransformComponent (四元数)** | v4.0 完成 | 脚本层变换操作的基础 |
| **ConsoleVariable** | 已实现 | 脚本调试变量的 C++ 端注册 |

**结论**: 项目已有**足够的基础设施**来承载工业级脚本引擎。ECS 和 JobSystem 已经是为"数据导向脚本"量身定制的运行时。

### 1.2 当前脚本能力缺陷

| 缺陷 | 影响 | 根因 |
|------|------|------|
| 所有 Gameplay 逻辑必须用 C++ 写 | 迭代速度极慢，每改一行都要重编译 | 无脚本虚拟机 |
| 无运行时调试 | 无法在运行时修改变量/逻辑 | 无 REPL 环境 |
| 无热重载 | 引擎重启才能看到逻辑修改 | 无脚本域隔离 |
| 无编辑器内脚本编辑 | 需要外部 IDE | 无 ScriptComponent |

---

## 二、工业界脚本引擎选型分析

### 2.1 候选方案全景对比

| 维度 | **C# (.NET 8+ CoreCLR)** | **Luau (Roblox)** | **Lua 5.4 + sol2** | **Python (CPython)** | **WebAssembly (wasm3)** |
|------|--------------------------|-------------------|-------------------|---------------------|------------------------|
| **性能** | ⭐⭐⭐⭐⭐ JIT/AOT | ⭐⭐⭐⭐ Luau VM | ⭐⭐⭐ Lua VM | ⭐⭐ CPython | ⭐⭐⭐⭐ wasm |
| **类型安全** | ⭐⭐⭐⭐⭐ 强类型 | ⭐⭐⭐⭐ 类型推导 | ⭐⭐ 弱类型 | ⭐⭐ 动态 | ⭐⭐⭐⭐⭐ 编译期 |
| **工具链** | ⭐⭐⭐⭐⭐ VS/Rider | ⭐⭐⭐ Roblox Studio | ⭐⭐⭐ ZeroBrane | ⭐⭐⭐⭐⭐ VS/PyCharm | ⭐⭐⭐ 浏览器 DevTools |
| **调试能力** | ⭐⭐⭐⭐⭐ 断点/步进/变量 | ⭐⭐⭐ 有限 | ⭐⭐ 基本 | ⭐⭐⭐⭐⭐ 完整 | ⭐⭐⭐ 源映射 |
| **热重载** | ⭐⭐⭐ AppDomain/ALC | ⭐⭐⭐⭐⭐ 原生支持 | ⭐⭐⭐ 全局表替换 | ⭐⭐⭐ 模块重载 | ⭐⭐⭐⭐⭐ 模块替换 |
| **GC 控制** | ⭐⭐⭐⭐⭐ GCMode/LatencyMode | ⭐⭐⭐ 暂停可控 | ⭐⭐ 不可控 | ⭐ | 无 GC |
| **包管理** | ⭐⭐⭐⭐⭐ NuGet | ⭐⭐⭐⭐ Wally | ⭐⭐⭐ LuaRocks | ⭐⭐⭐⭐⭐ PyPI | 无 |
| **C++ 互操作** | ⭐⭐⭐⭐ C-ABI P/Invoke | ⭐⭐⭐ C API | ⭐⭐⭐⭐ C API | ⭐⭐⭐ C API | ⭐⭐⭐⭐ WASM ABI |
| **社群生态** | ⭐⭐⭐⭐⭐ 宇宙级 | ⭐⭐⭐⭐ Roblox | ⭐⭐⭐⭐⭐ 游戏界 | ⭐⭐⭐⭐⭐ AI/数据 | ⭐⭐⭐ 新兴 |
| **二进制体积** | ⭐ 大 (~50MB runtime) | ⭐⭐⭐⭐⭐ (~500KB) | ⭐⭐⭐⭐⭐ (~300KB) | ⭐ (~30MB) | ⭐⭐⭐ (~2MB) |
| **学习曲线** | ⭐⭐⭐⭐⭐ 通用技能 | ⭐⭐⭐⭐ Roblox 专精 | ⭐⭐⭐ 易上手 | ⭐⭐⭐⭐⭐ 通用 | ⭐⭐⭐ 须懂 LLVM |

### 2.2 工业级引擎选型决策矩阵

```
决策权重 (1-5):
  性能         = 5 (游戏引擎核心要求)
  调试能力     = 5 (开发者体验核心)
  热重载       = 5 (迭代速度核心)
  GC 控制     = 4 (帧率稳定性)
  工具链       = 4 (生产效率)
  二进制体积   = 2 (现代项目不敏感)
  学习曲线     = 2 (团队可培训)

评分:
  C# (.NET 8) =  5*5 + 5*5 + 3*5 + 5*4 + 5*4 + 2*2 + 2*2 = 25+25+15+20+20+4+4 = 113
  Luau        =  4*5 + 3*5 + 5*5 + 3*4 + 3*4 + 5*2 + 4*2 = 20+15+25+12+12+10+8 = 102
  Lua 5.4     =  3*5 + 2*5 + 3*5 + 1*4 + 3*4 + 5*2 + 3*2 = 15+10+15+4+12+10+6 = 72
  Python      =  2*5 + 5*5 + 3*5 + 1*4 + 5*4 + 1*2 + 4*2 = 10+25+15+4+20+2+8 = 84
  WebAssembly =  4*5 + 3*5 + 5*5 + 5*4 + 3*4 + 5*2 + 3*2 = 20+15+25+20+12+10+6 = 108
```

### 2.3 推荐方案

**首选: C# (.NET 8+/CoreCLR)** — 评分 113，工业标准
**次选: Luau** — 评分 102，轻量级场景

| 决策依据 | 说明 |
|---------|------|
| **性能需求** | 项目中 Jolt Physics 已经用 JobSystem 做了多线程物理模拟，脚本层不应成为性能瓶颈。C# JIT 能达到原生 80% 性能，足够应对 Gameplay |
| **现有基础设施匹配** | ECS EntityManager + SyncSystem 的架构与 C# 组件模型天然匹配。`ScriptComponent` 可以无缝融入 `PhysicsSyncSystem` 的 Update 流程 |
| **调试体验** | 引擎编辑器 (`EditorDemo`) 已有一套面板框架，C# 的 VS/Rider 断点调试可以直接对接 |
| **热重载路径** | .NET 的 AssemblyLoadContext 允许卸载并重新加载程序集，配合已有的 FileWatcher 可以实现无缝热替换 |
| **GC 控制** | .NET 8 的 `System.Runtime.GCSettings.LatencyMode` + `GC.TryStartNoGCRegion` 可以精确控制 GC 暂停在 1ms 以内，避免帧率抖动 |

---

## 三、核心架构：C-ABI Bridge (纯 C 桥接层)

### 3.1 架构总览

```
┌─────────────────────────────────────────────────────────────────────┐
│  Gameplay Code (C# / Luau)                                          │
│  public class PlayerController : ScriptBehaviour {                  │
│      void OnUpdate(float dt) {                                      │
│          transform.Position += Vector3.Forward * speed * dt;         │
│      }                                                              │
│  }                                                                  │
├─────────────────────────────────────────────────────────────────────┤
│  Script Side Wrapper (C#/Luau)                                      │
│  P/Invoke / FFI → Engine_Entity_SetPosition(entityID, x, y, z)     │
├─────────────────────────────────────────────────────────────────────┤
│  C-ABI Boundary — EXPORT "C" (engine_api.h)                        │
│  Engine_Entity_SetPosition(uint64_t entityID, float x, float y,    │
│                            float z) {                               │
│      EntityManager::Get().GetComponent<TransformComponent>(entityID)│
│          ->SetPosition(Vec3(x, y, z));                              │
│  }                                                                  │
├─────────────────────────────────────────────────────────────────────┤
│  C++ Engine Core (当前项目)                                          │
│  ECS EntityManager · TransformComponent · PhysicsSyncSystem        │
│  JobSystem · BatchSetKinematicTargets · EventBus                   │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 C-ABI API 设计原则

```
原则 1: 全部 extern "C" 导出，关闭 Name Mangling
   ✓ 语言无关 — C#/Lua/Rust/WASM 都能调用
   ✓ ABI 稳定 — 不依赖编译器实现
   ✓ 版本兼容 — 接口可扩展不破坏旧版本

原则 2: 禁止 C++ 标准库类型穿越边界
   ✗ void Engine_Func(std::vector<uint64_t> ids);  // 禁止！
   ✓ void Engine_Func(uint64_t* ids, uint32_t count);  // 正确

原则 3: 数据扁平化，避免对象引用
   ✗ Entity* GetEntity(uint64_t id);  // 禁止！
   ✓ uint64_t Engine_Entity_Create();  // 正确

原则 4: 批量 API 优先
   ✓ void Engine_Physics_BatchSetTransform(uint64_t* ids, float* positions, uint32_t count);
```

### 3.3 C-ABI API 分层设计

#### 3.3.1 核心层 (Foundation API) — 必须实现

```cpp
extern "C" {

// ── 虚拟机生命周期 ──
bool Engine_Init(const char* scriptPath, uint32_t heapSizeMB);
void Engine_Shutdown();

// ── 实体操作 ──
uint64_t Engine_Entity_Create();
void     Engine_Entity_Destroy(uint64_t entityID);
bool     Engine_Entity_IsAlive(uint64_t entityID);

// ── Transform 操作 ──
void Engine_Entity_SetPosition(uint64_t entityID, float x, float y, float z);
void Engine_Entity_GetPosition(uint64_t entityID, float* outX, float* outY, float* outZ);
void Engine_Entity_SetRotation(uint64_t entityID, float qx, float qy, float qz, float qw);
void Engine_Entity_GetRotation(uint64_t entityID, float* outQX, float* outQY, float* outQZ, float* outQW);

// ── 物理操作 ──
void  Engine_Physics_SetLinearVelocity(uint64_t entityID, float x, float y, float z);
void  Engine_Physics_ApplyForce(uint64_t entityID, float x, float y, float z);
bool  Engine_Physics_Raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                              uint64_t* outEntityID, float* outPointX, float* outPointY, float* outPointZ);

// ── 输入 ──
bool Engine_Input_GetKey(uint32_t keyCode);
void Engine_Input_GetMousePosition(float* outX, float* outY);

// ── 日志 ──
void Engine_Log_Info(const char* message);
void Engine_Log_Warn(const char* message);
void Engine_Log_Error(const char* message);

// ── 时间 ──
float Engine_Time_GetDeltaTime();
float Engine_Time_GetTimeSinceStartup();

}
```

#### 3.3.2 扩展层 (Extended API) — 按需实现

```cpp
extern "C" {

// ── 物理高级操作 ──
void  Engine_Physics_BatchSetTransform(uint64_t* ids, float* px, float* py, float* pz,
                                        float* qx, float* qy, float* qz, float* qw,
                                        uint32_t count);
void  Engine_Physics_QuerySphere(float cx, float cy, float cz, float radius,
                                  uint64_t* outIDs, uint32_t* outCount);
void  Engine_Physics_CreateJoint(uint64_t entityA, uint64_t entityB, uint32_t jointType,
                                  float* anchorA, float* anchorB);
void  Engine_Physics_DestroyJoint(uint64_t jointID);

// ── 角色控制器 ──
void  Engine_Character_Move(uint64_t entityID, float vx, float vy, float vz, float dt);
void  Engine_Character_Jump(uint64_t entityID, float force);
bool  Engine_Character_IsGrounded(uint64_t entityID);

// ── 音频 ──
uint32_t Engine_Audio_PlayOneShot(const char* eventPath, float x, float y, float z);
void     Engine_Audio_SetListener(float x, float y, float z, float fx, float fy, float fz);

// ── 调试绘制 ──
void Engine_Debug_DrawLine(float x1, float y1, float z1, float x2, float y2, float z2,
                            float r, float g, float b, float a);
void Engine_Debug_DrawText(float x, float y, float z, const char* text, float r, float g, float b);

}
```

---

## 四、ECS 与脚本深度解耦：ScriptComponent 设计

### 4.1 C++ 端 ScriptComponent

```cpp
// engine/include/Engine/Core/ECS/ScriptComponent.h
#pragma once

#include "Engine/Types.h"
#include <functional>
#include <string>

namespace Engine {

// 脚本实例的生命周期状态
enum class ScriptInstanceState : uint8 {
    Created,    // 已创建但未初始化
    Running,    // 正在运行 OnUpdate
    Stopped,    // 已暂停
    Error       // 脚本异常（不继续执行）
};

// 缓存函数指针结构（避免每帧查找）
struct ScriptFunctionCache {
    void* onUpdate    = nullptr;  // void(*)(void* instance, float dt)
    void* onCreate    = nullptr;  // void(*)(void* instance)
    void* onDestroy   = nullptr;  // void(*)(void* instance)
    void* onCollision = nullptr;  // void(*)(void* instance, uint64_t otherEntityID)
};

struct ScriptComponent {
    // 配置
    std::string scriptClassName;     // "PlayerController"
    
    // 运行时状态
    void* scriptInstance  = nullptr; // 脚本虚拟机内部对象句柄
    ScriptInstanceState state = ScriptInstanceState::Created;
    
    // 缓存函数指针（由 ScriptSystem 初始化时填充）
    ScriptFunctionCache functions;
    
    // 序列化
    void* serializedState = nullptr; // OnSerialize 输出的二进制数据
    uint32 stateSize      = 0;
    
    // 热重载支持
    uint32 scriptVersion  = 0;       // 脚本 DLL 的版本号，用于检测热重载
};

}
```

### 4.2 C# 端基类 (API 给游戏开发者)

```csharp
// C# 端：提供给游戏开发者的基类
public abstract class ScriptBehaviour {
    internal ulong EntityID { get; set; }
    
    // ── 生命周期 ──
    public virtual void OnCreate() { }
    public virtual void OnUpdate(float dt) { }
    public virtual void OnDestroy() { }
    public virtual void OnCollisionEnter(ulong otherEntityID) { }
    
    // ── 便捷访问器 ──
    public Transform transform => new Transform(EntityID);
    public RigidBody rigidbody => new RigidBody(EntityID);
    public CharacterController character => new CharacterController(EntityID);
    
    // ── 静态工具 ──
    protected T GetComponent<T>() where T : Component, new() { ... }
    protected T AddComponent<T>() where T : Component, new() { ... }
    protected void Destroy(ulong entityID) { Engine_Entity_Destroy(entityID); }
    
    // ── 引擎 API 转发 ──
    protected DebugDraw debug => DebugDraw.Instance;
    protected Input input => Input.Instance;
}
```

### 4.3 ScriptSystem — 脚本的 ECS System 化管理

```cpp
// engine/src/Core/ECS/ScriptSystem.cpp (新增)
class ScriptSystem {
public:
    void Init(EntityManager* em, ScriptVM* vm);
    void Update(float dt);
    void OnEntityDestroyed(EntityHandle entity);

private:
    // 每帧执行脚本更新（JobSystem 并行）
    void UpdateScripts(float dt);
    
    // 处理脚本异常（Try-Catch 包裹）
    void SafeInvoke(ScriptComponent& script, void* funcPtr, void* instance, float dt);
    
    // GC 步进控制（防止 GC 引起帧率抖动）
    void StepGarbageCollector(float maxMilliseconds);
    
    // 热重载处理
    void CheckForHotReload();
    void PerformHotReload();
    
    EntityManager* m_EntityManager = nullptr;
    ScriptVM* m_ScriptVM = nullptr;
    float m_GCTimeBudget = 1.0f;  // 每帧最多 1ms 用于 GC
    bool m_HotReloadPending = false;
};
```

**ScriptSystem::Update 执行流程：**

```
ScriptSystem::Update(dt)
  │
  ├── 1. CheckForHotReload()
  │     └── FileWatcher 检测到 Scripts.dll 变更
  │         ├── 挂起所有脚本
  │         ├── 遍历 ScriptComponent 序列化 state 到 m_SerializedState
  │         ├── 卸载旧 AssemblyLoadContext
  │         ├── 加载新 Scripts.dll
  │         ├── 反序列化 state 到新实例
  │         ├── 刷新函数指针缓存
  │         └── 恢复脚本到 Running
  │
  ├── 2. UpdateScripts(dt)  // JobSystem::ParallelFor
  │     └── 对每个 Running 状态的 ScriptComponent:
  │           ├── SafeInvoke(functions.onUpdate, instance, dt)
  │           │     ├── 调用脚本函数 (通过函数指针)
  │           │     ├── 捕获异常 → state = Error
  │           │     └── 记录错误信息到 Console
  │           └── 更新 state 缓存
  │
  └── 3. StepGarbageCollector(m_GCTimeBudget)
        └── GC.TryStartNoGCRegion(maxMilliseconds * 10_000)
            ├── 执行 GC 收集
            └── 结束后 EndNoGCRegion()
```

---

## 五、工业级杀手锏：完美热重载 (Hot-Reloading)

### 5.1 传统热重载的三个死亡原因

| 死亡原因 | 传统做法 | 我们的做法 |
|---------|---------|-----------|
| **数据丢失** | 数据存在脚本实例里 (MonoBehaviour.speed) | **数据存在 C++ ECS 组件里**，脚本只存逻辑和缓存状态 |
| **引用悬挂** | 脚本卸载后 C++ 持有脚本对象的 dangling pointer | **ScriptComponent::scriptInstance 统一失效 + C-ABI 桥接层不传指针** |
| **状态不一致** | 热重载发生在 Update 中间的任意时刻 | **ScriptSystem::Update 的显式检查点 + 域隔离（AssemblyLoadContext）** |

### 5.2 热重载实现架构

```
热重载触发器: FileWatcher 检测 Scripts.dll 修改
         │
         ▼
  ┌──────────────── 阶段 1: 挂起 ────────────────┐
  │ ScriptSystem->SetPaused(true)                 │
  │ 停止 OnUpdate 调用，等待当前帧完成             │
  └───────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────── 阶段 2: 序列化状态 ───────────┐
  │ foreach (entity in View<ScriptComponent>) {    │
  │     Engine_Script_Serialize(instance, &buffer);│
  │     comp->serializedState = buffer;            │
  │     comp->scriptVersion = GetScriptVersion();  │
  │ }                                              │
  └───────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────── 阶段 3: 卸载旧域 ─────────────┐
  │ m_ScriptVM->UnloadAssembly("Scripts.dll");     │
  │ // .NET: 卸载 AssemblyLoadContext              │
  │ // Luau: 关闭 Lua State，创建新 State         │
  └───────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────── 阶段 4: 加载新域 ─────────────┐
  │ m_ScriptVM->LoadAssembly("Scripts.dll");       │
  │ .NET: 新 AssemblyLoadContext + Assembly.Load   │
  └───────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────── 阶段 5: 恢复状态 ─────────────┐
  │ foreach (entity in View<ScriptComponent>) {    │
  │     void* newInstance = Engine_Script_Create(  │
  │         comp->scriptClassName,                 │
  │         comp->serializedState);                │
  │     comp->scriptInstance = newInstance;        │
  │     CacheFunctionPointers(comp);               │
  │     SafeInvoke(comp, comp->functions.onCreate);│
  │ }                                              │
  └───────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────── 阶段 6: 恢复执行 ─────────────┐
  │ ScriptSystem->SetPaused(false)                 │
  │ // 玩家无感知，游戏继续运行                     │
  └───────────────────────────────────────────────┘
```

---

## 六、GC 控制策略 (GC Pause Mitigation)

### 6.1 .NET GC 控制 API

```csharp
// C# 端：在 ScriptSystem.Update 末尾调用
public static class ScriptGC {
    private static long m_BudgetTicks = 10_000; // 1ms = 10,000 ticks
    
    public static void Step(long maxTicks) {
        m_BudgetTicks = maxTicks;
        
        // 方式 1: 尝试非阻塞式 GC (推荐)
        if (System.Runtime.GCSettings.TryStartNoGCRegion(maxTicks * 100)) {
            // 在这个区域内分配不会触发 GC
            // 但如果超出预算，会自动触发阻塞式 GC
            System.Runtime.GCSettings.EndNoGCRegion();
        }
        
        // 方式 2: GC 让步式收集
        // GC.Collect(2, GCCollectionMode.Optimized);
    }
}
```

### 6.2 C++ 端 GC 预算控制

```cpp
void ScriptSystem::StepGarbageCollector(float maxMilliseconds) {
    if (!m_ScriptVM) return;
    
    // 转换到 .NET ticks (100ns 单位)
    int64_t budgetTicks = static_cast<int64_t>(maxMilliseconds * 10'000);
    
    // 记录开始时间
    uint64_t startCycles = __rdtsc();  // CPU cycle 级计时
    
    // 调用 GC Step
    m_ScriptVM->StepGC(budgetTicks);
    
    // 如果超预算，记录警告
    uint64_t elapsedCycles = __rdtsc() - startCycles;
    double elapsedMs = static_cast<double>(elapsedCycles) / m_CPUFreqMHz;
    if (elapsedMs > maxMilliseconds * 1.5f) {
        Log::Warn("[Script] GC exceeded budget: {:.2f}ms (budget {:.2f}ms)", 
                  elapsedMs, maxMilliseconds);
    }
}
```

---

## 七、落地实施路线图

### 阶段 0: C-ABI 层定义 (3-5 天)

| 任务 | 依赖 | 产出 |
|------|------|------|
| 设计 `engine_api.h` 完整 C-ABI 接口 | 无 | 约 80 个 extern "C" 函数 |
| 实现 C-ABI 函数体 (转发到 ECS) | ECS EntityManager | 编译通过的 engine_api.c |
| 编写 API 自动化测试 | 无 | 每个 C-ABI 函数的单元测试 |

### 阶段 1: 虚拟机宿主集成 (1 周)

**选择 C# (CoreCLR) 路径:**

| 任务 | 依赖 | 产出 |
|------|------|------|
| 集成 `coreclrhost.h` (微软官方宿主 API) | C-ABI 完成 | 从 C++ 启动 .NET 运行时 |
| 实现 `ScriptVM` 类 (Load/Unload/Call) | CoreCLR 集成 | C++ ↔ .NET 双向调用 |
| 编写 `scripts/ScriptBridge.cs` (P/Invoke 封装) | coreclrhost.h | C# 端可调用 Engine_Log_Info |
| 实现 `ScriptInstance` (对象创建/释放) | ScriptVM | `new PlayerController()` 在 C# 端 |
| 集成 1: FileWatcher 触发热重载 | FileWatcher 已有 | FileWatcher 检测 → 自动 Reload |

**选择 Luau 路径 (轻量备选):**

| 任务 | 依赖 | 产出 |
|------|------|------|
| 集成 Luau VM (lua luau 库) | 无 | 从 C++ 执行 .luau 文件 |
| 实现 C-ABI → Luau FFI 绑定 | Luau VM | Luau 脚本可调用 Engine_Physics_Raycast |
| 实现 ScriptVM 类 | Luau VM | LoadFile/Reload/CallFunction |
| 集成 FileWatcher 热重载 | FileWatcher 已有 | .luau 文件修改后自动重新加载 |

### 阶段 2: ScriptComponent + ScriptSystem (3 天)

| 任务 | 依赖 | 产出 |
|------|------|------|
| 定义 `ScriptComponent` (C++ 端) | 阶段 1 完成 | ECS 组件可挂载 |
| 实现 `ScriptSystem` | ScriptComponent | 每帧遍历执行 OnUpdate |
| 实现 GC 预算控制 | ScriptSystem | GC.TryStartNoGCRegion 封装 |
| 实现 JobSystem 并行脚本更新 | ScriptSystem + JobSystem | ParallelFor 分片执行 |
| 实现 SafeInvoke (异常隔离) | ScriptSystem | 单脚本崩溃不波及引擎 |

### 阶段 3: 热重载 (3 天)

| 任务 | 依赖 | 产出 |
|------|------|------|
| 实现状态序列化/反序列化 | ScriptComponent | 脚本变量 ↔ JSON buffer |
| 实现 AssemblyLoadContext 隔离 | ScriptVM | 旧 DLL 可安全卸载 |
| 实现热重载管线 (6 阶段) | 上述全部 | 无缝热替换 |
| 集成 ConsoleVariable | 热重载 | 运行时切换脚本版本 |

### 阶段 4: 编辑器集成 (远期)

| 任务 | 依赖 | 产出 |
|------|------|------|
| EditorDemo 中添加 ScriptAsset 浏览器 | Editor 框架 | 查看/打开 .cs 或 .luau 文件 |
| EditorDemo 添加 ScriptComponent Inspector | InspectorPanel | 运行时查看脚本变量 |
| 生成 `.sln` 解决方案文件 | 热重载 | VS 打开即可编译 |
| 连接 ConsolePanel 到脚本日志 | ConsolePanel | 脚本 Log 显示在引擎控制台 |

---

## 八、估算总工作量

| 阶段 | 人天 | 关键交付 |
|------|------|----------|
| 阶段 0: C-ABI 层 | 5 | engine_api.h, 80+ extern C 函数 |
| 阶段 1: CoreCLR 集成 | 7 | ScriptVM, C#↔C++ 双向调用 |
| 阶段 2: ScriptSystem | 3 | ScriptComponent, Update 管线 |
| 阶段 3: 热重载 | 3 | 6 阶段热替换管线 |
| 阶段 4: 编辑器 | 5 | ScriptAsset, Inspector, Console |
| **总计** | **~23 人天** | |

---

## 九、风险和缓解措施

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| CoreCLR 宿主 API 不稳定 | 低 | 高 | 使用 .NET 8 LTS 版本，微软官方支持 |
| 热重载时 AssemblyLoadContext 卸载不完全 | 中 | 中 | 所有脚本对象必须在 C++ 端显式释放，不能有 native→managed 反向引用 |
| C-ABI 函数签名错误导致崩溃 | 中 | 高 | 自动生成 C-ABI 函数签名和 P/Invoke wrapper，避免手写不对齐 |
| 脚本 GC 超出预算导致掉帧 | 低 | 中 | GC.TryStartNoGCRegion + __rdtsc 计时器强制中断 |
| 调试能力不足 | 中 | 中 | 优先集成 VS/Rider 断点调试 (C#)，Luau 可考虑 Roblox Studio 桥接 |

---

## 十、总结与推荐

| 问题 | 答案 |
|------|------|
| **选哪种语言？** | **C# (.NET 8+ CoreCLR)** — 工业标准、强类型、工具链成熟、绩效可控 |
| **架构核心是什么？** | **C-ABI Bridge** — 纯 C 扁平化 API，语言无关，ABI 稳定，无 Name Mangling |
| **数据放哪里？** | **C++ ECS 组件** — 脚本只存逻辑和缓存函数指针，数据由 ECS 管理 |
| **怎么热重载？** | **6 阶段管线** — 挂起→序列化→卸载→加载→反序列化→恢复 |
| **GC 怎么控制？** | **预算分配** — 每帧 1ms 配额，`TryStartNoGCRegion` + `__rdtsc` 计时器 |
| **成本多少？** | **~23 人天** — 含 C-ABI、虚拟机、ScriptSystem、热重载、编辑器 |