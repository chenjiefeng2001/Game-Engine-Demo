# Design Tokens

实现：`src/Nova.UI.Core/Tokens/TokenKeys.cs`、`NovaTokens.cs`、`DesignTokenSet.cs`。
主题：`src/Nova.UI.Theme/NovaThemeCatalog.cs`。
Avalonia 映射：`src/Nova.UI.Avalonia/NovaThemeAdapter.cs`、`NovaResourceKeys.cs`。

## 类型

| TokenType | 例子 | Avalonia 资源 |
| --- | --- | --- |
| Color | `color.bg.panel = #1D2025` | `Nova.Color.<key>` (`Color`) / `Nova.Brush.<key>` (`SolidColorBrush`) |
| Dimension | `spacing.md = 12` | `Nova.Number.<key>` (`double`) |
| FontSize | `font.size.sm = 12` | `Nova.Number.<key>` (`double`) |
| FontFamily | `font.family.ui = Segoe UI, ...` | `Nova.FontFamily.<key>` |
| FontWeight | `font.weight.semibold = SemiBold` | `Nova.FontWeight.<key>` |
| Duration | `duration.fast = 120` (ms) | `Nova.Duration.<key>` (`TimeSpan`) |

## 必需键（节选）

| 组 | 键 |
| --- | --- |
| 背景 | `color.bg.canvas/panel/elevated/input/hover/selected` |
| 前景 | `color.fg.primary/secondary/muted` |
| 语义 | `color.accent/accent.hover/accent.pressed`, `color.danger/warning/success`, `color.focus`, `color.border/border.strong`, `color.overlay` |
| 字体 | `font.family.ui/mono`, `font.size.xs/sm/md/lg/xl`, `font.weight.regular/semibold` |
| 间距 | `spacing.xxs/xs/sm/md/lg/xl` |
| 圆角 | `radius.sm/md/lg` |
| 尺寸 | `size.control.height(.sm/.lg)`, `size.icon`, `size.panel.header`, `size.splitter`, `size.panel.min-width/min-height` |
| 动效 | `duration.fast/normal` |

完整清单由 `TokenKeys.Required` 定义；`DesignTokenSet.Validate` 校验缺失键与语法。

## 规则

1. 新增控件状态优先复用现有 token；不足时才新增 token，并同时更新 Dark/Light。
2. token 值使用 invariant culture 的字符串表示。
3. 颜色支持 `#RRGGBB` 与 `#AARRGGBB`。
4. 主题切换通过重建 Avalonia resources/styles 完成；控件本身不包含主题分支。
5. 主题缺失/非法时启动失败应 fail-fast（`NovaThemeAdapter.Apply` 抛异常）；布局损坏则 fail-soft（回退默认布局）。

## 当前内置主题

- `nova.dark` — 默认离线深色主题。
- `nova.light` — 用于验证切换不会改变控件行为。

切换：`ThemeManager.Toggle()` 或命令 `theme.toggle`；当前主题 ID 持久化在 `WorkspaceLayout.ThemeId`。
