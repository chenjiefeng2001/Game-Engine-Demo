# ADR-009: 隐藏／未解析面板生命周期契约（D8-A）

- 状态：Accepted 并**已关闭**（D8-A 契约冻结、O-1 至 O-9 全部接受；D8-B、D8-C、
  D8-D、D8-E 与状态栏保存状态可见性修复全部通过用户验收；用户于 2026-10-26
  裁决"D8 关闭"，本阶段范围内无需继续阻止阶段结束的缺口）
- 日期：2026-10-26
- 关联：ADR-003（布局持久化）、ADR-005（DockTree 模型）、ADR-006（Avalonia 投影）、
  ADR-007（多根工作区与浮动宿主）、ADR-008（多根工作区持久化）
- 范围：D8 只处理**面板身份与生命周期**。D8-A 冻结契约；D8-B 实现纯模型与门禁；
  D8-C 实现 schema v3 序列化与持久化抑制；D8-D 实现视图所有权、协调器与 Shell
  接线；D8-E 补齐故障门禁、真实进程证据与记录。布局 Undo 不在本阶段，仍为后续
  独立切片

## 背景与基线核对

D7 已完成并通过验收：多根工作区、浮动宿主、跨进程持久化与恢复、保存串行化、
启动读取失败保护均已有自动化与真实进程证据。当前代码的面板生命周期只有一种
“存在于某个标签组”的状态：

| 现状 | 位置 | 对 D8 的影响 |
| --- | --- | --- |
| 面板身份是稳定 `PanelInstanceId`，内置四个：`editor.hierarchy/scene/inspector/console` | `DockPanelIds` | 未解析面板就是“身份不是这四个之一”的面板 |
| 面板只存在于 `DockTabGroupNode.Panels`，顺序即模型顺序，`ActivePanel` 必须属于 `Panels` | `DockNode.cs` / `DockValidator.cs` | 隐藏若另开旁路集合，会丢失顺序与组归属；需要模型内表示 |
| 空标签组非法；主根面板全部移除会被拒绝 | `DockValidator` / `DockWorkspace.RemovePanel` | 隐藏必须保留“每组至少一个可见面板”，否则投影无内容 |
| 关闭 = 从树中移除并释放视图；没有隐藏、没有 Undo | `DockWorkspace.RemovePanel` / `DockWorkspaceCoordinator.ClosePanel` | “关闭”已是破坏性语义，D8 不改其含义 |
| 未知面板让整个 Dock 回退默认；Shell 字段保留；不隔离；自动保存与退出快照被抑制，直到显式保存 | `WorkspaceLayoutSerializer` / `LayoutPersistenceService` / `EditorWorkspace` | 既有数据保护规则必须保留，不能被静默映射或自动覆盖 |
| 序列化器拒绝含未知面板的快照（`DockModelException`） | `WorkspaceLayoutSerializer.Serialize` | D8 需要让显式保存能忠实写回未解析身份 |
| 视图注册表对每个挂载面板调用工厂并创建一一对应的视图；未知面板会创建失败占位 | `DockPanelViewRegistry` / `EditorDockPanelFactory` | 隐藏面板不应再持有视图；未解析面板不能调用工厂 |
| 布局 schema v2，`dock` 无隐藏字段 | `WorkspaceLayout` / `DockWorkspaceSnapshot` | 隐藏状态需要 schema v3 与 v1/v2 迁移 |

D8 的前提是：不修改 D6 的拖放、回停、宿主令牌与故障补偿语义，不修改 D7 的保存
串行化、健康门控、读取失败保护与原子写入；只扩展“面板存在但不显示”和“面板
身份存在但没有实现”两类的表示与生命周期。

## 三个概念（不可混用）

| 概念 | 定义 | 模型表示 | 视图/资源 | 持久化 | 允许的操作 |
| --- | --- | --- | --- | --- | --- |
| 已知但隐藏（hidden） | 当前版本有实现，用户明确隐藏；工作区仍拥有该面板 | 仍在所属标签组的 `Panels` 顺序中，带 hidden 标记 | 不挂载；隐藏时释放视图，显示时重新创建 | 写入 hidden 标记；重启后仍隐藏且不创建视图 | 显示、关闭、随标签组/根移动或回停；不能作为拖放源或被激活 |
| 未解析（unresolved） | 序列化身份存在，但当前版本没有可用实现 | 仍在所属标签组的 `Panels` 顺序中；是否 hidden 与解析状态正交 | 只显示“本版本不可用”占位标签；**不调用面板工厂**、不创建真实视图 | 身份、顺序、hidden 状态原样往返；自动保存仍被抑制，显式保存可忠实写回 | 激活占位、隐藏/显示、关闭、随组移动/回停；不会被映射成任何内置面板 |
| 已关闭（closed） | 用户明确关闭；工作区不再拥有 | 从 `Panels` 与 hidden 集合中移除 | 若曾挂载则释放视图 | 下一次成功保存后从文件消失 | 不可再显示；只能通过未来 Undo 恢复（本阶段无 Undo） |

两条正交轴：**解析状态**（resolved / unresolved）与**可见状态**（visible / hidden）。
因此 `unresolved + hidden` 合法：它保留在文件中、没有占位视图、也不能被显示为
真实面板。隐藏不等于关闭（身份与顺序保留），未解析不等于隐藏（它可能有可见
占位）。未知面板永远不得被静默映射为内置面板。

## 决策

### D8-A-1 模型：隐藏标记位于标签组内

`DockTabGroupNode` 增加 hidden 集合，作为 `Panels` 的**有序子集**：

- `Panels` 仍是完整、稳定的顺序权威，隐藏不改变顺序与组归属；
- `HiddenPanels` 是 `Panels` 的子集，不允许重复、不允许空 ID；
- `VisiblePanels` 为按 `Panels` 顺序过滤后的可见面板；
- `ActivePanel` 必须属于 `VisiblePanels`；
- **每个标签组必须至少有一个可见面板**；主根与每个浮根因此也至少有一个可见
  面板；
- `EnumeratePanels()` 继续返回全部面板（含隐藏）；新增
  `EnumerateVisiblePanels()`；上限计数包含隐藏面板（仍受 `MaxPanels=256`
  约束）；
- 归一化只做 hidden 顺序规范化（按 `Panels` 顺序）与权重/结构归一化，不得
  丢弃 hidden、不得把 hidden 自动提升为 visible、不得为满足可见性而静默修改
  用户状态；
- 工作区摘要升级为 **W3**：树摘要包含每个标签组的 hidden 集合，工作区摘要
  前缀改为 `W3|`。隐藏/显示必须改变 `Revision` 与摘要。W3 只覆盖持久化布局
  身份与可见性——根/节点结构、有序 `Panels`、活动面板、hidden 子集、
  `RestoreBounds`/最大化/回停目标——**不得包含 `Resolved/Unresolved` 运行时
  结果**；同一布局文件在不同面板工厂环境下必须得到相同的 W3 摘要。

新增错误码：`PanelHidden`、`WouldEmptyGroup`；映射到现有 `DockError` 时保持
“非破坏性失败、模型不变”。

### D8-A-2 模型操作语义

| 操作 | 语义 |
| --- | --- |
| `HidePanel(panel)` | 面板必须存在且当前可见；隐藏后该组必须仍有可见面板，否则失败 `WouldEmptyGroup`；若隐藏的是活动标签，活动标签按确定性规则改为同组下一个可见面板（索引更大者优先，否则取最近的前置可见面板，**不循环绕回**）；提交一次 Revision |
| `ShowPanel(panel)` | 面板必须存在且当前隐藏；仅清除 hidden 标记，不自动激活、不改变顺序；提交一次 Revision |
| `ActivateTab(group, panel)` | hidden 面板不允许激活，返回 `PanelHidden`；不改变模型 |
| 拖放源 / `MovePanel` / `FloatPanel`（源） | hidden 面板没有挂载视图，不允许作为拖放或浮动源，返回 `PanelHidden`；若移走某组最后一个可见面板会留下 hidden-only 组，返回 `WouldEmptyGroup` |
| 关闭（`RemovePanel`，含隐藏面板） | 隐藏面板可被显式关闭（仅模型变更，无视图释放）；关闭可见面板时，若该组仍有 hidden 面板且会失去最后一个可见面板，返回 `WouldEmptyGroup`；主根最后一个面板仍返回 `WouldEmptyMainRoot`；浮根最后一个面板仍按现有规则关闭浮根 |
| `MergeGroups` / 跨组移动 / 回停 / 合并浮根 | 隐藏标记随面板与所属组移动/合并；操作后必须重新满足可见性不变量，否则原子失败、模型不变 |

`WouldEmptyGroup` 的判定以**最终工作区结构**为准：如果该标签组作为合法整体事务的一部分
被移除（例如整个源组并入目标组、或浮根整体回停），hidden 面板随组一起移动，
不应用“单面板移出”的规则误拒绝；只有当组仍然存在且没有任何可见面板时才拒绝。
所有失败操作必须保持原树、W3 摘要与相关活动面板状态不变。
| `RestoreDefault` / `ResetToDefault` | 清除全部 hidden 状态（默认布局无隐藏面板） |

所有操作保持不可变模型语义：失败返回错误且不产生部分状态。`DockWorkspace`
提供与 `DockTree` 对应的 `HidePanel` / `ShowPanel`，并保留全局唯一性与上限
校验。

### D8-A-3 未解析面板：进入模型，但保持既有持久化保护

这是本契约对 ADR-008“未知面板”行为的唯一修订，且必须在 D8-E 时回写 ADR-008
记录：

- **加载**：含未知面板 ID 的 Dock 不再被判定为 `UnresolvedPanels` 回退。结构
  合法时，未知身份按原位置、原顺序、原 hidden 状态装入工作区；
  `LayoutFallbackKind.UnresolvedPanels` 不再由加载路径产生（枚举值可保留但
  不得返回），`UsedFallback=false`，文件**不隔离**；
- `LayoutLoadResult.UnresolvedPanels` 继续作为诊断集合，供状态栏与测试使用；
- **持久化保护不变**：只要工作区含未解析面板，`PersistenceSuppressed` 为真，
  500 ms 防抖自动保存与退出快照都不写文件，文件保持原样；只有用户显式
  `Save Layout` 才允许写入。显式保存成功后清除抑制。**`Close Panel`、Hide/Show
  等普通模型操作不得解除该抑制**——只有显式保存或显式 Reset 才能结束抑制
  状态，避免一个看似普通的关闭操作意外清除磁盘上尚未解析的布局数据；
- **显式保存是忠实的**：schema v3 快照原样保留未解析面板的身份、顺序与 hidden
  状态；序列化器不再因未知 ID 抛 `DockModelException`，只做结构与可见性校验。
  显式 Reset 布局是有意的破坏行为，按既有语义丢弃未解析面板；
- **绝不静默映射**：未解析面板不经过 `EditorDockPanelFactory`，不会被替换为
  某个内置面板；投影只能显示明确的“unavailable”占位；
- 未解析面板与 hidden 正交：hidden 的未解析面板不会被挂载；显示它只会恢复
  占位标签；
- 未解析面板计入现有节点/面板上限，参与全局唯一性、拖放/移动/关闭等身份
  操作，语义与已知面板一致，唯一区别是永远没有真实视图；
- 若加载阶段的浮根创建/`Prepare` 失败，仍按 D7 的候选内合并规则处理；合并
  过程中 hidden 与未解析身份都必须保留。

### D8-A-4 视图所有权与协调器

**解析能力**：`IDockPanelViewFactory` 增加 `bool CanCreate(PanelInstanceId)`，
注册表只在 `CanCreate` 为真时调用 `Create`。内置编辑器工厂对四个内置面板返回
真，其它返回假。`DockPanelViewRegistration` 增加 `Unresolved` 标志；
`Failed` 仍只表示“工厂存在但创建失败”。

**挂载集合**：注册表只包含可见面板（已解析的真实视图 + 未解析的占位）；
隐藏面板没有注册表条目。占位不是失败：不产生降级、不进入 Retry、不 Dispose
任何东西。

**事务顺序（沿用 D6/D7）**：

| 操作 | 顺序与失败语义 |
| --- | --- |
| 隐藏 | 模型提交 → 逐宿主 Apply/Show → 释放隐藏面板视图（恰好一次）；提交后宿主失败按 D6 降级/Retry，模型保持隐藏状态，不回滚 |
| 显示 | 模型提交 → 逐宿主 Apply → `GetOrCreate` 创建视图；视图创建失败按现有失败占位与降级/Retry，模型保持可见状态，不回滚 |
| 关闭 | 保持现有顺序：模型提交 → 宿主应用/关闭浮窗 → 释放视图；隐藏面板关闭不调用释放 |
| 恢复 | 只为最终工作区中的**可见**面板创建视图；工厂调用次数等于“可见且已解析”的面板数；隐藏面板零调用；未解析面板零调用、只建占位 |
| 放弃/重试 | `Retry` 只重建缺失/失败的已解析视图，不重复创建已存在视图；隐藏面板不因 Retry 被创建 |

**健康与持久化**：健康门控、宿主数、令牌与摘要校验沿用 D7；摘要改为 W3 后，
`Capture` 仍要求宿主当前投影与提交根一致。含未解析面板时 `Capture` 可以被
显式保存使用，但自动保存调度仍按 D8-A-3 抑制。

**编辑器门面**：`EditorDockWorkspace` 新增 `HidePanel`、`ShowPanel`、
`HiddenPanels`、`UnresolvedPanels`；协调器新增对应方法。Shell 需要提供最小
生产入口：

- Dock 标签右键菜单增加 `Hide Panel` 与 `Close Panel`（主宿主与浮窗宿主一致；
  Close 仍受模型规则约束）；
- `View` 菜单增加 `Hidden Panels` 子菜单，按工作区动态列出隐藏面板，点击执行
  显示；没有隐藏面板时显示禁用项；
- 含未解析面板时状态栏显示明确提示（不可用面板数量 + “自动保存暂停，使用
  Save Layout”），不得静默。

### D8-A-5 持久化：Schema v3 与迁移

- `WorkspaceLayout.CurrentSchemaVersion = 3`；
- `dock` 的 `tabs` 节点新增可选 `hidden` 数组（字符串面板 ID）；`split` 节点
  出现 `hidden` 一律拒绝；
- `hidden` 必须是 `panels` 的子集、无重复、不包含 `activePanel`，且隐藏后仍
  至少一个可见面板；否则按 `InvalidDock` 整体回退默认并隔离；
- 写出时 `hidden` 按 `panels` 顺序输出，空集合省略；相同工作区重复序列化
  字节一致；
- v1 与 v2 迁移：Shell 字段与现有 Dock 保留；无 hidden（迁移后等价于全部可见）；
  不隔离；下一次成功保存升级为 v3；
- 未知 schema（>3）仍按 `UnsupportedSchema` 回退并隔离；
- 未解析面板不再使加载回退，但自动保存抑制与显式保存规则见 D8-A-3；
- 1 MiB 文件上限、树深度 64、节点 512、面板 256、浮根 16、bounds 限制不变；
  hidden 面板计入面板上限；
- 规范 JSON 示例（仅展示 tabs 的 hidden 字段）：

```json
{
  "schemaVersion": 3,
  "dock": {
    "main": {
      "kind": "tabs",
      "id": "dock.tabs.bottom",
      "panels": ["editor.inspector", "editor.console"],
      "hidden": ["editor.console"],
      "activePanel": "editor.inspector"
    },
    "floats": []
  }
}
```

### D8-A-6 加载与运行时故障语义

| 阶段 | 故障 | 处理 |
| --- | --- | --- |
| 加载 | JSON/schema/结构非法（含非法 hidden） | 整个布局回退默认；隔离；不创建候选宿主（沿用 D7） |
| 加载 | 面板 ID 未解析但结构合法 | Dock 正常装入；未解析占位；**不回退、不隔离**；自动保存抑制 |
| 加载 | hidden 状态合法 | 正常恢复；隐藏面板不创建视图、不调用工厂 |
| 加载 | 浮根创建/`Prepare` 失败 | 候选内按 D7 规则合并回主根；hidden/未解析身份保留；重新校验 |
| 加载 | 主/浮宿主 `Apply`/`Show` 失败 | D6 运行时降级 + Retry；不合并、不回滚；未解析占位不影响健康 |
| 运行时 | 隐藏/显示提交后宿主失败 | 模型保持新状态；宿主降级 + Retry；视图释放/创建恰好一次 |
| 运行时 | 视图工厂失败（已解析面板） | 现有失败占位 + 降级/Retry；模型不变 |
| 运行时 | 隐藏/关闭最后一个可见面板 | 原子失败 `WouldEmptyGroup`；模型、视图、持久化均不变 |
| 持久化 | 含未解析面板时的自动保存/退出快照 | 抑制写入；文件保持原样；显式保存可写入 |
| 持久化 | 显式保存含未解析面板 | 忠实写出 v3；清除抑制；失败时保留旧文件（沿用 D7） |

加载阶段与运行时路径不得混用：加载期不做运行时回停/补偿，运行时不因未解析
或 hidden 把浮根静默合并。

### D8-A-7 门禁计划

**D8-B 纯模型（Nova.UI.Core.Tests）**

| 场景 | 通过条件 |
| --- | --- |
| 隐藏/显示往返 | 面板顺序、组、活动标签、Revision、W3 摘要正确；显示不自动激活 |
| 活动标签被隐藏 | 按“后一个可见、否则前一个”确定性切换 |
| 每组至少一个可见 | 隐藏、关闭、移动、浮动最后可见面板全部原子拒绝 `WouldEmptyGroup` |
| hidden-only 组 | 构造、反序列化候选、归一化都不得产生；校验拒绝 |
| 关闭隐藏面板 | 模型移除成功；不要求视图；重启不出现 |
| 隐藏面板作为拖放/浮动源 | 拒绝 `PanelHidden`；模型不变 |
| 合并/回停携带 hidden | 组移动、跨组移动、浮根合并/回停保留 hidden，活动标签始终可见 |
| 上限与全局唯一 | hidden 面板计入 256 上限；重复 ID 仍拒绝 |
| 默认/重置 | 默认布局无 hidden；Reset 清除 hidden |

**D8-C 序列化与持久化（Core + Editor.Services/Integration）**

| 场景 | 通过条件 |
| --- | --- |
| v3 往返 | hidden 顺序、活动标签、结构、浮根全部保持；重复序列化字节一致；W3 摘要一致 |
| v2/v1 迁移 | Dock 与 Shell 字段保留；无 hidden；不隔离；保存升级 v3 |
| 非法 hidden | 非子集、重复、隐藏 active、全隐藏、split 带 hidden 全部 `InvalidDock` 回退并隔离 |
| 未解析面板加载 | 不回退、不隔离；工作区含原身份/顺序；`UnresolvedPanels` 诊断；自动保存与退出快照抑制 |
| 未解析面板显式保存 | v3 忠实写回身份/顺序/hidden；清除抑制；无静默映射 |
| 未解析 + hidden | 往返后仍隐藏、仍不调用工厂 |
| 未解析 + 结构非法 | 仍按 `InvalidDock` 整体回退并隔离 |
| 显式 Reset | 有意的破坏行为；未解析面板被丢弃 |

**D8-D 视图所有权与协调器（Controls/Integration）**

| 场景 | 通过条件 |
| --- | --- |
| 隐藏释放视图 | 工厂创建 1 次，隐藏后 Dispose 1 次；注册表无该面板 |
| 显示重建视图 | 显示后工厂再创建 1 次；不重复创建旧视图 |
| 未解析占位 | 工厂 `Create` 调用 0 次；占位可见、可激活/移动/关闭；不降级 |
| 恢复工厂计数 | 工厂调用数 = 可见且已解析面板数；隐藏 0、未解析 0 |
| 运行时宿主失败 | 隐藏/显示提交后宿主失败：模型保持、降级、Retry；视图释放/创建恰好一次 |
| 关闭隐藏面板 | 不调用视图释放、不产生异常 |
| 回停/合并携带 hidden | 浮根回停后 hidden 保留，视图集合正确 |
| Shell 入口 | 右键 Hide/Close、View > Hidden Panels、状态栏未解析提示均为生产绑定（非测试专用入口） |

**D8-E 真实进程与故障门禁**

| 场景 | 通过条件 |
| --- | --- |
| 隐藏后重启 | 真实进程退出/新 PID 启动后仍隐藏；重启不创建该面板视图 |
| 显示后重启 | 新进程恢复为可见，活动标签与顺序正确 |
| 未解析面板重启 | 加载不隔离、不覆盖文件；占位标签存在；自动保存抑制；显式保存后身份保留 |
| 未解析 + 自动保存抑制 | 普通变更与退出都不写文件；文件字节不变 |
| 浮窗隐藏/回停 | 浮根中的 hidden 经回停重启后保留 |
| 混合场景 | 主根与浮根同时含 hidden + unresolved；结构、身份、可见性全部保持 |
| 回归 | D6/D7 既有真实进程门禁不回退；Debug/Release 全量通过；format 退出 0；NU1900 如实记录 |

### D8-A-8 变更边界

- D8-A：仅本 ADR；不修改源码或测试；
- D8-B：`Nova.UI.Core` 的 `DockNode`/`DockTree`/`DockWorkspace`/校验/归一化/
  摘要，以及 `Nova.UI.Core.Tests`；
- D8-C：`Nova.UI.Core` 的 `WorkspaceLayout`/快照 DTO/序列化（schema v3），
  `Nova.Editor.Services` 的抑制与显式保存路径，以及对应 Core/Integration 门禁；
- D8-D：`Nova.UI.Avalonia` 的工厂契约、注册表、投影、协调器，
  `Nova.Editor.Views`/`Nova.Editor.App` 的门面、菜单与状态栏，以及
  Controls/Integration 门禁；
- D8-E：真实进程门禁、ADR-008 回写、ADR-009 实施记录与记忆更新；
- 全程不修改布局 Undo 相关代码；不新增第三方面板注册机制；不修改
  `editor_avalonia`、`native/gate` 或其他无关 dirty 文件；工作树保持未提交；
  不执行 commit、push、tag 或 merge。

## 待确认项（D8-A 审查）

> 以上 9 项已由用户在 2026-10-26 的 D8-A 评审中**全部接受**，本契约冻结。
> 实施时以本 ADR 与下方“D8-A 评审裁决记录”为准。

1. **隐藏表示**：hidden 作为标签组内 `Panels` 的有序子集（推荐），而不是独立
   旁路集合。这样顺序、组归属、浮根归属和持久化都无需第二套权威。
2. **可见性不变量**：每个标签组必须保留至少一个可见面板；隐藏/移动/关闭最后
   一个可见面板一律拒绝 `WouldEmptyGroup`，不做自动提升。若希望“关闭最后可见
   面板时自动显示下一个隐藏面板”，需要改为显式规则并补充交互语义。
3. **隐藏时活动标签**：确定性选择同组后一个可见面板，否则前一个；不跨组跳转。
4. **隐藏释放视图**：隐藏即 Dispose 视图，显示重新创建（推荐，资源语义与关闭
   一致）；而不是缓存隐藏视图。
5. **未解析加载**：结构合法时装入工作区并显示占位，不再整树回退；自动保存与
   退出快照保持抑制，只有显式 Save Layout 才写回。这是对 ADR-008 未知面板
   回退行的唯一修订。
6. **序列化器接受未解析 ID**：只校验结构与可见性；显式保存忠实往返。自动写入
   仍被抑制，因此不会在用户不知情时改写文件。
7. **Schema v3**：tabs 节点新增可选 `hidden`；v1/v2 迁移为全可见、不隔离；
   下一成功保存升级。工作区摘要升级 W3。
8. **最小 Shell 入口**：标签右键 `Hide Panel` / `Close Panel`，`View > Hidden
   Panels` 显示入口，状态栏未解析提示。若本阶段不希望暴露 `Close Panel` UI，
   可只保留 Hide/Show 入口，关闭继续走生产门面。
9. **不做布局 Undo**：隐藏状态不进入撤销栈；关闭仍不可恢复。Undo 作为后续
   独立切片，届时再冻结其与 hidden/unresolved 的交互。

## 后果与风险

- 模型从“面板只有存在/不存在两态”变为“解析轴 × 可见轴”，所有假定
  `EnumeratePanels()` 等于挂载集合的代码都需要审计（拖放目标、回停索引、视图
  释放、持久化捕获、测试工厂计数）；
- schema v3 与 W3 摘要会改变规范 JSON 与摘要值；测试不得硬编码旧摘要字符串；
- 未解析面板从“整树回退”改为“原地占位 + 自动保存抑制”，风险是投影/拖放中
  出现没有真实视图的标签；通过 `CanCreate`、占位注册与门禁降低风险；
- 隐藏释放视图意味着面板内部 UI 状态不保留；面板状态本就不在模型内，与现有
  Close 的资源语义一致；
- 多显示器/混合 DPI、第三方面板注册、布局 Undo 仍是未覆盖范围，不能从本轮
  结果推定支持。

## D8-A 评审裁决记录（2026-10-26）

**结论**：D8-A 评审通过，契约冻结。O-1 至 O-9 全部接受，D8-B（纯模型）批准
开始；D8-C/D8-D/D8-E 未开始。用户裁决依据为契约摘要，不表示其独立逐行检查
ADR 原文。

| 编号 | 决策 | 裁决 | 冻结要求 |
| --- | --- | --- | --- |
| O-1 | Hidden 是标签组内的有序子集 | 接受 | 复用 `Panels` 顺序与组归属，不另建布局权威 |
| O-2 | 每组至少一个可见面板 | 接受 | 隐藏/移动/关闭最后一个可见面板原子拒绝 `WouldEmptyGroup`，不自动提升；整组合法移除按最终结构验证 |
| O-3 | 隐藏活动标签的替代规则 | 接受 | 后项优先，否则最近前置可见项；不循环绕回 |
| O-4 | 隐藏时释放面板视图 | 接受 | 保留身份与顺序，显示时重新创建 |
| O-5 | 未解析面板恢复为占位并抑制自动保存 | 接受 | 自动保存与退出快照抑制；只有显式 `Save Layout` 解除；Close 等普通操作不得解除 |
| O-6 | 序列化器接受未解析 ID | 接受 | 校验结构/身份/顺序/可见性，不要求本机存在工厂 |
| O-7 | Schema v3 + W3 摘要 | 接受 | `tabs` 可选 `hidden`；v1/v2 全可见迁移；W3 不含解析状态 |
| O-8 | Shell 同时提供 Hide 与 Close Panel | 接受 | 标签菜单 Hide/Close，`View > Hidden Panels` 恢复入口 |
| O-9 | 布局 Undo 不在本阶段 | 接受 | 独立规划，不扩张 D8 范围 |

**三条不可变模型语义**

1. 解析状态与可见状态正交：解析状态只由运行时工厂决定，不得写入持久化布局
   或 W3；`unresolved + hidden` 合法，隐藏时不创建占位，显示时仍为占位；
   `PanelInstanceId` 保持不透明，未知 ID 永不映射到内置面板。
2. 每个标签组始终有可见内容：拒绝而不是自动提升；整组移除按最终结构验证；
   失败操作保持原树、W3 与活动面板不变。
3. Hide 与 Close 生命周期不同：Hide 保留身份/顺序/组与浮根归属，只改变可见性
   并释放视图；Close 从模型移除并释放资源，具有破坏性；含未解析面板时
   Close 不解除持久化抑制。

**D8-B 批准范围**：仅 `Nova.UI.Core` 的标签组 hidden 状态、`DockTree`/
`DockWorkspace` 的隐藏/显示/移动/关闭/活动标签语义、`WouldEmptyGroup` 与
`PanelHidden`、W3 摘要与纯模型测试。**不得**在 D8-B 引入 schema v3、序列化
变更、`CanCreate`、视图释放/重建或 Shell UI；这些分别属于 D8-C 与 D8-D。

**ADR-008 修订方式**：D7 的未知面板整树回退记录保持原样；在 ADR-008 末尾以
明确标注的 superseding decision 记录新行为，并注明自 D8-C 实施起生效。

## D8-B 实施记录（2026-10-26）

**范围**：仅 `Nova.UI.Core` 纯模型与 `Nova.UI.Core.Tests`。未修改 schema、序列化、
视图工厂、投影/协调器、Shell、真实进程门禁或布局 Undo。

**模型实现**

- `DockTabGroupNode` 增加可选 `hiddenPanels`；`Panels` 保持完整顺序权威，
  `HiddenPanels` 按 `Panels` 顺序规范化输出，`VisiblePanels` 为过滤结果，
  `IsHidden()` 为成员判定；`HiddenDeclarations`（internal）保留原始声明供校验，
  非法声明不会被静默丢弃；
- 校验新增 `invalid-hidden`、`duplicate-hidden`、`hidden-active`、`empty-visible`；
- 归一化只按 `Panels` 顺序规范化 hidden，并只把活动面板修复为第一个可见面板；
  没有可见面板时保留原状交给校验拒绝，不提升 hidden、不丢 hidden；
- `DockTree` 新增 `HidePanel` / `ShowPanel` / `EnumerateVisiblePanels` /
  `EnumerateHiddenPanels`；隐藏活动面板按“后一个可见，否则最近前置可见，
  不绕回”替换；隐藏、关闭、移动、边缘分离最后可见面板返回 `WouldEmptyGroup`；
  hidden 源在激活/重排/移动返回 `PanelHidden`；添加、跨组移动、合并组、
  浮根合并（`AppendPanels`）、删除均保留 hidden；整组被合法移除不误拒；
- `DockWorkspace` 新增 `HidePanel` / `ShowPanel` / `EnumerateVisiblePanels` /
  `EnumerateHiddenPanels`，错误码新增 `PanelHidden`、`WouldEmptyGroup`；
  浮根内隐藏/显示与主根语义一致，浮根回停保留 hidden；
- 树摘要加入规范 hidden 段（`|h:`，按 `Panels` 顺序）；工作区摘要前缀改为
  `W3|`。摘要不含解析状态，未知 `PanelInstanceId` 与内置 ID 同等对待。

**门禁**：新增 `DockHiddenLifecycleTests` 10 个纯模型测试（WM-20..WM-29），覆盖
隐藏/显示往返、活动标签替换、最后可见面板的隐藏/关闭/移动/浮动拒绝、
hidden-only 组拒绝、隐藏面板关闭、hidden 源拒绝、组合并与跨组移动/浮根回停
携带 hidden、W3 规范性与不透明 ID、256 上限与全局唯一、默认/Reset 清除 hidden。

**结果（2026-10-26）**：Debug 与 Release 全量构建均 0 error、18 条 NU1900
（不可达的 Azure China 源，非零警告）；`dotnet test Nova.sln --no-build` 两种
配置均 259/259（Core 93、Controls 58、Integration 103、Performance 5，其中
Core 新增 10 个）；`dotnet format --verify-no-changes --no-restore` 退出 0。

**未覆盖**：Schema v3/序列化、未解析占位、`CanCreate`、视图释放/重建、Shell
入口、真实进程证据均属 D8-C..D8-E；布局 Undo 不在本阶段。多显示器/混合 DPI
仍未验证，不能从本记录推断为已支持。


## D8-C 实施记录（2026-10-26）

**范围**：仅 schema v3 序列化、v1/v2 迁移与持久化数据语义，以及对应
`Nova.UI.Core.Tests` / `Nova.Editor.IntegrationTests` 门禁。未修改 `CanCreate`、
视图工厂/注册表/投影、Shell 的 Hide/Show/Close 入口、真实进程门禁或布局 Undo；
未触碰 `editor_avalonia`、`native/gate` 与无关 dirty 文件。

**Schema v3 与序列化**

- `WorkspaceLayout.CurrentSchemaVersion = 3`；`DockNodeSnapshot` 新增可选
  `Hidden`（JSON 顺序位于 `panels` 与 `activePanel` 之间）；
- 写出时 `hidden` 按 `Panels` 顺序输出（`HiddenPanels`），空集合省略；构造时
  保留原始 `HiddenDeclarations` 供校验，非法声明不会被静默丢弃；
- `split` 节点出现 `hidden`（包括空数组）按 tab 字段混用拒绝；
- 序列化前仍经 `ToWorkspace` 做结构与可见性校验：非法快照抛
  `DockModelException`，不会写出下次加载必然拒绝的数据；
- 未知面板 ID 不再导致序列化失败；规范化路径
  `ToWorkspace → FromWorkspace` 只重排 hidden、不删除身份、不替换为内置面板；
- v1 只迁移 Shell 字段并使用默认 Dock；v2 保留已有 Dock 并与 v3 一样迁移为
  **全部可见**；v2 文件若携带 `hidden` 字段视为版本不一致，按 `InvalidDock`
  整体回退并隔离，而不是静默忽略或“修复”；
- 非法 hidden（非 `panels` 子集、重复、隐藏 `activePanel`、全部隐藏、
  空 ID）由模型校验拒绝，加载路径整体回退默认并隔离；不做确定性自动修复，
  因此不会把非法快照报告成原样恢复成功。

**未解析面板与持久化**

- `LayoutFallbackKind.UnresolvedPanels` 已移除；未知面板不再产生 fallback。
  加载结果返回 `UsedFallback=false`、`ShouldQuarantine=false`，Dock 原位装入，
  未知 ID 保留在 `Panels` 的原顺序与原 hidden 状态中；
  `LayoutLoadResult.UnresolvedPanels` 仅作为诊断集合（含 JSON `Reason` 提示）；
- `LayoutPersistenceService.PersistenceSuppressed` 改为
  `ReadFailed || UnresolvedPanels.Count > 0`；仅 hidden（无未解析面板）不抑制；
- `InitializeLayoutAsync` 在 fallback 或抑制加载时把 `_lastSavedContent` 置空，
  使第一次显式 `Save Layout` 不会被“内容未变”去重跳过；显式保存成功后清除
  抑制，自动保存与退出快照继续拒绝写入；
- 显式保存忠实写出 v3：未解析身份、顺序、hidden 状态原样往返；未知 ID 不经过
  面板工厂；普通模型操作（含 Close/Hide/Show）不解除抑制；
- 既有 D7 保护未回退：读取失败、超限、损坏、非法 Dock 的隔离/抑制、
  原子写入、串行化、健康门控与去重语义保持不变；序列化失败仍保留旧文件。

**门禁（新增/重写的关键项）**

| 场景 | 门禁 | 结果 |
| --- | --- | --- |
| v3 hidden 往返与摘要 | `D8c_hidden_panels_round_trip_and_preserve_digest`、`D8c_hidden_order_is_canonical_without_losing_identity`、`D8c_hidden_panel_in_float_root_round_trip` | hidden 顺序规范、字节稳定、W3 摘要一致、浮根 hidden 与回停目标保持 |
| v1/v2 迁移 | `D8c_schema_v2_dock_snapshot_migrates_to_all_visible`、`D7b_schema_v1_migrates_shell_fields_and_uses_default_dock`、`D7b_schema_v2_without_dock_uses_default_workspace`、`Schema_v2_layout_loads_as_v3_with_every_panel_visible` | Dock/Shell 保留；无 hidden；不隔离；下一次保存升级 v3 |
| 非法 hidden | `D8c_illegal_hidden_declarations_fall_back_whole_layout`、`D8c_schema_v2_hidden_declaration_is_rejected_and_quarantined`、`Illegal_hidden_declarations_are_quarantined_at_startup` | 全部 `InvalidDock` 整体回退并隔离，无静默修复 |
| 未解析加载 | `D8c_unknown_panel_loads_in_place_without_fallback`、`Unknown_panels_load_in_place_and_suppress_automatic_writes_until_an_explicit_save` | 不回退、不隔离；身份/顺序/hidden 保留；自动保存与退出快照抑制 |
| 显式保存忠实写回 | `Unknown_panels_load_in_place_and_suppress_automatic_writes_until_an_explicit_save` | v3 写回未知 ID 与 hidden；清除抑制；文件不再被去重跳过 |
| 未解析 + hidden | `D8c_unknown_hidden_panel_round_trip` + 上述集成门禁 | 往返后仍隐藏、身份与顺序保持；视图层“不挂载/不调用工厂”属 D8-D |
| hidden 不抑制 | `Hidden_panels_do_not_suppress_saves_and_survive_explicit_writes` | 纯 hidden 布局可自动保存；hidden 跨保存保留 |
| 序列化失败 | `Serialization_failure_keeps_the_previous_valid_file` | 旧文件字节不变、无 temp 残留 |
| 损坏/超限 | `D7b_corrupt_and_unsupported_schema_kinds_are_distinct`、`Oversized_file_is_quarantined_before_parsing` | 仍按 D7 规则隔离/回退 |

**结果（2026-10-26）**：Debug 与 Release 全量构建均 0 error、18 条 `NU1900`
（不可达的 Azure China 源，非零警告）；`dotnet test Nova.sln --no-build` 两种
配置均 **269/269**（Core 100、Controls 58、Integration 106、Performance 5；
Core 净增 7、Integration 净增 3 个 D8-C 专项测试）；`dotnet format
Nova.sln --verify-no-changes --no-restore` 退出 0。

**未覆盖**：未解析占位的创建/释放、隐藏面板视图释放与恢复、`CanCreate`、
Shell Hide/Show/Close、状态栏提示、真实进程证据均属 D8-D/D8-E。当前投影器仍
会挂载全部 `Panels`（含 hidden），D8-C 只保证持久化数据语义，不能从本轮结果
推断隐藏面板已被正确卸载。显式 Reset 丢弃未解析身份的完整链路需要 D8-D 的
协调器重置接线；本轮未新增该 UI/协调器入口。多显示器/混合 DPI 仍未验证。

## D8-C 验收记录（2026-10-26）

**结论**：用户验收通过并关闭 D8-C，批准开始 D8-D。验收依据为实施方提交的执行
记录，用户未独立检查源码或重新运行测试。本轮不能描述为零警告构建。

- 通过依据：Debug/Release 全量测试各 269/269（Core 100、Controls 58、
  Integration 106、Performance 5）；两种配置构建均 0 error、各 18 条 `NU1900`；
  `dotnet format --verify-no-changes --no-restore` 退出 0；
- 数据边界确认：schema v3 与 v1/v2 迁移、未知面板原位恢复不隔离、持久化抑制、
  首次显式保存不被去重、非法 hidden 整体回退且不静默修复；
- D8-D 批准范围：`CanCreate` 与解析状态、隐藏视图释放/重建、未解析占位与 Shell
  生产入口、Reset 与资源所有权；必须防止投影器继续挂载 hidden，以及未解析面板
  触发工厂或整棵投影降级；
- D8-E 暂不开始；布局 Undo 仍不属于 D8。

## D8-D 实施记录（2026-10-26）

**范围**：`Nova.UI.Avalonia` 的工厂契约、注册表、投影、协调器、拖拽，
`Nova.Editor.Views` 的门面/菜单/状态栏，以及 Controls/Integration 门禁。未修改
schema/序列化（D8-C 成果原样使用）、真实进程门禁或布局 Undo；未触碰
`editor_avalonia`、`native/gate` 与无关 dirty 文件；工作树保持未提交。

**CanCreate 与解析状态**

- `IDockPanelViewFactory` 新增 `CanCreate(PanelInstanceId)`；注册表只在
  `CanCreate` 为真时调用 `Create`；`EditorDockPanelFactory` 仅对四个内置面板
  返回真；
- `DockPanelViewRegistration` 新增 `Unresolved`；`Failed` 收紧为
  `View is null && !Unresolved`，未解析占位不是失败；
- 未解析面板注册即占位（`DockPanelUnavailable`），不调用工厂、不进入失败集合、
  不触发 Retry 重建（`Retry` 只处理 `Failed`）、不产生降级；激活占位、移动、
  隐藏/显示、关闭全部走正常模型路径。

**投影只挂载可见面板**

- 标签构建、最终面板集合、失败收集、Retry、浮窗标题全部基于
  `VisiblePanels` / `EnumerateVisiblePanels()`；隐藏面板不创建、不挂载、不持有
  注册表条目；
- 拖拽源解析与插入槽计算改按可见标签计数（`ToModelIndex` 把视觉槽位映射回
  `Panels` 模型索引），hidden 面板在屏幕上没有槽位，不会被解析为拖拽源或插入
  位置；
- `ReplaceMainRoot`（含 `Apply`/恢复）按 `EnumerateVisiblePanels()` 判定释放
  集合，主根替换导致面板“变为 hidden”时同样释放视图；释放幂等。

**协调器与标签动作入口**

- 协调器新增 `HidePanel` / `ShowPanel` / `ChangePanelVisibility`：模型提交 →
  宿主 Apply/Show → 隐藏后 `ReleasePanels` 恰好一次；显示走 `GetOrCreate`
  重建一次；提交后宿主失败按 D6 降级 + Retry，模型不回滚；
- 标签右键菜单：主宿主 `Float Panel` + 所有宿主 `Hide Panel` / `Close Panel`；
  动作经 `PanelActionRequested` 回调（宿主 + 令牌校验，`ValidateCallback`）由
  协调器执行，失败不破坏模型。

**Shell 入口与诊断**

- `View > Hidden Panels` 动态菜单（无隐藏面板显示禁用项）；
- 状态栏 `DockDiagnosticText`：未解析面板数量提示；持久化抑制时追加
  “autosave paused, use Save Layout”，显式保存成功后经 `LayoutSaveCompleted`
  刷新消失；
- 工具栏 Reset 按钮获得 `x:Name="ResetLayoutButton"`（生产绑定，非测试专用
  入口）：先 `dock.ResetToDefault()`（清除未解析身份与 hidden）再执行
  `ResetLayout` 命令的显式保存，写出干净 v3 并清除抑制。

**资源所有权**

- 隐藏释放视图恰好一次（重复隐藏返回 `AlreadyInState`，不二次释放）；隐藏面板
  的 Close 为纯模型操作，不重复释放；显示重建一次（新注册表条目、新宿主控件）；
- `RestoreDefault` 丢弃未解析身份与 hidden，旧隐藏视图只释放一次；
- 浮根回停保留 hidden 身份，不重建其视图；工作区 Dispose 幂等释放。

**门禁（新增 11 个专项测试）**

| 层 | 测试 | 覆盖 |
| --- | --- | --- |
| Controls | `Hiding_a_panel_unmounts_and_releases_its_view_and_showing_recreates_it` | 隐藏释放一次/显示重建一次、顺序与活动标签、重复隐藏 `AlreadyInState` |
| Controls | `Closing_a_hidden_panel_does_not_release_its_view_twice` | 隐藏后关闭为纯模型操作、不二次释放 |
| Controls | `Unresolved_panels_mount_a_placeholder_without_calling_the_factory` | 零工厂调用、占位、激活/Retry/移动/隐藏/显示/关闭均不降级 |
| Controls | `Hiding_with_a_failed_projection_keeps_the_model_and_retry_does_not_recreate` | 宿主失败仍提交模型、Retry 不重建 hidden 视图 |
| Controls | `Redocking_a_float_keeps_hidden_identity_without_recreating_its_view` | 回停保留 hidden、视图复用 |
| Controls | `Restoring_the_default_layout_drops_unresolved_and_hidden_identities` | Restore 清除未解析与 hidden、工厂计数正确 |
| Controls | `Replacing_the_main_root_releases_panels_that_become_hidden` | 主根替换使面板变 hidden 时释放视图 |
| Integration | `Tab_context_menu_hide_unmounts_and_the_hidden_panels_menu_restores_it` | 真实右键 Hide/Show、主宿主菜单契约 [Float Panel, Hide Panel, Close Panel] |
| Integration | `Tab_context_menu_close_removes_the_panel_and_releases_its_view_once` | 真实右键 Close、模型移除、释放一次 |
| Integration | `Unknown_panels_render_placeholders_with_status_diagnostics` | 未知布局加载零工厂调用、占位、状态栏提示与抑制解除 |
| Integration | `Reset_layout_button_clears_unresolved_and_hidden_and_saves_a_clean_layout` | 真实 Reset 按钮：清除未解析/hidden、显式保存干净 v3、旧视图仅释放一次 |

D5 拖拽门禁（可见槽位语义）与既有回归全部保持通过。

**结果（2026-10-26）**：Debug 与 Release 全量构建均 0 error、各 18 条 `NU1900`
（不可达的 Azure China 源，非零警告；曾出现的 2 条 CS8600/CS8602 已修复）；
`dotnet test Nova.sln --no-build` 两种配置均 **280/280**（Core 100、
Controls 65、Integration 110、Performance 5；Controls 净增 7、Integration 净增
4 个 D8-D 专项测试）；`dotnet format --verify-no-changes --no-restore` 退出 0。

**未覆盖**：真实进程隐藏/显示/未解析重启证据、ADR-008 回写、混合场景故障门禁
属 D8-E；布局 Undo 不在 D8。多显示器/混合 DPI 仍未验证，不能从本记录推断为
已支持。Shell 菜单刷新与状态栏更新依赖 UI 线程事件路径，真实桌面（非测试
宿主）行为需 D8-E 真实进程证据确认。

## D8-D 验收记录（2026-10-26）

**结论**：用户验收通过并关闭 D8-D，批准开始 D8-E（真实进程恢复与故障门禁）。
验收依据为实施方提交的实现与执行记录，用户未独立检查源码或重新运行测试。

- 通过依据：Core 100、Controls 65、Integration 110、Performance 5；Debug 与
  Release 全量测试各 280/280；两种配置构建均 0 error、各 18 条 `NU1900`
  （不可达 Azure China 源，仍有警告）；format 退出 0；Shell 入口走真实右键
  菜单、菜单项与 Reset 按钮路径；工作树未提交、无关文件未修改；
- 实现评审确认四点：未解析面板不误调用内置工厂（`CanCreate` 为假即占位，不降级
  整树）；隐藏与关闭资源生命周期不同（隐藏释放视图保留身份，重复隐藏
  `AlreadyInState`，关闭隐藏面板为纯模型操作）；拖放源/插入槽按可见标签计算并
  经 `ToModelIndex` 映射回模型索引（不破坏 `Panels` 顺序权威）；Reset 先重置
  Dock 清除 hidden 与未解析身份，再显式保存干净 v3，与自动保存抑制规则不冲突；
- D8-E 批准范围（8 个场景）：隐藏→保存→重启仍隐藏且隐藏态不创建视图；隐藏→
  显示→重启视图创建/释放计数正确；未解析面板启动占位正确、零工厂调用、不降级；
  未解析面板自动保存与退出快照被抑制、原文件不被静默覆盖；显式 Save Layout
  忠实写回未知 ID/顺序/hidden 且清除抑制；Reset→保存→重启恢复干净默认；隐藏
  面板后关闭/重开 Shell 无重复释放、失效回调或顺序错误；组合故障（工厂失败、
  宿主应用失败、保存失败）互相隔离、工作区状态可解释；
- 两个重点：Shell 菜单刷新须用实际应用运行路径确认 `View > Hidden Panels`
  的条目、可用性与选择状态随工作区变化及时更新；未解析面板显式保存须断言原始
  未知 ID、顺序与 hidden 状态确实写入新文件，而非仅断言保存成功；
- D8-E 仍限定 Windows、单显示器、96 DPI；多显示器/混合 DPI/跨 DPI 未验证；
  布局 Undo 与第三方面板注册仍不属于 D8。

## D8-E 实施记录（2026-10-26）

**范围**：真实进程门禁、headless 组合故障/Shell 重开门禁、ADR-008 回写与本
记录。**未修改任何产品源码**；唯一的生产代码树改动是
`Nova.Editor.IntegrationTests` 的 TFM（`net9.0` → `net9.0-windows`）与
`Microsoft.WindowsDesktop.App` FrameworkReference（UIA 客户端所需），以及
`RealProcessRestoreTests.ResolveAppPath` 的 TFM 目录名映射修复。未触碰
`editor_avalonia`、`native/gate` 与无关 dirty 文件；工作树保持未提交。

**测试基础设施（真实应用运行路径）**

- 门禁进程内用 System.Windows.Automation（UIAutomationClient）读取真实编辑器
  窗口的 UIA 树：`MenuItem`（名称/IsEnabled/BoundingRectangle）、`TabItem`
  （名称/Selected/SelectionItemPattern）、`Text`；Avalonia 11.2.7 的 Windows
  UIA provider 默认可用，popup 在 MainWindow 的 UIA 树内；
- 真实输入驱动：SetCursorPos + mouse_event 点击菜单（按 UIA BoundingRectangle
  中心）、SendInput 注入 ESC、PostMessage WM_CLOSE 结束进程；
  `ActivateWindow`（ShowWindow + SetForegroundWindow + ALT trick）解决测试宿主
  后台进程的前台化，菜单点击每级最多 5 次 toggle 重试；
- harness：以 `--layout-file` 启动真实 `Nova.Editor.App` 进程 → 等待主窗口 →
  交互 → WM_CLOSE → 断言退出码 0 → 兜底 kill；种子文件全部经生产门面
  （`CreateSample` + `EditorDockWorkspace` + `AttachDockPersistence` +
  `SaveLayoutAsync` / `DockWorkspaceSnapshotCodec`）生成，无测试专用伪入口。

**真实进程门禁（`RealProcessHiddenPanelTests`，RP-1..RP-7）**

| 场景 | 门禁 | 覆盖的批准场景 |
| --- | --- | --- |
| 隐藏 → 保存 → 两次真实重启仍隐藏；UIA 菜单确认 hidden 列表含 console；TabItem 无 console 标签（隐藏态未创建视图） | `Hidden_console_stays_hidden_across_two_real_restarts_and_the_menu_lists_it` | 场景 1 |
| 真实 `View > Hidden Panels` 菜单点击恢复 console；恢复后菜单项消失、Inspector 保持 selected（菜单刷新与选择状态随工作区及时更新） | `Real_hidden_panels_menu_restores_the_console_and_refreshes_menu_state` | 场景 2 + 重点 1 |
| 未解析身份两次真实重启：文件字节不变、无 quarantine、占位存在 | `Unknown_panels_survive_two_real_restarts_without_rewriting_the_layout` | 场景 3 |
| 真实 `File > Save Layout`：新文件包含原始 `third.party.scene/chat` 身份与 hidden 状态（非仅保存成功），抑制解除 | `Real_save_layout_persists_unknown_identities_and_hidden_state_faithfully` | 场景 5 + 重点 2 |
| 真实 Reset 按钮 → 干净 v3 落盘 → 第二次重启确认无 unknown/hidden | `Real_reset_layout_clears_hidden_and_unknown_panels_and_saves_a_clean_layout` | 场景 6 |
| 布局文件被锁：真实 Show Panel 制造 dirty 后保存失败 → UIA 状态栏出现 "Layout save failed:"、进程存活、原文件字节不变（原子替换失败不破坏旧文件）→ 解锁后重试保存成功并在 UIA 中出现 "Layout saved" | `Locked_layout_file_isolates_save_failure_and_recovers_after_unlock` | 场景 8（保存失败隔离） |
| 浮窗内隐藏 console → 回停 → 重启仍隐藏 | `Hidden_console_inside_a_float_redocks_and_stays_hidden_after_restart` | 混合场景（浮根 hidden） |

**headless 门禁（`DockHiddenLifecycleEdgeTests`，场景 7/8 补集）**

- `Closing_and_reopening_the_shell_keeps_hidden_panels_without_double_releases`：
  shell1 经真实自动保存防抖（`LayoutSaveCompleted` 事件，500 ms）落盘 →
  dock.Dispose 后 console 仍只释放 1 次、`ActivateTab` 抛
  `ObjectDisposedException` → 同 workspace 重建 shell2 → hidden 恢复、工厂仅
  创建 3 个可见面板（console 零调用）、旧视图不再被释放、ShowPanel 正常重建
  （第 4 次调用、新视图 0 释放）；
- `Factory_failure_and_unresolved_panels_stay_isolated_and_save_faithfully`：
  Hierarchy 工厂失败 + 未解析种子共存 → `RestoreFailure` null、不降级、banner
  不可见、失败占位（`DockPanelFailure`）挂在左组、Inspector 正常创建、
  unresolved/hidden 语义与抑制完好 → 显式保存忠实写回并解除抑制 →
  修复工厂后 `Retry` 只恢复 Hierarchy。

**既有门禁修复（诚实记录）**

D7-D 真实进程门禁
`Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart`
在 solution 级 Release 全量运行中失败 2 次（隔离/过滤运行均通过）。定位为
**门禁自身的单发 `SwRestore` 与应用启动期最大化状态应用竞争**：`IsZoomed`
首次为真即发送还原请求，宿主尚未稳定时启动期工作会重新最大化。修复为在
10 s 轮询窗口内持续重发 `SwRestore` 直至状态保持（仍走真实 Win32 输入路径，
无测试专用入口）。修复后 solution 级 Release 全量连续 2 次 289/289。产品代码
无改动，判定为门禁时序缺陷而非产品回归。

**结果（2026-10-26）**

- Debug 全量构建 0 error、18 条 `NU1900`（不可达的 Azure China 源，非零
  警告）；`dotnet test Nova.sln`（Debug）**289/289**（Core 100、Controls 65、
  Integration 119、Performance 5；Integration 净增 9：RP-1..RP-7 + 场景 7/8）；
- Release 全量构建 0 error、18 条 `NU1900`；solution 级 Release 全量连续
  2 次 **289/289**；
- `dotnet format Nova.sln --verify-no-changes` 退出 0。

**发现的产品缺陷（由用户裁决修复，见下节）**

`EditorShellViewModel.StatusText`（保存结果文本，含 "Layout save failed /
not saved / saved"）没有绑定到任何 UI 元素：`EditorShellView.axaml` 的状态栏
`StatusText` TextBlock 绑定的是选择状态 store，保存结果只在 vm 属性与日志中。
真实进程门禁因此改以进程存活与文件字节断言隔离语义（RP-6 注释说明）。修复
属于 UI 绑定变更，超出 D8-E 范围，未实施。

## D8-E 验收与状态栏可见性修复（2026-10-26）

**用户裁决**

- D8-E 的 7 个真实进程门禁、headless 组合故障/Shell 重开门禁、289/289 全量
  回归**接受**；
- D7-D 门禁时序修复**接受**，保留四条验收要求：继续使用真实 Win32 输入而非
  直接修改应用内部窗口状态；最终断言必须确认窗口确实处于 Normal 且
  `RestoreBounds` 正确；不得仅凭某一瞬间 `IsZoomed == false` 判定通过，须确认
  应用状态稳定后再读取最终几何；保留修复前的失败证据与修复后的连续通过记录；
- `StatusText` 不可见**不作为长期已知限制**保留在 D8 交付记录中，窄范围修复；
- D8 整体关闭暂缓，待状态栏修复回归后由用户裁决。

**修复范围（产品代码，2 处）**

- `EditorShellView.axaml`：状态栏新增独立元素
  `SaveStatusText`（`Text="{Binding StatusText}"`），置于选择状态元素之前；
  原 `StatusText` TextBlock 的 `EditorUiIds.SelectionStatus` store 绑定**保持
  不变**，两条状态通道并存互不覆盖。工具栏保存按钮补 `x:Name="SaveLayoutButton"`
  （命令绑定不变，仍指向生产 `SaveLayoutCommand`）；
- `EditorShellViewModel.InitializeLayoutAsync`：抑制文本改为
  `Offline · autosave paused ({PersistenceSuppressionReason ?? result.Reason})`，
  避免理由缺失时出现空括号。

Schema v3、W3 摘要、自动保存策略与文件写入实现**未修改**。

**门禁（可见文本断言，替代 ViewModel 属性/日志断言）**

- headless `EditorSaveStatusTests`（3）：断言工具栏按钮 `Command` 与
  `viewModel.SaveLayoutCommand` 是同一实例后执行该 ICommand —— Avalonia 的
  `Button.ClickEvent` 冒泡不会触发命令绑定（`ButtonBase.OnClick` 才执行），
  以此保持生产入口；
  1. `Save_layout_reports_saved_without_replacing_the_selection_status`：
     状态栏出现 "Layout saved"，选择状态槽未被覆盖；
  2. `A_locked_layout_file_shows_the_failure_and_the_retry_shows_success`：
     持锁期间出现 "Layout save failed:"，锁释放后、重试前断言原文件字节
     完全一致（`FileShare.None` 句柄拒绝任何读取，故完整性断言必须在锁外
     执行），重试后出现 "Layout saved" 且 hidden 状态忠实落盘；
  3. `Unresolved_suppression_reports_paused_and_only_an_explicit_save_reports_saved`：
     未解析启动显示 "Offline · autosave paused (…cannot resolve…)"、不含
     "Layout saved"；真实 `ShowPanel` + 800 ms 防抖窗口后仍不误报、仍抑制；
     显式保存后才报 "Layout saved" 并解除抑制。
- 真实进程 `RealProcessHiddenPanelTests` 新增 OS UIA 可见文本断言
  （`ReadTexts` / `HasStatusText` / `AssertNoStatusText`，读取 Avalonia
  Windows UIA provider 暴露的 `ControlType.Text`）：
  - RP-3：两次真实重启的抑制期内状态栏不得出现 "Layout saved"；
  - RP-4：真实 `File > Save Layout` 前不得有 "Layout saved"，点击后必须在
    UIA 树中出现该文本；
  - RP-6：持锁 + 真实菜单点击保存后，UIA 中出现 "Layout save failed:"，
    且不得出现 "Layout saved"；解锁重试后 UIA 中出现 "Layout saved"，
    随后文件断言 `editor.console` 恢复可见。原“状态栏只显示选择状态、故只
    以进程存活与文件字节证明隔离”的降级注释与弱化断言已删除。

**结果（2026-10-26）**

- Debug 全量构建 0 error、18 条 `NU1900`（不可达的 Azure China 源，非零
  警告）；`dotnet test Nova.sln -c Debug --no-build` **292/292**（Core 100、
  Controls 65、Integration 122、Performance 5；Integration 净增 3：
  `EditorSaveStatusTests`）；
- Release 全量构建 0 error、18 条 `NU1900`；`dotnet test Nova.sln -c Release --no-build`
  **292/292**，D8-E 既有 7 个真实进程门禁与 D7-D 8 个真实进程门禁
  均在全量运行中通过，无回退；两次 `--no-build` 测试运行自身 0 条 NU1900；
- 修改后的 RP-3/RP-4/RP-6 以新的 UIA 可见文本断言单独过滤运行 3/3 通过；
- `dotnet format Nova.sln --verify-no-changes` 退出 0。

**未覆盖 / 不能推定**

- 多显示器、混合 DPI、跨 DPI 移动未验证（D8-E 限定单显示器 96 DPI）；
- 真实进程中视图创建/释放计数不可直接观测，由 headless 门禁（场景 7）以
  RecordingFactory 计数覆盖；隐藏态不创建视图在真实进程中以 UIA TabItem
  缺失间接断言（RP-1）；
- 状态栏的保存文本与选择文本共用 `StatusText` 通道：选择变化会覆盖上一次
  保存报告（既有过载通道语义，本次未改）；跨重启的历史保存结果不恢复；
- 布局 Undo、第三方面板注册不在 D8；
- 工作树保持未提交，未执行 commit、push、tag 或 merge。

## D8 关闭裁决（2026-10-26）

**裁决**：用户批准关闭 **D8-A 至 D8-E**，判定隐藏／未解析面板生命周期在已声明
的实现范围内完成验收，D8 阶段**正式关闭**。验收依据本 ADR 记录的执行结果，
用户未独立检查源码或重跑测试。

| 验收项 | 结果 |
| --- | --- |
| Core / Controls / Integration / Performance | 100 / 65 / 122 / 5 |
| Debug 全量测试 | 292/292 |
| Release 全量测试 | 292/292 |
| D8-E 真实进程门禁 | RP-1..RP-7 全通过 |
| D7-D 真实进程回归 | 8 项全通过，无回归 |
| 状态栏可见性门禁 | 3 项通过 |
| `dotnet format --verify-no-changes` | 退出 0 |
| 构建 | 两配置 0 error，各 18 条 `NU1900`（不可达 Azure China 源，**非零警告**） |

**闭环链路**：纯模型（hidden 为组内有序子集，`Panels` 仍是标签顺序唯一权威）→
Schema v3 与 W3 摘要（保留 hidden、身份与顺序）→ 视图生命周期（隐藏释放视图、
显示重建、未解析用占位且不调用不匹配工厂、身份保持不透明）→ Shell 接线
（Tab Hide/Close、`View > Hidden Panels`、状态栏诊断与保存结果、真实 Reset）→
真实进程恢复（重启后隐藏状态、占位、显式忠实写回与故障隔离）→ 保存状态在真实
UI 中可见。未解析面板存在时自动保存与退出快照受抑制，仅显式保存可写回；Reset
清除 hidden 与未解析身份后写出干净 v3。

**仍然有效的范围边界**（D8 关闭不等于这些已完成）：

| 项目 | 状态 |
| --- | --- |
| 多显示器、混合 DPI、跨 DPI | 未验证；不得由单显示器 96 DPI 结果推断 |
| 布局 Undo/Redo | 不属于 D8，未实现；下一阶段候选 |
| 第三方面板注册机制 | 不属于 D8，未实现 |
| 真实进程视图生命周期与恢复 | 仅覆盖 D8-E 声明的场景 |
| Git 交付节点 | 未创建；工作树仍未提交，无 commit/push/tag/merge |
| `editor_avalonia`、`native/gate` 及其他无关 dirty 文件 | 保持原状，未触碰 |

ADR-008（多根工作区持久化）的既有修订继续有效；ADR-009 契约保持冻结，后续
阶段（候选 D9）不得为布局 Undo 修改本阶段封存的模型与持久化契约。下一阶段仅
作为候选切片提出：先定义跨根布局操作的撤销事务、视图所有权与持久化交互边界，
再由用户裁决是否实现。
