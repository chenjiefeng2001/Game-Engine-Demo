# F2-S1 — Collider Serialization Evidence & Closure · Gate 0 Scan

**状态**：`GATE 0 — COMPLETE — READ-ONLY`
**日期**：2026-10-31
**范围**：只读核对 Collider 八字段序列化、冷启恢复、物理重建/行为证据、Bridge 证据、
T-1 共存证据、Managed/原生生产路径边界与 Release 基线。
**本轮未做**：未修改源码/测试/构建配置；未运行任何测试；未 commit / push / tag / merge；
未清理脏子模块或 `crashes/`。

---

## 1. 当前基线

| 项目 | 实际值 |
|---|---|
| 分支 | `avalonia` |
| HEAD | `c1f7933 feat(editor): land UI-H2 hierarchy and T-6 console UI` |
| 远端跟踪 | `origin/avalonia`，ahead 11 |
| 工作区 | `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` 为脏子模块；`crashes/` 未跟踪 |
| Collider 相关文件 | 自 `d3adebb` 至 HEAD 无改动（见 §3.4） |

本报告是 Gate 0 的唯一新增文件/写入产物；未改动任何既有源码、测试、构建或 Git 状态。
本轮所有测试结论均来自既有记录，不是 Gate 0 新增运行。Gate 0 不把记录中的
通过数当作本轮重跑结果。

---

## 2. F2-S1 的定义与候选边界

按 `docs/Roadmap-Decision-Record-2026-10-28.md` 与
`docs/F2-S1-Scope-and-Scheduling-Review.md`：

- `F2-S1` 是 **Collider Serialization 独立交付键**，不是 `F2-C` 的延续，也不自动落入 F3；
- `F2-C` 是 RigidBody Contract；T-1-B 是 RigidBody 三字段序列化，二者不因 F2-S1 追溯改动；
- F2-B 的 B5/B6 已提供 Cold Restart 与行为一致性证据；F2-S1 开工前先做 gap analysis，
  已覆盖部分不重复计新增；
- 当前证据不支持“F2-S1 阻塞全部 P1”。

Gate 0 采用的候选验收边界（待 Gate A 裁决，不是已冻结契约）：

1. `ColliderComponent` 八字段的 `components[]` 持久化与反序列化；
2. 真实 `SceneSerializerV1` Save → 磁盘 → 冷启 → `InstantiateScene` → Adapter 重建；
3. 重建后的真实物理行为与连续运行一致；
4. Bridge `SaveProject` / `OpenProject` 的字段持久化。

明确不在候选边界内、除非 Gate A 另行裁决：

- Collider schema 扩展（friction/restitution 等新字段）；
- Managed UI 的 Collider 作者入口与全字段 Inspector 验收；
- 生产 `EditorSession::Play` 的物理行为作为 F2-S1 验收；
- ECS/Jolt 3D Collider、Prefab、Collision Event API。

---

## 3. 源码事实

### 3.1 组件契约与八字段

`ColliderComponent` 声明八个声明式属性（`engine/include/Engine/Core/GameObject/ColliderComponent.h:98-104`）：

| 字段 | 类型 | 默认值 | 反射属性 |
|---|---|---|---|
| `enabled` | bool | `true`（`Component` 基类，`Component.h:92-93,142`） | 索引 0 |
| `shape` | `ColliderShape`（`Circle`/`Box`） | `Circle` | 索引 1 |
| `radius` | float | `0.5` | 索引 2 |
| `halfX` | float | `0.5` | 索引 3 |
| `halfY` | float | `0.5` | 索引 4 |
| `isSensor` | bool | `false` | 索引 5 |
| `category` | uint16 | `0x0001` | 索引 6 |
| `mask` | uint16 | `0xFFFF` | 索引 7 |

组件的持久化身份为 `GetComponentTypeName() == "Collider"`（`ColliderComponent.h:48-50`）。

### 3.2 Serialize

`ColliderComponent::Serialize` 写入全部八键
（`engine/src/Core/GameObject/ColliderComponent.cpp:123-133`）：

```json
{
  "enabled": true,
  "shape": "Circle",
  "radius": 0.5,
  "halfX": 0.5,
  "halfY": 0.5,
  "isSensor": false,
  "category": 1,
  "mask": 65535
}
```

不写入任何 Physics 运行时状态：没有 `BodyID` / `bodyID` / `runtimeBodyID` / `ShapeRef`。
B5-1 对落盘文本做了显式反断言（`tests/test_physics/ColliderBindingTest.cpp:975-983`）。

### 3.3 Deserialize

`ColliderComponent::Deserialize`
（`engine/src/Core/GameObject/ColliderComponent.cpp:135-157`）的实际语义：

| 输入情形 | 实际行为 | 返回值 |
|---|---|---|
| `data` 不是对象 | 不修改任何字段 | `false` |
| 字段缺失 | 保留当前/默认值，其他字段正常应用 | `true` |
| 已知字段类型错误（如 `radius: "oops"`） | 静默忽略该字段，其他字段正常应用 | `true` |
| `shape` 为未知字符串（如 `"Triangle"`） | 静默忽略，保留当前/默认 `Circle` | `true` |
| 未知额外键 | 忽略 | `true` |
| 数值字段（`radius`/`halfX`/`halfY`） | 任意 JSON number 直接 `get<float>()`；没有有限值/范围校验 | `true` |
| `category`/`mask` | 任意 number 收窄为 `uint16`；没有范围校验 | `true` |

结论：**Collider 的 Deserialize 是逐字段宽容、非原子的**。已出现的非法已知字段
不会导致整体拒绝，也不会产生 `DataBad` 告警；只有 `data` 不是对象时才返回 `false`。

`enabled` 通过基类 `Component::SetEnabled` 应用；Serialize 通过 `IsEnabled()` 读取。

### 3.4 场景加载链

| 步骤 | 源码 | 事实 |
|---|---|---|
| 捕获组件 | `SceneSerializerV1.cpp:119-146` | `CaptureScene` 对 `GetComponentTypeName()!=nullptr` 的组件调用 `Serialize`，写入 `components[]` |
| 输出 JSON | `SceneSerializerV1.cpp:29-51` | `{ "type": <稳定类型名>, "data": <组件 Serialize 产物> }` |
| 读取 JSON | `SceneSerializerV1.cpp:101-111` | `data` 只有在是对象时才保留，否则 `sc.data` 为空 |
| 冷启实例化 | `SceneSerializerV1.cpp:210-225` | 已知类型 → `Create` → `Deserialize`；`false` 才追加 `DataBad (kept defaults)` 告警，随后照常 `Attach` |
| 物理重建 | `PhysicsColliderAdapter.cpp:59-67,82,96,131-144,204-205,326-335` | 形状/半径/半宽高/sensor/category/mask 进入 BodyDef；`enabled` 控制 `SetActive`；mutation 走 `SetActive` 或 Rebuild |

`CaptureScene → SaveSnapshotToFile → LoadSnapshotFromFile → InstantiateScene` 是 B5 使用的
真实序列化链路，不是测试专用的内存捷径。

### 3.5 源码层已发现的文档不一致

1. `ColdRestart_Negative_NoCollider_UnknownType_CorruptData` 的注释写
   “Deserialize 拒绝”
   （`tests/test_physics/ColliderBindingTest.cpp:1221`），但实际 `Deserialize` 返回
   `true`，只是静默忽略错误 `radius`。该测试只断言组件保留默认值并可绑定
   （`:1238-1247`），没有断言返回值、告警或“拒绝”。
2. `Component-Ledger.md` EF-F2B-007 写 B5 Golden Case 覆盖“全字段
   （shape/radius/halfX/halfY/sensor/category/mask/enabled）”
   （`docs/Component-Ledger.md:341-343`）。实际 `enabled=false` 的冷启证据在
   B5-3 多实体测试（`ColliderBindingTest.cpp:1088-1093,1134`），不在 B5-1
   Golden Case；B5 套件整体覆盖八字段，但“Golden Case 单测覆盖全字段”的措辞不精确。
3. `docs/Evidence-Baseline.md:19` 仍记录 `test_physics` 期望计数 54；当前 T-5 关闭记录
   为 71/71（`docs/T-5-DebugDraw-v1-Final-Closure-Record.md:35`）。两者不一致，需核对
   Integrity Gate 的期望计数；这不是 Collider 代码问题，但 F2-S1 关闭记录不能沿用 54
   作为当前 target 计数。

---

## 4. 证据矩阵

### 4.1 字段 × 层

图例：`Y` = 有直接断言；`P` = 部分覆盖/只覆盖默认值；`N` = 无覆盖。

| 字段 | 引擎序列化/冷启 | 物理重建/行为 | Bridge Save→Reopen | Managed UI | 原生 EditorSession Play |
|---|---|---|---|---|---|
| `enabled` | Y：B5-3 disabled box（`:1088-1093,1134`）；B6-5(b)（`:1636-1658`） | Y：同上，`IsActive=false` + 探针穿透 | P：`F2ColliderContractLifecycle:1564-1566` 只读默认 `true`；SaveLoad 未设 | N | N |
| `shape` | Y：B5-1/B5-2/B5-3；B6-2/B6-5 | Y：Circle/Box 双路径 | Y：Box，`:1625-1626,1658-1661` | P：仅 `shape=Box` 经 Host API，`:45-48,83-84` | P：T-1-F 用 Box，未做全字段回读 |
| `radius` | Y：B5-1 `1.25`（`:951-953,1009,1024`）；B5-2 `1→2`（`:1030-1074`） | Y：B6-1/B6-2/B6-5 | P：只读默认 `0.5`，SaveLoad 未设非默认值 | N | N |
| `halfX` | Y：B5-1 `2.0`（`:959-963,1017`）；B5-3 | Y：B6-2/B6-6 | Y：`4.0`（`:1627-1628,1662-1664`） | N | P：T-1-F 设置但重开后未断言 |
| `halfY` | Y：B5-1 `1.5`；B5-3 | Y：B6-2/B6-6 | Y：`3.0`（`:1629-1630,1665-1668`） | N | P：同上 |
| `isSensor` | Y：B5-1/B5-3；B6-3 | Y：sensor 穿透 vs normal 阻挡 | Y：`true`（`:1631-1632,1669-1672`） | N | N |
| `category` | Y：B5-1 `0x0002`/B5-3 `0x0008` | Y：B6-4 真碰撞/滤除 | Y：`8`（`:1633-1634,1673-1676`） | N | N |
| `mask` | Y：B5-1 `0x0008`/B5-3 `0x0031` | Y：B6-4 | Y：`31`（`:1635-1636,1677-1680`） | N | N |

结论：

- **引擎层八字段全部有冷启证据**，且 `enabled=false` 与 Circle `radius` 都有非默认值；
- **Bridge Save→Reopen 只覆盖六字段**：`shape`、`halfX`、`halfY`、`isSensor`、`category`、`mask`；
  `enabled` 与 Circle `radius` 没有 Bridge Save→Reopen 断言；
- **Managed UI 没有 Collider Inspector 作者入口**；`T1ManagedAcceptanceGate` 通过
  Host 通用组件 API 设置 `shape=Box`，不是真实 Collider UI 控件；
- **原生 EditorSession Play** 使用 Collider，但 T-1-F 只设置 Box 半宽高并验证位置/碰撞，
  不构成 F2-S1 的八字段序列化证据，且属于 T-1 证据，不能替代 F2-S1。

### 4.2 引擎冷启与行为证据明细

`tests/test_physics/ColliderBindingTest.cpp` 当前有 **38 个 `F2ColliderPhysics` 用例**，
其中 B5 六个、B6 十一个：

| 用例 | 证明 |
|---|---|
| `ColdRestart_SaveDestroyLoadRebuild_GoldenCase`（`:946-1026`） | 真实 Save → 销毁 World → 冷启 → 2 实体逐字段重建；落盘无 BodyID/ShapeRef；Circle/Box/radius/half/sensor/category/mask 在 Physics 层核对 |
| `ColdRestart_SaveIsSnapshot_NotRuntimeState`（`:1030-1075`） | Save 即快照；未保存的 runtime mutation 不进入冷启 |
| `ColdRestart_MultiEntity_AllRestored`（`:1079-1137`） | 3 实体（Circle/Box Sensor/Box Disabled）→ 3 Bodies；`enabled=false` 的 `IsActive=false` |
| `ColdRestart_RemovedCollider_NotResurrected`（`:1141-1187`） | 删除未 Save → 冷启恢复快照；删除后 Save → 只恢复剩余组件 |
| `ColdRestart_Negative_NoCollider_UnknownType_CorruptData`（`:1191-1249`） | 无 Collider / 未知类型 / `radius` 字符串；实体存活、默认值可绑定（但该用例不证明“拒绝”） |
| `ColdRestart_Negative_NoWorldAtLoad_Recoverable`（`:1253-1287`） | Load 时无 World；组件受管，World 晚到 `RebuildAll` 可恢复 |
| `B6_Trace_AB_DynamicProbe_Circle`（`:1370-1418`） | 连续 vs 冷启探针 trace 对拍，epsilon `2e-3` |
| `B6_AB_DualPath_CircleAndBox`（`:1422-1481`） | Circle/Box 两 shape 分支行为一致 |
| `B6_Sensor_ProbePassesThrough_ConsistentAfterRestart`（`:1485-1536`） | sensor 穿透 vs normal 阻挡 |
| `B6_Filter_PhysicsActuallyAppliesCategoryMask_AfterRestart`（`:1540-1594`） | category/mask 在 Physics 层真实生效（被挡/穿透），非字段回声 |
| `B6_Mutation_RadiusThenRestart_BehaviorMatches`（`:1598-1632`） | radius=2 冷启行为与连续运行一致 |
| `B6_Mutation_EnabledFalse_ThenRestart_BodyStayDisabled`（`:1636-1658`） | enabled=false 冷启后 Body disabled，探针穿透 |
| `B6_ThreeWay_Continuous_MidDestroy_ColdRestart`（`:1662-1705`） | A 连续 ≈ B 中途 Destroy/Rebuild ≈ C 冷启 |
| `B6_RepeatColdRestart_x10_NoGrowth_NoDrift_NoGhost`（`:1709-1739`） | 10 次冷启 BodyCount/Tracked 恒 1，trace 不漂移 |
| `B6_Negative_InvalidGeometry_CleanFail_NoGhost`（`:1743-1778`） | radius/half extents ≤ 0 干净失败，无 ghost |
| `B6_Negative_WorldDestroyThenQueryBody_NoCrash`（`:1780-1803`） | World 销毁后旧 body 查询安全，可重建 |
| `B6_Negative_RepeatedObserveAfterColdLoad_NoDuplicate`（`:1805-1825`） | 重复 Observe 幂等，无 duplicate |

B5/B6 不是“字段回声”：B6 用独立 Dynamic 探针在真实 Box2D 世界中观测阻挡/穿透，
并且 F2-B 记录了一个由行为对拍才发现的真实缺陷（`ClearFixtures` 未清初始实心 shape），
见 `docs/Component-Ledger.md:391-397`。

### 4.3 Bridge 证据

| 用例 | 覆盖 | 不覆盖 |
|---|---|---|
| `F2ColliderContractLifecycle`（`:1535-1616`） | Host API Add/Set/Get/Remove；Box/halfX/halfY/sensor/category/mask；默认 Circle/radius/enabled 读取 | 非默认 `enabled`、非默认 Circle `radius`、Save/Reopen |
| `F2ColliderSaveLoadColdRestart`（`:1618-1684`） | Add → Set 六字段 → SaveProject → 场景文本含 Collider/shape/halfX/category → 重新 OpenProject → 六字段还原 | `enabled`、Circle `radius`；无物理世界/行为 |
| `F2ColliderNegativePaths`（`:1686-1726`） | Add 未知类型/越界实体/未挂载 Set/坏值/未知属性/重复 Remove 拒绝 | JSON 加载期的非法/部分/缺失字段语义 |

Bridge 的 `SaveProject/OpenProject` 走真实 RevisionStore `current` 路径；T-1-B 已用同一
路径验证 RigidBody，但 Collider 的 Bridge 字段覆盖仍停在六字段。

### 4.4 T-1 共存证据

`tests/test_content/RigidBodySerializationTest.cpp:201-247` 在 RigidBody 冷启场景中
加入 Collider Box/halfX/halfY，并验证 RigidBody 与 Collider 共存、Collider 的
`shape/halfX/halfY` 恢复、以及 `GetPropertyCount()==8`。这是共存证据，不是 Collider
八字段或物理行为证据；按 `docs/T-1-Stage0-Baseline.md:349-350` 不能计入 F2-S1。

### 4.5 Managed UI 与生产路径

- `editor_avalonia/MainWindow.axaml:215-266` 只有 Camera Inspector 与 RigidBody Inspector，
  没有 Collider Inspector；
- `editor_avalonia/Services/T1ManagedAcceptanceGate.cs:45-48` 通过
  `host.AddComponent` / `host.SetComponentProperty` 设置 Collider `shape=Box`；
  `:83-84` 重开后只检查 `shape`；
- `docs/T-1-Managed-UI-Acceptance.md:24-33,48-51` 明确 Managed UI 的 Collider 只作
  schema 对照，物理观测 B 是 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`；
- 生产原生路径已存在：`bridge/src/EditorSession.cpp:1355-1405` 在 Play 时克隆场景并
  建立 `PhysicsRuntime2D`；`tests/test_bridge/test_editor_capi.cpp:1849-1956`
  （T-1-F）覆盖 Collider Box + RigidBody 的 Save/Reopen/Play/Stop/Replay，但属于 T-1，
  不是 F2-S1 的验收证据。

### 4.6 Release 与回归基线

- F2-B 关闭记录没有按 Debug/Release 拆分测试数字
  （`docs/Engine-Foundation-F2B-Closure.md` 未出现 `Debug`/`Release` 配置表）；
- 后续 T-5 关闭记录给出当前可引用的双配置基线：
  `test_physics` **71/71 Debug；71/71 Release**，全量 CTest **19/19 Debug/Release**
  （`docs/T-5-DebugDraw-v1-Final-Closure-Record.md:35,49`）；
- `F2ColliderPhysics` 的 38 个用例包含在 `test_physics` 中；自 `d3adebb` 至 HEAD，
  Collider 组件、SceneSerializerV1、Adapter、ColliderBindingTest、Bridge Collider 测试、
  RigidBodySerializationTest 均无改动（`git diff --stat d3adebb..HEAD -- <上述文件>` 为空）；
- 因此可以把上述 71/71 双配置结果作为**当前源码基线**引用，但必须标注
  “既有记录，不是 Gate 0 重跑”。本轮没有新的 Release 运行。

---

## 5. 缺口登记

| 编号 | 缺口 | 分类 | 影响 | 处置建议 |
|---|---|---|---|---|
| G-1 | 没有 F2-S1 自己的字段 × 层 × 测试矩阵与关闭记录 | 文档缺口 | 证据分散在 F2-B/T-1-B/Bridge，无法独立收口 | Gate A 冻结矩阵格式，Closure 记录引用实际用例 |
| G-2 | Bridge Save→Reopen 未断言 `enabled` 与 Circle `radius` | 证据缺口（小） | Bridge 只证明六字段；引擎层已覆盖两者 | 若 Bridge 属于 F2-S1，则补两个纯测试断言，不改生产代码 |
| G-3 | Collider `Deserialize` 的非法/部分字段语义未冻结，且代码是逐字段宽容、非原子 | 契约/证据缺口 | 与 RigidBody T-1-B 的原子语义不同；损坏 `shape` 会静默回退 `Circle`，可能把 Box 变 Circle | Gate A 明确二选一：冻结宽容语义并补测试/告警，或改为原子拒绝（生产变更需单独授权） |
| G-4 | 无 Collider 的非法/部分 JSON 测试：未知 shape、错误类型、未知键、缺失字段、`data` 非对象、NaN/Inf/超范围 | 证据缺口 | 当前行为没有被测试钉住；B5-5a 只覆盖 `radius` 字符串且未断言返回值/告警 | 与 G-3 一起在 Gate A 决定测试矩阵；建议至少覆盖“已知字段非法”和“部分字段”两类 |
| G-5 | Managed UI 无 Collider 作者入口；仅有 Host API + `shape=Box` schema 对照 | 范围裁决 | 若 F2-S1 要求 Managed 全字段冷启，则构成实现缺口 | 推荐排除出 F2-S1 验收；保留 T-1 managed 证据为相邻证据，不升级为 F2-S1 |
| G-6 | 生产原生 `EditorSession::Play` 只有 T-1-F 的 Box/RigidBody E2E，非八字段 Collider 验收 | 范围裁决 | 若 F2-S1 要求生产 Play 路径，则构成证据缺口 | 推荐不纳入 F2-S1；若纳入，另立任务并明确字段/行为矩阵 |
| G-7 | 文档精度：B5-5a 注释称“拒绝”；EF-F2B-007 称 Golden Case 覆盖 enabled；Evidence-Baseline 的 test_physics 计数 54 与当前 71/71 不一致 | 文档修正 | 会让读者误判现有证据强度 | 在 F2-S1 Closure 中更正措辞；不改历史记录原文，新增勘误说明 |

**未发现**：在“引擎序列化 + 冷启 + 物理行为 + Bridge 六字段”的候选边界内，
没有必须先改生产代码才能推进的硬缺口。

---

## 6. Gate 0 结论

### 6.1 判定

- **引擎层**：`EVIDENCE SUFFICIENT — SCOPE-BOUND CANDIDATE`。八字段、冷启、物理重建、
  行为对拍、负路径、重复冷启均有真实证据。
- **Bridge 层**：`EVIDENCE GAP — SMALL`。缺 `enabled` 与 Circle `radius` 两个字段的
  Save→Reopen 断言；属于测试补强，不是生产实现缺口。
- **语义层**：`CONTRACT DECISION REQUIRED`。Collider 的逐字段宽容/非原子语义尚未冻结，
  与 RigidBody 的原子语义不同；`shape` 静默回退是最需要明确的风险点。
- **Managed/生产路径**：`SCOPE DECISION REQUIRED`。当前没有 Collider Inspector；
  T-1-F 是相邻证据，不能自动算作 F2-S1。
- **实现缺口**：在推荐窄边界下 **NONE**。

### 6.2 建议 Gate A 裁决项

1. **范围**：采用窄边界（引擎八字段 + 冷启 + 行为 + Bridge 六字段），还是把 Bridge
   八字段、Managed UI、生产 Play 一并纳入。
2. **非法/部分 JSON 语义**：
   - 方案 A（推荐用于收口）：冻结当前“逐字段宽容、非原子”语义；补测试钉住行为；
     更正 B5-5a 注释；把 `shape` 静默回退 `Circle` 写成显式已知限制。无需改生产代码。
   - 方案 B：改为原子拒绝，与 RigidBody 对齐；这是生产代码变更，必须单独授权，
     不能混在“仅证据/矩阵”的关闭切片里。
3. **Bridge 补强**：是否补 `enabled=false` 与 Circle `radius` 的 Save→Reopen 断言
   （仅测试）。
4. **Managed/生产路径**：是否排除出 F2-S1 验收；若纳入，另立任务，不把 T-1-F 或
   T-1 managed 证据直接算作 F2-S1。
5. **文档**：F2-S1 Closure 必须包含字段 × 层 × 测试矩阵、G-1～G-7 的处置、
   以及“哪些是 F2-B 既有证据、哪些是 F2-S1 新增证据”的明确分界。

### 6.3 建议的下一步

先完成 Gate A 契约冻结，再决定是否进入一个**只补测试/文档**的 F2-S1 收口切片。
在该切片内不得顺手扩展 Collider schema、修改 SceneSerializerV1 格式或接入新的生产
物理宿主；任何生产语义变更都必须单独裁决。

---

## 7. Gate 0 未做

- 未运行 `test_physics`、`test_bridge`、`test_content` 或全量 CTest；
- 未修改 `ColliderComponent`、`SceneSerializerV1`、`PhysicsColliderAdapter`、
  `PhysicsRuntime2D`、Bridge、`editor_avalonia` 或任何测试；
- 未新增/删除测试，未修改 CMake；
- 未 commit / push / tag / merge；
- 未清理脏子模块、`crashes/` 或任何既有工作区内容。
