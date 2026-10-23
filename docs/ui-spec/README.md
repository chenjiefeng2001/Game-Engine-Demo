# Nova UI Spec (v0.2)

本目录是 Nova UI 的实现契约，不是设计灵感集合。任何控件、主题或布局改动应至少更新对应条目，并在 `tests/` 中有可执行证据。

## 依赖规则

```
Nova.UI.Abstractions     (no Avalonia, no editor business)
        ↑
Nova.UI.Core             (tokens, state store, commands, layout model)
        ↑
Nova.UI.Theme            (theme definitions, catalog, active theme)
        ↑
Nova.UI.Avalonia         (token adapter, UiId attached property, state binding)
        ↑
Nova.UI.Controls         (P0 controls, style class vocabulary)
        ↑
Nova.Editor.Services     (scene model, selection, commands, persistence, workspace)
        ↑
Nova.Editor.Views        (shell view + view models)
        ↑
Nova.Editor.App          (composition root / window)
```

硬性规则：

- `Nova.UI.Core` 不得引用 Avalonia 或 `Nova.Editor.*`。
- 控件不得直接修改 `SceneNode`、`SceneDocument` 或 native/engine 对象。
- 不允许通过全局静态变量隐式共享 workspace 状态。`UiStateStore`、`SelectionService`、`ThemeManager` 均由 workspace 实例持有。
- 视图只通过 ViewModel → workspace/命令层修改模型。
- 颜色、尺寸、字体、圆角、动效时长必须来自 token；控件代码只使用样式类。

## 文档索引

- [design-tokens.md](design-tokens.md) — 令牌键、类型、主题与资源映射。
- [primitives.md](primitives.md) — P0 控件/布局原语 API 与状态。
- [interaction-states.md](interaction-states.md) — 焦点、键盘、禁用、错误、主题切换。
- [layout-and-rendering.md](layout-and-rendering.md) — Shell 布局、Splitter、虚拟化、持久化、渲染边界。
- [update-model.md](update-model.md) — 稳定 ID、局部更新、命令与撤销。
- [screenshot-and-baselines.md](screenshot-and-baselines.md) — 当前基线策略与缺口。
