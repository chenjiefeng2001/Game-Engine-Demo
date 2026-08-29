# Component Ledger — F0 Audit (+F1 已完成 / F2 进行中)

> 建立日期：2026-08-28 · 阶段：Engine Foundation v1 / F0→F1（已完成）→F2（进行中）
> 逐项资产台账。每项给出：运行时模型、序列化身份、脚本可达性、生命周期、
> 引用身份、现有证据。F0 只登记不修改；F1 起允许引擎改动（五线同步纪律）。

---

## 1. Entity / GameObject

- **运行时模型**：`GameObject`（结构型，`AddComponent<T>` 按 `typeid` 去重，
  `m_Components: unordered_map<size_t, shared_ptr<Component>>`）；另有数据导向
  ECS `EntityManager`（Archetype/Chunk/64-bit generational ID）。
- **接口**：`AddComponent/GetComponent/RemoveComponent/HasComponent/ForEachComponent`；
  层级 `SetParent/AddChild/RemoveChild/FindChild`；`SetActive/GetID/SetLayer`；
  **F1 新增契约操作** `AddComponentByName/GetComponentByName/HasComponentByName/
  RemoveComponentByName/Attach`（经 `ComponentRegistryGo` 稳定类型名，非 typeid）。
- **Editor**：bridge `EditorSession_{CreateEntity,DeleteEntity,RenameEntity}`，
  Avalonia `EditorHostService.CreateEntity/DeleteEntity/RenameEntity`。
- **序列化**：🟡 内容快照仅持久化 `{Name + Position + SpriteGUID + ScriptGUID}`，
  **children/active/layer 不落盘**。
- **脚本**：`Engine.entity.{find,spawn,destroy}` + `HandleFindByName/HandleSpawn/HandleDestroy`；
  **F1 新增 `Engine.component.*` 域**（乘车契约组件，反射属性读写）。
- **引用身份**：🟡 实体有 per-session `uint32 m_ID`；内容绑定 `EntityContentBinding`
  按 **实体 name** 键控；**无跨会话稳定 Entity GUID**。
  （契约**组件**已获稳定字符串类型名身份 `ComponentRegistryGo`）。
- **证据**：`GameObject.h`、`SceneSerializerV1.h`、`capi.cpp`、`GameplayAPI.h`、
  `tests/test_bridge/test_editor_capi.cpp`、AV-G3 gate。

## 2. Transform

- **运行时模型**：内置 `TransformComponent`（非 Component 派生，每个 GameObject 内联一份）；
  `TransformSystem`。子类监听见 `AnimationLocalTimeline`。
- **Editor**：`EditorSession_{GetEntityPosition,SetEntityPosition}`（仅平移）。
- **序列化**：🟡 仅 `position(px,py,pz)`；**rotation/scale 无字段、不落盘**。
- **脚本**：`Engine.transform.*`、`Handle{Get,Set}Position/Translate`。
- **证据**：`TransformComponent.h`、`GameplayAPI.cpp`、`test_scripting`。

## 3. SpriteRenderer

- **运行时模型**：`SpriteComponent : Component`；`GameObject::GetSprite()/AddComponent<SpriteComponent>()`。
- **Editor**：`EditorSession_{AssignSprite,GetEntitySprite}`，Avalonia inspector。
- **序列化**：✅ `spriteGuid`（资源 GUID）往返。
- **脚本**：❌ Lua 无 sprite 域；sprite 颜色/层级等仅 C++ 可改。
- **证据**：`SpriteComponent.h`、`SceneSerializerV1`、`test_bridge`、AV-G1/G2/G3。

## 4. Camera（**F1：首个“四面向全通”契约组件**）

- **运行时模型**：✅ `CameraComponent : Component`（`Core/GameObject/CameraComponent.{h,cpp}`）；
  配置 zoom / viewport / bounds（视口与世界 AABB 约束，字符串形式）+ 启停（复用
  `Component::SetEnabled/IsEnabled`）。`MakeOrthographic(aspect)` 由组件状态构造真实
  `OrthographicCamera`（应用 zoom/bounds）。反射走 `GetPropertyDesc/Value/SetPropertyValue`，
  稳定身份 `GetComponentTypeName()=="Camera"`。
- **Editor**：✅ bridge `EditorSession_{AddComponent,RemoveComponent,HasComponent,
  GetComponentCount,GetComponentTypeAt,GetComponentProperty,SetComponentProperty}` +
  `EV_COMPONENT_CHANGED`；Avalonia Camera inspector（加/移除/属性回填）。
- **序列化**：✅ `SceneSerializerV1` 快照新增 `components[]`；`Camera` 走
  `Component::Serialize/Deserialize` 组件化往返（type=稳定名 + data）。
- **脚本**：✅ 通用 `Engine.component.*` 域（add/has/get/remove + get/set number/bool/string），
  经反射元数据读写，不堆 `Engine.camera.*` 特化。
- **Lifecycle / Persistence**：✅ 冷重启：Save→磁盘 components[]→Open→反序列化→Attach 还原，
  zoom/enabled/viewport/bounds 全保真（F1CameraSaveLoadColdRestart）。未知类型 → warning + 实体照常加载。
- **Reference**：✅ 稳定字符串类型名（`ComponentRegistryGo` 工厂注册表，与持久化/Lua/Editor 共用）。
- **证据**：`CameraComponent.{h,cpp}`、`ComponentRegistry_Go.{h,cpp}`、`GameObject::AddComponentByName/Attach`、
  `SceneSerializerV1.*`、`GameplayAPI.cpp`（component 域）、`capi.{h,cpp}`、`MainViewModel.cs`、
  `test_bridge` F1×4、`test_scripting` F1E/F1C。

## 5. Collider

- **运行时模型**：🟡 无独立 Collider 组件；形状内嵌于 `BodyDef::shape` / `ShapeDef`
  （Box/Circle/Edge/Chain/Polygon）+ `isSensor`，随 `PhysicsComponent::CreateBody` 创建。
  3D 侧通过 ECS `PhysicsComponents.h` + Jolt。
- **Editor / Serialization / Script**：❌（形状参数不落盘、编辑器无碰撞面板）。
- **证据**：`PhysicsDefs.h`、`PhysicsComponent.h`。

## 6. RigidBody

- **运行时模型**：🟡 `PhysicsComponent`（2D，已验证）+ `PhysicsComponent3D`（3D）+
  ECS `RigidBody3DComponent`。body type Static/Dynamic/Kinematic。
- **Editor / Serialization / Script**：❌（Lua 无 physics 域；`Serialize` 虚方法存在但
  Content 快照不调用）。
- **证据**：`PhysicsComponent.h/.cpp`、`PhysicsComponent3D.h`、`ECS/PhysicsComponents.h`、
  `PhysicsSyncSystem.cpp`。

## 7. Physics Material

- **运行时模型**：🟡 `PhysicsMaterial{friction,restitution,density}` + 预设
  （Stone/Wood/Metal/Rubber/Ice/Bouncy）；可作 `BodyDef.material` / `ShapeDef.material`；
  无独立“材质资产”。
- **Editor / Serialization / Script**：❌。
- **证据**：`PhysicsDefs.h`。

## 8. Script

- **运行时模型**：✅ Lua 脚本；每实体脚本经 Script GUID 绑定；`ScriptInstance` / `LuaEngine`。
- **Editor**：✅ `AssignScript`、`ScriptRead/ScriptSave`、Avalonia 脚本编辑面板。
- **序列化**：✅ `scriptGuid` 往返。
- **Lifecycle**：✅ Play/Reload（`_PERSIST` 保留）/Stop，AV-G3 实测。
- **引用**：✅ 资产用 GUID；句柄表 Reload 后有效。
- **证据**：`Scripting/*`、`GameplayAPI`、`bridge` P3-D、`test_scripting` 23/23、AV-G3。

## 9. Input

- **运行时模型**：🟡 全局 `InputManager` / `InputAction`（非组件）。脚本经
  `IScriptInputProvider` 抽象 + `Engine.input.*`（键盘状态）。
- **Editor / Serialization**：❌ / —（无作者态输入绑定）。
- **证据**：`Core/InputManager.h`、`GameplayAPI.cpp`。

## 10. Audio

- **运行时模型**：🟡 `AudioSourceComponent : Component`；OpenAL 引擎
  （`OpenALAudioEngine`）；`Listener`。3D/2D 定位能力见摘要。
- **Editor / Serialization / Script**：❌（Lua 无 audio 域；source 参数不落盘）。
- **证据**：`Core/Audio/AudioSourceComponent.h`、`Core/Audio/IAudioEngine.h`。

## 11. Animation

- **运行时模型**：🟡 `SkinningComponent`（3D）、`AnimationController`/`AnimationManager`、
  `AnimationLocalTimeline`（可驱动 `SpriteComponent` transform）、AnimStateMachine/BlendTree。
- **Editor / Serialization / Script / Lifecycle**：🟡/❌（管理器驱动，无组件化作者态；
  不落盘）。
- **证据**：`engine/include/Engine/Animation/*`、`AnimationLocalTimeline.cpp:230`。

## 12. Particle

- **运行时模型**：🟡 `GPUParticleSystem` / `GPUParticle` + `OpenGLComputeParticles`。
- **Editor / Serialization / Script**：❌（无 ParticleComponent，GPU 代码直管）。
- **证据**：`Rendering/GPUParticleSystem.h`、`Core/Physics/GPUParticle.h`。

## 13. Game UI

- **运行时模型**：🟡 `GameHUD` / `TextRenderer`（非组件）；无 Canvas / UI 树组件。
- **Editor / Serialization / Script**：❌。
- **证据**：`Rendering/GameHUD.h`、`Rendering/TextRenderer.h`。

## 14. Tilemap

- **运行时模型**：❌ 全树无 Tilemap（源码/头文件零匹配）。
- **全部维度**：❌。
- **证据**：全仓库 `Tilemap` 检索 0 命中。

## 15. Prefab

- **运行时模型**：🟡 仅 `AssetType::Prefab` 枚举标记 + legacy ImGui Inspector 的
  "Revert to Prefab" 覆盖标记概念；**无 prefab 资源结构、无实例化、无序列化**。
- **Editor / Serialization / Script**：❌。
- **证据**：`Editor/AssetTypes.h:66`、`InspectorPanel.cpp:313`。

## 16. Scene

- **运行时模型**：✅ `Scene` / `SceneManager` / `Serializer` / Content `SceneSerializerV1`。
- **Editor**：✅ bridge `OpenProject/SaveProject`；Avalonia 项目级 Open/Save。
- **序列化**：✅（范围限于 §2 红线）。**Lifecycle**：✅ open→edit→save→close→reopen 一致。
- **引用**：🟡 资产 GUID 稳定；实体按 name 键控（list 顺序跨会话不可依赖，见 AV-GP-101）。
- **证据**：`Core/Scene/*`、`Content/SceneSerializerV1.*`、AV-G3。

## 17. Resource

- **运行时模型**：✅ `ResourceManager` / `ResourceGUID` / `ResourceRegistry` /
  `ContentRegistry` + `PakFile` / `OsFileMount`。
- **Editor**：✅ bridge `ImportAsset/RenameAsset/GetAssetGuid`；Avalonia 资产面板/导入。
- **Evidence**：`Core/Resources/*`、`Core/Content/*`、AV-G3（Import/Rename/缺失 GUID 告警）。

## 18. Runtime Spawn/Destroy

- **运行时模型**：✅ `HandleSpawn/HandleDestroy`（Lua）、`GameObject AddChild/RemoveChild`、
  `SetActive`、ECS `EntityCommandBuffer`。
- **Editor**：✅ `CreateEntity/DeleteEntity`。
- **Persistence**：🟡 运行态 spawn 的对象**不进内容快照**（authored/runtime 分离仅靠
  “不保存”约定，无显式标记）。
- **证据**：`GameplayAPI.h/.cpp`、`capi.cpp`、AV-G2/G3。

---

## 汇总：现有证据库

- `tests/test_bridge/test_editor_capi.cpp` — **25/25** PASS（F1 Camera×4：生命周期/负路径/落盘重启/未知类型告警）
- `tests/test_scripting/*` — **25/25** PASS（F1E 通用 `Engine.component.*`、F1C 落于真实 GameObject）
- F1 回归（本阶段）：test_core 20/20、test_content 42/42、test_gp01 22/22；Avalonia `dotnet build` 0 warn/0 err
- `AvaloniaEditor --gate3g` — ALL GREEN（D1–D5 + M1 + Authoring/Scripting/Persistence + E1–E3）
- `docs/Integrity-Report` / Evidence-Baseline — I1+I2+I3, **169 tests**（F2 baseline，`7b8e703` 冻结）

---

## F2 Ledger（进行中 · 依据 `docs/Engine-Foundation-F2-Charter.md`）

> 每个 F2 阶段完成的裁决 / 摩擦 / 证据在此登记，互见 Charter 与
> `Engine-Foundation-Phase-Plan.md` §F2。F2 只允许 3 个结局（A Promate / B Blocked / C Contract Failure）。

### EF-F2-001 · F2 范围控制（开工即登记）

- **裁决**：F2 = **单个第二实证组件 Collider**，不是“批量补齐物理组件”。成功标准不是
  “又做对了一个组件”，而是 **「证明 F1 模板不是 Camera 特例，能跨 Physics/ECS 边界」**。
- **范围红线（F2 不做）**：RigidBody、Prefab、Camera 渲染视口、RenderGraph、Collision Events
  API、大规模组件迁移、ECS 重构、Undo/Redo、为“完整性”堆组件。
- **契约纪律**：不为了序列化方便把 Physics 内部结构直接暴露给 Component Contract；
  Lua 只走通用 `Engine.component.*`，**不新增 `Engine.collider.*`/`Engine.physics.set_*` 特化**
  （否则 F1 通用组件原则退化为特化 API）。
- **状态**：OPEN（F2-0 ✅ Baseline Freeze / F2-A ✅ Collider Contract 声明式上卷；
  F2-B Physics Binding 待启动）。并存 F2-A 证据：`ColliderComponent.{h,cpp}`、`ComponentRegistry_Go.cpp`
  （+Collider）、test_bridge 28/28、test_scripting 27/27、baseline→174。

### GP-F2-001 · 双模型边界 / 假闭环禁令（开工即登记）

- **裁决（Golden-Path 视角）**：F2 不得出现 Camera 式的“组件存在、真实系统旁边另有一套对象”
  的假闭环。Collider 必须证明 **GameObject Component 与实际 Physics/ECS 数据之间存在明确、
  稳定、可序列化的契约**，并明确生命周期所有权与同步责任（Create/Attach/Modify/Remove/
  Destroy/Reload 各自发生什么）。
- **边界结果处置**：若 Collider 自身暴露双模型架构无法干净表达 → 这是 F2 最有价值的结果，
  进入 Result B（明确最小 Foundation 修复）或 Result C（停止补组件、重做 Foundation 架构）。
  **Result C 视作成功**：F2 判断的是“这个引擎是否具备支撑未来项目的基础设施”，而非
  “引擎有没有 Collider”。
- **状态**：OPEN（F2-A 未触碰 Physics 边界，等待 F2-B 验证所有权模型）。

### EF-F2B-001 · F2-B 开工：Collider Runtime Binding（B0 基线冻结）

- **基线**：`engine-foundation-v1-f2-a-collider`（`f674331`）；Integrity Gate 174/174；
  工作区仅余 `Application.cpp`（GP1-DX 遗留）+ spike 目录（均排除）。
- **证据包规划**：`docs/Engine-Foundation-F2B-Plan.md`（B0+B1+B2-B8）；结束时写
  `F2B-Closure.md`；完成后才更新 Maturity-Matrix / Evidence-Baseline / Phase-Plan。
- **状态**：✅（B0 完成，F2-B 起点零代码）。

### EF-F2B-002 · B1 Physics Architecture Audit 裁决（源码证据）

- **Body owner**：原生 body = Physics World（`b2DestroyWorld`/JPH PhysicsSystem）；包装
  `IPhysicsBody` = world 注册表 + 组件共享；Create 显式（`PhysicsComponent::CreateBody`）；
  Destroy 经组件析构→`DestroyBody`（安全，stale wrapper 静默 no-op）；**Recreate 无任何
  现有机制**（`PhysicsComponent::Serialize` 只存 `hasBody` 标记）。
- **World 生命周期**：无集中式 2D world 管理（`PhysicsSystemManager::CreateWorld2D` 返回
  nullptr）；世界销毁连带原生 body 全灭；**纠缠点**：`PhysicsComponent` 以 shared_ptr 持有
  世界 → 组件存活期间世界无法真正销毁。
- **四模型关系**：GameObject ↔ ECS Entity **未连接**（`ECSBridge` 全引擎零调用，dormant）；
  ColliderComponent 的映射目标 = 2D 路径（BodyDef/ShapeDef），无需 ECS。
- **能力缺口（登记，不偷改 Collider）**：**GAP-1** 初始形状 sensor 标记丢失
  （`CreateShapesFromBodyDef` 未赋 isSensor，`Box2DPhysicsWorld.cpp:75`）；
  **GAP-2** 运行时 category/mask 更新是 no-op（`Box2DPhysicsBody::SetFilterData` 空实现）。
- **契约决策**：D1 Adapter 持有 world + body 注册表（组件零物理引用）；D2 形状/滤波变更 =
  Destroy/Recreate，enabled = 实时 SetActive；D3 冷重启后 Adapter 显式 RebuildAll。
- **裁决**：**可承载 → 进入 B2（非 Result C）**。理由：F2-A 冻结的「组件零物理引用」恰好
  绕开现有最大纠缠点（组件钉住世界）；Destroy/Rebuild 可行；无强制反向同步路径。
  若 B3 破坏性测试失败 → 按 Charter 直接登记 Result C。
- **证据**：`Engine-Foundation-F2B-Plan.md` §B1；`Box2DPhysicsWorld.cpp`、`PhysicsComponent.cpp`、
  `PhysicsSystemManager.cpp`、`ECSBridge.cpp`、`PhysicsSyncSystem.cpp`、`Engine-Foundation-v1.md:23`。
- **状态**：✅（B1 完成，待 B2）。

### EF-F2B-003 · B2 最小 Runtime Binding 完成（晋升门全绿）

- **交付**：`engine/include/Engine/Core/Physics/PhysicsColliderAdapter.{h,cpp}`（**唯一 Runtime 映射层**）；
  API：`SetWorld/ReleaseWorld` + `Bind/Unbind/Rebuild/RebuildAll/Clear` + `IsBound/GetBody/BuildBodyDef`。
  `ColliderComponent` 零改动、零物理引用（F2-A 冻结保持）。
- **D1 落地**：Adapter 显式拥有/释放 World（`SetWorld/ReleaseWorld`），**未复制** PhysicsComponent
  的 shared_ptr 钉世界缺陷；BodyRegistry = `ColliderComponent*（observer）→ shared_ptr<IPhysicsBody>`。
- **GAP-1 规避（不修旧路径）**：Bind 时经 Fixture 路径重建形状，sensor + category/mask 落到真实
  Physics shape；GAP-1 保持登记（EF-F2B-002）。
- **v1 Mutation 契约（B2-4 表）已内建**：enabled → 即时 SetActive；shape/radius/halfX/halfY/
  sensor/category/mask → Rebuild（Destroy/Recreate），无隐式实时同步。
- **测试**：`tests/test_physics/ColliderBindingTest.cpp` +4（test_physics 16→**20/20**）：
  `Binding_CircleParamsLandOnPhysics` / `Binding_BoxSensorEnabledLandOnPhysics`（B2-5 sensor 正确性门：
  b2Shape_IsSensor/b2Shape_GetFilter 直接验真实 shape）/ `Destroy_BodyGone_ColliderSurvives` /
  `Rebuild_DestroyThenRecreate_ParamsFromComponent`。
- **晋升门核对**：Contract（Collider 无 Physics include ✓ / Adapter 唯一映射层 ✓）· Runtime
  （Create/Destroy/Rebuild ✓）· Correctness（Circle/Box/sensor/enabled/filter 真实落地 ✓）·
  Ownership（Component 不拥有 Body ✓ / Body 不拥有 Component ✓ / World 可控 ✓）·
  Negative（Destroy 后 Component 存活 ✓ / Rebuild 无重复 Body ✓ / Remove 无 dangling ✓）。
- **状态**：✅（B2 完成，晋升 → B3）。

### EF-F2B-004 · B3 World Lifecycle 完成 —— 裁决 **Result A（Runtime Lifecycle 成立）**

- **语义升级**：Adapter 分离 `m_Tracked`（受管组件集合，**跨 World 销毁存活**）与 `m_Bodies`
  （已绑 body）。`ReleaseWorld` 清 body + 释放世界但保留受管集合 → `SetWorld(new) + RebuildAll`
  即可从组件完整重建。`RebuildAll` 成为 Foundation v1 正式 Runtime contract（D3 落地）。
- **测试**：`ColliderBindingTest.cpp` +8（test_physics 20→**28/28**）：
  - B3-1 `WorldLifecycle_DestroyWorld_RestoreBodyFromComponent`：真 World 销毁 → Body#1 无效
    （b2Body_IsValid false）→ Component 地址/状态硬断言未变 → 新 World RebuildAll → Body#2
    参数一致、无重复。
  - B3-2 `WorldLifecycle_RebuildUsesCurrentComponent_NotOldBodySnapshot`：跨 World 修改 radius
    1.0→2.0，重建后 Body=2.0 —— **证明重建用 Component 当前状态而非旧 Body 快照**。
  - B3-3 `WorldLifecycle_MultiEntity_RebuildAllRestoresAllThree`：Circle / Box Sensor / Box
    Disabled 三实体，Release→SetWorld→RebuildAll 后 3 Bodies，逐个验 shape/sensor/filter/enabled。
  - B3-4 `WorldLifecycle_BehaviorConsistentAfterRebuild`：Run A（单世界）vs Run B（中途毁世重建）
    对拍 position/velocity/active/sensor/filter，重建后行为恢复对齐。
  - B3-5 四条失败路径：无 World（干净失败 + Log 可诊断）/ 重复 Bind（幂等）/ 重复 Rebuild
    （count 恒 1）/ Release 后 Clear（不访问已毁 World）。
- **裁决核对**：Result A 全项成立 —— World 可死、Component 不死、新 World 完全从 Component
  重建。**无 Result B 局部缺口**；无 Result C 触发（Adapter 独立管理 World，Component 零持有，
  恢复不依赖任何 Physics 内部 Entity）。
- **范围说明（非缺口）**：Collider 契约不含 body type → Adapter 固定映射 Static；velocity 类
  运动 observable 待 RigidBody 契约进入后测试（F2 红线，不在本阶段）。
- **状态**：✅（B3 完成，晋升 → **B4 Cold Restart**：场景落盘→冷启→Collider 还原→World 创建→
  RebuildAll→Body 恢复→模拟可用）。

> **两套数字记录（至 F2-B 收口前持续更新）**：Actual = test_physics **28/28**（F2ColliderPhysics 12）；
> Declared baseline = **174**（I2 预期红灯，Evidence-Baseline 在 B3/B4 最终冻结时统一更新）。

### EF-F2B-005 · B4 前置冻结（precondition freeze）

- **冻结基线**：B2 28/28 · B3 28/28；Adapter ownership 模型（m_Tracked 跨 World 存活 / m_Bodies
  已绑 body / Component = source of truth / World = disposable / Body = derived）。
- **B4 待验证问题**：GameObject 的 ColliderComponent 生命周期能否**不暴露 Physics 内部结构**、由
  Adapter **自动确定性跟随**（Add→Bind / Remove→Unbind / 属性变更→SetActive/Rebuild）。
- **B1 已知缺口**：GameObject 组件生命周期事件（Attach→OnCreate / Remove→OnDestroy）存在且同步，
  但**无统一通知机制** → 按预案 Result B 路径：增加一个很小的 Integration Hook
  （GameObject 组件生命周期监听器，默认空、零行为变化），不轮询、不 shadow registry。
- **状态**：✅（冻结完成，进入 B4 实现）。

### EF-F2B-006 · B4 Component Lifecycle Integration 完成 —— 裁决 **Result A（自动确定性跟随）**

- **Integration Hook（Result B 预案路径落地）**：`GameObject` 新增组件生命周期监听器
  （`AddComponentLifecycleListener/RemoveComponentLifecycleListener`，默认空、零行为变化）；
  通知点：`Attach`（含 AddComponentByName）/ `AddComponent<T>`（挂载）、`RemoveComponentByName` /
  `RemoveComponent<T>` / `~GameObject` / `OnDestroy`（移除）。事件驱动，无轮询、无 shadow registry。
- **变更钩子（Physics-free）**：`ColliderComponent` 新增 `ColliderMutationHook`（std::function +
  稳定属性名），直接 setter 与反射 `SetPropertyValue` 成功后触发；组件**零 Physics 依赖不变**。
- **Adapter 集成 API**：`ObserveGameObject/UnobserveGameObject` —— 监听 Collider 的 Add/Remove/
  属性变更：Add→Bind / Remove→Unbind（并清受管集合，已删除 Collider 绝不复活）/ enabled→即时
  SetActive（唯一实时通道）/ 其余属性→自动 Rebuild（B2-4 契约正式挂到组件生命周期）。
- **测试**：`ColliderBindingTest.cpp` +9（test_physics 28→**37/37**）：AutoBind / AutoUnbind+
  不复活 / Mutation 自动 Rebuild 无泄漏（BodyCount 恒 1）/ B4-4 顺序矩阵 CaseA-D（Remove 先于毁世、
  毁世后 Remove、毁世重建后 Remove、跨世界两次 mutate 用最后状态）/ B4-5 负路径矩阵 + SetWorld(nullptr)
  可恢复。
- **回归**：GameObject 改动后**全 9 套测试全绿**（test_bridge 28 / test_scripting 27 / test_content 42 /
  test_gp01 22 / test_core 20 / test_e2e 6 / test_job 4 / test_renderer 9 / test_physics 37；实际合计 195）。
- **Result A 核对**：GameObject lifecycle → Collider lifecycle → Adapter lifecycle → Body lifecycle
  全程自动、确定性、可恢复。**无 Result C 触发**（GameObject 生命周期本就存在 OnCreate/OnDestroy，
  缺口仅为统一通知机制，已按 Result B 预案以最小 Hook 补齐）。
- **登记观察项（非阻塞）**：① `Component::SetEnabled`（基类，非虚）不触发变更钩子——enabled 变更
  走反射路径或显式 API；② `~GameObject`/`OnDestroy` 可能对同一组件双通知（Unbind 幂等，安全）；
  ③ Adapter 必须先于被观察 GameObject 销毁或先 Unobserve（观察者生命周期纪律）。
- **状态**：✅（B4 完成，晋升 → **B5 Cold Restart / SaveLoad 集成**：场景落盘→冷启→Collider 还原→
  World 创建→RebuildAll→模拟可用，并断言无 Body 状态入盘）。

> **两套数字（更新）**：Actual = test_physics **37/37**（F2ColliderPhysics 21）+ 其余 8 套全绿
> （实际合计 195）；Declared baseline = **174**（I2 预期红灯，收口时统一更新）。

### EF-F2B-007 · B5 Cold Restart / SaveLoad 集成完成 —— 裁决 **Result A（序列化状态是唯一事实源）**

- **真实生命周期（非 ReleaseWorld/SetWorld 捷径）**：测试走完整产品链路 —— `CaptureScene →
  SaveSnapshotToFile → 磁盘 → LoadSnapshotFromFile → InstantiateScene（全新 Scene + 全新
  GameObject + components[] 还原）→ SetWorld + ObserveGameObject（组件生命周期自动 Bind）`。
- **测试**：`ColliderBindingTest.cpp` +6（test_physics 37→**43/43**）：
  - Golden Case：2 实体全字段（shape/radius/halfX/halfY/sensor/category/mask/enabled）落盘→毁世→
    冷启→BodyCount==ComponentCount==2，逐字段 Physics 层核对；**落盘证据断言无 BodyID/ShapeRef/
    runtimeBodyID**。
  - `SaveIsSnapshot_NotRuntimeState`：radius=1 保存→mutate 2（不保存）→冷启=1；再保存→冷启=2。
    **正式确立「Save 即快照」：Serialized Component State = Cold-Restart source of truth**。
  - 多实体（Circle / Box Sensor / Box Disabled）全恢复，3 Components→3 Bodies，0 duplicate/missing。
  - `RemovedCollider_NotResurrected`：Remove 未 Save → 冷启恢复快照（A+B）；Remove 后 Save →
    只恢复剩余（A）。已删除 Collider 绝不复活。
  - 负路径：无 Collider 场景（实体存活）/ 未知组件类型（warning + 实体照常，F1 原则）/ 组件数据
    损坏（默认值 + 实体存活 + 可绑定）/ Load 时无 World（组件受管，World 晚到 RebuildAll 可恢复）。
- **契约升级**：`Component = runtime source of truth` → **`Serialized Component State = cold-restart
  source of truth`**。磁盘保存的永远是 components[]（ColliderComponent 声明数据），Physics Body
  状态从不入盘。
- **Result A 核对**：Serialized Component → GameObject Component → Adapter → Body 全链成立；多实体、
  删除、未知类型、失败路径全过。无 Result B/C。
- **状态**：✅（B5 完成，晋升 → **B6 Physics Behavior Consistency**：不仅证明 Body 重建，还证明
  重建后实际物理行为与未经历 World destruction 的运行一致）。

> **两套数字（更新）**：Actual = test_physics **43/43**（F2ColliderPhysics 27）+ 其余 8 套全绿
> （实际合计 201）；Declared baseline = **174**（I2 预期红灯，收口时统一更新）。
### EF-F2B-008 · B6 前置冻结 + 观测机制选型

- **冻结基线**：B5 43/43 · `Serialized Component State = Cold-Restart source of truth` ·
  `Save = snapshot` · Body/Shape/BodyID 不入盘 · Load 无 World → 后到 RebuildAll 可恢复。
  **不更新 174 baseline**（收口时统一）。
- **B6 核心问题**：恢复出来的 Static Collider 在真实 Physics 层的**行为**是否与从未冷启动的
  连续运行等价（不再只证「字段恢复」）。
- **观测机制选型（关键决策）**：Collider 固定 Static，velocity/position 运动 observable 本不可得
  （RigidBody F2 红线）→ 选**动态探针真交互**：经 `Box2DPhysicsWorld::CreateBody` 直接投一枚
  Dynamic 小圆，落于静态 collider 上。探针是否被挡 / 落点高度 / 是否穿透由 collider 的
  shape / sensor / enabled / category / mask 驱动 —— 即对**物理行为**的直接观测，而非字段回声。
  A/B 对拍 trace（t=0..2s @ dt=1/60，显式 epsilon 2e-3）。
- **传感器 / filter 的真交互证明纪律**：必须证明 Physics 层真正使用 sensor/category/mask（探针
  穿透 vs 阻挡），不得只读字段满足；现有 Box2D 路径已够（无需新增 Collision Event API）。
- **状态**：✅（冻结 + 机制选定，进入 B6 实现）。

### EF-F2B-009 · B6 Physics Behavior Consistency 完成 —— 裁决 **Result A（行为一致）** + 真实缺陷修复

- **测试**：`ColliderBindingTest.cpp` +11（test_physics 43→**54/54**），`B6_*`：
  - B6-1 A/B trace 对拍（Circle）：探针轨迹逐帧等价 + representation 对齐（pos/radius/sensor/filter/active）。
  - B6-2 Circle / Box 双路径：两个 ShapeDef 分支行为等价 + 形状参数回读一致。
  - B6-3 Sensor：sensor collider → 探针**穿透**（不产生实体响应）；normal → 阻挡；冷重启后行为一致。
  - B6-4 Filter：collider cat/mask=0x0002 只与 Player 碰撞；探针 0x0002→被挡 / 0x0004→穿透（几何仍重叠）
    —— 证明 Physics **实际应用**恢复的 category/mask，非字段回声。
  - B6-5 Mutation→Restart：radius=2 保存冷启 → 行为==radius=2；enabled=false 保存冷启 → Body 保持
    disabled（探针穿透）。
  - B6-6 三路对拍：A 连续 / B 中途 Destroy→Rebuild / C 冷重启 → A≈B≈C。
  - B6-7 稳定性：Cold Restart ×10 → BodyCount 恒 1、m_Tracked 恒 1、trace 不漂移、无 duplicate/ghost。
  - B6-8 负路径：非法 radius/half≤0 干净失败（不 crash / 无 ghost，Bind 前几何校验）；World 销毁后
    查询旧 body 安全；重复 Observe 幂等。
- **B6 暴露并修复的真实缺陷（重要架构发现）**：`Box2DPhysicsBody::ClearFixtures()` 原只清
  `m_Shapes`（AddFixture 追踪的 fixture），**不销毁 CreateBody 经 CreateShapesFromBodyDef 生成的
  初始实心 shape**。Adapter 的 GAP-1 规避（clear + 重加 sensor/filter）因之残留一个实心 shape →
  sensor=1 的新 shape 与初始实心 shape 并存，后者照常参与碰撞 → **sensor/filter 行为在 Physics 层
  实际失效**（读字段全对，B2-B5 因只读 FirstShape=新 shape 链首而漏检）。已修复：`ClearFixtures()`
  现经 `b2Body_GetShapeCount/GetShapes/b2DestroyShape` 清空 body 上**全部** shape。
  这正是 B6「真交互行为观测」的设计价值 —— B3-4 的 representation 对拍不足以发现本缺陷。
- **Result A 核对**：Cold Restart → Serialized Component → Collider → Adapter → Physics Body →
  Physics Behavior，与连续运行 A/B 对拍一致（shape/sensor/filter/enabled/radius/行为 trace 全对齐）。
  无 Result B 局部缺口；无 Result C（修复局限在 Physics 适配层 ClearFixtures，未动 Collider Contract /
  ownership 模型）。Collision Event / RigidBody / ECS 均未引入（B6 红线守牢）。
- **回归**：全 8 套全绿（test_physics 54 / test_bridge 28 / test_scripting 27 / test_content 42 /
  test_gp01 22 / test_core 20 / test_e2e 6 / test_job 4；实际合计 203）。
- **状态**：✅（B6 完成，晋升 → **B7 双模型边界 / ECS 隔离**）。

> **两套数字（更新）**：Actual = test_physics **54/54**（F2ColliderPhysics 38）+ 其余 8 套全绿
> （实际合计 203）；Declared baseline = **174**（I2 预期红灯，收口时统一更新）。
