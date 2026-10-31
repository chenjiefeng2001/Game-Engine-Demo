# F2-S1 — Gate B Test Record

**状态**：`F2-S1 GATE B — PASS — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：HEAD `c1f7933`，分支 `avalonia`
**输入**：`docs/F2-S1-GateA-Contract.md`（FROZEN）
**证据边界**：本记录只报告 Gate B 实际修改与实际运行结果；未修改生产代码，
未把 F2-B/T-1/T-5 的既有通过数计为 F2-S1 新增测试。

---

## 1. 实际修改

| 文件 | 修改 |
|---|---|
| `tests/test_content/ColliderSerializationTest.cpp` | 新增 9 个 F2-S1 序列化/反序列化语义测试 |
| `tests/CMakeLists.txt` | 将新测试加入 `test_content` |
| `tests/test_bridge/test_editor_capi.cpp` | 扩展 `F2ColliderSaveLoadColdRestart`：增加 `enabled=false`、Circle `radius=2.25` 的 Save→Reopen 断言 |
| `tests/test_physics/ColliderBindingTest.cpp` | 仅更正 B5-5a 注释：从“Deserialize 拒绝”改为冻结的宽容语义说明 |
| `docs/F2-S1-GateA-Contract.md` | Gate A 状态由提案转为 FROZEN，记录用户裁决 |

明确未修改：`ColliderComponent.*`、`SceneSerializerV1.*`、`PhysicsColliderAdapter.*`、
`PhysicsRuntime2D.*`、Bridge ABI、`editor_avalonia/*`、场景格式、第三方子模块、`crashes/`。

---

## 2. 新增测试

命令：

```text
./build/tests/Debug/test_content.exe --gtest_filter=F2S1_ColliderSerialization.*
./build/tests/Release/test_content.exe --gtest_filter=F2S1_ColliderSerialization.*
```

结果：**9/9 Debug；9/9 Release**。

| 测试 | 证明 |
|---|---|
| `SerializeWritesAllEightFieldsWithoutRuntimeState` | 恰好八个键、八字段值正确；无 BodyID/runtimeBodyID/ShapeRef |
| `JsonRoundTripAllEightFields` | 八字段 JSON 往返 |
| `MissingFieldsKeepCurrentValues` | 缺失字段保留当前值，部分字段正常应用 |
| `InvalidKnownFieldsAreIgnoredFieldLocally` | 非法字段逐字段忽略，合法字段仍应用；`category` 收窄 `uint16`；非原子语义 |
| `UnknownShapeKeepsCurrentShape` | 未知 shape 保留当前形状；新组件保持默认 Circle |
| `UnknownKeysAreIgnored` | 未知键忽略，不回写 |
| `NonObjectDataIsRejected` | array/null 返回 `false`，字段不变 |
| `InvalidKnownFieldDoesNotProduceDataBad` | 场景加载中非法字段不产生 `DataBad`，组件按默认/有效字段挂载 |
| `InvalidShapeSceneLoadKeepsDefaultCircle` | 真实文件加载含 `"shape":"Triangle"` 的场景后保持默认 Circle，无 warning |

---

## 3. Bridge 八字段冷启

命令：

```text
./build/tests/Debug/test_bridge.exe --gtest_filter=EditorBridgeTest.F2ColliderSaveLoadColdRestart
./build/tests/Release/test_bridge.exe --gtest_filter=EditorBridgeTest.F2ColliderSaveLoadColdRestart
```

结果：**1/1 Debug；1/1 Release**。

覆盖：

- `ColRig`：Box + `halfX=4.0` + `halfY=3.0` + `isSensor=true` + `category=8` +
  `mask=31` + `enabled=false`；SaveProject → OpenProject 后逐项还原；
- `ColCircle`：Circle + `radius=2.25`；SaveProject → OpenProject 后还原；
- 落盘文本断言包含 `"enabled": false` 与 `"radius": 2.25`。

日志中的 `CreateTexture ... refused: no current OpenGL context` 是该 Bridge 测试的
既有 headless 纹理加载噪声，不是测试失败；两个配置均通过。

---

## 4. 引擎物理证据（既有，不重复计新增）

命令：

```text
./build/tests/Debug/test_physics.exe --gtest_filter=F2ColliderPhysics.*
./build/tests/Release/test_physics.exe --gtest_filter=F2ColliderPhysics.*
```

结果：**38/38 Debug；38/38 Release**。

这些是 F2-B 的既有 B2-B6 测试；Gate B 只更正了 B5-5a 注释，没有新增或改写
物理断言。F2-S1 Closure 的字段矩阵将引用其中的 B5/B6 结果，并明确标注
“F2-B 既有证据”。

---

## 5. 完整回归

### 5.1 目标级

| 目标 | Debug | Release | 日志 |
|---|---|---|---|
| `test_content` 全量 | **117 个：116 通过 / 1 skip** | 同左 | `build/f2s1_test_content_full_{debug,release}.log` |
| `test_bridge` 全量 | **103/103** | **103/103** | `build/f2s1_test_bridge_full_{debug,release}.log` |
| `test_physics` 全量 | **71/71** | **71/71** | `build/f2s1_test_physics_full_{debug,release}.log` |

`test_content` 的唯一 skip 是既有的 `ShaderCompileRule.SkippedWithoutVulkanOrShaderc`
（Vulkan/shaderc 不在当前构建），不是 F2-S1 新增 skip。

### 5.2 完整 CTest

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug | **19/19 passed，0 failed** | `build/f2s1_ctest_debug.log` |
| Release | **19/19 passed，0 failed** | `build/f2s1_ctest_release.log` |

---

## 6. 构建与警告

| 构建 | 观察值 | 日志 |
|---|---|---|
| Debug `test_content + test_bridge + test_physics` | 成功；日志含 1 条既有 CMake Shaderc 未找到警告；未观察到 MSVC warning/error 行或 LNK4006 | `build/f2s1_build_debug.log` |
| Release 同目标 | 成功；日志未观察到 warning/error 行 | `build/f2s1_build_release.log` |

这是增量构建，不宣称 clean-build 零警告；本轮没有做 clean rebuild。

---

## 7. 已知限制（F2-S1 v1 接受）

1. 非法 `shape` 静默保留当前/默认形状；新组件加载 `"shape":"Triangle"` 会得到
   Circle，不产生 warning。
2. 非法已知字段不会让对象路径返回 `false`，不会产生 `DataBad`；同对象其他合法字段
   仍会被应用（非原子）。
3. `radius`/`halfX`/`halfY` 没有 NaN/Inf/超大值校验；`category`/`mask` 没有范围校验。
4. Managed UI Collider 作者入口与生产 `EditorSession::Play` 不在 F2-S1 验收范围；
   T-1 managed/T-1-F 只作相邻证据。
5. B5-5a 注释已更正；EF-F2B-007 的“Golden Case 覆盖全字段”措辞需在 Closure 中
   作为勘误说明，B5 套件整体（B5-1 + B5-3）才覆盖 `enabled=false`。

---

## 8. Gate B 未做

- 未修改任何生产序列化/物理/编辑器代码；
- 未新增 Bridge ABI、Collider 字段或场景格式版本；
- 未运行 managed UI / 生产 Play 的 F2-S1 验收；
- 未 commit / push / tag / merge；
- 未清理脏子模块、`crashes/` 或既有工作区内容。
