# P0 Primitives

实现：`src/Nova.UI.Controls`；样式类：`NovaControlClasses`。
原则：优先继承 Avalonia 标准控件与模板；只有标准控件无法满足绘制/性能需求时才自绘。

> 继承标准控件的 Nova 控件必须覆写 `StyleKeyOverride` 返回基类类型（例如 `TextBox`、`Button`、`Expander`），否则 Avalonia 会按具体子类查找 ControlTheme，导致模板缺失、控件不可见。Playground 截图回归已捕获过这一类缺陷。

## 布局与视觉

| 控件 | 基类 | 说明 |
| --- | --- | --- |
| `NovaBox` | `Border` | 默认 8px padding 的容器 |
| `NovaRow` | `StackPanel` | 水平、Spacing=4 |
| `NovaColumn` | `StackPanel` | 垂直、Spacing=4 |
| `NovaGrid` | `Grid` | 无行为包装，统一类型词汇 |
| `NovaScrollArea` | `ScrollViewer` | 双向滚动条 Auto |
| `NovaSurface` | `Border` | `nova-panel`（panel 背景/边框/圆角） |
| `NovaElevatedSurface` | `Border` | `nova-elevated` |
| `NovaText` | `TextBlock` | 继承全局字体/前景 |
| `NovaMutedText` | `TextBlock` | `nova-muted` |
| `NovaIcon` | `TextBlock` | `Glyph` → `Text` |
| `NovaDivider` | `Border` | 1px 分隔线 |
| `NovaSplitter` | `GridSplitter` | 预览关闭、最小 4px |

## 输入控件

| 控件 | 基类 | 关键 API | 状态 |
| --- | --- | --- | --- |
| `NovaButton` | `Button` | `Kind`：Default/Primary/Subtle/Danger | hover/pressed/disabled |
| `NovaIconButton` | `NovaButton` | `Glyph` | 同上 |
| `NovaTextField` | `TextBox` | `IsInvalid`, `TextCommitted`, `MarkCommitted` | focus/invalid/disabled |
| `NovaNumberField` | `TextBox` | `Value` (TwoWay), `Minimum`, `Maximum`, `Step`, `Format`, `ValueCommitted`, `Commit()`, `StepBy()` | 中间编辑态、invalid、focus |
| `NovaToggle` | `CheckBox` | `Label` | checked/disabled |
| `NovaSelect` | `ComboBox` | 继承 ItemsSource/SelectedItem | focus/hover/disabled |

### `NovaNumberField` 契约

- 键盘输入先进入 `NumberEditState`，不立即写模型。
- `1.`、`-`、`1e` 等中间文本可存在；`IsInvalid` 在文本非空且不可解析时置位。
- Enter / 失焦 / spinner 才提交；提交时 clamp 到 `Minimum`/`Maximum`。
- Escape 回退到最后一次提交值。
- 外部 `Value` 变化会覆盖编辑文本；外部值为 `null` 显示空串。
- `ValueCommitted` 只在成功提交后触发。

## 编辑器控件

| 控件 | 基类 | 说明 |
| --- | --- | --- |
| `NovaPropertyRow` | `UserControl` | `Label` + `Value` + `Description`；`Value` 接受任意编辑控件 |
| `NovaInspectorSection` | `Expander` | 默认展开；键盘/焦点由 Expander 提供 |

## 集合控件

| 控件 | 基类 | 虚拟化 |
| --- | --- | --- |
| `NovaTreeView` | `TreeView` | Avalonia 内置容器虚拟化；不要替换为非虚拟化 ItemsPanel |
| `NovaListView` | `ListBox` | ListBox 默认虚拟化；用于 Console / 大列表 |

## 样式类

`NovaControlClasses` 是唯一公开的类名词汇：`nova-button`, `nova-primary`, `nova-subtle`, `nova-danger`, `nova-text-field`, `nova-number-field`, `nova-invalid`, `nova-property-row`, `nova-inspector-section`, `nova-panel`, `nova-elevated`, `nova-toolbar`, `nova-divider`, `nova-mono`, `nova-muted`, `nova-section-title`, `nova-property-label`。

新增控件类必须同时：
1. 在 `NovaControlClasses` 声明；
2. 在 `NovaThemeAdapter` 提供样式；
3. 在 `Nova.UI.Controls.Tests` 覆盖；
4. 在 Playground 展示。
