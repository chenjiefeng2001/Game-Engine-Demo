# Engine Foundation v1

> 建立日期：2026-08-28 · 上游：AV-G3 Result A（Phase 3 FROZEN），AV-Dogfood-01 暂缓
> 目的：把引擎从“能生产一个游戏”建设为“未来项目可依赖的产品级基础”。

---

## 1. 为什么现在进入 Foundation，而不是 AV-Dogfood-01

AV-G3 证明的是 **Avalonia Editor 能完成一条完整生产链**（open → 作者 → 调脚本 →
运行/热重载(_PERSIST) → 落盘 → 关/开 → 复现），但这**没有证明引擎组件模型足够完整**。

AV-G3 路线是**纵向验证**（Content→Scene→Script→Gameplay→Save→Restart 已验很多遍）。
现在缺的是**横向完整性**：`Gameplay / Physics / Presentation` 三条线的每个能力
能不能“作者 → 存储 → 脚本 → 还原”。因此 F0 采取 **审计先行、零引擎改动**。

## 2. F0 审计结论（摘要）

完整证据见 `docs/Component-Maturity-Matrix.md` 与 `docs/Component-Ledger.md`。

- **最尖锐缺口**：内容序列化只覆盖 `{Name, Position, SpriteGUID, ScriptGUID}`
  （`SceneSerializerV1.h`）。引擎里跑得动的能力一旦 Open/Save 就全部丢失。
- **架构分裂**：两个并行组件模型（Go-based `GameObject/Component` vs 数据导向 ECS），
  Editor/内容管线只用前者，3D 物理走后者。
- **不存在**：Camera / Tilemap / Prefab / Game UI 的组件实体；Lua 无 sprite/physics/
  audio 域；实体引用靠 name + per-session ID，无稳定 Entity GUID。

## 3. 核心架构问题（F1 先裁决）

目标契约：

```text
Entity
  └── Component[]
          │
          ├── Runtime
          ├── Serialization
          ├── Scripting
          └── Editor / Avalonia
```

现状：这套“单一 Entity → 组件列表，四面向统一”的模型**并不成立**：

| 面向 | GameObject 模型 | ECS 模型 |
|------|-----------------|----------|
| Runtime | `AddComponent<T>`（typeid 去重） | `EntityManager::AddComponent`（Archetype） |
| Serialization | Component 有虚 `Serialize/Deserialize`，但 Content 快照不调用 | 无快照路径 |
| Scripting | 实体→name/句柄；Lua 仅 log/time/input/entity/transform | 无脚本绑定 |
| Editor/Avalonia | bridge 实体 CRUD + sprite/script | 无 |

**裁决问题（F1）**：
- (a) 收敛到单一 GameObject/Component 契约，并把 ECS 的 3D 物理数据以“内部实现”方式
  挂到该契约下？或
- (b) 以 ECS 为底层，GameObject 作为其上的一层 API？或
- (c) 桥接：维持两模型，但为“可作者组件”定义一套统一反射 + 序列化元数据，两模型共享。

F0 不做选择，只把问题显式化；F1 依据矩阵给裁定。

## 4. Foundation 范围红线

- **F0**：零引擎功能改动，仅文档（本阶段即是）。
- **不新增**无 Authoring/Serialization 支撑的孤立组件。
- **串行定义**：Serialization / Component 契约先于具体组件补齐，避免各组件各写一套。

## 5. 产出物索引（F0）

| 文件 | 内容 |
|------|------|
| `docs/Component-Maturity-Matrix.md` | 全组件 × 9 维度矩阵 + 红线 |
| `docs/Component-Ledger.md` | 逐项证据台账（本清单的细节） |
| `docs/Engine-Foundation-Phase-Plan.md` | F1–F5 阶段与 F1 候选裁定 |
| `docs/Engine-Foundation-v1.md` | 本文件：概述 + 架构问题 |