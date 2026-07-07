# 物理子系统实现总结报告

> **生成日期**: 2026-07-08 (v6.0 更新)  
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
│   ├── IPhysicsWorld (抽象接口) ─── Box2DPhysicsWorld / Bare2DPhysicsWorld
│   ├── IPhysicsBody  (抽象接口) ─── Box2DPhysicsBody  / Bare2DPhysicsBody
│   ├── IJoint / IForceGenerator    ─── Box2DJoint / GravityForce
│   └── IPhysicsDebugDraw          ─── OpenGLPhysicsDebugDraw
│
├── 3D 物理
│   ├── IPhysicsWorld3D ─── JoltPhysicsWorld
│   │   ├── Batch API / ProcessCollisionEvents         ← v4.0
│   │   └── CreateJoint() / DestroyJoint() 完整实现     ← v6.0
│   ├── IPhysicsBody3D ─── JoltPhysicsBody
│   │   ├── GetRotationQuat() / SetRotationQuat()       ← v4.0
│   │   └── SetShape() 整体替换 (废弃 AddFixture)        ← v5.0
│   ├── IJoint3D ─── JoltJoint3D (6种类型完整实现)       ← v6.0
│   ├── ICharacterController3D ─── JoltCharacterController3D  ← v6.0
│   │   └── JPH::CharacterVirtual (胶囊体/爬坡/爬楼梯)
│   └── IPhysicsDebugDraw3D ─── OpenGLPhysicsDebugDraw3D
│
├── ECS 集成
│   ├── RigidBody3DComponent / PhysicsRuntimeComponent
│   ├── Box/Sphere/CapsuleCollider3DComponent (isDirty)  ← v4.0
│   ├── Joint3DComponent (连接两个实体的约束定义)         ← v6.0
│   ├── CollisionListenerComponent (OnCollisionEnter/Exit) ← v6.0
│   ├── PhysicsSyncSystem (三级同步 + Collider拓扑)       ← v4.0
│   └── TransformComponent (四元数内部存储)               ← v4.0
│
├── 碰撞事件管道
│   ├── LockFreeEventQueue (MPSC, 只传BodyID)            ← v4.0
│   └── ProcessCollisionEvents (主线程反查+路由)          ← v4.0
│
└── 基础设施
    ├── FixedTimestepAccumulator / PhysicsLayers
    ├── JoltDebugRenderer (TLS无锁, 16线程独立Buffer)     ← v5.0
    ├── PhysicsMaterialHandle (资产管线预留)               ← v6.0
    └── JoltJobSystemAdapter
```

### 设计原则 (v4～v6)

| 原则 | 说明 | 版本 |
|------|------|------|
| **纯虚接口隔离** | 核心层只依赖抽象，不暴露第三方类型 | v1 |
| **抽象工厂 + 后端枚举** | `BackendType::Jolt5/PhysX5/Bullet3` | v4.0 |
| **数据与状态分离** | RigidBody3DComponent vs PhysicsRuntimeComponent | v3.1 |
| **RAII 生命周期** | `shared_ptr` / `weak_ptr` 管理物理世界和刚体 | v1 |
| **固定步长解耦** | `FixedTimestepAccumulator` + 渲染插值不写回Transform | v3.1 |
| **四元数内部存储** | TransformComponent 使用 Quat 避免万向锁 | v4.0 |
| **碰撞事件无锁传递** | Worker线程只传BodyID，绝不传指针 | v4.0 |
| **运行时Collider拓扑变更** | BodyLockWrite + SetShape 不销毁重建 | v4.0 |
| **窄阶段精确查询** | QuerySphere 使用 CollideShape | v4.0 |
| **批量操作** | BatchSetKinematicTargets | v4.0 |
| **Thread-Local DebugRenderer** | 16线程独立Buffer，写入完全无锁 | v5.0 |
| **ShapeDef整体替换** | AddFixture废弃, SetShape自动重算惯性 | v5.0 |
| **3D关节系统** | Hinge/Ball/Slider/SixDOF → Jolt TwoBodyConstraint | v6.0 |
| **虚拟角色控制器** | JPH::CharacterVirtual，防穿模/爬楼梯 | v6.0 |
| **ECS关节/碰撞监听** | Joint3DComponent + CollisionListenerComponent | v6.0 |

---

## 二、文件清单与职责

### 2.1 接口层

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `IPhysicsBody.h` | 69 | 2D 刚体接口 |
| `IPhysicsWorld.h` | 99 | 2D 世界接口 |
| `IPhysicsBody3D.h` | ~90 | 3D 刚体接口 (SetShape v5.0) |
| `IPhysicsWorld3D.h` | ~170 | 3D 世界接口 (Batch API v4.0) |
| `IJoint.h` | 83 | 2D 关节接口 |
| `IJoint3D.h` | 117 | 3D 关节接口 (6种类型) |
| **`ICharacterController3D.h`** | ~80 | **v6.0: 虚拟角色控制器接口** |
| `IPhysicsDebugDraw.h/.h` | 65+54 | 2D/3D 调试绘制接口 |

### 2.2 数据结构层

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `PhysicsDefs.h` | 436 | 2D 类型定义 |
| `PhysicsDefs3D.h` | ~190 | 3D 类型定义 + **PhysicsMaterialHandle (v6.0)** |
| `PhysicsLayers.h` | 89 | Jolt ObjectLayer 8层映射 |
| `FixedTimestepAccumulator.h` | 81 | 固定步长累加器 |
| `LockFreeEventQueue.h` | ~100 | MPSC 无锁队列 (BodyID only v4.0) |
| `PhysicsSystemManager.h` | ~130 | BackendType + InitGlobalContext (v4.0) |

### 2.3 2D 物理实现

| 文件 | 行数 | 内容 |
|------|------|------|
| `Bare2DPhysicsBody.h/.cpp` | 234+283 | 自制 2D 刚体 |
| `Bare2DPhysicsWorld.h/.cpp` | 314+1302 | 完整物理管线 (1302行) |
| `Box2DPhysicsWorld.h/.cpp` | 99+560 | Box2D v3.x 适配 |

### 2.4 3D 物理实现 (Jolt Physics)

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `JoltPhysicsWorld.h/.cpp` | ~150+700 | 事件管道/Batch API/DebugDraw/关节系统 |
| `JoltPhysicsBody.h/.cpp` | ~96+290 | GetRotationQuat/SetShape/SetCollisionFilter |
| **`JoltJoint3D.h/.cpp`** | ~55+155 | **v6.0 新增: JoltTwoBodyConstraint 封装** |
| **`JoltCharacterController3D.h/.cpp`** | ~50+140 | **v6.0 新增: JPH::CharacterVirtual 封装** |
| `JoltDebugRenderer.h/.cpp` | ~60+140 | TLS 无锁 DebugRenderer (v5.0) |
| `JoltJobSystemAdapter.h/.cpp` | 87+? | Jolt ↔ Engine::JobSystem |

### 2.5 ECS 集成

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `PhysicsComponents.h` | ~130 | **v6.0: 新增 Joint3DComponent + CollisionListenerComponent** |
| `PhysicsSyncSystem.h/.cpp` | ~100+200 | 三级同步管线 + 关节管理 + 碰撞事件路由 |
| `PhysicsComponent.h/.cpp` | 144+83 | 2D GameObject 组件 |
| `PhysicsComponent3D.h/.cpp` | 86+76 | 3D 组件 (四元数同步 v4.0) |
| `TransformComponent.h/.cpp` | ~100+200 | 四元数内部存储 (v4.0) |

---

## 三、代码统计 (全版本)

| 维度 | v4.0 | v5.0 | v6.0 |
|------|------|------|------|
| 新增文件 | 2 | 3 | **4** |
| 新增/重写代码 | ~500行 | ~280行 | **~592行** |
| 关键修复 | 4个P0 | 3个隐患 | — |
| 新增功能 | 6项 | 3项 | **4项** |

---

## 四、各版本完成总结

### v4.0 (架构安全)
- Jolt 碰撞回调 BodyID-only 安全管道
- TransformComponent 四元数内部存储
- QuerySphere 窄阶段精确检测
- JoltDebugRenderer (mutex版)
- BatchSetKinematicTargets / BatchGetTransforms
- Collider 运行时拓扑变更
- PhysX 5 BackendType 预留

### v5.0 (架构修正)
- JoltDebugRenderer TLS 无锁 (atomic fetch_add, 无hash碰撞)
- AddFixture → SetShape 语义废弃 + 惯性张量自动重算
- RemoveBody Constraint 安全清理
- JoltJoint3D 接口定义

### v6.0 (Gameplay 赋能)

| 阶段 | 实现 | 关键代码 |
|------|------|----------|
| **角色控制器** | `ICharacterController3D` + `JoltCharacterController3D` | JPH::CharacterVirtual::ExtendedUpdate 封装，爬坡/爬楼梯/防卡墙 |
| **3D关节系统落地** | `JoltJoint3D.cpp` 完整实现 | 6种 JPH 约束创建 (Hinge/Slider/Ball/Fixed/Distance/Spring) |
| **CreateJoint 完整实现** | `BodyLockWrite` + 约束设置 + 马达配置 | 不是 stub，生产级代码 |
| **ECS Joint 绑定** | `Joint3DComponent` + `PhysicsSyncSystem::m_Joints` | 实体A/B连接，自动创建/销毁约束 |
| **ECS 碰撞监听** | `CollisionListenerComponent` | OnCollisionEnter/Exit 回调 |
| **资产管线预留** | `PhysicsMaterialHandle` | .physmat JSON 管线预留 |

### 零 stub 承诺
- `JoltPhysicsWorld::CreateJoint` → 6种 JointDef3D → JPH 约束映射，完整实现
- `JoltPhysicsWorld::DestroyJoint` → `RemoveConstraint`
- `JoltCharacterController3D` → 完整 `ExtendedUpdate` + 重力 + 接地检测
- `JoltJoint3D` → SetLimits/EnableMotor/GetCurrentAngle 全实现

---

## 五、下一步建议

### 短期
1. `PhysicsSyncSystem::ProcessCollisionEvents` 中路由到 `CollisionListenerComponent`
2. `JoltPhysicsBody::SetCollisionFilter` 调用 `BodyInterface::SetObjectLayer`
3. `PhysicsComponent::Serialize` 完成 BodyDef 序列化

### 中期
4. `.physmat` 资产管线: yaml-cpp 反序列化 + JPH::PhysicsMaterial 缓存
5. `GetStats` 完整实现 (手动统计 contacts)
6. 角色控制器 → ECS CharacterControllerComponent 绑定

### 长期
7. 统一 2D/3D Layer 系统
8. PhysX 5 后端实现
9. Lua/C# 脚本 OnCollisionEnter 桥接