# T-1 RigidBody 闭环：C/D/E/F 实施与最终记录

**状态**：`T-1 — CLOSED — SCOPE-BOUND`
**范围例外**：Managed UI 物理观测 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`（见 §11）
**日期**：2026-10-29
**范围**：T-1-C 2D 物理运行时、T-1-D Lua、T-1-E Avalonia Inspector、T-1-F 端到端。
**未执行**：commit / push / tag / merge。
**未改**：Collider schema、SceneSerializerV1 格式、RevisionStore/Rename 冻结规则、ECS 3D/Jolt、F2-S1。

本记录只列实际修改与实际运行结果；T-1-A/B 的细节见
`docs/T-1-A-RigidBody-Contract.md` 与 `docs/T-1-B-RigidBody-Serialization.md`。

---

## 1. 阶段状态

| 阶段 | 状态 | 主要产物 |
|---|---|---|
| T-1-A | 实现完成，已接受 | RigidBody 契约、注册、7 个契约门禁 |
| T-1-B | 实现完成，已接受 | 三字段序列化/冷启、10 引擎门禁 + 1 Bridge 门禁 |
| **T-1-C** | **实现完成，已接受** | `PhysicsRuntime2D`、Adapter 组合绑定、Play 生命周期、10 运行时门禁 |
| **T-1-D** | **实现完成，已接受** | RigidBody 通用 `Engine.component.*` 脚本门禁（无专用 API） |
| **T-1-E** | **实现完成，已接受** | Avalonia RigidBody Inspector + PP-08b 生产门禁 |
| **T-1-F** | **实现完成（原生 E2E），已接受** | 保存/冷启/Play/碰撞/Lua 改 Static/Stop/重放 E2E |

---

## 2. T-1-C：PhysicsWorld 与 Play 生命周期

### 2.1 冻结的运行时契约

| 项目 | 决策 |
|---|---|
| World 所有权 | `Engine::PhysicsRuntime2D` 拥有 Box2D World 与 `PhysicsColliderAdapter` |
| Play 生命周期 | `EditorSession::Play` 创建运行时；`Stop`/回滚/析构销毁 |
| 固定步进 | `kFixedDt = 1/60s`，`kMaxSubSteps = 8`，单帧输入钳制到 `0.25s` |
| Collider-only | 保持 Static 碰撞区域 |
| RigidBody-only | 无 Collider 几何 → 不创建 body |
| RigidBody + Collider | body type 来自 `motionType`；shape/sensor/filter 来自 Collider |
| 初始位置/角度 | 来自 GameObject Transform；角度以弧度写入 `BodyDef` |
| Kinematic | 步进前 Transform → Body |
| Dynamic/Kinematic | 步进后 Body → Transform；Static 不回写 |
| 属性变更 | Collider / RigidBody mutation hook 触发显式重建 |
| 移除 RigidBody | 组件表 erase 前显式按 Static 重建（避免读到待移除组件） |
| 对象销毁 | GameObject 析构先触发组件 detach；`IsBeingDestroyed` 阻止重建 |
| 运行时生成对象 | 每帧刷新场景观察集合（Lua spawn 可被接管） |
| 失败 | 非法 dt/未运行返回 false + 诊断；Play 初始化失败不进入 Play |

### 2.2 实现差异

| 文件 | 差异 |
|---|---|
| `engine/include/Engine/Core/Physics/PhysicsRuntime2D.h` | 新增运行时接口 |
| `engine/src/Core/Physics/PhysicsRuntime2D.cpp` | World/Adapter 所有权、固定步进、观察刷新、Transform 同步 |
| `engine/include/Engine/Core/Physics/PhysicsColliderAdapter.h` | 新增 `BindGameObject`/`RebuildGameObject`/组合 `BuildBodyDef` |
| `engine/src/Core/Physics/PhysicsColliderAdapter.cpp` | RigidBody 组合、motionType switch、初始 Transform、RigidBody hook |
| `engine/include/Engine/Core/GameObject/RigidBodyComponent.h` | 新增 mutation hook（T-1-C 集成所需，不新增字段） |
| `engine/src/Core/GameObject/RigidBodyComponent.cpp` | setter/反射写入触发 hook |
| `engine/include/Engine/Core/GameObject/GameObject.h` | 新增 `IsBeingDestroyed()` 生命周期查询 |
| `engine/src/Core/GameObject/GameObject.cpp` | 析构开始置位 |
| `engine/src/Box2D/Box2DPhysicsWorld.cpp` | `CreateBody` 应用 `BodyDef.angle` → `bd.rotation` |
| `bridge/src/EditorSession.h/.cpp` | Play 创建运行时；Stop/回滚/析构释放；RuntimeTick 脚本后步进 |

明确未做：

- 未改 Collider schema/序列化；
- 未实现 fixedRotation/allowSleep/CCD/gravityScale；
- 未实现 friction/restitution/mass/density 作者字段；
- 未接入 ECS/Jolt；
- 未支持一个 GameObject 多个 Collider（当前契约单实例）。

### 2.3 门禁

`tests/test_physics/PhysicsRuntime2DTest.cpp`，10 个：

1. 动态刚体下落并停在地面（不穿透）。
2. Collider-only 保持 Static。
3. RigidBody-only 无 body。
4. Kinematic 跟随 Transform。
5. motionType 变更重建 body。
6. 移除 RigidBody 显式回退 Static。
7. 对象销毁不崩溃、body 解绑。
8. Stop/重启重建运行时。
9. 固定步进不受帧 delta 影响。
10. 非法 dt 清理失败并可恢复。

Bridge `T1CPlayStartsAndStopsPhysicsRuntime`：真实 Play → 5 次 RuntimeTick → Stop → 再 Play → Stop。

结果：Debug/Release 均 **10/10**；`test_physics` 全量 **71/71**；Bridge 该用例 **1/1**。

---

## 3. T-1-D：Lua API

**生产代码未改**。现有 `Engine.component.*` 的反射通道已覆盖 RigidBody；
未新增 `Engine.rigidbody.*` 或旁路物理域。

新增 `tests/test_scripting/ScriptingGameplayTest.cpp` 两个门禁：

- `T1D_ComponentContract_LuaRigidBody`：add/has/get/remove、默认值、
  `set_string("motionType")`、`set_number` damping、非法 motion/负阻尼拒绝。
- `T1D_LuaWriteRebuildsRuntimeBody`：Lua 将 Dynamic 改为 Static 后，
  `PhysicsRuntime2D` 重建 body，Transform 停止下落。

结果：`ScriptingGameplayTest.T1D*` **2/2**；`test_scripting` 全量 **29/29**（Debug/Release）。

---

## 4. T-1-E：Avalonia Inspector

### 4.1 实现

| 文件 | 差异 |
|---|---|
| `editor_avalonia/ViewModels/MainViewModel.cs` | `HasRigidBody`、Motion Type、两个 damping 字段、Add/Remove/Apply、RefreshRigidBody |
| `editor_avalonia/MainWindow.axaml` | RigidBody 区块（ComboBox + 两个 TextBox + 三按钮） |
| `editor_avalonia/MainWindow.axaml.cs` | Add/Remove/Apply handlers |
| `editor_avalonia/Services/ProductionPathGate.cs` | PP-08b：默认值、合法写回、非法 motion/damping 拒绝、Remove |

所有编辑走 `EditorHostService` 的通用组件 API，不接触物理后端。
`ApplyRigidBody` 在 UI 侧预校验 motionType 与有限非负 damping，避免部分写入。

### 4.2 证据

- `dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug/Release`：成功。
  构建摘要 **6 warnings**：2×NU1900、1×CS8602、3×CS0414（既有）。
- `AvaloniaEditor.exe --prodpath`：`[HRC-PROD-PATH GATE] ALL GREEN`，
  包含 PP-08b 与 PP-11 Play/Reload/Stop。
- T-1-E 阶段未做 managed UI 的“保存→关闭→重开”自动化；该缺口后来由 §10 的
  Managed UI Acceptance A（Debug/Release `--t1ui`）补做并通过。T-1-F 的原生 E2E
  仍只证明原生 `EditorSession` 路径，不表述为 managed UI E2E。

---

## 5. T-1-F：端到端闭环

### 5.1 场景与步骤

`EditorBridgeTest.T1FEndToEndPhysicsSaveReopenPlayStopReplay`：

1. 打开 scratch 工程；
2. 创建 `T1F_Ground`（Collider Box，5×0.5）与 `T1F_Body`
   （Collider Box，0.5×0.5，y=3，RigidBody Dynamic）；
3. SaveProject；
4. 导入 `t1f_physics.lua` 并 SaveProject；
5. 冷启动重新 OpenProject，确认 RigidBody 仍为 Dynamic；
6. Play 脚本，60 次 RuntimeTick：
   - Lua 读取 Transform Y 写入 `_PERSIST.y_milli`；
   - 断言 Y 在 (500, 3000) 毫米之间 → 真实下落且未穿透地面；
7. 继续到第 90 次 tick，Lua 将 RigidBody 改为 Static；
8. 再 tick 30 次，Y 保持不变 → Lua 写入真实重建 body；
9. Stop → 编辑态位置仍为 y=3（运行时未污染编辑态）；
10. SaveProject → OpenProject → 位置 y=3、motionType=Dynamic；
11. 再 Play → 新运行时从编辑态重建，30 tick 内 Y 再次下降；
12. Stop。

### 5.2 结果

- Debug/Release `EditorBridgeTest.T1F*`：**1/1**。
- `test_bridge` 全量：Debug **103/103**；Release 清理 scratch 后 **103/103**。
- 该用例使用现有 `EditorSession_RuntimePersistInt` 探针，没有新增 ABI。

---

## 6. 最终回归

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `test_physics` | **71/71** | **71/71** | `build/t1_final_test_physics_{debug,release}.log` |
| `test_scripting` | **29/29** | **29/29** | `build/t1_final_test_scripting_{debug,release}.log` |
| `test_bridge` | **103/103** | **103/103** | `build/t1_final_test_bridge_{debug,release}.log` |
| `test_content` | **108：102 通过 / 5 既有失败 / 1 skip** | 同左 | `build/t1_final_test_content_{debug,release}.log` |
| 完整 CTest | **16/17** | **16/17** | `build/t1_final_ctest_{debug,release}.log` |
| Managed `--prodpath` | **ALL GREEN** | — | `build/t1_final_prodpath_debug.log` |

既有 `test_content` 5 失败仍为 DF03×2、Dogfood05/06/07，未新增/改写。

### 构建警告

- 第一方全量 Debug/Release：各 **2×LNK4006**（既有 RHI 符号重复）。
- T-1-C 定向 Debug 构建另含 1 条 CMake Shaderc 未找到警告。
- Managed Debug/Release：各 **6 warnings**（2×NU1900、1×CS8602、3×CS0414）。
- 未做 clean rebuild；不宣称零警告/零失败。

### 测试夹具问题

`test_bridge` 首次 Release 全量曾因 scratch 目录被占用出现 9 个
`remove_all` 级联失败；清理测试生成的 `gp01_editor_scratch` 后复跑 103/103。
这是既有 scratch 隔离问题，T-1 未修改夹具掩盖它。

---

## 7. 已知限制 / 未验证项

1. **Managed UI 物理观测未验证（已接受范围例外）**：`EditorHostService.RuntimePersistInt` 在
   EngineHost 生产中明确 DEFERRED，没有 EngineHost equivalent；因此 managed UI
   无法读取运行时物理位置。T-1-F 的物理行为证据来自原生 `EditorSession` E2E，
   不能表述为 managed UI 物理 E2E。该缺口已作为 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`
   接受（见 §11），不表示 managed UI 物理 E2E 已通过。
2. **Managed UI 保存/关闭重开**：T-1-E 初始记录未自动化；后续 §10 的 `--t1ui`
   验收 A 已覆盖并通过（Debug/Release）。
3. **`enabled` 不持久化**：T-1-B 的已知限制；C++ 层关闭的 RigidBody 冷启后
   回到默认启用。
4. **物理作者字段**：`mass/density/friction/restitution/fixedRotation/allowSleep/
   CCD/gravityScale` 仍未进入契约；Adapter 仍使用 density=1、friction=0.3、
   restitution=0.2。
5. **`allowSleep` 既有缺陷**：`Box2DPhysicsWorld` 仍把它写成 `isAwake`；
   本阶段未修正。
6. **`fixedRotation` 既有 no-op**：MarioDemo 的 `fixedRotation=true` 在 Box2D
   路径仍无效。
7. **单个 Collider**：GameObject 组件模型单实例；多 Collider 未支持。
8. **初始速度/角度**：角度已接入；初始线速度/角速度仍未作为作者字段。
9. **T-1-A/B 源码未提交**：当前工作树包含全部 T-1 变更，无 commit 边界。
10. **F2-S1**：未修改、未关闭，也不能用 T-1 证据替代。

---

## 8. 基线保护

- T-2 关键文件 SHA-256 与 Stage 0 一致（RevisionStore/AtomicFile/ProjectPaths/
  Utf8Path/capi.h/T-2 关闭记录/test_revision_commit）。
- `crashes/` 仍为 115 个产物；3 个脏子模块保持原样。
- 未修改 T-2 Content 代码、导入服务或故障测试。
- 未执行 commit/push/tag/merge。

---

## 9. 裁决前结论（已被 §11 取代）

（裁决前记录）T-1-A～T-1-F 的实现与本地证据已完成，T-1 可作为
`IMPLEMENTATION COMPLETE (SCOPE-BOUND)` 提交最终裁决。以下两项必须先由裁决方接受：

- T-1-F 的 managed UI 物理观测为 `UNVERIFIED`，原生 EditorSession E2E 不能替代；
- `enabled` 不持久化及其他延期字段仍按 T-1-A/B 记录保留。

该限制已被 §11 的最终裁决取代：T-1 已关闭；#141、P1 及任何后续阶段仍按 §11 保持独立、未启动。

---

## 10. Managed UI Acceptance 复审补充（裁决前，2026-10-29）

- 验收项 A（真实 managed Inspector 配置、保存、关闭重开、字段恢复、Play/Stop
  后回填）：**PASS**，Debug/Release `--t1ui` 均 exit=0。
- 验收项 B（managed UI 对物理行为的确定性观测）：**UNVERIFIED**；
  EngineHost 的 `RuntimePersistInt` 仍为 DEFERRED，现有 Console/Inspector
  不提供运行时位置，未新增 Bridge ABI。
- 原生 T-1-F E2E 的物理证据仍然有效，但不能替代 managed UI 物理观测。
- 详细步骤、日志与关闭条件见 `docs/T-1-Managed-UI-Acceptance.md`。

以上为裁决前复审记录。裁决方已于 2026-10-29 接受 B 为范围例外，T-1 状态改为
`CLOSED — SCOPE-BOUND`；最终裁决见 §11。

---

## 11. T-1 Final Adjudication（2026-10-29）

以下为裁决方提供的最终封存文本；标题层级按本记录调整，文字未改。

### T-1 Final Adjudication

**Final status:** CLOSED — SCOPE-BOUND

### Accepted evidence

- T-1-A through T-1-F implementation and reported native runtime E2E evidence are accepted within their declared scope.
- Managed UI Acceptance A: PASS. The real `editor_avalonia --t1ui` gate passed in Debug and Release, covering Inspector configuration, Save, Close/Reopen, field restoration, Collider schema verification, and Play/Stop.
- Native `EditorSession` E2E: PASS within the native test path, covering dynamic falling, ground collision, Lua modification of motion type, Stop, reopen, and repeated Play.

### Managed UI Acceptance B

**Status:** UNVERIFIED — ACCEPTED SCOPE EXCEPTION.

The current managed production path does not provide a deterministic runtime Transform/physics observation channel suitable for this test. `EditorHostService.GetRuntimePersistInt` remains deferred; the managed Console does not project runtime Transform state; the current Viewport cannot provide a deterministic position assertion for the test body.

This exception does not mean that managed UI physical E2E passed. Native `EditorSession` evidence must not be represented as equivalent managed UI evidence.

### Scope boundary

- No new Bridge ABI or EngineHost runtime diagnostic capability is added as part of T-1.
- A future managed runtime-observability requirement must be proposed and evaluated as a separately scoped task.
- F2-S1 remains independent and is not closed by T-1.
- Existing test failures, build warnings, and other declared limitations remain documented.

### Git and baseline

- No commit, push, tag, or merge was performed.
- Existing dirty submodules and unrelated worktree artifacts remain untouched.

**Adjudication basis:** The reported test results and implementation records supplied for review. No independent repository inspection or test execution is claimed.
