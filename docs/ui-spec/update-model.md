# Update Model

## 稳定 ID

`UiId` 是不可变字符串标识。可用于：

- scene node identity（示例：`entity.player`）；
- UI projection slot（`inspector.header`, `status.selection`）；
- command id（`editor.undo`, `scene.add-node`）。

禁止使用数组下标、控件名称或 `GetHashCode()` 作为持久身份。

## 状态投影

`IUiStateReader` / `IUiStateStore`：

```csharp
store.Set(nodeId, "property.position.x", 4.25);
store.TryGet<double>(nodeId, "property.position.x", out var value);
```

- 写入相同值不会发通知，也不会推进 `Revision`。
- 订阅者只处理匹配 `(UiId, key)` 的变化；不重建无关面板。
- 变更日志有界（默认 4096）；落后消费者应重新读取当前值。
- 订阅者异常被隔离：坏的 binder/console sink 不会回滚已应用的模型修改，也不会阻止命令进入历史。v0.2 对这种投影失败只做 best-effort 诊断，不保证 UI 与模型立即重新对齐。
- workspace 拥有 store；不提供全局单例。

## Avalonia 绑定

```csharp
NovaUiBinding.Bind(
    target: StatusText,
    property: TextBlock.TextProperty,
    store: workspace.UiState,
    id: EditorUiIds.SelectionStatus,
    key: "text");
```

- 只写目标 AvaloniaProperty；不替换 DataContext，不重建视觉树。
- store 在后台线程写入时通过 `Dispatcher.UIThread` 回到 UI 线程。

## 命令层

```
Menu/Toolbar/Shortcut
  → CommandRouter.Execute(id)
  → handler (workspace)
  → CommandHistory.Execute(IUndoableCommand)
  → command.Execute()  (model mutation)
  → projection callback / StructureChanged / Log
```

- 路由是边界：handler 异常转换为 `CommandResult.Fail`，不击穿 shell。
- 新命令会清空 redo 栈。
- 命令执行失败不进入历史。
- 结构性变更（增删节点）触发 `StructureChanged`；属性变更不触发。
- 属性编辑通过显式 `SceneNodeSchema` 校验、clamp，再创建 `EditPropertyCommand`。

### 上下文与陈旧命令

- 每个 `SceneDocument` 有不可变 `ContextId`。
- 生产命令实现 `ContextId`，`CommandHistory` 用同一个 token 创建。
- `CommandHistory.Execute/Undo/Redo` 会拒绝来自其他上下文的命令。
- `IUndoableCommand.CanUndo()` 与 `CanExecute()` 分离；节点被外部删除后，Undo 不会把旧值写回一个已脱离文档的节点。
- `CommandHistory.CanUndo/CanRedo` 读取栈顶命令的实际可用性，菜单/工具栏状态随之为灰色。
- 当前未实现“切换场景/工作区”UI；上述 token 是生命周期切片的基础，不应被描述为完整生命周期管理。

## 局部更新验收

`WorkspaceCommandChainTests` 验证：

- 属性编辑只产生一条历史记录；
- `UiStateStore` 只更新对应 `(node, property)` 槽位；
- undo/redo 恢复模型与投影；
- 非法输入不写入模型、不产生历史记录。
