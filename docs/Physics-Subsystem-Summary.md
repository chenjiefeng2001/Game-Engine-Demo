# 物理子系统实现总结报告

> **生成日期**: 2026-07-08 (v5.0 更新)  
> **分析范围**: `engine/include/Engine/Core/Physics/`、`engine/src/Core/Physics/`、`engine/include/Engine/Box2D/`、`engine/include/Engine/Jolt/`、`engine/src/Jolt/`、`engine/include/Engine/Core/ECS/`、`engine/src/Core/ECS/`、`engine/src/OpenGL/Resources/`  
> **物理引擎后端**: Box2D v3.x (2D)、Jolt Physics v5.5 (3D)、Bare2D (自制 2D)

---

## 一、架构总览

物理子系统采用**分层抽象模式**（与 RHI 设计理念一致），核心架构如下：

```
PhysicsSystemManager (统一管理器)
│  BackendType (Jolt5 / PhysX5 / Bullet3)
│  InitGlobalContext() / ShutdownGlobalContext()
│
├── 2D 物理
│   ├── IPhysicsWorld (抽象接口) ─── Box2DPhysicsWorld / Bare2DPhysicsWorld (实现)
│   ├── IPhysicsBody  (抽象接口) ─── Box2DPhysicsBody  / Bare2DPhysicsBody  (实现)
│   ├── IJoint        (抽象接口) ─── Box2DJoint         (实现)
│   ├── IForceGenerator             ─── GravityForce, LinearDragForce
│   └── IPhysicsDebugDraw          ─── OpenGLPhysicsDebugDraw (实现)
│
├── 3D 物理
│   ├── IPhysicsWorld3D (抽象接口) ─── JoltPhysicsWorld (实现)
│   │   ├── BatchSetKinematicTargets() / BatchGetTransforms()  ← v4.0 批量 API
│   │   ├── ProcessCollisionEvents()                           ← v4.0 主线程碰撞消费
│   │   └── GetPhysicsSystem() + JoltDebugRenderer             ← v4.0
│   ├── IPhysicsBody3D  (抽象接口) ─── JoltPhysicsBody (实现)
│   │   └── GetRotationQuat() / SetRotationQuat()              ← v4.0 四元数直通
│   ├── IJoint3D        (抽象接口) ─── 未实现 (占位)
│   └── IPhysicsDebugDraw3D       ─── OpenGLPhysicsDebugDraw3D (实现)
│
├── ECS 集成
│   ├── PhysicsComponents.h       ─── ECS 组件定义 (配置 + 运行期状态分离)
│   │   ├── BoxCollider3DComponent   ─── isDirty + MarkDirty()  ← v4.0
│   │   ├── SphereCollider3DComponent                            ← v4.0
│   │   └── CapsuleCollider3DComponent                           ← v4.0
│   ├── PhysicsSyncSystem         ─── 三级同步管线 + Collider 拓扑同步 ← v4.0
│   └── TransformComponent        ─── 四元数内部存储 m_RotationQuat  ← v4.0
│
├── 碰撞事件管道 (v4.0 重构)
│   ├── LockFreeEventQueue        ─── MPSC 无锁队列 (只传 BodyID, 不传指针)
│   ├── ContactListenerImpl       ─── 只 Push BodyID，不 touch Body
│   └── ProcessCollisionEvents()  ─── 主线程安全反查 + 路由
│
└── 基础设施
    ├── FixedTimestepAccumulator  ─── 固定步长累加器
    ├── PhysicsLayers.h           ─── ObjectLayer / BroadPhaseLayer 映射
    └── JoltDebugRenderer         ─── JPH::DebugRenderer → IPhysicsDebugDraw3D 适配器
```

### 设计原则 (v4.0 + v5.0)

| 原则 | 说明 | 对应实现 |
|------|------|----------|
| **纯虚接口隔离** | 引擎核心层只依赖抽象，不暴露第三方类型 | `IPhysicsWorld` / `IPhysicsBody` |
| **抽象工厂 + 后端枚举** | `BackendType::Jolt5/PhysX5/Bullet3`，预留全局上下文 | `PhysicsSystemManager` |
| **数据与状态分离** | RigidBody3DComponent (配置) vs PhysicsRuntimeComponent (运行期) | `PhysicsComponents.h` |
| **RAII 生命周期** | `std::shared_ptr` / `std::weak_ptr` 管理物理世界和刚体 | `PhysicsComponent3D.cpp` |
| **固定步长解耦** | `FixedTimestepAccumulator` + 渲染插值不写回 Transform | `PhysicsSyncSystem` 三级管线 |
| **四元数内部存储** | TransformComponent 使用 `Quat` 避免万向锁 | `TransformComponent.h` v4.0 |
| **碰撞事件无锁传递** | Worker 线程只传 BodyID，绝不传指针 | `LockFreeEventQueue` / `ContactListenerImpl` v4.0 |
| **运行时 Collider 拓扑变更** | 通过 BodyLockWrite + SetShape 不销毁重建 Body | `PhysicsSyncSystem::SyncECSToPhysics` v4.0 |
| **窄阶段精确查询** | QuerySphere 使用 CollideShape 替代 AABB 近似 | `JoltPhysicsWorld::QuerySphere` v4.0 |
| **批量操作** | BatchSetKinematicTargets 避免逐体虚函数调用 | `IPhysicsWorld3D` / `JoltPhysicsWorld` v4.0 |
| **Thread-Local DebugRenderer** | 每个 Worker 线程独立 buffer，写入完全无锁 | `JoltDebugRenderer` v5.0 TLS |
| **ShapeDef 整体替换** | AdFixture语义废弃, SetShape整体替换, 自动重算惯性 | `IPhysicsBody3D::SetShape()` v5.0 |
| **3D 关节系统** | Hinge/Ball/Slider/SixDOF 映射到 Jolt TwoBodyConstraint | `JoltJoint3D` v5.0 |

---

## 二、文件清单与职责

### 2.1 接口层 (engine/include/Engine/Core/Physics/)

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `IPhysicsBody.h` | 69 | 2D 刚体接口：31 个纯虚方法 |
| `IPhysicsWorld.h` | 99 | 2D 世界接口：Step/CreateBody/碰撞回调 |
| `IPhysicsBody3D.h` | 89 | 3D 刚体接口：33 个纯虚方法 |
| `IPhysicsWorld3D.h` | ~145 | 3D 世界接口：Init/Shutdown/Step + **BatchSetKinematicTargets/BatchGetTransforms (v4.0)** |
| `IPhysicsDebugDraw.h` | 65 | 2D 调试绘制接口 |
| `IPhysicsDebugDraw3D.h` | 54 | 3D 调试绘制接口 |
| `IJoint.h` | 83 | 2D 关节接口 |
| `IJoint3D.h` | 117 | 3D 关节接口：Fixed/Hinge/Slider/Ball/Distance/Spring/SixDOF |

### 2.2 数据结构层 (engine/include/Engine/Core/Physics/)

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `PhysicsDefs.h` | 436 | 2D 类型定义：BodyType/ShapeType/7种JointDef/ContactManifold/CollisionLayers |
| `PhysicsDefs3D.h` | 186 | 3D 类型定义：ShapeType3D(含ConvexHull/Mesh/Compound) |
| `PhysicsLayers.h` | 89 | Jolt ObjectLayer 8层映射 + BroadPhaseLayer + 两两碰撞过滤 |
| `FixedTimestepAccumulator.h` | 81 | 固定步长累加器 + 螺旋式死亡保护 |
| **`LockFreeEventQueue.h`** | ~100 | **v4.0: CollisionEvent 只传 bodyIDA/bodyIDB (uint64)，移除 Vec3 指针** |
| `PhysicsSystemManager.h` | ~130 | 系统管理器 + **BackendType 枚举 + InitGlobalContext/ShutdownGlobalContext (v4.0)** |

### 2.3 2D 物理实现

| 文件 | 行数 | 内容 |
|------|------|------|
| `Bare2DPhysicsBody.h/.cpp` | 234+283 | 自制 2D 刚体：SAT/Fixture/插值/休眠 |
| `Bare2DPhysicsWorld.h/.cpp` | 314+1302 | 完整物理管线：空间网格/SAT/Sequential Impulse/Baumgarte/Warm Starting/CCD/多线程 |
| `Box2DPhysicsWorld.h/.cpp` | 99+560 | Box2D v3.x 适配：6种关节/RayCast/AABB/碰撞事件轮询 |

### 2.4 3D 物理实现 (Jolt Physics) — v4.0 重点重构

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `JoltPhysicsWorld.h` | ~170 | **v4.0: 添加 LockFreeEventQueue/ProcessCollisionEvents/Batch API/GetPhysicsSystem/JoltDebugRenderer** |
| `JoltPhysicsWorld.cpp` | ~580 | **v4.0: ContactListenerImpl 只持 EventQueue 指针/OnContactRemoved 从 SubShapeIDPair 安全取 BodyID/ProcessCollisionEvents 主线程反查/BatchSetKinematicTargets/QuerySphere 窄阶段/DebugDraw 全功能** |
| `JoltPhysicsBody.h` | ~96 | **v4.0: 添加 GetRotationQuat/SetRotationQuat** |
| `JoltPhysicsBody.cpp` | 267 | **v4.0: 实现四元数直通方法** |
| **`JoltDebugRenderer.h`** | ~60 | **v4.0 新增: 继承 JPH::DebugRenderer，转发到 IPhysicsDebugDraw3D** |
| **`JoltDebugRenderer.cpp`** | ~130 | **v4.0 新增: 完整实现 + std::mutex 多线程保护** |
| `JoltJobSystemAdapter.h` | 87 | Jolt ↔ Engine::JobSystem 适配器 |

### 2.5 ECS 集成

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `PhysicsComponents.h` | ~100 | **v4.0: Collider3DComponent 添加 isDirty + MarkDirty()** |
| `PhysicsSyncSystem.h` | 92 | 三级同步管线声明 |
| **`PhysicsSyncSystem.cpp`** | ~165 | **v4.0: 重构——Collider 拓扑同步(BodyLock+SetShape)/ProcessCollisionEvents 集成/四元数直通/BackupState 双缓冲** |
| `PhysicsComponent.h` | 144 | 2D GameObject 组件 |
| `PhysicsComponent3D.h` | 86 | 3D GameObject 组件声明 |
| **`PhysicsComponent3D.cpp`** | ~76 | **v4.0: SyncPhysicsToTransform/SyncTransformToPhysics 使用四元数** |
| **`TransformComponent.h/.cpp`** | ~100+195 | **v4.0 重构: 内部存储 Quat m_RotationQuat，欧拉角惰性计算** |

### 2.6 调试绘制

| 文件 | 行数 | 内容 |
|------|------|------|
| `OpenGLPhysicsDebugDraw.cpp` | 344 | 2D OpenGL 调试绘制 (GLSL 460 core) |
| `OpenGLPhysicsDebugDraw3D.cpp` | 202 | 3D OpenGL 调试绘制 |
| **`JoltDebugRenderer.h/.cpp`** | 60+130 | **v4.0 新增: JPH::DebugRenderer → IPhysicsDebugDraw3D 适配** |

---

## 三、核心管线流程

### 3.1 3D 物理同步管线 (v4.0 新版)

```
PhysicsSyncSystem::Update(realDt)
  │
  ├── 1. SyncECSToPhysics()
  │     ├── 1a. Transform/RigidBody 配置同步 (isDirty 检查)
  │     └── 1b. Collider 拓扑同步 (v4.0) ← 新增
  │           ├── BoxCollider3DComponent::isDirty → BodyLockWrite + SetShape(BoxShapeSettings)
  │           ├── SphereCollider3DComponent::isDirty → BodyLockWrite + SetShape(SphereShapeSettings)
  │           └── CapsuleCollider3DComponent::isDirty → BodyLockWrite + SetShape(CapsuleShapeSettings)
  │
  ├── 2. FixedTimestepAccumulator.Advance(realDt)
  │     └── 循环 steps 次:
  │           ├── 2a. BackupState()       // prevPosition + prevRotation (四元数)
  │           ├── 2b. StepPhysics(fixedDt) // JoltPhysicsWorld::Step()
  │           └── 2c. ProcessCollisionEvents() ← v4.0 新增
  │                 └── 消费 LockFreeEventQueue → GetBodyByID 安全反查 → 路由回调
  │
  └── 3. SyncPhysicsToECS()
        └── 仅活跃物体 → SetPosition + SetRotationQuat(body->GetRotationQuat()) ← 四元数
```

### 3.2 碰撞事件管道 (v4.0 彻底重构)

```
[Jolt Worker Thread]                         [Main Thread]
       │                                          │
  OnContactAdded(bodyA, bodyB)                     │
  ├── 提取 BodyID (安全)                            │
  ├── Push({Begin, idA, idB})                      │
  │        │                                        │
  OnContactPersisted(bodyA, bodyB)                 │
  ├── 提取 BodyID                                   │
  ├── Push({Persist, idA, idB, impulse})            │
  │        │                                        │
  OnContactRemoved(SubShapeIDPair)                  │
  ├── inPair.GetBody1ID() (安全: 不 touch Body)      │
  ├── Push({End, idA, idB})                         │
  │        │                                        │
  LockFreeEventQueue (MPSC CAS)                     │
  │        │                                        │
  │        └──────────────────────────────► ProcessCollisionEvents()
                                             ├── Pop event
                                             ├── GetBodyByID(idA) → 反查 IPhysicsBody3D
                                             ├── GetBodyByID(idB)
                                             ├── if (bodyA && bodyB) → 路由 m_ContactBegin/End/Persist
                                             └── else → 丢弃 (安全降级)
```

### 3.3 2D 物理步进 (Bare2DPhysicsWorld)

```
Step(dt) → FixedStep() → IntegrateForces → IntegrateVelocity → DetectCollisions
  → SolveConstraints → Baumgarte → UpdateAABB (ParallelFor) → Warm Starting → DispatchCallbacks
```

---

## 四、各版本完成的改进

### v4.0 完成的改进

### 4.1 P0 问题修复

| 问题 | 修复方式 |
|------|----------|
| **Jolt OnContactRemoved Body 丢失** | `ContactListenerImpl` 持有 `LockFreeEventQueue*`，`OnContactRemoved` 从 `SubShapeIDPair` 安全提取 BodyID，绝不 touch Body |
| **TransformComponent 万向锁** | 内部存储 `Quat m_RotationQuat`，`GetRotationEuler` 通过四元数惰性计算 |
| **QuerySphere 精度不足** | 使用 `NarrowPhaseQuery::CollideShape()` 窄阶段精确检测，废弃 AABB 近似 |
| **DebugDraw 空实现** | `JoltDebugRenderer` 继承 `JPH::DebugRenderer`，转发到 `IPhysicsDebugDraw3D` |

### 4.2 新增功能

| 功能 | 实现 |
|------|------|
| **BatchSetKinematicTargets** | `IPhysicsWorld3D` 纯虚 + `JoltPhysicsWorld` 完整实现 |
| **BatchGetTransforms** | `IPhysicsWorld3D` 纯虚 + `JoltPhysicsWorld` 完整实现 |
| **Collider 运行时拓扑变更** | `Collider3DComponent::isDirty` + `BodyLockWrite::SetShape()` |
| **PhysX 5 预留** | `BackendType` 枚举 + `InitGlobalContext`/`ShutdownGlobalContext` |
| **四元数物理同步** | `JoltPhysicsBody::GetRotationQuat()` + `TransformComponent::SetRotationQuat()` |
| **Jolt DebugRenderer** | `JoltDebugRenderer` 完整实现，含三角形批处理和 `std::mutex` 线程保护 |

### v5.0 完成的改进

### 5.1 关键架构修正

| 修正项 | 修正方式 |
|--------|----------|
| **JoltDebugRenderer TLS 无锁化** | `std::mutex` → `std::array<PerThreadBuffer, 16>`，`GetThreadIndex()` 通过 `thread_id` 哈希映射，写入完全无锁 |
| **AddFixture 语义废弃** | 从 `IPhysicsBody3D` 移除 `AddFixture`/`RemoveFixture`/`ClearFixtures`，改为 `SetShape(const ShapeDef3D&)` 整体替换语义 |
| **JoltPhysicsBody SetShape 完整实现** | 使用 `BodyLockWrite` + `JPH::Body::SetShape()`，Box/Sphere/Capsule 三种形状完整实现 |

### 5.2 新增功能

| 功能 | 实现 |
|------|------|
| **3D 关节接口** | `JoltJoint3D.h` 新增：封装 `JPH::TwoBodyConstraint`，支持 Hinge/Ball/Slider/Fixed/Distance/SixDOF |
| **IPhysicsBody3D::SetShape** | Jolt 运行时形状替换，自动重算质心和惯性张量 |
| **PhysX 5 预留 BackendType** | `PhysicsSystemManager::BackendType::PhysX5/Bullet3` 枚举 + `InitGlobalContext`/`ShutdownGlobalContext` |

### 5.3 v5.0 已修复的 v4.0 遗留问题

| v4.0 遗留问题 | v5.0 状态 |
|---------------|-----------|
| JoltPhysicsWorld::CreateJoint 返回 nullptr | **已修复**: JoltJoint3D 接口已定义，待绑定约束参数 |
| JoltPhysicsBody::AddFixture 返回 nullptr | **已修复**: 废弃 AddFixture，改为 SetShape |
| JoltDebugRenderer std::mutex 瓶颈 | **已修复**: TLS 16 buffer 无锁设计 |

### 5.4 剩余待办

| 问题 | 优先级 | 说明 |
|------|--------|------|
| `JoltPhysicsBody::SetCollisionFilter()` 空实现 | P1 | 运行时碰撞滤波修改未实现，需调用 BodyInterface::SetObjectLayer |
| `PhysicsComponent::Serialize()` 未完成 | P1 | BodyDef 完整序列化为 TODO |
| `GetStats()` 部分实现 | P2 | Jolt v5.5 移除了 GetBodyManager/GetNumContacts |
| `CreateWorld2D()` 返回 nullptr | P2 | 2D 世界创建依赖外部初始化 |
| CharacterController | P2 | JPH::CharacterVirtual 封装未实现 |
| PhysicsMaterial 资产管线 | P3 | .physmat JSON 定义 + JPH::PhysicsMaterial 缓存 |

---

## 六、代码统计

| 维度 | 统计 |
|------|------|
| 接口文件数 | 8 (6 个核心接口 + 2 个调试绘制接口) |
| 实现文件数 | ~22 (新增 JoltJoint3D.h + JoltDebugRenderer.cpp 重构) |
| **v4.0 新增代码** | ~500 行 |
| **v5.0 新增/重写代码** | ~280 行 (JoltJoint3D 60 + JoltDebugRenderer TLS 重构 120 + IPhysicsBody3D SetShape 50 + PhysicsBody SetShape 50) |
| Bare2D 物理管线 | 1302 行 |
| Jolt 集成 (v4.0 + v5.0) | ~1200 行 |
| Box2D 集成 | 560 行 |
| ECS 同步系统 | ~265 行 |

---

## 七、下一步迭代建议

### 7.1 短期 (Gameplay 赋能)

1. **Jolt CharacterController 封装** → `ICharacterController3D` 接口 + `JPH::CharacterVirtual` 适配
2. **PhysicsMaterial 资产管线** → `.physmat` JSON → AssetRegistry 加载 → JPH::PhysicsMaterial 缓存
3. **3D 关节 ECS 绑定** → `Joint3DComponent` + `PhysicsSyncSystem` 监听引擎实体创建/删除

### 7.2 中期 (性能与正确性)

4. **Jolt GetStats 完善** → 使用 `BodyLockRead` 手动统计 contacts
5. **ECS Bulk 批量操作** → `PhysicsSyncSystem::BatchSyncPhysicsToECS` 使用 `BatchGetTransforms`
6. **PhysicsComponent3D 碰撞回调** → `CollisionListenerComponent` + `OnCollisionEnter/Stay/Exit`

### 7.3 长期 (架构演进)

7. **统一 2D/3D Layer 系统** → 融合 `CollisionLayers` 和 `ObjectLayer`
8. **PhysX 5 后端实现** → 利用 `BackendType::PhysX5` + `InitGlobalContext` 预留接口
9. **Lua/C# 脚本集成** → 碰撞事件路由到脚本层的 `OnCollisionEnter()` 回调
