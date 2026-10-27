# Workspace / Scene Lifecycle (Lifecycle v0.1)

实现：`src/Nova.Editor.Services/Projects/`、`EditorWorkspace.OpenProject/OpenScene/CloseProject`。
Gate：`tests/Nova.Editor.IntegrationTests/LifecycleTests.cs`。

## 身份

| 身份 | 类型 | 说明 |
| --- | --- | --- |
| `ProjectId` | `Guid` | 项目身份；项目切换后变化 |
| `SceneId` | `UiId` | 场景身份；项目内唯一 |
| `SessionId` | `Guid` | 当前可执行命令的编辑会话；每次打开/重开场景都变化 |
| `SceneDocument.ContextId` | `Guid` | 文档命令上下文；等于当前 `SessionId` |

命令携带 `ContextId`；`CommandHistory` 绑定当前会话。旧场景/旧项目的命令即使仍被外部持有，也不能在新上下文执行。

## 状态转移

| 操作 | 命令历史 | Inspector 草稿 | 选中对象 | 布局 |
| --- | --- | --- | --- | --- |
| 新建项目 | 新 session，空历史 | 丢弃 | 新场景默认对象 | 项目默认布局或 `WorkspaceLayout.Default` |
| 切换项目 | 旧历史失效 | 丢弃 | 新场景默认对象 | 目标项目默认布局 |
| 切换场景 | 旧 session 历史失效 | 丢弃 | 新场景默认对象 | 保留当前工作区布局 |
| 关闭项目 | 清空并绑定新 session | 丢弃 | 清空 | 保存或保留，由调用方决定 |
| 取消关闭 | 完全保留 | 保留 | 保留 | 不变 |
| 重开工作区 | 新 session、空历史 | 不恢复过期草稿 | 新场景默认对象 | 从持久化布局恢复 |

### 失败语义

`Transition` 的顺序：

1. 解析目标项目/场景。
2. **先创建候选 `SceneDocument`**；factory 抛异常则立即返回失败。
3. 只有候选创建成功后才替换 document/project/scene/session。
4. `History.Reset(newSession)`，清空 selection/UI state。
5. 选中新场景默认对象。
6. 最后发布 `SessionChanged`。

因此加载失败不会留下“新场景已选中、旧模型仍活动”的半切换状态；旧 document、history、selection、draft 上下文保持不变。

## 草稿策略

Lifecycle v0.1 的产品策略是：

> 切换选中对象、场景或项目时，未提交的 Inspector 文本草稿被丢弃；不会隐式提交，也不会写入旧对象。

原因：

- 未提交草稿没有可靠的 schema 校验；
- 跨场景/项目隐式提交会把目标对象的新上下文污染；
- v0.1 暂不引入保存/丢弃确认对话框；未来可在命令层加入确认，View 不自行处理。

数字字段的中间态由 `NumberEditState` 保持；未 Enter/blur 前不会写模型。

## 外部删除

`TryEditProperty` 在写模型前再次按 `UiId` 查文档。目标不存在时：

- 不创建命令；
- 不写旧对象；
- 清除仍然指向该对象的 selection；
- 返回失败，由 Shell 显示和记录。

## 重开

关闭后重新 `OpenProject`/`OpenScene` 会创建新的 `SessionId` 和 `SceneDocument`。旧 `CommandHistory` 已清空且上下文不同，旧命令无法执行。布局由 `LayoutPersistenceService` 恢复；命令、选择、草稿不会从旧 session 复活。

## 事件与订阅

- `SessionChanged` 在项目/场景切换和关闭后发布。
- `StructureChanged` 只用于节点增删/结构性 undo。
- 事件订阅者异常被隔离，不能回滚已完成的转移。
- 重复切换复用同一个 workspace 服务实例，不重复订阅；Gate 通过事件计数检查。

## 未覆盖

- 真实磁盘项目/场景加载与保存。
- 关闭前“保存/丢弃/取消”对话框。
- 多窗口共享同一项目。
- 跨 session 的 draft 恢复。
- 真实 viewport 生命周期。
