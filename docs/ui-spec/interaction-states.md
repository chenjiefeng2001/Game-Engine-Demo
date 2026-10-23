# Interaction States

## 焦点

- 可交互控件使用 Avalonia 默认 Tab 顺序；不要通过 `IsTabStop=false` 规避布局问题。
- 自定义控件继承标准控件，保留其焦点行为（Button/TextBox/ComboBox/Expander/TreeView）。
- 焦点视觉由 token `color.focus` 驱动；TextBox focus 边框使用该 token。

## 键盘

| 场景 | 行为 |
| --- | --- |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo（shell KeyBinding → CommandRouter 命令） |
| `Ctrl+Shift+N` | 新增 Entity |
| `Delete` | 删除选中实体（根节点禁止） |
| `Ctrl+Shift+T` | 切换主题 |
| NumberField `Up`/`Down` | 按 `Step` 增减并提交 |
| NumberField `Enter` | 提交 |
| NumberField `Escape` | 回退到最后提交值 |
| TextField `Enter` / blur | 触发 `TextCommitted`，由行 ViewModel 走命令层 |

命令注册表 `CommandDescriptor.Gesture` 是快捷键元数据；实际 KeyBinding 由 Host/View 安装。v0.2 在 `EditorShellView` 中显式安装，便于后续迁移到可配置快捷键服务。

## 禁用态

- 按钮 disabled 使用 opacity 0.45，由主题样式统一设置。
- 命令层 `CanExecute` 是唯一事实源；菜单/工具栏按钮状态应绑定命令可用性。
- 禁用控件不进入键盘交互；TreeView/ListBox 的禁用继承 Avalonia 行为。

## 错误态

- `NovaTextField.IsInvalid` / `NovaNumberField` 解析失败添加 `nova-invalid` 类，边框使用 `color.danger`。
- 模型校验失败（命令层）不会写入模型，不产生撤销记录；诊断进入 Console。
- `PropertyRowViewModel` 在失败时从模型重新同步显示值，避免 UI 与模型漂移。

## 主题切换

- 主题是数据；切换只重建 resource/style 集合。
- 控件的状态机、命令、焦点不随主题改变。
- 当前主题 ID 写入 `WorkspaceLayout.ThemeId` 并持久化。
