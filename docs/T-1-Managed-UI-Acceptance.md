# T-1 Managed UI Acceptance（复审补充）

**状态**：`T-1 — CLOSED — SCOPE-BOUND`；Managed UI 物理观测 B 为
`UNVERIFIED — ACCEPTED SCOPE EXCEPTION`（2026-10-29 最终裁决）。
**日期**：2026-10-29
**Git**：未 commit / push / tag / merge。

本记录只补裁决要求的窄范围 managed UI 验收，不重开已通过的阶段，
不新增 Bridge ABI，不修改物理运行时。

---

## 1. 验收项 A：Inspector 与持久化 — PASS

新增 `editor_avalonia/Services/T1ManagedAcceptanceGate.cs`，
通过 `--t1ui` 启动真实 `editor_avalonia` 窗口，使用临时 scratch 工程
（复制 `assets/gp01`，不写 repo fixture）。

实际步骤：

1. `MainViewModel.OpenProjectRooted(scratch, "assets/gp01/manifest.json",
   "assets/gp01/Main.scene")` 打开 scratch 工程；
2. 通过 `CreateEntity` 创建 `T1UI_Body`，等待生产事件/pump 刷新后选中；
3. 通过 Host 通用组件 API 添加 Collider 并设置 `shape=Box`（schema 对照）；
4. 点击 `AddRigidBodyBtn`；
5. 在 Inspector 设置 `Motion Type=Kinematic`、`Linear Damping=0.25`、
   `Angular Damping=0.5`，点击 `ApplyRigidBodyBtn`；
6. `SaveProject()`；
7. `ResetSession()` 关闭后重新 `OpenProjectRooted`；
8. 重新选中 `T1UI_Body`，检查 `HasRigidBody` 与三个字段回填；
9. 检查 Collider 的 `shape` 仍为 `Box`；
10. `PlayScript(-1)` → 生产 pump 运行 → `StopPlaying()`；
11. 再次检查 `HasRigidBody` 与 `motionType` 未被 Play/Stop 改写。

结果：

| 配置 | 结果 | 日志 |
|---|---|---|
| Managed Debug `--t1ui` | **PASS**（exit=0） | `build/t1ui_gate_debug3.log` |
| Managed Release `--t1ui` | **PASS**（exit=0） | `build/t1ui_gate_release.log` |

证明范围：

- 真实 Avalonia 窗口、真实 Inspector 按钮/字段、真实 `EditorHostService` 通用组件通道；
- 保存 → 关闭 → 重新打开后三字段与 Collider 数据仍在；
- Play/Stop 后编辑态组件状态未被运行时替换。

不证明：

- managed Inspector 的非法输入对话框/手工交互（PP-08b 已覆盖非法 motion/damping 拒绝）；
- managed UI 物理行为观测（见 B）。

---

## 2. 验收项 B：物理效果 managed 观测 — UNVERIFIED — ACCEPTED SCOPE EXCEPTION

### 2.1 现有能力核对

- `EditorHostService.GetRuntimePersistInt` 在 EngineHost 生产路径中明确
  **DEFERRED**（`editor_avalonia/Services/EditorHostService.cs:487-493`），
  没有 EngineHost equivalent；
- managed Console 只投影 `EV_*` 事件（Entity/Asset/Project/Play 等），
  不包含运行时 Transform 或物理位置；
- Inspector 只读编辑态场景；Play 运行态没有位置回显；
- 现有 Viewport 渲染不能对“无 Sprite 的测试刚体”提供确定性位置断言。

### 2.2 裁决要求的边界

按复审意见：

- 不为了测试临时新增 Bridge ABI；
- 不把原生 `EditorSession` 的物理结果说成 managed UI E2E；
- 缺口必须记录，并说明关闭它是否需要正式运行时诊断能力。

### 2.3 结论

`T-1-F` 的原生 E2E 已证明物理运行时行为（保存/冷启/下落/碰撞/Lua 改 Static/
Stop/重放）；**managed UI 物理观测仍为 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`**。

关闭该缺口需要以下之一，且必须单独授权：

1. 在 EngineHost 生产路径提供正式的运行时位置/状态查询（不是测试专用 ABI）；
2. 或提供稳定的 UI 运行时观测面（例如运行时 Transform 面板/可断言的 HUD 文本）。

本轮不实现上述能力；裁决方已于 2026-10-29 明确接受 `UNVERIFIED` 作为范围例外，
T-1 据此关闭。该例外不表示 managed UI 物理 E2E 已通过，也不把原生 `EditorSession`
E2E 等价为 managed UI 证据。

---

## 3. 构建与基线

- Managed Debug/Release：构建成功，摘要 **6 warnings**
  （2×NU1900、1×CS8602、3×CS0414，既有）。
- 未新增或修改 Bridge ABI；未改物理运行时、序列化、Collider schema。
- 未写 repo fixture；`--t1ui` 只使用系统临时目录 scratch 并在 finally 删除。
- 暂存区为空；未执行 Git 写操作。
- T-2 关键文件哈希与 Stage 0 一致；`crashes/` 仍为 115 个产物；
  3 个脏子模块保持原样。

---

## 4. 复审结论建议

| 项目 | 结果 |
|---|---|
| Managed UI Inspector + 保存/重开 + Play/Stop（A） | **PASS** |
| Managed UI 物理行为观测（B） | **UNVERIFIED — ACCEPTED SCOPE EXCEPTION** |
| 原生 T-1-F E2E | **PASS（已记录）** |
| T-1 最终关闭 | **CLOSED — SCOPE-BOUND；B 为已接受范围例外** |

建议保留 T-1-C～T-1-F 实现与原生 E2E，不回退、不扩大实现范围。
裁决方已接受 B 为显式范围例外，并据此关闭 T-1；本阶段不新增 Bridge ABI 或
EngineHost 运行时诊断能力。若未来需要 managed 运行时观测，应作为独立范围的任务
重新提出与评估，不在本次 T-1 封存中启动。
