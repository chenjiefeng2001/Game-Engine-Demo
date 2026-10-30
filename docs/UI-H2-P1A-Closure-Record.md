# UI-H2 P1-A — Adaptive Layout & Viewport Centering 关闭记录

**状态**：`UI-H2 P1-A — PASS — SCOPE-BOUND / CLOSED`
（随 P2 最终验收一并封存，2026-10-30）
**日期**：2026-10-29（最终裁决 2026-10-30）
**范围**：Splitter / 最小尺寸 / 缩放退化 / Viewport 1:1 居中 letterbox。
**未执行**：P1-B（排版、工具栏分组、状态统一）与 P1-C（全局 Dark token）、P2、
Nova Dock、T-6-A 行为/Console 逻辑、Bridge ABI、Engine Core；未 commit/push/tag/merge。

---

## 1. 实施内容

### 1.1 自适应分区

`editor_avalonia/MainWindow.axaml`：

- 窗口 `MinWidth="900"`、`MinHeight="600"`。
- `MainGrid` 改为显式行列定义：
  - 列：Hierarchy `240`（Min 200）、8px splitter、Viewport `*`（Min 320）、
    8px splitter、Inspector `280`（Min 240）。
  - 行：主工作区 `*`（Min 240）、8px splitter、底部面板 `220`（Min 140，Max 360）。
- 新增三个 `GridSplitter`：`HierarchySplitter`、`InspectorSplitter`、`ConsoleSplitter`，
  均有 AutomationId，支持拖动调整列宽/底栏高度。
- 底部面板仍位于第 3 行，跨 Viewport+Inspector 列；Inspector 只在第 1 行，
  因此 P1-A 之后仍无重叠。

### 1.2 Viewport 1:1 居中

- `ViewportBorder.ClipToBounds="True"`，防止图像溢出覆盖相邻面板。
- `ViewportImage` 从 `Left/Top` 改为 `Center/Center`，保持 `Stretch="None"` 与
  原始像素尺寸；外层 Viewport 填充分配区域，剩余空间作为 letterbox。
- 不改变 picking 坐标前提：`e.GetPosition(ViewportImage)` 仍相对图像像素。
- 1920×1080 下图像居中，左右剩余区域为 `#1E1E1E` letterbox；未做 fit-to-window
  拉伸（仍需单独授权并更新 mapper）。

### 1.3 测试入口

- 新增 `editor_avalonia/Services/UiH2P1AGate.cs`。
- `MainWindow.axaml.cs` 增加 `--uih2p1a` 最小注册。
- 新增 `tools/run_uih2p1a_gate.ps1`、`build/uih2p1a_capture.ps1`。
- 未改 `MainViewModel.cs`、`EditorHostService.cs`、T-6-A 生产行为。

### 1.4 T-6 UIA harness 调整（需单独说明）

P1-A 布局改变了 Console 列表的可见高度/虚拟化窗口。既有 T-6 UIA helper
`ReadConsoleTexts` 在读取前滚到底部，导致早期 `[project] opened:` 行不被 UIA
realize，从而误报超时。已将 helper 改为在读取时同时扫描列表顶部与底部
（`ScrollConsoleTo` 0/100），**断言与通过条件未改变**；T-6-A 生产代码未修改。
这是测试 harness 的读取窗口修正，不是放宽 T-6 断言。

---

## 2. P1-A 门禁结果

`UiH2P1AGate` 真实窗口打开 scratch 工程，断言：

- 窗口 Min 尺寸至少 900×600；
- 五个列/三个行的 Min/Max 约束；
- 三个 GridSplitter 可见；
- 900/1100/1440/1920 下 Console 与 Inspector 不重叠；
- ViewportImage 有实际尺寸、居中误差 ≤1.5px、ViewportBorder 开启 ClipToBounds；
- 修改列 0 宽度与底行高度后，Hierarchy/Console 实际尺寸响应；
- Console 内容表面仍不透明。

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug `--uih2p1a` | exit=0，全部断言通过 | `build/uih2p1a_gate_Debug.log` |
| Release `--uih2p1a` | exit=0，全部断言通过 | `build/uih2p1a_gate_Release.log` |
| Debug 构建 | 0 error，6 warnings（2×NU1900、1×CS8602、3×CS0414 既有） | `build/uih2p1a_final_build_debug.log` |
| Release 构建 | 0 error，6 warnings（同上） | `build/uih2p1a_build_Release.log` |

### 2.1 Splitter 真实拖动证据

外部采集脚本在 1100×720 下用真实鼠标拖动：

- `HierarchySplitter`：HierarchyList 宽度 `230 → 310`；
- `ConsoleSplitter`：ConsoleList 高度 `130 → 190`。

前后截图：

- `docs/evidence/UI-H2-P1A/after/1100x720_hierarchy_splitter_dragged.png`
- `docs/evidence/UI-H2-P1A/after/1100x720_console_splitter_dragged.png`

### 2.2 截图矩阵

Before 为 H2-A 验收截图副本（`docs/evidence/UI-H2-P1A/before/`）；After 位于
`docs/evidence/UI-H2-P1A/after/`：

| 尺寸 | After 文件 | 结果 |
|---|---|---|
| 900×600 | `900x600_inspector_scrolled.png`、`900x600_script_empty.png`、`900x600_console.png` | 无重叠；Inspector 可滚动；分栏约束生效 |
| 1100×720 | `1100x720_*.png` | 无重叠；Viewport 1:1 居中；Splitter 可拖动 |
| 1440×900 | `1440x900_*.png` | 无重叠；比例稳定 |
| 1920×1080 | `1920x1080_*.png` | Viewport 居中，左右 letterbox 明确；无相邻面板遮挡 |

说明：设置 `Window.Width=900/Height=600` 时，Windows 非客户区使 DWM 可见捕获为
约 902×632；内容按 900×600 的最小约束布局。截图文件名为请求尺寸。

---

## 3. 回归结果

| 项目 | Debug | Release | 日志 |
|---|---|---|---|
| `--uih2a`（P0 回归） | exit=0 | exit=0 | `build/uih2a_gate_Debug.log` / `..._Release.log` |
| `--t6console` | exit=0 | exit=0 | `build/t6console_gate_Debug.log` / `..._Release.log` |
| T-6 UIA | 1/1 PASS | 1/1 PASS | `build/t6ui_test_Debug.log` / `..._Release.log` |
| `--prodpath` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1a_reg_prodpath_debug.log` / `..._release.log` |
| `--t1ui` | exit=0，PASS | exit=0，PASS | `build/uih2p1a_reg_t1ui_debug.log` / `..._release.log` |
| `--hrc2` | exit=0，ALL GREEN | exit=0，ALL GREEN | `build/uih2p1a_reg_hrc2_debug.log` / `..._release.log` |
| #141 UIA（M1 + T6） | 2/2 PASS，exit=0 | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log` / `..._Release.log` |
| `test_bridge` | 103/103 PASS | 103/103 PASS | `build/uih2p1a_test_bridge_debug.log` / `..._release.log` |

T-6-A 契约仍由 `--t6console` 覆盖：10,000 行容量、清空、过滤、工程分段均通过。

---

## 4. 未做 / 限制

- **P1-B 未做**：排版层级、工具栏分组、移除 `[P3-D]` 提示、统一选中/焦点/禁用/
  运行/错误/警告状态。
- **P1-C 未做**：全局 Dark Theme token/资源字典；Tab 头部仍为 Fluent 默认表面；
  Console 列表仍为默认白色。
- **Viewport 未做 fit-to-window/缩放**：保持 1:1 与居中 letterbox；图像大于视野时
  由 ClipToBounds 裁剪，不拉伸。
- Splitter 宽度/高度不持久化；重启恢复默认值。
- 900×600 下 Viewport 仍较小；P1-B 可继续评估信息密度。
- T-6 UIA harness 的读取窗口已调整为顶部+底部；断言未变，T-6-A 生产代码未变。
- 未做 clean rebuild；不宣称零警告/clean build。

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

P1-A（自适应布局 + Viewport 居中）已在真实窗口、四个尺寸与真实 Splitter 拖动下
通过；H2-A P0 与全部既有 managed/UIA/Bridge 回归通过。最终裁决随
`UI-H2-P2 — PASS — SCOPE-BOUND / CLOSED` 一并确认 P1-A 为
`PASS — SCOPE-BOUND / CLOSED`；P1-B/P1-C 亦已分别关闭，`UI-H2 — CLOSED — SCOPE-BOUND`。
