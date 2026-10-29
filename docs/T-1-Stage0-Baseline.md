# T-1 Stage 0：RigidBody 只读基线核验

**核验日期**：2026-10-29
**状态**：`STAGE 0 — READ-ONLY BASELINE VERIFIED`
**T-1-A**：`NOT STARTED`（等待单独授权）
**约束**：本文件不授权 commit / push / tag / merge；不授权 T-1-A 实现。
既有子模块脏标记与 `crashes/` 保持原状。

本文件只记录工作树与源码的**当前事实**。除本记录外，Stage 0 未修改源码、
未运行构建或测试、未执行任何 Git 写操作。T-2 的测试数字只引用关闭记录，
不在本轮重跑或改写。

---

## 1. 基线状态与保护边界

### 1.1 Git 基线

| 项目 | 观测值 | 证据 |
|---|---|---|
| 分支 | `avalonia` | `git branch --show-current` |
| HEAD | `c87c8ee3dbb6894cc9f70bb98a079ea51127a42f`（2026-10-28 03:54:38 +0800） | `git log -1 --format='%H %ad %s'` |
| 暂存区 | 空 | `git diff --cached --stat` 无输出 |
| 已跟踪修改 | 28 个条目（25 个文件 + 3 个子模块），+1249 / -174 | `git diff --stat` |
| 未跟踪文件 | 141 个文件；`git status --short` 折叠为 27 个条目 | `git ls-files --others --exclude-standard` |
| 未跟踪非 crash 文件 | 26 个（T-2 的 Content 代码/测试/文档） | 见 §1.3 |
| `crashes/` | 115 个既有崩溃产物，未跟踪，未触碰 | `ls crashes \| wc -l` |
| 脏子模块 | `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` | `git status --short` 的 3 个 ` m` 条目 |

### 1.2 T-2 关闭状态（引用，不重跑）

| 项目 | 记录值 | 来源 |
|---|---|---|
| T-2 总状态 | `CLOSED — SCOPE-BOUND` | [T-2-Closure-Record.md](T-2-Closure-Record.md) §1 |
| `test_content` | 92 通过 / 5 既有失败 / 1 skip | 同上 §2 |
| `test_bridge` | 100/100 | 同上 §2 |
| 完整 CTest | 16/17 | 同上 §2 |
| 第一方全量构建 | Debug/Release 各 5 条既有警告 | 同上 §2 |
| 托管增量构建 | 4×NU1900（既有 NuGet 告警） | 同上 §2 |
| RF-4 | `UNVERIFIED — ACCEPTED SCOPE EXCEPTION` | 同上 §4 |
| 物理断电持久性 | 未验证、不作保证 | 同上 §5 |

### 1.3 T-2 工作树交付物（保护对象）

26 个非 crash 未跟踪文件全部为 T-2 交付物：

- Content 头文件 4：`AtomicFile.h`、`ProjectPaths.h`、`RevisionStore.h`、`Utf8Path.h`
- Content 实现 4：`AtomicFile.cpp`、`ProjectPaths.cpp`、`RevisionStore.cpp`、`Utf8Path.cpp`
- 文档 9：M1-A～M1-F 六份阶段记录、`Roadmap-Decision-Record-2026-10-28.md`、`T-2-Closure-Record.md`、`Content-v2-M1-B-Ordering-Conflict.md`
- 测试 9：`M1FCrashHarness.cpp`、`test_import_service.cpp`、`test_m1f_fault_matrix.cpp`、`test_project_root.cpp`、`test_rename_script.cpp`、`test_revision_commit.cpp`、`ManifestStrictLoadTest.cpp`、`ProjectPathTest.cpp`、`RevisionStoreTest.cpp`

关键 T-2 文件的基线哈希（SHA-256），用于后续确认未被 T-1 改动：

| 文件 | SHA-256 |
|---|---|
| `engine/src/Core/Content/RevisionStore.cpp` | `d03bbe873959599fea0cc719a73e9210996f8cf4ad5bf222c7a437289368b1f2` |
| `engine/include/Engine/Core/Content/RevisionStore.h` | `aa5c2d838c1f998ad2887d4540d205944c5fa84c4368e021eb2e26cdbd4f1ec0` |
| `engine/src/Core/Content/AtomicFile.cpp` | `b7d109df1f05ee82673c4eff4d403aad26c7657d56949dbb42217adb86461eb9` |
| `engine/src/Core/Content/ProjectPaths.cpp` | `0abfd67987ec4d2c0e29a3a715a3c2ffdb05df3e80289869827ae9050c857036` |
| `engine/src/Core/Content/Utf8Path.cpp` | `dd67d4040b419d95369262cfd198303a58785ff9630f73a4730e7098c3521121` |
| `bridge/include/editor_bridge/capi.h` | `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886` |
| `docs/T-2-Closure-Record.md` | `355c513f0a22c2ca95d41b7ccfd6b5bd7ffbba85c453c3b489661d56a862222c` |
| `tests/test_bridge/test_revision_commit.cpp` | `8c8fd870fe633c1d2021577f48c840770272d224ff64dd269eb8099cfae71b3a` |

> 基线纪律：T-1-A 开工前再次核对上表；除非用户另行授权，任何 T-1 变更不得改写
> T-2 的 Content 代码、关闭记录与证据数字。

---

## 2. RigidBody 相关入口与调用链

### 2.1 契约组件链（当前唯一被生产编辑器消费的组件模型）

```text
GameObject::AddComponentByName(type)
  └─ ComponentRegistryGo::Create(type)
       └─ EnsureRegistered: Camera / Collider / Skinning
  └─ GameObject::Attach(comp)  → owner + OnCreate + lifecycle notify
  └─ 反射：GetPropertyCount / GetPropertyDesc / GetPropertyValue / SetPropertyValue
  └─ 序列化：SceneSerializerV1::CaptureScene → entity.components[] {type,data}
       └─ InstantiateScene → RegistryGo::Create(type) → Deserialize → Attach
  └─ Lua：Engine.component.add/has/get/remove + get/set_number|bool|string
  └─ Bridge：Add/Remove/Has/GetComponentCount/GetComponentTypeAt/Get|SetComponentProperty
  └─ 托管 Inspector：目前只有 Camera 专用 UI
```

关键事实：

- 稳定契约身份是 `Component::GetComponentTypeName()`；默认返回 `nullptr`，
  表示“未收编、不进入 `components[]`”
  （`engine/include/Engine/Core/GameObject/Component.h:114-116`）。
- 反射默认 0 属性；只有契约组件覆盖后才对 Inspector/Lua/桥接可见
  （`Component.h:118-134`）。
- 注册表只有三个内置工厂：Camera、Collider、Skinning
  （`engine/src/Core/GameObject/ComponentRegistry_Go.cpp:27-34`）。
- `CaptureScene` 只收集 `GetComponentTypeName() != nullptr` 的组件
  （`engine/src/Core/Content/SceneSerializerV1.cpp:134-143`）。
- `InstantiateScene` 对未知类型只告警并跳过，实体照常加载
  （`SceneSerializerV1.cpp:210-225`）。
- Lua 通用组件 API 见 `engine/src/Scripting/GameplayAPI.cpp:114-330`；
  没有物理专用域（`Engine.physics.*` 或 `Engine.rigidbody.*`）。
- Bridge 组件 ABI 见 `bridge/include/editor_bridge/capi.h:248-282`；
  ABI 版本为 v1.8（`capi.h:438`）。
- 托管 Inspector 只有 Camera 字段与 Add/Remove 按钮
  （`editor_avalonia/ViewModels/MainViewModel.cs:255-319`、
  `editor_avalonia/MainWindow.axaml:151-163`）；
  `GetComponentCount/GetComponentTypeAt` 在 `editor_avalonia` 中无调用点。

**结论**：契约、反射、`components[]`、Lua、Bridge 五条通用通道已经真实存在且被
Camera/Collider/Skinning 验证；RigidBody 若按同一模板收编，不需要新造组件模型。
缺的是契约本身、序列化条目、物理绑定与 Inspector 入口。

### 2.2 Collider → Box2D 物理绑定链（F2-B，已冻结）

```text
ColliderComponent（声明状态）
  └─ PhysicsColliderAdapter::Bind
       └─ BuildBodyDef：
            type = BodyType::Static        ← 硬编码
            shape = Circle / Box
            isSensor / category / mask
            位置 = (0,0)，角度 = 0          ← 未接 Transform
       └─ IPhysicsWorld::CreateBody → Box2D body
  └─ mutation hook：
       enabled → SetActive
       shape/radius/halfX/halfY/sensor/category/mask → Rebuild
  └─ ObserveGameObject → GameObject 组件生命周期监听（Add→Bind / Remove→Unbind）
  └─ Step 由世界持有者调用（测试/沙盒），Adapter 自身不推进时间
```

证据：

- `engine/src/Core/Physics/PhysicsColliderAdapter.cpp:218-242`；
  `def.type = BodyType::Static` 在 :223；位置未映射的说明在 :239-240。
- `engine/include/Engine/Core/Physics/PhysicsColliderAdapter.h:60-152`。
- `ColliderComponent` 本身就是“静态声明”：Circle/Box、radius、halfX/halfY、
  sensor、category、mask、enabled
  （`engine/include/Engine/Core/GameObject/ColliderComponent.h:43-107`）。
- Collider 反射共 8 个属性，序列化写入 `enabled/shape/radius/halfX/halfY/
  isSensor/category/mask`
  （`engine/src/Core/GameObject/ColliderComponent.cpp:30-113,123-157`）。
- 仓库内 `PhysicsColliderAdapter` 的 `SetWorld/ObserveGameObject` 调用点**只在
  `tests/test_physics/ColliderBindingTest.cpp`**；`bridge/`、`editor_avalonia/`
  零引用。`docs/Architecture-Runtime-Integration-Inventory.md:563` 也记录
  `AttachWorld` 为 `UNREACHABLE`、零外部引用。

**结论**：Collider 的物理映射是真实的、被测试充分覆盖的，但它是**静态碰撞区域**
语义；它不是 RigidBody，也没有生产编辑器接线。

### 2.3 遗留 2D `PhysicsComponent` 链（sandbox / 旧 ImGui 编辑器）

```text
GameObject::GetPhysics()
  └─ PhysicsComponent::CreateBody(shared_ptr<IPhysicsWorld>, BodyDef)
       └─ IPhysicsBody（Box2D），BodyDef 可 Dynamic/Kinematic/Static
  └─ Scene::PostPhysicsUpdate → SyncPhysicsToTransform
```

证据：

- `engine/include/Engine/Core/Physics/PhysicsComponent.h:41-142`；
  `CreateBody` 在 :61；`Serialize/Deserialize` 在 :131-132。
- `engine/src/Core/GameObject/GameObject.cpp:220-226`。
- `engine/src/Core/Scene/Scene.cpp:212-230`。
- 序列化走旧 `JsonSerializer`，注册名为 `"PhysicsComponent"`
  （`engine/src/Core/Physics/PhysicsComponent.cpp:8,65-80`），**不是**
  `components[]` 契约路径；它没有 `GetComponentTypeName()` 覆盖。
- 使用点集中在 `sandbox/src/{PhysicsTest,MarioDemo,SystemTest,ImGuiTest}`；
  生产 `editor_avalonia`/`bridge` 零引用。`engine/src/Editor/InspectorPanel.cpp`
  仍保留旧 ImGui 编辑器的引用，但该编辑器不是生产目标。

**结论**：可作为 2D 动态刚体能力的参考实现，但不能直接作为 T-1 契约模型：
它的身份、序列化和生命周期语义都与 F1 契约不一致。

### 2.4 遗留 3D `PhysicsComponent3D` 链

```text
PhysicsComponent3D::CreateBody(shared_ptr<IPhysicsWorld3D>, BodyDef3D)
  └─ IPhysicsBody3D（Jolt）
  └─ SyncPhysicsToTransform / SyncTransformToPhysics
```

证据：

- `engine/include/Engine/Core/GameObject/PhysicsComponent3D.h:38-84`。
- 没有稳定类型名、没有反射、没有 `Serialize/Deserialize`、没有 Registry 注册；
  仓库中未发现生产调用点。

**结论**：不是契约组件，也没有进入编辑器；T-1 不应把它当作既有生命周期模型。

### 2.5 ECS 3D 刚体链（独立模型，未接生产）

```text
EntityManager
  ├─ RigidBody3DComponent（motionType/layer/mass/friction/restitution/damping/sleep/CCD/gravity/trigger）
  ├─ PhysicsRuntimeComponent（runtimeBodyID/prevPosition/prevRotation/isActive/isDirty）
  └─ Box/Sphere/CapsuleCollider3DComponent（halfExtents/radius/height/offset/density/friction/restitution/sensor）
        └─ PhysicsSyncSystem::Update
             ├─ SyncECSToPhysics：形状 isDirty → Jolt SetShape
             ├─ StepPhysics → Jolt Step
             └─ SyncPhysicsToECS
```

证据：

- `engine/include/Engine/Core/ECS/PhysicsComponents.h:31-66,72-107`。
- `engine/src/Core/ECS/PhysicsSyncSystem.cpp:29-110`（Init/Update）、
  `:112-199`（shape 拓扑同步）。
- 全仓库 `runtimeBodyID =` 的赋值点除 `PhysicsSyncSystem` 的读取/守卫外，
  只在 `sandbox/src/ECSTest/ECSTest.cpp:283` 出现（测试值 `12345`）；
  未发现 ECS 侧的生产 body 创建系统。
- `PhysicsSyncSystem` 自身只被其 .cpp 定义引用；`PhysicsSystemManager`
  也没有外部使用者。`PhysicsSystemManager::CreateWorld2D` 直接返回 `nullptr`
  并提示走既有 Box2D 路径（`engine/src/Core/Physics/PhysicsSystemManager.cpp:16-20`），
  `CreateWorld3D` 才会创建 Jolt 世界（:34-45）。
- `docs/Component-Maturity-Matrix.md:21` 明确记录 ECS 3D 物理
  “未接入 Editor / 序列化 / Avalonia”。

**结论**：ECS 是独立的数据导向模型，当前既不是生产路径，也不是可复用的
GameObject 生命周期实现。T-1-A 只应写“边界规范”，不实现 ECS 桥接
（与 `docs/Roadmap-Decision-Record-2026-10-28.md:119-129` 一致）。

### 2.6 生产编辑器运行时链（`editor_avalonia` → bridge）

```text
EditorSession::Play
  ├─ CaptureScene(edit scene)
  ├─ InstantiateScene → runtime Scene（components[] 还原）
  ├─ GameplayAPI::SetScene(runtime) + HandleAdopt
  ├─ Lua director Initialize / OnCreate
  └─ m_Application->SetPlaying(true)

EditorSession::RuntimeTick
  └─ ScriptInstance::OnUpdate(dt)      ← 仅 Lua；无物理步进

EngineHost
  └─ Application::PumpOneFrame → InternalUpdate
       ├─ OnUpdate(dt)
       ├─ DispatchSubsystemUpdates(dt)
       └─ MenuManager（仅播放态）
```

证据：

- `EditorSession::Play`：`bridge/src/EditorSession.cpp:1294-1398`。
- `EditorSession::RuntimeTick`：`EditorSession.cpp:1441-1447`。
- `EditorSession::ViewportTick` 只驱动相机：`EditorSession.cpp:1610-1627`。
- `Application::InternalUpdate`：`engine/src/Core/Application.cpp:460-514`。
- `Scene::PostPhysicsUpdate` 的调用点只在 sandbox（如
  `sandbox/src/PhysicsTest/PhysicsTestApp.cpp:411`）与 engine 内部
  `Level/LevelManager`；`bridge/`、`editor_avalonia/` 无调用。
- `EditorSession::SaveProject` 走
  `CaptureScene → SerializeSnapshot → RevisionStore::Publish`
  （`EditorSession.cpp:309-363`）；打开工程时从 revision 读取
  （`EditorSession.cpp:210-286`）。

**结论（Gate 0 最重要的现状）**：

1. 生产编辑器已经有**组件数据闭环**：编辑 → `components[]` → revision → 冷启打开。
2. 生产编辑器**没有任何物理世界、物理步进或 Transform↔Body 同步**：
   Play 只执行 Lua；Collider 即使被 ABI 挂上，也不会产生物理行为。
3. 因此 T-1-C 不是“把现有适配器接上”那么简单，而是要先确定
   **生产运行时的物理宿主与步进所有权**；这是 Stage 0 要求 Gate 0 明确指出的缺口。

---

## 3. GameObject 模型 vs ECS 模型差异

| 维度 | GameObject / Component 契约（生产编辑器） | ECS（3D，独立模型） |
|---|---|---|
| 对象身份 | `shared_ptr<GameObject>` + Scene 索引 | 64-bit generational `EntityHandle` |
| 组件存储 | 每对象 `typeid` map | Archetype/Chunk 列式存储 |
| 类型身份 | 稳定字符串 `GetComponentTypeName()` | `ComponentType<T>::ID()` + 元数据注册 |
| 创建方式 | `ComponentRegistryGo` 字符串工厂 + `Attach` | `EntityManager::AddComponent<T>` |
| 序列化 | `SceneSerializerV1` 的 `components[]` JSON | 当前无场景序列化接入 |
| 脚本 | `Engine.component.*` 反射 API | 未接入 GameplayAPI |
| 编辑器 | `editor_avalonia` 编辑模型 | 未接入 Editor / Avalonia |
| 2D 物理 | Collider → `PhysicsColliderAdapter` → Box2D（Static） | 不适用 |
| 3D 物理 | 无生产绑定 | `RigidBody3DComponent` + `PhysicsSyncSystem` → Jolt |
| 刚体类型 | Collider 不含 body type；Adapter 固定 Static | `MotionType::Static/Kinematic/Dynamic` |
| 形状 | Circle / Box（2D） | Box / Sphere / Capsule（3D） |
| 质量/阻尼/重力/CCD | 契约无字段；BodyDef 有部分能力但未暴露 | `RigidBody3DComponent` 显式字段 |
| 运行时状态 | Adapter 的 `m_Bodies`/`m_Tracked` map | `PhysicsRuntimeComponent.runtimeBodyID` 等 |
| 变更传播 | setter → mutation hook（仅 Collider） | `isDirty` 标记 + Sync 系统 |
| 生命周期 | GameObject 组件监听器（Add/Remove） | ComponentRemovedCallback + 系统查询 |
| Transform 同步 | `Scene::PostPhysicsUpdate`（仅旧 2D 路径） | `PhysicsSyncSystem::SyncPhysicsToECS` |
| 生产状态 | 编辑器组件闭环真实存在 | 数据与形状同步存在，但无 body 创建/编辑器接线 |

差异结论：

- 两者不是同一模型的两种视图：身份、存储、序列化、生命周期都不同。
- T-1 的目标模型应是 **GameObject 契约模型**；ECS 只能作为边界规范对象。
- 在 T-1-A 规范中必须写明：RigidBody 契约不持有 ECS `EntityHandle`、
  `runtimeBodyID`、Jolt `BodyID` 或任何运行时句柄；这些属于未来桥接实现，
  不在本轮范围。

---

## 4. 缺口清单（按 T-1 阶段）

| 区域 | 当前真实状态 | 缺口 / T-1 需要决定的事 | 阶段 |
|---|---|---|---|
| 契约身份 | 无 `RigidBody` 类型；Registry 只有 Camera/Collider/Skinning | 稳定类型名、字段集、反射元数据、Registry 工厂 | T-1-A |
| 组件边界 | Collider 只含形状/滤波；body type 被 Adapter 硬编码 | RigidBody 与 Collider 的字段所有权（type/mass/damping vs shape/sensor/filter） | T-1-A |
| ECS 边界 | ECS 模型独立且未接生产 | 写成规范；不实现桥接 | T-1-A |
| 序列化 | `components[]` 通道已存在；Collider 已往返 | RigidBody 新条目；冷启后重建；不得改 Collider schema | T-1-B |
| 冷启 | 编辑器 Save→Open 能还原组件属性 | 冷启后 RigidBody 的物理表示与行为；编辑器当前无物理世界 | T-1-B / T-1-C |
| 2D 绑定 | Adapter 固定 Static；不映射 Transform；无质量/阻尼/重力/CCD | 动态 body type、质量/阻尼、Transform 初始位置、同步回写 | T-1-C |
| 运行时宿主 | 编辑器无 `IPhysicsWorld`、无 Step、无 `PostPhysicsUpdate` | 谁创建世界、谁 Step、Play/Stop 如何建立与销毁 | T-1-C |
| Adapter 演进 | F2-B 已冻结，静态 Collider 语义有测试 | 扩展还是新增绑定路径；不得静默改变既有静态语义 | T-1-C |
| Lua | 通用 `Engine.component.*` 已存在；无物理专用域 | 通用 API 是否足够；v2.1 是否只补测试/便捷函数 | T-1-D |
| Bridge | 通用组件 ABI v1.8 已存在 | 是否需要 RigidBody 专用 API；优先复用通用 ABI | T-1-E |
| 托管 Inspector | 只有 Camera 专用字段/按钮 | 通用组件枚举与 RigidBody 编辑 UI；`GetComponentCount/TypeAt` 尚未被托管使用 | T-1-E |
| 端到端 | 编辑器无物理行为；RF-4 托管导入仍未验证 | 真实 UI → 保存 → 冷启 → 物理行为的可重复门禁 | T-1-F |
| F2-S1 | Collider 静态冷启证据存在；F2-S1 未关闭 | 保持独立；不得把 T-1 证据算作 F2-S1 完成 | 独立 |

补充说明：

- **Lua**：当前通用 API 已能对任意已注册契约组件做 add/has/get/remove 与
  number/bool/string 读写；RigidBody 一旦注册并实现反射，即可被脚本访问。
  T-1-D 的主要风险不是“没有 API”，而是**是否需要一个物理专用域**；
  若无必要，不应复制一套平行的 `Engine.rigidbody.*`。
- **Inspector**：Bridge 已提供 `GetComponentCount/GetComponentTypeAt`，
  但托管 UI 完全未使用；T-1-E 应基于反射元数据做通用枚举，
  而不是再写一个 Camera 式的硬编码面板。
- **RF-4 / 导入**：与 T-1 无依赖；#141 仍为独立后续任务，不因 T-1 改变状态。

---

## 5. Collider Serialization（F2-S1）依赖判定

### 5.1 既有证据

| 证据 | 覆盖范围 | 不覆盖 |
|---|---|---|
| `tests/test_physics/ColliderBindingTest.cpp` B5-1～B5-5（:892-1287） | 真实 `SceneSerializerV1` Save → 销毁世界 → 冷启 → `InstantiateScene` → Adapter 重建 → 逐字段验证 | 生产编辑器物理宿主；动态 RigidBody |
| 同文件 B6-1～B6-8（:1297-1810） | 静态 Collider 的 A/B/C 行为对拍、sensor/filter、mutation、重复冷启、负路径 | 动态 body（探针是测试自建 Dynamic body，Collider 仍是 Static） |
| `tests/test_bridge/test_editor_capi.cpp:1618-1684` | 编辑器 ABI：Add Collider → Set 属性 → Save → 重开工程 → 属性还原 | 物理表示/行为（bridge 无物理世界） |
| `docs/Engine-Foundation-F2B-Closure.md:22-23,40-44` | B5 Cold Restart、B6 Behavior Consistency 已验收 | F2-S1 独立交付未关闭 |

### 5.2 判定

1. **T-1-A / T-1-B 不以 F2-S1 关闭为前置条件。**
   `components[]` 序列化、Collider 字段往返、引擎级冷启重建都已有真实证据；
   T-1-B 只需为 RigidBody 增加独立条目。
2. **F2-S1 仍然独立、未完成。**
   按 `docs/Roadmap-Decision-Record-2026-10-28.md:79-88`，F2-S1 是 Collider
   单组件的独立交付键，阶段归属待定；开工前还要做 gap analysis。
3. **不得把 T-1 的证据计入 F2-S1。**
   T-1-F 的端到端证据可以证明 RigidBody 闭环，但不能替代 F2-S1 的
   Collider 交付判定；两者粒度不同。
4. **T-1-B 不得修改 Collider 的字段语义或 schema。**
   RigidBody 使用新的 `type` 字符串；Collider 的 8 个属性与 `components[]`
   结构保持不变，否则会同时污染 F2-S1 的基线。
5. **F2-S1 的真实缺口**（建议在 T-1-B 之后再单独立项）：
   - 生产编辑器物理宿主下的 Collider 冷启（当前编辑器没有世界）；
   - 托管 UI 的 Collider 作者入口（当前 Inspector 只有 Camera）；
   - F2-B 的 B5/B6 是 engine test 证据，不是 `editor_avalonia` 生产路径证据。

---

## 6. Gate 0 需要裁决的事项

以下不是 Stage 0 的结论，而是进入 T-1-A 前必须由用户明确的边界。
Stage 0 只给出证据与建议。

| 编号 | 问题 | 证据 | 建议 | 影响阶段 |
|---|---|---|---|---|
| G-1 | RigidBody 契约的目标维度/后端 | Collider 是 2D Circle/Box，Adapter 是 Box2D；ECS 3D/Jolt 未接生产 | **先做 2D Box2D**，与现有 Collider/F2-B 对齐；3D 另立阶段 | T-1-A |
| G-2 | body type 与字段所有权 | Adapter 固定 Static；Collider 无 body type | RigidBody 拥有 `motionType`/质量/阻尼等；Collider 保留形状/sensor/filter；由 Adapter 组合 | T-1-A |
| G-3 | 生产编辑器物理宿主 | 编辑器无世界、无 Step、无 Transform 同步 | T-1-C 明确“谁创建/步进/销毁世界”，并把 Play/Stop 纳入生命周期 | T-1-C |
| G-4 | Adapter 演进方式 | F2-B 已冻结且静态语义有测试 | 采用加法路径（扩展或新增组合绑定），不改既有静态 Collider 行为 | T-1-C |
| G-5 | ECS 边界 | ECS 独立、无 body 创建、未接编辑器 | 只写规范，不实现桥接；不复制 ECS 运行时状态 | T-1-A |
| G-6 | F2-S1 关系 | F2-S1 未关闭、阶段归属未定 | 保持独立；T-1-B 不修改 Collider schema；T-1-F 不替代 F2-S1 | T-1-B |

Gate 0 的结论：

- 现有真实路径足以支撑 T-1-A 的契约模板（F1 五通道已存在）。
- 现有真实路径**不足以**支撑 T-1-C 的动态物理闭环（没有编辑器物理宿主，
  Adapter 固定 Static，Transform 未接入）。
- 现有真实路径**不足以**支撑 T-1-E 的 Inspector（托管 UI 仅 Camera）。
- 因此 T-1-A 必须先冻结契约与边界，不能把 Adapter/序列化/Lua/Inspector
  写成 T-1-A 的完成条件（与 roadmap §2.5 一致）。

---

## 7. 本轮未做 / 未验证

- 未运行任何构建或测试；§1.2 的数字全部引用 T-2 关闭记录。
- 未创建新组件、未改 Adapter、未改序列化、未改 Lua、未改 Inspector。
- 未验证动态 RigidBody 的冷启行为（当前不存在该契约）。
- 未验证 `editor_avalonia` 中的物理行为（当前不存在物理宿主）。
- 未验证 ECS→GameObject 桥接（本轮明确不做）。
- F2-S1 仍为独立未完成项，不能因本记录视为关闭。

---

## 8. 下一步

等待用户对 Gate 0（尤其 G-1～G-6）的裁决。在获得单独授权前：

- 不进入 T-1-A；
- 不修改任何源码、测试、构建配置或 Git 历史；
- 保持 T-2 工作树、脏子模块与 `crashes/` 原状。

T-1-A 获得授权后的第一件事应是：按 F1 模板写 RigidBody 契约规范
（稳定类型名、字段、反射、Registry、与 ECS `RigidBody3DComponent` 的边界），
并明确不包含 Adapter、序列化、Lua 与 Inspector 的实现。
