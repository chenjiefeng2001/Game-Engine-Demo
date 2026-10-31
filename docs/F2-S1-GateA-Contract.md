# F2-S1 — Collider Serialization Evidence & Closure · Gate A Contract

**状态**：`F2-S1 GATE A — PASS — CONTRACT FROZEN — CLOSED`
**日期**：2026-10-31
**Gate A 关闭**：`CLOSED`（2026-10-31 用户确认；契约冻结，Gate B 已执行）
**基线**：HEAD `c1f7933`，分支 `avalonia`
**Gate B**：`AUTHORIZED / EXECUTED — PASS`（2026-10-31 用户确认全部裁决点；仅测试/文档；结果见 `docs/F2-S1-GateB-Test-Record.md`）
**输入**：
- `docs/F2-S1-Gate0-Scan.md`（Gate 0 只读扫描）
- `docs/F2-S1-Scope-and-Scheduling-Review.md`
- `docs/Engine-Foundation-F2B-Closure.md`
- `docs/Component-Ledger.md`（EF-F2B-007 / EF-F2B-009）
- `docs/T-1-B-RigidBody-Serialization.md`
- `docs/T-1-Closure-Record.md`、`docs/T-1-Managed-UI-Acceptance.md`
- 用户 2026-10-31 指示：按 Gate 0 建议策略准备 Gate A

**证据边界**：
- Gate A 冻结轮次未修改源码、测试、构建配置；未运行测试；未执行 Git 写操作。
- 2026-10-31 用户确认 §8 的四个裁决点，本契约转为 `FROZEN`，Gate B 仅授权测试/文档变更。
- 本契约不重新打开 T-7、UI-H2、T-5 或 T-1；不把 T-1/T-5 的通过数计入 F2-S1 新增证据。

---

## 0. Formal Adjudication Record

| ID | 最终裁决 | 冻结要求 |
|---|---|---|
| **S1-1** | 窄边界 | F2-S1 只覆盖 Collider 八字段序列化、引擎冷启恢复、真实物理重建/行为、Bridge Save→Reopen 字段持久化。 |
| **S1-2** | 八字段 schema | 只承认 `enabled`、`shape`、`radius`、`halfX`、`halfY`、`isSensor`、`category`、`mask`；不扩展 Collider schema。 |
| **S1-3** | 序列化写入 | `Serialize` 必须写入全部八字段；不写入任何 Physics 运行时状态或句柄。 |
| **S1-4** | 反序列化宽容语义 | 冻结当前逐字段、非原子行为：`data` 非对象才返回 `false`；对象内缺失字段保留当前/默认值；已知字段非法时仅忽略该字段；未知键忽略；对象路径不因单个字段非法返回 `false`（正常转换下返回 `true`）。 |
| **S1-5** | 已知限制显式化 | 非法 `shape` 静默保留当前/默认形状；非法已知字段不产生 `DataBad`；浮点字段不做有限值/范围校验；`category`/`mask` 收窄为 `uint16`。这些作为 v1 已知限制写入关闭记录，不伪装成已验证的正确性。 |
| **S1-6** | Bridge 八字段 | `F2ColliderSaveLoadColdRestart` 扩展为八字段 Save→Reopen；补充 `enabled=false` 与 Circle 非默认 `radius` 断言。仅测试变更。 |
| **S1-7** | 证据复用 | B5/B6 的既有结果作为 F2-S1 的引擎层证据引用，但在矩阵中明确标注“F2-B 既有证据”，不重复计为新增。 |
| **S1-8** | 新增测试范围 | 只补 Gate 0 识别的语义缺口：直接 Serialize/Deserialize 八字段、缺失/非法/未知键、非对象、未知 shape 回退；不改生产序列化代码。 |
| **S1-9** | Managed/生产路径 | Managed UI Collider 作者入口与生产 `EditorSession::Play` 不作为 F2-S1 验收；T-1 managed/T-1-F 只作相邻证据，不替代 F2-S1。 |
| **S1-10** | 关闭条件 | 字段 × 层 × 测试矩阵、Debug/Release 结果、已知限制、文档勘误全部写入 F2-S1 Closure 后，才可提议关闭。 |

---

## 1. 范围与术语

### 1.1 术语

| 术语 | 定义 |
|---|---|
| Collider Serialization | `ColliderComponent` 通过稳定类型名 `"Collider"` 和 `components[].data` 的持久化 |
| 冷启 | 从磁盘 `engine.scene` 重新加载为全新 Scene/GameObject/组件/Physics Body |
| Save 即快照 | 磁盘只保存 `components[]` 声明状态；未保存的 runtime mutation 不进入冷启 |
| 宽容语义 | 单个已知字段非法时只忽略该字段，其他字段继续应用，整体不失败 |
| 原子语义 | 任一已知字段非法则整体拒绝，不应用任何字段（RigidBody T-1-B 的语义） |
| 字段 × 层矩阵 | 八字段 × {组件序列化, 引擎冷启, 物理行为, Bridge} 的证据映射 |

### 1.2 范围内

- `ColliderComponent::Serialize/Deserialize` 的八字段契约；
- `SceneSerializerV1` 的 `components[]` 写入/读取；
- `PhysicsColliderAdapter` 从组件状态重建 Body/Shape；
- B5/B6 的冷启与真实物理行为证据复用；
- Bridge `SaveProject` / `OpenProject` 八字段持久化；
- F2-S1 字段 × 层 × 测试矩阵与关闭记录；
- B5-5a 注释更正、EF-F2B-007 措辞勘误、Evidence-Baseline 计数不一致的登记。

### 1.3 范围外

- Collider schema 扩展（friction/restitution、动态 body type、更多 shape）；
- 把 Collider Deserialize 改为原子语义或新增 `DataBad` 告警（生产变更，另行授权）；
- Managed UI Collider Inspector/全字段作者入口；
- 生产 `EditorSession::Play` 的 F2-S1 行为验收；
- Jolt/ECS 3D Collider、Prefab、Collision Event API；
- 修改 `SceneSerializerV1` 格式版本或 `components[]` 结构；
- 清理脏子模块或 `crashes/`。

---

## 2. 冻结的八字段 Schema

### 2.1 字段表

| 字段 | JSON 类型 | 写入规则 | 读取规则 |
|---|---|---|---|
| `enabled` | bool | 始终写入 `IsEnabled()` | 缺失保留当前；非 bool 忽略；应用后写回组件 |
| `shape` | string | 始终写入稳定名 `"Circle"` / `"Box"` | 缺失保留当前；未知字符串忽略；合法值应用 |
| `radius` | number | 始终写入 `m_Radius` | 缺失保留当前；非 number 忽略；number 直接转 `float` |
| `halfX` | number | 始终写入 `m_HalfX` | 同上 |
| `halfY` | number | 始终写入 `m_HalfY` | 同上 |
| `isSensor` | bool | 始终写入 `m_IsSensor` | 缺失保留当前；非 bool 忽略 |
| `category` | number | 始终写入 `m_Category` | 缺失保留当前；非 number 忽略；number 收窄为 `uint16` |
| `mask` | number | 始终写入 `m_Mask` | 同上 |

### 2.2 不写入的状态

- `BodyID`、`runtimeBodyID`、World 指针、Box2D/Jolt 句柄；
- Physics Body 的位置、速度、shape/fixture 运行时状态；
- ECS Entity ID、帧局部句柄；
- 任何不属于八字段的临时 UI/编辑器状态。

B5-1 已对落盘文本做无 BodyID/ShapeRef 反断言；F2-S1 新增的 Serialize 测试也必须做键集合断言，而不仅是值断言。

---

## 3. 冻结的 Serialize / Deserialize 语义

### 3.1 Serialize

```text
Serialize(json)
  json["enabled"]  = IsEnabled()
  json["shape"]    = "Circle" | "Box"
  json["radius"]   = m_Radius
  json["halfX"]    = m_HalfX
  json["halfY"]    = m_HalfY
  json["isSensor"] = m_IsSensor
  json["category"] = m_Category
  json["mask"]     = m_Mask
```

约束：

- 八键始终存在，不因默认值省略；
- 不支持未知键透传；
- `shape` 只输出稳定名；不输出枚举整数；
- `enabled` 来自 `Component::IsEnabled()`，不新增 Collider 私有 enabled 字段。

### 3.2 Deserialize

| 输入 | 行为 | 返回值 |
|---|---|---|
| `data` 非对象 | 不修改任何字段 | `false` |
| 空对象 `{}` | 保留全部当前/默认值 | `true` |
| 字段缺失 | 只保留该字段当前/默认值，其他字段继续应用 | `true` |
| 已知字段类型错误 | 忽略该字段，其他字段继续应用 | `true` |
| `shape` 未知字符串 | 忽略，保留组件当前形状（新组件默认 `Circle`） | `true` |
| 未知额外键 | 忽略 | `true` |
| 任意数值 float | 直接转 `float`，不做有限值/范围校验 | `true` |
| `category`/`mask` 任意 number | 收窄为 `uint16`，不做范围校验 | `true` |

明确冻结：**非原子**。某个已知字段非法不会回滚同一对象中已经应用的其他字段，
也不会让对象路径返回 `false`（正常数值转换下返回 `true`）。

### 3.3 加载器行为

`SceneSerializerV1::InstantiateScene`：

- `Deserialize` 返回 `false` → 追加 `DataBad (kept defaults)` 告警，组件仍以默认值挂载；
- `Deserialize` 返回 `true` → 不追加 `DataBad`，即使对象内存在被忽略的非法字段；
- 未知组件类型 → 既有 warning，实体照常加载；
- 本阶段不新增警告通道。

### 3.4 已知限制（v1 接受）

1. `shape` 损坏时会静默回退到当前/默认形状；如果组件是新构造的，则 Box 数据可能被加载为 Circle。该行为必须在 F2-S1 Closure 中显式记录，不得描述为“拒绝坏数据”。
2. 非法 `radius`/`halfX`/`halfY` 不会失败，也不会告警；只保留默认/当前值。
3. 浮点字段没有 NaN/Inf/超大值校验；`category`/`mask` 没有范围校验。
4. 宽容语义与 RigidBody T-1-B 的原子语义不同；这是 Collider v1 的已冻结差异，不是 RigidBody 契约的延伸。
5. 若未来需要原子语义或更严格校验，必须单独立项；F2-S1 不改生产代码。

---

## 4. 验收门禁

### 4.1 门禁清单

| Gate | 必须证明 | 承载 |
|---|---|---|
| S1-G1 Schema | 八字段名称、类型、稳定 shape 名；无运行时状态 | 源码核对 + 新增 Serialize 测试 |
| S1-G2 宽容语义 | 缺失/非法/未知键/非对象/未知 shape 的实际行为 | 新增 `ColliderSerializationTest` |
| S1-G3 引擎冷启 | 八字段在真实 Save→冷启→Adapter 重建中恢复 | B5-1 + B5-3（既有证据） |
| S1-G4 Save 即快照 | 未保存 runtime mutation 不进入冷启；删除不复活 | B5-2 + B5-4（既有证据） |
| S1-G5 真实行为 | 重建后的碰撞/sensor/filter/enabled 行为与连续运行一致 | B6-1..B6-8（既有证据） |
| S1-G6 Bridge | 八字段 SaveProject→OpenProject 全部还原 | 扩展后的 `F2ColliderSaveLoadColdRestart` |
| S1-G7 负路径 | 无 Collider/未知类型/坏数据/无 World 可恢复，不 crash/无 ghost | B5-5a/5b + B6-8 + 新增非对象/非法字段测试 |
| S1-G8 双配置 | Debug/Release 定向与回归通过 | Gate B 实际运行记录 |
| S1-G9 文档 | 字段 × 层 × 测试矩阵、已知限制、勘误、既有/新增证据分界 | F2-S1 Closure |

### 4.2 新增测试清单（Gate B 实施）

新增 `tests/test_content/ColliderSerializationTest.cpp`，至少包含：

1. `SerializeWritesAllEightFieldsWithoutRuntimeState`
   - 设置全部八字段后 Serialize；
   - 断言恰好八个键、类型与值正确；
   - 断言无 `BodyID` / `bodyID` / `runtimeBodyID` / `ShapeRef`。
2. `JsonRoundTripAllEightFields`
   - 八字段 Serialize → Deserialize → 逐字段回读。
3. `MissingFieldsKeepCurrentValues`
   - 空对象与部分对象；缺失字段保留当前/默认值，其他字段应用，返回 `true`。
4. `InvalidKnownFieldsAreIgnoredFieldLocally`
   - 同时包含合法字段与非法字段（错误类型/未知 shape）；
   - 断言合法字段已应用、非法字段被忽略、返回 `true`，证明非原子语义。
5. `UnknownShapeKeepsCurrentShape`
   - 先把组件设为 `Box`，再 Deserialize `{"shape":"Triangle"}`；
   - 断言仍是 `Box`；新组件同输入则保持默认 `Circle`。这是已知限制的显式测试。
6. `UnknownKeysAreIgnored`
   - 额外键不影响已知字段，且不会被 Serialize 回写。
7. `NonObjectDataIsRejected`
   - array/null → `false`，字段不变。
8. `InvalidKnownFieldDoesNotProduceDataBad`（scene/Instantiate 层）
   - 构造带非法字段的 `SerializedComponent`；
   - 断言加载成功、组件默认值、warnings 为空；
   - 该测试把“无 DataBad”的已知限制钉住。
9. `InvalidShapeSceneLoadKeepsDefaultCircle`（scene/Instantiate 层）
   - 落盘场景含 `"shape":"Triangle"`；
   - 断言加载为 Circle，无 warning；
   - 与第 5 条一起构成 shape 回退证据。

不新增重复的 B5/B6 冷启/行为测试；现有测试已经覆盖的内容只在矩阵中引用。

### 4.3 Bridge 测试扩展

扩展 `tests/test_bridge/test_editor_capi.cpp` 的 `F2ColliderSaveLoadColdRestart`：

- 现有 `ColRig`（Box）增加 `enabled=false`，Save 后重开断言 `enabled=false`；
- 新增 `ColCircle` 实体：`shape=Circle`、`radius=2.25`，Save 后重开断言 `shape=Circle`、`radius=2.25`；
- 落盘文本检查包含 `"enabled": false` 与 `"radius": 2.25`；
- 不新增生产 Bridge ABI，不修改 Collider 序列化代码。

### 4.4 测试命令（Gate B 执行）

```text
cmake --build build --config Debug --target test_content test_bridge test_physics
./build/tests/Debug/test_content.exe --gtest_filter=F2S1_ColliderSerialization.*
./build/tests/Debug/test_bridge.exe --gtest_filter=EditorBridgeTest.F2ColliderSaveLoadColdRestart
./build/tests/Debug/test_physics.exe --gtest_filter=F2ColliderPhysics.*
cmake --build build --config Release --target test_content test_bridge test_physics
./build/tests/Release/test_content.exe --gtest_filter=F2S1_ColliderSerialization.*
./build/tests/Release/test_bridge.exe --gtest_filter=EditorBridgeTest.F2ColliderSaveLoadColdRestart
./build/tests/Release/test_physics.exe --gtest_filter=F2ColliderPhysics.*
```

Gate B 记录必须写出实际命令、实际通过数、失败数与警告数；不得把既有 F2-B 数字当作本轮新增结果。

---

## 5. 预期修改文件

| 文件 | 变更性质 |
|---|---|
| `tests/test_content/ColliderSerializationTest.cpp` | 新增 F2-S1 语义测试 |
| `tests/CMakeLists.txt` | 将新测试加入 `test_content` |
| `tests/test_bridge/test_editor_capi.cpp` | 扩展 `F2ColliderSaveLoadColdRestart` 八字段断言 |
| `tests/test_physics/ColliderBindingTest.cpp` | 仅更正 B5-5a 注释（`Deserialize 拒绝` → 实际宽容语义） |
| `docs/F2-S1-GateB-Test-Record.md` | 新增 Gate B 测试记录 |
| `docs/F2-S1-Closure-Record.md` | 新增最终关闭记录与矩阵 |

**明确不修改**：

- `ColliderComponent.h/.cpp`；
- `SceneSerializerV1.*`；
- `PhysicsColliderAdapter.*`、`PhysicsRuntime2D.*`；
- `bridge/src/*`、Bridge ABI；
- `editor_avalonia/*`；
- `assets/scenes/*` 或场景格式版本；
- 任何第三方子模块或 `crashes/`。

---

## 6. 证据矩阵模板（Closure 填写）

| 字段 | 组件 Serialize | 引擎冷启 | 物理行为 | Bridge Save→Reopen |
|---|---|---|---|---|
| `enabled` | 新增 Serialize 测试 | B5-3 / B6-5(b) | B5-3 / B6-5(b) | 扩展后的 SaveLoad |
| `shape` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-2 / B6-5 | 扩展后的 SaveLoad |
| `radius` | 新增 Serialize 测试 | B5-1 / B5-2 | B6-1 / B6-2 / B6-5 | 扩展后的 SaveLoad（Circle 实体） |
| `halfX` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-2 / B6-6 | 扩展后的 SaveLoad |
| `halfY` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-2 / B6-6 | 扩展后的 SaveLoad |
| `isSensor` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-3 | 扩展后的 SaveLoad |
| `category` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-4 | 扩展后的 SaveLoad |
| `mask` | 新增 Serialize 测试 | B5-1 / B5-3 | B6-4 | 扩展后的 SaveLoad |

矩阵必须区分“F2-B 既有证据”和“F2-S1 新增证据”，不得把 B5/B6 的通过数再次计入新增测试数。

---

## 7. 关闭条件

F2-S1 只有同时满足以下条件才可提议关闭：

1. S1-G1～S1-G9 全部门禁有实际证据；
2. 新增测试与扩展后的 Bridge 测试在 Debug/Release 均通过；
3. `test_physics`、`test_content`、`test_bridge` 回归结果记录实际通过/失败数；
4. 没有修改本契约声明范围外的生产代码；
5. Closure 记录包含字段 × 层 × 测试矩阵、已知限制、既有/新增证据分界；
6. B5-5a 注释更正、EF-F2B-007 勘误、Evidence-Baseline 计数不一致登记全部完成；
7. Managed UI/生产 Play 未纳入验收的事实被明确写出，不以 T-1 证据替代。

---

## 8. 已裁决问题（2026-10-31 用户确认）

| 编号 | 问题 | 最终裁决 |
|---|---|---|
| A-1 | 宽容 vs 原子 | 接受宽容、非原子语义作为 Collider v1 冻结语义；本阶段不改生产代码。 |
| A-2 | Bridge 八字段 | 扩展 `F2ColliderSaveLoadColdRestart`，补齐 `enabled` 与 Circle `radius`。 |
| A-3 | Managed/生产路径 | 确认排除出 F2-S1 验收；T-1 managed/T-1-F 仅作相邻证据。 |
| A-4 | `shape` 静默回退 | 接受为 v1 已知限制，必须在 Closure 中显式记录；若未来需要原子校验，另立生产语义任务。 |

---

## 9. Gate A 本轮未做

- 未修改源码/测试/构建配置；
- 未构建、未运行任何测试；
- 未新增 CMake 目标或测试文件；
- 未 commit / push / tag / merge；
- 未清理脏子模块或 `crashes/`。
