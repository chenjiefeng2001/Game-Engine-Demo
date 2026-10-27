# ADR-006: Avalonia Dock 主工作区投影契约

- 状态：Accepted（D1 评审通过、D2/D3/D3.1/D4 实施完成，2026-10-25）
- 日期：2026-10-25
- 契约来源（只引用，不复制）：
  - `p7_ui` `docs/ui-spec/11-docking-model.md` §2、§7.1、§7.4
  - `p7_ui` `docs/decisions/ADR-009-dock-tree-workspace-state.md` A、C、F
  - `p7_ui` `docs/ui-spec/14-docking-interactions.md` §8.2、§8.3
  - `p7_ui` `docs/ui-spec/13-panel-lifecycle.md` §7、§8、§9.1
  - Nova `docs/decisions/ADR-005-dock-tree-model.md`
- 实现（阶段 D2–D4）：`src/Nova.UI.Avalonia/Docking/`、
  `src/Nova.Editor.Views/Docking/`
- 门禁（阶段 D2–D4）：`tests/Nova.UI.Controls.Tests/DockProjectionTests.cs`、
  `tests/Nova.Editor.IntegrationTests/DockShellIntegrationTests.cs`、
  `tests/Nova.Editor.IntegrationTests/DockInteractionTests.cs`

本 ADR 只冻结模型到 Avalonia 的适配边界，不新建第二份 Docking 架构说明。

## 背景与范围

ADR-005 已冻结纯 `DockTree` 模型并通过 25 个模型门禁。D2 在适配层实现最小
投影后，D3 已把投影接入真实工作区入口，替换 v0.2 固定 Grid，并在真实
`TabControl` 激活路径上启用代次守卫（DA-11）。

本切片只覆盖主工作区：`DockSplitNode → 拆分容器`、`DockTabGroupNode → 标签
容器`、`ActivePanel` 和模型提交后的视觉更新。不包含拖放、可交互 Splitter、
浮动窗口、隐藏/未解析面板、Dock 结构持久化、布局 Undo。

## 冻结契约

### 1. 权威与写入边界

- `DockTree` 是结构、面板顺序、活动标签和权重的唯一权威。适配层只接收不可变
  快照，不提供修改树、权重、顺序或活动标签的入口。
- 视觉树是只读投影，禁止从控件尺寸、`SelectedIndex` 或控件顺序反推布局。
- 适配层缓存（节点容器、面板视图）是可丢弃派生数据；除投影所需的
  `Current` 诊断结果外，不保存任何可与模型竞争的状态。
- 模型操作失败时不调用 `Apply`；适配层不补齐、不回滚、不重试模型事务。
- Nova 模型暂不提供 `Revision`。本切片用 `DockTree.Digest` 标识提交快照，
  用适配层私有的 `Generation` 标识视觉应用代次；**不伪造 `Revision`**。
- `ActivePanel` 只表示标签活动状态，不触碰场景选择权威（ADR-005）。

### 2. 节点映射

- 每个 `DockNodeId` 对应一个模型节点；节点类型与 ID 一一对应。类型或方向在
  同一 ID 下改变视为新节点：旧子树释放，新子树重建。
- 节点身份只认 `DockNodeId`，不认控件类型、标题或兄弟索引。
- 同一提交快照重复 `Apply` 是空操作，不重建节点容器。
- 节点从树中移除后，其容器和不再被任何面板引用的资源必须释放；
  `FindNode` 不得再返回已移除节点。

### 3. 面板映射

- 每个 `PanelInstanceId` 在树内唯一，对应一个 `IDockPanelView`，由注入的
  `IDockPanelViewFactory` 创建。标题仅用于显示，永远不参与查找、缓存或事件路由。
- 标题相同但 `PanelInstanceId` 不同的面板必须是两个独立标签、两份独立视图。
- D2 对树内每个实例创建一次视图（含非活动标签）；`Apply` 期间实例仍在树内时
  复用视图，不重复调用工厂。非活动视图保留但不挂载显示。工厂只构造显示内容，
  不得在创建时启动昂贵工作；延迟创建留给后续切片。
- 工厂抛出或返回无效对象：为该实例显示可诊断占位，布局仍有效，其他面板不受
  影响；不自动重试，不把占位当作关闭。该错误计入
  `DockApplyResult.PanelFailures`，不使整次投影降级。用户显式 `Retry()` 时必须
  只重试这些失败实例；普通重复 `Apply` 不重新调用工厂，健康面板不得被重建。
- 面板离开树时释放其视图恰好一次；同一 ID 之后重新加入视为新生命周期。

### 4. Split 投影

- `Horizontal` → `Grid` 列：`First` 在左，`Second` 在右。
- `Vertical` → `Grid` 行：`First` 在上，`Second` 在下。
- 两个子单元使用模型权重作为 `GridUnitType.Star` 比例，不换算像素、不写回
  实测尺寸。权重只改变对应 Split，不影响面板视图。
- D2 不渲染可交互 Splitter，不捕获拖动预览，也不把视觉测量结果提交模型。
- 权重非法时模型已经拒绝，适配层不接收该快照；适配层自行“修正”权重属于
  违约。

### 5. Tab 投影

- `DockTabGroupNode.Panels` 的顺序就是标签顺序；`ActivePanel` 就是当前选中项。
- D2 的标签容器为 `TabControl`，每个标签是保持稳定顺序的 `TabItem`：
  `Header` 显示视图标题，`Content` 承载对应 `IDockPanelView.Content`。
- 激活只改变 `SelectedIndex`，不得重建面板视图、改变 `Panels` 顺序或调用工厂。
  重排只移动标签项，不重建对应视图。
- 组变为空时由模型规范化移除；适配层负责释放该组容器，不能保留空标签页。

### 6. 失败恢复与降级

- 只有成功提交的 `DockTree` 才能进入 `Apply`。模型已提交而视觉失败时，
  **不得回滚模型**，也不得让新旧视觉树混合显示。
- 视觉应用失败（由 `IDockNodeApplyInterceptor` 等确定性注入触发）：丢弃本次
  新建的部分视觉树，按同一提交快照重建一次。
- 重建仍失败：`Root` 换为可诊断降级占位，`Status = Degraded`，
  `AppliedDigest != CommittedDigest`，`IsCurrent == false`。模型和已提交快照不变。
- 降级只能由用户显式 `Retry()` 恢复；重试使用同一提交快照，不伪造新模型状态。
  本切片没有自动保存路径，也不允许降级状态进入未来的保存流程。
- 面板工厂失败不是整树应用失败，按第 3 节处理为面板级 `Failed` 占位；面板级
  `Retry()` 不改变整树 `Applied` 状态。
- 降级期间 `FindNode` 不返回任何节点；同摘要 `Apply` 只返回 `Degraded`，不自动
  重试；不同摘要的 `Apply`、或显式 `Retry()`，才能开始新的视觉应用尝试。

## 接口契约（D2 实现）

```csharp
namespace Nova.UI.Avalonia.Docking;

public readonly record struct DockApplyToken(long Generation, string Digest);

public enum DockApplyStatus { Unchanged, Applied, Degraded }

public sealed record DockApplyResult(
    DockApplyStatus Status,
    long Generation,
    string CommittedDigest,
    string? AppliedDigest,
    IReadOnlyList<PanelInstanceId> PanelFailures,
    string? Diagnostic)
{
    public bool IsHealthy => Status != DockApplyStatus.Degraded;
    public bool IsCurrent => IsHealthy && AppliedDigest == CommittedDigest;
}

public interface IDockPanelView : IDisposable
{
    string Title { get; }
    Control Content { get; }
}

public interface IDockPanelViewFactory
{
    IDockPanelView Create(PanelInstanceId instance);
}

// 仅作为确定性失败注入门禁，不参与生产布局决策。
public interface IDockNodeApplyInterceptor
{
    void OnApplying(DockNodeId node, DockApplyToken token);
}

public sealed class DockWorkspaceProjector : IDisposable
{
    public DockWorkspaceProjector(
        IDockPanelViewFactory panelFactory,
        IDockNodeApplyInterceptor? interceptor = null);

    public Control Root { get; }              // 稳定的 ContentControl 宿主
    public bool IsDisposed { get; }
    public DockApplyResult? Current { get; }

    public DockApplyResult Apply(DockTree tree);
    public DockApplyResult Retry();            // 仅重试失败面板；降级时整树重试

    // 只读诊断/门禁入口；不暴露可变缓存。
    public Control? FindNode(DockNodeId id);
    public IDockPanelView? FindPanel(PanelInstanceId instance);

    public void Dispose();
}
```

`Apply`/`Retry`/`Dispose` 必须在 Avalonia UI 线程调用。`Apply` 在已释放实例上
抛出 `ObjectDisposedException`；`tree` 为空抛出 `ArgumentNullException`。

## 应用协议

1. 计算 `tree.Digest`。若当前健康且代次已应用该摘要，
   返回 `Unchanged`：不递增代次、不调用工厂、不移动控件；上次结果中的
   `PanelFailures` 原样返回，不自动重试失败的工厂。
2. `Generation + 1`，构造以 `(Generation, Digest)` 为标识的应用尝试。
3. 从提交快照自上而下构建视觉树：节点容器可按需重建或复用，但仍在树内的
   面板实例必须复用既有视图；新面板调用一次工厂。
4. 任一节点应用失败：释放本次新建的节点控件，按同一快照完整重建一次。
   已存在的面板视图可继续复用；不得把失败的部分树挂到 `Root`。
5. 重建成功：一次性替换 `Root` 内容，再释放不再引用的旧容器；返回 `Applied`，
   `AppliedDigest = CommittedDigest`。
6. 重建仍失败：释放本次新建的部分树，`Root` 显示降级占位，返回 `Degraded`。
   已创建的面板视图保留在缓存中供显式 `Retry` 复用，并在 `Dispose` 或面板
   离开树时释放。上一次成功摘要保留在 `AppliedDigest` 供诊断，但不得声称
   当前视觉已应用。
7. `Retry()` 在健康但存在 `PanelFailures` 时，只重新调用失败实例的工厂；成功则
   替换占位并更新标题，失败则保留面板级占位。仅在 `Current.Status == Degraded`
   时执行整树应用尝试；其他无失败状态直接返回当前结果，不做空转重建。
8. 每次实际应用尝试（含自动重建、整树 `Retry`、面板级 `Retry`）都递增
   `Generation`；`Unchanged` 和健康无失败的空 `Retry` 不递增。失败尝试产生的
   token 立即失效，不能用于当前或后续投影。

### 代次与恢复语义

- `Digest` 只标识已提交模型内容；`Generation` 只标识视觉应用尝试。不得把
  `Generation` 当作模型 Revision，也不得让两者互相推导。
- 使代次失效的事件：新摘要 `Apply`、首次尝试失败后的自动重建、显式整树
  `Retry`、面板级 `Retry`、`Dispose`。
- 保留代次的事件：同摘要健康 `Apply`（`Unchanged`）、健康且无失败的空
  `Retry`。
- 恢复性重建使用最新权威 `DockTree` 的快照和新的 token；被丢弃的部分投影、
  被替换的旧面板视图以及旧回调均不得继续视为当前。
- 降级时禁止：把任何节点视为已挂载、重复 `Apply` 同一摘要自动重试、自动保存、
  用旧 `AppliedDigest` 声称 `IsCurrent`。
- `Retry()` 只使用最后一次 `Apply` 的 `DockTree`/`Digest`；结果通过
  `CommittedDigest`、`AppliedDigest` 和 `IsCurrent` 判定是否对应当前模型。

## 事件与资源生命周期

- 同一提交快照重复 `Apply`：零工厂调用、零控件重建、零新增订阅。
- 面板视图在实例留在树内期间跨多次 `Apply` 保持同一对象；标签激活/重排只改变
  容器位置，不重建视图。
- 面板所有权按每次提交树的**最终成员关系**结算：跨组移动保留视图与宿主；容器
  （Split/TabGroup）重建不释放仍属于新树的面板视图；只有不在最终树中的实例才
  释放。降级时同样按最后一次提交树结算，保留属于该树的视图供 `Retry`。
- 节点/面板移除：先脱离视觉树，再释放视图或容器一次；释放后不得再被复用。
- 面板视图自己持有的订阅必须在 `IDisposable.Dispose` 中解除；适配层负责调用
  一次。失败/降级时已创建的视图保留在缓存中供 `Retry`，最终仍由 `Dispose`
  统一释放，不得重复创建同名实例的视图。
- 适配层自己的回调（D3 已启用标签激活回调）必须捕获应用代次，执行前校验当前
  代次和 `IsDisposed`；旧代次回调直接丢弃，不得修改活动标签、顺序或权重。该规则
  由 DA-11 在真实回调路径上覆盖。工作区会话替换通过释放旧适配层实例实现，
  旧实例回调同时因 `IsDisposed`/旧代次失效。
- 适配层实例的生命周期不得超过工作区会话；会话替换时调用方必须释放旧实例并
  创建新实例，旧实例的所有延迟回调因 `IsDisposed`/旧代次失效。
- `Dispose` 幂等：释放所有已创建视图与容器、使旧代次失效；再次调用不抛异常、
  不重复释放。释放后不得再接受 `Apply`。

## 验收矩阵（D2/D3 门禁映射）

| ID | 场景 | 期望 | 阶段 |
| --- | --- | --- | --- |
| DA-01 | 水平/垂直 Split 方向与子序 | 水平为左右列、垂直为上下行；`First` 在左/上；嵌套方向正确 | D2/D3 |
| DA-02 | 权重投影 | 两个子单元星号比例等于模型权重；`ResizeSplit` 后只更新布局，不调用工厂、不改变视图对象 | D2/D3 |
| DA-03 | 标签顺序 | 标签顺序等于 `Panels`；`ReorderTab` 后顺序正确且面板视图对象不变 | D2/D3 |
| DA-04 | 活动标签同步 | 选中项/选中内容等于 `ActivePanel`；`ActivateTab` 只同步选中项，面板视图对象不变 | D2/D3 |
| DA-05 | 同名面板身份 | 标题相同、实例 ID 不同 → 两个标签两份视图；单独移除/激活互不影响 | D2/D3 |
| DA-06 | 重复投影幂等 | 同摘要再次 `Apply` 返回 `Unchanged`；代次、节点/视图引用、摘要均不变，工厂零调用 | D2/D3 |
| DA-07 | 节点/面板移除释放 | 移除后视图释放恰好一次；空组折叠后容器释放；`FindNode` 返回 null，旧视图不再复用 | D2/D3 |
| DA-08 | 模型失败不污染视觉 | 失败操作不产生树；不调用 `Apply`；再次应用最后成功树返回 `Unchanged`，模型摘要与视觉均不变 | D3 |
| DA-09 | 首次视觉失败后重建 | 注入一次节点应用失败 → 丢弃部分树并从已提交快照重建成功；`Applied/IsCurrent`，无新旧混合树；失败与重建各推进一次代次 | D3 |
| DA-10 | 持续失败显式降级 | 注入持续失败 → `Degraded`、显示占位、`AppliedDigest != CommittedDigest`；模型不回滚；同摘要 `Apply` 不自动重试；显式 `Retry` 使用最后提交树并可恢复 | D3 |
| DA-11 | 旧代次迟到回调 | 旧代次/已释放实例的回调被丢弃，不改变标签、顺序或权重 | D3 |
| DA-12 | 释放与幂等 | `Dispose` 释放全部视图/容器一次且可重入；释放后 `Apply` 抛 `ObjectDisposedException` | D2/D3 |
| DA-13 | 面板工厂失败 | 占位显示，`PanelFailures` 含该实例，状态仍为 `Applied`，兄弟面板正常 | D3 |
| DA-14 | 隐藏/浮动/未解析 | 本切片不实现；沿用 WM-15..17、WM-19..21 的延后决定 | 延后 |
| DA-15 | 面板跨组移动的所有权 | `MovePanel` 后视图与工厂调用数不变；容器重建不释放视图；最终移除才释放 | D2/D3 |
| DA-16 | 面板级显式重试 | 同摘要普通 `Apply` 零工厂；`Retry` 只重试失败实例，健康视图不变；再次失败仍是面板级占位且整树 `Applied` | D2/D3 |

D3 必须保持既有 Dock 模型门禁 25/25 和全量基线不回退。DA-11 已在真实
`TabControl.SelectionChanged` 路径上启用，不使用测试专用伪入口。

## 与既有契约的映射

| 既有项 | 本次处置 |
| --- | --- |
| `11` §2 分层、`ADR-009` A | 模型唯一权威；适配层缓存可丢弃（第 1、2 节） |
| `ADR-009` C 身份 | `DockNodeId`/`PanelInstanceId` 分离；标题非身份（第 2、3 节） |
| `ADR-009` F、`11` §7.4、`14` §8.2 | 模型提交与视觉应用分离；重建一次后显式降级（第 6 节、DA-09/10） |
| `14` §8.3 | 旧代次回调丢弃；适配层实例不跨工作区会话（生命周期、DA-11） |
| `13` §7、§8 | 视图释放幂等、订阅不重复、会话/代次守卫（生命周期、DA-06/07/12） |
| `13` §9.1 | 工厂失败 → 面板占位，布局有效（第 3 节、DA-13） |
| DI-01/03/15/24 | 分别映射到 DA-04/03/02/09；拖放和交互手势不在本切片 |
| PL-05/07/10 | 分别映射到 DA-06/12/13 |

## 明确不做（D2/D3 边界）

- 不实现 `DockHost` 拖放、`DragController`、命中测试或可交互 Splitter。
- 不实现标签关闭、隐藏/显示、浮动/停靠、未注册面板恢复。
- 不把 Dock 结构写入布局文件；D3 的 `WorkspaceLayout` 只保留窗口尺寸、主题
  和既有面板字段，不再承担结构权威。
- 不引入布局 Undo/Redo，不修改 `EditorCommandStack` 或场景命令。
- 不把视觉尺寸、`SelectedIndex` 或控件树写回模型。
- 不提交、不推送、不创建标签或合并；`editor_avalonia`、native/gate 和无关
  dirty 文件保持不变。

## D2 退出门槛

| 验收项 | 完成标准 |
| --- | --- |
| 模型回归 | DockTree 专项 25/25 继续通过 |
| 全量回归 | Debug / Release 既有测试不回退 |
| Split / Tab 投影 | 视觉结构与模型一致（DA-01..04） |
| 实例身份 | 同名面板可区分（DA-05） |
| 幂等性 | 同摘要重复投影不调用工厂（DA-06） |
| 资源所有权 | 移动保留视图，最终移除才释放（DA-07、DA-15） |
| 故障恢复 | 部分投影失败重建一次，仍失败显式降级（DA-09、DA-10） |
| 重试 | 失败面板可显式重试且不影响健康面板（DA-16） |
| 变更边界 | 只改适配层代码、投影测试与本 ADR |

构建必须如实报告 `NU1900`；18 条警告来自不可达的 Azure China 漏洞源，不能
把该构建称为零警告构建。

## D1 评审与 D2 启动（2026-10-25）

D1 六项冻结面有条件通过；D2 获准实现主工作区最小投影。D2 只新增
`src/Nova.UI.Avalonia/Docking/` 和 `tests/Nova.UI.Controls.Tests/` 中的投影
门禁，不修改 Dock 纯模型、`EditorShellView`、持久化或交互代码。

D1 阶段只新增本 ADR，未实现投影器、未新增测试。上一阶段基线（ADR-005 实测）：
Debug/Release 均 0 错误，各有 18 条 `NU1900`；全量 98/98、Dock 过滤 25/25
通过；`dotnet format --verify-no-changes` exit 0。

## D2 实施与验证（2026-10-25）

D2 只新增以下文件，Dock 纯模型、`EditorShellView`、交互与持久化均未改动：

- `src/Nova.UI.Avalonia/Docking/DockProjectionContracts.cs`
- `src/Nova.UI.Avalonia/Docking/DockWorkspaceProjector.cs`
- `tests/Nova.UI.Controls.Tests/DockProjectionTests.cs`
- 本 ADR 的 D2 章节

投影覆盖 DA-01..DA-10、DA-12、DA-13、DA-15、DA-16；新增 12 个投影门禁。
同摘要 `Apply` 返回 `Unchanged` 且零工厂调用；`Retry()` 在健康但有面板失败时只
重建失败实例，降级时按最后提交树整树重试；每次实际应用尝试推进一次
`Generation`，空操作不推进。面板所有权按最终树成员结算，跨组移动复用视图，
容器重建不释放仍在树内的视图，最终移除才释放。

实测结果：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建 | 0 错误，18 条 `NU1900`（Azure China 漏洞源不可达） |
| Release 构建 | 0 错误，18 条 `NU1900`（同上） |
| Dock 模型专项 | 25/25 通过 |
| 投影门禁 | 12/12 通过 |
| Debug 全量 | 110/110 通过（Core 49、Integration 38、Performance 5、Controls 18） |
| Release 全量 | 110/110 通过（同上分布） |
| 格式检查 | `dotnet format Nova.sln --verify-no-changes --no-restore` exit 0 |
| 变更边界 | 仅新增适配层、投影测试与本 ADR；未提交、未推送、未打标签、未合并 |

D2 当时的限制：DA-11 尚未有真实视图回调面；未接入 `EditorShellView`；
未实现拖放、可交互 Splitter、浮动窗口、隐藏/未解析面板、持久化与布局 Undo。
这些限制由 D3 部分解除，见下节。

## D3 实施与验证（2026-10-25）

### D3-A：真实 Shell 接入

- `EditorShellView` 不再用固定 Grid 承载四个内联面板，改为单个
  `ContentControl x:Name="DockRoot"`；构造时创建 `EditorDockWorkspace`，
  以 `DockTree.CreateDefault()` 作为唯一结构权威，并把投影根挂到 `DockRoot`。
- 默认树映射为左侧 Hierarchy、中央 Scene、底部 Inspector/Console 标签组；
  真实面板视图为 `HierarchyPanelView`、`ScenePanelView`、`InspectorPanelView`、
  `ConsolePanelView`，由 `EditorDockPanelFactory` 创建，保留原有 ViewModel 与
  UiState 绑定。
- `WorkspaceLayout` 降级为窗口尺寸、主题和既有面板字段的持久化数据，不再
  决定工作区结构；Reset Layout 同时重置 Dock 树；Dock 结构在 D3 不写入布局
  文件。隐藏面板不在本切片范围，因此移除对应菜单项。
- 新增文件：
  - `src/Nova.Editor.Views/Docking/HierarchyPanelView.axaml(.cs)`
  - `src/Nova.Editor.Views/Docking/ScenePanelView.axaml(.cs)`
  - `src/Nova.Editor.Views/Docking/InspectorPanelView.axaml(.cs)`
  - `src/Nova.Editor.Views/Docking/ConsolePanelView.axaml(.cs)`
  - `src/Nova.Editor.Views/Docking/EditorDockPanelFactory.cs`
  - `src/Nova.Editor.Views/Docking/EditorDockWorkspace.cs`
  - 修改 `EditorShellView.axaml(.cs)` 与本 ADR。

### D3-B：真实回调面与 DA-11

- 投影器新增 `PanelActivated` 事件，参数携带 `(GroupId, Panel, DockApplyToken)`。
  事件只由真实 `TabControl.SelectionChanged` 触发；每次构建给对应标签容器一个
  `TokenSlot`，初始选中期间抑制回调，避免投影自身制造激活事件。
- `EditorDockWorkspace` 是唯一消费者，按顺序校验：实例未释放、
  `Current.IsCurrent == true`、token 代次等于当前 `Generation`、token 摘要等于
  当前 `CommittedDigest`。任一不满足即丢弃，不修改模型或视觉。
- 面板级 `Retry()` 推进代次后刷新所有仍挂载的 token slot，使当前标签继续可
  激活；已脱离的旧控件保留旧 token，迟到回调必然失效。
- 普通同摘要 `Apply` 返回 `Unchanged`，不推进代次，因此当前代次回调保持有效。
- DA-11 全程使用生产调用链，没有测试专用伪回调入口。

### D3-C：端到端场景与结果

新增 `tests/Nova.Editor.IntegrationTests/DockShellIntegrationTests.cs`，11 个门禁：

| 场景 | 验证 |
| --- | --- |
| 有效树投影 | 真实 Shell 挂载投影根；默认树结构与标签顺序正确 |
| 重复刷新 | 同摘要 `Apply` 为 `Unchanged`，零工厂调用，四个视图对象不变 |
| 面板移动 | 跨组移动复用视图；容器重建不释放仍在树内的视图 |
| 面板移除 | 最终移除只释放一次，其他视图不受影响 |
| 视觉失败恢复 | 一次失败自动重建成功；持续失败显式降级并显示占位 |
| 显式重试 | 降级后 `Retry` 使用最后提交树恢复，代次继续推进 |
| 模型失败隔离 | 失败模型操作不产生树、不改变摘要、不触碰视觉；再次 `Apply` 为 `Unchanged` |
| 当前标签激活 | 真实 `SelectionChanged` 更新模型活动标签并重建容器，不新建视图 |
| 面板级重试 | 重试后代次更新，当前标签仍可激活 |
| 旧代次迟到回调 | 快速连续投影后，旧标签容器回调被丢弃，不污染模型 |
| 释放后回调 | `Dispose` 后回调被忽略，`Apply` 抛 `ObjectDisposedException` |

实测结果：

| 验证项 | 结果 |
| --- | --- |
| Debug 构建 | 0 错误，18 条 `NU1900`（Azure China 漏洞源不可达） |
| Release 构建 | 0 错误，18 条 `NU1900`（同上） |
| Dock 模型专项 | 25/25 通过 |
| 投影门禁 | 12/12 通过 |
| Shell / 回调门禁 | 11/11 通过 |
| Debug 全量 | 121/121 通过（Core 49、Integration 49、Performance 5、Controls 18） |
| Release 全量 | 121/121 通过（同上分布） |
| 格式检查 | `dotnet format Nova.sln --verify-no-changes` exit 0 |
| 变更边界 | 只改适配层、Editor Shell/Dock 面板宿主、相关门禁与本 ADR；未提交、未推送、未打标签、未合并 |

D3 结论：真实 Shell 的模型投影与面板身份一致；DA-11 已在真实视图回调路径
启用并通过；失败、重建、降级、重试和迟到回调语义均按冻结契约工作。

仍有限制：未实现拖放、可交互 Splitter、浮动窗口、隐藏/未解析面板、Dock 结构
持久化与布局 Undo；降级提示与用户重试入口由 D3.1 补齐，见下节；UI 线程约束为
契约要求，实现不主动校验线程。Debug/Release 各 18 条 `NU1900`，不是零警告
构建。

## D3.1 降级恢复入口（2026-10-25）

D3 的 `Retry()` 只是工作区编程入口。D3.1 补上 Shell 内的可诊断降级提示与用户
可触发的显式重试，不改变 Dock 模型、投影恢复语义或持久化边界。

### 行为

- `EditorDockWorkspace` 新增 `IsDegraded`、`IsRetrying` 和 `StateChanged` 事件；
  每次 `Apply`、模型提交后的投影以及 `Retry` 完成后通知 Shell。降级判断只读
  `DockApplyResult.Status`，不复制状态。
- `EditorShellView` 在 Dock 宿主之上叠加降级提示条，显示投影诊断文本；健康或
  重试成功后隐藏，普通刷新不会误显示。
- “Retry layout”按钮直接调用现有 `EditorDockWorkspace.Retry()`，不新增恢复
  逻辑。重试再次失败时提示条保留并更新为最新诊断；成功后清除。
- 重试期间按钮禁用；工作区拒绝 `Apply`、`ActivateTab`、`MovePanel`、
  `RemovePanel`、`ResizeSplit`、`ResetToDefault` 以及嵌套 `Retry()`，面板激活
  回调直接丢弃，避免重试过程中模型和视觉进一步分歧。
- `Retry()` 仍只使用最后一次提交的权威 `DockTree`；结果通过
  `CommittedDigest`、`AppliedDigest` 和 `IsCurrent` 判定，`Generation` 与旧
  回调失效规则不变。
- 为在真实 Shell 上注入确定性视觉失败，`EditorShellView` 增加一个接收
  `IDockNodeApplyInterceptor`/`IDockPanelViewFactory` 的构造重载；生产入口仍
  使用默认工厂，未引入测试专用回调路径。

### 门禁与结果

`DockShellIntegrationTests` 新增 2 个门禁：真实 Shell 显示降级提示、按钮重试
失败保留诊断、成功后隐藏并恢复视图；重试进行中布局变更与嵌套重试均被拒绝，
且恢复后提交摘要等于当前权威树。原有 11 个 D3 门禁不回退。

| 验证项 | 结果 |
| --- | --- |
| Shell / 回调 / 恢复门禁 | 13/13 通过 |
| Dock 模型专项 | 25/25 通过 |
| 投影门禁 | 12/12 通过 |
| Debug 全量 | 123/123 通过（Core 49、Integration 51、Performance 5、Controls 18） |
| Release 全量 | 123/123 通过（同上分布） |
| Debug / Release 构建 | 0 错误，各 18 条 `NU1900` |
| 格式检查 | `dotnet format Nova.sln --verify-no-changes` exit 0 |
| 变更边界 | 只改工作区控制器、Editor Shell、Shell 门禁与本 ADR；未提交、未推送、未打标签、未合并 |

D3.1 后仍不包含拖放、可交互 Splitter、浮动窗口、隐藏/未解析面板、Dock 结构
持久化和布局 Undo。Debug/Release 各 18 条 `NU1900`，不是零警告构建。

## D4 可交互 Splitter 与标签交互（2026-10-25）

D4 让用户通过现有 `DockTree` 操作修改布局，不引入第二套交互状态。拖动预览与
正式提交分离；标签选择仍以 `ActivePanel` 为权威。

### D4-A：Splitter

- `DockSplitterThumb : Thumb` 作为覆盖层加入投影原有的两列/两行 `Grid`，不改变
  D2 的子节点顺序与权重断言。水平 Split 位于第一列右缘，垂直 Split 位于第一行
  下缘；`First` 始终对应左/上子节点。
- 拖动只把两列/两行的 `GridLength` 改为像素预览；`DragStarted`/`DragDelta`
  期间不调用投影、不推进代次、不触发工厂、不提交模型。
- `DragCompleted` 只发起一次 `SplitResizeRequested`；工作区用真实
  `DockTree.ResizeSplit` 提交，成功后才把 `Accepted` 置真。失败、取消、无变化或
  `total < 2 × 32px` 时恢复模型星号权重，已提交树不变。
- 拖动夹取最小窗格 32px（极小容器取 `total / 4`）；提交的权重仍由模型校验和
  归一化，非法/极小尺寸不会写入模型。
- Avalonia `Thumb` 在指针捕获丢失时也会抛出 `DragCompleted`。`DockSplitterThumb`
  在 `OnPointerCaptureLost` 标记丢失并在随后的完成通知中回滚预览，避免把捕获
  丢失误当成正常释放。
- 每个 Splitter 持有自己的 `TokenSlot`，并加入投影的挂载令牌列表；面板级
  `Retry()` 推进代次时刷新令牌，整树重建生成新令牌。工作区在提交前校验
  `IsCurrent`、`Generation` 与 `Digest`，降级、旧代次或重试中的请求一律拒绝。

### D4-B：标签交互

- 标签切换以真实 `TabControl.SelectionChanged` 为入口；工作区只在
  `ActivateTab` 成功提交后接受，投影只把模型 `ActivePanel` 作为初始选中项。
- 被拒绝的选择（旧代次、降级、重试中）会把视觉 `SelectedIndex` 恢复到本次构建
  的模型活动标签，不留下部分选择状态。
- 同标题不同 `PanelInstanceId` 的标签按实例区分；切换只重建 `TabItem` 容器，
  面板视图对象和工厂调用次数不变，旧标签容器的迟到回调不能修改当前模型。
- **D4 明确不包含标签重排交互**：`DockTree.ReorderTab` 模型操作仍可用，但本阶段
  不提供拖拽标签重排的 UI；重排留在后续切片。

### D4-C：门禁映射与结果

新增 `tests/Nova.Editor.IntegrationTests/DockInteractionTests.cs`，13 个门禁：

| 场景 | 验证 |
| --- | --- |
| 水平拖动 | 预览只改像素列宽；模型/代次/事件/工厂不变 |
| 单次提交 | 释放后仅一次 `ResizeSplit`；新列星号权重与模型一致 |
| 垂直拖动 | 行方向、子序、星号权重与模型一致 |
| 取消 | 显式取消与捕获丢失都恢复星号权重，零提交 |
| 无变化释放 | 模型 `AlreadyInState` 被拒绝，视觉恢复且零提交 |
| 极小容器 | 低于最小窗格总宽度时拒绝提交并恢复 |
| 极端拖动 | 夹取 32px 后提交，权重归一化且模型有效 |
| 一次视觉失败 | 提交后自动重建一次并保持新权重 |
| 持续视觉失败 | 降级占位；旧 Splitter 交互被拒绝；`Retry` 恢复提交权重 |
| 旧 Splitter 回调 | 代次不匹配时被拒绝，模型和当前视觉不变 |
| 面板级重试令牌 | `Retry()` 刷新 Splitter 令牌后仍可提交 |
| 标签提交/回滚 | 成功切换一次提交；旧容器选择回滚且模型不变 |
| 同标题实例 | 两个实例视图独立，切换不新建视图 |

实测结果：

| 验证项 | 结果 |
| --- | --- |
| Dock 模型专项 | 25/25 通过 |
| 投影门禁 | 12/12 通过 |
| Shell / 回调 / 恢复门禁 | 13/13 通过 |
| D4 交互门禁 | 13/13 通过 |
| Debug 全量 | 136/136 通过（Core 49、Integration 64、Performance 5、Controls 18） |
| Release 全量 | 136/136 通过（同上分布） |
| Debug / Release 构建 | 0 错误，各 18 条 `NU1900`（Azure China 漏洞源不可达） |
| 格式检查 | `dotnet format Nova.sln --verify-no-changes` exit 0 |
| 变更边界 | 只改适配层、工作区控制器、交互门禁与本 ADR；未提交、未推送、未打标签、未合并 |

交互门禁在无窗口的 Avalonia 布局上运行：测试直接调用 `Measure`/`Arrange`
布置真实投影网格，并通过公开的 Splitter/标签容器状态验证模型一致性。捕获丢失
门禁会先注入 `Thumb` 的指针起点，再走 Avalonia 自身的捕获丢失路径，因此覆盖的是
框架真实行为而不是测试替身。

D4 后仍不包含拖放停靠、浮动窗口、隐藏/未解析面板、Dock 结构持久化、布局 Undo
和标签重排交互。Debug/Release 各 18 条 `NU1900`，不是零警告构建。

## D5 拖放停靠与目标定位

### D5-A：拖放契约（先行冻结，2026-10-25）

拖放会话只是临时交互状态；`DockTree` 仍是唯一布局权威。模型操作继续复用既有
`MovePanel`（同组重排、跨组移动、边缘分割），不新增模型入口，也不引入第二套
活动标签或布局状态。

- **源身份**：拖动源为 `PanelInstanceId` 加起始 `DockNodeId`；目标为组
  `DockNodeId` 加 `DockTarget.Center(index)` 或 `DockTarget.Edge(side)`。全程
  不使用显示标题作为身份。
- **会话开始**：在标签条内按下左键先记录待定候选，指针移动超过 4px 才进入
  拖放。未越过阈值的按下/释放保持现有标签点击语义。
- **按下触发激活**：按下未激活标签会先走真实 `ActivateTab` 并重建投影。尚未
  越过阈值的候选允许重新绑定到重建后的源面板（源仍存在且投影健康）；已经
  开始的拖放在任何 `Apply`、`Retry`、降级或重建时立即取消。
- **命中规则**（组内相对坐标）：
  - 顶部最外 `min(10px, 标签条高/2)` → `DockEdge.Top`；
  - 其余标签条（高 `min(36px, 组高/4)`）→ `DockTarget.Center(插入下标)`；
  - 内容区左/右/下外缘 `min(48px, 可用尺寸/4)` → 对应 `DockEdge`；
  - 内容中心 → `DockTarget.Center(null)`（追加）；源组内容中心不作为目标。
- **插入下标语义**：`Center.Index` 是“移除源面板后的目标列表下标”，与现有
  `MovePanel` 一致。同组重排按视觉槽位在源项之后时扣除 1；模型继续负责夹取、
  `AlreadyInState`、`TargetGone` 与树合法性校验。
- **预览**：投影根内的透明覆盖层只显示拖动幽灵、目标高亮和插入线。预览不修改
  `DockTree`、不推进代次、不调用面板工厂、不触发 `StateChanged`。
- **提交**：释放时只发起一次 `PanelMoveRequested`（源面板、目标、会话令牌）。
  工作区校验 disposed/retrying/`IsCurrent`/Generation/Digest 后调用
  `MovePanel`；只有成功才置 `Accepted` 并投影。拒绝、失败、无变化或目标消失
  不产生任何部分布局变化。
- **取消**：释放到无目标区域、捕获丢失、Escape（根有焦点时）、阈值不足、源面板
  在提交时已不存在、投影重建/降级/重试中，一律丢弃会话，模型不变。
- **资源**：跨组移动只重建容器；仍在树内的面板视图按 `PanelInstanceId` 复用，
  不新增工厂调用、不释放；离开树的面板按既有规则释放一次。
- **明确不包含**：浮动窗口、跨窗口拖放、隐藏/未解析面板、自动滚动、根级停靠
  区域、Dock 结构持久化和布局 Undo。真实 OS 鼠标拖放作为 D5 初步冒烟项单独
  记录；未通过前不宣称桌面交互已验证。

### D5-B：实现接入

- `DockDragController` 挂在投影根，使用隧道/冒泡指针事件和根级捕获；标签条按下
  只记录候选，越过 4px 阈值才建立活动会话。会话记录源 `PanelInstanceId`、源组
  `DockNodeId`、起始下标与会话代次令牌，不使用标题匹配目标。
- 目标定位基于已布置的组边界和真实 `TabItem` 几何：顶部边缘、标签条插入槽、
  内容区左/右/下边缘与内容中心分别映射到 `DockEdge` 或 `DockTarget.Center`；
  标签条高度优先取实际首个 `TabItem` 底边，取不到时才回退到
  `min(36px, 组高/4)`。插入槽在同组源项之后时换算为“移除后下标”。
- 覆盖层 `DockDragOverlay` 只承载幽灵、组高亮和插入线，`IsHitTestVisible=false`；
  拖动期间不调用 `DockTree`、不推进代次、不触发工厂和 `StateChanged`。
- 释放时控制器只发起一次 `PanelMoveRequested`；工作区沿用既有
  disposed/retrying/`IsCurrent`/Generation/Digest 门禁，调用真实
  `MovePanel`，成功才置 `Accepted` 并投影。`Apply`/`Retry`/降级/重建会取消
  活动会话；按下触发的激活重建只允许尚未越过阈值的候选重新绑定并重新捕获。
- 释放到无目标区域、源组内容中心、拖出窗口、捕获丢失、Escape 和模型拒绝均不
  产生部分布局变化。跨组移动只重建容器；仍在树内的视图按实例复用。

### D5-C：门禁映射与结果

新增 `tests/Nova.Editor.IntegrationTests/DockDragTests.cs`，11 个门禁：

| 场景 | 验证 |
| --- | --- |
| 同组重排 | 一次提交；标签顺序正确；四个面板视图对象和工厂调用数不变 |
| 跨组标签条投放 | `MovePanel` 成功；目标组活动面板正确；源视图复用 |
| 左边缘停靠 | 水平 Split、左新组、子序与新分割 0.5/0.5、父权重保留 |
| 下边缘停靠 | 垂直 Split、新组位于目标之后、父权重保留 |
| 源内容中心投放 | 无目标，模型/摘要/代次/事件/工厂不变 |
| 捕获丢失取消 | 取消活动会话，零提交，预览清除 |
| 工作区外释放 | 零提交，预览清除 |
| 拖动中重建 | 旧释放被丢弃，当前布局不变 |
| 拖动中视觉失败 | 单次重建后失败即降级；旧交互被拒绝；`Retry` 恢复且可再次提交 |
| 未激活标签按下 | 激活重建后候选重新绑定，随后拖动可提交 |
| 阈值不足 | 不建立会话，零提交 |

实测结果：

| 验证项 | 结果 |
| --- | --- |
| Dock 模型专项 | 25/25 通过（本轮未改模型） |
| 投影门禁 | 12/12 通过 |
| Shell / 回调 / 恢复门禁 | 13/13 通过 |
| D4 交互门禁 | 13/13 通过 |
| D5 拖放门禁 | 11/11 通过 |
| Debug 全量 | 147/147 通过（Core 49、Integration 75、Performance 5、Controls 18） |
| Release 全量 | 147/147 通过（同上分布） |
| Debug / Release 构建 | 0 错误，各 18 条 `NU1900`（Azure China 漏洞源不可达） |
| 格式检查 | `dotnet format Nova.sln --verify-no-changes` exit 0 |
| 变更边界 | 只改适配层投影器/契约、拖放控制器、工作区控制器、D5 门禁与本 ADR；未改 `DockTree`，未提交、未推送、未打标签、未合并 |

### D5 真实桌面冒烟（2026-10-25）

在 Windows 交互会话中启动 `Nova.Editor.App`，通过系统输入队列注入指针和键盘
事件（`SetCursorPos` / `mouse_event` / `SendInput`），并用窗口 `PrintWindow`
读取应用自身渲染结果。该环境的屏幕 `CopyFromScreen` 返回了过期的桌面合成帧，
因此交互结果以窗口渲染和可见状态变化为准，不以全屏截屏为准。

| 场景 | 实测 |
| --- | --- |
| 输入可达性 | 真实点击 `+ Entity` 后 Hierarchy 新增 `Entity 01`、计数 6→7、Inspector 同步 |
| 同组重排完成 | 按住底部 Console 标签拖过 Inspector 后释放，标签变为 Console/Inspector，Console 内容激活 |
| 边缘停靠完成 | 按住 Inspector 标签拖到 Scene 内容左缘释放，形成 Hierarchy / Inspector / Scene 三列，新分割近 0.5/0.5，底部仅剩 Console |
| 预览不提交 | 拖动中截图同时显示覆盖层高亮/幽灵和仍在原位的源标签；释放前模型未变 |
| Escape 取消 | 拖 Scene 到有效底部目标后按 Escape 再释放，布局与拖动前一致 |
| 无效目标 | Hierarchy 投放到自身内容中心，布局不变 |
| 拖出窗口释放 | Scene 拖到工作区外释放，布局不变且外部应用未收到点击（指针捕获有效） |
| 关闭清理 | 正常关闭应用；删除本次冒烟生成的 `%LOCALAPPDATA%\NovaEditor\layout.json` |

冒烟证明真实桌面输入能走通命中、捕获、取消与完成路径，但它不是人工手感测试，
也不覆盖多显示器/高 DPI、视觉合成延迟和无响应窗口等场景；完整目标矩阵仍以
headless 门禁为准。D5 后仍不包含浮动窗口、跨窗口拖放、隐藏/未解析面板、自动
滚动、根级停靠、Dock 结构持久化和布局 Undo。Debug/Release 各 18 条 `NU1900`，
不是零警告构建。
