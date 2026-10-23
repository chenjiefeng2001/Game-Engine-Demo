# ADR-003: 布局持久化异步、原子、失败回退

- 状态：Accepted
- 日期：2026-08
- 实现：`Nova.UI.Core/Layout`, `Nova.Editor.Services/Layout/LayoutPersistenceService.cs`

## 决策

1. 布局模型使用带 `SchemaVersion` 的 record，字段明确；v0.2 只支持固定四区 Shell。
2. 读取在启动异步完成；UI 先显示默认布局，不阻塞首帧。
3. 损坏/不兼容/不可读 → `WorkspaceLayout.Default`，记录原因；损坏文件尽力隔离为 `.corrupt-*`。
4. 保存写同目录 temp 文件后原子替换；`SemaphoreSlim` 串行化并发保存。
5. 关闭窗口时取消首次关闭，等待异步保存后重新关闭；不在 UI 线程做文件 I/O。

## 后果

- 布局文件损坏不会阻止离线启动。
- 旧 schema 暂不迁移，直接回退默认；v0.3 Dock 模型需要显式迁移器。
- 面板隐藏保留上次尺寸；恢复时使用保留值，而不是 0。
- 原子替换在 Windows 上依赖 `File.Move(overwrite: true)` 的同卷语义。

## 测试证据

- `LayoutSerializerTests`：schema/损坏/归一化。
- `LayoutPersistenceTests`：磁盘往返、缺失文件、损坏隔离、未知 schema 隔离、原子替换失败保留旧文件、并发保存无 temp 残留。
