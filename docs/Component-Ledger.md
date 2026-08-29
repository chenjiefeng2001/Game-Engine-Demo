# Component Ledger — F0 Audit

> 建立日期：2026-08-28 · 阶段：Engine Foundation v1 / F0
> 逐项资产台账。每项给出：运行时模型、序列化身份、脚本可达性、生命周期、
> 引用身份、现有证据。F0 只登记不修改。

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
- `docs/Integrity-Report` / Evidence-Baseline — I1+I2+I3, 163 tests