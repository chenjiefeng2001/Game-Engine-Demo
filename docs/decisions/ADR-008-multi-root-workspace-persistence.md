# ADR-008: 多根 Workspace 持久化与恢复契约（D7-A）

- 状态：Accepted（D7-A 契约已批准；D7-B、D7-C、D7-D 已完成并通过 Debug/Release
  全量门禁，D7-D 已由用户验收通过，D7 阶段完成）
- 日期：2026-10-26
- 关联：ADR-003（布局持久化异步/原子/失败回退）、ADR-005（单根 DockTree）、
  ADR-006（Avalonia 投影）、ADR-007（多根工作区与浮动宿主）
- 范围：D7-A 只冻结持久化与恢复契约，不写 UI 或持久化代码。D7-B 实现纯数据
  序列化/反序列化与校验，D7-C 接入保存与启动恢复，D7-D 补齐故障边界、真实
  进程恢复门禁与记录。D7-C 曾为满足“至少一次真正独立进程启动”提前实现
  真实进程重启子集；D7-D 已将其并入完整门禁，不再有遗留的 D7-D 未测项
  （单显示器/DPI 限制除外）

## 背景与基线核对

D6-D 已完成：主窗口、真实 OS 浮动窗口、跨窗口拖放、X 回停、视图所有权和
宿主级故障恢复均已通过自动化门禁与单显示器 96 DPI 桌面冒烟。Dock 工作区仍是
**仅进程内状态**，重启后回到默认布局。

现有持久化链路（ADR-003）只覆盖固定的四区域 Shell 布局：

| 能力 | 现状 | D7 处理 |
| --- | --- | --- |
| 文件与模型 | `WorkspaceLayout` schema v1；`LayoutPersistenceService` | 扩展为 schema v2，新增 `dock` |
| 原子写入 | 同目录 temp + `File.Move(overwrite: true)`；`SemaphoreSlim` 串行化 | 复用，不重写 |
| 损坏保护 | 解析失败/未知 schema → 默认布局；损坏文件隔离为 `.corrupt-*` | 复用并扩展到 dock 校验失败 |
| 防抖 | **不存在**。当前只在手动保存、布局切换和窗口关闭时写入 | D7-C 新增 500 ms 防抖与合并 |
| 独立备份 | **不存在**。只有 `.corrupt-*` 隔离；原子替换保证写入失败保留旧文件 | D7 不新增 `.bak`；若需要 last-known-good 备份须单独裁决 |
| 持久化健康门控 | **不存在**。宿主 `IsHealthy` 只用于 UI 降级/重试 | D7-C 新增写入前健康检查 |

另外两个现状约束：

- `DockWorkspaceCoordinator` 构造时拒绝带浮动根的工作区（“restore is not
  implemented”）；`EditorDockWorkspace` 总是从 `DockWorkspace.CreateDefault()`
  开始。启动恢复必须新增“先验证候选、再创建宿主”的路径。
- `EditorShellView` 在构造时立即创建 Dock 宿主，布局文件在 `Loaded` 后才异步
  读取。D7-C 必须把 Dock 宿主创建推迟到布局验证完成之后。

D7 不修改 D6 的拖放、回停、视图所有权和运行时失败补偿语义。唯一对 D6 模型的
修订是浮动窗口“正常 bounds”的表示（见 D7-A-1），用于最大化状态的可恢复性。

## 范围与非目标

**本契约覆盖**

- 主根、浮动根、节点、面板顺序、活动标签、浮动 bounds/最大化/回停目标的
  确定性序列化与恢复；
- schema v2 与 v1 迁移、非法数据拒绝、损坏隔离；
- 变更后防抖保存、显式保存、退出最终保存；
- 保存前健康门控与保存失败保护；
- 加载阶段浮动宿主创建失败的候选内合并；
- 真实新进程恢复与真实浮动窗口的证据要求。

**明确不包含**

- 隐藏面板、未解析面板的完整生命周期；
- 布局 Undo；
- 新的拖放交互或停靠语义；
- 多显示器、混合 DPI、跨 DPI 坐标保真；
- 第三方/扩展面板注册机制；
- 主窗口位置/最大化状态、工作区多配置文件、跨进程实时联动；
- 多实例并发写同一布局文件的协调（D7 不支持，见 D7-A-6）。

## 决策

### D7-A-1 单一文件、Schema v2 与确定性格式

**文件**：继续使用 `%LocalAppData%/NovaEditor/layout.json`。Shell 偏好与 Dock
工作区必须在**同一次原子写入**中保存，避免两个文件之间的版本/一致性分裂。

**Schema v2** 在 v1 字段之上新增 `dock`：

```json
{
  "schemaVersion": 2,
  "windowWidth": 1280,
  "windowHeight": 800,
  "hierarchyWidth": 240,
  "inspectorWidth": 300,
  "consoleHeight": 180,
  "showHierarchy": true,
  "showInspector": true,
  "showConsole": true,
  "themeId": "nova.dark",
  "dock": {
    "main": {
      "kind": "split",
      "id": "dock.main",
      "orientation": "horizontal",
      "firstWeight": 0.28,
      "secondWeight": 0.72,
      "first": {
        "kind": "tabs",
        "id": "dock.tabs.left",
        "panels": ["editor.hierarchy"],
        "activePanel": "editor.hierarchy"
      },
      "second": {
        "kind": "split",
        "id": "dock.split.center",
        "orientation": "vertical",
        "firstWeight": 0.75,
        "secondWeight": 0.25,
        "first": {
          "kind": "tabs",
          "id": "dock.tabs.scene",
          "panels": ["editor.scene"],
          "activePanel": "editor.scene"
        },
        "second": {
          "kind": "tabs",
          "id": "dock.tabs.bottom",
          "panels": ["editor.inspector", "editor.console"],
          "activePanel": "editor.inspector"
        }
      }
    },
    "floats": [
      {
        "id": "dock.float.1",
        "bounds": { "x": 680, "y": 404, "width": 640, "height": 480 },
        "maximized": false,
        "returnTarget": { "groupId": "dock.tabs.bottom", "index": 1 },
        "root": {
          "kind": "tabs",
          "id": "dock.float.1.tabs.1",
          "panels": ["editor.console"],
          "activePanel": "editor.console"
        }
      }
    ]
  }
}
```

**节点编码**

- `kind` 只允许 `split` / `tabs`，未知类型拒绝；
- `split` 必须恰有两个非空子节点、`orientation` 为 `horizontal`/`vertical`、
  两个权重有限且为正且和为 1（容差 1e-6）；
- `tabs` 必须至少一个面板、`activePanel` 必须属于 `panels`、`panels` 内不得重复；
- 节点 ID、面板实例 ID、浮动根 ID 在主根与全部浮根之间全局唯一；
- 主根和每个浮根都必须非空。

**旧 Shell 字段**

`windowWidth/height` 与 `themeId` 继续有效。`hierarchyWidth`、`inspectorWidth`、
`consoleHeight`、`showHierarchy/showInspector/showConsole` 保留为 v1 兼容字段，
D7 不再用它们重建 Dock 结构；隐藏面板不在 D7 范围。Dock 树是面板结构的唯一
权威。

**浮动几何：正常 bounds 与最大化状态分离**

D6-B 让 `DockFloatRoot.Bounds` 表示“最近一次上报的窗口几何”。窗口最大化时
该值可能是工作区几何，不能作为恢复后的正常尺寸。D7-D 要求最大化状态恢复时
“最大化与 Normal Bounds 均符合契约”，因此 D7-B 对模型做**唯一修订**：

- `DockFloatRoot` 增加 `RestoreBounds`（正常/还原几何，DIP）；
- 新建浮根时 `RestoreBounds = Bounds`；
- `UpdateFloatBounds(bounds, maximized)`：
  - 始终更新瞬时 `Bounds` 与 `Maximized`，保持 D6 行为；
  - 当 `maximized == false` 时同时更新 `RestoreBounds`；
  - 当 `maximized == true` 时不覆盖 `RestoreBounds`；
- 持久化只写 `RestoreBounds`（JSON 字段 `bounds`）与 `maximized`；
- 恢复时以 `RestoreBounds` 创建窗口，再应用最大化状态；
- 工作区摘要升级为 W2：主根摘要 + 按 ID 排序的
  `(rootId, rootDigest, RestoreBounds, Maximized, ReturnTarget)`。
  **瞬时 `Bounds` 不进入摘要**，否则“最大化后保存、重启恢复”无法得到相同摘要。

这是 D7 对 D6 冻结模型的唯一修改；D6 的运行时代偿、Revision、令牌和拖放语义
不变。

**不持久化**

- `Revision`（恢复后重新从 0 开始计数；加载阶段候选合并按模型操作递增）；
- `Digest`（加载时重算，并与序列化内容的规范模型比较）；
- 宿主 `Generation`、`DockApplyToken`、HWND/窗口句柄、可见性、Z 序、焦点；
- 拖放会话、预览、源/目标命中快照、投影缓存；
- 时间戳、随机 ID、绝对路径。

**确定性**

- 浮根按 `FloatRootId` 序数升序写出；节点子节点顺序即模型顺序；
- 数字使用不变文化的最短往返表示，`-0` 归一为 `0`；
- 同一工作区重复序列化必须得到相同字节（测试门禁）；
- 反序列化后重算的 `WorkspaceDigest` 必须与保存前一致。

### D7-A-2 解析、校验、迁移与回退

**加载管线**

1. 读取文件；文件大于 1 MiB 直接拒绝（防止恶意/失控输入）。
2. JSON 解析深度上限 256（树深度本身由模型限制为 64）。
3. 按 `schemaVersion` 分派：
   - `1`：显式迁移，保留全部 Shell 字段，Dock 使用默认工作区；**不隔离**；
   - `2`：解析 Shell 字段与可选 `dock`；
   - 其他：不支持，按损坏处理。
4. 构造候选 `DockWorkspace`：所有节点、面板、浮根、bounds、权重、上限和
   全局唯一性必须通过现有 `DockWorkspaceValidator`；候选工作区只在此后交给宿主。
5. 编辑器层校验面板可解析性：D7 只接受当前内置面板
   `editor.hierarchy` / `editor.scene` / `editor.inspector` / `editor.console`。
   其它面板 ID 属于“本版本不可解析”，不作为损坏文件处理（见下表）。
6. 重算摘要，仅用于内存与门禁比较，不写回文件。

**无效输入**

| 情况 | 结果 | 文件处理 |
| --- | --- | --- |
| 文件不存在 | 默认 Shell + 默认 Dock，`UsedFallback=true` | 不创建文件 |
| v1 文件 | Shell 字段按 v1 迁移，默认 Dock，`UsedFallback=false` | 保留；下次保存升级为 v2 |
| v2 缺少 `dock` | Shell 字段有效，默认 Dock | 保留 |
| JSON 损坏、读取失败、schema 未知 | 整个布局回退默认，记录原因 | 隔离为 `.corrupt-<timestamp>`（沿用现有语义） |
| `dock` 结构/模型非法（重复 ID、空组、活动标签非法、权重非法、bounds 非法、超限、未知 kind） | 整个布局回退默认，记录诊断 | 隔离为 `.corrupt-<timestamp>` |
| 结构合法但包含本版本不可解析的面板 ID | Shell 字段保留，Dock 回退默认；记录原因 | **不隔离**（文件可能对未来版本有效） |
| 保存写入失败 | 内存布局与健康状态不变 | 保留上一份有效文件，清理 temp |

D7-D 补充：读取失败时的隔离仍是 best-effort；若文件被独占锁定则隔离失败且
文件保持原样。读取失败与“本版本不可解析面板”一样会抑制自动保存和退出快照，
直到显式保存成功（见 D7-D 记录）。

**明确不做**

- 不对非法树做静默修复（不补空组、不丢弃面板、不重排权重）；
- 不从未来 schema 猜测迁移；
- 不把默认布局在加载失败时立即写回文件；只有后续正常保存触发才写。

### D7-A-3 保存时机、防抖与健康门控

**可保存条件（persistable）**

同时满足才允许写入：

- 主宿主健康（`Current` 为健康投影）；
- 全部浮动宿主健康且可见；
- 没有进行中的拖放会话；
- 工作区没有未完成的加载恢复/重试；
- 协调器不在关机状态。

不健康时：跳过本次写入，保留上一份有效文件，记录诊断；**不改变宿主健康状态、
不合并浮根、不修改模型**。健康恢复后，若仍有未保存变更，重新安排一次防抖保存。

**防抖**

- Dock 模型提交（`Revision` 变化，包括 bounds 更新）触发 500 ms 防抖；
- 连续提交合并为一次写入；写入前比较当前 `WorkspaceDigest` 与上次成功写入的
  摘要，相同则跳过；
- 拖动期间不写入；拖动取消或提交后按需重新安排；
- 只有一个待写快照（latest-wins）；文件写入仍由现有 `SemaphoreSlim` 串行化，
  保证旧快照不会覆盖新快照；
- 加载阶段发生的候选合并（见 D7-A-4）标记为脏，但**不在启动时立即写回**；
  下一次正常保存或退出时写入。

**显式保存**

菜单/快捷键 `Save Layout` 立即取消防抖并尝试写入；同样受健康门控。保存结果
显示在状态栏；失败不回滚布局。

**关闭顺序（对 D6-D 的明确补充）**

1. 同步取消防抖定时器与进行中的拖放会话（取消不提交模型）。
2. 检查可保存条件。
3. 可保存时，在 UI 线程同步捕获不可变的
   `WorkspaceLayout` 快照（Shell 实测尺寸 + 当前 `DockWorkspace`）。
4. 同步调用 `BeginShutdown`：浮窗关闭、不再触发回停事务、工作区模型不再提交。
5. 异步原子写入步骤 3 捕获的快照；等待完成后关闭主窗口。
6. 不可保存或写入失败时：跳过/记录，仍继续关机；上一份有效文件保留。

快照在 `BeginShutdown` 之前捕获，因此保存内容就是关机前的真实工作区；写入可以
在宿主关闭后完成，不会把浮根自动合并回主根。D6-D 的“先 BeginShutdown”顺序
保持不变，只是明确最终保存的快照必须更早捕获。

**保存失败**

- 不改变布局健康状态；
- 不覆盖上一份有效文件（原子替换失败时旧文件仍在）；
- 保留“脏”标记，下一次防抖/显式保存/退出继续尝试；
- 不进入自动重试循环。

**退出语义**

- 浮根不会因退出被合并；文件中保留浮动根及其 `bounds/maximized/returnTarget`；
- 用户在浮窗 X 上明确回停后再退出，文件中是已合并的主根布局；
- 显式关闭面板后，文件中不再包含该面板。

### D7-A-4 启动恢复流程与加载阶段故障

启动时**先验证候选，再创建任何 Dock 宿主**。首帧不阻塞：Shell 可先显示
非交互占位，读取/解析完成后再创建真实宿主；恢复完成前不接受拖放或布局操作。

顺序：

1. 异步读取并解析布局，得到 Shell 字段与候选 `DockWorkspace`。
2. 执行 D7-A-2 的模型校验与面板可解析性检查。
3. 处理浮动根（按 ID 升序）：
   - 创建隐藏浮宿主并执行 D6 的 `Prepare`（候选根校验）；
   - 创建或 `Prepare` 失败：**不注册宿主**，在候选工作区内把该浮根按
     `ReturnTarget`（组不存在时取主根第一个标签组）合并回主根，移除浮根，
     重新校验候选；记录诊断并继续处理下一个浮根；
   - 这是加载阶段的候选修改，不调用运行时的回停/补偿事务。
4. 若某个浮根合并失败或最终候选无效：丢弃已创建的加载宿主，整个布局回退
   默认，记录原因；不留下半创建的窗口或视图。
5. 创建真实主宿主并应用最终主根；主宿主 `Apply`/`Show` 失败按 D6 运行时
   语义降级（可见 Retry），不阻止浮根恢复、不回滚候选。
6. 创建最终浮动宿主，应用根并显示；初始几何使用 `RestoreBounds` +
   `Maximized`。`Apply`/`Show` 失败按 D6 运行时语义保留浮根、标记不健康、
   主 Shell 提供 Retry；**不在加载阶段重新合并**。
7. 校验每个宿主的当前令牌/摘要与其根一致后，恢复才算完成；此后启用防抖保存。
   未完成前不写入文件。
8. 加载阶段发生过合并的工作区标记为脏；退出或下一次正常保存时写入恢复后的
   布局。

**面板视图所有权**：恢复只创建当前工作区中出现的面板视图；不预创建未出现的
面板；工厂调用次数必须等于面板数，恢复/合并/重试不得重复创建或提前释放。

### D7-A-5 加载阶段与运行时故障语义对照

| 阶段 | 故障 | 处理 |
| --- | --- | --- |
| 加载 | JSON/schema/模型非法 | 整个布局回退默认；隔离无效文件；不创建候选宿主 |
| 加载 | 面板 ID 本版本不可解析 | Shell 字段保留，Dock 回退默认；不隔离文件 |
| 加载 | 浮动宿主创建/`Prepare` 失败 | 在候选工作区内合并回主根；重新校验；无运行时补偿 |
| 加载 | 合并失败/最终候选无效 | 丢弃加载宿主，整个布局回退默认 |
| 加载 | 主/浮宿主 `Apply`/`Show` 失败 | 进入 D6 运行时降级：模型保留、宿主不健康、Retry 可见 |
| 运行时 | 任何宿主故障 | 严格沿用 D6-C/D：模型先提交、逐宿主降级、不整体回滚、宿主级令牌与 Retry |

两条路径不得混用：加载阶段不得调用运行时回停/补偿来“修复”候选；运行时不得
因为宿主失败而把浮根静默合并回主根。

### D7-A-6 数据保护、并发与边界

- **原子写入**：沿用 temp + atomic replace；进程内单写者；失败清理 temp。
- **损坏隔离**：沿用 `.corrupt-<timestamp>`；隔离是诊断行为，不能阻止启动。
- **没有独立备份**：D7 不新增 `layout.json.bak`。上一份有效文件的保护依赖
  原子替换（写失败不替换）。若未来需要 last-known-good 自动回滚，另立契约。
- **并发实例**：D7 不支持两个编辑器实例同时写同一文件；没有文件锁/租约，
  最后一次原子替换获胜。此限制写入文档，不作为静默保证。
- **加载只读**：加载路径不写文件，唯一例外是隔离损坏文件；不把默认布局
  立即写回。
- **上限**：文件 ≤ 1 MiB；树深度 ≤ 64；总节点 ≤ 512；总面板 ≤ 256；
  浮根 ≤ 16；坐标/尺寸 ±32768 且宽高为正。
- **隐私**：文件只包含面板身份、结构、几何和主题 ID，不含 HWND、路径或用户
  数据。
- **失败可见性**：加载回退原因、保存失败、健康门控跳过都必须进入现有状态栏/
  日志，不允许静默吞掉。

### D7-A-7 门禁计划

**D7-B 纯模型/序列化（`Nova.UI.Core.Tests`）**

| 场景 | 通过条件 |
| --- | --- |
| 默认与复杂工作区往返 | 字节确定；节点/面板/浮根 ID、顺序、活动标签、权重、bounds、maximized、returnTarget 全部保持 |
| 摘要一致 | 往返后重算 `WorkspaceDigest` 与保存前一致（W2） |
| 最大化/正常 bounds | 保存最大化浮根后恢复：`Maximized=true` 且 `RestoreBounds` 为最大化前正常几何 |
| v1 迁移 | Shell 字段保留，Dock 为默认；不隔离 |
| v2 缺少 dock | Shell 字段保留，Dock 为默认 |
| 非法数据 | 重复 ID、空组、非法活动标签、非法权重、非法/非有限 bounds、超限、未知 kind/orientation、坏 returnTarget 全部拒绝 |
| 未知 schema | 回退默认并隔离 |
| 文件保护 | 序列化/校验失败不改变输入文件；无 temp 残留 |

**D7-C 接线与假宿主（Integration/Controls）**

| 场景 | 通过条件 |
| --- | --- |
| 防抖保存 | 连续变更合并为一次写入；摘要未变则跳过 |
| 拖动期间 | 不写入；取消/提交后按需写入 |
| 退出保存 | 快照在 `BeginShutdown` 前捕获；文件包含浮根；浮根未被自动合并 |
| X 回停后保存 | 文件是已合并布局；恢复不再出现该浮窗 |
| 显式关闭后保存 | 已关闭面板不出现在文件中 |
| 健康门控 | 任一宿主降级时不写入；健康恢复后可写入 |
| 保存失败 | 上一份有效文件不变；temp 清理；健康状态不变；退出不被阻塞 |
| 加载阶段宿主创建失败 | 候选内合并、最终工作区合法、无泄漏宿主/视图 |
| 运行时宿主失败 | D6 语义不变（模型保留、Retry、令牌隔离） |
| 视图所有权 | 工厂调用数等于面板数；恢复/合并/重试不重复创建或提前释放 |

**D7-D 真实进程与桌面（必测）**

| 场景 | 通过条件 |
| --- | --- |
| 多根保存/重启 | 新 PID 启动后主树与浮根结构、面板身份/顺序/活动标签正确 |
| 浮窗 bounds/maximized | 真实 OS 浮窗的 `RestoreBounds` 与最大化状态正确；取消最大化回到保存前的正常几何 |
| 退出含浮窗 | 保存时未合并；新进程恢复出浮窗 |
| X 回停后退出 | 新进程恢复的是合并后的主根布局 |
| 显式关闭面板 | 新进程不出现该面板 |
| 损坏/写入失败 | 上一份有效文件保留；损坏文件隔离；应用可启动 |
| 真实宿主 | 恢复使用新进程创建的 OS 窗口，不复用旧进程 HWND |

- 完整 Debug/Release 回归、`dotnet format` 与警告记录沿用现有纪律；
- 单显示器 100% DPI 为必测；多显示器、混合 DPI、跨 DPI 只记录实际环境，
  不从单显示器或 headless 结果推定覆盖；
- 新进程恢复必须使用真实 `Nova.Editor.App` 与真实浮动窗口，不能只比较 JSON。

### D7-A-8 变更边界

- D7-A：仅本 ADR；不修改源码或测试。
- D7-B：`Nova.UI.Core` 的 `WorkspaceLayout` v2、Dock 快照 DTO、序列化/校验、
  `DockFloatRoot.RestoreBounds` 与 W2 摘要；只改 `Nova.UI.Core.Tests`。
- D7-C：`Nova.Editor.Services`（持久化接线/防抖/健康门控）、
  `Nova.UI.Avalonia`（协调器恢复入口、初始浮根宿主、关闭快照顺序）、
  `Nova.Editor.Views`/`Nova.Editor.App`（启动占位、恢复接线）及对应门禁。
- D7-D：持久化门禁、跨进程/真实桌面证据、ADR/记忆记录。
- 全程不修改 `editor_avalonia`、`native/gate` 或其他无关 dirty 文件；
  工作树保持未提交；不执行 commit、push、tag 或 merge。

## 待确认项（D7-A 审查）

以下为契约中的关键裁决，审查通过后按此冻结：

1. **同一文件扩展 v2**：Shell 偏好与 Dock 工作区同文件、同一次原子写入；
   `dock` 可缺省（缺省 = 默认工作区），不引入第二个布局文件。
2. **唯一模型修订**：`DockFloatRoot` 增加 `RestoreBounds`，工作区摘要升级为
   W2 并排除瞬时 `Bounds`；这是为了让最大化状态与正常几何都能正确恢复。
3. **防抖与健康门控是新增能力**：现有 L2 没有防抖、独立备份或持久化健康门控；
   D7-C 新增 500 ms 防抖与“主/浮宿主全部健康才写”的门控，复用原子写入与隔离。
4. **关闭顺序**：可保存时先在 UI 线程捕获不可变快照，再 `BeginShutdown`，
   然后异步写入快照；不可保存或写入失败不阻塞退出、不合并浮根。
5. **回退策略**：v1/缺少 `dock` 迁移为默认 Dock；`dock` 非法按损坏文件整体
   回退并隔离；仅面板 ID 本版本不可解析时保留 Shell 字段、Dock 回退默认且
   不隔离。
6. **加载阶段浮根失败**：只在候选工作区内按 D6 回停规则合并回主根；
   运行时的 `Apply`/`Show` 失败仍按 D6 降级/Retry，不在加载阶段合并。
7. **面板范围**：D7 只恢复四个内置编辑器面板；未解析面板生命周期、隐藏面板
   和布局 Undo 不在本阶段。
8. **无独立备份**：D7 不新增 `.bak`；写失败靠原子替换保留旧文件。若需要
   last-known-good 自动恢复，须单独裁决。

## 后果与风险

- 需要在启动时把 Dock 宿主创建推迟到布局验证之后，`EditorShellView` 的构造/
  加载时序会改变；首帧显示非交互占位。
- 协调器需要新增“从已验证候选恢复”的入口，并在加载阶段逐浮根处理宿主创建
  失败；D6 的运行时事务顺序不被复用为加载恢复。
- 摘要 W2 会改变现有摘要值（测试不应硬编码摘要字符串）；D6 的运行时语义不变。
- 500 ms 防抖和健康门控是新的持久化行为；不健康期间用户变更可能延迟到恢复后
  或退出时写入。
- 不支持多实例并发写同一文件；需要在文档/状态中明确最后写入获胜。
- 多显示器/混合 DPI 仍是已知缺口，D7 不宣称覆盖。

## D7-B 实施记录（2026-10-26）

**结论**：D7-B 已完成，范围严格限定为纯数据序列化/反序列化与恢复候选校验；
未接入真实宿主、保存调度、防抖、健康门控或退出流程（均属 D7-C）。

**代码变更**

| 文件 | 变更 |
| --- | --- |
| `src/Nova.UI.Core/Docking/DockFloatRoot.cs` | 新增 `RestoreBounds`（可选构造参数，默认等于 `Bounds`）；`Bounds` 保留 D6 瞬时语义 |
| `src/Nova.UI.Core/Docking/DockWorkspace.cs` | `UpdateFloatBounds` 仅在非最大化时写 `RestoreBounds`；所有浮根重建路径经 `RebuildFloatRoot` 保留该值 |
| `src/Nova.UI.Core/Docking/DockWorkspaceValidation.cs` | 校验 `RestoreBounds` 有效 |
| `src/Nova.UI.Core/Docking/DockWorkspaceDigest.cs` | 摘要升级为 `W2|...`，使用 `RestoreBounds`，排除瞬时 `Bounds` 与 `Revision` |
| `src/Nova.UI.Core/Layout/DockWorkspaceSnapshot.cs` | 新增扁平节点 DTO、`DockSnapshotResult` 与 `DockWorkspaceSnapshotCodec` |
| `src/Nova.UI.Core/Layout/WorkspaceLayout.cs` | `CurrentSchemaVersion = 2`；新增可空 `Dock` |
| `src/Nova.UI.Core/Layout/LayoutValidator.cs` | 归一化时保留 `Dock` |
| `src/Nova.UI.Core/Layout/WorkspaceLayoutSerializer.cs` | v2 序列化/反序列化、v1 迁移、`LayoutFallbackKind` 与 `ShouldQuarantine` |

**行为要点**

- 序列化前先经 codec 转回模型再规范化输出：浮根按 ID 排序、`-0` 归一、
  仅写 `RestoreBounds` 为 `bounds`、忽略运行时字段。
- 反序列化严格拒绝未知 kind/orientation、跨 kind 字段、缺失子节点/权重/
  面板/活动标签/回停目标、负索引、非法 bounds、重复/跨根冲突 ID 与超限。
- `Revision` 恢复为 0，不参与内容摘要；相同工作区重复序列化字节一致。
- v1 与缺少 `dock` 均迁移为默认 Dock 且不隔离；非法 Dock 整体回退默认并
  可隔离；未知面板保留 Shell、Dock 回退默认、`ShouldQuarantine=false`。
- 序列化遇到非法或含未知面板的 Dock 快照时抛出 `DockModelException`，
  不静默丢数据。

**D7-B 门禁结果**

新增 13 个 Core 测试：4 个 `RestoreBounds`/W2 用例（`DockWorkspaceTests`）
与 9 个 `D7b_*` 用例（`DockWorkspaceSnapshotTests`），覆盖字节确定性往返、
浮根顺序、W2 摘要、v1/缺省迁移、未知面板不隔离、11 类非法 Dock、
损坏/未知 schema 区分、上限与跨根冲突、序列化拒绝非法快照。

| 命令 | 结果 |
| --- | --- |
| `dotnet test Nova.sln -c Debug --no-restore` | 219/219 通过（Core 83、Controls 55、Integration 76、Performance 5） |
| `dotnet test Nova.sln -c Release --no-restore` | 219/219 通过（同上分布） |
| `dotnet format Nova.sln --verify-no-changes --no-restore` | 退出码 0（仅有 workspace 加载提示） |

警告记录：上述每次测试运行各出现 **7 条 `NU1900`**，均来自不可达的
`https://nuget.cdn.azure.cn/v3/index.json`；这是漏洞源不可达警告，
**不称为零警告构建**。

**移交 D7-C 的已知接线缺口（本阶段未修改）**

1. `LayoutPersistenceService.LoadAsync` 仍按 `UsedFallback` 决定隔离，需改为
   `ShouldQuarantine`，否则未知面板文件会被错误隔离。
2. `LayoutPersistenceService.SaveAsync` 在 try 之外调用 `Serialize`，新的
   `DockModelException` 会逃逸；需纳入失败返回。
3. 加载管线尚未执行 ≤1 MiB 文件上限检查。
4. `DockWorkspaceCoordinator` 仍拒绝带浮根的初始工作区；
   `EditorShellView` 仍在布局加载前创建宿主；`MainWindow` 关机顺序仍需按
   D7-A-3 调整为“先捕获快照，再 `BeginShutdown`”。
5. 现有 L2 仍无防抖、持久化健康门控与最终退出保存，均由 D7-C 新增。

## D7-C 实施记录（2026-10-26）

**结论**：D7-C（C1–C5）已实现并通过 Debug/Release 全量门禁，等待验收。工作树
保持未提交；未新增 `.bak`、未重写既有原子写入、未扩展第三方/隐藏面板。D7-D
仅为满足 D7-C 的独立进程要求提前实现了“真实进程重启”子集，其余 D7-D 门禁
仍未完成、未宣称通过。

### 代码变更

| 文件 | 变更 |
| --- | --- |
| `src/Nova.UI.Avalonia/Docking/DockWorkspaceCoordinator.cs` | 新增静态 `Restore` 加载阶段事务：校验候选 → 创建隐藏浮宿主并 `Prepare` → 创建/Prepare 失败在候选内按返回目标合并 → 重新校验；发生合并时 Revision 回拨到候选原值；随后创建主宿主及最终浮宿主，浮窗使用 `RestoreBounds` + `Maximized`；`Apply`/`Show` 失败沿用 D6 降级，不合并浮根。公共构造函数拒绝带浮根的工作区 |
| `src/Nova.Editor.Services/Layout/LayoutPersistenceService.cs` | 读取前执行 1 MiB 上限；隔离统一由 `ShouldQuarantine` 决定；序列化/规范化移入 `SaveAsync` 的失败边界，在任何 temp 文件创建或旧文件替换之前完成 |
| `src/Nova.Editor.Services/Layout/DockPersistenceContracts.cs` | 新增 `IDockPersistenceSource` 与不可变 `DockPersistenceCapture`（可写性、原因、Revision、快照） |
| `src/Nova.Editor.Services/Layout/LayoutSaveScheduler.cs` | 新增 500 ms latest-wins 防抖；代次守卫丢弃过期定时器，并在请求方 `SynchronizationContext` 上触发 |
| `src/Nova.Editor.Services/EditorWorkspace.cs` | 自动保存生命周期：显式保存取消待写防抖；序列化规范内容去重；未保存脏标记；未知面板/恢复失败抑制；健康门控；退出快照捕获与最终保存；保存结果事件 |
| `src/Nova.Editor.Views/EditorShellView.axaml.cs` | 构造阶段不再创建 Dock 宿主；`InitializeAsync` 先异步加载/验证布局，再按候选创建真实宿主；加载合并或恢复失败标记脏/抑制；卸载时安全释放 |
| `src/Nova.Editor.Views/Docking/EditorDockWorkspace.cs` | `Capture` 校验宿主数、可见性、健康、当前投影、宿主令牌与根摘要，并拒绝拖放/重试/关机中的快照 |
| `src/Nova.Editor.Views/ViewModels/EditorShellViewModel.cs` | 显示保存成功/跳过/失败状态；显式保存走 `userInitiated` 路径 |
| `src/Nova.Editor.App/MainWindow.cs` | 关闭固定为：等待初始化 → 取消交互并捕获快照 → `BeginShutdown` → 异步写快照 → 无论成败关闭；浮根不会被退出流程合并 |
| `src/Nova.Editor.App/App.axaml.cs` | 新增生产组合根选项 `--layout-file <path>`，用于隔离真实进程的布局文件；默认路径不变，不是测试专用伪入口 |
| `tests/Nova.Editor.IntegrationTests/DockPersistencePolicyTests.cs` | 新增 12 个保存/退出/隔离策略门禁 |
| `tests/Nova.Editor.IntegrationTests/RealProcessRestoreTests.cs` | 新增真实进程门禁：两次独立启动重建最大化浮窗；以及锁定布局文件后退出的保存失败/不阻塞退出门禁 |
| `tests/Nova.UI.Controls.Tests/DockWorkspaceRestoreTests.cs` | 新增 3 个加载阶段 vs 运行时宿主故障语义门禁 |
| `tests/Nova.Editor.IntegrationTests/DockDragTests.cs` | 新增真实拖放会话中的 `Capture` 阻止门禁（生产调用链，非伪入口） |
| `tests/Nova.Editor.IntegrationTests/DockShellIntegrationTests.cs` | Shell 测试改为等待 `InitializeAsync`，并验证初始化前没有 Dock 宿主/内容 |
| `tests/Nova.Editor.IntegrationTests/Nova.Editor.IntegrationTests.csproj` | 以 `ReferenceOutputAssembly=false` 建立对 `Nova.Editor.App` 的仅构建依赖 |

### 关键语义

**C1 启动恢复顺序**

1. `InitializeLayoutAsync` 完成后才创建任何 Dock 宿主；初始化前 `DockRoot.Content`
   与 `EditorDockWorkspace` 均为空。
2. `Restore` 在候选状态内处理浮根创建/`Prepare` 失败，逐根合并并重新校验；
   这是加载事务，不调用运行时回停/补偿。
3. 加载阶段合并会递增候选 Revision；协调器采用最终候选时回拨到原始候选
   Revision，因此启动合并不表现为运行时 Revision 前进。
4. 只有主宿主与全部浮宿主都 `Apply`/`Show` 成功且令牌/摘要匹配后，正常防抖
   持久化才被视为可用。
5. `Apply`/`Show` 失败属于运行时 D6 语义：模型保留、宿主降级、Retry 可见，
   **不**在加载阶段静默合并浮根。

**C2 防抖、健康门控与去重**

- 500 ms 防抖，latest-wins；同一时刻只保留一个待写快照。
- 显式保存立即取消防抖并直接尝试写入。
- 写入前比较规范序列化内容；内容未变返回 `Skipped`，不重写文件。
- 健康门控检查宿主数量、可见性、`IsHealthy`、当前投影 `IsCurrent`、令牌
  HostId/Digest 与已提交根摘要，而不是只看 `DockWorkspace.Digest`。
- 拖放、重试、关机、加载未收敛时 `Capture` 返回阻止原因；不改变模型/宿主健康。
- 模型健康、宿主健康与持久化成功是独立状态；保存失败保留脏标记与旧文件，
  不进入自动重试循环。
- 拖动/尺寸预览的连续提交只会在静默 500 ms 后写入一次；退出快照在关机前
  捕获，关机后不再接受正常布局操作或定时保存。

**C3 未知面板与保存失败保护**

- 未知面板：保留 Shell 偏好，Dock 回退默认，文件**不隔离**；自动保存被抑制，
  直到用户显式保存或重置布局。
- 恢复阶段候选被整体拒绝时同样抑制自动保存，避免用默认布局覆盖结构有效的
  原文件；显式保存可解除抑制。
- `SaveAsync` 先规范化并序列化候选，再创建 temp 或替换文件；序列化失败不会
  产生新文件或 temp 残留。

**C4 退出快照**

`OnClosing` 首次关闭始终取消并走异步序列：完成初始化 → 取消拖放/重试交互并
测量 Shell → 捕获不可变快照（此时宿主仍存活）→ `BeginShutdown` 关闭浮窗且
不回停 → 异步写入已捕获快照 → 无论成功、失败或初始化异常都执行最终关闭。
最终保存失败只记录日志，不阻塞退出，也不会重新读取模型或合并浮根。

### 门禁结果

**真实进程重启（C5 必测）**

`RealProcessRestoreTests` 在当前单显示器 Windows 桌面实际启动了两个独立的
`Nova.Editor.App.exe` 进程（不同 PID），使用真实临时 `--layout-file`：

1. 预写 schema v2：主根 + 一个最大化的浮根，`RestoreBounds` 为
   `(180,160,720,500)`，浮根标签顺序 `[editor.console, editor.scene]`，活动标签
   `editor.console`，文件故意带非规范空白。
2. 第一进程：观察到真实主窗口与真实浮窗（标题 `Nova — Console`），
   `IsZoomed` 为真；发送 `WM_CLOSE` 后正常退出；文件被重写为规范 JSON，
   Shell 宽度 236 与完整 Dock 结构保持不变。
3. 将 Shell 宽度改为 252 并再次写入非规范空白，启动第二进程；同样观察到
   最大化真实浮窗，正常退出；文件再次规范化，Shell 宽度 252 与同一 Dock
   结构保持不变。

该门禁验证了主/浮根、浮根 ID、面板顺序、活动标签、`maximized`、
`RestoreBounds`、真实 OS 窗口恢复，以及“新进程从磁盘状态恢复而非复用旧进程
内存”的要求。

另一个真实进程门禁在浮窗恢复并最大化后，以 `FileShare.None` 独占锁定布局
文件，再发送 `WM_CLOSE`：进程仍以退出码 0 结束；锁定前后文件字节完全一致；
文件中的主根、最大化浮根、面板顺序与活动标签保持不变。该门禁覆盖“退出最终
保存失败不阻塞关闭、不覆盖上一份有效文件、不合并浮根”。

尚未验证的 D7-D 项（取消最大化后的真实正常几何、X 回停后重启、显式关闭面板
后重启、**启动阶段**损坏/占用文件恢复）不在此记录中宣称通过；本次只覆盖了
退出阶段文件被占用。

| 命令 | 结果 |
| --- | --- |
| `dotnet test tests/Nova.Editor.IntegrationTests -c Debug --filter RealProcessRestoreTests` | 2/2 通过（三次真实进程启动） |
| `dotnet build Nova.sln -c Debug` | 退出码 0，**18 Warning(s)**、0 Error(s)，全部为 `NU1900` |
| `dotnet test Nova.sln -c Debug --no-build` | **237/237** 通过（Core 83、Controls 58、Integration 91、Performance 5），退出码 0 |
| `dotnet build Nova.sln -c Release` | 退出码 0，**18 Warning(s)**、0 Error(s)，全部为 `NU1900` |
| `dotnet test Nova.sln -c Release --no-build` | **237/237** 通过（分布同上），退出码 0 |
| `dotnet format Nova.sln --verify-no-changes --no-restore` | 退出码 0（仅 workspace 加载提示） |

警告记录：两个完整构建各 **18 条 `NU1900`**，来自不可达的
`https://nuget.cdn.azure.cn/v3/index.json`，不称为零警告构建；上述两次
`--no-build` 测试运行各出现 **0 条** `NU1900`（未执行 restore/build）。为运行
真实进程门禁而单独构建测试项目时，观察到 **10 条** `NU1900`（5 条 restore +
5 条 build）。均为实际运行结果。

### 已知限制与未完成项

- 仍只在单显示器 96 DPI 环境验证；多显示器、混合 DPI、跨 DPI 坐标未覆盖，
  不从当前结果推定。
- D7-D 其余门禁未完成：X 回停后重启、显式关闭面板后重启、启动阶段损坏/
  占用/写入失败恢复、取消最大化后的真实正常几何、真实桌面交互矩阵。本次
  仅覆盖退出阶段的文件占用与最终保存失败。**该限制已在 D7-D 中补齐，见下。**
- 不新增 `.bak`，不复用/重写原子写入之外的文件保护机制；不扩展第三方/隐藏
  面板；不修改 `editor_avalonia`、`native/gate` 或其他无关 dirty 文件。
- 工作树保持未提交；未执行 commit、push、tag 或 merge。

## D7-D 实施记录（2026-10-26）

**结论**：D7-D 已完成。D7-A 契约与 D7-B 序列化实现未改动，未重写保存调度器，
未新增 `.bak`，未扩大第三方/隐藏面板或布局 Undo 范围。先核对现有退出保存
实现是否已消除“旧快照覆盖最终快照”的竞态；结论是 `EditorWorkspace` 的
`_saveGate` 与 `_shuttingDown` 二次检查已经提供串行化，D7-D 只补充可重复的
故障门禁，不重复实现。

### 代码变更

| 文件 | 变更 |
| --- | --- |
| `src/Nova.UI.Avalonia/Docking/DockFloatWindow.cs` | `IsMaximized` 同时查询 Avalonia `WindowState` 与 Windows 原生 `IsZoomed`，修复真实桌面最大化过渡期将最大化位置写入 `RestoreBounds` 的缺陷 |
| `src/Nova.Editor.Services/Layout/LayoutPersistenceService.cs` | 启动读取失败（`LayoutFallbackKind.ReadFailed`）纳入 `PersistenceSuppressed`：从未被解释的文件不得被自动保存替换 |
| `src/Nova.Editor.Services/EditorWorkspace.cs` | 读取失败时抑制普通自动保存与退出快照；任何回退加载都把 `_lastSavedContent` 置空，避免第一次显式保存被去重跳过；显式保存成功清除抑制 |
| `tests/Nova.Editor.IntegrationTests/DockPersistencePolicyTests.cs` | 新增 4 个门禁：启动读取失败不覆盖原文件且抑制自动保存、显式关闭面板保存/恢复、退出取消待处理防抖、退出最终保存与进行中自动保存串行化且最终快照获胜 |
| `tests/Nova.Editor.IntegrationTests/LayoutPersistenceTests.cs` | 新增 2 个门禁：缺失文件由第一次保存创建、保存命令失败被记录且保留上一份有效文件 |
| `tests/Nova.Editor.IntegrationTests/RealProcessRestoreTests.cs` | 新增 6 个真实进程门禁：启动文件被占用、启动文件损坏、X 回停后重启、显式关闭面板后重启、运行时最大化后重启、取消最大化后重启；测试宿主支持无浮窗/非最大化启动与关闭前故障注入 |

### 关键语义

**D7-D-1 启动读取失败与损坏文件**

- 读取失败（文件被占用、权限等）不等同于损坏：失败内容不会被解析成布局，
  也不会用回退布局覆盖文件。`LoadAsync` 仍会尝试隔离，但在独占锁（本轮真实
  验证场景）下移动失败，因此文件字节不变且不会产生 `.corrupt-*`。自动保存与
  退出快照均被抑制，直到用户显式保存。
- 损坏文件仍按 `ShouldQuarantine` 隔离：原文件移动为
  `.corrupt-<timestamp>` 并保留原始字节；回退默认布局有效，退出时写出新的
  规范文件；后续进程加载该有效文件，不再重复隔离。
- 回退加载不再把“默认布局”当作已保存内容，因此缺失/损坏/读取失败后的第一次
  显式保存一定会执行，而不是被“未变化”去重跳过。

**D7-D-2 X 回停与显式关闭**

- 真实浮窗收到系统关闭（X）后回停到主根；等待文件不再包含浮根后再关闭主窗；
  重启进程不再出现浮窗，Console/Scene 均位于主根。
- 显式关闭经生产门面 `EditorDockWorkspace.RemovePanel`（协调器的显式关闭
  路径；当前 Shell 没有面板关闭 UI，故未新增 UI）写入不含 Console 的布局；
  两次真实进程重启都不重新引入该面板，也不产生浮根。

**D7-D-3 保存失败、待处理防抖与进行中保存**

- 退出时先取消防抖再捕获快照：被取消的旧快照不会在最终保存后写入。
- 在自动保存已经持有真实写入路径时捕获退出快照：最终保存排队等待
  `_saveGate`，自动保存完成后最终快照写入并获胜。门禁通过反射持有生产
  `LayoutPersistenceService._writeGate` 与 `EditorWorkspace._saveGate`，
  没有测试专用伪入口。
- 保存命令失败会写入日志并保留上一份有效文件；退出最终保存失败不阻塞关闭，
  文件字节与内容保持不变；无 `.tmp` 残留。

**D7-D-4 最大化与正常几何（发现并修复真实缺陷）**

- 已有门禁证明最大化状态与 `RestoreBounds` 经过两次真实进程重启保持。
- 新增“正常浮窗在运行时最大化”门禁首次运行时失败：持久化 `bounds.x` 为
  `-8` 而不是 `180`。原因是 Avalonia 先收到平台的位置/尺寸变化，再更新
  `WindowState`；过渡期的 Position 事件以 `windowState == Normal` 上报，
  `UpdateFloatBounds(maximized: false)` 遂把最大化位置写进 `RestoreBounds`。
- 根因修复：`AvaloniaFloatWindow.IsMaximized` 在 Windows 上同时查询
  `WindowState` 与原生 `IsZoomed`。过渡期事件因此始终以最大化状态上报，
  `RestoreBounds` 保持不变；非 Windows 平台只使用 `WindowState`。未修改
  D7-A 契约、D7-B 序列化或模型 API。
- 修复后该门禁验证：最大化后文件为 `maximized=true` 且 `bounds` 仍为
  `(180,160,720,500)`（容差 1 DIP）；重启后窗口仍最大化，退出后几何不变。
- 还原门禁在恢复出的最大化真实浮窗上执行系统还原：文件在关闭前变为
  `maximized=false`，`bounds` 回到 `(180,160,720,500)`；重启后不再最大化，
  且几何与第一次保存完全一致。

### D7-D 门禁结果

| 场景 | 门禁 | 结果 |
| --- | --- | --- |
| X 回停后重启 | `Restored_float_returns_to_the_main_workspace_on_system_close_and_stays_after_restart` | 2 个真实进程；文件无浮根，Console/Scene 在主根，重启不复现浮窗 |
| 显式关闭后重启 | `Explicitly_closed_panel_stays_closed_across_real_process_restarts` + `Explicit_panel_close_is_saved_and_restored_without_reintroducing_the_panel` | 生产门面关闭；2 个真实进程均不出现 Console，无浮根 |
| 启动损坏 | `Corrupt_layout_at_startup_is_quarantined_and_replaced_by_a_valid_default` + 既有隔离门禁 | 原字节进入 1 个 `.corrupt-*`；默认布局有效；第二进程不重复隔离 |
| 启动占用/读取失败 | `Locked_layout_at_startup_is_neither_partially_restored_nor_overwritten` + `Unreadable_layout_at_startup_is_left_untouched_and_suppresses_automatic_saves` | 无浮窗；文件字节不变；无 `.corrupt-*`/`.tmp`；释放锁并退出后仍不覆盖；下一进程正常恢复原浮根 |
| 保存/写入失败 | 既有 `Exit_final_save_failure_...`、`Final_save_failure_...`、`Save_command_failure_...` | 退出不被阻塞；旧文件保留；失败进入日志；无 temp 残留 |
| 最大化保存/重启/还原 | `Maximizing_a_restored_float_persists_state_and_restore_bounds_across_a_real_restart` + `Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart` + `Restarted_editor_process_...` | 修复前最大化门禁失败（`bounds.x=-8`）；修复后最大化与还原均跨重启保持 `RestoreBounds`、`maximized` 与真实窗口状态 |
| 退出待处理防抖 | `Exit_cancels_a_pending_auto_save_and_writes_only_the_shutdown_snapshot` | 被取消的旧快照不写入；只写退出快照 |
| 退出进行中保存 | `Exit_final_save_serializes_with_an_in_flight_auto_save_and_wins` | 最终保存排队；自动保存写 240，最终保存写 260，最终内容获胜 |
| 真实进程恢复 | `Restarted_editor_process_restores_the_persisted_float_and_rewrites_the_layout` + 以上真实进程门禁 | 独立 PID、真实主窗/浮窗、活动标签、最大化/非最大化与几何均符合持久化内容 |

| 命令 | 结果 |
| --- | --- |
| `dotnet test ... --filter RealProcessRestoreTests`（Debug） | **8/8** 通过（15 次真实进程启动），约 42 s |
| `dotnet build Nova.sln -c Debug` | 退出码 0，**18 Warning(s)**、0 Error(s)，全部为 `NU1900` |
| `dotnet test Nova.sln -c Debug --no-build` | **249/249** 通过（Core 83、Controls 58、Integration 103、Performance 5） |
| `dotnet build Nova.sln -c Release` | 退出码 0，**18 Warning(s)**、0 Error(s)，全部为 `NU1900` |
| `dotnet test Nova.sln -c Release --no-build` | **249/249** 通过（分布同上） |
| `dotnet format Nova.sln --verify-no-changes --no-restore` | 退出码 0（仅 workspace 加载提示） |

警告记录：两个完整构建各 **18 条 `NU1900`**，来自不可达的
`https://nuget.cdn.azure.cn/v3/index.json`；不称为零警告构建。两次
`--no-build` 全量测试未执行 restore/build，因此没有新增 `NU1900` 记录。

### 已知限制

- 真实桌面验证仅限已声明支持的 Windows、单显示器、96 DPI；多显示器、混合
  DPI、跨 DPI 未验证，不能从本轮结果推定已支持。
- 显式关闭面板没有 Shell UI 入口；本轮验证的是宿主实际使用的生产关闭门面
  及跨进程持久化恢复，不是鼠标点击 UI。
- 不新增 `.bak`；不支持多实例并发写同一布局文件；隐藏/未解析面板生命周期
  与布局 Undo 仍不在 D7 范围。
- 不修改 `editor_avalonia`、`native/gate` 或其他无关 dirty 文件；工作树保持
  未提交，未执行 commit、push、tag 或 merge。

### 后续阶段裁决

D7-D 完成后，D7 的“多根工作区持久化恢复证据链”已闭合。下一阶段只能在
“隐藏/未解析面板生命周期”与“布局 Undo”之间单独选择其一，不同时扩张范围；
两者都不属于本轮已验收能力。

**验收记录（2026-10-26）**：用户验收通过 D7-D，D7-A 至 D7-D 全部完成。
下一阶段选择“隐藏／未解析面板生命周期”，布局 Undo 暂不启动。验收依据为
本 ADR 记录的 Debug/Release 全量门禁与真实进程证据，不表示用户独立检查源码
或重新运行测试。

## Superseding decision（由 ADR-009 / D8 取代，自 D8-C 起生效）

- 日期：2026-10-26
- 状态：**已生效**（D8-C 于 2026-10-26 实现并通过 Debug/Release 门禁）
- 裁决来源：ADR-009《隐藏／未解析面板生命周期》O-5 与 O-6

**被取代的旧行为（D7 历史事实，保留不改写）**

本 ADR 的 D7-B/C/D 记录描述当时的行为，继续作为历史证据保留：

- 结构合法但包含本版本不可解析面板 ID 的文件：Shell 字段保留、Dock 回退
  默认、不隔离、自动保存与退出快照被抑制；
- 序列化器遇到未知面板 ID 时抛出 `DockModelException`，拒绝写出。

**取代后的新行为（自 D8-C 起）**

- 含未解析面板 ID 且结构合法的 Dock 正常装入工作区；面板身份以占位形式保留，
  **不整树回退、不隔离**；自动保存与退出快照**仍被抑制**，只有显式
  `Save Layout` 才允许写回；
- 序列化器接受未解析 ID，只校验结构、身份、顺序与可见性；显式保存忠实保留
  未解析面板的身份、顺序与 hidden 状态；
- 未知面板永不映射到内置面板；`Close Panel` 等普通操作不得解除持久化抑制；
- W3 摘要不包含解析状态，只包含布局身份与可见性。

D8-C 已按上述行为落地：布局 schema 升级为 v3（`tabs.hidden`），v1/v2 确定性
迁移为全部可见；`LayoutFallbackKind.UnresolvedPanels` 不再产生（实现中已移除
该枚举值），未知面板仅通过 `LayoutLoadResult.UnresolvedPanels` 作为诊断返回。
D7 历史记录保持原样，作为当时行为的证据。

D8-E（2026-10-26）已用真实编辑器进程补齐跨进程证据：含未解析身份的文件经
真实进程退出/重启后字节不变、不被静默覆盖、不隔离（RP-3）；真实 `File >
Save Layout` 菜单把原始未知 ID、顺序与 hidden 状态忠实写入新文件并解除抑制
（RP-4）。记录见 ADR-009 的 D8-E 实施记录。

