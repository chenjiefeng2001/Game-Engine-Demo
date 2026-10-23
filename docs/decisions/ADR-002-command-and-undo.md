# ADR-002: 所有模型修改经过命令层，撤销使用命令对象

- 状态：Accepted
- 日期：2026-08
- 实现：`Nova.UI.Abstractions/Commands.cs`, `Nova.UI.Core/Commands`, `Nova.Editor.Services/Commands`

## 上下文

原 ImGui 编辑器允许控件直接改 GameObject/组件；撤销信息只能靠额外快照，容易出现 UI 与模型、撤销与重做不一致。Avalonia 属性绑定也容易把“按键即写模型”变成默认行为。

## 决策

1. 控件只产生“编辑意图”（例如 `NumberValue` 提交、`TextCommitted`）。
2. ViewModel/Workspace 调用 schema 校验，创建 `IUndoableCommand`。
3. `CommandHistory` 执行并记录；新命令清空 redo。
4. 路由层 `CommandRouter` 为菜单/工具栏/快捷键/自动化提供统一入口；异常转为失败结果。
5. 属性变更通过投影回调写 `UiStateStore`；结构性变更单独触发 `StructureChanged`。

## 后果

- 控件不能直接持有/修改 `SceneNode`，需要 ViewModel 中介。
- 命令必须成对实现 `Execute`/`Undo`；组合命令需保持顺序可逆。
- 撤销栈不持久化（v0.2）；跨会话恢复历史不属于本版本。
- 属性编辑不重建 Hierarchy；只有增删/结构 undo 触发 `StructureChanged`。
- 命令携带 `ContextId`；`CommandHistory` 绑定当前 `SceneDocument.ContextId`，拒绝来自其他工作区的命令。
- `CanUndo()` 与 `CanExecute()` 分离：节点被外部删除后，历史中的属性命令不可再 Undo，UI 的 Undo 命令也应变为不可用。
- 命令抛出异常时不得进入历史；若未来引入可能“部分修改后抛异常”的复合命令，必须增加事务/补偿，不能只保证历史栈不变。

## 测试证据

- `CommandHistoryTests`：顺序、redo 清空、深度上限、异常不记录。
- `WorkspaceCommandChainTests`：真实 schema 校验、clamp、undo/redo、投影同步、无变化提交、节点删除后的陈旧命令、跨工作区命令拒绝。
- `CommandHistoryTests`：上下文隔离、`CanUndo` 与栈状态一致性、异常不记录。
- `InteractionBudgetTests`：1 万节点下命令开销。
