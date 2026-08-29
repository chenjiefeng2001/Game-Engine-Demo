# Component Maturity Matrix — F0 Audit > 建立日期：2026-08-28 · 阶段：Engine Foundation v1 / F0（基线）→ **F1（Camera 实证，已更新）**
> 本文件是 **Engine Foundation v1** 的基础审计产物（F0 零引擎改动，仅文档）。
> **F1 更新**：以 Camera 为实证，“组件契约 + 反射 + 序列化 + Lua + Editor/Avalonia”五线已
> 打通（见 §5 与 `Engine-Foundation-Phase-Plan.md`），Camera 成为矩阵中第一个“全 ✅”行。
> 证据源：引擎源码（`engine/include`、`engine/src`）、Editor Bridge ABI
> （`bridge/src/capi.cpp`、`editor_avalonia/Native/EditorBridgeApi.cs`、
> `editor_avalonia/Services/EditorHostService.cs`）、内容序列化
> （`engine/include/Engine/Core/Content/SceneSerializerV1.h`）、Lua API
> （`engine/include/Engine/Scripting/GameplayAPI.h` + `src/Scripting/GameplayAPI.cpp`）、
> AV-G3 冻结证据（`docs/AV-G3-Ledger.md`）。

---

## 0. 审计前置架构结论（必须先读）

引擎当前存在 **两套并行的组件模型**，这是 F0 最重要的输出：

| 模型 | 代码位置 | 用途 / 现状 | 状态 |
|------|----------|-------------|------|
| **GameObject / Component**（结构型，Go-based） | `engine/include/Engine/Core/GameObject/` + `Core/GameObject/` | Sprite / Physics / Mesh / Light / Audio 等挂到 `GameObject`，`AddComponent<T>` 按 `typeid` 去重。被 Editor / 内容管线 / AV-G3 生产链使用。 | ✅ 已验证的生产路径 |
| **ECS**（数据导向，Archetype/Chunk/64-bit generational ID） | `engine/include/Engine/Core/ECS/` | `EntityManager` / `Archetype` / `Chunk` / `EntityCommandBuffer`；含 `RigidBody3DComponent`、`Joint3DComponent`、`PhysicsRuntimeComponent`，通过 `PhysicsSyncSystem` 接 Jolt 3D 物理。 | 🟡 运行时 3D 物理路径，**未接入 Editor / 序列化 / Avalonia** |

架构副作用：**同一“Entity”有两套定义与两套生命周期**；内容快照只认识
GameObject 模型的浮字段子集（见 §2 序列化红线）。这是 F1 必须裁决的统一对象。

---

## 1. 成熟度矩阵

图例：✅ 完整　🟡 部分（有实质能力但有明确缺口）　❌ 缺失　— 不适用/未定义
权威定义见 `docs/Component-Ledger.md`（每项带证据文件）。

| 组件 | Runtime(创建/销毁/启停) | Editor(创建/配置/删除) | Serialization(Save/Load) | Script(Lua 访问) | Lifecycle(Reload/Restart) | Persistence(authored/runtime) | Reference(GUID/Entity 稳定) | Contract Test | Avalonia Authoring UI |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Entity / GameObject | ✅ | ✅ | 🟡 | ✅ | ✅ | 🟡 | 🟡 | ✅ | ✅ |
| Transform | ✅ | ✅ | 🟡 | ✅ | ✅ | 🟡 | ✅ | ✅ | ✅ |
| SpriteRenderer | ✅ | ✅ | ✅ | ❌ | ✅ | 🟡 | ✅ | ✅ | ✅ |
| Camera | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Collider | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | 🟡 | ❌ |
| RigidBody | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | 🟡 | ❌ |
| Physics Material | 🟡 | ❌ | ❌ | ❌ | — | — | ❌ | 🟡 | ❌ |
| Script | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Input | 🟡 | ❌ | — | ✅ | 🟡 | — | — | ✅ | ❌ |
| Audio | 🟡 | ❌ | ❌ | ❌ | 🟡 | — | ❌ | 🟡 | ❌ |
| Animation | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | 🟡 | ❌ |
| Particle | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | 🟡 | ❌ |
| Game UI | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | ❌ | ❌ |
| Tilemap | ❌ | ❌ | ❌ | ❌ | ❌ | — | ❌ | ❌ | ❌ |
| Prefab | 🟡 | ❌ | ❌ | ❌ | ❌ | — | ❌ | ❌ | ❌ |
| Scene | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 | ✅ | ✅ |
| Resource | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Runtime Spawn/Destroy | ✅ | ✅ | — | ✅ | ✅ | 🟡 | 🟡 | ✅ | ✅ |

> 补充（3D 已存在但不在上面清单内）：Mesh、MeshRenderer、Light、Skinning（3D）均以
> Component 形态存在于 GameObject 模型（`MeshComponent`、`MeshRendererComponent`、
> `LightComponent`、`SkinningComponent`），但**没有 Editor / Avalonia / 内容序列化覆盖**。

---

## 2. 关键红线：序列化只覆盖一个很窄子集

`SceneSerializerV1.h` 给出的内容快照模型为：

```text
Entity = { Name, Position(px,py,pz), SpriteAssetGUID?, ScriptAssetGUID? }
```

即 **内容 Save/Load 只持久化 名称 + 平移 + Sprite GUID + Script GUID**。这直接导致：

- 层级（children）、旋转、缩放、「多组件」**不会持久化**
- Physics / Collider / RigidBody / Mesh / Light / Audio / Animation / Camera / UI / Tilemap /
  Prefab **在引擎里存在的能力，内容管线上全部丢失**
- 引擎组件类（如 `PhysicsComponent`）**已经有 `Serialize`/`Deserialize` 虚方法**，但
  `SceneSerializerV1` 根本不会调用它们 —— 序列化路径与组件反射是脱节的

> 结论：**横向完整性的核心缺口不是“运行时能否做”，而是“作者改的东西能否存下来并还原”。**
> 绝大多数组件在 `Runtime` 维度有真实实现，却在 `Serialization` 维度全军覆没。

---

## 3. 按能力域聚合判断

```
                    Engine Foundation
                          │
        ┌──────────────────┼──────────────────┐
        ↓                  ↓                  ↓
     Gameplay           Physics            Presentation
        │                  │                  │
     Script ✅          Collider 🟡         Sprite ✅
     Input  🟡          RigidBody 🟡        Camera ✅  ← F1 全通（首个四面向实证）
        │                  │                  │
        └──────────────────┼──────────────────┘
                           ↓
                 Scene / Resource
                        ✅
                           ↓
                       Editor ✅
                           ↓
                      Avalonia ✅
```

- **Gameplay / Presentation 的“可作者→可运行”链路已成立**（AV-G3 Result A 验证）；
- **Physics / Presentation 的 Serialization 完全缺失**，`Camera`、`Tilemap`、`Prefab`、
  `Game UI` 等横向能力在“组件化+可持久化”意义上**不存在**；
- 缺 `Camera` 意味着“哪个/哪些实体决定视口”没有任何可存储的表达 —— 这是未来项目
  **最不可接受**的单点缺口（多场景/多视角项目无从谈起）。

---

## 4. 逐维摘要（供 Phase Plan 设门用）

| 维度 | 现状 | 风险 |
|------|------|------|
| Runtime | 大部分组件有真实实现；**Camera 已有契约组件 `CameraComponent`（MakeOrthographic 应用 zoom/viewport/bounds）** | Tilemap/Prefab/Game UI 仍无组件实体 |
| Serialization | **F1 起 `SceneSerializerV1` 快照新增 `components[]`**：契约组件(type=稳定名,data=Component::Serialize)走组件化往返 | 非契约组件的参数（物理/光源等）仍不落盘 |
| Script(Lua) | **F1 新增 `Engine.component.*` 域**（add/get/has/remove + get/set number/bool/string，经反射元数据读写） | sprite/physics/audio/animation 仍无专属组件 API |
| Lifecycle | GameObject 模型 Reload/_PERSIST 已验；合约组件 反序列化→Attach 还原已通 | ECS 模型无 Reload 契约 |
| Editor / Avalonia | **F1 新增 per-component inspector**：bridge `AddComponent/RemoveComponent/GetComponentProperty/SetComponentProperty` + Avalonia Camera 面板 | 仅 Camera 有作者 UI；其余组件无 add-remove |
| Reference | 资产用 GUID（✅）；**契约组件用稳定字符串类型名（ComponentRegistryGo）** | 实体的跨会话稳定 Entity GUID 仍未建 |
| Test | test_bridge **25/25** + test_scripting **25/25** + test_core 20/20 + test_content 42/42 + test_gp01 22/22 + AV gate | 缺 Tilemap/Prefab/Physics-serialization 契约测试 |

---

## 5. 明确裁定（供评审引用）

1. **不要继续“往上堆” Audio / Prefab 等孤立组件**，除非先统一 Entity/Component 契约 ——
   否则会在两套模型上各实现一遍、序列化继续脱节（对应架构问题见
   `docs/Engine-Foundation-v1.md` §3）。
2. **Serialization 是横切最大短板**：先定义“组件如何进快照、如何还原”，比新增任何单个组件
   都优先 —— 已由 F1 落地（`components[]` 组件化快照）。
3. **Camera 已作为 F1/F2 的实证对象完成**：一次性打通
   “组件契约 + 反射 + 序列化 + Lua + Editor/Avalonia + 契约测试”全链，
   成为后续组件批量补齐的模板（见 `Engine-Foundation-Phase-Plan.md`）。