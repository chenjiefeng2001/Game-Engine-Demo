# UI-H2 P1-C — Dark Token & Surface Unification 关闭记录

**状态**：`UI-H2 P1-C — PASS — SCOPE-BOUND / CLOSED`
**日期**：2026-10-29
**范围**：全局 Dark 基线、语义资源、窗口/面板/Tab/列表/输入/状态栏表面统一。
**未执行**：P2 图标/动画/细节美化、完整排版 token 迁移、Nova Dock、T-6-A 行为/Console
逻辑、Bridge ABI、Engine Core；未 commit/push/tag/merge。

---

# UI-H2 P1-C — Final Acceptance

**Decision:** PASS — SCOPE-BOUND

**Scope:** Dark Token & Surface Unification

## Accepted

- Dark Theme baseline and centralized semantic resources in `App.axaml`.
- Window, menu, toolbar, panels, tabs, lists, inputs, splitters and status bar
  unified on the dark semantic surfaces.
- Real-window P1-C gate passed in Debug and Release, including contrast checks.
- P1-A/P1-B/H2-A, T-6-A Console, managed production-path gates, #141 UIA and
  `test_bridge` remained green.

## Deferred

- P2 visual details (implemented separately; later accepted as `PASS — SCOPE-BOUND / CLOSED` on 2026-10-30).
- Full typography/spacing token migration beyond the P1-C surface layer.
- Light theme is out of scope.

## Boundaries

- UI-H1 remains FROZEN.
- T-6-A behavior remains unchanged.
- No Engine, Bridge or Console behavior changes.
- No commit, push, tag, or merge was performed.

**Next state:** UI-H2 P1-C CLOSED — SCOPE-BOUND. P2 proceeds under its own record.

---

## 1. 实施内容

### 1.1 Dark 基线与语义资源

`editor_avalonia/App.axaml`：

- `RequestedThemeVariant="Dark"`，本期唯一视觉基线。
- `Application.Resources` 新增语义令牌：
  `EditorCanvasBrush`、`PanelSurfaceBrush`、`PanelHeaderBrush`、`BorderSubtleBrush`、
  `TextPrimaryBrush`、`TextSecondaryBrush`、`AccentBrush`/Hover/Pressed、
  `SelectionBrush`、`WarningBrush`、`ErrorBrush`、`InputSurfaceBrush`、
  `StatusBarBrush`、`WhiteBrush`。
- `Application.Styles` 新增全局控件规则：Window、Menu、Button（含 primary/danger/
  disabled/focus）、TextBox、ComboBox、ListBox/ListBoxItem（selected/hover）、
  TabControl/TabItem、GridSplitter。
- P1-B 的局部 `Window.Styles` 与 P1-B 的 TabControl 局部样式已收敛到 App 级资源。

### 1.2 表面接线

`editor_avalonia/MainWindow.axaml`：

- Window 背景/前景、Menu、工具栏、状态栏使用语义资源。
- Hierarchy / Inspector / Viewport / TabControl / Console/Asset/Script Tab 表面
  统一为 PanelSurface / EditorCanvas；边框统一为 BorderSubtle。
- Console 过滤框/Clear、TextBlock 危险/警告色、Script 空态、Inspector 文本等
  改为动态资源。
- ListBox 背景/前景统一为 PanelSurface/TextPrimary；选中态使用 SelectionBrush。
- 三个 GridSplitter 使用 BorderSubtle 并降低视觉存在感（Opacity 0.2）；
  Fluent 模板的拖动手柄细节仍留 P2。
- 不再有页面级硬编码色值（MainWindow 中仅语义资源引用）。

### 1.3 测试入口

- 新增 `editor_avalonia/Services/UiH2P1CGate.cs`。
- `MainWindow.axaml.cs` 增加 `--uih2p1c` 最小注册。
- 新增 `tools/run_uih2p1c_gate.ps1`、`build/uih2p1c_capture.ps1`。
- 未改 `MainViewModel.cs`、`EditorHostService.cs`、T-6-A 生产行为。

---

## 2. P1-C 门禁结果

`UiH2P1CGate` 真实窗口断言：

- `Application.RequestedThemeVariant == Dark`；
- Window 背景/前景为暗/亮且对比度 ≥ 4.5:1；
- Hierarchy/Asset/Console/Inspector/三个 Tab 表面/StatusBar 均不透明且为暗表面；
- hierarchy/console/filter/script empty/status 前景为亮色；
- console/filter 文本对比度 ≥ 4.5:1；
- 选中 Tab 为暗表面 + 亮文字；
- 900/1100/1440/1920 下主题表面保持一致。

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug `--uih2p1c` | exit=0，全部断言通过 | `build/uih2p1c_gate_Debug.log` |
| Release `--uih2p1c` | exit=0，全部断言通过 | `build/uih2p1c_gate_Release.log` |
| Debug 构建 | 0 error，6 warnings（2×NU1900、1×CS8602、3×CS0414 既有） | `build/uih2p1c_final_build_debug.log` |
| Release 构建 | 0 error，6 warnings（同上） | `build/uih2p1c_final_build_release.log` |

### 截图

Before 为 P1-B 验收截图副本（`docs/evidence/UI-H2-P1C/before/`）；After 位于
`docs/evidence/UI-H2-P1C/after/`。

| 文件 | 观察 |
|---|---|
| `1100x720_running_toolbar.png` | 全暗表面；运行 badge/primary 操作/状态栏统一 |
| `1100x720_asset_browser.png` | Tab、列表、工具栏、Inspector 均为暗表面；选中行深蓝 |
| `900x600_script_empty.png` | Script 空态、Tab 标题、Inspector 在 Dark 下可读 |
| `1920x1080_inspector_scrolled.png` | 全暗布局；Viewport 1:1 居中 + letterbox |
| `1440x900_*` | 同上，比例稳定 |

---

## 3. 回归结果

| 项目 | Debug | Release | 日志 |
|---|---|---|---|
| `--uih2p1b` | exit=0 | exit=0 | `build/uih2p1b_gate_Debug.log` / `..._Release.log` |
| `--uih2p1a` | exit=0 | exit=0 | `build/uih2p1a_gate_Debug.log` / `..._Release.log` |
| `--uih2a` | exit=0 | exit=0 | `build/uih2a_gate_Debug.log` / `..._Release.log` |
| `--t6console` | exit=0 | exit=0 | `build/t6console_gate_Debug.log` / `..._Release.log` |
| T-6 UIA | 1/1 PASS | 1/1 PASS | `build/t6ui_test_Debug.log` / `..._Release.log` |
| `--prodpath` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1c_reg_prodpath_debug.log` / `..._release.log` |
| `--t1ui` | exit=0，PASS | exit=0，PASS | `build/uih2p1c_reg_t1ui_debug.log` / `..._release.log` |
| `--hrc2` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1c_reg_hrc2_debug.log` / `..._release.log` |
| #141 UIA（M1 + T6） | 2/2 PASS，exit=0 | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log` / `..._Release.log` |
| `test_bridge` | 103/103 PASS | 103/103 PASS | `build/uih2p1c_test_bridge_debug.log` / `..._release.log` |

T-6-A 的容量、清空、过滤、工程分段由 `--t6console` 覆盖并通过；Console 数据模型/行为
未改。

---

## 4. 未做 / 限制

- **P2 未做**：图标统一、动画、细节间距、空态/分隔线精修。
- 未完整迁移所有面板字号/间距 token；P1-C 只完成颜色/表面/基础状态统一。
- GridSplitter/ScrollBar 的 Fluent 模板细节仍有默认视觉；P1-C 只统一了背景与透明度。
- 强制 Dark 基线；Light 主题不在本期范围。
- Tab 内容与列表已暗化；长时间/高密度信息下的排版密度仍待 P2。
- P1-A/B/C 均待裁决方最终验收；P2 不因本轮自动授权。
  （历史记录；已由 2026-10-30 P2 最终裁决取代：P1-A/B/C/P2 均为
  `PASS — SCOPE-BOUND / CLOSED`，`UI-H2 — CLOSED — SCOPE-BOUND`。）
- T-6 UIA helper 的顶部+底部读取调整仍属 P1-A 记录中的测试 harness 修正；断言未变。

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

P1-C 的全暗基线、语义资源与主要表面统一已在真实窗口和四尺寸验证；P1-A/P1-B/
H2-A、T-6-A 与全部既有 managed/UIA/Bridge 回归通过。裁决方已确认
`UI-H2 P1-C — PASS — SCOPE-BOUND`，本阶段关闭；P2 另行执行。
