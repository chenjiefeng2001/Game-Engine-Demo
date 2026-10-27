# ADR-005: Nova Dock Tree 纯模型（复用 v0.3 Docking 契约）

- 状态：Accepted
- 日期：2026-10-25
- 契约来源：`p7_ui` worktree（分支 `avalonia-p7`）的
  `docs/ui-spec/11-docking-model.md` 与
  `docs/decisions/ADR-009-dock-tree-workspace-state.md`
- 实现：`src/Nova.UI.Core/Docking/`
- 门禁：`tests/Nova.UI.Core.Tests/DockTreeTests.cs`（WM-01..14 主树子集）

## 背景

Nova 的 v0.2 布局是固定四区域 `WorkspaceLayout`，不能表达可停靠面板。
v0.3 Docking 规范已经冻结了纯数据模型、二叉 Split + TabGroup、身份命名空间、
候选事务、规范化和 WM-01..21 验收矩阵。本切片直接复用该契约，不另建一套
Docking 架构。

## 决策

1. **范围**：只实现主工作区树。浮动根、隐藏面板、未注册面板、JSON 持久化、
   Avalonia DockHost、拖放/Splitter UI 和布局 Undo 均不在本切片。
2. **节点**：`DockSplitNode` 固定二叉，方向为 Horizontal（左右）或
   Vertical（上下）；`DockTabGroupNode` 保存有序面板和活动面板。根只能是
   非空 Split 或 TabGroup。
3. **身份**：`DockNodeId` 与 `PanelInstanceId` 是 `UiId` 的强类型包装；
   面板实例 ID 在整棵树内唯一，标题、类型名和数组下标不得作为长期身份。
   本切片所有面板按唯一实例处理，实例 ID 可直接使用面板类型 ID。活动标签由
   `DockTabGroupNode.ActivePanel` 表达；场景选择仍由既有 `SelectionService`
   持有，Dock 模型不复制选择权威。
4. **未知面板**：模型把面板 ID 当作不透明稳定值，不查询注册表；未注册面板
   的过滤/隔离属于后续加载管线，不在本切片伪造。
5. **不变量**（复用 DM-001..013 的主树子集）：节点 ID 与面板 ID 唯一；
   Split 恰好两个非空子节点；组非空；活动面板属于本组；权重有限且为正，
   和为 1（容差 `1e-6`）；深度 ≤ 64；节点 ≤ 512；面板 ≤ 256。
6. **原子性**：每次操作构造候选树，规范化、完整校验后返回新的不可变
   `DockTree`；失败返回 `DockOperationResult`，原树与摘要逐字段不变。
   本切片不引入 `Revision`；协调器和持久化切片再增加版本号。
7. **操作**：`AddPanel`、`MovePanel`（Center 插页 / Edge 边缘拆分）、
   `ReorderTab`、`ActivateTab`、`RemovePanel`、`MergeGroups`、
   `ResizeSplit`。边缘拆分的节点 ID 使用确定性的 `dock.split.N` /
   `dock.tabs.N` 分配，保证相同操作序列产生相同结构。
8. **稳定结构**：面板顺序和活动面板显式保存；`DockTreeDigest` 生成规范化
   canonical 摘要，作为测试和后续持久化的稳定比较点。

## 与既有契约的映射

| 既有项 | 本切片处置 |
| --- | --- |
| WM-01..14 | 在 Nova 模型门禁中实现并保持同一编号语义 |
| WM-15（隐藏/显示） | 延后；本切片不包含 `HiddenPanels` |
| WM-16、WM-19..21（浮动根） | 延后；本切片不包含 `FloatRoot` |
| WM-17（未注册面板加载） | 延后；模型不查询注册表 |
| WM-18（失败原子性） | 覆盖所有模型操作 |
| 持久化、DockHost、交互 | 延后到后续切片 |

## 后果

- 模型可在无 Avalonia、无文件 I/O 的测试中完整验证。
- 现有 `WorkspaceLayout` 暂不迁移；DockTree 先作为独立模型存在，后续由
  适配/持久化切片接线。
- 不提供布局 Undo；`MergeGroups` 等操作通过重新操作或恢复默认布局回退。
- 浮动、隐藏、未知面板的既有契约不因本切片被改写或删除。

## 实现与验证（2026-10-25）

实现位于 `src/Nova.UI.Core/Docking/`：

| 文件 | 职责 |
| --- | --- |
| `DockIds.cs` | `DockNodeId`、`PanelInstanceId` 强类型身份 |
| `DockNode.cs` | 不可变 `DockSplitNode`、`DockTabGroupNode` |
| `DockResult.cs` | `DockError`、`DockOperationResult`、`DockTarget` |
| `DockValidation.cs` / `DockValidator.cs` | DM 主树子集的完整校验 |
| `DockNormalizer.cs` | 确定性、幂等的结构规范化 |
| `DockTree.cs` | 创建、查询、候选事务操作与原子提交 |
| `DockTreeDigest.cs` | canonical SHA256 布局摘要 |
| `DockPanelIds.cs` | 默认面板实例身份 |

`DockTree` 不可变；每次成功操作返回新树，失败返回
`DockOperationResult` 且不返回树。`DockTree.Create` 拒绝无效原始树；
`DockTree.Normalize` 只修复结构冗余，不修复非法权重、重复身份等语义错误。

门禁结果（均为真实退出码）：

- `dotnet build Nova.sln -c Debug`：exit 0，0 错误，18 条 `NU1900`
  （Azure China 漏洞源不可达，与代码无关）。
- `dotnet build Nova.sln -c Release`：exit 0，0 错误，18 条 `NU1900`。
- `dotnet test Nova.sln -c Debug --no-build`：98/98 通过。
- `dotnet test Nova.sln -c Release --no-build`：98/98 通过。
- `dotnet test tests/Nova.UI.Core.Tests --filter FullyQualifiedName~DockTreeTests`：
  25/25 通过，覆盖 WM-01..14、WM-18 及 Add/Move/Reorder/Merge/Resize/
  Remove、根组边缘拆分、未知面板不透明性和接收者不可变性。
- `dotnet format Nova.sln --verify-no-changes --no-restore`：exit 0。

本切片未启动 DockHost、拖放、Splitter UI、浮动窗口、持久化、隐藏或未解析
面板；`editor_avalonia`、native/gate 和其他无关 dirty 文件未被修改。
