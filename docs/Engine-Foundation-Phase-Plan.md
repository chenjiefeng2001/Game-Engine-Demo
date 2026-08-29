# Engine Foundation Phase Plan

> 建立日期：2026-08-28 · F0 完成后据此裁定 F1 并逐阶段推进。
> **2026-08-28 F1 状态：已完成（Camera 实证全通，见 §2 F1）。**
> **2026-08-28 F2 状态：F2-0 Baseline Freeze 已完成（Gate ALL GREEN 169）；
> F2-A Collider Contract 已完成（声明式上卷，test_bridge 28 / test_scripting 27，baseline→174；见
> `Engine-Foundation-F2-Charter.md`）；F2-B Physics Binding 待启动。**

---

## 1. 总览

```text
Foundation Phase
│
├── F0 Component Completeness Audit          ✅ 本阶段（零引擎改动）
├── F1 Entity / Component Contract            ✅ Camera 实证全链打通（本阶段）
├── F2 Essential Runtime Components           ◀ 下一步（Camera ✅，Collider/RigidBody/Prefab 待办）
├── F3 Serialization + Runtime Lifecycle
├── F4 Avalonia Component Authoring
└── F5 Foundation Golden Gate
```

## 2. 阶段定义

### F0 — Component Completeness Audit（已完成）
- 产出：`Component-Maturity-Matrix.md`、`Component-Ledger.md`、
  `Engine-Foundation-v1.md`、本计划。
- 零引擎改动。

### F1 — Entity / Component Contract（已完成 ✅）
- **裁决** `Engine-Foundation-v1.md` §3：采用**候选 1（推荐）**——收敛到单一
  GameObject/Component 契约（ECS 3D 物理日后以“内部实现”方式挂入，不在本阶段处理），
  以 **Camera 为实证对象**打通四面向。
- **契约落地**：`Engine/Core/GameObject/Component.h` 增虚 `GetComponentTypeName`
  + 反射元数据（`ComponentValueType`、`ComponentPropertyDesc/Value`、
  `GetPropertyCount/GetPropertyDesc/GetPropertyValue/SetPropertyValue`）；
  `ComponentRegistryGo`：稳定类型名 → 工厂（`Register/IsRegistered/Create/Count/EnsureRegistered`）；
  `GameObject::{Add/Get/Has/Remove}ComponentByName + Attach`（经注册表，非 typeid）。
- **Camera 实证** `CameraComponent`（zoom/viewport/bounds/enabled，`MakeOrthographic` 构造实际相机）
  + `RegisterCameraComponent`：**Runtime + Serialization + Lua + Editor/Avalonia 四线全通**，
  含契约测试（test_bridge F1×4、test_scripting F1E/F1C，全部 PASS）。
- **序列化推广**：`SceneSerializerV1` 快照新增 `components[]`
  （type=稳定名 + data=Component::Serialize），未知类型告警 + 实体保留。
- 产出物索引见 `docs/Component-Maturity-Matrix.md`（Camera 行全 ✅）与
  `docs/Component-Ledger.md`（§4 Camera）。

### F2 — Essential Runtime Components
- 按矩阵缺口补齐运行时组件，**以 Camera 为实证对象**（体量最小、缺口最明确、
  一次性检验契约+反射+序列化+Editor 四条线）：
  1. **Camera**（缺到尽头的组件 → 定义“哪个实体决定视口”）
  2. Collider / RigidBody / Physics Material 的**组件化**（把 `BodyDef::shape` 独构成形）
  3. Prefab（资源结构 + 实例化）
- 每个组件必须：可创建/销毁、可启停、可经脚本访问。

### F3 — Serialization + Runtime Lifecycle
- 以 F2 的组件覆盖为基础，扩展内容快照：从 `{name,pos,sprite,script}` 推广为
  **组件化完整快照**（组件→GUID 参数表）。
- 统一 GameObject 与 ECS 的 Reload/Restart 契约；定义
  **authored vs runtime 状态分离标记**（解决现在“运行态 spawn 不保存”仅靠约定的问题）。
- Save/Load 契约测试：所有已验组件都能往返。

### F4 — Avalonia Component Authoring
- per-component inspector（Add/Remove/配置参数）经 bridge ABI 暴露；
  修复 AV-GP-101~104 的 UX 摩擦（资产按 GUID/Path 持有选择态、Reload 状态徽标、
  双选中分配、运行时探针转通用读）。
- Camera 等多视角组件有作者 UI。

### F5 — Foundation Golden Gate
- 门禁：`Component-Maturity-Matrix` 中标记为 🟡 的关键项（Serialization/Script/Lifecycle/
  Avalonia 维度）全部转 ✅；新增组件加入金门契约测试。

---

## 3. F1 候选裁定（供评审选择）

F0 矩阵清楚显示：**横向完整性的第一优先是“组件契约 + 序列化”，而不是单个业务组件**
（因为绝大多数组件在 Runtime 维度已成立，缺口集中在 Serialization/Script/Editor）。

### 候选 1（推荐）：先做 Component Contract + Camera 实证
- 把 Camera 做成第一个“四面向全通”的组件，一次性打通
  契约→反射→序列化→脚本→Editor/Avalonia→金门 的模板。
- 之后其余组件按同一模板批量补齐，成本最低、口径最统一。

### 候选 2：先做 Content Serialization 推广
- 纯粹先解决“能力不落盘”的最大短板，暂不动组件化。
- 风险：没有统一契约时，相机/物理各自写序列化，容易出现多套口径（回到现状）。

### 候选 3：先统一 GameObject 与 ECS
- 纯架构收敛，周期长、会阻塞可见进度。
- 建议推迟：除非评审明确判断双模型在 F2/F3 会成为硬障碍。

---

## 4. 纪律（沿用既有）

- 每阶段结束以 gate 冻结，产出证据；重复项记录为新 Ledger 摩擦（遵 AV 惯例）。
- F1 起**允许引擎改动**，但每次改动必须伴随：内容快照更新 + 脚本 API + bridge ABI +
  Avalonia UI + Contract Test，五线同步，避免回到“能力在、存不进”的状态。
- F0 零引擎改动纪律已遵守：本阶段仅新增 4 个文档，无引擎/编辑器功能变更。