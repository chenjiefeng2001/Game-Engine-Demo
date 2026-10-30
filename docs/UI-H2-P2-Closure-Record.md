# UI-H2 P2 — Visual Details & Empty States 关闭记录

**状态**：`UI-H2 P2 — PASS — SCOPE-BOUND / CLOSED`
**阶段状态**：`UI-H2 — CLOSED — SCOPE-BOUND`（P0/P1/P2 全部关闭）
**日期**：2026-10-29（最终裁决 2026-10-30）
**范围**：空态、Console 内容密度、Gizmo 图标、细节间距与分隔线。
**未执行**：Nova Dock、Engine/Bridge、T-6-A 行为/Console 逻辑、Bridge ABI；
未 commit/push/tag/merge。

---

## 1. 实施内容

### 1.1 面板空态

`MainViewModel` 新增只读投影：
`HasEntities`、`HasAssets`、`HasConsoleLines`，并在集合刷新/清理与 Console 写入/清空时
发出通知。

`MainWindow.axaml` 新增：

- `HierarchyEmptyState`：`(no entities)`，工程关闭/无实体时显示；
- `AssetEmptyState`：`(no assets)`，无资产时显示；
- `ConsoleEmptyState`：`(console is empty)`，清空后或初始无日志时显示；
- 空态使用 `TextSecondaryBrush`、居中、`IsHitTestVisible=False`，不遮挡列表交互；
- Script Editor 与 Inspector 原有空态保留并纳入统一表面。

### 1.2 Console 内容密度

- `ConsoleList` 使用 `Classes="console"`。
- App 级样式：Console 行 `Padding=8,2`、`MinHeight=22`、等宽字体、`FontSize=12`。
- 保持 T-6-A 的 10,000 行容量、过滤、清空与工程分段逻辑不变。

### 1.3 Gizmo 图标

- Q/W/E/R/G 文本替换为统一 glyph 图标：
  `⌖` Select、`✥` Translate、`⟳` Rotate、`⤢` Scale、`▦` Grid。
- 保留 28×28 尺寸、ToolTip 与原有 Click handler；新增 `gizmo` 样式
  （Segoe UI Symbol，16px，零 padding）。
- 未引入第三方图标库或外部资源。

### 1.4 细节间距 / 分隔

- Hierarchy/Inspector 面板 padding 统一为 8；标题下间距 8；Camera/RigidBody 分组
  间距 12/8；Tab 工具栏 margin 8,4、spacing 8。
- 工具栏分组分隔线、Splitter 背景继续使用 `BorderSubtleBrush`。
- 未做全量字号/间距 token 迁移；P2 只处理已识别的细节。

### 1.5 测试入口

- 新增 `editor_avalonia/Services/UiH2P2Gate.cs`。
- `MainWindow.axaml.cs` 增加 `--uih2p2` 最小注册。
- 新增 `tools/run_uih2p2_gate.ps1`、`build/uih2p2_capture.ps1`。
- P2 修改了 `MainViewModel` 的只读投影属性（空态需要），未改 host/Console 行为。

---

## 2. P2 门禁结果

`UiH2P2Gate` 真实窗口断言：

- 打开工程后 HasEntities/HasAssets/HasConsoleLines 为 true，三个空态隐藏；
- `ConsoleList` 使用 `console` class，实际 ListBoxItem 为 12px、行高 ≥22；
- 五个 Gizmo 按钮使用 `gizmo` class 且包含 glyph；
- 真实 Clear 按钮清空后 ConsoleEmptyState 可见；
- 真实 `+ Entity` 写入日志后 ConsoleEmptyState 隐藏；
- `ResetSession` 后 HasEntities/HasAssets 为 false，Hierarchy/Asset 空态可见。

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug `--uih2p2` | exit=0，全部断言通过 | `build/uih2p2_gate_Debug.log` |
| Release `--uih2p2` | exit=0，全部断言通过 | `build/uih2p2_gate_Release.log` |
| Debug 构建 | 0 error，6 warnings（2×NU1900、1×CS8602、3×CS0414 既有） | `build/uih2p2_final_build_debug.log` |
| Release 构建 | 0 error，6 warnings（同上） | `build/uih2p2_final_build_release.log` |

### 截图

Before 为 P1-C 验收截图副本（`docs/evidence/UI-H2-P2/before/`）；After 位于
`docs/evidence/UI-H2-P2/after/`。

| 文件 | 观察 |
|---|---|
| `1100x720_console_empty.png` | 清空后 Console 空态、Console 密度、Gizmo 图标 |
| `1100x720_running_toolbar.png` | 运行 badge、primary 操作、暗表面 |
| `1100x720_asset_browser.png` | 资产列表密度、选中态、Tab 表面 |
| `900x600_script_empty.png` | Script 空态与紧凑窗口 |
| `1920x1080_inspector_scrolled.png` | Viewport 居中、Inspector 滚动、全暗表面 |

---

## 3. 回归结果

| 项目 | Debug | Release | 日志 |
|---|---|---|---|
| `--uih2p1c` | exit=0 | exit=0 | `build/uih2p1c_gate_Debug.log` / `..._Release.log` |
| `--uih2p1b` | exit=0 | exit=0 | `build/uih2p1b_gate_Debug.log` / `..._Release.log` |
| `--uih2p1a` | exit=0 | exit=0 | `build/uih2p1a_gate_Debug.log` / `..._Release.log` |
| `--uih2a` | exit=0 | exit=0 | `build/uih2a_gate_Debug.log` / `..._Release.log` |
| `--t6console` | exit=0 | exit=0 | `build/t6console_gate_Debug.log` / `..._Release.log` |
| T-6 UIA | 1/1 PASS | 1/1 PASS | `build/t6ui_test_Debug.log` / `..._Release.log` |
| `--prodpath` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p2_reg_prodpath_debug.log` / `..._release.log` |
| `--t1ui` | exit=0，PASS | exit=0，PASS | `build/uih2p2_reg_t1ui_debug.log` / `..._release.log` |
| `--hrc2` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p2_reg_hrc2_debug.log` / `..._release.log` |
| #141 UIA（M1 + T6） | 2/2 PASS，exit=0 | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log` / `..._Release.log` |
| `test_bridge` | 103/103 PASS | 103/103 PASS | `build/uih2p2_test_bridge_debug.log` / `..._release.log` |

T-6-A 的容量、清空、过滤、工程分段仍由 `--t6console` 覆盖并通过。

---

## 4. 未做 / 限制

- Gizmo 使用系统 glyph，不是完整自绘图标库；不同 Windows 字体渲染可能略有差异。
- GridSplitter/ScrollBar 仍使用 Fluent 模板的默认手柄细节（P2 仅统一背景/透明度）。
- 未完成全量字号/间距 token 迁移与所有面板空态；已覆盖主要空态。
- 未引入动画/过渡；P2 不改变交互语义。
- P1-A/P1-C/P2 的最终验收已由本轮裁决确认；UI-H2 不自动扩展其他阶段。
- T-6 UIA helper 的顶部+底部读取修正仍有效，断言未变。

### 基线

| 项 | 观测 |
|---|---|
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8`（`avalonia`） |
| T-2 八文件 SHA-256 | 全部 MATCH（含 `capi.h` = `02dbce31…b2886`） |
| `crashes/` | 115 个产物，未触碰 |
| 脏子模块 | `third_party/d3d12ma`、`openal-soft`、`vma`，未触碰 |
| Git 写入 | 无 commit/push/tag/merge |

---

## 5. 最终验收

**裁决**：`UI-H2-P2 — PASS — SCOPE-BOUND / CLOSED`；
`UI-H2 — CLOSED — SCOPE-BOUND`（P0/P1/P2 全部关闭）。

**已接受**：

- P2 的空态（Hierarchy / Asset / Console）、Console 内容密度、Gizmo glyph 图标与
  细节间距/分隔线；
- P1-A 自适应布局与 Viewport 1:1 居中 letterbox、P1-B 工具栏分组与状态统一、
  P1-C 全局 Dark token 与表面统一；
- H2-A 的 P0 修复与 T-6-A 保持不变的 Console 行为；
- P2 门禁与最终回归结果（§2、§3），以 Debug/Release 分开记录。

**已知限制（接受，不阻塞）**：

- Gizmo 使用系统 glyph，不同 Windows 字体环境渲染可能略有差异；
- GridSplitter/ScrollBar 仍沿用 Fluent 模板手柄细节，仅统一了背景/透明度；
- 全量字号/间距 token 迁移与动画/过渡未在本期完成；
- 1920×1080 下工作区利用率与 Viewport 细节仍可在未来单独评估。

**受保护边界**：

- `UI-H1 — FROZEN` 不变；
- T-6-A 功能契约与 Console 行为不变（容量、清空、过滤、工程分段）；
- T-6-B（Engine `spdlog` 接入）保持独立延后；
- 不新增 Bridge ABI；
- 不引入 Nova Dock；
- 本轮无 commit/push/tag/merge，无 Git 写入。

**后续约束**：不自动授权新一轮 UI 改版。下一阶段应先根据实际使用体验重新确定
需求，而不是为了继续优化而无限扩大 UI-H2 的范围。

> 最终裁决原文：“最终裁决：批准 UI-H2 本期 P0/P1/P2 全部关闭。本轮不执行 Git 写入，
> 不修改 T-6-A 的功能契约，也不自动授权新一轮 UI 改版。下一阶段应先根据实际使用
> 体验重新确定需求，而不是为了继续优化而无限扩大本次 UI-H2 的范围。”

---

## 6. 结论

P2 的空态、Console 密度、Gizmo 图标与细节间距已在真实窗口/四尺寸验证；
P1-A/B/C、H2-A、T-6-A 与既有 managed/UIA/Bridge 回归均通过。经最终裁决，
`UI-H2-P2 — PASS — SCOPE-BOUND / CLOSED`，`UI-H2 — CLOSED — SCOPE-BOUND`。

