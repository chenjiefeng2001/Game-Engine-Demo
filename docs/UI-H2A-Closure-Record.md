# UI-H2-A — P0 Layout & Accessibility Repair 关闭记录

**状态**：`UI-H2-A — PASS — SCOPE-BOUND / CLOSED`；`UI-H1 — FROZEN`；`UI-H2 P1/P2 — NOT AUTHORIZED`（历史状态；P1/P2 已于 2026-10-30 全部关闭，`UI-H2 — CLOSED — SCOPE-BOUND`）
**日期**：2026-10-29
**范围**：仅 H2-A1 Grid 重叠、H2-A2 Tab/Script 可读性、H2-A3 Inspector 可达性。
**未执行**：P1/P2 视觉优化、Splitter/MinSize、Nova Dock、T-6-A 行为/Console 逻辑改动、
Bridge ABI、Engine Core；未 commit/push/tag/merge。

---

# UI-H2-A — Final Acceptance

**Decision:** PASS — SCOPE-BOUND

**Scope:** P0 Layout & Accessibility Repair

## Accepted

- H2-A1: Grid row/span conflicts corrected so Viewport and Inspector no longer overlap the bottom TabControl.
- H2-A2: Local opaque Tab content surfaces and readable Script Editor empty-state styling implemented.
- H2-A3: Inspector content made vertically scrollable, preserving access to component fields and Apply actions.
- Real-window geometry and scrolling gates passed at 900×600, 1100×720, 1440×900, and 1920×1080.
- Reported regressions passed in Debug and Release, including T-6-A Console behavior, managed production-path gates, #141 UIA, and `test_bridge`.

## Deferred

- Splitters, minimum dimensions, and broader responsive layout.
- Global Dark Theme tokens and toolbar hierarchy.
- Viewport centering and letterbox refinement at 1920×1080.
- Full Console visual restyling.

## Boundaries

- UI-H1 remains FROZEN.
- T-6-A behavior remains unchanged.
- No changes to Engine, Bridge, MainViewModel, or EditorHostService were authorized under this slice.
- No commit, push, tag, or merge was performed.

**Next state:** UI-H2-A CLOSED — SCOPE-BOUND. UI-H2 P1 remains NOT AUTHORIZED and requires separate approval.
（历史记录；P1/P2 已于 2026-10-30 全部关闭，`UI-H2 — CLOSED — SCOPE-BOUND`。）

**Evidence basis:** Implementation and test outcomes reported for review; no independent repository inspection or test execution is claimed.

---

## 1. 实施内容

### H2-A1 — Grid 重叠

`editor_avalonia/MainWindow.axaml`：

- Viewport 从 `Grid.RowSpan="3"` 改为 `Grid.Row="0"`，并命名 `ViewportHost`。
- Inspector 从 `Grid.RowSpan="3"` 改为 `Grid.Row="0"`，并命名 `InspectorPanel`。
- 底部 `MainTabs` 保持 `Grid.Row="2"`、`Grid.Column="2"`、`ColumnSpan="3"`。
- Hierarchy 仍 `RowSpan=3`，保留左侧全高导航。

结果：Viewport/Inspector 与底部 Console 位于不同行，不再共享像素区域。
Splitter/MinSize 未加入（P1）。

### H2-A2 — Tab/Script 可读性

- `TabControl` 设置局部 `Background="#252526"`、`Foreground="#F1F1F1"`。
- 增加局部 `TabItem`/`TabItem:selected` 样式：`#252526`/`#B8B8B8` 与 `#2D2D30`/`#F1F1F1`。
- Console/Asset/Script 三个 Tab 内容根增加不透明 `Border Background="#252526"`
  （`ConsoleTabSurface` / `AssetTabSurface` / `ScriptTabSurface`）。
- Script Editor 空态文本设置可读前景（`#F1F1F1` / `#B8B8B8`），并命名
  `ScriptEmptyTitle` / `ScriptEmptyHint`。
- Console 过滤框与 Clear 按钮设置局部深色表面与浅色前景。
- Inspector 滚动容器命名 `InspectorScroll` 并暴露 AutomationId。

这是 P0 的局部可读性修复，没有建立全局主题/令牌体系；Tab 头部仍使用 Fluent
默认表面，P1 再统一。

### H2-A3 — Inspector 可达性

`MainWindow.axaml` 的 Inspector 内容包在
`ScrollViewer x:Name="InspectorScroll" VerticalScrollBarVisibility="Auto"
HorizontalScrollBarVisibility="Disabled"` 中；标题固定在上方。

结果：Camera + RigidBody 的字段与 `Apply RigidBody` 可通过滚动访问。

### 测试入口

- 新增 `editor_avalonia/Services/UiH2AGate.cs`：真实窗口几何/可读性/滚动断言。
- `MainWindow.axaml.cs` 增加 `--uih2a` 最小注册。
- 新增 `tools/run_uih2a_gate.ps1`。
- 新增 `build/uih2a_capture.ps1`（截图采集，不是产品入口）。
- 未修改 `MainViewModel.cs`、`EditorHostService.cs`、T-6 门禁或 T-6-A 行为。

---

## 2. H2-A 门禁结果

`UiH2AGate` 在真实 Avalonia 窗口打开 scratch 工程、选择 `Director`、通过真实
`+ Camera`/`+ RigidBody` 按钮添加组件，然后在四个尺寸执行：

| 尺寸 | 断言 |
|---|---|
| 900×600 | ConsoleList 不与 InspectorPanel/ViewportHost 重叠；Inspector 需要并可滚动；Apply RigidBody 在窗口内 |
| 1100×720 | 同上；Console 不覆盖 RigidBody；Apply 在窗口内 |
| 1440×900 | 无重叠、无越界 |
| 1920×1080 | 无重叠、无越界；Viewport 不遮挡相邻面板 |

另外在 900×600 检查：

- TabControl 背景不透明；
- Console/Asset/Script 三个 Tab 表面不透明；
- Console 过滤框/Clear 前景可读；
- Script Editor 空态文本可见、前景可读。

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug `--uih2a` | exit=0，全部断言通过 | `build/uih2a_gate_Debug.log` |
| Release `--uih2a` | exit=0，全部断言通过 | `build/uih2a_gate_Release.log` |
| Debug 构建 | 0 error，6 warnings（2×NU1900、1×CS8602、3×CS0414 既有） | `build/uih2a_build_Debug.log` |
| Release 构建 | 0 error，6 warnings（同上） | `build/uih2a_build_Release.log` |

---

## 3. 截图证据（前后对照）

Before 来自 UI-H0 基线；已复制到 `docs/evidence/UI-H2A/before/`，原始证据仍在
`docs/evidence/UI-H0/`。After 位于 `docs/evidence/UI-H2A/after/`。

| 尺寸 | Before | After | 结果 |
|---|---|---|---|
| 900×600 | `UI-H0/03_900x600_selected.png` | `UI-H2A/after/900x600_inspector_scrolled.png` | 无重叠；Inspector 滚动后可访问 RigidBody 与 Apply |
| 1100×720 | `UI-H0/01_default_1100x720_selected.png`、`08/09` | `UI-H2A/after/1100x720_inspector_scrolled.png` | Console 不再覆盖 RigidBody；Apply 在窗口内 |
| 1440×900 | `UI-H0/02_1440x900_selected.png` | `UI-H2A/after/1440x900_inspector_scrolled.png` | 区域不重叠；布局比例正常 |
| 1920×1080 | `UI-H0/04_1920x1080_selected.png` | `UI-H2A/after/1920x1080_inspector_scrolled.png` | 无重叠/越界；Viewport 不遮挡相邻面板 |
| Script 空态 | `UI-H0/06_script_editor_1100x720.png` | `UI-H2A/after/900x600_script_empty.png`、`1100x720_script_empty.png` | 空态在深色表面上清晰可读 |
| Console | `UI-H0/07_console_1100x720.png` | `UI-H2A/after/1100x720_console.png` | Tab 内容可读；过滤/Clear 可辨认 |
| Asset Browser | `UI-H0/05_asset_browser_1100x720.png` | `UI-H2A/after/1100x720_asset_browser.png` | 无 Inspector 覆盖；Tab 内容可读 |

截图采集在 Debug 构建上完成；配置为 1100×720 的窗口实际外框尺寸。

---

## 4. 回归结果

| 项目 | Debug | Release | 日志 |
|---|---|---|---|
| `--t6console` | exit=0 | exit=0 | `build/t6console_gate_Debug.log` / `..._Release.log` |
| T-6 UIA | 1/1 PASS | 1/1 PASS | `build/t6ui_test_Debug.log` / `..._Release.log` |
| `--prodpath` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2a_reg_prodpath_debug.log` / `..._release.log` |
| `--t1ui` | exit=0，PASS | exit=0，PASS | `build/uih2a_reg_t1ui_debug.log` / `..._release.log` |
| `--hrc2` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2a_reg_hrc2_debug.log` / `..._release.log` |
| #141 UIA（M1 + T6） | 2/2 PASS，exit=0 | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log` / `..._Release.log`；`build/uih2a_m1import_debug.log` / `..._release.log` |
| `test_bridge` | 103/103 PASS | 103/103 PASS | `build/uih2a_test_bridge_debug.log` / `..._release.log` |

T-6-A 冻结契约由 `--t6console` 覆盖并通过：

- 10,000 行容量与超限丢最旧；
- 清空只清展示缓存；
- 大小写不敏感子串过滤不修改原始缓存；
- 工程切换历史分段与边界语义。

未修改 T-6 门禁或断言。

---

## 5. 范围与限制

### 已做

- 仅 H2-A1/A2/A3；`MainWindow.axaml`、`MainWindow.axaml.cs` 的最小注册、
  `UiH2AGate.cs`、运行器、文档与截图。

### 未做（留给 P1/P2）

- 未增加 `GridSplitter`、`MinWidth`/`MinHeight`、列宽/底栏自适应；
- 未实现全局 Dark token/资源字典；Tab 头部仍是 Fluent 默认表面；
- 未统一工具栏分组、运行态、Console 级别/密度；
- Viewport 仍保持 1:1 左上对齐；1920 下右侧灰色空区未处理（D-UI-5 的居中/letterbox
  属于后续切片）；
- 900×600 下 Viewport 仍较小；P1 处理退化策略；
- 未改 `MainViewModel.cs`、`EditorHostService.cs`、T-6-A 行为、Bridge ABI、Engine。

### 基线

| 项 | 观测 |
|---|---|
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8`（`avalonia`） |
| T-2 八文件 SHA-256 | 全部 MATCH（含 `capi.h` = `02dbce31…b2886`） |
| `crashes/` | 115 个产物，未触碰 |
| 脏子模块 | `third_party/d3d12ma`、`openal-soft`、`vma`，未触碰 |
| H2-A 范围差 | 仅 `MainWindow.axaml` / `MainWindow.axaml.cs` 与新增测试/文档/证据 |
| Git 写入 | 无 |

---

## 6. 结论

H2-A 三个 P0 切片均已在真实窗口与四种尺寸验证通过；T-6-A 与既有 managed 门禁
无回归。裁决方已确认 `UI-H2-A — PASS — SCOPE-BOUND`，本阶段关闭；P1/P2 仍需
单独授权，本次授权不扩展。
