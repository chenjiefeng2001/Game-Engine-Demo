# UI-H1 — Visual Hierarchy Contract

**状态**：`FROZEN`（契约不变）；`UI-H2 — CLOSED — SCOPE-BOUND`（P0/P1/P2 已验收）
**裁决**：`UI-H0 — PASS`；`UI-H1 — APPROVED FOR FREEZE`（2026-10-29）；
`UI-H2 — CLOSED — SCOPE-BOUND`（2026-10-30）
**依据**：`docs/UI-H0-Visual-Hierarchy-Audit.md`、`docs/evidence/UI-H0/`
**日期**：2026-10-29（状态更新 2026-10-30）
**范围**：`editor_avalonia` 主窗口与现有面板；不接 Nova 平行 Shell，不改 T-6-A 行为。

本文件只冻结视觉层级、布局与控件规则；不含实现。UI-H2 的实现必须单独授权。

---

## 0. Formal Adjudication Record

**Decision:** APPROVED FOR FREEZE

**Implementation status:** UI-H2 P0/P1/P2 implemented and closed:
`UI-H2 — CLOSED — SCOPE-BOUND`（2026-10-30）。

### D-UI Decisions

- **D-UI-1 — Theme baseline: APPROVED.** Use the existing Dark Theme as the sole visual baseline for this iteration. Centralize semantic style tokens. A Light Theme is out of scope.
- **D-UI-2 — Layout model: APPROVED.** Retain the existing `editor_avalonia` Avalonia layout model. Correct row/column overlap, introduce appropriate sizing constraints and splitters, and keep Nova Dock outside the default implementation path.
- **D-UI-3 — Typography and spacing: APPROVED.** Use a consistent typography hierarchy and a 4px base spacing scale.
- **D-UI-4 — Semantic colors and contrast: APPROVED.** Standardize surfaces, text, borders, focus, selection, disabled, success, warning, and error states.
- **D-UI-5 — Viewport adaptation: APPROVED.** Preserve 1:1 mapping and aspect ratio. Center rendered content in the available viewport and use a distinct letterbox surface. Do not stretch content to fill the viewport.
- **D-UI-6 — Console visuals: APPROVED.** Establish a clearly differentiated panel surface, toolbar/filter area, and content area. Do not invent severity or source metadata that the current event model does not provide.

### Implementation priority

**P0:** Correct Grid overlap; restore opaque, readable tab content; make all Inspector fields and actions reachable through proper scrolling.

**P1:** Improve resizing and splitters, enforce minimum dimensions, clarify toolbar grouping and typography, and unify control-state presentation.

**P2:** Refine icons, spacing, empty states, separators, and non-blocking visual details.

### Acceptance

Use the UI-H0 screenshots at 900×600, 1100×720, 1440×900, and 1920×1080 as comparison baselines. Verify Console, Asset, Script, and Inspector states. Record before/after screenshots, actual test outcomes, and relevant regression results.

### Boundaries

- T-6-A behavior remains frozen.
- T-6-B Engine `spdlog` integration remains a separate deferred task.
- No new Bridge ABI.
- No Nova Dock migration in this iteration.
- No source implementation is authorized by this contract freeze alone.
- No commit, push, tag, or merge is authorized.

**Next state:** UI-H1 may be marked FROZEN after these decisions are incorporated into the contract. UI-H2 requires separate implementation authorization.

**Incorporation status:** 上述裁决已纳入本文件；`UI-H1 — FROZEN`；UI-H2 未授权。
（历史记录；2026-10-30 最终裁决已更新为 `UI-H2 — CLOSED — SCOPE-BOUND`，见 §9。）

---

## 0.1 D-UI 决定（FROZEN）

| # | 决定 | 裁决 | 冻结要求 |
|---|---|---|---|
| D-UI-1 | 主题基线 | **APPROVED** | 现有 Dark Theme 为本期唯一视觉基线；集中语义样式令牌；Light Theme 不在本期 |
| D-UI-2 | 布局模型 | **APPROVED** | 沿用现有 Avalonia 布局；修正行列重叠，增加合理的尺寸约束与 Splitter；Nova Dock 不作为本期默认路径 |
| D-UI-3 | 字体与间距 | **APPROVED** | 统一字体层级 + 4px 基础间距体系；禁止各面板自行累积不一致规则 |
| D-UI-4 | 语义色与对比度 | **APPROVED** | 统一背景、文本、边界、焦点、选中、禁用、成功、警告、错误语义色 |
| D-UI-5 | Viewport 适配 | **APPROVED** | 保留 1:1 映射与等比显示；内容居中 + 独立 letterbox；不得拉伸填满 |
| D-UI-6 | Console 视觉 | **APPROVED** | 不透明且有层次的面板、工具栏/过滤区、内容区；不虚构级别/来源元数据 |

D-UI-1…D-UI-6 已全部批准；UI-H1 已冻结，UI-H2 未授权。
（历史记录；UI-H2 已于 2026-10-30 关闭为 `CLOSED — SCOPE-BOUND`。）

---

## 1. 层级系统

| 级别 | 内容 | 规则 |
|---|---|---|
| L0 应用级 | 菜单、全局工具栏、状态栏 | 只放全局命令与全局状态；不承载选中对象属性 |
| L1 工作区级 | Hierarchy / Viewport / Inspector / Console | 每个区域有独立面板容器与标题；不互相覆盖 |
| L2 面板级 | 面板标题、面板工具栏、内容区、状态/空态 | 标题与工具条固定；内容区滚动；状态区固定 |
| L3 分组级 | Inspector 的 Transform / Camera / RigidBody；Asset 的 Type/Path/GUID | 分组标题 + 分隔；组间间距大于组内 |
| L4 控件级 | 输入框、按钮、列表行、Tab | 主/次/危险、选中/悬停/聚焦/禁用有统一规则 |

**布局规则**：

- 菜单、工具栏、状态栏在 DockPanel 的 Top/Top/Bottom；主工作区为填充的 Grid。
- 主工作区列：`240 | 6 | * | 6 | 280`；6px 为 `GridSplitter`，可拖动；
  Hierarchy 最小 200，Inspector 最小 240，Viewport 最小 320。
- 主工作区行：`* | 6 | 240`；6px 为水平 `GridSplitter`；底部面板最小 140，
  最大不超过窗口内容高度的 40%。
- **禁止** Viewport/Inspector 使用 `RowSpan=3` 与底部 TabControl 共用行。
- 窗口 `MinWidth=900`、`MinHeight=600`；低于最小尺寸不要求可用。
- Inspector、Hierarchy、Console、Asset 列表必须有滚动容器；内容不得越出窗口。
- Tab 内容背景必须不透明，不得透出 Viewport 黑色背景。

---

## 2. 字体体系

| 角色 | 字号 | 字重 | 用途 |
|---|---|---|---|
| Page/Window 内标题 | 16 | SemiBold | 面板外的页面级标题（当前可无） |
| Panel title | 14 | SemiBold | Hierarchy / Inspector / Console / Asset Browser |
| Section title | 13 | SemiBold | Transform / Camera / RigidBody / Asset 列头 |
| Body | 13 | Normal | 属性值、按钮、列表正文 |
| Helper | 12 | Normal | 辅助说明、空态、单位 |
| Caption | 11 | Normal | 时间戳、计数、GUID、状态栏次要文本 |
| Code/Log | 12 | Normal（等宽） | Console 行、Lua 编辑器、GUID |

规则：

- 不使用主题默认字号作为“未定义层级”；所有面板标题/分组标题必须有明确字号。
- 辅助文本使用 `TextSecondary`，不再用 0.4/0.5/0.55/0.7 等任意 opacity。
- 时间戳、计数、GUID 使用 Caption；Console 的消息与时间戳分离视觉权重。
- 字体族：正文使用系统 UI 字体；代码/日志使用 `Consolas, Courier New`。
- 粗体只用于标题与关键状态，不用于整段正文。

---

## 3. 间距体系

| token | 值 | 用途 |
|---|---|---|
| `Space-1` | 4 | 图标与文本、紧凑行内 |
| `Space-2` | 8 | 控件间距、按钮组内 |
| `Space-3` | 12 | 面板内边距、分组内边距 |
| `Space-4` | 16 | 分组间距 |
| `Space-5` | 24 | 面板外大分隔 |

规则：

- 面板 padding = 12；面板标题高 32；工具栏高 40；状态栏高 24。
- 分组标题与组内容间距 8；组与组间距 16。
- 工具栏按钮间距 8；按钮组之间用 8px 分隔或 Separator。
- 列表行高 24（Console/Asset/Hierarchy 统一）；Console 可采用 22–24。
- 不再使用 2/6/10 等无来源的孤立值；迁移时映射到最近 token。

---

## 4. 颜色、主题与对比度

### 4.1 主题基线

- 采用 Dark 基线：`RequestedThemeVariant="Dark"` + 自定义资源字典。
- 所有自定义背景/边框/文字必须引用资源，不在 XAML 内散落十六进制色值。
- Light 主题不在本期支持；未来支持时通过同一语义资源切换，不允许逐控件改色。

### 4.2 语义色（冻结值）

| token | 建议值 | 用途 |
|---|---|---|
| `EditorCanvas` | `#1E1E1E` | Viewport 外围/主画布 |
| `PanelSurface` | `#252526` | 面板内容背景 |
| `PanelHeader` | `#2D2D30` | 面板标题/工具栏背景 |
| `BorderSubtle` | `#3F3F46` | 面板分隔、列表分隔 |
| `TextPrimary` | `#F1F1F1` | 正文/标题 |
| `TextSecondary` | `#B8B8B8` | 辅助文本/时间戳/空态 |
| `Accent` | `#4F9CF9` | 主操作、焦点、选中强调 |
| `SelectionBackground` | `#094771` | 选中行/选中 Tab |
| `Warning` | `#D7BA7D` | 警告、dirty |
| `Error` | `#F48771` | 错误、危险操作 |
| `DisabledText` | `#6F6F6F` | 禁用文字（仍需可辨识） |

对比度要求：

- 正文/标题对背景 ≥ **4.5:1**；大字号/非文本 UI ≥ **3:1**。
- Tab 标题、Script 空态、Console 过滤栏不得出现黑字叠黑底或低对比占位符。
- 警告/错误不能只靠颜色：同时使用 `WARN`/`ERROR` 文本或图标。
- Disabled 仍需可辨识；不得用 opacity < 0.5 表达禁用。

---

## 5. 控件规则

### 5.1 按钮

| 类型 | 视觉 | 使用 |
|---|---|---|
| Primary | Accent 背景/高对比文字 | 每个上下文最多一个主操作（Save/Apply/Import 之一） |
| Secondary | PanelHeader/Surface + 边框 | 常用辅助操作 |
| Destructive | Error 文本/边框或危险色 | Delete/Remove；必须有确认或可恢复路径 |
| Toolbar | 紧凑、同高 28–32 | 全局/编辑/运行分组，组间 Separator |

- 工具栏分组：`Open/Save` │ `+ Entity` │ `Play/Reload/Stop`。
- Play 在运行中切换为明确的 `Playing` 状态（禁用或 badge），Stop/Reload 变为可用。
- 生产工具栏不得常驻 `[P3-D] ...` 调试说明；说明放 Tooltip 或诊断区。
- 菜单与工具栏可保留同一命令，但菜单是完整入口，工具栏是高频入口；不重复视觉权重。

### 5.2 输入框

- Inspector 使用“标签列 + 输入列”的固定对齐；标签列建议 96–112px。
- 禁止只用 Watermark 当标签；所有属性有可见标签。
- 非法输入/失败在输入附近显示错误文本，同时写入 Console；不只依赖状态栏。

### 5.3 列表与 Tab

- 列表行高统一 24；选中行使用 `SelectionBackground` + `TextPrimary`；
  悬停使用比背景高一档的表面色；聚焦有 1px Accent 边框/焦点环。
- Asset Browser 增加列头 `Type | Path | GUID`；GUID 用 Caption 等宽。
- Console 行：时间戳 `TextSecondary`、消息 `TextPrimary`；**不根据文本推断**
  Info/Warn/Error 级别或来源。工程边界行可以使用独立分隔线与样式，但不得伪装成
  日志严重级别。过滤框/清空按钮与 PanelHeader 一致对比。
- Tab：选中项使用 `PanelHeader` 背景 + 2px Accent 下划线；未选中 `TextSecondary`；
  内容区背景 `PanelSurface`，不得透明。

### 5.4 Viewport

- Viewport 控件**填充分配给它的工作区**，不得溢出或覆盖相邻面板。
- 图像保持 1:1 像素与等比显示并**居中**；四周 letterbox 使用独立的 `EditorCanvas`
  表面，与普通面板背景区分。
- 不修改 `ViewportImage` 的 `Stretch=None` 与像素尺寸；不得用非等比拉伸填满区域。
  如要 fit-to-window，必须单独授权并同步更新 picking mapper/验收，不能只改 XAML。
- 窗口调整、Splitter 拖动、面板缩放不得导致渲染区域溢出或覆盖相邻面板。
- Gizmo 工具组放在右上，使用统一按钮尺寸与当前模式高亮；不得与图像内容叠压到不可点击。
- 左下状态文本使用 Helper/Caption，不显示 Phase 开发占位文案。
- 1920×1080 的验收对象是工作区利用率、图像居中与 letterbox 是否正确，而不是要求
  方形内容无条件铺满宽屏。

---

## 6. 面板规则

| 面板 | 规则 |
|---|---|
| Hierarchy | 顶部搜索；中部列表；底部重命名/删除/创建；空态文案；列表滚动 |
| Inspector | 32px 标题；全部内容在 ScrollViewer；分组标题 13 SemiBold；Apply 按钮在组内；空态“(select an entity)” |
| Viewport | 标题可省；工具组叠加；图像居中 letterbox；不显示开发占位 |
| Console | 工具栏（过滤/清空）+ 可滚动列表 + 空态；工具栏/过滤区/内容区表面分层；不推断级别或来源 |
| Asset Browser | 工具栏（类型/搜索/导入/重命名/计数）+ 列头 + 列表 + 底部 Assign |
| Script Editor | 顶部标题/脏标记/保存/错误；编辑区等宽；空态在 PanelSurface 上居中可见 |
| 状态栏 | 左：项目/运行状态；右：dirty/错误计数；高度 24；文本 ≥ 12px |

---

## 7. 状态语义

| 状态 | 表达 |
|---|---|
| 选中 | 列表 `SelectionBackground`；Inspector 顶部显示对象名；Viewport 选中轮廓可选（需另批） |
| 悬停 | 比当前表面高一档的背景；不改变布局 |
| 聚焦 | 1px Accent 焦点环；键盘可达 |
| 禁用 | `DisabledText` + 禁用表面；仍可辨识；不使用低 opacity 混色 |
| 运行中 | 工具栏 `Playing` badge + Stop/Reload 可用；状态栏显示 `Playing` |
| 脏 | 状态栏 `Unsaved` + Warning 点；Script 标题旁 `(edited)` 同色语义 |
| 错误 | 相关面板内 error banner + 结构化错误文本；Error 色；Console 只显示原始事件文本，不推断级别 |
| 警告 | 面板 Warning 文本/状态；不只靠颜色；Console 不根据文本推断 WARN 级别 |
| 空态 | `PanelSurface` 上的 `TextSecondary` 文案；不能叠在 Viewport 黑底上 |

---

## 8. 自适应与验收

### 8.1 尺寸矩阵

| 尺寸 | 要求 |
|---|---|
| 900×600（最小） | 无重叠；Inspector 可滚动；关键操作可达；工具栏不重叠 |
| 1100×720（默认） | Console 不覆盖 RigidBody 区域；Apply RigidBody 位于有效窗口范围 |
| 1440×900 | 正常工作尺寸下的层级、间距与比例稳定 |
| 1920×1080 | 工作区利用率合理；1:1 Viewport 居中；letterbox 明确；底部面板不悬空 |
| 2560×1600（本机） | 同上；文字不因过大空白失去层级 |

### 8.2 验收项

1. 截图矩阵逐项人工复核；UI-H1 冻结的层级/字号/间距/颜色在截图中可辨认。
2. UIA 边界断言：Inspector 的最后一个可操作控件（Apply RigidBody）在窗口内；
   ConsoleList 与 Inspector 的矩形不相交；ScrollViewer 可把隐藏属性滚入视口。
3. 对比度：用 §4 调色板计算正文/标题/辅助/禁用/警告/错误的对比度并记录。
4. 真实控件行为：过滤/清空、Tab 切换、Inspector Apply、Asset 选择、Script 空态。
5. 功能回归：`--t6console`、`--prodpath`、`--t1ui`、`--hrc2`、#141 UIA、
   `test_bridge` 全部保持 PASS；不得用视觉改动削弱 T-6-A 行为。
6. 每一批提供修改前后截图、窗口尺寸、实际验收结果与相关回归结果；不得只凭主观
   评价宣布布局已改善。
7. Debug/Release 分开记录；不宣称零警告/clean build；首次失败保留。

---

## 9. UI-H2 分批建议（历史；P0/P1/P2 已全部关闭）

**H2-A 状态**：`PASS — SCOPE-BOUND / CLOSED`（`docs/UI-H2A-Closure-Record.md`）。
**P1-A 状态**：`PASS — SCOPE-BOUND / CLOSED`（`docs/UI-H2-P1A-Closure-Record.md`；
随 P2 最终验收一并封存）。
**P1-B 状态**：`PASS — SCOPE-BOUND / CLOSED`（`docs/UI-H2-P1B-Closure-Record.md`）。
**P1-C 状态**：`PASS — SCOPE-BOUND / CLOSED`（`docs/UI-H2-P1C-Closure-Record.md`）。
**P2 状态**：`PASS — SCOPE-BOUND / CLOSED`（`docs/UI-H2-P2-Closure-Record.md`）。
**UI-H2 总状态**：`UI-H2 — CLOSED — SCOPE-BOUND`（P0/P1/P2 全部关闭，2026-10-30）。

实施顺序必须 **P0 → P1 → P2**：

- **P0 = H2-A**：Grid 重叠、可读的不透明 Tab 内容、Inspector 滚动与可达性。
- **P1 = H2-B + H2-C**：资源令牌/字体间距、Splitter/MinSize、自适应、工具栏分组、
  状态语义。
- **P2 = H2-D 及细节**：Viewport 居中/letterbox、Gizmo 状态、图标/空态/分隔线/
  Console 密度。

| 切片 | 内容 | 前置 |
|---|---|---|
| H2-A | 消除 RowSpan/Row 2 重叠；Inspector ScrollViewer；Tab 不透明背景 | UI-H2 单独授权 |
| H2-B | 资源字典、Dark 基线、字体/间距/语义色 token；Splitter/MinSize | H2-A 完成 |
| H2-C | 工具栏分组/运行态、Console 面板层次与边界行分隔、Asset 列头、状态语义 | H2-A/B 完成 |
| H2-D | Viewport 居中/letterbox、Gizmo 状态；picking 适配需单独授权 | H2-A/B 完成 |

每片单独验收，不把 H2-C/D 混入 H2-A 的范围；P2 不得先于 P0。

---

## 10. 非目标与冻结边界

- 不接 Nova Dock 作为 UI-H2 的默认路径；Dock 接入另立阶段。
- 不改 T-6-A 的 generation 与 Console 行为：
  - 10,000 行容量、超限丢最旧；
  - 清空只作用于展示缓存；
  - 大小写不敏感子串过滤，仅影响显示；
  - 工程切换的历史分段语义；
  - Console 不根据文本推断级别/来源；工程边界行可独立分隔，但不冒充严重级别。
- 不新增 T-6-B Engine `spdlog` 接入；不新增 Bridge ABI。
- 本契约冻结不授权任何源码实施；UI-H2 必须单独批准。
- 不在 UI-H1 冻结前实施全局样式/配色/控件模板。
- 不追求完整 Design System/图标库/动画体系；先形成可落地的最小规范。
- 不执行 commit/push/tag/merge。
