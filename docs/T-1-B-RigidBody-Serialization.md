# T-1-B：RigidBody 序列化与冷启动恢复

**状态**：`T-1-B — IMPLEMENTATION COMPLETE, ACCEPTED (T-1 CLOSED — SCOPE-BOUND, 2026-10-29)`
**日期**：2026-10-29
**范围**：仅 `RigidBody` 的 `components[]` 持久化与组件状态冷启恢复。
**未启动**：T-1-C（PhysicsWorld/Play）、T-1-D（Lua）、T-1-E（Inspector）、T-1-F（端到端）。

本记录只陈述实际修改与实际运行结果。未执行 commit / push / tag / merge；
T-1-A 的源码与测试仍处于未提交状态，T-1-B 也未建立新的提交边界。

---

## 1. 冻结的序列化语义

### 1.1 Schema

`RigidBody` 只持久化 T-1-A 已冻结的三个字段：

```json
{
  "type": "RigidBody",
  "data": {
    "motionType": "Kinematic",
    "linearDamping": 0.25,
    "angularDamping": 0.5
  }
}
```

| 字段 | JSON 类型 | 写入规则 | 读取规则 |
|---|---|---|---|
| `motionType` | string | `Static` / `Kinematic` / `Dynamic` 稳定名 | 缺失保留当前/默认值；未知名 → 失败 |
| `linearDamping` | number | 有限、`>= 0` 的 float | 缺失保留当前/默认值；负数/NaN/Inf/超 float 范围 → 失败 |
| `angularDamping` | number | 同上 | 同上 |

明确不写入：

- `enabled`（T-1-A 未把它定义为契约反射字段）；
- `BodyID` / `runtimeBodyID` / World 指针 / Box2D 句柄；
- 位置、旋转、速度等 Transform/运行时状态；
- Collider 的几何、sensor、category/mask。

### 1.2 缺失与非法数据处理

| 输入情形 | `Deserialize` 行为 | 加载器可见结果 |
|---|---|---|
| `data` 是空对象 `{}`（T-1-A 过渡数据） | 返回 `true`，全部保留默认值 | 无 `DataBad` 告警 |
| 某个字段缺失 | 返回 `true`，该字段保留当前/默认值，其他有效字段正常应用 | 无告警 |
| 已出现的已知字段非法 | 返回 `false`，**不应用任何字段**（原子拒绝） | `InstantiateScene` 记录 `DataBad (kept defaults)`，组件仍挂载 |
| 未知额外字段 | 忽略 | 无告警 |
| `data` 不是对象 | 返回 `false`，不修改当前值 | `DataBad` 告警，组件挂载默认值 |

`enabled` 不持久化是已知限制：通过 C++ `Component::SetEnabled` 关闭的 RigidBody，
冷启后会回到默认启用状态。T-1-C/T-1-E 若需要作者可编辑的启用状态，必须先做契约扩展。

---

## 2. 实际源码差异

### 2.1 新增 / 修改

| 文件 | 差异 |
|---|---|
| `engine/include/Engine/Core/GameObject/RigidBodyComponent.h` | 增加 `Serialize/Deserialize` 声明（2 个 override） |
| `engine/src/Core/GameObject/RigidBodyComponent.cpp` | 增加三字段序列化、原子反序列化、damping 安全解析 |
| `tests/test_content/RigidBodySerializationTest.cpp` | 新增 10 个引擎级序列化/冷启门禁 |
| `tests/test_bridge/test_editor_capi.cpp` | 新增 1 个 Bridge 级 Save/Open 冷启门禁（含 T-1-A 空 data 迁移） |
| `tests/CMakeLists.txt` | `test_content` 加入 `RigidBodySerializationTest.cpp`（1 行） |
| `docs/T-1-B-RigidBody-Serialization.md` | 本记录 |

### 2.2 明确未修改

- `SceneSerializerV1.*`、场景格式版本、`components[]` 结构；
- `ColliderComponent.*`、Collider schema；
- `PhysicsColliderAdapter.*`、`Box2DPhysicsWorld.*`、任何物理绑定；
- `GameplayAPI.cpp`、`capi.*`、`editor_avalonia/*`、Bridge ABI；
- T-2 的 Content 代码、RevisionStore、导入服务与故障测试。

---

## 3. 测试证据（实际运行）

### 3.1 定向门禁

命令：

```text
cmake --build build --config Debug --target test_content test_bridge
./build/tests/Debug/test_content.exe --gtest_filter=T1B_RigidBodySerialization.*
./build/tests/Debug/test_bridge.exe --gtest_filter=EditorBridgeTest.T1B*
cmake --build build --config Release --target test_content test_bridge
./build/tests/Release/test_content.exe --gtest_filter=T1B_RigidBodySerialization.*
./build/tests/Release/test_bridge.exe --gtest_filter=EditorBridgeTest.T1B*
```

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `T1B_RigidBodySerialization.*` | **10/10** | **10/10** | `build/t1b_test_serialization_{debug,release}.log` |
| `EditorBridgeTest.T1B*` | **1/1** | **1/1** | `build/t1b_test_bridge_debug3.log` / `build/t1b_test_bridge_release.log` |

引擎级 10 个用例：

1. `SerializeWritesOnlyFrozenFields`：仅三键，无 enabled/运行时句柄。
2. `JsonRoundTrip`：三字段 JSON 往返。
3. `MissingFieldsKeepCurrentValues`：缺失字段保留当前值。
4. `InvalidKnownFieldsAreAtomic`：未知枚举、负数、超范围、错误类型、NaN/Inf 全部原子拒绝。
5. `UnknownKeysAreIgnored`：未知额外键忽略。
6. `NonObjectDataIsRejected`：数组/null 拒绝。
7. `SaveLoadColdStartRestoresFields`：真实文件 Save→Load→Instantiate，多实体 + Collider 共存。
8. `T1AEmptyDataLoadsDefaults`：T-1-A 空 data 恢复默认值且无告警。
9. `InvalidDataWarnsAndKeepsDefaults`：非法数据触发 `DataBad` 告警，组件以默认值挂载。
10. `RepeatedSaveLoadIsStable`：三次 Save/Load 无漂移。

Bridge 级用例 `T1BRigidBodySaveLoadColdRestart`：

- Add RigidBody → 设置三字段 → SaveProject；
- 场景 JSON 含 `RigidBody`、三字段，且无 `runtimeBodyID`/`bodyID`；
- 重新 OpenProject（走 RevisionStore 的 `current`）→ 三字段逐项还原；
- 在 scratch 内移除 `.engine-content` 后打开 `T1BEmpty.scene`，验证 T-1-A 空 data 走 legacy scene 路径时恢复默认值。该移除仅用于测试隔离，不是生产恢复流程。

### 3.2 全量回归

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `test_content` 全量 | **108 个：102 通过 / 5 既有失败 / 1 skip** | 同左 | `build/t1b_test_content_{debug,release}.log` |
| `test_bridge` 全量 | **101/101** | **101/101**（清理 scratch 后复跑） | `build/t1b_test_bridge_full_{debug,release2}.log` |
| 完整 CTest | **16/17** | **16/17** | `build/t1b_baseline_ctest_{debug,release}.log` |

`test_content` 的 5 个失败仍是 T-2 既有失败，未新增、未改写：

- `ContentDogfood.DF03_Smoke_LoadRunTimerExpire`
- `ContentDogfood.DF03_EntityCount_MultiScript`
- `Dogfood05.LoadAndRun`
- `Dogfood06.LoadAndRun`
- `Dogfood07.LoadRunRestart`

### 3.3 test_bridge Release 首次全量运行的 scratch 锁

- 首次 Release 全量（`build/t1b_test_bridge_full_release.log`）出现 **9 个失败**，
  全部是同一 scratch 目录 `gp01_editor_scratch/gate_p3` 的
  `remove_all: The process cannot access the file because it is being used by another process`。
  失败从 `MissingGuidAssetWarnsAndEntityRemains` 开始级联，T-1-B 断言本身没有被执行到。
- 删除测试生成的 `gp01_editor_scratch` 后复跑（`build/t1b_test_bridge_full_release2.log`），
  **101/101 通过**。
- 结论：这是既有测试夹具的 scratch 文件占用/隔离问题，不是 T-1-B 的序列化失败；
  本轮没有修改夹具来掩盖它。该问题应作为独立测试基础设施项跟踪。

### 3.4 构建警告（按实际日志）

| 构建 | 观察值 | 日志 |
|---|---|---|
| Debug `test_content + test_bridge` 定向 | **2×LNK4006** + **1 条 CMake Shaderc 未找到警告** | `build/t1b_build_debug.log` |
| Release 定向 | **2×LNK4006** | `build/t1b_build_release.log` |
| 全量 Debug/Release 增量构建 | 日志中 **0 条 warning 行**；这是增量构建，不能作为 clean-build 零警告结论 | `build/t1b_build_all_{debug,release}.log` |

T-2 记录的第一方全量构建基线为 Debug/Release 各 5 条既有警告；本轮未做 clean rebuild。

---

## 4. 兼容性与边界

| 项目 | 结果 |
|---|---|
| 旧项目（无 RigidBody） | 仍按既有 `components[]`/GUID 规则加载；T-1-B 未改加载器 |
| T-1-A 空 data | 可加载，恢复三字段默认值，无 `DataBad` |
| 非法字段 | 原子拒绝，组件保留默认值并产生 `DataBad` 告警 |
| RevisionStore | Bridge 用例走真实 SaveProject/OpenProject；`current` 仍是提交权威；未改 M1-D 规则 |
| Collider schema | 未修改；共存测试只验证互不破坏 |
| F2-S1 | 未关闭、未计入证据 |
| 包体/场景格式版本 | 未变（仍为 `engine.scene` v1） |

---

## 5. 已知限制 / 未完成项

1. **`enabled` 不持久化**：C++ 层关闭的 RigidBody 冷启后会回到默认启用状态；
   这是 T-1-A 未把 enabled 纳入反射契约的直接结果。T-1-C/T-1-E 前必须裁决。
2. **没有物理 cold start**：本阶段只恢复组件状态；没有 PhysicsWorld、body 创建或物理行为。
3. **没有 Lua/Inspector**：通用通道尚未验证 RigidBody 的脚本/UI 行为。
4. **没有 Adapter 改动**：RigidBody + Collider 仍是 T-1-A 的静态映射；动态绑定属于 T-1-C。
5. **T-1-A 提交边界未建立**：T-1-A 源码/测试仍未提交；T-1-B 记录与 T-1-A 差异目前在同一工作树中。
6. **test_bridge scratch 锁**：见 §3.3；独立跟踪，不在本阶段修复。
7. **未做 clean rebuild**：警告数字按 §3.4 的增量日志原样保留。

---

## 6. 基线保护核对

- T-2 关键文件 SHA-256 与 Stage 0 记录逐项一致。
- 暂存区为空；`crashes/` 仍为 115 个产物；3 个脏子模块保持原样。
- 未修改 T-2 的 Content 代码、RevisionStore、导入服务或故障测试。
- 未执行 Git 写操作；本记录不授权 commit / push / tag / merge。

---

## 7. 复审入口

建议按以下顺序判定：

1. 三字段 schema 是否接受；`enabled` 不持久化是否接受其已知限制。
2. 非法字段“原子拒绝 + DataBad + 默认值”的语义是否接受。
3. Bridge 用例中为验证 T-1-A 空 data 而移除 scratch `.engine-content` 是否接受为测试隔离手段。
4. `test_bridge` 首次 Release 全量的 scratch 锁是否接受为既有夹具问题（复跑 101/101）。
5. 既有 5 个 `test_content` 失败与 16/17 CTest 是否原样保留。

T-1-B 通过前不启动 T-1-C；T-1-C 通过前不启动 T-1-D。
