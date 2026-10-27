# ADR-004: 先固定工作区/场景生命周期，再进入 Docking

- 状态：Accepted
- 日期：2026-10
- 实现：`src/Nova.Editor.Services/EditorWorkspace.cs`, `Projects/`
- Gate：`tests/Nova.Editor.IntegrationTests/LifecycleTests.cs`

## 背景

v0.2 的 `EditorWorkspace` 只有一个文档和一个 `CommandHistory`。在加入 Docking、Timeline 或真实视口前，必须先定义项目、场景、编辑会话的生命周期，否则旧命令、旧草稿和旧选择可能跨上下文污染。

## 决策

1. 区分 `ProjectId`、`SceneId`、`SessionId`。`SessionId` 同时作为文档命令上下文。
2. 项目/场景切换集中在 `EditorWorkspace`，ViewModel 不自行清空历史或选择。
3. 先创建候选 `SceneDocument`，成功后才替换当前 document；加载失败保留旧会话。
4. 关闭支持确认回调；取消关闭不得改变历史、草稿、选择或布局。
5. 关闭后旧命令通过 context token 与空历史双层失效。
6. 未提交 Inspector 草稿在选中对象/场景/项目切换时丢弃，不隐式提交。
7. 项目默认布局在项目打开时应用；场景切换保留当前布局；工作区重开由持久化布局恢复。

## 后果

- `EditorWorkspace.Document` 从只读单文档变为可替换的当前文档。
- `CommandHistory.Reset` 可在保留服务实例/订阅的同时切换上下文。
- `SessionChanged` 成为 Shell 重建层级/Inspector 的正式事件。
- 真实项目文件加载、草稿确认对话框、多窗口仍待后续切片。

## 测试证据

- `Project_switch_invalidates_old_history_and_session`
- `Scene_switch_keeps_project_and_layout_but_resets_session`
- `Scene_load_failure_preserves_the_current_session`
- `Cancelled_close_preserves_document_history_and_selection`
- `Close_invalidates_commands_and_clears_projection`
- `Reopen_creates_a_new_session_without_reviving_old_commands`
- `External_selected_node_delete_does_not_write_or_keep_selection`
- `Repeated_scene_switches_do_not_duplicate_session_events`
- `Repeated_close_and_reopen_cycles_do_not_duplicate_events`
- `Uncommitted_text_draft_is_discarded_on_selection_and_session_changes`
- `Cancelled_close_keeps_inspector_draft_selection_and_history`
- `External_selection_delete_clears_inspector_projection`
- `Shell_scene_and_close_commands_update_the_projection`
