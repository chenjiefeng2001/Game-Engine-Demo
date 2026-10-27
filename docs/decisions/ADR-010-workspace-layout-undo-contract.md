# ADR-010: 工作区布局撤销与重做契约（D9-A 已冻结）

- 状态：**Accepted（已于 2026-10-26 由用户裁决批准冻结，O-1…O-8 全部接受，
  允许进入 D9-B）**。本文件是布局历史的权威契约；D9-B 之后的实现阶段只能
  向本契约对齐，不得反向修改已冻结条款
- 阶段进度（2026-10-27 用户裁决）：D9-A 冻结 ✅ / D9-B ✅ / D9-C ✅ / D9-D ✅ /
  D9-E 正式通过（HOLD 解除）✅ / D9-F 最终回归与封印 **通过** ✅ →
  **D9 = CLOSED — scope-bound**。关闭不扩大已验证范围：多显示器／混合 DPI／无人值守
  环境未验证，§11.4 的未指名失败仍为"未判定"，详见 §11.7–§11.9。
- 日期：2026-10-26
- 冻结命名：`DockLayoutSnapshot`（`src/Nova.UI.Core/Docking/DockLayoutSnapshot.cs`）
  与 `DockLayoutHistory`（`src/Nova.UI.Core/Docking/DockLayoutHistory.cs`）；
  操作来源枚举 `DockLayoutOperationSource { Direct, Undo, Redo }`；
  容量常量 `DockLayoutHistory.DefaultMaxUndoDepth = 100`
- 关联：ADR-002（命令层与撤销）、ADR-005（DockTree 模型）、ADR-006（Avalonia
  投影）、ADR-007（多根与浮动宿主）、ADR-008（多根工作区持久化）、
  ADR-009（隐藏／未解析面板生命周期，已关闭）
- 前置：D8 已于 2026-10-26 由用户裁决关闭。D9 不得为布局 Undo 修改 D6–D8
  已封存的模型、视图所有权与持久化契约
- 工作树：保持未提交，无 commit/push/tag/merge；`editor_avalonia`、
  `native/gate` 及其他无关 dirty 文件保持原状

## 一、目标与定位

D9 要解决的问题：用户执行布局操作后，能否安全回到此前的工作区状态，且**不丢失
面板身份、标签顺序、hidden 状态、浮动根几何，也不破坏 D6–D8 已验收的事务、
资源所有权与持久化语义**。

布局历史与场景命令历史是两套独立机制：

| 概念 | 现状 | D9 关系 |
| --- | --- | --- |
| 场景实体命令历史 | 已存在：`CommandHistory`（`src/Nova.UI.Core/Commands/CommandHistory.cs:18`，`maxDepth = 100`），由 `EditorWorkspace` 持有并在 `editor.undo` / `editor.redo` 中执行（`src/Nova.Editor.Services/EditorWorkspace.cs:828-841`，手势 `"Ctrl+Z"` :833 / `"Ctrl+Y"` :841；ID 定义 `src/Nova.UI.Abstractions/Commands.cs:106-107`），快捷键绑定于 `EditorShellView.axaml:8-9` | 不共用、不合并、不替换 |
| 工作区布局历史 | **不存在**：`Nova.UI.Core/Docking`、`Nova.UI.Avalonia/Docking`、`Nova.Editor.Views/Docking` 三个命名空间中 `Undo`/`Redo` 标识符 0 命中。现有涉及 Undo 的测试（`CommandHistoryTests`、`WorkspaceCommandChainTests`、`ShellViewModelTests`、`LifecycleTests`、`InteractionBudgetTests`）全部只覆盖场景命令历史 | D9 新增 |
| 布局持久化 | D7/D8：canonical JSON + 500 ms 防抖 + 健康门控 + 去重 + 未解析抑制 | 只读复用，不改策略 |

## 二、现状基线核对（契约必须绕开的事实）

1. **模型变更入口分散**：`DockWorkspace` 的可变操作均返回
   `DockWorkspaceResult`（`DockWorkspaceResult.cs:22-33`，失败不携带工作区）：
   `FloatPanel:122`、`MovePanel:200`、`ReDockFloatRoot:244`、`MergeFloatRoot:258`、
   `CloseFloatRoot:307`、`UpdateFloatBounds:323`、`RemovePanel:366`、
   `ActivateTab:424`、`HidePanel:445` / `ShowPanel:448`、`ResizeSplit:490`、
   `RestoreDefault:514`。`Revision`（`DockWorkspace.cs:25`）只在私有
   `CommitWorkspace` 自增（`DockWorkspace.cs:712`）。
2. **协调器无单一提交漏斗**：`DockWorkspaceCoordinator` 有 14 处
   `_workspace =` 赋值；但所有"模型已提交"的结果都经 `CommittedSuccess`
   （`DockWorkspaceCoordinator.cs:1793`）或 `CommittedHostFailure`（:1808）
   返回，且两者都调用 `NotifyStateChanged`（:1823）。这两个方法是候选记录点。
3. **两个必须显式处理的既有分支**：
   - `ReplaceMainRoot` 在 Digest 相等时返回 `CommittedSuccess`
     （`DockWorkspaceCoordinator.cs:358-365`）但**不推进 Revision**；
   - `CommittedHostFailure` 仍报告 `ModelCommitted: true`（:1808 起），
     即"模型已提交、投影失败"；`Retry`（:769）明确报告
     `ModelCommitted: false`（:791）。
   - `ReplaceMainRoot` 的变更分支用 `DockWorkspace.Create(mainRoot, floats,
     previous.Revision + 1)`（:370-373）绕过 `CommitWorkspace`。
4. **持久化快照不带历史所需的运行时语义**：`DockWorkspaceSnapshot`
   （`src/Nova.UI.Core/Layout/DockWorkspaceSnapshot.cs:10`）无 revision/digest
   字段，`ToWorkspace` 强制 `revision: 0`（:183）。Digest 只能由重建
   `DockWorkspace` 得到（`DockWorkspace.Digest`，构造函数 `DockWorkspace.cs:22`；
   `DockWorkspaceDigest.Compute` 为 internal，前缀 `W3|main=`，SHA-256）。
5. **视图不是历史的一部分**：`DockPanelViewRegistry.GetOrCreate`（:52）→
   `CreateView`（:126，`CanCreate` 门控 :128、未解析占位 :132）与
   `Release`（:92）；被释放的面板再次出现时**必然由工厂新建视图**
   （`EditorDockPanelFactory.cs:24-37`），只有 `PanelInstanceId` 与位置存活，
   面板内部内容状态不存活。

## 三、决策（D9-A 已冻结条款）

### D9-A-1 历史边界与操作覆盖

| 操作 | 入栈 | 粒度 |
| --- | --- | --- |
| 标签组间移动 `MovePanel` | 是 | 每次成功提交一项 |
| 标签重排（同组 `Panels` 顺序变更） | 是 | 每次成功重排一项 |
| Splitter 调整 `ResizeSplit` | 是 | 一次完整拖动一项，不逐帧 |
| 浮动 `FloatPanel` / 回停 `ReDockFloatRoot` / 合并 `MergeFloatRoot` | 是 | 每次成功事务一项 |
| 浮窗几何 `UpdateFloatBounds` | 是 | 一次完成的几何更新一项 |
| `HidePanel` / `ShowPanel` | 是 | 每次成功可见性变更一项 |
| `RemovePanel`（Close Panel） | 是 | 每次成功关闭一项 |
| `RestoreDefault`（Reset Layout） | 是 | 每次成功重置一项（见 O-5） |
| `ReplaceMainRoot`（Digest 变化） | 是 | 每次成功替换一项 |
| 仅切换活动标签 `ActivateTab` | 否 | 导航不单独成项；但每项快照仍含 active，Undo 回到操作前的 active |
| 拖动预览、取消、失败、无变化（Digest 相同）、`Retry`、主题切换、场景选择、属性编辑 | 否 | 不产生历史项 |

布局历史**只覆盖 Workspace 状态**，场景/文档命令一律留在 ADR-002 的历史里。

### D9-A-2 唯一记录点

历史入栈只允许发生在协调器的"模型已提交"漏斗（`CommittedSuccess` /
`CommittedHostFailure`，见基线核对 2），由 `EditorDockWorkspace` 侧统一封装；
**禁止**在按钮、菜单回调或投影器内各自维护或写入历史。派生规则：

- `ModelCommitted == false`（拒绝、Prepare 失败、无效模型、`Retry`）→ 不入栈，
  游标不变；
- `ModelCommitted == true` → 入栈一次，包括投影失败的降级情形；
- Digest 与当前相等的分支不入栈（杜绝"无变化空项"）；
- 一次 Undo/Redo 本身即一次成功提交，因此也走同一漏斗（游标前移/后移，
  见 D9-A-4）。

**精确化（用户裁决要求，冻结）**：记录点必须携带**操作来源**
`DockLayoutOperationSource { Direct, Undo, Redo }`（或等价的内部事务标识），
不得让"重放"被再次当作普通变更追加：

| 提交结果 × 来源 | 历史动作 |
| --- | --- |
| 未提交（`ModelCommitted == false`，含 `Retry`） | 不入栈、不移动游标、不丢弃 Redo 分支 |
| 已提交 + Digest 与当前项相等 | 不入栈、不移动游标（`Unchanged`），重复通知因此幂等 |
| 已提交 + Digest 变化 + `Direct` | 恰好追加一项，游标指向新项，并丢弃该游标之后的 Redo 分支 |
| 已提交 + Digest 变化 + `Undo` | 只把游标后移一格（`CursorMoved`），**绝不追加新项**；与待撤销目标不符则整笔拒绝（`Rejected`） |
| 已提交 + Digest 变化 + `Redo` | 只把游标前移一格，同上对称 |
| `CommittedHostFailure`（模型已提交、投影失败） | 与 `CommittedSuccess` 同规则入栈一次，即使随后进入降级 |

同一提交被漏斗重复通知（`CommittedSuccess` 与 `NotifyStateChanged` 双路、或
宿主重入）时，第二次必然落入"Digest 与当前项相等"分支，故来源标识与内容
身份共同保证**一次提交最多推进一次游标**。

### D9-A-3 快照内容

**必须包含**：主 `DockTree` 结构与全部浮动根（稳定身份、`ReturnTarget`、
bounds、`RestoreBounds`、最大化状态）；每组 `Panels` 顺序、`active`、`hidden`
有序子集；split 权重；未解析面板身份（保持 D8 的不透明语义，不映射内置面板）。

**必须排除**：HWND / 窗口句柄、视图对象、宿主 `Generation`、投影 token、拖放
会话与预览、`Revision`、缓存的 Digest。

**类型约束**：新增独立的历史快照类型（冻结命名 `DockLayoutSnapshot`，
控制器冻结命名 `DockLayoutHistory`，均位于 `Nova.UI.Core/Docking`），
**不复用** `DockWorkspaceSnapshot` 充当历史载体——后者按基线核对 4 强制
`revision: 0` 且语义属于持久化 DTO；复用会污染 Schema v3 的语义边界（见 O-3）。

### D9-A-4 Revision 与 Digest

- Undo/Redo 是**新的成功提交**：`Revision` 单调递增一次，绝不倒退或复用旧值；
  重建候选时使用 `previous.Revision + 1` 语义，不得沿用快照的 `revision: 0`；
- 恢复后的 `DockWorkspace.Digest` **可以**等于历史中某个先前快照的 Digest，
  这是正确的（内容身份相同）；
- 判定"是否同一布局"只用 Digest，判定"是否为新的提交"只用 Revision 单调性；
- 持久化去重键仍是 canonical JSON（`EditorWorkspace._lastSavedContent`），
  与历史栈互不干涉。

### D9-A-5 Redo 分支与容量

- Undo 之后执行新的成功布局提交 → 丢弃该游标之后的全部 Redo 项；
- 失败、无变化、被拒绝的操作**不得**丢弃 Redo 分支；
- 历史有界：`maxUndoDepth = 100`（与 `CommandHistory` 既有约定一致，O-2）。
  **该数值指"最多可以撤销 100 次布局状态变更"，不是内部快照对象的数量上限**；
  历史按"状态列表 + 游标"存储，因此保留项数上界是 `maxUndoDepth + 1 = 101`
  （第 100 次撤销所回到的那个内容也必须留在栈里）。把存储项数直接砍到 100
  会静默退化成"只能撤销 99 次"，属于契约违背。

**精确化（用户裁决要求，冻结）**：
- 淘汰只发生在游标之前（淘汰最旧项），**永不**改变当前布局、当前项或 Redo
  分支的数量；游标不会因淘汰而出界；
- 淘汰后预算仍精确为 100：`UndoableCount == 100`、`Count == 101`；
- 丢弃 Redo 分支（追加新项）会释放该项占用的存储，之后重新填满时必须重新
  满足同一条容量规则，不得出现"截断一次就永久少一格"；
- 门禁必须覆盖四个边界：连续 100 次可撤销变更；第 101 次变更按规则淘汰；
  淘汰不改变当前布局且游标不出界；Redo 分支截断后容量语义仍正确。

### D9-A-6 Undo/Redo 与宿主故障

- Prepare 失败 / 拒绝（模型未提交）：模型与历史游标均不变；
- 模型提交成功但 Apply/Show 失败：历史反映**已提交的权威模型**，进入 D6 既有
  降级；降级期间禁用 Undo/Redo 与一切布局交互，直到 `Retry` 恢复健康；
- `Retry` 不产生历史项，且一次 `Retry` 不得重复提交同一历史操作；
- 严禁因投影失败静默回滚模型或回滚游标。

### D9-A-7 与持久化的交互（不改 D7/D8）

Undo/Redo 成功后走既有链路：`IDockPersistenceSource`
（`src/Nova.Editor.Services/Layout/DockPersistenceContracts.cs:28-34` 的
`Revision` / `Capture` / `PersistenceStateChanged`）→
`EditorWorkspace.OnDockPersistenceStateChanged`（:591-601）→ 500 ms 防抖保存。

- 健康状态：正常防抖保存；
- 宿主降级：拒绝保存不健康状态；
- 未解析面板存在：继续抑制自动保存与退出快照，仅显式 `Save Layout` 允许忠实
  写回；
- 历史栈**不写入 Schema v3**，仅进程内；重启后布局从文件恢复，历史从空开始；
- 退出快照与最终保存顺序（capture → `BeginShutdown` → save）不变。

D9 不重新实现防抖、健康门控、去重或文件写入。

**精确化（用户裁决要求，冻结）：Reset 与未解析面板抑制必须双向兼容**
- `RestoreDefault`（Reset）本身是一条普通历史项；它的 Undo 必须恢复 Reset 前
  的完整快照，**包括 hidden 成员与未解析（unresolved）面板身份**——不得因为
  Reset 已把干净的 v3 文件写上磁盘，就在历史快照里丢掉这些不透明身份；
- 若 Undo 使未解析面板重新出现在模型中，D8 的**持久化抑制必须重新生效**：
  自动保存与退出快照不得因此覆盖磁盘上原有的未知面板数据；
- 只有显式 `Save Layout`，或一次符合本契约的显式 Reset，才允许改变抑制状态；
  Undo/Redo 本身**不**解除抑制、不直接写文件；
- 结果：一次"Reset → Undo"序列在磁盘上的可见效果，与用户从未执行 Reset
  时必须一致。

### D9-A-8 Shell 入口与快捷键归属

- 入口：`Edit > Undo Layout` / `Edit > Redo Layout` 两个独立菜单命令，显示
  当前可执行状态；无历史项、降级中、重试中、关机期间禁用；
- 执行失败必须给出可见诊断：状态栏文本（沿用 D8 关闭前建立的"结果必须渲染到
  真实 UI 并以 UIA 文本断言"的要求），不得只写日志；
- **首版不占用 Ctrl+Z / Ctrl+Y**：该手势今天由场景 `CommandHistory` 拥有
  （`EditorWorkspace.cs:833/:841` 注册，`EditorShellView.axaml:8-9` 绑定）。
  布局 Undo 的快捷键归属必须等统一 Undo 路由设计明确后再议（O-4）。

### D9-A-9 视图所有权与资源生命周期

历史项只保存模型身份与结构，不持有视图。Undo/Redo 需要恢复已关闭面板时，
必须经 `DockPanelViewRegistry.GetOrCreate` → `CreateView`（`CanCreate` 门控，
未解析用占位）重新创建，**不得**尝试复用已释放的 `ContentControl`。
由此产生一条必须对用户明示的语义：恢复保留身份、顺序、可见性、几何，
**不保留面板内部内容状态**（Console 日志、滚动位置、Inspector 编辑中的值等）
——见 O-6。

### D9-A-10 已关闭 D9 的窄幅补遗：降级门禁与落定几何校准（F1，2026-10-28）

- **性质**：本节是对已关闭 D9（§11.7）的窄幅后续补遗。D9-A-1…A-9 的既有语义、
  §11 的关闭记录与 D9-F 的验收数字均不因本节而改写。补遗只处理两件事：把
  D9-A-6 的"降级期间禁用 Undo/Redo 与一切布局交互"落实为协调器行为，以及修复
  浮窗 `Retry` 后历史当前项仍停留在提交时几何（P）而模型/窗口已落定到 S 的 F1
  缺陷。
- **术语区分（冻结）**：
  - **提交记录（commit record）**：仅 `CommittedSuccess` / `CommittedHostFailure`
    经 `RecordLayoutCommit` 写入历史。只有它们新增历史项、推进游标、截断 Redo
    分支。
  - **落定校准（settlement calibration）**：仅在降级的浮窗宿主由 `Retry` 成功
    `ShowHost` 之后，把历史**当前项**原地替换为落定快照。不新增项、不移动游标、
    不淘汰、不截断 Redo、不推进 `Revision`。校准不是提交；`Retry` 仍不产生历史
    项（D9-A-1/D9-A-6 不变）。
  - **`Replay` 严格校验不变**：Undo/Redo 仍要求 `committed.SameContent(pending)`
    逐字相等。校准只改变当前项内容，不改变重放规则，也不放宽摘要比较。
  - **D9-B 恢复基线重定座**（`DockWorkspaceCoordinator.Restore` /
    `InitializeRestoredHosts` 的基线重设）是既有例外，不属于本补遗的校准语义。
- **降级门禁（D9-A-6 的实现要求，冻结）**：
  - 只要存在任何降级宿主，所有会改变 Dock 工作区或其可持久化几何的入口必须在
    **模型变更之前**被协调器拒绝：标签激活（令牌回调与编程式入口）、分割条调整、
    隐藏/显示/关闭、浮动/回停/合并、浮窗几何更新、根替换与 Reset。
  - 不能只在菜单或按钮层禁用。`Retry`、`BeginShutdown` 与只读诊断不受此门禁
    限制。
  - 拒绝时模型、`Revision`、历史游标与历史项均不变；结果沿用既有用户反馈约定，
    返回 `HostUnavailable` 并携带可见诊断。`ReplayLayout` 保持其既有专用降级
    诊断。
  - 拖动开始/预览不改变模型，不在门禁内；拖动提交经 `MovePanel`/`FloatPanel`
    受同一门禁约束。
- **`ReconcileCurrent` 的前置条件（冻结）**：

  ```csharp
  public DockLayoutHistoryReconcileResult ReconcileCurrent(
      DockLayoutSnapshot expectedCurrent,
      DockLayoutSnapshot settled);
  ```

  - 无当前项 → `Rejected`，不改变任何状态；
  - 当前项内容 ≠ `expectedCurrent` → `Rejected`，不改变任何状态；
  - 当前项内容 == `settled` → `Unchanged`；
  - 当前项内容 == `expectedCurrent` → 原地替换为 `settled`，返回 `Reconciled`；
  - 只替换当前项：项数、游标、容量、Redo 分支与 `Revision` 均不变；
  - 不接受单参数形式。前置匹配是 API 的安全边界，不只是测试约束。
- **浮窗 `Retry` 的顺序（冻结）**：`RetryHost` 成功且确实发生了成功的
  `ShowHost` 之后，依次执行：捕获归一化前工作区快照 → 验证历史当前项仍等于该
  快照 → `NormalizeFloatGeometry` → 捕获落定后快照 →
  `ReconcileCurrent(expectedCurrent, settled)` → 通知状态更新。主宿主 `Retry`
  不触发浮窗几何校准；归一化与校准都不构成新的布局提交。
- **校准失败（冻结）**：验证或校准被意外拒绝时不得静默忽略。必须进入可诊断的
  失败处理：关闭该浮窗宿主（保持降级与保护生效），若已完成归一化则把模型恢复
  到 `expectedCurrent` 内容，并返回携带诊断的失败结果。不得在校准未成立时恢复
  Undo/Redo 或自动保存权限，也不得报告为正常完成；下一次 `Retry` 必须仍可自愈。
- **F1-01 门禁（修正后，冻结）**：
  - 降级期间必须断言 `CanUndoLayout == false` **且** `CanRedoLayout == false`。
    原稿的 `CanUndoLayout == true` 与 D9-A-6 冲突，作废；历史项的存在必须由
    Retry 之后的历史行为/项数证明，不得用"Undo 可用"证明。
  - 门禁阶段：① 首次 Show 失败：模型提交一次、历史记录一次；进入降级；
    `Capture()` 被阻止；Undo/Redo 均不可用。② 降级期间尝试其他布局操作
    （激活、分割条、隐藏/关闭等）：在模型变更前被拒绝；模型/`Revision`/历史不变。
    ③ Retry 成功：生效几何写回模型并校准当前历史项；项数与游标不变；`Revision`
    不变。④ 立即 `Capture()`：快照记录落定几何 S，而不是提交时的 P。⑤ 立即
    Undo：恢复前一布局；`Revision` 按既有规则 +1。⑥ 立即 Redo：以 S 成功重放，
    不因摘要不符被拒绝。⑦ 重放后：模型、窗口、历史与快照几何一致；不新增历史项。
- **LM-07 修订**：旧断言"降级期间直接 `ResizeSplit` 仍按 D6 契约提交"与本补遗的
  门禁冲突，按 D9-A-6 冻结条款作废；LM-07 其余部分（Retry、重放冻结、历史计数）
  保持。被拒绝的操作不新增历史项。
- **证据纪律**：D9-F 的原有测试数字与 §11 的关闭记录不得重写；本轮新增门禁的
  数字按实际执行记录，且构建仍按"零错误、实际 `NU1900` 条数"口径报告。

## 四、门禁映射（契约已冻结；D9-B 行见第八节，D9-C/D 见第九节，D9-E 见第十节，D9-F 封印见第十一节；D9-A-10 补遗见第三节与第十二节）

**D9-B 纯历史模型**（无 Avalonia、无宿主、无文件 I/O）

| 用例 | 预期 |
| --- | --- |
| 单次操作后 Undo | 恢复操作前布局内容 |
| Undo 后 Redo | 恢复撤销前布局 |
| 连续 Undo/Redo | 游标与模型一致，Revision 每次 +1 |
| Undo 后新操作 | 旧 Redo 分支失效 |
| 失败 / 被拒绝操作 | 历史与模型均不变，Redo 不丢 |
| 无变化操作（Digest 相同） | 不生成历史项 |
| 到达容量上限 | 淘汰最旧，游标语义正确；100 次可撤销变更对应 101 个保留项 |
| Redo 分支截断 | 释放的存储可再次填满，容量规则不被截断破坏 |
| 多根布局 | 主根与全部浮动根（含 `RestoreBounds`/最大化）一起恢复 |
| Hidden / unresolved | hidden 顺序与不透明身份完整保留 |
| Close 或 Reset 后 Undo | 恢复模型身份，不依赖已释放视图 |
| 相同内容恢复 | Digest 相等，游标不与内容身份混淆 |
| Revision 单调性 | Undo/Redo 各推进一次，绝不倒退 |
| Snapshot 成本 | 实测 100 项上限下的真实内存开销，不作未经测量的假设 |
| 载荷纯净性 | 快照只含模型内容，反射可见成员不含视图/句柄/代次/令牌/Revision |

**D9-C 协调器集成**：唯一记录点（含 `ModelCommitted == false` 不入栈、降级入栈
一次、Digest 相等不入栈）、游标只在提交成功后推进、Retry 不重复提交、视图经
注册表重建。

**D9-D Shell**：真实菜单命令 Undo/Redo、可用性状态、失败诊断可见文本、
Ctrl+Z 仍属场景历史（回归断言不被抢占）。

**D9-E 真实进程与故障**：Move/Undo/Redo、Splitter 一次拖动成一项、Float/回停、
Hide→Undo、Close→Undo 经工厂重建、Reset→Undo（含 hidden/未解析）、Prepare 失败
游标不变、Apply/Show 失败保持权威并降级、退出重启后布局在而历史为空、
未解析与 Undo/Redo 不绕过抑制、快速连续操作顺序确定。

**D9-F 回归与封存**：Debug/Release 全量、真实进程保存/恢复、如实记录
`NU1900`（当前基线：每配置 18 条，**非零警告**）、工作树仅含 D9 必需变更。
用户 2026-10-27 授权时把本阶段限为四项：最终源码状态的全量回归（通过数、
构建错误数、`NU1900` 数都取实测）、浮窗 Bounds 稳定性复验（重点最大化／还原
过渡期，保留此前一次失败与后续通过的完整记录，不得预先归为环境噪声）、
D9-A…D9-E 证据封印（含 LM-08 术语修正与初稿失败审计）、最终覆盖限制清单。
禁止项：不把单显示器、96 DPI 的结果外推为 DPI／多显示器验收，不扩大到与
本阶段无关的架构变更。封印记录见第十一节。

## 五、裁决结果（O-1…O-8 已于 2026-10-26 全部接受）

| 编号 | 问题 | 冻结结论 |
| --- | --- | --- |
| O-1 | Splitter 一次拖动 / 浮窗一次几何更新的成项边界 | 以"一次完整交互提交"为界，不逐帧；取消、捕获丢失、无变化一律不记录 |
| O-2 | 历史容量数值与淘汰策略 | `maxUndoDepth = 100` 指 100 次**可撤销变更**（保留项上界 101，见 D9-A-5 精确化）；淘汰最旧 |
| O-3 | 历史快照类型 | 新增独立 `DockLayoutSnapshot`，**不复用**持久化 `DockWorkspaceSnapshot` |
| O-4 | Ctrl+Z / Ctrl+Y 归属 | 首版完全不占用快捷键，只提供菜单命令 |
| O-5 | `RestoreDefault` 后历史是否清空 | Reset 作为普通历史项，Undo 回到 Reset 前布局（含 hidden 与未解析身份），并触发 D9-A-7 抑制复活 |
| O-6 | 面板内部内容状态不可恢复 | 接受，并必须在 UI/文档对用户明示；不引入视图内容快照 |
| O-7 | 菜单是否显示条目名 | 只显示 `Undo Layout` / `Redo Layout`，不带条目名 |
| O-8 | 进程退出时历史栈是否需要诊断 | 不需要；不持久化，退出时静默丢弃，不写入 Schema v3 |


## 六、变更边界（D9 明确不包含）

- 不修改 ADR-005/007/008/009 已封存的模型、宿主、持久化与生命周期契约；
- 不实现多显示器、混合 DPI、跨 DPI；
- 不实现第三方面板注册机制；
- 不统一或替换场景实体命令历史，不做全局统一 Undo 路由；
- 不跨进程持久化历史栈；
- 不改防抖、健康门控、去重与文件写入实现。

## 七、后果与风险

- 记录点若不在协调器漏斗集中，14 处 `_workspace =` 赋值必然导致漏记或重复记录；
- `ReplaceMainRoot` 的 Digest 相等分支若不显式处理，会产生"无变化却入栈"的空项，
  并使 Revision 与历史长度不一致；
- "模型已提交但投影失败"入栈一次，意味着降级状态下 Undo 会再次改动权威模型，
  必须以"降级期间禁止交互"约束兜住；
- 快照虽不含视图，但每项含完整多根结构；100 项上限下的内存与序列化成本需在
  D9-B 门禁中实测，不能推定（实测结果见第八节）；
- 面板内部内容不可恢复是用户可感知的限制，需在 UI 或文档明示；
- 本 ADR 已由用户裁决冻结并进入实施。**D9-B 只允许纯历史模型与门禁**：不引入
  Avalonia、宿主、Shell 或文件 I/O，不修改场景 `CommandHistory`。在 D9-B 全部
  通过之前，不得启动协调器接线（D9-C）或 Shell Undo/Redo 入口（D9-D）。

## 八、D9-B 实现与门禁证据（2026-10-26）

### 8.1 交付物（全部纯模型，零 Avalonia / 宿主 / Shell / 文件 I/O 依赖）

| 文件 | 内容 |
| --- | --- |
| `src/Nova.UI.Core/Docking/DockLayoutSnapshot.cs` | 独立历史快照类型（O-3）。`Capture(workspace)` 用 `DockWorkspace.Create(main, floats, revision: 0)` 取内容并计算 W3 Digest，**丢弃 Revision**；`RestoreWorkspace(revision)` 由调用方传入当前提交序列的下一个 Revision；`SameContent` 只做 Digest 序数比较 |
| `src/Nova.UI.Core/Docking/DockLayoutHistory.cs` | 状态列表 + 游标控制器。`DockLayoutOperationSource { Direct, Undo, Redo }`（D9-A-2 精确化的操作来源）；`Record` 返回 `DockLayoutHistoryResult { Outcome, Diagnostic }`，Outcome ∈ `Recorded / CursorMoved / Unchanged / Rejected`；`Reset(baseline)` 用于装载基线；`TryGetUndoTarget` / `TryGetRedoTarget` / `UndoableCount` / `RedoableCount` / `Current`；`DefaultMaxUndoDepth = 100`，保留项上界 `101` |
| `tests/Nova.UI.Core.Tests/DockLayoutHistoryTests.cs` | 17 项门禁 `Lh01`–`Lh17`，每项前置 `// LH-NN:` 依据注释 |

场景 `CommandHistory`、协调器、投影器、Shell、持久化均未改动；
`DockLayoutSnapshot` 与 `DockWorkspaceSnapshot` / Schema v3 无任何类型或字段耦合。

### 8.2 第四节 D9-B 维度 → 用例

| 契约维度 | 门禁 |
| --- | --- |
| Undo/Redo 往返与游标一致 | `Lh02`、`Lh03` |
| Revision 只前进（重放不倒退） | `Lh01`、`Lh04` |
| Redo 分支被新操作丢弃 | `Lh05`、`Lh06` |
| 无变化提交 / 重复通知不入栈、不动游标 | `Lh06`、`Lh07` |
| 与承诺目标不符的重放整笔拒绝且不改状态 | `Lh08` |
| 容量：100 次可撤销变更 = 101 个保留项 | `Lh09` |
| 容量：第 101 次变更淘汰最旧、当前布局与游标不受扰 | `Lh10` |
| 容量：Redo 截断释放存储后重新填满 | `Lh11` |
| 多根：主根 + 每个浮动根的身份/几何/`ReturnTarget` 同批恢复 | `Lh12` |
| Hidden 有序子集与未解析不透明身份完整保留 | `Lh13` |
| Close / Reset 后 Undo 只恢复模型身份，不依赖旧视图 | `Lh14` |
| 相同内容 = 同一 Digest，但可占两个历史位置 | `Lh15` |
| 载荷纯净性（反射可见成员只含模型内容，无宿主程序集） | `Lh16` |
| Snapshot 成本实测 | `Lh17` |

### 8.3 Snapshot 内存实测（非推定）

`Lh17` 在一个 48 面板主根 + 8 个浮动根的工作区上保留 101 个布局，
用 `GC.GetTotalMemory(forceFullCollection: true)` 差值实测：

| 形态 | 101 项合计 | 每项 | 阈值 |
| --- | --- | --- | --- |
| 顺序编辑、共享未改动子树 | 216.0 KiB | 2190 B | ≤ 512 KiB |
| 101 个独立构建的工作区 | 2605.8 KiB | 26419 B | ≤ 16 MiB |

Debug 与 Release 两次测量数值一致（不可变对象尺寸与保留量确定，与配置无关）。
结论：容量上限 100 的内存代价在本机可接受，且真实布局远小于该测试工作区。

### 8.4 回归证据

- `Nova.UI.Core.Tests` 117/117（其中 D9-B 新增 17）；
  `Nova.UI.Controls.Tests` 65/65；`Nova.Editor.IntegrationTests` 122/122；
  `Nova.Editor.PerformanceTests` 5/5 → 每个配置 **309/309**（D8 关闭基线 292 + 17）。
- Debug 与 Release 全解决方案构建：均 **0 错误**，各 **18 条 `NU1900`**
  （9 个项目 × restore/build 两条记录，来自不可达的 Azure China 源），
  **不是零警告构建**。
- `dotnet format Nova.sln --verify-no-changes` 退出码 0。
- 工作树保持未提交：HEAD 仍为 `6017998`，无 commit/push/tag/merge；
  `editor_avalonia`、`native/gate` 及其他无关 dirty 文件保持原状。
- 执行事故如实记录：一次 Integration 运行出现 3 项失败，该次运行与
  `dotnet format` 的工作区加载并发执行；同批二进制单独复跑为 122/122，
  随后的全量 Debug/Release 各 309/309。未观察到产品缺陷证据，
  但真实进程门禁不得与并行构建/格式任务混跑，后续回归一律串行执行。

### 8.5 D9-B 边界（尚未做，属后续阶段）

- 未实现任何用户界面；未接入协调器唯一记录点（`CommittedSuccess` /
  `CommittedHostFailure` / `Retry` / `ReplaceMainRoot` 的 Digest 相等分支）——**D9-C**；
- 未实现 `Edit > Undo Layout` / `Redo Layout` 菜单与可见诊断——**D9-D**；
  首版仍不占用 Ctrl+Z / Ctrl+Y（O-4）；
- Reset↔未解析抑制复活（D9-A-7 精确化）在纯模型侧已由 `Lh13`/`Lh14` 证明
  身份与 hidden 可完整恢复，但**磁盘抑制行为**必须由 D9-C/D/E 门禁验证；
- 真实进程与故障门禁（D9-E）、全量回归封存（D9-F）未启动。

> 本节是 D9-B 完成时刻的快照；D9-C 与 D9-D 已按第九节交付，其中列出的
> "尚未做"项不再成立，D9-E 见第十节、D9-F 封印见第十一节。

## 九、D9-C 协调器接线与 D9-D Shell 入口（2026-10-27）

### 9.1 D9-C 交付与精确化落点

| 冻结要求 | 实现位置 | 结论 |
| --- | --- | --- |
| 唯一记录点 | `CommittedSuccess` / `CommittedHostFailure` → `ApplyHostAfterCommit` → `_layoutHistory.Record(DockLayoutSnapshot.Capture(_workspace), source)`（`DockWorkspaceCoordinator.cs:2079`） | 14 处 `_workspace =` 赋值不再各自入栈 |
| `ModelCommitted == false` 不入栈 | 拒绝分支只经 `Reject(...)`，不调用记录点 | `Retry`（:791 报 `ModelCommitted: false`）既不记录也不推进游标 |
| 降级仍记录一次 | `CommittedHostFailure`（模型已提交、投影失败）走同一记录点 | 进入降级的那一笔可被 Undo |
| Digest 相等不入栈 | `Record` 的相邻性判定（`DockLayoutSnapshot.SameContent`） | `ReplaceMainRoot` 的 Digest 相等分支返回 `Unchanged` |
| 游标只在提交成功后推进 | `ReplayLayout`（:832）先 Prepare 主宿主与新增浮动宿主，再采纳候选；失败时模型与游标都不变 | 被拒/未 Prepare 的重放不改历史 |
| 区分普通变更 / Undo / Redo | `DockLayoutOperationSource`，重放由 `ReplayLayout` 显式传入 | 重复通知不会二次入栈 |
| 视图经注册表重建 | 重放只提交模型；视图仍由 `DockPanelViewRegistry.GetOrCreate` 生成 | 历史不持有视图 |
| 导航不单独成项 | `ActivateTab` 两个重载显式 `recordHistory: false`（:673、:734） | 与 §二 表格一致 |
| Undo 使未解析面板重现 ⇒ D8 抑制复活 | `EditorWorkspace.PausePersistenceForUnresolvedPanels` + `_acceptedUnresolvedPanels`：只有本 build 从未写入过的身份重现才重新抑制；每次成功写入后重新接受当前集合 | 自动保存与退出快照不覆盖磁盘上的未知面板数据；只有显式 `Save Layout` 或契约内 Reset 改变抑制状态 |

### 9.2 D9-D 交付

- 命令：`NovaCommandIds.UndoLayout = layout.undo` / `RedoLayout = layout.redo`
  （`src/Nova.UI.Abstractions/Commands.cs`），注册于 `EditorWorkspace.RegisterCommands`
  并**不带任何手势**——Ctrl+Z / Ctrl+Y 仍是场景命令历史（O-4）。
- 菜单：`EditorShellView.axaml` 的 `Edit` 下新增 `UndoLayoutMenuItem` /
  `RedoLayoutMenuItem`，标题为纯 `Undo Layout` / `Redo Layout`（O-7）。
- **Avalonia 实测事实**：本项目的 `MenuItem.IsEnabled` 不由 `ICommand.CanExecute`
  驱动（`CanExecute == false` 时 `IsEnabled` 仍为 `True`），因此可用性由
  `EditorShellViewModel.CanUndoLayout` / `CanRedoLayout`（镜像
  `Router.CanExecute`）显式绑定，并在 `RaiseCommandStates()` 中发布。
- 可见诊断：成功——状态栏 `Layout undone / redone (panel content is not restored)`，
  把 O-6 限制在结果发生的时刻明示给用户；被拒——状态栏显示
  `layout.undo: command is currently unavailable`，同时降级横幅
  （`DockDegradedText`）显示宿主诊断，`Retry layout` 按钮是唯一恢复入口。
- 修复的真实缺陷：`EditorDockWorkspace.Retry()` 期间的通知都在
  `IsRetrying == true` 时发布，重试成功后菜单可用性停在旧值。现在重试结束时
  补发一次 `PersistenceStateChanged`（`EditorDockWorkspace.cs:152`），
  可用性与横幅同源刷新。

### 9.3 D9-C / D9-D 门禁映射

`LW-01`…`LW-11` 位于 `tests/Nova.UI.Controls.Tests/DockWorkspaceCoordinatorTests.cs`，
每项前置 `// LW-NN:` 依据注释；`LS-01`…`LS-05` 位于
`tests/Nova.Editor.IntegrationTests/DockLayoutHistoryShellTests.cs`。

| 契约维度 | 门禁 |
| --- | --- |
| 基线来自会话装载的布局 | `LW-01` |
| 唯一记录点：被拒操作不入栈、不动 Redo 分支、不推进 Revision | `LW-02` |
| 导航不单独成项（`ActivateTab`） | `LW-03` |
| 降级入栈一次、`Retry` 不记录、降级期间重放不可用 | `LW-04` |
| 重放本身是提交：往返只走同一组条目（Digest 相等不二次入栈） | `LW-05`、`LW-06` |
| 历史只含模型身份：关闭的面板经工厂重建 | `LW-07`、`LW-08` |
| 被阻塞的重放不改模型与游标（拖动中 / `BeginShutdown` 后） | `LW-09` |
| 重放在采纳前 Prepare 每一个宿主，失败则整笔不变 | `LW-10`、`LW-11` |
| 未解析面板重现重新抑制写入 | `LW-12`（`DockPersistencePolicyTests`）、`Undo_after_a_reset_returns_unknown_panels_and_pauses_persistence_again`（`DockHiddenPanelShellTests`） |
| 真实菜单 Undo / Redo 与可见状态文本 | `LS-01`、`LS-02`（`DockLayoutHistoryShellTests`） |
| Ctrl+Z 仍属场景历史、布局命令无手势、两套历史互不越界 | `LS-03` |
| 降级停放可用性、`Retry` 释放同一步 | `LS-04` |
| 被拒命令必须出声、且不改模型 | `LS-05` |
| 真实进程：Reset→Undo→Redo 经鼠标与 UIA、Ctrl+Z 不抢占、退出文件仍正常 | `Real_shell_menu_undo_and_redo_replay_a_reset_without_borrowing_ctrl_z`（`RealProcessLayoutUndoTests.cs`） |

### 9.4 D9-C / D9-D 回归证据（2026-10-27）

- `Nova.UI.Core.Tests` 117/117；`Nova.UI.Controls.Tests` 76/76；
  `Nova.Editor.IntegrationTests` 130/130；`Nova.Editor.PerformanceTests` 5/5
  → 每个配置 **328/328**，Debug 与 Release 各一遍，真实进程门禁串行执行。
- Debug 与 Release 全解决方案构建：**0 错误**，各 **18 条 `NU1900`**
  （9 个项目 × restore/build，源 `nuget.cdn.azure.cn` 不可达），
  **不是零警告构建**。
- `dotnet format Nova.sln --verify-no-changes` 退出码 0。
- 工作树保持未提交：HEAD 仍为 `6017998`，无 commit/push/tag/merge；
  `editor_avalonia`、`native/gate` 及其他无关 dirty 文件保持原状；
  调试用的临时 trace 测试与日志目录已删除。
- 过程事实：D9-D 门禁初稿按"切换活动标签可被 Undo"编写，实测被驳回——
  §二 已冻结"导航不单独成项"，产品行为正确，门禁改为真实 `Reset Layout`
  流程；另有 5 项初稿失败源于把一次性注入失败当作持续降级，实测投影器
  内部会重建一次并吸收单帧失败，只有 `AlwaysFail` 才进入降级。

### 9.5 仍未做（属 D9-E / D9-F）

- Splitter 一次拖动成一项、浮动/回停、Close→Undo 经工厂重建、快速连续操作
  顺序确定性等真实进程与故障矩阵；
- 多显示器、混合 DPI 不在 D9 范围；
- 面板内部内容状态不可恢复（O-6）仍是产品限制，本阶段只做了 UI 明示。

> 本节是 D9-C / D9-D 完成时刻的快照；上面列出的故障矩阵与真实进程项已由
> 第十节 D9-E 交付，多显示器/混合 DPI 仍未验证，D9-F 封印见第十一节。

## 十、D9-E 故障矩阵与真实进程交互（2026-10-27）

> **裁决记录**：本节先经用户 HOLD（唯一阻塞项是 §10.5 的 Reset 术语），对齐后于
> 2026-10-27 由用户裁决**正式通过、HOLD 解除**，并授权启动 D9-F。用户同时确认
> 该结论是对其所收到证据记录的评审，不代表其独立复跑构建；§10.6 的浮窗边界复验
> 项仍是**待验证风险，封印时不得删除**。

范围按用户裁决保持原样：不动 Shell 结构、不加快捷键、不改持久化格式。
新增门禁全部落在 `tests/Nova.Editor.IntegrationTests/`：
`DockLayoutFaultMatrixTests.cs`（LM-01…LM-07）、`DockPersistencePolicyTests.cs`
（LM-08）、`RealProcessLayoutUndoTests.cs`（RP-L1、RP-L2）。

### 10.1 七项强制验收场景 → 门禁

| 场景 | 门禁 | 证到的事实 |
| --- | --- | --- |
| 分割条拖动 | `LM-01`、`LM-02` | 真实 `DockSplitterThumb` + 真实指针事件：按下与 5 次连续增量后 Digest 仍等于基线且 `CanUndoLayout == false`；一次完整释放只入栈一项；`Cancel`、丢失捕获、零距离释放各产生 0 项。拖动会话期间 `CanUndo/CanRedo` 均为 false，两种重放被拒且模型与 Revision 不动；释放后提交并把被 Undo 的分支截断。 |
| 浮动与回停 | `LM-03` | `MovePanel(Scene, Float)` 只入栈一项且浮动根 Bounds 等于目标矩形；回停到底部组后 `[Inspector, Scene, Console]` 顺序正确、四个视图全部存活、`DisposeCount` 全 0；Undo 逐字段还原浮动根 Id/Bounds/RestoreBounds/ReturnTarget，Redo 再回停并停在游标末端。 |
| `Close → Undo` | `LM-04`、`RP-L2` | headless 走真实拖放与真实标签右键菜单（Hide→Close）；Undo 恢复身份、顺序、hidden 记录与 active 标签，同时每次重放都向工厂要一个全新视图（`DisposeCount` 与注册表调用次数都被断言），因此**不声称恢复面板内部内容**（O-6）。真实进程里右键关闭→Undo 后标签回到 UIA，再用真实点击激活成功，证明恢复的是活视图而非残影。 |
| 快速连续操作 | `LM-05`、`RP-L1` | 一次性注入失败经投影器自愈只留一项（状态 Applied、代次 +2、一次 `StateChanged`）；hide/show/resize/拖动重排/remove 连续六笔得到六项；Undo 三笔后游标停在 s3，新提交截断 redo 分支，`CanRedoLayout` 转 false 且被拒的 Redo 不改 Digest；真实鼠标拖动→Undo→Redo 后 UIA 标签顺序与屏幕列都回到拖动前。 |
| Prepare 失败 | `LM-06`（主宿主侧见 `LW-10`、`LW-11`） | 重放只为**需要新建**的浮动宿主调用 Prepare，因此故障注入指向 Redo 方向：拒绝时模型 Digest、Revision、浮动根数量、游标全部不动，孤立宿主被创建即被 Dispose，诊断文本回传，清除故障后同一 Redo 在新建于宿主上成功并入栈一项。 |
| Commit 后宿主应用失败 | `LM-07`（Shell 侧见 `LS-04`） | `AlwaysFail` 让 Apply 在提交之后失败：外观仍是"提交成功"，但 Digest 等于已提交状态、Revision 前进、游标随提交推进（这正是"模型已提交"与"模型未提交"必须分开断言的地方）；降级后两种重放都被停放，被阻塞的 Undo 不改模型与 Revision；降级期间直接 `ResizeSplit` 仍按 D6 契约提交；`Retry` 成功后 `CanUndoLayout` 恢复为 true、`CanRedoLayout` 为 false，Digest 不变，历史条目数保持 2。 |
| Reset 与未解析身份 | `LM-08`（工作区侧，逐态）、`LS-06`（真实 `Reset Layout` 按钮 + 真实 `Edit > Undo Layout`）；既有 `Reset_layout_button_clears_unresolved_and_hidden_and_saves_a_clean_layout`、`RP-5`、`LW-12` | 四态逐一断言见 §10.5：模型重置丢弃两个未知身份但抑制不动、写入仍为 0；显式保存写干净 v3 并结束抑制；无历史项的标签激活排出一笔自动保存；Undo 使身份（含 hidden 的 `third.party.chat`）重现时抑制在写入前重新武装，那笔在飞的自动保存被取消（释放延迟后既不落文件也不产生完成事件），真实后续变更（右键 Hide）仍写不出，退出快照被拒（`cannot resolve`）且文件保持干净；再次显式保存忠实写回身份；已被接受的身份在 Redo 后不再抑制并排自己的自动保存；干净写入重新忘记这两个身份，因此下一次 Undo 又构成新的重现。 |

**术语前提（D9-E 评审阻塞项，已在 §10.5 展开）**：Shell 的 `Reset Layout`
是两段命令——先 `EditorDockWorkspace.ResetToDefault()`（只改模型：丢弃 hidden
与未解析身份，**不写文件**），再
`SaveLayoutAndReportAsync(userInitiated: true)`。ADR-009 D8-A-3 的"只有显式保存
或显式 Reset 才能结束抑制状态"里，"显式 Reset"指**这条完整命令**，真正结束抑制
的是它的显式保存那一段（`EditorWorkspace` 只在用户请求的写入成功时清除抑制）。
`LM-08` 驱动的是前一段，所以它的断言是"不写文件的模型重置不是那次写入，故不结束
抑制"——与 ADR-009 一致，此前 ADR-010 与测试注释把它笼统写成"Reset 不解除抑制"
属于措辞缺陷，已修正；ADR-009 冻结原文未作任何修改。

### 10.2 过程事实：初稿失败与判定原因

本阶段 10 次初稿失败，另有 1 项由用户 HOLD 评审指出的契约措辞缺陷，全部记录以免
只剩最终通过数字：

- **测试假设错误（产品行为正确）**：LM-04 初稿按"Hide 不释放视图"编写，实测
  `DisposeCount == 1`。冻结的 D8-D 契约是隐藏即释放、显示即重建；门禁改为断言
  这一事实，反而把 O-6 的"重放只恢复布局、不恢复内容"钉成了可执行证据。
- **历史算术错误（我的期望漏了截断）**：LM-02 与 LM-05 初稿分别期望 3 项与
  `AreNotEqual`，实际是 2 项与"被拒的 Redo 不改 Digest"——`Append` 会把被 Undo
  的 redo 分支截掉，游标落点决定条目数。
- **注入时机错误**：LM-05 初稿在对象初始化器里就武装拦截器，构造期的首次投影把
  一次性失败吃掉，导致代次只 +1；改为构造后再设置 `FailNode/FailuresRemaining`。
- **准备点理解错误（运行前读代码纠正）**：LM-06 初稿把 Prepare 故障指向 Undo
  方向，但重放只 Prepare 需要新建的宿主，已存在的浮动宿主不会被再次 Prepare；
  改为"浮动→Undo 关闭宿主→注入→Redo"。
- **持久化时序错误**：LM-08 初稿在 `AttachDockPersistence` 之前调用
  `EnableAutoSave()`，而 attach 先执行 detach 并把 `_autoSaveEnabled` 清零，
  于是自动保存被静默关闭；另外初稿把退出快照放在中段，而
  `CaptureShutdownSnapshot` 会置 `_shuttingDown`，之后的普通保存按契约返回
  Skip——快照采集是终点，门禁顺序据此重排。
- **真实进程注入事实（非产品缺陷）**：RP-L1 初稿第一次注入点击只激活了窗口，
  拖动从未开始；与 RP-L2 一致改为拖动前 `ActivateWindow`。`SetCursorPos` 的
  连续移动 + 按住左键能驱动 `DockDragController` 的 4px 阈值与指针捕获。
- **发现既有门禁的竞态（已修）**：D8-C 的 `LW-12`
  （`An_unresolved_panel_that_reappears_pauses_writes_again`）用
  `CompletionProbe.Next()` 按 FIFO 取结果，而它前面还有一次显式保存排在队列里，
  于是等待可能在被释放的自动保存真正落盘之前就返回，全量运行里连续两次失败
  （单测隔离必过）。改为按完成事件计数等待
  （`WaitSavedCompletionAsync`），并把同一写法用于 LM-08。
  此前 328/328 的记录本身是真实通过的，但该门禁确实带着潜在竞态，现予修正并如实记录。
- **环境敏感的真实进程门禁**：`Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart`
  在第一次全量 Debug 运行里失败（持久化的浮动 X 读回 0 而非 180），随后
  Debug、Release 全量运行均通过。该门禁依赖窗口管理器在最大化/恢复过渡期给出
  的帧矩形，属桌面状态敏感项，D9-F 若再遇到须按同样口径记录，不得归入"偶发忽略"。
- **契约措辞缺陷（用户 D9-E HOLD 指出，已对齐）**：初稿把 `LM-08` 概括为"Reset
  本身不解除 D8 抑制"，读起来与 ADR-009 D8-A-3"只有显式保存或显式 Reset 才能结束
  抑制状态"冲突。逐项核对后确认**产品实现与冻结契约都一致，冲突只在我的术语**：
  `LM-08` 驱动的是 Shell `Reset Layout` 的模型段（`ResetToDefault()`，不写文件），
  而 ADR-009 说的是完整命令（模型段 + 显式保存段）。对齐动作有四：(a) `LM-08`
  的注释与断言消息改为按状态命名其前提，不再出现无主语的"Reset"；(b) 新增
  `LS-06` 用真实 `Reset Layout` 按钮 + 真实 `Edit > Undo Layout` 把完整命令的两段
  与 Undo 后的重新抑制跑通；(c) §10.1 写出术语前提、新增 §10.5 四态映射；
  (d) `EditorWorkspace` 的 `Reset Layout` 注册处留一行说明"配对的保存是契约要求"。
  **ADR-009 冻结原文未作任何修改**，本次也没有改动实现行为——被改的只有措辞与
  覆盖面。LM-01…LM-07、RP-L1、RP-L2 的证据不受该阻塞项影响。
- **编译层返工**：`Pointer` 与 `System.Reflection` 名称冲突（CS0104）、
  `CreateDock` 重载歧义（CS0121，浮动构造重载改名 `CreateFloatingDock`）、
  `PointerEventArgs` 的 `rootVisual` 位置参数（CS8323）。

### 10.3 D9-E 回归证据（2026-10-27，HOLD 阻塞项对齐后重跑）

- `Nova.UI.Core.Tests` 117/117；`Nova.UI.Controls.Tests` 76/76；
  `Nova.Editor.IntegrationTests` 141/141（130 + LM-01…LM-08 + RP-L1、RP-L2 +
  对齐阻塞项新增的 `LS-06`）；`Nova.Editor.PerformanceTests` 5/5 →
  每个配置 **339/339**。最终源码状态下 Debug 与 Release 各跑两遍全量，
  真实进程门禁串行执行、不与构建或 format 并发。
- §10.5 涉及的同族持久化/Shell 门禁（`DockPersistencePolicyTests` +
  `DockLayoutHistoryShellTests`）在对齐后单独过滤运行 **27/27** 通过。
- 移交 D9-F 复验的
  `Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart`
  在本轮对齐后的 **4 次全量运行（Debug×2、Release×2）中均通过**；此前那次失败
  （持久化浮动 X 读回 0 而非 180）仍按 §10.6 保留为待复验事实，本轮结果不改变
  复验要求。
- Debug 与 Release 构建：**0 错误**，MSBuild 汇总各 **18 条 `NU1900`** 警告
  （源 `nuget.cdn.azure.cn` 不可达，取不到漏洞公告；9 个项目 × restore/build）。
  `dotnet test --no-build` 自身 0 条。**不是零警告构建。**
- `dotnet format Nova.sln --verify-no-changes` 退出码 0（工作区加载有警告，
  未产生格式差异）。
- 工作树保持未提交：HEAD 仍为 `6017998`，无 commit/push/tag/merge，暂存区为空；
  `editor_avalonia`、`native/gate` 及其他无关 dirty 文件保持原状；测试日志写在
  `$TEMP`，未落入仓库。

### 10.4 限制与未覆盖环境（不得从通过数字外推）

- 真实进程侧覆盖的是**标签拖动**与**标签右键关闭**；分割条一次拖动成一项、
  浮动/回停多根几何、Prepare 拒绝、提交后宿主失败四项只有 headless 真实控件
  证据（无 Avalonia headless 平台，控件由测试手工 `Measure/Arrange`）。
- 全部证据来自单显示器、96 DPI 交互式桌面；多显示器、混合 DPI、无桌面会话
  （服务/CI 无人值守）下的重放与真实进程门禁未验证。
- Release 的 `Nova.Editor.App` 由测试自行按输出目录推断配置定位，若只构建
  Debug 则真实进程门禁会以 `Assert.Inconclusive`/文件缺失暴露，不会静默跳过。
- O-6 仍是产品限制：Undo/Redo 恢复布局与身份，不恢复面板内部内容状态；
  本阶段继续在状态栏与 ADR 中明示。
- 本节的限制由第十一节原样带入封印记录；D9 整体是否关闭以用户对 D9-F 的裁决为准。

### 10.5 Reset 与抑制解除的四态契约（D9-E HOLD 阻塞项对齐）

ADR-009 的抑制规则只有一条真实判据：**抑制在"用户请求的一次写入"成功时结束**。
`EditorWorkspace` 的实现与此一致——清除抑制的只有 `userInitiated` 保存与退出快照
写入，模型变更本身从不清除。"Reset"这个词则横跨两个层次，故下表把四种状态转换
分别钉到门禁断言上（种子文件均为默认布局 + Console 隐藏，再把 `editor.scene` →
`third.party.scene`、`editor.console` → `third.party.chat`，即未解析 2 个、
hidden 1 个）。

| 状态转换 | 契约所指 | 门禁与实测断言 |
| --- | --- | --- |
| **1. 正常 Reset**（完整 Shell 命令） | 显式破坏性操作：移除 hidden 与未解析身份，并**显式保存**干净的 schema v3 布局；结束抑制的是这条命令里的保存段 | `LS-06`：真实 `ResetLayoutButton` 的 `Button.ClickEvent` → 完成事件 `Saved`、状态栏出现 `Layout saved`、`UnresolvedPanels.Count == 0`、`IsPersistenceSuppressed == false`、文件不含 `third.party`。既有 D8-D 门禁 `Reset_layout_button_clears_unresolved_and_hidden_and_saves_a_clean_layout` 另断言旧视图只释放一次与 hidden 菜单清空；`RP-5` 在真实进程里加二次重启确认。 |
| **1'. 只驱动模型那一段** | `ResetToDefault()` 不写文件，因此**不是**那次写入 | `LM-08`：`dock.ResetToDefault()` 返回 `Applied`、两个未知身份离开模型，但 `IsPersistenceSuppressed` 仍为 true 且防抖延迟计数仍为 0（一笔写入都没排）。 |
| **2. Undo Reset** | 未解析身份**重新出现**= 当前未解析集合非空 → 在任何写入之前重新抑制；自动保存与退出快照都不写 | `LM-08`：`UndoLayout()` 后 Digest 回到种子、两个身份回来、抑制重新武装（理由含 `third.party.scene`）、`CanRedoLayout` 仍为 true；随后 `delay.CompleteAll()` + 200 ms，那笔在飞的自动保存既不产生完成事件也不落文件，文件字节等于干净写入。`LS-06`：真实 `Edit > Undo Layout` 菜单项回放到种子（连 hidden 的 `third.party.chat` 一起回来）后，一次**真实右键 Hide Panel** 仍写不出任何东西（完成事件计数不变、文件不变），`CaptureShutdownSnapshot` 返回 null 且理由含 `cannot resolve`。 |
| **3. 显式 Save Layout** | 按 ADR-009 忠实保留未知身份/顺序/hidden，并结束**当前**抑制 | `LM-08`：Undo 后强制 `SaveLayoutAsync()` → `Saved`、文件同时含 `third.party.scene` 与 `third.party.chat`、抑制结束。同一事实在真实菜单路径由 D8-E 的 `Real_save_layout_persists_unknown_identities_and_hidden_state_faithfully`（`RP-4`）覆盖。 |
| **4. Redo 与后续 Undo** | 抑制状态由**实际状态转换**决定，不得只看历史游标或上一次操作的名字 | `LM-08`：那次显式保存把"已接受集合"记为这两个身份，所以紧接的 Redo 结果里未解析数为 0 → **不**重新抑制，并排出自己的自动保存（`delay.Count == 2`，等到第 3 笔完成事件，写出干净文件）；而干净写入把已接受集合**替换**为空，于是再一次 Undo 构成**新的**重现 → 抑制重新武装且 `delay.Count` 保持 2。 |

实现侧的两条判据（读代码确认，非测试专用语义）：`_acceptedUnresolvedPanels` 在每次
成功写入时被**当时的**未解析集合替换（不是并集），所以"曾经被接受"不会越过一次干净
写入继续生效；`PausePersistenceForUnresolvedPanels` 在未解析集合非空时取消调度器，
已排队但尚未触发的防抖会被 `Cancel()` 的代次递增吞掉，因此不会有"迟到的一写入"绕过
重新武装的抑制。

### 10.6 移交 D9-F 的显式事项（不在本轮预先结论）

- **四态契约的持久化回归**：`LM-08` + `LS-06` + `LW-12` 属同一族，D9-F 全量回归
  必须继续覆盖它们，且真实进程门禁仍串行执行，不与构建或 `dotnet format` 并发。
- **浮窗边界抖动门禁的稳定性复验**：
  `Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart`
  （`RealProcessRestoreTests`）在本轮第一次全量 Debug 运行失败（持久化浮动 X 读回
  0 而非 180），之后的 Debug 与 Release 全量运行均通过。D9-F 须把它列为显式复验
  项并**保留每次真实结果**：若再次失败，必须判断是窗口管理器在最大化/还原过渡期
  给出帧矩形时序造成的测试不稳定，还是产品在过渡期确有持久化缺陷——**不得预先
  把任何复现归为环境噪声**。
- **D9-E 的限制须原样带入封印记录**：真实进程侧只覆盖标签拖动与标签右键关闭；
  分割条一次拖动成一项、浮动/回停几何、Prepare 拒绝、提交后宿主失败四项只有
  headless 真实控件证据；全部证据来自单显示器、96 DPI 交互式桌面。

## 十一、D9-F 最终回归与证据封印（2026-10-27）

### 11.1 授权范围与实际遵守的边界

用户 2026-10-27 裁决 D9-E 正式通过、解除 HOLD 并授权 D9-F，范围限四项：最终源码
状态的 Debug/Release 全量回归、浮窗 Bounds 稳定性复验、D9-A…D9-E 证据封印、
最终覆盖限制清单。本阶段的实际执行边界：

- **零产品源码改动**。D9-F 期间只写了文档与本节；`git status` 中 Nova 路径的
  变更集与第十节结束时完全一致（23 个 `M` + 38 个 `??`，均为 D2…D9-E 的交付物），
  暂存区为空，HEAD 仍为 `6017998`，无 commit/push/tag/merge。
  按 mtime 核实"最终源码状态"：最后一个被修改的手写源文件是
  `DockPersistencePolicyTests.cs`（2026-10-27 05:55，属 §10.5 对齐工作），
  D9-F 时间窗（14:55 之后）内 `src/`、`tests/`、`samples/` 下 0 个手写源文件变更，
  只有 `obj/**/AssemblyInfo.cs` 这类构建生成物。
- ADR-005/007/008/009 的冻结原文未作任何修改；场景命令历史、快捷键路由、
  现有文件写入策略未被触碰。
- 未做 DPI／多显示器／无人值守验收，也未做任何与本阶段无关的架构变更——
  这是用户明确的禁止项，本节的通过数字不外推为这些环境的验收。
- 真实进程门禁全程串行执行，且不与构建或 `dotnet format` 并发。

### 11.2 最终源码状态的全量回归（第 1 项，实测）

| 步骤 | Debug | Release |
| --- | --- | --- |
| 从零全量重建 `dotnet build Nova.sln -c <cfg> --no-incremental` | Build succeeded，**0 错误**，MSBuild 汇总 **18 条警告**（日志内 `NU1900` 共 36 行：项目段与汇总段各列一次），耗时 14.19 s | Build succeeded，**0 错误 / 18 条警告**（同样 36 行记录），耗时 8.46 s |
| 重建后全量测试 `dotnet test Nova.sln -c <cfg> --no-build` | Performance 5、Core 117、Controls 76、Integration 141（2 m 49 s）→ **339/339，0 失败、0 跳过** | 同配置逐项 5 / 117 / 76 / 141（2 m 39 s）→ **339/339，0 失败、0 跳过** |
| §10.6 移交的同族门禁过滤复跑（按文件：`DockLayoutFaultMatrixTests` 7 项 = LM-01…LM-07，`DockPersistencePolicyTests` 21 项 = 含 LM-08 与 LW 族的持久化策略门禁，`DockLayoutHistoryShellTests` 6 项 = LS-01…LS-06，合计 34） | **34/34** | **34/34** |

`dotnet test --no-build` 自身不产生 `NU1900`（0 条）。`NU1900` 来自不可达的
`nuget.cdn.azure.cn` 漏洞源，与 D9 无关，但**本轮两次构建都是"零错误、存在警告"，
不是零警告构建**。

### 11.3 浮动窗口 Bounds 稳定性复验（第 2 项）

复验对象是 §10.6 移交的
`RealProcessRestoreTests.Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart`
（最大化／还原过渡期），连同该类的全 8 项真实进程门禁一起、在两个配置下各跑一次，
日志完整落盘：

| 配置 | 结果 | 过渡期两项的实测耗时 |
| --- | --- | --- |
| Debug | 8/8 通过 | `Maximizing_…` 6 s、`Unmaximizing_…` 8 s |
| Release | 8/8 通过 | `Maximizing_…` 7 s、`Unmaximizing_…` 6 s |

历史事实按用户要求**保留、不在封印时删除**：该门禁在 2026-10-27 的第一次全量
Debug 运行中失败过（磁盘里的浮动 X 读回 `0` 而非 `180`），此后每一次 Debug 与
Release 全量运行均通过。本轮从零重建后两个配置的全量运行各含该门禁一次，
加上定向复验一次，**每配置 2 次、共 4 次全部通过**。本轮复验**没有拿到可判定的
复现**，因此既不判定为窗口管理器在最大化/还原过渡期给出帧矩形的时序不稳定，也不
判定为产品在过渡期有持久化缺陷——它作为**待验证风险**封印。若再次失败，判据是：
比较写入文件的 `RestoreBounds`/`maximized` 与 UIA／Win32 当刻观察到的窗口状态，
两者不一致属于产品过渡期缺陷，两者一致而断言取到过渡中途的帧矩形属于门禁时序
不稳定。

### 11.4 本轮的一次未命名失败（证据缺陷，如实记录）

- 第一次 Release 全量运行报 `Nova.Editor.IntegrationTests` **Failed: 2 / Passed: 139**。
  当时的输出管道用 `grep` 只保留了汇总行，**失败用例名与消息被丢弃**，该次失败
  无法指名。这是本阶段自身的证据缺陷（日志处理方式造成），记录在此而不掩盖。
- 同一批 Release 二进制随后两次完整复跑**未复现**：单独运行 Integration
  **141/141**（3 m 06 s），从零重建后全量 **339/339**。
- 该次运行期间没有任何并发构建或 `dotnet format`，所以不能引用 §8.4 记录过的
  "并发负载导致假失败"作为解释。
- 从这次之后所有运行改用 `--logger "console;verbosity=normal"` 并完整落盘，
  用例名不再可能丢失。
- 结论：失败**未复现且未指名**，按"未判定"封印；最终通过数字取自从零重建后的
  两次全量运行（11.2）。本轮不把这次失败解释为环境噪声。
- 诚实标注一条**可能性**（不作为结论）：那次是 Release 全量运行，理论上无法排除
  未指名的两项中含 §11.3 那条环境敏感的浮窗边界门禁；但没有任何证据支持这一归因，
  因此只记为可能性。它同时说明为什么 §11.3 的定向复验要单独跑并留全量日志。

### 11.5 D9-A…D9-E 证据封印清单（第 3 项）

| 应封存的内容 | ADR-010 落点 | 状态 |
| --- | --- | --- |
| 冻结契约九条（历史边界、唯一记录点、快照内容、Revision/Digest、Redo 分支与容量、宿主故障、持久化交互、Shell 入口与快捷键归属、视图所有权） | 第三节 D9-A-1…D9-A-9 + 第五节 O-1…O-8 与三项精确化 | 已封存，后续阶段未反向修改 |
| 历史边界与既有事实基线（场景命令历史并存、14 个赋值点收敛到提交通道） | 第二节、第三节 D9-A-1/D9-A-2 | 已封存 |
| 纯历史模型 + 17 门禁 + 快照内存实测 | 第八节（8.1–8.4，`Lh01`…`Lh17`；216.0 KiB/2 190 B 与 2 605.8 KiB/26 419 B 两种实测） | 已封存（D9-B 已接受） |
| 协调器接线：唯一记录点、`ModelCommitted == false` 不入栈、降级入栈一次、Retry 不记录不提交、`MayReplayLayout` 前置拒绝 | 第九节 9.1、9.3 | 已封存（D9-C 已接受） |
| Shell 入口实测事实：`MenuItem.IsEnabled` 不由 `ICommand.CanExecute` 驱动，故显式绑定可用性并把拒绝原因写进状态栏；Retry 置位后必须重新发布状态 | 第九节 9.2 | 已封存（D9-D 已接受） |
| 宿主故障矩阵：Prepare 拒绝（历史游标与模型不动、孤立宿主即弃、清除故障后重放成功）与 Commit 后 Apply/Show 失败（外观"提交成功"但模型已提交、降级停放两种重放、`Retry` 复原） | 10.1 的 `LM-06`/`LM-07`，主宿主侧 `LW-10`/`LW-11`/`LS-04` | 已封存 |
| LM-08 术语修正与四态 Reset／抑制契约（正常 Reset / 只驱动模型段 / Undo Reset / 显式保存 / Redo 与后续 Undo） | 10.1 术语前提、10.5 四态表、`LS-06`、`EditorWorkspace` 的配对保存注释 | 已封存，用户确认"不改冻结契约、只澄清状态语义"的方法 |
| 真实进程结果：标签拖动一笔＋Undo/Redo（`RP-L1`）、标签右键关闭→Undo 回到活标签（`RP-L2`），与 D8-E 的 `RP-4`/`RP-5` 交叉验证 | 10.1、10.3 | 已封存 |
| 初稿失败审计：10 项门禁/编译返工 + 1 项由 HOLD 指出的契约措辞缺陷，逐条写明"测试假设错误"还是"产品事实" | 10.2 | 已封存（应用户要求，不只留最终通过数字） |
| D9-F 自身的证据缺陷（11.4 的未命名失败） | 11.4 | 已封存为未判定项 |

门禁全清单在本文可指认：`Lh01`…`Lh17`、`LW-05`…`LW-12`、`LS-01`…`LS-06`、
`LM-01`…`LM-08`、`RP-L1`/`RP-L2`，另引用 D8-E 的 `RP-4`/`RP-5`。

### 11.6 最终覆盖限制（第 4 项，不得外推）

- **真实进程侧只覆盖标签拖动与标签右键菜单关闭**（`RP-L1`、`RP-L2`）。分割条
  一次拖动成一项、浮动/回停的多根几何、Prepare 拒绝、Commit 后宿主失败四项
  主要由 headless 真实控件门禁覆盖（`LM-01`/`LM-03`/`LM-06`/`LM-07` 等）——本
  测试套件没有 Avalonia headless 平台，控件由测试手工 `Measure/Arrange`，
  因此"真实控件"不等于"真实窗口输入"。
- 全部证据来自**单显示器、96 DPI 的交互式桌面会话**。多显示器、混合 DPI、
  跨 DPI 感知、以及无桌面会话的无人值守环境（服务／CI）下的重放与真实进程门禁
  **未验证**；11.2 的通过数字不构成对这些环境的任何承诺。
- 浮窗最大化／还原过渡期的边界门禁带有一条**已指名但未复现**的历史失败（11.3），
  本轮另有一条**未指名的** Release 全量失败无法排除与它相关（11.4）；两条都按
  待验证风险封印，不写成已解释。
- **O-6 是既定产品限制，不是本轮缺陷**：Undo/Redo 恢复布局与身份（含 hidden
  顺序与不透明未解析身份），不恢复面板内部内容状态——冻结的 D8-D 语义下 Hide
  与 Close 都释放视图，重放必然向工厂要一个全新实例。状态栏与本文都明示这一点。
- 布局历史**不落盘**：真实进程重启后布局在、历史为空（D9-A-7），Undo/Redo 不跨
  进程，也不绕过 D8 的未解析抑制。
- Release 的 `Nova.Editor.App` 路径由测试按输出目录推断配置定位；若只构建了
  Debug，真实进程门禁会以文件缺失／`Assert.Inconclusive` 暴露，不会静默跳过。
- 构建始终有 `NU1900`（每配置 18 条汇总记录），因此本轮以及此前所有阶段的证据
  都是"零错误、存在警告"，不得表述为零警告构建。

### 11.7 D9 关闭记录（用户 2026-10-27 裁决）

- D9-A 契约冻结、D9-B/D9-C/D9-D 已接受、D9-E 正式通过（HOLD 解除）、D9-F **正式
  通过**，四项授权工作全部交付：本节即为封印记录。

| 里程碑 | 最终状态 |
| --- | --- |
| D9-A — 契约冻结 | 已完成 |
| D9-B — 纯历史模型 | 已完成 |
| D9-C — 协调器接线 | 已完成 |
| D9-D — Shell 入口与门禁 | 已完成 |
| D9-E — 实时交互与故障矩阵 | 已正式验收 |
| D9-F — 最终回归与证据封存 | 已通过 |
| D9 总体 | **CLOSED — scope-bound** |

- 关闭记录以本节为准；此前各阶段的初稿失败与修正过程继续保留在 §8.4、§9.4、
  §10.2 与 §11.4，不以最终测试数字覆盖审计历史。
- 用户明确：本次关闭**不代表所有环境均已验证，也不代表所有历史测试异常均已找到
  根因**；裁决基于提交的回归数据与本节记录，用户未独立检查工作树或复跑测试。

### 11.8 残余风险纪律（D9 关闭后仍然生效）

后续任何阶段的报告与门禁都不得违反以下四条：

1. **未指名的 Release 失败不得改写为"已查明原因"**。§11.4 那 2 项失败永久无法指名，
   状态是"未判定"；若再次出现，必须完整保存失败用例名、断言消息与整份测试日志，
   重新判断其对产品正确性的影响。后续通过不能反向证明它是误报。
2. **构建数字口径固定**：两次从零重建均为"零错误、18 条警告"。日志里 36 行
   `NU1900` 是同一警告在项目段与汇总段重复呈现，既不得记为 36 条独立警告，也不得
   表述为零警告构建。
3. **通过数字不外推**：D9 的 339/339 只覆盖单显示器、96 DPI 交互式桌面；多显示器、
   混合 DPI、无人值守环境属于 §11.6 声明的未验证范围。
4. **浮窗 Bounds 异常是复验事实不是历史噪声**：§11.3 保留那次已指名的失败及其
   判定判据；封印没有把它归为环境噪声。

### 11.9 封印后的工作树状态

- 工作树保持未提交：HEAD `6017998`、暂存区空、无 commit/push/tag/merge；本裁决
  只关闭 D9 技术验收，**不授权任何 Git 操作**。
- `editor_avalonia`、`native/gate` 及其他无关 dirty 文件保持原状。所有测试日志写在
  `$TEMP`；仓库内只有 MSTest 自 2026-10-22 起就在用的、已被 `.gitignore:119`
  忽略的 `TestResults/` 部署目录，没有新增未忽略的产物。
- 下一阶段（D10：Docking 多显示器与 DPI 鲁棒性）不得扩张 D9 范围，须建立独立契约
  与验收门禁；D9 之后的第一步是 D6–D9 累积工作树的范围盘点，见
  `docs/D6-D9-Worktree-Inventory.md`。

### 11.10 关闭记录的接受与 D10 交接（2026-10-27，不改动以上任何数字）

- 用户裁决：**D9 关闭记录（§11.7–§11.9）与工作树盘点作为交付文档予以接受**，
  D9 总体维持 `CLOSED — scope-bound`；补强记录**不重写 D9-F 当时的测试数字**，
  本轮交付未执行构建或测试，因此也不补写不存在的验证结果。
- 盘点暴露的两项追溯性缺口——**33 个未被 ADR 直接点名的 src/test 文件**、
  **`LW` 族仅 11/39 用例有 ID**——按**已披露缺口**保留，不自动推翻 D9 验收；
  D10 新增或修改的行为与门禁改在 ADR-011 §四·一 建立条款级映射。
- D10-A 契约（ADR-011 的 O-1…O-6）已于同日逐项裁决并冻结为 Accepted，D10-B 获授权
  启动，**生产代码修改仍未授权**。ADR-010 §11.6/§11.8 所列未验证环境与"未判定"失败
  继续原样生效，由 ADR-011 继承而非消解。
- **继承下来的一次再取证（不改动 §11.4 的原始记录，也不据此归因）**：D10-B 第二轮的
  Release 全量回归里，本族的失败第一次带上了完整指名——
  `DockHiddenPanelShellTests.Undo_after_a_reset_returns_unknown_panels_and_pauses_persistence_again`
  抛 `System.TimeoutException`（`tests/Nova.Editor.IntegrationTests/DockHiddenPanelShellTests.cs:321`
  的 10 秒保存完成等待），同批 345/346；单独连续 3 轮复跑通过。判定仍为**未判定**，
  完整指名、异常、行号与未过滤日志见 ADR-011 §十·六。

## 十二、D9-A-10 补遗实现与门禁证据（F1，2026-10-28）

### 12.1 生产改动落点

- **Core**（`src/Nova.UI.Core/Docking/DockLayoutHistory.cs`）：新增
  `DockLayoutHistoryReconcileOutcome` / `DockLayoutHistoryReconcileResult` 与
  `ReconcileCurrent(expectedCurrent, settled)`。前置匹配在 API 内部：无当前项或
  当前项内容与 `expectedCurrent` 不符 → `Rejected` 且零变更；已等于 `settled` →
  `Unchanged`；相等 → 原地替换并返回 `Reconciled`。只替换当前项，项数、游标、
  容量、Redo 分支与 `Revision` 均不受影响；未扩展 `DockLayoutHistoryOutcome`。
- **Coordinator 降级门禁**（`src/Nova.UI.Avalonia/Docking/DockWorkspaceCoordinator.cs`）：
  `RejectIfDegraded` 在模型变更之前介入，覆盖 `FloatPanel`、`MovePanel`（含拖动
  提交与回停）、`ReplaceMainRoot`、`RestoreDefault`、`CloseFloatRoot`、
  `ChangePanelVisibility`（隐藏/显示）、`ClosePanel`、`ActivateTab`（令牌与编程式）、
  `ResizeSplit`（令牌与编程式）、`MergeAndClose`；`UpdateFloatBounds` 另行拒绝并把
  窗口几何回写为已提交值，使拒绝可见。`Retry`、`BeginShutdown` 与只读诊断不受
  门禁限制；`ReplayLayout` 保留其专用降级诊断。拖动开始/预览不改变模型，保持
  可用；拖动提交经同一门禁。
- **浮窗 `Retry` 校准**：`RetryHost` 成功且确实由本次 `Retry` 完成 `ShowHost` 的
  隐藏浮窗，依次执行"捕获归一化前快照 → 验证当前历史项 → `NormalizeFloatGeometry`
  → 捕获落定快照 → `ReconcileCurrent` → 通知"。主宿主与可见浮窗的 `Retry` 不校准；
  归一化与校准不新增历史项、不移动游标、不推进 `Revision`。
- **校准失败处理**：验证或校准被拒绝时关闭该浮窗宿主以保持保护生效；若归一化已
  写模型，则按 `expectedCurrent` 内容恢复到同一 `Revision`；返回
  `DockHostError.RetryFailed` 与诊断，不报告成功，下一次 `Retry` 仍可自愈。

### 12.2 门禁

| 门禁 | 位置 | 覆盖 |
| --- | --- | --- |
| Lh18 | `tests/Nova.UI.Core.Tests/DockLayoutHistoryTests.cs` | 前置匹配、仅替换当前项、幂等、无当前项/不匹配拒绝零变更、计数/游标/Undo/Redo 分支一致 |
| Lh19 | 同上 | 校准前的 P 不得仅因属于同一次浮动操作而被接受；只有落定的 S 可重放；`Replay` 校验不放宽 |
| F1-01 | `tests/Nova.Editor.IntegrationTests/DockSettlementCalibrationTests.cs` | 真实 `EditorDockWorkspace` + 真实 `DockWindowRootHost` + 假 `IDockFloatWindow`（首次 `Show` 抛错）；七阶段：①首个 Show 失败后模型提交一次、历史一项、降级、`Capture()` 阻止、`CanUndoLayout == false` 且 `CanRedoLayout == false`；②降级期间激活/分割条/隐藏/关闭/移动/再浮动/Undo/Redo 全部在模型变更前被拒，模型/`Revision`/历史不变；③Retry 后模型写回落定几何 S、`Revision` 不变、项数与游标不变；④立即 `Capture()` 记录 S；⑤立即 Undo 恢复基线且 `Revision` +1；⑥立即 Redo 以 S 成功重放；⑦模型、窗口、历史与快照一致且不新增历史项 |
| 协调器补充 | `tests/Nova.UI.Controls.Tests/DockWorkspaceCoordinatorTests.cs` | 降级期间 `UpdateFloatBounds` 被拒、模型不写入；既有 `Retry_recovers_only_the_degraded_float_host` 保持宿主/视图/令牌/`Revision` 不变 |
| LM-07 | `tests/Nova.Editor.IntegrationTests/DockLayoutFaultMatrixTests.cs` | 按 D9-A-6 修订：降级期间直接 `ResizeSplit` 被拒且不记录；Retry 后历史仅含既有提交 |

实证几何（F1-01）：主屏 2560×1600、工作区 2560×1552、缩放 1.0；提交时请求
P = (2400, 1400, 640, 480)，首次 Show 钳制到 S = (1920, 1072, 640, 480)。

### 12.3 已执行证据（实测）

| 项 | 结果 |
| --- | --- |
| `dotnet build Nova.sln -c Debug` | 0 错误；18 条 `NU1900`（同一不可达包源警告，项目段与汇总段共 36 行） |
| `dotnet test Nova.sln -c Debug --no-build` | **371/371** 通过（Core 119、Controls 101、Integration 146、Performance 5） |
| `dotnet build Nova.sln -c Release` | 0 错误；18 条 `NU1900`（同一警告，共 36 行） |
| `dotnet test Nova.sln -c Release --no-build` | **371/371** 通过（分布同上） |
| `dotnet format Nova.sln --verify-no-changes --no-restore` | 退出码 0（仅 workspace 加载警告） |
| `DockLayoutHistoryTests`（含 Lh18/Lh19，Debug 定向） | 19/19 通过 |
| `DockSettlementCalibrationTests`（F1-01，Debug 定向） | 1/1 通过 |
| `DockLayoutFaultMatrixTests`（LM-01…LM-07，Debug 定向） | 7/7 通过 |
| `DockWorkspaceCoordinatorTests` + `DockFloatPlacementHostTests` | 45/45 通过 |

### 12.4 全量回归记录与既有未判定项

- 2026-10-28 解除测试暂停后补跑 Debug/Release 全量：两配置均 371/371、0 错误、
  18 条 `NU1900`，`dotnet format` 退出 0。较 D9-F 的 368 新增 3 项，即 Lh18、
  Lh19 与 F1-01。
- ADR-011 中"未判定"的 Release 失败
  （`DockHiddenPanelShellTests.Undo_after_a_reset_returns_unknown_panels_and_pauses_persistence_again`）
  在本次 Release 全量中通过；判定仍保持**未判定**，不因一次通过改写既有失败记录。
- D9-F 的 368/368（双配置）、§11 关闭记录与 D9 总体 `CLOSED — scope-bound` 均按
  原样保留，不被本节改写。
- D10 总体状态与 D10-D 真实多显示器/混合 DPI 验收保持原样，不因 F1 补遗推进。

### 12.5 边界纪律

- 2026-10-28 用户指示提交当前全部改动：创建 `dfd70f5`（416 个文件，排除
  `crashes/` 本地转储）；无 push/tag/merge；提交前未修改 `editor_avalonia`、
  `native/gate` 等内容，未修改系统显示缩放。
- 未改动 schema 版本、未放宽 `Replay`、未引入 O1/O2 或无关 Shell 重构。




