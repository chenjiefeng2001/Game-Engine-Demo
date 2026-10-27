# ADR-007: 多根工作区与浮动停靠契约（D6-A）

- 状态：Accepted（D6-A 契约冻结；D6-B 纯模型、D6-C 宿主抽象/故障恢复、D6-D 真实浮窗与跨窗口交互均已验收；D7 持久化另见 ADR-008）
- 日期：2026-10-26（D6-A 冻结与 D6-B 实施同日）
- 关联：ADR-005（单根 DockTree）、ADR-006（Avalonia 投影）、
  p7_ui `docs/ui-spec/11-docking-model.md`、`ADR-009-dock-tree-workspace-state.md`
- 范围：D6-A 冻结多根模型/宿主契约；D6-B 只实现纯模型。宿主抽象、
  视图注册表、真实浮窗、跨窗口拖放均不在 D6-B 内

## 背景

D5 已验收。当前 Nova 只有一个主工作区树 `DockTree`，`DockWorkspaceProjector`
每重建一次就拥有并释放该树中的面板视图。继承规范已经把完整工作区定义为
`WorkspaceState(MainRoot, FloatingRoots, HiddenPanels, UnresolvedPanels)`，
并冻结了 WM-16、WM-19、WM-20、WM-21 等浮动语义；Nova 实现的是主根子集。

直接把浮动窗口加在当前单根投影器上会产生三个不可接受的结果：

1. 主树与浮动窗口变成两套布局权威；
2. 面板在根之间移动时视图所有权不清，容易重复创建、提前释放或双重释放；
3. 窗口创建/关闭失败没有模型级原子语义，可能留下“面板已离开主树、但浮动宿主
   不存在”的丢失状态。

本 ADR 只裁决多根模型和宿主生命周期的契约。D6-B 起才实现纯模型，
D6-C 接入宿主抽象，D6-D 才接入真实 OS 顶层窗口。

## 范围与非目标

**本契约覆盖**

- 主根与浮动根之间的模型关系、全局身份和上限；
- 面板视图跨根移动时的唯一所有权；
- 浮动窗口系统关闭与显式关闭面板的区别；
- 浮动宿主创建、投影、显示失败的补偿与降级；
- 多根下的提交、摘要、代次、拖放令牌与逐宿主恢复语义。

**明确不包含**

- Dock 结构持久化、恢复和迁移；
- 隐藏面板、未解析面板、布局 Undo；
- 多工作区、跨进程布局、任意嵌套浮动根；
- 多实例面板的完整行为（只保留身份和唯一性接口）；
- 第三方 Dock 库替换模型；
- 多显示器混合 DPI 的完整保真（D6-D 只做最佳努力并记录边界）。

## 决策

### D6-A-1 模型形状：一个不可变工作区，多个单根树

新增 `DockWorkspace`，它是唯一布局权威：

```csharp
public sealed class DockWorkspace
{
    public long Revision { get; }
    public DockTree MainRoot { get; }
    public IReadOnlyList<DockFloatRoot> FloatingRoots { get; }
    public string Digest { get; }
}

public sealed class DockFloatRoot
{
    public DockFloatRootId Id { get; }
    public DockTree Root { get; }
    public DockBounds Bounds { get; }        // DIP 设计空间
    public bool Maximized { get; }
    public DockReturnTarget ReturnTarget { get; } // 可选的回停提示
}

public readonly record struct DockBounds(double X, double Y, double Width, double Height);
public readonly record struct DockReturnTarget(DockNodeId GroupId, int Index);
```

- `DockTree` 继续作为**单根结构引擎**：二叉 Split、标签组、规范化、单根校验和
  单根摘要全部复用。工作区级操作负责跨根事务，不另写一套 Split/TabGroup 逻辑。
- `DockWorkspace` 只包含 `MainRoot + FloatingRoots`；`HiddenPanels` 和
  `UnresolvedPanels` 留到独立切片，但命名和校验器不得阻止以后加入。
- 模型不引用 Avalonia、窗口、控件或 ViewModel。`Bounds` 是数值，不是窗口对象。
- 工作区提交是原子的：所有根、`Revision`、`Digest` 一起替换；失败返回失败结果，
  当前 `DockWorkspace` 及其根逐字段不变。跨根操作不得出现“源已移除、目标未插入”。

### D6-A-2 身份、节点 ID 与全局唯一

- `PanelInstanceId` 在主根和全部浮动根之间**全局唯一**；跨根移动不改变实例 ID。
  同一个面板实例不得同时出现在两个宿主中，因此也不存在“同一视图克隆到两个窗口”
  的语义。
- `DockNodeId` 在主根和全部浮动根之间**全局唯一**。`FloatRootId` 是独立身份类型，
  不得与 `DockNodeId` 混用。
- 节点 ID 按根命名空间确定性分配：
  - 主根保持既有 `dock.main` / `dock.split.*` / `dock.tabs.*` 等 ID 和分配方式；
  - 浮动根使用 `dock.float.<ordinal>.split.N` / `dock.float.<ordinal>.tabs.N`；
  - 同一操作序列必须产生相同 ID，不得依赖标题、数组下标或运行时控件。
- `FloatingRoots` 按 `FloatRootId` 升序保存，保证摘要和未来序列化稳定；窗口
  Z 序属于宿主运行时状态，不写入模型。
- 工作区校验器负责跨根检查：节点 ID 重复、面板 ID 重复、浮动根 ID 重复、
  根数/总节点数/总面板数超限、空浮动根。单根校验器只负责根内结构。
- 主根必须始终非空。任何会使主根变空的操作（浮动最后一个主根面板、关闭最后一个
  主根面板）返回 `WouldEmptyMainRoot`，不得提交。

### D6-A-3 视图所有权：工作区级注册表是唯一所有者

当前 `DockWorkspaceProjector` 同时拥有投影和面板视图。多根下继续这样做会导致
跨根移动时源投影器把视图当作“离开本树”而释放，目标投影器又创建新视图，破坏
D5 已保证的视图复用，并可能双重释放。

契约：

- 新增工作区级 `DockPanelViewRegistry`，按 `PanelInstanceId` 拥有
  `IDockPanelView`，是面板视图和标题的**唯一所有者**。所有根共享同一个注册表。
- 每个根有自己的 `DockWorkspaceProjector` / 宿主，只负责把注册表中的视图内容
  挂载到本根控件树；投影器不再拥有视图，也不在重建时释放树外视图。
- 跨根移动只转移视图内容的挂载位置：源宿主卸载 `Content`，目标宿主挂载同一个
  视图对象；不调用面板工厂、不释放、不重建业务状态。
- 只有以下情况释放视图：显式关闭面板、显式关闭整个浮动根、`RestoreDefault`
  移除浮动根、工作区整体 Dispose。释放恰好一次；`Dispose()` 抛异常只记录诊断，
  不回滚已提交模型。
- 注册表、宿主和投影器都限定在 UI 线程；模型保持无线程亲和。
- 面板视图创建失败时保留失败记录和占位内容，其他面板不受影响；`Retry` 只重建
  失败项。失败视图仍归注册表所有，跨根移动不会把它变成“新面板”。

### D6-A-4 窗口生命周期：系统关闭与显式关闭分离

| 动作 | 语义 | 模型结果 | 宿主结果 |
| --- | --- | --- | --- |
| 浮动窗口标题栏关闭（X） | 窗口系统关闭，不是关闭面板 | 全部面板按原顺序回停主根；`MergeFloatRoot` 成功 | 成功提交后关闭浮宿主 |
| 显式关闭单个面板 | 关闭面板实例 | 从所在根移除；浮根空了则移除该根 | 释放该面板视图；浮根空了关闭宿主 |
| 关闭浮动根全部面板 | 显式 close-all，必须确认 | `CloseFloatRoot` 移除根及全部面板 | 释放全部视图后关闭宿主 |
| 关闭主窗口 | 应用/工作区关闭 | 不再提交布局变更 | 关闭全部浮宿主，工作区整体 Dispose |
| 关闭最后一个主根面板 | 不允许 | `WouldEmptyMainRoot`，不提交 | 宿主不变 |

**窗口 X 的推荐语义是“回停主根”，不是“关闭其中面板”。** 理由：

- 不引入隐藏面板状态（隐藏/未解析面板仍未实现），面板不会因关窗而不可达；
- 关窗不会丢失面板业务状态，也不需要判断面板是否允许关闭；
- `CloseFloatRoot` 的破坏性语义只用于用户显式选择的“关闭全部”，不绑定到 X。

回停目标解析规则（Coordinator 层，模型只接收具体 `MergeFloatRoot` 目标）：

1. 优先使用 `DockFloatRoot.ReturnTarget` 指向的主根组；
2. 该组已不存在时，按主根先序遍历选择第一个标签组；
3. 把浮动根中的全部面板按先序遍历顺序（组内保持原顺序）追加到目标组，
   目标组原活动面板保持不变；
4. 目标组不存在或主根不可用则失败，取消本次关窗，保持浮根和窗口不变；
5. 回停只保证面板身份、顺序和内容不丢，不承诺恢复浮动前的 Split 结构。

**主窗口关闭**：Coordinator 进入 shutting-down 状态，关闭全部浮宿主但**不**触发
X 回停提交；工作区只 Dispose 一次注册表和投影器。布局保存仍由主窗口统一负责，
浮窗口不得各自保存布局。这个规则在 D6-C 用假宿主门禁验证，在 D6-D 用真实窗口
冒烟验证。

**焦点与活动面板**：活动标签仍由各组 `ActivePanel` 表达；没有工作区级“当前
活动面板”。在一个宿主中激活标签不改变其他宿主的 `ActivePanel`，也不改变既有
`SelectionService` 的选择权威。

### D6-A-5 宿主失败：预检失败不提交，提交后失败可恢复

浮动宿主分三阶段：

1. **Prepare**：根据候选 `DockWorkspace` 创建隐藏宿主并投影候选浮根；任何窗口
   创建或投影失败 → 释放宿主，返回宿主错误，**模型保持原样**。
2. **Commit**：模型事务一次性提交新的主根/浮根/Revision；因为候选已完整校验，
   此处失败视为内部错误，释放预检宿主并保持模型原样。
3. **Show**：显示窗口。失败时不静默回滚模型，也不丢弃面板；保留已提交浮根，
   将宿主标记为 degraded，并在主 Shell 显示可诊断信息和显式的“回停主根”按钮。

模型提交成功后的任何视觉失败都沿用 ADR-006 的恢复契约：

- 不静默回滚模型；
- 丢弃失败的局部视觉子树，按已提交根重建；
- 重建仍失败则显示该宿主的降级占位，并保留明确的重试入口；
- 只有提交并成功应用的健康状态才允许以后持久化（持久化仍不在本切片）；
- 一个宿主降级不影响其他宿主。主根、各浮根分别记录健康状态和最后成功摘要。

窗口 X 回停时，模型提交成功即允许关闭浮宿主；主根投影即使随后降级，也不阻止
关窗，主 Shell 显示重试提示。模型未提交时失败必须取消关窗并保持窗口存在。

### D6-A-6 提交、摘要、代次与拖放令牌

- `DockWorkspace.Revision` 从 0 开始，每次成功工作区提交恰好 +1；失败、无变化、
  被拒绝的操作不改变 Revision。
- 根摘要继续使用现有 `DockTree.Digest`，只覆盖该根的结构/面板/活动标签/权重。
- 工作区摘要是诊断和未来持久化的比较点：
  `mainDigest + 按 FloatRootId 排序的 (rootId, rootDigest, bounds, maximized,
  returnTarget)`。`UpdateFloatBounds` 改变工作区摘要，但不改变任何根摘要。
- **代次是宿主级的**：每个根宿主拥有自己的
  `DockApplyToken(Generation, RootDigest)`，令牌只在产生它的宿主内比较，绝不跨
  宿主比较。跨根操作同时更新源根和目标根；两边的投影器各自推进代次。未受影响的
  宿主保持原令牌，不因其他窗口重建而失效。
- 跨窗口拖放会话由工作区级 Coordinator 持有，不由单个投影器持有。释放时校验：
  源宿主令牌、源根摘要、已有目标宿主的令牌与摘要、目标组仍存在。任一项过期或被
  重建则拒绝，模型不变。
- 新浮动根没有既有目标宿主，只需校验源令牌和候选目标；宿主预检按 D6-A-5 执行。
- `UpdateFloatBounds` 不重建投影、不刷新令牌；只更新窗口几何和模型摘要。
- 每个宿主独立降级/重试；`Retry` 只重建该宿主根，不替换其他宿主，不改变模型
  或 Revision。降级或重试中的宿主不接受移动/激活/缩放请求；恢复后重新取得当前
  令牌。

### D6-A-7 操作、目标与原子性

`DockTarget` 在现有 `Center` / `Edge` / `Invalid` 之外增加：

```csharp
Float(DockBounds Bounds)   // 创建新浮动根；D6-B 仅允许从主根浮动
```

投放已有浮动窗口不需要新的目标类型：节点/组 ID 已全局唯一，`Center` / `Edge`
可以直接指向浮根中的组。

建议的工作区操作表：

| 操作 | 前置 | 成功结果 | 主要失败 |
| --- | --- | --- | --- |
| `FloatPanel(instance, bounds)` | 实例在主根；主根不会变空；浮动根 < 16 | 从主根移除，建立单面板浮根，记录 `ReturnTarget` | InvalidSource、WouldEmptyMainRoot、LimitExceeded、InvalidModel |
| `DockPanel(instance, target)` | 实例在浮根；目标在主根或另一浮根 | 跨根移动；空浮根移除 | InvalidSource、TargetGone、WouldEmptyMainRoot、InvalidModel |
| `MovePanel(instance, target)` | 实例在任一主根/浮根 | 同根委托 `DockTree.MovePanel`；跨根执行原子转移 | 同上 |
| `MergeFloatRoot(rootId, target)` | 浮根存在；目标在主根 | 全部面板按顺序追加，移除浮根 | TargetGone、InvalidTarget、InvalidModel |
| `CloseFloatRoot(rootId)` | 浮根存在；调用方已确认全部面板可关闭 | 移除浮根及全部面板；主根不变 | TargetGone、InvalidModel |
| `UpdateFloatBounds(rootId, bounds, maximized)` | 浮根存在；边界合法 | 只更新边界/最大化 | TargetGone、InvalidModel、AlreadyInState |
| `RemovePanel(instance)` | 实例在任一树 | 从所在根移除；空浮根移除；主根不得变空 | InvalidSource、WouldEmptyMainRoot |
| `ActivateTab` / `ResizeSplit` | 节点在任一树 | 委托对应根树操作 | 既有单根错误 |
| `RestoreDefault()` | 默认模板有效 | 主根替换为默认，移除全部浮根（先确认） | InvalidModel |

- 跨根移动先构造候选主根和候选浮根，分别规范化/校验，再执行工作区全局校验，
  最后一次性替换。任何一步失败都返回失败，当前工作区不变。规范化后为空的浮根
  直接从候选工作区移除；主根规范化后为空则整次操作失败；浮根列表按根 ID 升序
  重排。工作区规范化必须是确定性、幂等的，且不改变有效面板集合。
- `FloatPanel` 的 `ReturnTarget` 取浮动前源组 ID 和下标。源组因移走最后一项而
  消失时，回停按 D6-A-4 的兜底规则选择主根第一个组。
- `UpdateFloatBounds` 只接受合法有限值：`x/y ∈ [-32768, 32768]`，
  `width/height ∈ (0, 32768]`。模型不根据显示器静默夹取；适配层在调用模型前
  完成工作区约束。`NaN`、`Infinity`、零和负尺寸拒绝。
- 上限：工作区浮动根 ≤ 16，总节点 ≤ 512，总面板 ≤ 256，单根深度 ≤ 64。
- `RestoreDefault`、`CloseFloatRoot` 等破坏性操作必须先由调用方确认；模型不读取
  面板描述符或脏状态，确认属于 Coordinator/UI 职责。

### D6-A-8 视图复用与恢复的边界

- 跨根移动复用同一 `IDockPanelView` 和同一 `Control`，不增加面板工厂调用；
  只有注册表可以创建或释放视图。
- 宿主重建只重新挂载已注册视图；失败面板保留占位，不创建第二个实例。
- 浮根回停主根、浮根合并、跨根拖动都不得改变面板实例身份；标题由注册表维护，
  不作为身份。
- 显式关闭面板或关闭浮根时，先提交模型，再释放视图；释放失败不回滚模型。
- 工作区 Dispose 释放全部视图一次，关闭全部宿主；主窗口关闭期间的浮窗口 X
  不触发回停事务。

### D6-A-9 浮动触发与跨窗口拖放的分阶段策略

- D6-B 只实现模型操作；D6-C 只提供显式“Float Panel”命令（菜单/快捷键）和
  显式“回停/合并”命令，不改变 D5 的视觉行为：拖出主工作区后释放仍然取消。
- D6-D 再增加跨窗口拖放与 tear-off。建议默认仍保持“拖出窗口释放 = 取消”，
  只有按住明确修饰键（或从专用拖动把手）才创建新浮动根；该策略需在 D6-D
  单独冻结并用真实桌面冒烟验证，避免把 D5 已验收的取消语义静默改掉。
- 跨窗口拖放会话由 Coordinator 持有，预览仍是覆盖层/目标高亮，不修改模型；
  释放只提交一次跨根操作。源浮根变空、目标宿主关闭、任一代次过期都必须取消
  或拒绝，不得留下部分布局。
- 目标命中继续使用稳定组/节点 ID，不使用窗口标题；窗口边界只用于“是否离开
  工作区”和“新浮根 bounds”的计算。

## 建议接口形态（命名可在实现时调整）

```csharp
public enum DockWorkspaceError
{
    None, InvalidSource, InvalidTarget, DuplicatePanel,
    TargetGone, AlreadyInState, InvalidModel,
    WouldEmptyMainRoot, LimitExceeded,
}

public readonly record struct DockWorkspaceResult(
    bool Success, DockWorkspace? Workspace,
    DockWorkspaceError Error, string Diagnostic);

public enum DockHostError
{
    None, CreationFailed, ProjectionFailed, ShowFailed, ClosingCanceled,
}

public readonly record struct DockHostResult(
    bool Success, DockHostError Error, string Diagnostic);
```

- `DockWorkspaceResult` 只表达模型事务；宿主预检/显示失败用独立的
  `DockHostResult`，不得把 OS 失败伪装成模型错误。
- `DockFloatRootId` 与 `DockNodeId` 分开；`DockBounds` 是 DIP 数值记录。
- Coordinator 维护 `Dictionary<FloatRootId, IDockRootHost>`、工作区级拖放会话和
  shutting-down 标志；模型层完全不知道窗口存在。

## 门禁计划（D6-B/C/D 实现时）

**D6-B 纯模型门禁**

| 场景 | 期望 |
| --- | --- |
| 工作区创建/校验 | 主根 + 浮根全局节点/面板 ID 唯一；空浮根、重复 ID、超限拒绝 |
| FloatPanel | 主根移除、单面板浮根、ReturnTarget 正确；最后一个主根面板拒绝 |
| DockPanel/MovePanel 跨根往返 | 面板集合不变、空浮根移除、结构有效（WM-16） |
| MergeFloatRoot | 全部面板按序追加、目标活动保持、浮根移除；目标无效时两边不变（WM-19） |
| UpdateFloatBounds | 合法更新；非法/无变化拒绝；根摘要不变、工作区摘要/Revision 正确（WM-20） |
| CloseFloatRoot | 浮根及面板移除，主根面板集合不变（WM-21） |
| 失败原子性 | 所有失败路径 Revision 和工作区逐字段不变（WM-18） |
| 确定性 | 相同操作序列产生相同节点 ID、根顺序和工作区摘要 |

**D6-C 宿主门禁（假宿主/真实投影器，无真实窗口）**

| 场景 | 期望 |
| --- | --- |
| 预检创建失败 | 无模型提交、无残留宿主 |
| 预检投影失败 | 宿主释放，模型和 Revision 不变 |
| 提交后 Show 失败 | 模型保留、宿主 degraded、主 Shell 出现回停入口 |
| 窗口 X 回停 | 成功提交后关闭；失败取消关窗并保留面板 |
| 显式关闭 | 最后一个浮面板关闭会移除浮根/宿主；最后一个主面板拒绝 |
| 视图所有权 | 跨根移动复用同一视图，工厂只调用一次，Dispose 恰好一次 |
| 逐宿主恢复 | 一个宿主降级不影响其他宿主；Retry 只重建该根 |
| 令牌隔离 | 其他根重建不使本根令牌过期；旧浮宿主令牌被拒绝 |
| Bounds | 更新不重建投影、不推进宿主代次 |
| 关闭顺序 | 主窗口关闭期间浮窗口不触发回停；注册表只 Dispose 一次 |

**D6-D 真实桌面冒烟**

- 创建真实浮窗、显示、移动/调整 bounds、X 回停、显式关闭浮根；
- 主→浮、浮→主拖动，预览不提交，捕获/取消/完成；
- 拖出窗口默认取消与修饰键 tear-off 的区分；
- 单显示器 100% DPI 为必测；多显示器/混合 DPI 只记录实际结果，不宣称完整覆盖。

## 已确认项（D6-A 审查通过）

O-1 至 O-6 已全部确认，按“建议”列执行；D6-B 严格按这些选择实现。

| 编号 | 选择 | 结论 | 影响 |
| --- | --- | --- | --- |
| O-1 | 浮窗 X 语义 | 回停主根 | 不丢面板、不引入隐藏面板；回停不恢复原 Split 结构 |
| O-2 | 关闭最后一个主根面板 | 拒绝 | 保持 MainRoot 非空；空工作区状态另立切片 |
| O-3 | 视图所有权 | 工作区注册表 | 需要重构当前投影器的视图所有权（D6-C） |
| O-4 | 代次作用域 | 宿主级 | 未受影响窗口不因其他窗口重建而失效（D6-C） |
| O-5 | 浮动触发 | 先显式命令，D6-D 再加修饰键 tear-off | D5 的拖出取消语义在 D6-C 前保持不变 |
| O-6 | Bounds 约束 | 模型只校验数值，适配层先夹取 | 模型不静默改值；混合 DPI 留到 D6-D 记录 |

## 后果与风险

- 需要把当前 `DockWorkspaceProjector` 的视图所有权拆到工作区注册表，并把单根
  工作区控制器升级为多根 Coordinator；这是 D6-C 的主要改动面。
- D6-B 已为 `DockTree` 增加根命名空间，主根既有 ID 和摘要保持不变；工作区
  全局唯一性和上限由工作区校验器负责，单根校验语义不变。
- 多窗口会带来焦点、Owner、关闭顺序和 UI 线程问题；D6-C 用假宿主先验证，
  D6-D 再验证真实窗口。
- 不引入持久化前，浮动根只存在于当前会话；重启不恢复，Reset Layout 会移除全部
  浮根。D6 不会宣称可恢复浮动布局。
- 回停只保证面板不丢，不保证恢复浮动前的精确 Split 结构和权重；用户可通过
  正常停靠操作重新整理。

## D6-B 实施记录（纯模型）

### 已实现

- `DockBounds`：有限数值校验，`X/Y ∈ [-32768, 32768]`，
  `Width/Height ∈ (0, 32768]`；只校验，不夹取，不查询显示器。
- `DockFloatRoot` / `DockFloatRootId` / `DockReturnTarget`：独立身份，绑定
  单根 `DockTree`、bounds、maximized 和回停提示；根树节点前缀重绑定为浮根 ID。
- `DockWorkspace`：不可变工作区，持有 `MainRoot`、按 ID 升序的 `FloatingRoots`、
  `Revision` 和 `Digest`。操作全部返回 `DockWorkspaceResult`，成功返回新工作区，
  失败返回空工作区且接收者逐字段不变。
- 工作区操作：`FloatPanel`、`MovePanel`、`ReDockFloatRoot`、`MergeFloatRoot`、
  `CloseFloatRoot`、`UpdateFloatBounds`、`RemovePanel`、`ActivateTab`、
  `ResizeSplit`、`RestoreDefault`。
- 全局身份与上限：主根 + 全部浮根的 `DockNodeId`、`PanelInstanceId`、
  `DockFloatRootId` 全局唯一；浮动根 ≤ 16、总节点 ≤ 512、总面板 ≤ 256、
  单根深度 ≤ 64。空浮根自动移除，空主根拒绝。
- `DockTree` 根命名空间：主根保持 `dock` 前缀，既有节点 ID 和摘要不变；浮根使用
  `dock.float.<N>...`，同一操作序列产生相同 ID。
- 内部 `DockTree.AppendPanels`：合并时按序追加且保持目标组活动标签，供
  `MergeFloatRoot` 使用。
- `DockTarget.Float(DockBounds)`：D6-B 只允许从主根浮动；对已在浮根中的面板返回
  `InvalidSource`。
- 确定性回停：优先 `ReturnTarget.GroupId`，组不存在时取主根先序遍历第一个标签组；
  浮动根内按先序遍历、组内保序追加；目标活动标签保持；`ReturnTarget.Index`
  只作为提示，合并忽略索引。合并失败时工作区和浮根保持不变。
- 主根非空：浮动/跨根移走/删除最后一个主根面板返回 `WouldEmptyMainRoot`；
  关闭浮根最后一个面板允许提交，前提是同一事务移除该浮根并满足全局不变量。
- 原子提交：跨根操作先构造候选根，逐根规范化/校验，再做工作区全局校验，最后
  一次性替换。任一失败不产生部分更新、重复 ID 或丢失面板。
- 提交计数：成功模型提交 `Revision` 恰好 +1；失败与无变化不推进。
  `UpdateFloatBounds` 推进 Revision 并改变 `WorkspaceDigest`，不改变任何根摘要。

### 摘要与代次的分层（D6-B 边界）

D6-B 只实现并验证三层中的前两层：

| 层 | D6-B 状态 | 说明 |
| --- | --- | --- |
| `DockTree.Digest` | 已实现 | 单根结构/面板/活动标签/权重；bounds 更新不变 |
| `DockWorkspace.Digest` | 已实现 | 主根摘要 + 浮根 ID/摘要/bounds/maximized/return target；排除 Revision |
| 宿主 `Generation` / `DockApplyToken` | 已实现（D6-C） | 宿主身份 + 代次 + 根摘要；令牌只在所属宿主内有效 |

D6-C 已用真实投影器 + 假宿主门禁确认：`UpdateFloatBounds` 提交模型、推进 `Revision`
并改变 `WorkspaceDigest`，但不调用宿主 `Apply`、不推进宿主 `Generation`、不刷新令牌。

### 未实现（D6-B 明确排除）

- `DockPanelViewRegistry`、投影器视图所有权重构、Prepare/Commit/Show 宿主生命周期；
- 宿主代次/令牌、跨窗口拖放会话、X 回停的宿主侧取消与降级 UI；
- 真实 Avalonia 浮动窗口、跨窗口拖动/tear-off、显式 Float Panel 命令；
- Dock 持久化、隐藏/未解析面板、布局 Undo、多显示器和混合 DPI。

这些留给 D6-C（假宿主/真实投影器）和 D6-D（真实桌面），不得视为 D6-B 已交付。

### D6-B 门禁与回归结果

| 门禁 | 结果 |
| --- | --- |
| 新增工作区测试 | 21 项，覆盖创建/校验、Float、跨根移动、回停/合并、bounds、关闭、失败原子性、全局身份/上限、Revision 与确定性 |
| `Nova.UI.Core.Tests` Debug | 70/70 通过（D5 时 49） |
| `Nova.sln` Debug | 168/168 通过（Core 70、Editor.Integration 75、UI.Controls 18、Editor.Performance 5） |
| `Nova.sln` Release | 168/168 通过，分布相同 |
| 构建 | Debug/Release 均 0 错误；每配置 18 次 NU1900 记录（9 个项目 × restore/build 两阶段，MSBuild 汇总 9 条警告），来自不可达的 Azure China 漏洞源，不称为零警告构建 |
| 格式 | `dotnet format Nova.sln --verify-no-changes --no-restore` 退出码 0（仅工作区加载警告） |

D6-B 未修改 `editor_avalonia`、`native/gate` 或其他无关 dirty 文件；工作树保持
未提交，未执行 commit、push、tag 或 merge。

## D6-C 实施记录（宿主抽象与故障恢复）

### 已实现

- `DockRootHostId` / `DockRootHostDescriptor` / `DockRootHostFactory` /
  `IDockRootHost`：主根宿主固定为 `main`，浮根宿主为 `float:{floatRootId}`；
  宿主提供 `Prepare`、`Apply`、`Show`、`Retry`、`Close`、`IsVisible`、
  `IsHealthy`、`Current`、`TryGetCurrentToken`、`FindNode`、`FindPanel`。
  D6-C 没有真实 OS 窗口，`IsVisible` 是逻辑宿主状态，不是窗口可见性。
- `DockPanelViewRegistry`：工作区级唯一视图所有者。每个面板只有一个
  `ContentControl Host`；`GetOrCreate` 复用现有条目，`Retry` 只重建失败面板，
  `Release`/`Dispose` 负责最终释放。工厂失败转为 `DockPanelFailure` 占位并保留
  面板身份；视图 `Dispose` 抛出的异常被记录为 `ReleaseFailureCount` /
  `LastDiagnostic`，不会中断其他面板释放或外泄到模型事务。
- `DockProjectorRootHost`：基于真实 `DockWorkspaceProjector` 的宿主实现。
  `Prepare` 校验候选根；`Apply` 投射并把 `Degraded` 映射为
  `ProjectionFailed`；`Retry` 重建已提交根；注册表模式下投影器只挂载/分离
  共享 Host，不释放视图。
- `DockWorkspaceCoordinator`：工作区级协调器，每个根一个宿主。构造时主宿主
  依次 `Prepare` → `Apply` → `Show`。`FloatPanel` 的顺序为：模型候选 →
  创建宿主 → `Prepare` → 提交模型 → 应用主根 → 应用浮根 → `Show`。
  跨根移动先提交模型，再关闭/销毁被移除的浮宿主（不释放已移动视图），
  最后按“源宿主、目标宿主”顺序应用存活宿主，避免共享 Host 同时挂载到两个父级。
  D6-C 的真实投影宿主 `Prepare` 是候选根预检；每个面板只有一个注册表拥有的
  `ContentControl Host`，共享 Host 的视觉挂载必须等源宿主分离后执行，因此
  D6-C 不在 `Prepare` 阶段预挂载候选视图。预检失败在提交前处理，视觉应用
  失败按提交后降级处理。
- `DockApplyToken` 增加 `HostId`；`DockWorkspaceProjector` 增加注册表构造与
  宿主身份。旧的本地面板工厂模式保留原语义，既有投影门禁不回归。
- 系统关闭与显式关闭分离：浮窗 X 走 `ReDockFloatRoot`/`MergeFloatRoot`。
  实际顺序为：模型候选 → 目标主宿主 `Prepare` → 提交模型 → 关闭/销毁浮宿主 →
  主宿主 `Apply` → 主宿主不可见时 `Show`。模型失败或目标预检失败时不提交、
  不关闭浮宿主；关窗请求返回 `ClosingCanceled`，显式合并返回底层宿主错误。
  提交后主根 `Apply`/`Show` 失败不回滚，主 Shell 降级并可 `Retry(main)` 恢复。
  显式关闭提交模型后关闭宿主并释放对应视图，不回停主根。`BeginShutdown`
  只关闭浮宿主，不修改工作区，shutdown 期间的关闭请求返回 `ShuttingDown`，
  避免把应用退出误判为回停。
- 故障语义：新建浮宿主 `Prepare` 失败时模型、主宿主、现有视图均不变，
  新宿主被销毁且不注册；系统关闭的目标主宿主 `Prepare` 失败时模型、浮宿主
  和视图均不变，浮宿主不 `Close`/`Dispose`；宿主创建异常/非法身份返回
  `CreationFailed`；`Show` 失败时模型已提交、宿主显式降级且可回停恢复；
  合并后主根 `Apply`/`Show` 失败仍保留已提交模型，可通过 `Retry(main)` 恢复；
  关闭/视图释放失败返回 `CloseFailed`，但 `ModelCommitted` 为 true，模型不回滚。
- 令牌隔离：`ValidateCallback` 同时校验 `token.HostId`、宿主当前令牌、
  `Generation`、根 `Digest` 和节点归属宿主；过期、跨宿主或外来节点令牌均返回
  `StaleToken`，工作区不变。
- `UpdateFloatBounds` 只提交模型：`Revision` +1、工作区摘要变化，但不调用
  宿主 `Apply`、不推进宿主代次、不刷新令牌、不重建节点。
- 最后主根面板保护：`ClosePanel`/浮动/跨根移动均遵守模型
  `WouldEmptyMainRoot`，协调器不绕过，也不触碰宿主或视图。
- `Retry(hostId)` 只重试指定宿主；重试成功后若宿主仍不可见（例如创建时
  `Show` 失败），协调器补一次 `Show`，保证降级宿主有真实的恢复路径。

### D6-C 事务顺序核对

D6-A-5 冻结的三阶段是 `Prepare` → `Commit` → `Show`。D6-C 的真实投影宿主
`Prepare` 是候选根预检而非预挂载共享视图（原因见上），因此核对重点是
“预检失败必须在提交前返回，提交后视觉失败不得回滚”。以下门禁全部在 Debug
和 Release 下通过：

| 场景 | 实际顺序与断言 | 门禁 |
| --- | --- | --- |
| 新建浮宿主 Prepare 失败 | 模型候选 → 创建宿主 → `Prepare` → 失败即销毁新宿主并返回，不提交。工作区引用、`Revision`、摘要、主/浮根、主宿主令牌、已注册视图及其挂载均不变，且没有残留的宿主注册。 | `Prepare_failure_leaves_the_model_and_existing_hosts_unchanged` |
| 系统关闭目标主宿主 Prepare 失败 | 合并候选 → 目标主宿主 `Prepare` → 失败即返回，不提交、不关闭浮宿主。`ModelCommitted=false`；关窗返回 `ClosingCanceled`，显式合并返回 `ProjectionFailed`；`Revision`、浮根、浮宿主 `Close`/`Dispose`、面板视图均不变。故障清除后同一浮根可正常回停。 | `Target_prepare_failure_cancels_system_close_without_touching_the_float_host` |
| 提交后目标 Apply/Show 失败 | 提交 → 关闭/销毁浮宿主 → 主宿主 `Apply`（不可见时再 `Show`）。失败不回滚，`ModelCommitted=true` 且主宿主显式降级；面板仍由注册表持有、视图未释放；`Retry(main)` 从权威工作区重建并恢复。`Show` 失败保持不可见/不健康，不报告健康状态。 | `Host_failure_after_system_close_keeps_the_model_and_recovers_with_retry`；`Target_show_failure_after_system_close_is_degraded_and_retry_recovers`；浮宿主 Show 失败：`Show_failure_commits_the_model_and_redocking_recovers_the_view` |
| 模型失败（前置） | 合并模型事务失败时不进入目标预检、不提交、不关闭宿主；浮宿主 `Close`/`Dispose` 只在提交成功后发生。 | `Model_failure_cancels_close_requests_and_hosts_are_destroyed_only_after_commit` |

### 明确未实现（D6-C 排除）

- 真实 OS 顶层浮动窗口、窗口所有权/焦点/Z 序、跨窗口拖放、修饰键 tear-off；
- Dock 持久化、隐藏/未解析面板、布局 Undo、多显示器和混合 DPI；
- 真实窗口创建/显示失败路径（D6-C 通过测试内假宿主注入宿主级故障）。

### D6-C 门禁与回归结果

| 门禁 | 结果 |
| --- | --- |
| 新增 Coordinator 测试 | 21 项：主根投射与宿主令牌、浮动视图复用、系统关闭与回停、跨浮根移动、主根重建下的浮宿主令牌隔离、新建浮宿主 Prepare/创建/Show 失败、目标主宿主 Prepare/Show 失败、关闭取消与销毁顺序、显式关闭、最后主根面板保护、bounds、Retry 隔离、恢复失败、过期/跨宿主/外来节点令牌、合并后宿主失败恢复、shutdown、工厂失败与注册表重试、关闭/释放失败 |
| `Nova.UI.Controls.Tests` Debug | 39/39 通过（D6-B 时 18） |
| `Nova.UI.Core.Tests` Debug | 70/70 通过 |
| `Nova.sln` Debug | 189/189 通过（Core 70、UI.Controls 39、Editor.Integration 75、Editor.Performance 5） |
| `Nova.sln` Release | 189/189 通过，分布相同 |
| 构建 | Debug/Release 均 0 错误；每配置 18 次 NU1900 记录（9 个项目 × restore/build 两阶段，MSBuild 汇总 9 条警告），来自不可达的 Azure China 漏洞源，不称为零警告构建 |
| 格式 | `dotnet format Nova.sln --verify-no-changes --no-restore` 退出码 0（仅工作区加载警告） |

D6-C 未启动真实浮动窗口，未修改 `editor_avalonia`、`native/gate` 或其他无关
dirty 文件；工作树保持未提交，未执行 commit、push、tag 或 merge。

## D6-D 契约冻结（真实 OS 窗口与跨窗口交互）

D6-D 在 D6-C 的 `Prepare → Commit → Apply → Show` 顺序和失败恢复语义之上，
只增加真实窗口生命周期、跨窗口拖放与修饰键 tear-off。Dock 模型和 D6-C
恢复契约保持冻结；不引入持久化、隐藏/未解析面板或布局 Undo。

### D6-D-1 真实浮窗宿主

- `IDockFloatWindow` 是宿主与 OS 窗口之间的适配层；生产实现
  `AvaloniaFloatWindow` 包裹真实 `Window`，测试使用同契约的假窗口驱动同一
  `DockWindowRootHost` 逻辑。
- 浮窗在宿主创建时以隐藏状态构造；`Prepare` 仍只做候选根校验，不在提交前
  把注册表共享视图挂到新窗口。`Apply` 在模型提交后挂载视图，`Show` 显示窗口。
  预检失败不提交模型；提交后 `Show` 失败保留已提交浮根，宿主标记为不健康，
  主 Shell 必须提供可见的 Retry / 回停入口。
- 浮窗由主窗口 `Owner` 拥有，不进入任务栏；浮窗不参与布局保存。
- 用户点击窗口 X 时，`Closing` 首先取消本次系统关闭，再由工作区调用
  `RequestCloseFloatRoot`。模型/目标预检成功提交后宿主关闭；失败时窗口保持
  可见，关闭被取消。关闭请求处理中重复点击 X 不产生第二次事务。
- 用户移动/缩放/最大化浮窗触发模型 `UpdateFloatBounds`；只更新 bounds、
  `Revision` 和 `WorkspaceDigest`，不调用宿主 `Apply`、不推进宿主代次、
  不刷新令牌。宿主自身设置初始几何时抑制回写事件。
- 主窗口退出时先调用 `BeginShutdown`：浮窗关闭但不触发回停事务，工作区模型
  不再提交；注册表只在工作区 `Dispose` 时释放一次。

### D6-D-2 跨窗口拖放

- 跨窗口拖放会话由 `DockWorkspaceCoordinator` 持有，不由单个投影器持有。
  会话记录：源宿主、源面板、源宿主令牌（含 `HostId`/`Generation`/摘要）、
  源组 ID、目标宿主及其首次命中时的令牌快照、目标组 ID。
- 拖动期间只绘制源幽灵和目标宿主预览；不修改模型、不推进代次、不调用面板
  工厂。源宿主本地命中继续沿用 D5 的 `DockDragController` 几何逻辑；源宿主
  之外的命中通过各宿主把屏幕坐标转换到自身投影面后解析，源宿主不参与外部
  命中。目标宿主按稳定组/节点 ID 解析目标。
- 释放时在提交前重新校验：源宿主存在且健康、源令牌与当前令牌一致、面板仍在
  源宿主、目标宿主存在且健康、目标宿主令牌与命中时快照一致、目标组仍存在。
  任一失败返回 `StaleToken` / `HostUnavailable` / 模型目标错误，工作区不变。
- 校验通过后只调用一次 `DockWorkspace.MovePanel`；跨根移动复用同一
  `IDockPanelView` 和同一 `Control`，工厂调用数不变，注册表不释放视图。
  提交后按源宿主、目标宿主顺序应用存活宿主；被移空的浮根关闭并销毁宿主。
- 捕获丢失、Escape、投影重建、宿主降级、工作区关机都取消会话且不提交。
- D5 行为保持不变：无修饰键时拖出所有宿主后释放仍是取消；D6-D 只新增
  “命中另一宿主”与“显式 tear-off”两条路径。

### D6-D-3 修饰键 tear-off

- 触发条件同时满足才创建新浮根：
  1. 已建立标签拖动会话并越过 4px 阈值；
  2. 释放点位于源顶层窗口之外；
  3. 释放点没有命中任何宿主（含源宿主）的合法停靠目标；
  4. 释放时按住 `Ctrl`。
- 命中合法停靠目标时目标优先，`Ctrl` 不改变停靠语义。无修饰键、窗口内空区
  释放、捕获丢失、Escape 仍按 D5 取消。
- tear-off 只允许主根面板（D6-B 模型限制）；浮根面板 tear-off 返回
  `InvalidSource`，不创建新窗口。一次 tear-off 只创建一个单面板浮根，
  bounds 为释放点屏幕位置换算到 DIP、默认 640×480；适配层在调用模型前把
  位置夹取到模型允许范围，尺寸保持正值。
- tear-off 与显式 Float 命令共用 `FloatPanel` 宿主创建/失败恢复路径。

### D6-D-4 门禁与真实桌面边界

- Headless 门禁使用同契约假浮窗驱动真实 `DockWindowRootHost` 和协调器，
  覆盖创建/显示/X 回停/关机/失败恢复、跨窗口令牌隔离、视图复用与 tear-off
  策略。
- 真实桌面门禁在 Windows 交互会话运行 `Nova.Editor.App`，用系统输入验证：
  浮窗显示、移动、X 回停、主↔浮拖动、Ctrl tear-off、应用关机。
- 单显示器 100% DPI 为必测；多显示器、混合 DPI 和跨 DPI 坐标只记录实际
  结果，不从单显示器或 headless 结果推定完整覆盖。

**D6-D 明确排除**：Dock 持久化/恢复、隐藏或未解析面板、布局 Undo、浮窗
布局保存、跨进程窗口、同一悬浮根拆成多个窗口、任意嵌套浮根、浮动窗口
多显示器 DPI 完整保真。

## D6-D 实施记录（真实窗口、跨窗口会话与桌面证据）

### 生产缺陷与修复

真实桌面冒烟发现：从主窗口发起的跨窗口拖动从不建立协调器会话。原因是
`DockProjectorRootHost.DragEvent` 直接把订阅转发给内部投影器，协调器收到的
`sender` 是 `DockWorkspaceProjector`，无法通过 `IDockDragSourceHost` 判定，
`OnHostDragEvent` 在入口返回；本地拖动仍经 `PanelMoveRequested` 提交，因此
既有门禁未暴露。浮窗宿主一直以自身作为 `sender` 转发，所以浮窗→主窗口可用。
修复：主宿主持有自己的 `DragEvent`，构造函数把投影器事件桥接为以宿主为
`sender` 的事件，与 `DockWindowRootHost` 的契约一致。

新增真实调用链门禁
`DockDragTests.Main_window_drag_opens_a_coordinator_session_for_cross_window_moves`：
使用 `EditorDockWorkspace`、真实投影器和真实指针事件，断言越过阈值后
`IsDragging` 为真。已确认该门禁在未修复的转发实现上失败、在修复后通过；
未引入任何仅测试可见的入口。

### 自动化门禁结果

- Debug：Core 70、Controls 55、Integration 76、Perf 5，共 206 通过，0 失败。
- Release：同一组 206 通过，0 失败。
- 两种配置均为 0 错误、每配置 18 条 NU1900（来自不可达的 Azure China
  漏洞源），不称为零警告构建。
- `dotnet format Nova.sln --verify-no-changes --no-restore` 退出码 0。

### 真实桌面冒烟（实际环境：单显示器、96 DPI、Windows 交互会话）

- 通过标签上下文命令浮动 Scene，窗口位于 (680,404)、外框 656×519、客户区
  640×480；`SetWindowPos` 移至 (1300,620) 后宿主仍健康，无恢复横幅。
- 主窗口→浮窗：把 Console 拖到浮窗中部，浮窗显示目标预览，释放后 Console
  进入浮窗，浮窗保留，Console 内容（日志行）原样保留。
- 浮窗→主窗口：把 Console 拖回 Inspector 组，主窗口提交，浮窗保留 Scene，
  Console 内容仍为同一视图。
- 多面板浮窗（Scene+Console）通过窗口 X（`WM_CLOSE`）回停：两个面板回到主
  窗口同一组，Console 内容未丢失；浮窗销毁，主窗口正常。
- D5 语义保持：无 Ctrl 把 Console 拖到窗口外释放，不创建浮窗，布局与激活
  面板不变。
- Ctrl tear-off：把 Console 拖到屏幕 (300,300) 释放并按住 Ctrl，创建外框
  (300,300) 656×519（客户区 640×480）的浮窗，内容保留；主窗口只剩
  Hierarchy/Scene/Inspector。
- 浮窗存在时关闭主窗口：应用退出，浮窗一并关闭，stderr 为空。
- 未覆盖：多显示器、混合 DPI、跨 DPI 坐标保真；异常宿主恢复未在桌面强制
  注入（由 headless 门禁覆盖）；浮窗创建/显示失败的桌面强制路径未覆盖。

工作树保持未提交；未修改 `editor_avalonia`、`native/gate` 或其他无关 dirty
文件；未执行 commit、push、tag 或 merge。

## 变更边界

D6-A 只冻结契约；D6-B 仅新增/修改 `Nova.UI.Core` 纯模型与
`Nova.UI.Core.Tests`；D6-C 仅新增/修改 `Nova.UI.Avalonia` 的宿主契约、
视图注册表、投影器注册表模式与协调器，以及 `Nova.UI.Controls.Tests` 的
协调器门禁。D6-D 新增 `Nova.UI.Avalonia` 的真实窗口宿主/跨窗口会话、
`Nova.Editor.Views` 与 `Nova.Editor.App` 的浮窗接入及其门禁。四者均不修改
`editor_avalonia`、`native/gate` 或其他无关 dirty 文件。工作区保持未提交；
不执行 commit、push、tag 或 merge。
