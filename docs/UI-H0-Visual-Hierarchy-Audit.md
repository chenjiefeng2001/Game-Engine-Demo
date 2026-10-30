# UI-H0 — Visual Hierarchy Audit

**状态**：`UI-H0 — PASS — READ-ONLY AUDIT COMPLETE`
**裁决**：`UI-H0 — PASS`（2026-10-29）；后续规范见 `docs/UI-H1-Visual-Hierarchy-Contract.md`（FROZEN）
**日期**：2026-10-29
**范围**：`editor_avalonia` 生产编辑器的真实窗口、布局与渲染结果。
**未执行**：未修改任何 UI 源码/样式/资源；未改 T-6-A、Docking、Nova、Bridge ABI；
未 commit/push/tag/merge。

本审查只回答“当前界面为什么缺少视觉层级、哪些问题是结构性的”，不实施美化，
也不替代 UI-H1 规范冻结或 UI-H2 授权。

---

## 1. 方法

- 以 XAML 与代码为结构证据：`editor_avalonia/MainWindow.axaml`、`App.axaml`、
  `MainViewModel.cs`。
- 以真实窗口为渲染证据：`--t6ui-host <scratch>` 打开 scratch `assets/gp01`，
  UIA 选中 `Director`，在 1100×720、1440×900、900×600、1920×1080 四个尺寸截图；
  再切换 Console / Asset Browser / Script Editor，追加 Camera/RigidBody 组件后截图。
- 截图保存在 `docs/evidence/UI-H0/`；采集脚本在 `build/uih0_capture.ps1`（非产品源码）。
- UIA 边界测量在 1100×720 下单独采集（`build/uih0_bounds.ps1`）。

> 注：窗口标题在测试宿主下为 `Avalonia Editor — T6UI-HOST`；生产标题仍是
> `MainWindow.axaml:5` 的 `Avalonia Editor — Phase 1 Shell (P1-A/B/C)`。
> 测试宿主只打开 scratch 工程，不改变生产布局或路径。

---

## 2. 真实布局与测量

### 2.1 XAML 结构

| 事实 | 证据 |
|---|---|
| 全局结构是 `DockPanel`：菜单 Top、工具栏 Top、状态栏 Bottom、四区 Grid 填充 | `MainWindow.axaml:8-43` |
| 四区 Grid 固定列 `240,8,*,8,280`、固定行 `*,8,220` | `MainWindow.axaml:43` |
| Hierarchy / Viewport / Inspector 都 `RowSpan=3` | `MainWindow.axaml:46,74,130` |
| 底部 TabControl 位于 `Row=2`、`Column=2`、`ColumnSpan=3` | `MainWindow.axaml:211` |
| 没有 `GridSplitter`、`MinWidth`、`MinHeight` | 全文件无匹配 |
| 应用只注册 FluentTheme，`RequestedThemeVariant="Default"` | `App.axaml:4-6` |
| 没有共享样式/资源字典；颜色、透明度、边距大量内联 | `MainWindow.axaml` 全文 |

**结构性结论**：Viewport 与 Inspector 跨 3 行，底部 TabControl 又占第 3 行，
两者在 Grid 中重叠；不是“上下分区”，而是“底部面板覆盖在 Viewport/Inspector 上”。

### 2.2 1100×720 的 UIA 边界（屏幕坐标）

| 元素 | 边界 | 判读 |
|---|---|---|
| 窗口 | `x=68 y=91 w=1100 h=720`（底边 811） | 默认尺寸 |
| HierarchyList | `x=77 y=199 w=230 h=466` | 左栏完整 |
| Console 标签 | `x=320 y=560 w=108 h=48` | 第 3 行从 y≈560 开始 |
| ConsoleFilterBox | `x=336 y=614 w=220 h=32` | 位于第 3 行内容区 |
| ConsoleList | `x=332 y=650 w=820 h=130` | 横向覆盖到 Inspector（x=893 起） |
| Inspector `Camera` | `x=893 y=491` | 仍在第 3 行之上 |
| Inspector `RigidBody` | `x=893 y=736` | **落在 ConsoleList 的 y 区间内** |
| `+ RigidBody` | `x=891 y=757` | 落在 ConsoleList 区间内 |
| `Apply RigidBody` | `x=893 y=903` | **在窗口底边 811 之外** |
| StatusBar | `x=76 y=788` | 底部状态栏 |

这组数值与截图一致：Inspector 的后半段被 Console 覆盖，`Apply RigidBody`
完全不可达；不是“视觉上拥挤”，而是布局/层级错误。

### 2.3 截图矩阵

| 文件 | 场景 | 关键观察 |
|---|---|---|
| `01_default_1100x720_selected.png` | 默认尺寸，选中 Director | Inspector 在 RigidBody 下方被 Console 覆盖；标签文字黑底黑字；Console 列表与 Inspector 重叠 |
| `02_1440x900_selected.png` | 1440×900 | 重叠仍存在；Viewport 变宽但仍被底部覆盖；Inspector 仍未滚动 |
| `03_900x600_selected.png` | 900×600 | Viewport 仅约 340px 宽；Inspector 字段被截断；Tab 标题与 Inspector 文本互相叠压 |
| `04_1920x1080_selected.png` | 1920×1080 | 侧栏仍固定 240/280；Viewport 的 1:1 图像只占左上区域，其余为大块灰色空区；底部面板固定 220 |
| `05_asset_browser_1100x720.png` | Asset Browser | 工具栏控件与 Inspector 边缘拥挤；无列头；GUID 10px/0.5 opacity；底部 Assign 按钮对比度低 |
| `06_script_editor_1100x720.png` | Script Editor 空态 | 空态文字与 Tab 标题在黑色 Viewport 上几乎不可见 |
| `07_console_1100x720.png` | Console | 过滤栏/清空按钮低对比；Console 行无级别区分；列表覆盖 Inspector |
| `08_inspector_rigidbody_1100x720.png` | 加 RigidBody 后 | RigidBody 字段完全不可见；仅底部边缘可见残留按钮；状态栏 DIRTY 可见 |
| `09_inspector_camera_rigidbody_1100x720.png` | 加 Camera+RigidBody 后 | Enabled/Zoom 可见，Viewport/Bounds/Apply Camera 与 RigidBody 全部被覆盖或越界 |

---

## 3. 五个维度的审查结论

### 3.1 主窗口的信息层级

当前区域存在，但层级关系被布局破坏：

| 区域 | 现状 | 问题 |
|---|---|---|
| 顶部菜单/工具栏 | Open/Save/+Entity/Play/Reload/Stop + 调试提示 | 全局命令、编辑命令、运行命令混在同一行；菜单与工具栏重复 Open/Save |
| 左侧 Hierarchy | 搜索、重命名、删除、创建、实体列表 | 有导航，但缺少“对象/资源”分组与空态 |
| 中央 Viewport | 1:1 图像 + 右上 Gizmo + 左下 “Viewport” | 图像不填充/不居中；Gizmo 无选中态；被底部面板覆盖 |
| 右侧 Inspector | 选中对象、Sprite/Script、Transform、Camera、RigidBody | 无滚动容器；内容越出窗口；被 Console 覆盖 |
| 底部 Console/Assets/Script | TabControl | 与 Inspector/Viewport 重叠；Tab 标题在黑色背景上不可读 |
| 底部状态栏 | 单行 StatusText + DIRTY | 无运行态、无模式、无分组；与 Console 重复 project loaded |

结论：信息分区“看起来是四区”，实际是“三栏 + 一个覆盖层”。层级问题首先是
**结构问题**，不是配色问题。

### 3.2 视觉优先级

- 工具栏所有按钮同一尺寸/同一默认样式；Open/Save/+Entity/Play/Reload/Stop
  没有主次、没有分组、没有分隔。
- `[P3-D] Save → Play → Edit → Reload (保留 _PERSIST) → Stop` 是开发者说明，
  与命令同排、同视觉层级；生产界面不应把它当作常驻信息。
- Gizmo Q/W/E/R/G 是 28×28 的浅灰方块，字母对比度低；没有当前模式高亮。
- Console 中时间戳、消息、警告、错误全部同一字号/颜色；出错时需要逐行读文本。
- Asset Browser 的 `33` 计数没有标签；GUID 比路径更显眼地弱，却不帮助当前操作。
- 状态栏只有一行低对比文本；DIRTY 用黄色小点，运行态没有对应 badge。

结论：用户无法快速回答“当前工作区在哪、选中了什么、主操作是什么、出错先看哪里”。

### 3.3 排版与密度

- 显式字号只有 10/11/12/20 四种，绝大部分控件使用主题默认；没有面板标题/
  分组标题/正文/辅助文本的稳定层级。
- 透明度散落为 0.4/0.5/0.55/0.7/0.8；同一类“辅助文本”在不同面板使用不同值。
- 边距/间距值出现 2/4/6/8/10/12 多套；没有 4px 基准或共享 token。
- 固定列 240/280、固定底行 220；在 900×600 下 220 底行与 Inspector 内容冲突，
  在 1920×1080 下底行仍 220，Console 大面积空白。
- Inspector 无 ScrollViewer；内容高度随组件增加而无限增长。
- Console 行高较大、时间戳与消息同权；33 个资产的列表在 220 高面板中只露出
  约 2 行，缺少列头与密度控制。

### 3.4 状态表达

| 状态 | 现状 | 问题 |
|---|---|---|
| 选中 | Hierarchy 行浅灰高亮 | 无 accent；Inspector 没有“当前对象”标题层级；Viewport 无选中轮廓 |
| 悬停/聚焦 | 依赖 Fluent 默认 | 与自绘暗色背景不一致；无明显焦点环 |
| 禁用 | 浅灰文字/按钮 | Reload/Stop、Remove Camera 等禁用/启用差异弱 |
| 运行中 | 仅按钮 enabled 变化 | 没有 PLAYING/RUNNING 状态 badge；Play 仍可用 |
| 脏 | 状态栏黄色 `● DIRTY` | 位置弱、无文本“unsaved changes”；Script 也有独立黄色 `(edited)` |
| 错误 | `#FF8070` 文本、ScriptError | Console 无 error 行样式；无面板内错误 banner |
| 警告 | `#FFD060` | 仅颜色；无图标/级别前缀 |
| 空态 | Script Editor 有文案 | 文案在黑色 Viewport 上不可见；Hierarchy/Console/Inspector 无统一空态 |

状态主要依赖颜色或细微亮度，且相同状态在不同面板表达不同。

### 3.5 缩放与布局稳定性

- 无 MinWidth/MinHeight；900×600 已出现 Inspector 截断与 Tab/Inspector 文本叠压。
- 无 GridSplitter：240/280/220 全固定，用户无法按任务调整；大屏浪费、小屏挤压。
- Viewport 使用 `Image Stretch="None" HorizontalAlignment="Left" VerticalAlignment="Top"`
  （为 picking 坐标前提），但没有外层 letterbox/居中；1920 下图像只占左上，
  右侧是大块灰色空区。
- 底部 TabControl 与 Inspector/Viewport 重叠，任何窗口尺寸都不成立。
- Tab 内容背景透明，叠在黑色 Viewport 上导致 Tab 标题、Script 空态不可读。
- 工具栏无换行/溢出策略；窄窗口下按钮与提示文本继续挤压。

---

## 4. 严重性分级

| 级别 | 问题 | 影响 |
|---|---|---|
| **P0** | Viewport/Inspector `RowSpan=3` 与底部 `Row=2` TabControl 重叠 | Inspector 后半段不可见；Apply RigidBody 越出窗口 |
| **P0** | Inspector 无 ScrollViewer，内容随组件增长越界 | 关键属性无法编辑 |
| **P0** | Tab 内容透明叠在黑色 Viewport 上 | Tab 标题、Script 空态、部分控件不可读 |
| **P1** | 固定列/行、无 Splitter、无 Min 尺寸 | 900×600 破版；1920 浪费空间；无法按任务调整 |
| **P1** | Viewport 1:1 图像不填充/不居中/无 letterbox | 大屏空区；小屏可视区过小；picking 前提需保留 |
| **P1** | 工具栏无分组/主次/运行态；调试提示常驻 | 主操作不突出，运行状态不明确 |
| **P1** | Console 无级别/来源视觉；过滤/清空低对比 | 诊断信息难扫读；T-6-A 功能可见性被削弱 |
| **P1** | 选中/悬停/聚焦/禁用/运行/错误状态不统一 | 用户需要猜测控件状态 |
| **P2** | 字体/间距/透明度无 token；Asset 列表无列头；空态不统一；生产标题含 Phase 文案 | 一致性差，新增面板会继续累积风格 |

---

## 5. 对 UI-H1 的输入（不实施）

UI-H1 至少需要冻结：

1. **主题基线**：明确 Dark 或跟随系统；当前“深色 TextBox + 白色列表 + 黑色
   Viewport + 透明 Tab”是混合结果，不能作为规范。
2. **布局模型**：保留四区 Grid，还是接入现有 Dock 模型；无论哪种，先消除
   RowSpan/Row 2 重叠，并定义可调整分隔与最小尺寸。
3. **层级/字号/间距 token**：应用级、面板级、分组级、控件级；4px 基准；
   辅助文本不得依赖任意 opacity。
4. **语义色与对比度**：背景/面板/分隔/文字/选中/聚焦/警告/错误/禁用；
   正文至少 4.5:1，非文本 UI 至少 3:1；不再用黑字叠黑底。
5. **控件与面板规则**：主/次/危险按钮、工具栏分组、Tab、输入、列表、Console
   级别、Inspector 滚动、资产列头。
6. **Viewport 适配策略**：在保持 picking 坐标前提的前提下，定义 letterbox/
   居中/缩放边界；不得直接改 `Stretch` 而不更新 mapper。
7. **状态语义**：选中、悬停、聚焦、禁用、运行、脏、错误、警告、空态的统一表达。
8. **验收方法**：900×600 / 1100×720 / 1440×900 / 1920×1080 截图矩阵，
   UIA 边界断言“关键控件在窗口内、可滚动、可点击”，并跑既有功能门禁。

---

## 6. 非目标

- 不在 UI-H0 修改任何源码或样式。
- 不把全局改版塞进 T-6-A；T-6-A 的功能与冻结矩阵保持不变。
- 不把 Nova Shell 发展成平行 UI 主线；主线仍是 `editor_avalonia`。
- 不因视觉工作新增 Bridge ABI、EngineHost 诊断或 Engine 日志能力。
- 不在 UI-H1 冻结前启动全局 UI 美化实现。

---

## 7. 证据索引

| 事实 | 位置 |
|---|---|
| 四区 Grid / 固定尺寸 | `editor_avalonia/MainWindow.axaml:43` |
| RowSpan=3 | `MainWindow.axaml:46,74,130` |
| 底部 TabControl Row=2 Span=3 | `MainWindow.axaml:211` |
| 无 Splitter/MinSize | `MainWindow.axaml` 全文 |
| Theme 只有 FluentTheme/Default | `editor_avalonia/App.axaml:4-6` |
| 生产标题包含 Phase 文案 | `MainWindow.axaml:5` |
| 默认 1100×720 截图 | `docs/evidence/UI-H0/01_default_1100x720_selected.png` |
| 1440×900 / 900×600 / 1920×1080 | `docs/evidence/UI-H0/02…04_*.png` |
| Asset/Script/Console | `docs/evidence/UI-H0/05…07_*.png` |
| RigidBody/Camera 溢出 | `docs/evidence/UI-H0/08…09_*.png` |
| UIA 边界（Apply RigidBody y=903 > 窗口底 811） | `build/uih0_bounds.ps1` 输出 |
