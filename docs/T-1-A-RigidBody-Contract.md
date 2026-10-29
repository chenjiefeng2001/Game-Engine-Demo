# T-1-A：RigidBody Contract 交付记录

**状态**：`T-1-A — IMPLEMENTATION COMPLETE, ACCEPTED (T-1 CLOSED — SCOPE-BOUND, 2026-10-29)`
**日期**：2026-10-29
**授权**：Gate 0 `PASS`，仅授权契约设计、组件注册、最小契约实现与直接相关单元测试。
**未启动**：T-1-B（序列化/冷启）、T-1-C（物理绑定）、T-1-D（Lua）、T-1-E（Inspector）。

本记录只陈述实际源码差异与测试结果。未执行 commit / push / tag / merge；
T-2 工作树、脏子模块与 `crashes/` 保持原状。

---

## 1. 冻结的契约范围

T-1-A 只冻结 **RigidBody 的声明式配置**，不包含任何物理运行时对象、序列化字段、
Lua 专用 API、Bridge 专用 ABI 或 Inspector 入口。

### 1.1 属性表（冻结）

| # | 属性名 | 反射类型 | 默认值 | 单位 | 合法范围 | 非法值行为 | 消费方 |
|---|---|---|---|---|---|---|---|
| 0 | `motionType` | String（稳定枚举名） | `"Dynamic"` | — | `"Static"` / `"Kinematic"` / `"Dynamic"`，大小写敏感 | 未知字符串 / 类型不符 → `false`，原值不变 | T-1-C 的 `BodyDef.type` |
| 1 | `linearDamping` | Float | `0.0` | Box2D damping 系数（引擎不做单位转换） | 有限且 `>= 0`，无上限 | 负数 / NaN / 正负无穷 / 类型不符 → `false`，原值不变 | T-1-C 的 `BodyDef.linearDamping` / `SetLinearDamping` |
| 2 | `angularDamping` | Float | `0.0` | Box2D damping 系数（引擎不做单位转换） | 有限且 `>= 0`，无上限 | 负数 / NaN / 正负无穷 / 类型不符 → `false`，原值不变 | T-1-C 的 `BodyDef.angularDamping` / `SetAngularDamping` |

默认值依据（不是按名称推断）：

- `motionType = Dynamic` 对齐 `Engine::BodyDef::type` 的默认值
  （`engine/include/Engine/Core/Physics/PhysicsDefs.h:151-152`）。
- `linearDamping/angularDamping = 0.0` 对齐 `Engine::BodyDef` 的默认值
  （`PhysicsDefs.h:169-170`）。
- 三个字段都能被现有 `Box2DPhysicsWorld::CreateBody` 实际消费：
  `type`、`linearDamping`、`angularDamping`
  （`engine/src/Box2D/Box2DPhysicsWorld.cpp:204-208`）。

### 1.2 明确不进入 T-1-A 契约的字段

**修订记录（2026-10-29）**：按复审结论重写本表与结论；只修订文档措辞，
未改源码、测试或证据数字。

> 判定纪律：下表区分三种不同事实——
> （a）Box2D 库能力；（b）Engine 封装层是否接线；（c）契约/Adapter 所有权决策。
> “暂不进入契约”不等于“后端不能实现”，也不等于已承诺后续扩展。

| 字段 | 不进入 T-1-A 的理由（代码证据） | 归属/后续 |
|---|---|---|
| `mass` / `density` | Box2D **创建期**支持 `BodyDef.material.density` / `FixtureDef.density`，也提供 `b2Body_SetMassData`（`third_party/box2d/include/box2d/box2d.h:504`）；`Box2DPhysicsWorld::CreateShapesFromBodyDef` 会应用 `def.material.density`（`engine/src/Box2D/Box2DPhysicsWorld.cpp:75-83`），`Box2DPhysicsBody::CreateShapeFromDef` 会应用 `FixtureDef.density`（`engine/src/Box2D/Box2DPhysicsBody.cpp:168-183`）。但 `IPhysicsBody` 未暴露 mass/density setter（`engine/include/Engine/Core/Physics/IPhysicsBody.h:34-37`），且 Collider Adapter 当前固定 `fd.density = 1.0f`（`engine/src/Core/Physics/PhysicsColliderAdapter.cpp:85`）。 | 不是后端无能力；是 RigidBody 契约没有该字段、Adapter 当前固定、质量/密度唯一来源未裁决。T-1-C 只能使用固定/派生规则；若需作者可编辑，必须回到契约阶段单独裁决，不在 T-1-C 内新增字段。 |
| `friction` / `restitution` | Box2D 创建期支持 `BodyDef.material` / `FixtureDef`；Adapter 当前固定 `fd.friction = 0.3f`、`fd.restitution = 0.2f`（`PhysicsColliderAdapter.cpp:86-87`）。按 Gate 0 G-2，接触表面属性归 Collider；但现有 Collider 契约没有这两个字段（`engine/include/Engine/Core/GameObject/ColliderComponent.h:43-107`）。 | 当前**没有作者通道**：不在 RigidBody 重复定义，也不修改 Collider schema（G-6）。若要作者可编辑，必须作为 Collider schema 扩展单独裁决（F2-S1/后续）；T-1-C 只能保留/记录 Adapter 常量，不得临时加字段。 |
| `fixedRotation` | `Engine::BodyDef` 有 `fixedRotation`（`PhysicsDefs.h:173`），但 Box2D 3.x 的 `b2BodyDef` 没有该字段（只有 `motionLocks`，`third_party/box2d/include/box2d/types.h:176-186,239`）；`Box2DPhysicsWorld::CreateBody` 未应用（`Box2DPhysicsWorld.cpp:201-221`），`IPhysicsBody` 无对应 setter。`Bare2DPhysicsBody` 支持该字段（`engine/src/Core/Physics/Bare2DPhysicsBody.cpp:52,280`）。现有 `MarioDemo` 设置了 `fixedRotation=true`，但 Box2D 生产路径实际是 no-op（`sandbox/src/MarioDemo/MarioDemoApp.cpp:84,117,160`）。 | 既有 no-op，不是 T-1 引入。T-1-C 若要支持，必须定义 Box2D 等价实现（如 `motionLocks`/mass data）并做 legacy 回归；契约字段需单独裁决，不能由 Adapter 隐式添加。 |
| `allowSleep` | `Box2DPhysicsWorld::CreateBody` 把 `allowSleep` 赋给 `b2BodyDef::isAwake`（`Box2DPhysicsWorld.cpp:209`），没有设置 `b2BodyDef::enableSleep`（`types.h:242,245`）。`IPhysicsBody` 只有 `SetAwake`，没有 enableSleep 语义的 setter。 | **既有语义缺陷**：`allowSleep=false` 实际会以睡眠状态创建且仍可能休眠/唤醒。T-1-C 修正时必须覆盖 legacy `PhysicsComponent` 回归；在此之前不得把 `allowSleep` 当作已支持。 |
| `isBullet` / CCD | Engine/Box2D 的 `BodyDef` 有该字段，Box2D 也提供 `b2Body_SetBullet`（`third_party/box2d/include/box2d/box2d.h:576`）；但 Engine `CreateBody` 未应用，`IPhysicsBody` 无 setter。`Bare2D` 支持（`Bare2DPhysicsWorld.cpp:955`）。 | 引擎封装层未接线，不是 Box2D 无能力。T-1-C 若需要，必须补 wrapper 能力并做契约裁决；不得只加字段不落地。 |
| `gravityScale` | Engine `BodyDef` / `IPhysicsBody` 没有 per-body gravityScale；Box2D 有 `b2BodyDef::gravityScale`（`types.h:226`）与 `b2Body_SetGravityScale`（`box2d.h:531`）。`EngineSettings::Physics::gravityScale` 是全局配置（`engine/include/Engine/Core/EngineSettings.h:66-70`），当前未接入 body。 | 引擎封装层未暴露；全局设置不等于 per-body 能力。T-1-C 若需要，必须补 wrapper + 契约裁决；不得把全局值当作 RigidBody 字段。 |
| 初始线速度/角速度 | Box2D `b2BodyDef` 有 `linearVelocity/angularVelocity`（`types.h:207-210`），`Bare2D` 会应用；但 Box2D 的 `CreateBody` 未应用（`Box2DPhysicsWorld.cpp:201-221`）。 | 本轮不设作者字段是**阶段/产品决策**，不是后端不可能；T-1-C 可用运行时 `SetLinearVelocity` 或另行契约裁决。 |
| 位置/旋转 | `BodyDef.position` 被 `CreateBody` 消费；`BodyDef.angle/rotation` 未被消费；位置/旋转归 GameObject `TransformComponent`（Gate 0 G-2）。 | 不复制为 RigidBody 字段。但 T-1-C **必须**显式从 Transform 设置 position/angle（或 `SetTransform`），这是绑定前置条件，不是“延期字段”。 |
| `enabled` | 复用 `Component::IsEnabled()`，不新增第二个开关；`ColliderComponent` 有 mutation hook（`ColliderComponent.h:76-83`），`RigidBodyComponent` 没有。 | T-1-C 必须定义 RigidBody 属性变更/启用状态的传播方式（mutation hook 或显式 rebuild）；当前契约没有该机制，不能假定自动同步。 |
| `shape/radius/halfX/halfY/isSensor/category/mask` | Collider 契约既有字段。 | 不复制（G-2/G-6）。 |

**结论：延期项不是契约扩展承诺。**
`mass/density/friction/restitution/fixedRotation/allowSleep/CCD/gravityScale`
以及初始速度均为 T-1-A 的延期项；是否进入契约需单独裁决。T-1-C 不得在
Adapter 内临时补字段，也不得把“Box2D 库支持”当作“Engine 封装已支持”。

**T-1-C 映射前置（与延期字段直接相关）：**

- `RigidBodyMotionType` 的数值序是 `Static=0, Kinematic=1, Dynamic=2`，
  而 `Engine::BodyType` 是 `Static=0, Dynamic=1, Kinematic=2`
  （`PhysicsDefs.h:27-31`）。T-1-C 必须按稳定字符串/`switch` 映射，
  **不得 `static_cast<BodyType>`**，否则 Dynamic/Kinematic 互换。
- 位置/角度同步、属性变更传播、`allowSleep/fixedRotation/CCD/gravityScale`
  的后端等价实现，均应在 T-1-C 契约中冻结并测试，不能只在本表登记。

### 1.3 稳定身份与入口

| 项目 | 冻结值 | 实现位置 |
|---|---|---|
| 稳定类型名 | `"RigidBody"` | `RigidBodyComponent::GetComponentTypeName()` |
| 显示名 | `"RigidBody"` | `GetTypeDisplayName()` |
| 类名 | `Engine::RigidBodyComponent` | `engine/include/Engine/Core/GameObject/RigidBodyComponent.h` |
| 注册入口 | `ComponentRegistryGo::Register("RigidBody", factory)` | `engine/src/Core/GameObject/RigidBodyComponent.cpp:130-134` |
| 内置注册 | `EnsureRegistered()` 调用 `RegisterRigidBodyComponent()` | `engine/src/Core/GameObject/ComponentRegistry_Go.cpp:31-34` |
| 创建入口 | `GameObject::AddComponentByName("RigidBody")` | 复用 F1 既有入口，无新组件模型 |
| 反射入口 | `GetPropertyCount/Desc/Value/SetPropertyValue` | 3 个属性，顺序固定 |

未新增 Lua、Bridge、Inspector 或 Scene 序列化代码；这些通道若已存在通用逻辑，
只会按稳定类型名/反射元数据自动看到 RigidBody。专用入口属于 T-1-D/T-1-E。

---

## 2. 组件缺失语义（冻结）

| 配置 | T-1-A 行为 | 测试 | T-1-C 后续约定 |
|---|---|---|---|
| 仅 Collider | 保持 F2-B 行为：静态碰撞区域，`BuildBodyDef` 仍为 `Static` | `MissingSemanticsProduceNoImplicitBody` ① | 不因“没有 RigidBody”而创建动态 body |
| 仅 RigidBody | 没有几何 → 不创建任何物理 body；Adapter 不跟踪 | 同上 ② | 没有 Collider 时不创建 body |
| RigidBody + Collider | T-1-A 不改变 Adapter：仍为 `Static`，不得隐式切换为 Dynamic | 同上 ③ | T-1-C 通过显式映射使用 `motionType`；不得依赖全局状态推断 |
| 两者都无 | 无 body | 由 ② 的路径推导 | 不创建 body |

这组语义是 T-1-A 的关键防回归门：**注册 RigidBody 不得改变既有 Collider-only
的静态行为**，也不得让缺失 RigidBody 的物体意外获得动态物理。

---

## 3. 源码差异

### 3.1 新增

| 文件 | 内容 |
|---|---|
| `engine/include/Engine/Core/GameObject/RigidBodyComponent.h` | 枚举 + 契约类 + 反射 + 注册声明 |
| `engine/src/Core/GameObject/RigidBodyComponent.cpp` | 校验、反射、Registry 注册 |
| `tests/test_physics/RigidBodyContractTest.cpp` | 7 个 T-1-A 契约门禁 |
| `docs/T-1-A-RigidBody-Contract.md` | 本记录 |

### 3.2 修改

| 文件 | 差异 |
|---|---|
| `engine/src/Core/GameObject/ComponentRegistry_Go.cpp` | include + `RegisterRigidBodyComponent()`（2 处，共 3 行） |
| `tests/CMakeLists.txt` | `test_physics` 增加 `RigidBodyContractTest.cpp`（1 行） |

### 3.3 明确未修改

- `ColliderComponent.h/.cpp`、`SceneSerializerV1.*`、`PhysicsColliderAdapter.*`、
  `Box2DPhysicsWorld.*`、`GameplayAPI.cpp`、`capi.*`、`editor_avalonia/*`。
- 未修改 Collider schema、场景格式版本、Bridge ABI、Lua API、Inspector。
- 未修改 T-2 的 Content 代码、关闭记录或故障测试。

---

## 4. 测试与构建证据（实际运行）

### 4.1 T-1-A 契约测试

命令：

```text
cmake --build build --config Debug --target test_physics
./build/tests/Debug/test_physics.exe --gtest_filter=T1A_RigidBodyContract.*
cmake --build build --config Release --target test_physics
./build/tests/Release/test_physics.exe --gtest_filter=T1A_RigidBodyContract.*
```

结果：

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug | **7/7 通过** | `build/t1a_test_physics_contract_debug.log` |
| Release | **7/7 通过** | `build/t1a_test_physics_contract_release.log` |

覆盖的 7 个门禁：

1. `DefaultsAndStableIdentity`：默认值、类型名、Registry 创建。
2. `MotionTypeNamesRoundTrip`：稳定字符串解析/输出、非法名称/空指针。
3. `ReflectionRoundTrip`：3 个属性的读写与越界索引。
4. `InvalidValuesAreRejectedWithoutMutation`：未知枚举、负数、NaN、无穷、
   类型不符全部拒绝且不改值。
5. `LifecycleAndSingleInstance`：Add 单实例、Remove、重复 Add、生命周期监听。
6. `FieldOwnershipNoDuplicateStorage`：RigidBody 仅 3 个属性，不含 Collider/
   Transform 字段；Collider 仍为 8 个属性。
7. `MissingSemanticsProduceNoImplicitBody`：三种配置下无隐式 body 切换。

### 4.2 相关回归

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `test_physics` 全量 | **61/61** | **61/61** | `build/t1a_test_physics_{debug,release}.log` |
| `test_bridge` 全量 | **100/100** | **100/100** | `build/t1a_test_bridge_{debug,release}.log` |
| `test_scripting` 全量 | **27/27** | **27/27** | `build/t1a_test_scripting_{debug,release}.log` |
| `test_content` 全量 | **92 通过 / 5 既有失败 / 1 skip** | 同左 | `build/t1a_test_content_{debug,release}.log` |
| 完整 CTest | **16/17** | **16/17** | `build/t1a_baseline_ctest_{debug,release}.log` |

`test_content` 的 5 个失败与 T-2 关闭记录完全一致，未新增、未改写：

- `ContentDogfood.DF03_Smoke_LoadRunTimerExpire`
- `ContentDogfood.DF03_EntityCount_MultiScript`
- `Dogfood05.LoadAndRun`
- `Dogfood06.LoadAndRun`
- `Dogfood07.LoadRunRestart`

### 4.3 构建警告（按实际日志）

| 构建 | 观察值 | 日志 |
|---|---|---|
| Debug `test_physics` 定向构建 | **2×LNK4006**（既有 RHI 符号重复）+ **1 条 CMake Shaderc 未找到警告** | `build/t1a_build_test_physics_debug.log` |
| Release `test_physics` 定向构建 | **2×LNK4006**（同上） | `build/t1a_build_test_physics_release.log` |
| 全量 Debug/Release 增量构建 | 日志中 **0 条 warning 行**；因为 `EngineCore` 已在定向构建阶段链接完成，不能据此宣称“零警告全量构建” | `build/t1a_build_all_{debug,release}.log` |

T-2 记录的第一方全量构建基线是 Debug/Release 各 5 条既有警告；本轮**未做**
clean rebuild，因此不能把增量日志的 0 warning 当作新的全量警告结论。

---

## 5. 已知限制 / 未完成项（必须带入复审）

1. **T-1-A 不实现 Serialize/Deserialize。**
   `SceneSerializerV1::CaptureScene` 会收集所有 `GetComponentTypeName() != nullptr`
   的组件（`engine/src/Core/Content/SceneSerializerV1.cpp:134-143`）。因此在此期间
   把 RigidBody 加入场景并保存，会写入 `type: "RigidBody"` + 空 `data`；冷启后
   组件存在但字段恢复默认值。**T-1-B 完成前，不得把 RigidBody 当作可持久化的
   作者数据，也不得宣称冷启恢复已通过。** 这是 T-1-A 的已知中间态，不是 T-1-B 的
   验收证据。
2. **T-1-A 不改变物理绑定。** 三种配置的 body 类型仍由既有
   `PhysicsColliderAdapter` 决定（Collider-only/both 均为 Static）；动态映射、
   Transform 位置同步、World 生命周期属于 T-1-C。
3. **没有 Lua 专用域。** 通用 `Engine.component.*` 可按类型名 add/has/get/remove，
   但未验证 RigidBody 的脚本行为；T-1-D 负责。
4. **没有 Inspector。** 托管 UI 仍只显示 Camera；T-1-E 负责。
5. **没有 ECS 桥接。** `RigidBody3DComponent` 未接通、未迁移（G-5）。
6. **未验证真实物理行为。** T-1-A 只证明声明/反射/缺失语义；没有任何
   “动态 body 下落/碰撞/冷启”结论。
7. **未做 clean rebuild。** 警告数字按 §4.3 的增量日志原样保留。

---

## 6. 基线保护核对

- T-2 关键文件 SHA-256 与 Stage 0 记录逐项一致（`RevisionStore.cpp/h`、
  `AtomicFile.cpp`、`ProjectPaths.cpp`、`Utf8Path.cpp`、`capi.h`、
  `T-2-Closure-Record.md`、`test_revision_commit.cpp`）。
- 相对 Stage 0 基线，工作树新增仅 T-1-A 的 3 个未跟踪源码/测试文件 +
  2 个已跟踪文件修改（外加本记录）；暂存区仍为空。
- `crashes/` 仍为 115 个产物，未删除/改写。
- 3 个脏子模块（`d3d12ma`、`openal-soft`、`vma`）保持原样。
- 未执行 Git 写操作。

---

## 7. 复审入口与下一步

T-1-A 的复审建议只按以下顺序判定：

1. 属性表是否接受（尤其是“只保留 3 个有真实后端消费方的字段”这一收窄）。
2. 缺失语义是否接受（尤其“RigidBody + Collider 在 T-1-A 仍为 Static”）。
3. 已知限制第 1 条（空 data 过渡态）是否接受；若接受，T-1-B 必须首先补齐
   Serialize/Deserialize 与冷启门禁。
4. 测试/警告/既有失败数字是否按本记录原样保留。

T-1-A 通过前不启动 T-1-B；T-1-B 通过前不启动 T-1-C。
