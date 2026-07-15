# 工业级脚本引擎集成方案 v2 — 通用数据视图架构

> **生成日期**: 2026-07-15 (v2 重构)  
> **分析范围**: 语言选型、C-ABI 桥接设计、ECS 深度解耦、热重载架构、落地实施路线  
> **当前项目状态**: C++20 ECS 架构 + Jolt Physics v4.0-v7.0 完成 + JobSystem + EventBus + FileWatcher + Vulkan/OpenGL 双后端

---

## 一、核心理念：从"语言绑定"到"数据协议"

### 1.1 旧思维 vs 新思维

| 维度 | v1 方案（旧） | v2 方案（新） |
|------|-------------|-------------|
| **出发点** | "选哪种语言做脚本？" → **语言中心** | "如何让多语言共享数据？" → **数据中心** |
| **集成方式** | 为每种语言写胶水代码（P/Invoke / FFI） | 定义统一数据契约，自动生成多语言绑定 |
| **数据所有权** | 脚本实例持有数据，C++ 通过函数调用读写 | **C++ ECS 池持有数据**，脚本通过内存映射直读 |
| **热重载策略** | 序列化→卸载→加载→反序列化（有拷贝开销） | **数据不动**，只替换逻辑执行体（零拷贝） |
| **多语言支持** | 选一种（C#）深度绑定 | **WASM/C#/Lua/Python 平等**，共享同一数据层 |
| **边界调用风格** | `Engine_Entity_SetPosition(id, x, y, z)` 逐个属性 | `get_component_ptr(id, type)` 返回指针，脚本直接操作内存 |

### 1.2 新架构总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                  引擎核心内存（ECS Component Pools）                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                │
│  │Transform  │  │ RigidBody│  │Collider  │  │  ...     │  (POD 结构体)   │
│  │ x,y,z,rot │  │ vx,vy,vz │  │ type,size│  │          │                │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘                │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────── 组件注册表 + 代码生成器 ───────────────────┐
│  ComponentRegistry (元数据系统)                                │
│  ├── 扫描 C++ 头文件 / 读取中立 DSL (JSON/YAML) 定义          │
│  └── 自动生成各语言的"内存访问代理层"                           │
│        ├── C++:   struct Transform { float x,y,z,rot; };       │
│        ├── Rust:  #[repr(C)] struct Transform { ... }          │
│        ├── C#:    [StructLayout(LayoutKind.Sequential)]         │
│        ├── LuaJIT: ffi.cdef[[ struct Transform { ... } ]]      │
│        └── Python: ctypes.Structure 定义                       │
└────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────── 极小化 C-ABI 层 (约 20 个函数) ───────────────┐
│  不暴露"语义操作"（SetPosition），只暴露"内存访问"              │
│  ├── void*  get_component_ptr(EntityID id, ComponentType type) │
│  ├── EntityID* query_entities(ComponentMask mask, uint32_t* n) │
│  ├── EntityID  create_entity()                                 │
│  ├── void      destroy_entity(EntityID id)                     │
│  └── (极少数工具函数) Engine_Log / Engine_Input / Engine_Time  │
└────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌────────────────── 脚本虚拟机层（可插拔后端） ─────────────────┐
│                                                               │
│  ┌──────────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │   WASM VM    │  │ CoreCLR  │  │ LuaJIT   │  │ Python   │ │
│  │ (Wasmtime)   │  │ (.NET 8) │  │ (ffi)    │  │ (ctypes) │ │
│  │              │  │          │  │          │  │          │ │
│  │ ● 沙盒隔离   │  │ ● JIT    │  │ ● 极速   │  │ ● AI     │ │
│  │ ● 第三方 Mod │  │ ● 调试   │  │ ● 原型   │  │ ● 工具链 │ │
│  │ ● 无 GC      │  │ ● 生态   │  │ ● 灵活   │  │ ● 生态   │ │
│  └──────┬───────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘ │
│         │               │             │             │       │
│          └──────────────┴─────────────┴─────────────┘        │
│              统一通过 get_component_ptr 访问 ECS 内存         │
└────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌────────────── ScriptSystem（语言无关的调度器）──────────────┐
│  LanguageRegistry (映射 "WASM/C#/Lua" → VM 后端)            │
│  ScriptComponent { language, scriptName, scriptConfig }      │
│  System Dispatcher                                          │
│  ├── Step 1: 按语言分组 ScriptComponent                      │
│  ├── Step 2: 对每组调用对应 VM 的 Execute(fn, chunk)         │
│  └── Step 3: 异常隔离 + GC 步进                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、设计决策深度分析

### 2.1 为什么是"极小化 C-ABI"而不是"丰富 C-ABI"？

v1 方案设计了 80+ 个 C-ABI 函数（SetPosition/SetRotation/ApplyForce/Raycast...），每个函数都是"语义操作"。

**问题**：如果有 20 种组件、每种 5 个操作，就是 100 个函数。每种语言都要绑定这 100 个函数。**胶水代码爆炸**。

**v2 解法**：只暴露 3 个核心内存访问函数——

```cpp
extern "C" {
    // ── 核心：内存访问 ──
    void*  Engine_GetComponentPtr(uint64_t entityID, uint32_t componentType);
    // 返回指向 ECS Component Pool 中对应实体数据的指针
    // 脚本拿到指针后，直接按本地语言的 struct 布局读写内存
    
    // ── 查询 ──
    uint64_t* Engine_QueryEntities(uint32_t* componentMask, uint32_t count, uint32_t* outCount);
    // 返回匹配所有指定组件的实体 ID 数组
    
    // ── 实体生命周期 ──
    uint64_t Engine_CreateEntity();
    void     Engine_DestroyEntity(uint64_t id);
    
    // ── 工具（约 10 个） ──
    bool   Engine_Input_GetKey(uint32_t keyCode);
    void   Engine_Log(const char* msg);
    float  Engine_Time_GetDeltaTime();
    // ... 极少量的、无法通过内存访问表达的语义操作
}
```

**为什么这个就够了？**
- `GetComponentPtr` + 预生成的 struct 定义 = 脚本可以直接 `ptr.x = 10`
- 不再需要 `SetPosition`、`SetRotation`、`SetLinearVelocity`…… 脚本拿到指针自己赋值
- 新加组件类型时，**不需要加任何 C-ABI 函数**，只需要更新组件注册表（代码生成）

#### 对比

| 场景 | v1（语义 API） | v2（内存视图 API） |
|------|---------------|-------------------|
| 移动实体位置 | 调用 `Engine_Entity_SetPosition(id, x, y, z)` | 取 `Transform*`，写 `ptr.x = x` |
| 批量更新 1000 个实体 | 循环调用 1000 次 C-ABI（每次跨边界） | 一次查询拿到连续数组，内存直写 |
| 新增 ComponentType | 加 5 个新的 C-ABI 函数 | 更新注册表，重新生成绑定代码 |
| 性能损耗 | 每次 C-ABI 调用约 10-50ns | **零边界开销**（脚本直接读内存） |

### 2.2 多语言如何共享数据？

所有语言通过 `Engine_GetComponentPtr` 拿到的是**同一块物理内存**的指针（或 WASM 中的偏移量）。

```cpp
// ── 引擎端（C++）──
void* Engine_GetComponentPtr(uint64_t entityID, uint32_t componentType) {
    auto& pool = ECS::GetComponentPool(componentType);
    return pool.GetData(entityID);  // 返回指向池中 POD 结构体的指针
}
```

```rust
// ── WASM 端（Rust）──
#[repr(C)]
struct Transform {
    x: f32, y: f32, z: f32,
    rotation: f32,
}

fn on_update( entity_id: u64 ) {
    // 通过导入函数获取指针（WASM 中为线性内存偏移量）
    let ptr = engine_get_component_ptr(entity_id, COMPONENT_TRANSFORM);
    let transform: &mut Transform = unsafe { &mut *(ptr as *mut Transform) };
    transform.x += 1.0;  // 直接修改引擎 ECS 池中的数据！
}
```

```csharp
// ── C# 端 ──
[StructLayout(LayoutKind.Sequential)]
struct Transform {
    public float x, y, z, rotation;
}

unsafe void OnUpdate(ulong entityID) {
    IntPtr ptr = Engine_GetComponentPtr(entityID, ComponentType.Transform);
    Transform* t = (Transform*)ptr.ToPointer();
    t->x += 1.0f;  // 直接修改引擎 ECS 池中的数据！
}
```

```lua
-- ── LuaJIT 端 ──
ffi.cdef[[
    typedef struct { float x, y, z, rotation; } Transform;
]]
local Transform_ptr = ffi.typeof("Transform*")

function on_update(entity_id)
    local ptr = engine_get_component_ptr(entity_id, COMPONENT_TRANSFORM)
    local t = ffi.cast(Transform_ptr, ptr)
    t.x = t.x + 1.0  -- 直接修改引擎 ECS 池中的数据！
end
```

**关键洞察**：所有语言最终操作的**是同一块物理内存**。C++ 修改了 Transform，WASM 下一帧读到最新值，无需序列化、无需同步。

### 2.3 WASM 如何处理 32 位地址空间限制？

WASM 运行在 32 位线性内存中，无法直接持有 64 位 C++ 指针。

**解法：宿主内存映射（Memory Import）**

```
WASM 线性内存 (32位地址空间)
┌─────────────────────────────────────┐
│  WASM 代码 + 堆栈                   │
├─────────────────────────────────────┤
│  Mapped Region (由宿主预映射)        │
│  ├── Transform[0..N]  ← 指向 C++ ECS│
│  ├── RigidBody[0..M]  ← 指向 C++ ECS│
│  └── Collider[0..K]   ← 指向 C++ ECS│
└─────────────────────────────────────┘
         ▲                    ▲
         │  wasmtime::Memory   │ 共享同一块物理页
         │  import             │
         ▼                    ▼
C++ ECS Component Pool (物理内存)
┌─────────────────────────────────────┐
│  Transform[0..N] (真实数据所在地)    │
│  RigidBody[0..M]                    │
└─────────────────────────────────────┘
```

**具体实现（Wasmtime C++ API）：**

```cpp
// 引擎初始化时
void SetupWasmMemoryMapping() {
    // 1. 创建 Wasmtime 引擎和存储
    auto engine = wasmtime::Engine::New();
    auto store = wasmtime::Store::New(engine);
    
    // 2. 创建 WASM 线性内存，大小 = ECS 池所需
    auto memory_type = wasmtime::MemoryType::New({ .min = pages_needed });
    auto wasm_memory = wasmtime::Memory::New(store, memory_type);
    
    // 3. 关键：将 ECS 组件池的内存物理页映射到 WASM 线性内存的对应区域
    //    这不是 memcpy，而是页表级别的映射（零拷贝）
    //    使用 platform-specific 的共享内存机制：
    //    - Linux: mmap with MAP_SHARED
    //    - Windows: CreateFileMapping + MapViewOfFile
    MapECSComponentPoolToWasmMemory(
        ecs.GetPoolPtr<Transform>(),
        wasm_memory.Data(store) + TRANSFORM_OFFSET,
        ecs.GetPoolSize<Transform>()
    );
    
    // 4. 将 memory 作为 import 传递给 WASM 实例
    auto instance = wasmtime::Instance::New(store, module, {wasm_memory});
    
    // 5. WASM 端的 Transform* 指针 = TRANSFORM_OFFSET + index * sizeof(Transform)
    //    直接读写即可，物理上操作的就是 C++ ECS 池
}
```

**没有 mmap 可用怎么办？**（如 Windows 上 wasmtime 的限制）

后备方案：使用 **`memcpy` 同步窗口**（性能低于页映射，但实现简单）：

```cpp
// 每帧开始时：ECS → WASM 内存（一次批量拷贝）
memcpy(wasm_memory + TRANSFORM_OFFSET, ecs.GetPoolPtr<Transform>(), pool_size);

// WASM 执行所有脚本逻辑（读写本地拷贝）

// 每帧结束时：WASM 内存 → ECS（一次批量拷贝回写）
memcpy(ecs.GetPoolPtr<Transform>(), wasm_memory + TRANSFORM_OFFSET, pool_size);
```

对 10 万个 Transform（每个 16 字节）= 1.6MB 的单向拷贝，在 DDR5 上约 0.05ms，完全可以接受。相比每个实体调用一次 C-ABI 的 10-50ns × 100k = 1-5ms，**批量拷贝反而更快**。

---

## 三、组件注册表与代码生成

### 3.1 组件定义（中立格式）

```yaml
# engine/scripts/component_defs/transform.yaml
name: Transform
namespace: Engine
size: 16  # 4 floats × 4 bytes
fields:
  - name: x
    type: float
    offset: 0
  - name: y
    type: float
    offset: 4
  - name: z
    type: float
    offset: 8
  - name: rotation
    type: float
    offset: 12
```

或者直接从 C++ 头文件解析（使用 Clang libTooling）：

```cpp
// engine/include/Engine/Core/ECS/Components.h
#pragma once

// @script_component
struct Transform {
    float x, y, z;
    float rotation;
};

// @script_component
struct RigidBody {
    float vx, vy, vz;
};

// @script_component  
struct Collider {
    uint32_t type;  // 0=box, 1=sphere, 2=capsule
    float radius;
    float height;
};
```

### 3.2 代码生成器

```
Clang LibTooling / 自定义 DSL 解析器
         │
         ▼
  Component Definitions (中间表示)
         │
         ├──→ C++ Header Generator
         │     └──→ engine/include/Engine/Core/ECS/Components.generated.h
         │
         ├──→ Rust Binding Generator  
         │     └──→ scripts/wasm/src/components.generated.rs
         │
         ├──→ C# Binding Generator
         │     └──→ scripts/csharp/Components.generated.cs
         │
         ├──→ LuaJIT FFI Generator
         │     └──→ scripts/lua/components_generated.lua
         │
         ├──→ Python ctypes Generator
         │     └──→ scripts/python/components_generated.py
         │
         └──→ Component Registry (C++)
               └──→ engine/src/Core/ECS/ComponentRegistry.generated.cpp
                     (包含每个组件的 typeID、size、offset 元数据)
```

**生成的 C# 代码示例：**

```csharp
// Components.generated.cs — 自动生成，手动勿改
[StructLayout(LayoutKind.Sequential)]
public struct Transform {
    public float x;
    public float y;
    public float z;
    public float rotation;
}

public static class ComponentType {
    public const uint32 Transform = 0;
    public const uint32 RigidBody = 1;
    public const uint32 Collider  = 2;
}

public static class EngineAPI {
    [DllImport("engine_core", CallingConvention = CallingConvention.Cdecl)]
    public static extern unsafe void* Engine_GetComponentPtr(
        ulong entityID, uint32 componentType);
    
    [DllImport("engine_core", CallingConvention = CallingConvention.Cdecl)]
    public static extern unsafe ulong* Engine_QueryEntities(
        uint32* componentMask, uint32 maskCount, out uint32 outCount);
}
```

**生成的 Rust 代码示例：**

```rust
// components.generated.rs — 自动生成
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Transform {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub rotation: f32,
}

pub const COMPONENT_TRANSFORM: u32 = 0;
pub const COMPONENT_RIGIDBODY: u32 = 1;

extern "C" {
    pub fn Engine_GetComponentPtr(entity_id: u64, component_type: u32) -> *mut std::ffi::c_void;
    pub fn Engine_QueryEntities(mask: *const u32, count: u32, out_count: *mut u32) -> *mut u64;
}
```

---

## 四、ScriptComponent 与 ScriptSystem（语言无关）

### 4.1 ScriptComponent

```cpp
// engine/include/Engine/Core/ECS/ScriptComponent.h
#pragma once

#include "Engine/Types.h"
#include <string>

namespace Engine {

enum class ScriptLanguage : uint8 {
    WASM = 0,   // Rust/Zig → .wasm (沙盒, 第三方 Mod)
    CSharp,     // C# → .NET DLL (高性能, 工具链完善)
    Lua,        // LuaJIT (快速原型)
    Python,     // CPython (AI/工具链集成)
    COUNT
};

// 存储语言相关的执行上下文
struct ScriptExecutionContext {
    void* vmHandle       = nullptr;  // VM 内部句柄
    void* functionTable  = nullptr;  // 缓存函数指针表
    uint32 scriptVersion = 0;        // 热重载版本号
};

struct ScriptComponent {
    // ── 配置（由编辑设置）──
    ScriptLanguage language = ScriptLanguage::CSharp;
    std::string scriptName;      // "PlayerController" (类名 / 模块名)
    std::string scriptPath;      // 文件路径 (用于 FileWatcher)
    
    // ── 运行时状态 ──
    ScriptExecutionContext context;
    bool isPaused = false;
    bool hasError  = false;
    std::string errorMessage;
    
    // ── 序列化（热重载支持）──
    void* serializedState = nullptr;
    uint32 stateSize      = 0;
};

}
```

### 4.2 ScriptSystem（语言无关的调度器）

```cpp
// engine/src/Core/ECS/ScriptSystem.cpp
class ScriptSystem {
public:
    void Init(EntityManager* em) {
        // 注册所有语言后端
        m_LanguageRegistry.Register<WasmVM>(ScriptLanguage::WASM);
        m_LanguageRegistry.Register<CoreCLRVM>(ScriptLanguage::CSharp);
        m_LanguageRegistry.Register<LuaVM>(ScriptLanguage::Lua);
        m_LanguageRegistry.Register<PythonVM>(ScriptLanguage::Python);
    }
    
    void Update(float dt) {
        // 1. 检查热重载（FileWatcher 触发）
        CheckHotReloads();
        
        // 2. 按语言分组
        auto groups = GroupByLanguage<ScriptComponent>(m_EntityManager);
        
        // 3. 对每种语言，批量执行
        for (auto& [lang, entities] : groups) {
            auto* vm = m_LanguageRegistry.Get(lang);
            if (!vm) continue;
            
            // ── 关键：传递给 VM 的是 EntityID 数组 ──
            // VM 内部通过 Engine_GetComponentPtr 批量读取/写入 ECS 内存
            vm->ExecuteBatch(entities.data(), entities.size(), dt);
            
            // 4. GC 步进（对需要 GC 的语言）
            vm->StepGC(1.0f);  // 预算 1ms
        }
    }

private:
    LanguageRegistry m_LanguageRegistry;
    FileWatcher m_FileWatcher;
};
```

### 4.3 语言后端的统一接口

```cpp
// engine/include/Engine/Scripting/IScriptVM.h
class IScriptVM {
public:
    virtual ~IScriptVM() = default;
    
    // 生命周期
    virtual bool Init(const std::string& scriptRoot) = 0;
    virtual void Shutdown() = 0;
    
    // 执行
    virtual void ExecuteBatch(EntityID* entities, uint32_t count, float dt) = 0;
    
    // 热重载
    virtual bool CheckForReload(const std::string& path) = 0;
    virtual void Reload() = 0;
    
    // GC 控制
    virtual void StepGC(float maxMilliseconds) = 0;
    
    // 异常安全
    virtual bool IsHealthy() const = 0;
    virtual std::string GetLastError() const = 0;
};
```

---

## 五、多语言热重载方案对比

### 5.1 核心原则

> **数据不动，逻辑替换。**

所有组件的 POD 数据始终在 C++ ECS 池中。热重载只替换"如何解释/处理这些数据"的逻辑。

### 5.2 各语言的具体差异

| 语言 | 热重载触发 | 替换方式 | 数据恢复 | 状态序列化 |
|------|-----------|---------|---------|-----------|
| **WASM** | `.wasm` 文件变更 | 重新实例化 Module + 恢复 Memory Import | **零拷贝** — 数据在 C++ 池中，重新挂载即可 | 不需要 |
| **C#** | `.dll` 文件变更 | 卸载旧 AssemblyLoadContext，加载新的 | 重新调用 `Engine_GetComponentPtr` 获取指针 | 脚本内部变量需要序列化 |
| **LuaJIT** | `.lua` 文件变更 | 清除全局表，重新 `dofile` | 重新通过 FFI 获取 C 指针 | 全局变量存到 C++ 侧的 `ScriptComponent::serializedState` |
| **Python** | `.py` 文件变更 | `importlib.reload()` | 同 Lua | 同 Lua |

### 5.3 WASM 热重载详解（最优路径）

```
触发: FileWatcher 检测到 scripts.wasm 修改
  │
  ├── Step 1: 暂停帧（标记 ScriptSystem::m_Paused = true）
  │      └── 当前帧正在执行的 WASM 逻辑完成后不再调度新帧
  │
  ├── Step 2: 保留 ECS 数据（什么都不用做！）
  │      └── Transform/RigidBody 等数据一直在 C++ 池里
  │
  ├── Step 3: 创建新 WASM 实例
  │      ├── 编译新的 .wasm Module
  │      ├── 创建新的 Instance，传入：
  │      │     ├── 同一块 Memory（共享内存映射）
  │      │     └── 同一组 Import Functions
  │      └── 获取新的导出函数指针 (on_update, on_init)
  │
  ├── Step 4: 重新挂载
  │      └── 遍历所有 ScriptComponent { language = WASM }：
  │            ├── 用新实例的 functionTable 替换旧的
  │            ├── 调用新脚本的 on_init(entityID)
  │            └── 脚本通过 Engine_GetComponentPtr 读到的是旧数据！
  │                （因为 ECS 池从未被清理）
  │
  └── Step 5: 恢复执行（ScriptSystem::m_Paused = false）
         └── 下一帧开始，新逻辑运行在旧数据上，玩家无感知
```

**WASM 热重载核心优势**：不需要序列化、不需要 memcpy、不需要反序列化。数据始终在 ECS 池的一处，新旧脚本只是"看数据的视角"不同。

### 5.4 C# 热重载详解（需要序列化的场景）

C# 脚本可能在 `ScriptBehaviour` 子类中有私有字段（如 `private float m_Speed = 5.0f`）。这些字段不在 ECS 组件中，需要序列化。

```
触发: FileWatcher 检测到 Scripts.dll 修改
  │
  ├── Step 1: 暂停
  │
  ├── Step 2: 序列化状态
  │      └── 对每个 ScriptComponent { language = CSharp }：
  │            ├── 调用 Engine_Script_Serialize(instance) → JSON buffer
  │            ├── 存储到 comp.serializedState
  │            └── 释放旧实例
  │
  ├── Step 3: 卸载旧 AssemblyLoadContext
  │      └── .NET: alc.Unload() + 等待 GC 回收
  │
  ├── Step 4: 加载新 DLL
  │      └── 新 AssemblyLoadContext → Assembly.Load("Scripts.dll")
  │
  ├── Step 5: 反序列化状态
  │      └── 对每个 ScriptComponent：
  │            ├── 创建新实例 (new PlayerController())
  │            ├── 通过 Engine_GetComponentPtr 重新获取 Transform 指针
  │            ├── 反序列化 JSON → 恢复私有字段
  │            └── 调用 OnCreate()
  │
  └── Step 6: 恢复执行
```

### 5.5 三种热重载模式的选择

| 模式 | 延迟 | 数据安全性 | 适用语言 | 适用场景 |
|------|------|-----------|---------|---------|
| **零拷贝** | < 1ms | 最高（数据不动） | WASM | 核心 Gameplay 逻辑 |
| **部分序列化** | 1-5ms | 高（ECS 数据不动，仅脚本字段序列化） | C# | 有状态脚本类 |
| **完全序列化** | 5-50ms | 中（全部数据走序列化管道） | Lua/Python | 简单逻辑/工具脚本 |

---

## 六、多语言共存策略

### 6.1 语言选择指南

| 语言 | 推荐用途 | 理由 |
|------|---------|------|
| **WASM (Rust/Zig)** | 核心 Gameplay、第三方 Mod、网络同步 | 沙盒安全、无 GC、确定性、原生性能 |
| **C# (.NET 8)** | 编辑器工具、UI 逻辑、资产管线 | IDE 支持、NuGet 生态、强类型 |
| **LuaJIT** | 快速原型、配置文件、Mod 脚本 | 极简语法、FFI 直接、零安装 |
| **Python** | AI/ML 集成、工具链、测试脚本 | 数据科学生态、动态灵活 |

### 6.2 混合使用示例

```lua
-- init_player.lua (Lua 快速原型)
function on_create(entity_id)
    -- 通过 FFI 直接操作 ECS 内存
    local t = ffi.cast("Transform*", engine_get_component_ptr(entity_id, COMPONENT_TRANSFORM))
    t.x = 0; t.y = 10; t.z = 0
    
    -- 调用 C# 实现的复杂逻辑
    engine_invoke_csharp("InventorySystem", "GiveItem", entity_id, "sword", 1)
end
```

```csharp
// PlayerController.cs (C# 核心逻辑)
public unsafe class PlayerController : ScriptBehaviour {
    private float m_Speed = 5.0f;
    private float m_Health = 100.0f;
    
    public override void OnUpdate(float dt) {
        Transform* t = GetComponentPtr<Transform>();
        RigidBody* r = GetComponentPtr<RigidBody>();
        
        if (Engine_Input_GetKey(KEY_W)) {
            r->vz = m_Speed;  // 直接修改物理组件的速度
        }
        
        // 调用 WASM 实现的伤害计算
        float damage = Engine_InvokeWasm("DamageSystem", "Calculate", m_Health);
        if (damage > 0) m_Health -= damage;
    }
}
```

### 6.3 跨语言调用

```
C# Script
    │
    ├── 直接调用: 通过 C-ABI (Engine_GetComponentPtr) → ECS 内存
    │
    └── 跨语言: ScriptSystem 内部路由
          ├── 注册表: Map<function_name, {language, vm, entry_point}>
          └── Engine_Invoke("DamageSystem.Calculate", args)
                ├── 查找注册表 → WASM
                ├── 切换到 WASM VM 执行
                └── 返回结果
```

---

## 七、架构师决策记录

### 决策 1: 极小化 C-ABI + 内存视图（否定了"丰富 C-ABI"方案）

**背景**: v1 方案设计了 80+ 语义 API，每个组件操作对应 N 个函数。  
**决策**: 改为只暴露内存访问 API（`GetComponentPtr` + `QueryEntities`），加上极少量的工具函数。  
**理由**: 
- 新增组件不需要新增 C-ABI 函数
- 所有语言通过 struct 布局直接读写内存，零边界开销
- 批量操作天然高效（一次查询，连续内存读写）

### 决策 2: 数据在 ECS 池，脚本只做逻辑（否定了"脚本持有数据"方案）

**背景**: Unity/MonoBehaviour 模式中，数据在脚本实例里，热重载需要序列化。  
**决策**: 所有运行时可变数据强制放在 C++ ECS 组件中，脚本只持有逻辑和缓存状态。  
**理由**: 
- 热重载时数据不动（ECS 池持久化），脚本替换零成本
- 多语言共享数据无需同步（同一块物理内存）
- Gameplay 逻辑可以无缝在 C++ 和脚本之间迁移

### 决策 3: 代码生成代替手写绑定

**背景**: 手写 P/Invoke、FFI 绑定容易出错、维护成本高。  
**决策**: 基于 Clang LibTooling 或 YAML 定义，自动生成各语言的 struct 定义和绑定代码。  
**理由**: 
- 组件 字段变更时，所有语言的绑定自动更新
- 消除 手写不对齐导致的内存错位 bug
- 增加 新语言支持只需要加一个代码生成器后端

### 决策 4: 多语言优先，不限制用户选择

**背景**: v1 方案锁定 C#。  
**决策**: 设计语言无关的 ScriptSystem，WASM/C#/Lua/Python 共享同一数据层。  
**理由**: 
- WASM 提供安全沙盒（第三方 Mod）
- C# 提供完整 IDE 体验（内部核心逻辑）
- Lua 提供快速原型（策划脚本）
- Python 提供 AI/ML 集成（工具链）

---

## 八、落地实施路线图

### 阶段 0: 基础设施（5 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| 实现 Component Registry（元数据系统） | 2天 | `ComponentRegistry` 运行时组件类型查询 |
| 实现代码生成器（Clang libTooling / YAML 解析） | 2天 | 从 C++ 头文件生成多语言 binding |
| 定义极小 C-ABI 层（`Engine_GetComponentPtr` + `Engine_QueryEntities` + 工具函数） | 1天 | `engine_api.h` (约 15-20 个函数) |

### 阶段 1: WASM 后端（5 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| 集成 Wasmtime C++ SDK | 1天 | WASM VM 可加载执行 |
| 实现 WASM Memory Import（ECS 池映射） | 1天 | WASM 脚本可直接读写 ECS 内存 |
| 实现 C-ABI Import Functions（供 WASM 调用） | 1天 | WASM 可调用 `Engine_Log` / `Engine_Input_GetKey` |
| 实现 WASM 脚本的 ExecuteBatch | 1天 | 批量执行 WASM System |
| WASM 热重载（零拷贝模式） | 1天 | FileWatcher → 无缝替换 |

### 阶段 2: C# 后端（5 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| 集成 CoreCLR 宿主 API | 2天 | C++ 启动 .NET 运行时 |
| 实现 C# → C-ABI 的自动 P/Invoke 封装（代码生成器生成） | 1天 | C# 可直接调用 `Engine_GetComponentPtr` |
| 实现 ExecuteBatch（C# unsafe 指针操作） | 1天 | C# 脚本每帧执行 OnUpdate |
| C# 热重载（AssemblyLoadContext + 部分序列化） | 1天 | DLL 热替换 |

### 阶段 3: ScriptSystem + ScriptComponent（3 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| 实现 `ScriptComponent`（语言无关版本） | 1天 | ECS 组件可挂载 |
| 实现 `ScriptSystem`（按语言分组的调度器） | 1天 | 多语言脚本每帧执行 |
| 实现 SafeInvoke 异常隔离 + GC 步进 | 1天 | 单脚本崩溃不波及引擎 |

### 阶段 4: LuaJIT + Python 后端（额外 4 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| LuaJIT FFI 集成 | 1天 | Lua 通过 FFI 调用 `Engine_GetComponentPtr` |
| Lua 热重载 | 1天 | dofile 重新加载 |
| CPython 集成 | 1天 | Python ctypes 调用 C-ABI |
| Python 热重载 | 1天 | importlib.reload() |

### 阶段 5: 编辑器集成（5 天）

| 任务 | 工时 | 产出 |
|------|------|------|
| ScriptComponent Inspector 面板 | 2天 | 选择语言 + 脚本文件 |
| WASM/C#/Lua 脚本资产浏览器 | 1天 | 查看工程中的脚本文件 |
| ConsolePanel 连接脚本日志 | 1天 | 脚本 Log 显示在引擎控制台 |
| 代码生成集成到 CMake 构建 | 1天 | 构建时自动生成 binding 代码 |

---

## 九、工作量总览

| 阶段 | 人天 | 关键交付 |
|------|------|----------|
| **阶段 0**: 基础设施 | 5天 | ComponentRegistry + 代码生成器 + 极小 C-ABI |
| **阶段 1**: WASM 后端 | 5天 | WASM VM + 内存映射 + 零拷贝热重载 |
| **阶段 2**: C# 后端 | 5天 | CoreCLR 宿主 + P/Invoke 自动生成 |
| **阶段 3**: ScriptSystem | 3天 | 语言无关调度器 + 异常隔离 |
| **阶段 4**: LuaJIT + Python | 4天 | 额外语言后端 |
| **阶段 5**: 编辑器集成 | 5天 | Inspector + 资产浏览 + 构建管线 |
| **总计** | **~27 人天** | （支持 4 种语言的全功能脚本系统） |

### 最小可行产品（MVP）路径

如果资源有限，可以先实现 阶段 0 + 阶段 1 + 阶段 3 = **13 天**，获得 **WASM-only** 但完整的脚本系统：

```
MVP: [阶段0] C-ABI + 代码生成 → [阶段1] WASM 后端 → [阶段3] ScriptSystem
      └── 支持：Rust/Zig 编写游戏逻辑、零拷贝热重载、安全沙盒、ECS 内存直写
```

后续再依次添加 C#、Lua、Python 后端。

---

## 十、总结

| 问题 | 答案 |
|------|------|
| **架构核心是什么？** | **通用数据视图（Universal Data View）** — 不绑定具体语言，通过统一内存视图 + 极小 C-ABI 让多语言共享 ECS 数据 |
| **C-ABI 有多少函数？** | **~15-20 个**（v1 的 80+ 缩减为 3 个核心 + 约 12 个工具函数），新加组件类型不需要新增 C-ABI 函数 |
| **数据在哪里？** | **C++ ECS 组件池** — 脚本通过 `Engine_GetComponentPtr` 获取指针，直接读写内存 |
| **怎么支持多语言？** | **代码生成器** — 从统一组件定义自动生成 C++/Rust/C#/Lua/Python 的 struct 和绑定代码 |
| **怎么热重载？** | **数据不动，只换逻辑** — ECS 池持久化，WASM 零拷贝热重载，C# 部分序列化 |
| **WASM 怎么处理 32 位地址？** | **宿主内存映射** — WASM 线性内存直接映射到 ECS 组件池物理内存，零拷贝 |
| **MVP 需要多久？** | **13 天** — 阶段 0 + 阶段 1 + 阶段 3，获得 WASM-only 但完整的脚本系统 |
| **全功能需要多久？** | **~27 人天** — 支持 WASM/C#/Lua/Python 四种语言 |