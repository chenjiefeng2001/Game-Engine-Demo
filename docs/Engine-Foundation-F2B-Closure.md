# Engine Foundation v1 — F2-B Closure（Physics Binding / 生命周期 / 行为一致性）

> 建立日期：2026-08-28 · 上游：`Engine-Foundation-F2B-Plan.md`（B0-B8）
> 基线：F2-A tag `engine-foundation-v1-f2-a-collider`（`f674331`）
> 本文件对应 tag：**`engine-foundation-v1-f2-b-collider`**
> 裁决：**Result A —— Foundation Ready（契约无污染 · 单向映射 · ownership 唯一 · Destroy/Rebuild ·
> Cold Restart · Remove · 行为一致 · 双模型无歧义）**

---

## 0 · 停点声明

F2-B 已完成并冻结。当前不再横向堆功能（不补 RigidBody / Collision Event / ECS / Editor）。
行为证据已足够强，继续写代码只会削弱本阶段证据边界。

```text
F2-A  Contract                         ✅ (f674331)
B1    Ownership Audit                 ✅
B2    Adapter Mapping                 ✅
B3    World Lifecycle                 ✅
B4    Component Lifecycle             ✅
B5    Cold Restart / Persistence      ✅
B6    Behavior Consistency            ✅
B7    双模型边界 / ECS 隔离            → 待 F2-C/D 后裁决
B8    F2-B Closure                    ✅ (本文件)
```

---

## 1 · F2-B Physics Binding Golden Gate（唯一收口门）

以下 8 个硬门由 `tests/test_physics/ColliderBindingTest.cpp`（F2ColliderPhysics 38 项）承载，
门表引用对应 Ledger 条目（`docs/Component-Ledger.md`）。

| Gate        | 必须证明                                                       | 承载                                                                 |
|-------------|----------------------------------------------------------------|----------------------------------------------------------------------|
| Ownership   | Component 不拥有 Body；World 可独立销毁；body 生命周期由 Adapter 唯一管理 | B1 audit · B3-1 `WorldLifecycle_DestroyWorld_RestoreBodyFromComponent` |
| Mapping     | Component → Shape/Filter 无损                                   | B2-1/2-5 `Binding_CircleParamsLandOnPhysics` · `Binding_BoxSensorEnabledLandOnPhysics` |
| Lifecycle   | Add / Remove / Rebuild 无重复、无 dangling                        | B4-1/2/5 `Lifecycle_AddCollider_AutoBind` · `Lifecycle_RemoveCollider_AutoUnbind_NoResurrection` · 顺序矩阵 CaseA-D |
| Mutation    | setter / 反射 → Physics 行为正确                                  | B4-3 `Lifecycle_Mutation_AutoRebuild_NoLeak` · B6-5 `B6_Mutation_*` |
| Persistence | Save → Cold Load → Bind 完整恢复，Save 即快照                      | B5 `ColdRestart_SaveDestroyLoadRebuild_GoldenCase` · `ColdRestart_SaveIsSnapshot_NotRuntimeState` · `ColdRestart_MultiEntity_AllRestored` |
| Behavior    | Continuous ≈ Cold Restart（真交互 trace 对拍）                      | B6-1/6 `B6_Trace_AB_DynamicProbe_Circle` · `B6_ThreeWay_Continuous_MidDestroy_ColdRestart` |
| Negative    | 无 World / 坏数据 / 删除 / 未知类型可恢复，不 crash / 无 ghost        | B3-5 · B5-5a/5b · B6-8 `B6_Negative_*` |
| Stability   | 重复 Cold Restart ×10 / 多实体无累积                               | B6-7 `B6_RepeatColdRestart_x10_NoGrowth_NoDrift_NoGhost` · B5-3 |

---

## 2 · 最终计数与回归

```text
In-scope 合计 = 212（Integrity Gate 校验，I1+I2+I3 全绿）
  test_scripting 27 · test_physics 54 · test_renderer 9 · test_content 42
  test_job 4 · test_e2e 6 · test_gp01 22 · test_bridge 28 · test_core 20
Excluded：test_ecs（旧 EntityManager API，未解除 deferred）
```

`docs/Evidence-Baseline.md` 已按上表冻结。后续任何改动需先过 Gate。

---

## 3 · 核心证据链（F2-B 全链）

```text
Serialized Component State（components[]）
   ↓ CaptureScene / SaveSnapshotToFile / LoadSnapshotFromFile / InstantiateScene
GameObject + ColliderComponent（声明事实源，零物理引用）
   ↓ ObserveGameObject（生命周期自动跟随）
PhysicsColliderAdapter（唯一映射层：observer→body）
   ↓ BuildBodyDef + ClearFixtures + AddFixture
ShapeDef / BodyDef（纯数据）
   ↓ Box2DPhysicsWorld
Physics Body（Static，可 Destroy/Rebuild）
   ↓ box2d Step
Physics Behavior（被动态探针直接观测）
```

三路对拍：**A 连续运行 ≈ B 中途 Destroy→Rebuild ≈ C Cold Restart** —— 行为一致，
证明恢复出来的 Body 与从未冷启动的 Body 行为等价（B6-6）。

---

## 4 · 关键架构发现 & 升格为 Foundation 原则

B6 暴露了一个只靠「字段正确」永远发现不了的缺陷：

> `Box2DPhysicsBody::ClearFixtures()` 原只清 `m_Shapes`（AddFixture 追踪的 fixture），
> **不销毁** `CreateBody` 经 `CreateShapesFromBodyDef` 生成的初始实心 shape。Adapter 的
> GAP-1 规避（clear + 重加 sensor/filter）因此残留一个实心 shape，与 sensor shape 并存，
> sensor/filter 行为在 Physics 层实际失效（读字段全对）。已修复：`ClearFixtures()` 现清空
> body 上全部 shape（`b2Body_GetShapeCount/GetShapes/b2DestroyShape`）。

**据此升格为 Foundation v1 统一晋升原则（适用于 F2-C/F2-D 及所有 Physics Component）：**

> **任何 Physics Component 的“正确性”必须至少包含一次真实物理行为验证；仅验证 serialized
> fields / first shape / runtime metadata 不足以构成正确性证据。**

- 本原则已具化为 B6 的动态探针真交互对拍（sensor 穿透 vs 阻挡、filter 实际应用）与 B6-6 三路对拍。
- 后续 RigidBody / 其他组件 Golden Gate 应沿用同一纪律。

---

## 5 · F2-B 明确范围边界（红线守牢）

- ✅ 已交付：Collider Contract（F2-A）→ Adapter → Lifecycle Hook → Mutation Hook →
  Cold Restart → Behavior 一致性 → ClearFixtures 缺陷修复。
- ✅ 明确不做（本阶段保持登记于 Ledger / 后续阶段）：Prefab · RigidBody · Collision Event API ·
  Editor Inspector（F2-E）· UI · RenderGraph · 扩 Circle/Box 之外 shape · 为测试改 Gameplay ·
  把 Physics 内部类型暴露进 Contract · **大规模 ECS 整合**。

**ECS 现在仍未引入**（F0 双模型分裂已确认）。GameObject → Component → Adapter → Box2D 路径
已独立成立；是否需要桥接 ECS 的裁决点后移至 F2-B/C/D 完成之后。

---

## 6 · 冻结产物

- code tag：`engine-foundation-v1-f2-b-collider`
- `docs/Engine-Foundation-F2B-Plan.md`（B0-B8 全状态）
- `docs/Component-Ledger.md` §F2 Ledger（EF-F2B-001..009）
- `docs/Evidence-Baseline.md`（in-scope = 212）
- `tests/test_physics/ColliderBindingTest.cpp`（F2ColliderPhysics 38 项，B2-B6 全链）

> 后续 RigidBody（F2-C）将以本 tag 为基线，精确证明是在已通过
> **Destroy/Rebuild + Cold Restart + Behavior** 三门的 Collider 基础上新增能力。