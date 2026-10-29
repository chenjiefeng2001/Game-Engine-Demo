# F2-S1 — Collider Serialization 范围与排期评审（只读）

**日期**：2026-10-29
**状态**：`READ-ONLY REVIEW — SCHEDULING DECISION REQUIRED`
**范围**：只读核对 Collider 保存、冷启恢复、物理重建的现有实现与证据；不修改源码/测试；
不运行 F2-S1 测试（本轮未重跑，既有测试只引用不计数）。
**前置**：#141 已 `PASS`；T-1 已 `CLOSED — SCOPE-BOUND`；T-2 仍 `CLOSED — SCOPE-BOUND`。

---

## 1. 编号与边界

- `F2-S1` 是 **Collider Serialization 独立交付键**，不是阶段字母，也不继承 F2-C；
  阶段归属仍按 `docs/Roadmap-Decision-Record-2026-10-28.md` §D-1 留空。
- F2-C = RigidBody Contract；T-1-B = RigidBody 三字段序列化。二者**不因 F2-S1 追溯改动**。
- F2-B 的 B5/B6 已提供 Collider Cold Restart / 行为一致性证据；F2-S1 开工前先做
  gap analysis，已覆盖的部分不重复计为新增证据。
- 本评审不提出新的物理运行时模型，不改 Collider schema，不把“现有测试能恢复”
  直接当作“F2-S1 全部验收完成”。

---

## 2. 现有实现（只读）

| 层 | 事实 | 证据 |
|---|---|---|
| 组件契约 | `ColliderComponent` 拥有 8 个声明式属性：`enabled`、`shape`、`radius`、`halfX`、`halfY`、`isSensor`、`category`、`mask` | `engine/include/Engine/Core/GameObject/ColliderComponent.h:50-93` |
| 序列化 | `Serialize` 写入全部 8 字段；`Deserialize` 读取全部 8 字段 | `engine/src/Core/GameObject/ColliderComponent.cpp:123-162` |
| 场景持久化 | `SceneSerializerV1` 对 `GetComponentTypeName()=="Collider"` 走通用 `components[]` | `engine/src/Core/Content/SceneSerializerV1.cpp`（T-1 Stage 0 已核对） |
| 物理重建 | `PhysicsColliderAdapter` 从 Collider 声明状态构建 Shape/Body；F2-B B5/B6 已覆盖 | `docs/Engine-Foundation-F2B-Closure.md` §1/§3 |
| Bridge | `F2ColliderSaveLoadColdRestart` 保存、重开、逐字段回读 | `tests/test_bridge/test_editor_capi.cpp:1618-1685` |
| 引擎冷启/行为 | B5 Golden Case、B5 多实体/删除/负路径、B6 sensor/filter/mutation/enabled/三路对拍/重复 ×10 | `tests/test_physics/ColliderBindingTest.cpp:946-1850` |
| RigidBody 共存 | T-1-B 冷启测试含 Collider 共存；T-1-F 原生 E2E 用 Collider + RigidBody 保存/重开/Play | `tests/test_content/RigidBodySerializationTest.cpp:200-260`；T-1-F |

结论：**Collider 的序列化与物理冷启并非空白实现**；现有代码已经持久化全部 8 字段，
F2-B 已有真实物理行为对拍。

---

## 3. Gap analysis（按“是否真的缺”分类）

| 项目 | 现状 | 是否构成 F2-S1 缺口 | 备注 |
|---|---|---|---|
| 8 字段序列化 | 全部写入/读取 | **否** | 代码层已完整 |
| 引擎冷启 + 物理行为 | B5 Golden Case 覆盖 Circle/Box、sensor、category/mask；B6 覆盖 enabled/filter/ mutation/三路对拍/重复 ×10 | **否（已在 F2-B）** | 不得重复计新增 |
| Bridge 冷启 | `F2ColliderSaveLoadColdRestart` 检查 shape/halfX/halfY/isSensor/category/mask | **部分** | 未显式检查 `enabled` 与 Circle `radius` 的 Bridge 冷启；但引擎层 B5/B6 已覆盖 |
| 多实体/删除/未知类型/损坏数据 | B5-3/5a/5b、B5-4、B6-8 | **否（已在 F2-B）** | 负路径有真实恢复证据 |
| Managed Inspector 持久化 | T-1 A 仅检查 Collider `shape=Box` 重开后可读 | **可能** | F2-S1 是否要求 managed 全字段冷启，需契约裁决；T-1 A 不声称覆盖全部 8 字段 |
| 非法/部分 JSON 语义 | `ColliderComponent::Deserialize` 对错误类型静默忽略、逐字段应用，非原子 | **待裁决** | RigidBody T-1-B 采用原子校验；Collider 是否要求同样语义，F2-S1 契约需明确 |
| F2-S1 专属证据矩阵 | 现有证据分散在 F2-B/T-1-B/Bridge | **文档缺口** | 缺一份 F2-S1 自己的字段×层×测试矩阵与关闭记录 |
| 真实 managed UI 导入/保存 | #141 只覆盖资产导入；T-1 A 覆盖 Collider shape | **否（不在 F2-S1 范围）** | 不把 #141 当 Collider 序列化证据 |

**当前判断**：F2-S1 的主要剩余工作是**契约/gap 结论与证据矩阵**，可能附带少量
Bridge 字段覆盖测试；没有发现必须先改生产代码才能推进 P1 的硬缺口。

---

## 4. 与 P1 的依赖分析

P1 计划项：

| P1 项 | 是否依赖 Collider 序列化 | 判断 |
|---|---|---|
| T-4 GPUProfiler | 否 | 渲染/计时，不依赖场景 Collider 持久化 |
| T-6 Console | 否 | 日志/诊断，不依赖 Collider 序列化 |
| T-7 Audio EFX / Thread Safety | 否 | 音频，不依赖 Collider 序列化 |
| T-5 Generic DebugDraw | 否（可能消费物理调试数据，但不依赖保存/冷启格式） | 不构成 F2-S1 前置 |

因此：**当前证据不支持“F2-S1 阻塞全部 P1”**。若未来某项 P1 明确消费
Collider 冷启语义，应在该项 Gate A 单独登记依赖，而不是把 F2-S1 变成通用前置。

---

## 5. 排期选项（需要裁决）

### 方案 A：先做 F2-S1 收口切片，再开 T-4

- 范围：冻结 F2-S1 契约（8 字段、非法/部分 JSON 语义、managed 是否在范围）、
  建字段×层×测试矩阵、补必要 Bridge 字段测试（如 `enabled`、Circle `radius`）、
  写 F2-S1 关闭记录。
- 预计：**以测试/文档为主**，不改生产序列化代码，除非契约裁决要求原子语义。
- 优点：先把遗留交付键关闭，P1 无悬而未决的 Collider 语义。
- 代价：推迟 T-4 一个短切片。

### 方案 B：先开 T-4，F2-S1 保持独立未关闭（推荐）

- 范围：按 P1 Gate 0/A 启动 T-4；F2-S1 保留独立待办与验收边界，不并入 T-4。
- 优点：P1 与 Collider 序列化无已知依赖，避免为“可能的未来需要”阻塞核心能力。
- 代价：F2-S1 继续作为未关闭事项存在；需在后续依赖出现时重新排期。

### 方案 C：暂缓 F2-S1，直到出现具体依赖

- 范围：不启动 F2-S1；仅保留台账。
- 适用：确认短期没有 Collider 序列化消费者。
- 风险：如果后续 P1/P2 需要，可能临时插入；不如方案 B 明确。

### 不采用：把 F2-S1 当作所有 P1 的前置

- 与 `Roadmap-Decision-Record` 的“独立交付键/阶段归属留空”冲突，也没有依赖证据。

---

## 6. 推荐

推荐 **方案 B**：

1. F2-S1 现有功能覆盖已由 F2-B/T-1-B/Bridge 证据支撑，未发现必须先改生产代码的缺口；
2. T-4/T-6/T-7/T-5 与 Collider 序列化没有已知依赖；
3. 保持 F2-S1 为独立未关闭项，避免把“遗留交付键”误升级为 P1 的通用前置。

若裁决选择方案 A，则应先冻结 F2-S1 契约（尤其非法/部分 JSON 是否原子、managed 是否
在范围），再决定是否改代码；不得顺手把 F2-S1 扩张成 Collider schema 重构。

---

## 7. 本轮未做

- 未运行 F2-S1 或 F2-B 测试；本文件只引用既有测试代码与关闭记录，不报告新通过数。
- 未修改 Collider 源码、SceneSerializerV1、PhysicsColliderAdapter 或 Bridge。
- 未 commit / push / tag / merge；未触碰 T-2 关闭记录、脏子模块、`crashes/`。
