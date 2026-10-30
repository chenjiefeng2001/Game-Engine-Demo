# UI-H2 P1-B — Toolbar Grouping & State Unification 关闭记录

**状态**：`UI-H2 P1-B — PASS — SCOPE-BOUND / CLOSED`
**日期**：2026-10-29
**范围**：工具栏分组/主次/危险操作、运行态 badge、选中/悬停/禁用/focus 状态统一。
**未执行**：P1-C 全局 Dark token、Tab/Console 全表面统一、完整排版体系、P2、
Nova Dock、T-6-A 行为/Console 逻辑、Bridge ABI、Engine Core；未 commit/push/tag/merge。

---

# UI-H2 P1-B — Final Acceptance

**Decision:** PASS — SCOPE-BOUND

**Scope:** Toolbar Grouping & State Unification

## Accepted

- Toolbar grouped into global/edit/transport actions with visible separators.
- Developer `[P3-D]` hint removed from the production toolbar.
- Primary Play, destructive Delete/Remove actions, runtime badges, and selected/
  hover/disabled/focus state styles verified in a real window.
- P1-B gate passed in Debug and Release; H2-A/P1-A gates, T-6-A Console, managed
  production-path gates, #141 UIA, and `test_bridge` remained green.

## Deferred

- Global Dark Theme tokens and full surface unification (P1-C).
- Complete typography/spacing hierarchy and error/warning banners (P1-C/P2).
- Splitter persistence and further responsive refinements.

## Boundaries

- UI-H1 remains FROZEN.
- T-6-A behavior remains unchanged.
- No Engine, Bridge, MainViewModel, or EditorHostService changes were authorized
  for P1-B.
- No commit, push, tag, or merge was performed.

**Next state:** UI-H2 P1-B CLOSED — SCOPE-BOUND. P1-C requires separate implementation
authorization.

---

## 1. 实施内容

### 1.1 工具栏

`editor_avalonia/MainWindow.axaml`：

- 工具栏拆为三组：`ToolbarGlobalGroup`（Open/Save）、`ToolbarEditGroup`（+ Entity）、
  `ToolbarTransportGroup`（Play/Reload/Stop），组间使用可见分隔线。
- 移除常驻开发者提示 `[P3-D] Save → Play ...`。
- `Play` 使用 `Classes="primary"`（Accent 背景），是工具栏唯一主操作；
  Play/Stop 内容翻转与既有 `SyncPlayButton` 行为保持不变。
- Reload/Stop 保持 `IsEnabled="{Binding IsRunning}"`，并新增 ToolTip。
- 增加 `RunningBadge`（`PLAYING`）与状态栏 `RunningStatusBadge`（`● RUNNING`），
  仅在 `IsRunning` 时显示。
- 为 Delete/Remove Camera/Remove RigidBody 增加 `Classes="danger"`。

### 1.2 状态样式

新增 `Window.Styles`（局部，不建立全局 Dark token）：

| 选择器 | 规则 |
|---|---|
| `Button:disabled` | Opacity 0.55 |
| `Button:focus` | Accent 边框 |
| `Button.primary` / `:pointerover` / `:pressed` | Accent 主操作三级状态 |
| `Button.danger` | Error 文本/边框 |
| `ListBoxItem:selected` 与模板 ContentPresenter | `#094771` 背景 + 浅色前景 |
| `ListBoxItem:pointerover` | 浅色 hover 表面 |
| `TextBox:focus` | Accent 边框 |

选中态现在在 Hierarchy/Asset/Console 列表统一使用深蓝选择背景；运行态由工具栏与
状态栏双 badge 表达；禁用按钮使用统一透明度；危险操作统一使用 Error 色。

### 1.3 测试入口

- 新增 `editor_avalonia/Services/UiH2P1BGate.cs`。
- `MainWindow.axaml.cs` 增加 `--uih2p1b` 最小注册。
- 新增 `tools/run_uih2p1b_gate.ps1`、`build/uih2p1b_capture.ps1`。
- 未改 `MainViewModel.cs`、`EditorHostService.cs`、T-6-A 生产行为。

---

## 2. P1-B 门禁结果

`UiH2P1BGate` 真实窗口打开 scratch 工程并断言：

- 三个工具栏组有布局且顺序为 global → edit → transport；
- 工具栏不存在 `[P3-D]` 文本；
- Play 为 primary，Delete/Remove Camera/Remove RigidBody 为 danger；
- 初始 Play 内容为 `Play`，Reload/Stop 禁用，运行 badge 隐藏；
- 点击 Play 后 `IsRunning`、Play 内容变 `Stop`、两个 running badge 可见、
  Reload/Stop 启用；
- 点击 Stop 后状态全部恢复；
- 真实执行一次 `+ Entity` 后 DirtyBadge 可见。

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug `--uih2p1b` | exit=0，全部断言通过 | `build/uih2p1b_gate_Debug.log` |
| Release `--uih2p1b` | exit=0，全部断言通过 | `build/uih2p1b_gate_Release.log` |
| Debug 构建 | 0 error，6 warnings（2×NU1900、1×CS8602、3×CS0414 既有） | `build/uih2p1b_build_Debug.log` |
| Release 构建 | 0 error，6 warnings（同上） | `build/uih2p1b_build_Release.log` |

### 截图

Before 为 P1-A 验收截图副本（`docs/evidence/UI-H2-P1B/before/`）；After 位于
`docs/evidence/UI-H2-P1B/after/`。

| 文件 | 观察 |
|---|---|
| `1100x720_running_toolbar.png` | Play 变为蓝色 `Stop`；`PLAYING` 工具栏 badge 与 `● RUNNING` 状态栏 badge；Reload/Stop 启用 |
| `1100x720_stopped_toolbar.png` | Play 恢复蓝色 Play；badge 消失；Reload/Stop 禁用 |
| `1100x720_console.png` | 三组工具栏与分隔线；Hierarchy 选中为深蓝/浅字；Delete/Remove 使用危险色 |
| `900x600_*` / `1440x900_*` / `1920x1080_*` | 工具栏与状态 badge 在各尺寸保持；无布局重叠 |

---

## 3. 回归结果

| 项目 | Debug | Release | 日志 |
|---|---|---|---|
| `--uih2p1a` | exit=0 | exit=0 | `build/uih2p1a_gate_Debug.log` / `..._Release.log` |
| `--uih2a` | exit=0 | exit=0 | `build/uih2a_gate_Debug.log` / `..._Release.log` |
| `--t6console` | exit=0 | exit=0 | `build/t6console_gate_Debug.log` / `..._Release.log` |
| T-6 UIA | 1/1 PASS | 1/1 PASS | `build/t6ui_test_Debug.log` / `..._Release.log` |
| `--prodpath` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1b_reg_prodpath_debug.log` / `..._release.log` |
| `--t1ui` | exit=0，PASS | exit=0，PASS | `build/uih2p1b_reg_t1ui_debug.log` / `..._release.log` |
| `--hrc2` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1b_reg_hrc2_debug.log` / `..._release.log` |
| #141 UIA（M1 + T6） | 2/2 PASS，exit=0 | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log` / `..._Release.log` |
| `test_bridge` | 103/103 PASS | 103/103 PASS | `build/uih2p1b_test_bridge_debug.log` / `..._release.log` |

T-6-A 契约由 `--t6console` 覆盖并通过：容量、清空、过滤、工程分段未回归。
P1-A 的布局/Splitter/Viewport 居中门禁仍通过。

---

## 4. 未做 / 限制

- **P1-C 未做**：全局 Dark token/资源字典；Tab 头部仍是 Fluent 默认浅色表面；
  Console/Asset/Inspector 列表仍为默认白色；深浅表面仍混用。
- **排版体系未完成**：除工具栏/状态 badge 外，未统一所有面板字号与间距。
- 错误/警告没有新增独立 banner；沿用现有 ScriptError、Console 文本、Dirty 色。
- `ListBoxItem:pointerover` 在白色列表上使用浅蓝 hover；与 P1-C 的 Dark 表面统一后需复核。
- Focus 样式依赖控件模板；截图与门禁覆盖主路径，未逐控件做像素级断言。
- Splitter 尺寸不持久化（P1-A 既有限制）。
- P1-A/P1-B 均仍待裁决方验收；P1-C/P2 不因本轮自动授权。
  （历史记录；已由 2026-10-30 P2 最终裁决取代：P1-A/P1-B/P1-C/P2 均为
  `PASS — SCOPE-BOUND / CLOSED`，`UI-H2 — CLOSED — SCOPE-BOUND`。）

### 基线

| 项 | 观测 |
|---|---|
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8`（`avalonia`） |
| T-2 八文件 SHA-256 | 全部 MATCH（含 `capi.h` = `02dbce31…b2886`） |
| `crashes/` | 115 个产物，未触碰 |
| 脏子模块 | `third_party/d3d12ma`、`openal-soft`、`vma`，未触碰 |
| Git 写入 | 无 commit/push/tag/merge |

---

## 5. 结论

P1-B 的工具栏分组、主/危险操作、运行态 badge 与选中/悬停/禁用/focus 基础状态
已在真实窗口验证；P1-A/H2-A/T-6-A 及全部既有门禁无回归。裁决方已确认
`UI-H2 P1-B — PASS — SCOPE-BOUND`，本阶段关闭；P1-C 另行执行。
