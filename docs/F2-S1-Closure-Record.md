# F2-S1 — Collider Serialization Evidence & Closure

**状态**：`F2-S1 — CLOSED — SCOPE-BOUND`
**日期**：2026-10-31
**最终关闭**：2026-10-31 用户确认 F2-S1 整体关闭
**基线**：HEAD `c1f7933` + Gate B 工作区变更；Gate B 实现提交 `4f4b955`
**契约**：`docs/F2-S1-GateA-Contract.md`（`PASS — FROZEN — CLOSED`）
**测试记录**：`docs/F2-S1-GateB-Test-Record.md`
**裁决**：用户 2026-10-31 确认 Gate A 全部四项裁决点；Gate B 仅授权测试/文档。

---

## 0. 停点声明

F2-S1 的候选范围已完成证据收集与测试补强。所有 Gate 条件已满足，用户于
2026-10-31 确认整体关闭，F2-S1 状态为 `CLOSED — SCOPE-BOUND`。

本记录不改变 T-1、T-2、T-4、T-5、T-6、T-7、UI-H、F2-C 或 P1 的任何状态。

---

## 1. 范围

F2-S1 是 Collider Serialization 的独立交付键，不是 F2-C 的延续，也不自动落入 F3。

本阶段覆盖：

- `ColliderComponent` 八字段的 `components[]` 序列化/反序列化；
- 真实 `SceneSerializerV1` Save → 磁盘 → 冷启 → `InstantiateScene` → Adapter 重建；
- 重建后的真实物理行为一致性；
- Bridge `SaveProject` / `OpenProject` 的八字段持久化；
- 字段 × 层 × 测试矩阵与已知限制记录。

明确不覆盖：

- Managed UI Collider Inspector/全字段作者入口；
- 生产 `EditorSession::Play` 的 F2-S1 行为验收；
- Collider schema 扩展（friction/restitution、更多 shape、body type）；
- ECS/Jolt 3D Collider、Prefab、Collision Event API；
- 修改 `SceneSerializerV1` 格式版本或 `components[]` 结构。

---

## 2. 合同冻结的语义

### 2.1 八字段

`ColliderComponent` 只持久化以下八字段
（`engine/src/Core/GameObject/ColliderComponent.cpp:123-133`）：

| 字段 | 类型 | 写入 | 读取 |
|---|---|---|---|
| `enabled` | bool | 始终写入 | 缺失保留当前；非 bool 忽略 |
| `shape` | string | `"Circle"` / `"Box"` | 缺失保留当前；未知字符串忽略 |
| `radius` | number | 始终写入 | 缺失保留当前；非 number 忽略 |
| `halfX` | number | 始终写入 | 同上 |
| `halfY` | number | 始终写入 | 同上 |
| `isSensor` | bool | 始终写入 | 缺失保留当前；非 bool 忽略 |
| `category` | number | 始终写入 | 缺失保留当前；非 number 忽略；收窄 `uint16` |
| `mask` | number | 始终写入 | 同上 |

不写入任何 Physics 运行时状态或句柄；B5-1 已对落盘文本做无
`BodyID`/`bodyID`/`runtimeBodyID`/`ShapeRef` 反断言
（`tests/test_physics/ColliderBindingTest.cpp:975-983`）。

### 2.2 冻结的反序列化语义

- `data` 非对象 → `false`，不修改字段；
- 空对象/缺失字段 → 保留当前/默认值，返回 `true`；
- 已知字段类型错误 → 仅忽略该字段，其他字段继续应用，对象路径返回 `true`；
- 未知 `shape` → 保留当前/默认形状，不告警；
- 未知额外键 → 忽略；
- 非原子：一个字段非法不回滚同一对象中已应用的其他字段；
- `SceneSerializerV1` 只有 `Deserialize == false` 才产生 `DataBad (kept defaults)`；
  对象内非法字段不会产生该告警。

这是 Collider v1 的显式宽容语义，与 RigidBody T-1-B 的原子语义不同，不互相追溯。

---

## 3. 字段 × 层 × 测试矩阵

图例：`新` = F2-S1 新增测试；`旧` = F2-B 既有证据；`Bridge` = 扩展后的
`F2ColliderSaveLoadColdRestart`；`—` = 不在 F2-S1 范围。

| 字段 | 组件 Serialize | 引擎冷启 | 物理行为 | Bridge |
|---|---|---|---|---|
| `enabled` | 新：`SerializeWritesAllEightFieldsWithoutRuntimeState` / `JsonRoundTripAllEightFields` | 旧：B5-3 disabled box（`ColliderBindingTest.cpp:1088-1093,1134`）；B6-5(b)（`:1636-1658`） | 旧：B5-3 / B6-5(b)（`IsActive=false` + 探针穿透） | Bridge：`enabled=false` |
| `shape` | 新：同上 | 旧：B5-1/B5-2/B5-3（`:946-1137`） | 旧：B6-2/B6-5（Circle/Box 双路径） | Bridge：Box + Circle |
| `radius` | 新：同上 | 旧：B5-1 `1.25`（`:951-953,1009,1024`）；B5-2 `1→2`（`:1030-1074`） | 旧：B6-1/B6-2/B6-5 | Bridge：Circle `radius=2.25` |
| `halfX` | 新：同上 | 旧：B5-1 `2.0`；B5-3 | 旧：B6-2/B6-6 | Bridge：`4.0` |
| `halfY` | 新：同上 | 旧：B5-1 `1.5`；B5-3 | 旧：B6-2/B6-6 | Bridge：`3.0` |
| `isSensor` | 新：同上 | 旧：B5-1/B5-3 | 旧：B6-3（sensor 穿透 vs normal 阻挡） | Bridge：`true` |
| `category` | 新：同上 | 旧：B5-1/B5-3 | 旧：B6-4（真实碰撞/滤除） | Bridge：`8` |
| `mask` | 新：同上 | 旧：B5-1/B5-3 | 旧：B6-4 | Bridge：`31` |

补充矩阵（语义/负路径）：

| 情形 | 证据 |
|---|---|
| 八键且无运行时状态 | 新：`SerializeWritesAllEightFieldsWithoutRuntimeState` |
| 缺失字段保留当前值 | 新：`MissingFieldsKeepCurrentValues` |
| 非法字段逐字段忽略、非原子 | 新：`InvalidKnownFieldsAreIgnoredFieldLocally` |
| 未知 shape 保留当前/默认 | 新：`UnknownShapeKeepsCurrentShape`；`InvalidShapeSceneLoadKeepsDefaultCircle` |
| 未知键忽略 | 新：`UnknownKeysAreIgnored` |
| 非对象拒绝 | 新：`NonObjectDataIsRejected` |
| 非法字段无 `DataBad` | 新：`InvalidKnownFieldDoesNotProduceDataBad` |
| Save 即快照 / 删除不复活 | 旧：B5-2 / B5-4 |
| 多实体 / 无 World 晚恢复 | 旧：B5-3 / B5-5b |
| 行为 A/B/C 对拍 / 重复 ×10 | 旧：B6-6 / B6-7 |
| 负路径无 ghost | 旧：B6-8 |

---

## 4. Gate 结果

| Gate | 结果 | 证据 |
|---|---|---|
| S1-G1 Schema | PASS | 源码八字段 + 新 Serialize 测试 |
| S1-G2 宽容语义 | PASS | 9 个 `F2S1_ColliderSerialization` 测试 |
| S1-G3 引擎冷启 | PASS（既有） | B5-1 + B5-3，38/38 F2ColliderPhysics |
| S1-G4 Save 即快照 | PASS（既有） | B5-2 + B5-4 |
| S1-G5 真实行为 | PASS（既有） | B6-1..B6-8 |
| S1-G6 Bridge | PASS | 扩展后的 `F2ColliderSaveLoadColdRestart` 1/1 |
| S1-G7 负路径 | PASS | B5-5a/5b + B6-8 + 新增非对象/非法字段测试 |
| S1-G8 双配置 | PASS | Debug/Release 定向 + 全量回归 |
| S1-G9 文档 | PASS | 本记录 + Gate B 测试记录 |

### 4.1 实际运行结果

| 项目 | Debug | Release |
|---|---|---|
| `F2S1_ColliderSerialization.*` | 9/9 | 9/9 |
| `EditorBridgeTest.F2ColliderSaveLoadColdRestart` | 1/1 | 1/1 |
| `F2ColliderPhysics.*` | 38/38 | 38/38 |
| `test_content` 全量 | 116 通过 / 1 skip（共 117） | 同左 |
| `test_bridge` 全量 | 103/103 | 103/103 |
| `test_physics` 全量 | 71/71 | 71/71 |
| 完整 CTest | 19/19 | 19/19 |

日志：

- `build/f2s1_test_content_serialization_{debug,release}.log`
- `build/f2s1_test_bridge_{debug,release}.log`
- `build/f2s1_test_physics_{debug,release}.log`
- `build/f2s1_test_content_full_{debug,release}.log`
- `build/f2s1_test_bridge_full_{debug,release}.log`
- `build/f2s1_test_physics_full_{debug,release}.log`
- `build/f2s1_ctest_{debug,release}.log`

构建：Debug/Release 目标构建成功；Debug 日志含 1 条既有 CMake Shaderc 未找到警告，
未观察到 MSVC warning/error 或 LNK4006；这是增量构建，不宣称 clean-build 零警告。

---

## 5. 既有证据与新增证据分界

### 5.1 F2-B 既有证据（不重复计入新增）

- `F2ColliderPhysics` 38 个用例中的 B2-B6；
- B5 的冷启/多实体/删除/负路径；
- B6 的物理行为对拍、sensor/filter、enabled、重复冷启、负路径；
- 本轮 `F2ColliderPhysics.*` 的 38/38 是对既有测试的当前回归，不是新增物理证据。

### 5.2 F2-S1 新增证据

- `tests/test_content/ColliderSerializationTest.cpp` 的 9 个语义测试；
- `F2ColliderSaveLoadColdRestart` 中新增的 `enabled=false` 与 Circle `radius=2.25`
  Save→Reopen 断言（测试数仍为 1，不新增用例）；
- B5-5a 注释更正（行为未变，不计新增测试）。

---

## 6. 已知限制与残余风险

1. **shape 静默回退**：非法 `shape` 保留当前/默认形状；新组件加载
   `"shape":"Triangle"` 会得到 Circle。可能把损坏的 Box 数据加载为 Circle。
2. **非原子**：同一对象中合法字段会被应用，非法字段被忽略；没有整体拒绝。
3. **无告警**：对象内的非法已知字段不会产生 `DataBad`。
4. **无数值校验**：`radius`/`halfX`/`halfY` 没有 NaN/Inf/超大值校验；
   `category`/`mask` 没有范围校验，按 `uint16` 收窄。
5. **Managed/生产路径排除**：Managed UI 没有 Collider Inspector；生产
   `EditorSession::Play` 只通过 T-1-F 作为相邻证据，不能替代 F2-S1。
6. **基线计数不一致**：`docs/Evidence-Baseline.md:19` 的 `test_physics` 期望计数 54
   与当前 71/71 不一致；本阶段未修改该全局基线，建议单独维护。

上述限制是 F2-S1 v1 的显式范围边界，不是隐藏的通过条件。若未来要求原子语义或更严格
数值校验，应另立生产语义任务，并单独授权修改 `ColliderComponent::Deserialize`。

---

## 7. 文档勘误

1. `tests/test_physics/ColliderBindingTest.cpp:1221` 原注释写“Deserialize 拒绝”，
   与实际的逐字段宽容语义不符；本轮已更正为“宽容语义忽略该字段”，并注明返回 `true`
   且不产生 `DataBad`。
2. `docs/Component-Ledger.md` EF-F2B-007 的“Golden Case 覆盖全字段
   （shape/radius/halfX/halfY/sensor/category/mask/enabled）”表述不精确：
   B5-1 Golden Case 未设置 `enabled=false`；该项由 B5-3 多实体测试与 B6-5(b) 覆盖。
   B5 套件整体覆盖八字段，但不应写成 Golden Case 单测覆盖。
   本阶段不重写历史 Ledger，仅在此登记勘误。
3. `docs/Evidence-Baseline.md:19` 的 `test_physics` 计数 54 与当前 71/71 不一致；
   本阶段不修改全局基线，登记为独立的基线维护项。

---

## 8. 边界与非声明

- 不声称 Managed UI 已完成 Collider 全字段冷启；
- 不声称生产 `EditorSession::Play` 已通过 F2-S1 的八字段验收；
- 不声称 Collider 已具备 friction/restitution、动态 body type 或多 shape；
- 不把 F2-B 的 B5/B6 通过数重复计为 F2-S1 新增；
- 不把 T-1-F、T-1 managed 或 T-5 的证据替代 F2-S1；
- 不修改 Collider schema、场景格式版本或 Bridge ABI。

---

## 9. 关闭条件核对

| 条件 | 状态 |
|---|---|
| S1-G1～S1-G9 有实际证据 | PASS |
| 新增测试与 Bridge 扩展在 Debug/Release 通过 | PASS |
| `test_physics`/`test_content`/`test_bridge` 回归记录实际数字 | PASS |
| 未修改范围外生产代码 | PASS |
| Closure 包含矩阵与已知限制 | PASS |
| B5-5a 注释更正与 EF-F2B-007/Evidence-Baseline 勘误登记 | PASS（基线计数未改，已登记） |
| Managed/生产路径排除事实已写明 | PASS |

**结论**：F2-S1 于 2026-10-31 关闭为 `F2-S1 — CLOSED — SCOPE-BOUND`。
关闭 F2-S1 不代表 T-1、F2-C、P1 或其他阶段关闭。

---

## 10. 本轮未做

- 未 commit / push / tag / merge；
- 未修改 `ColliderComponent`、`SceneSerializerV1`、`PhysicsColliderAdapter`、
  `PhysicsRuntime2D`、Bridge 或 `editor_avalonia`；
- 未新增 Collider 字段、场景格式版本或 Bridge ABI；
- 未清理脏子模块、`crashes/` 或既有工作区内容。
