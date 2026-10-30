# T-6 Console — Gate A 契约（FROZEN）

**状态**：`Gate A — FROZEN`；`T-6-A — AUTHORIZED (Gate B)`；`T-6-B — NOT AUTHORIZED`
**裁决**：`APPROVED WITH D-5 REQUIREMENTS`（2026-10-29）
**依据**：`docs/T-6-Console-Gate0-Scan.md`（Gate 0 PASS — READ-ONLY SCAN COMPLETE）
**日期**：2026-10-29
**性质**：本文件冻结范围、行为规则、D-5 状态表、测试矩阵与变更白名单。
T-6-A 实现必须严格落在 §10 白名单内；T-6-B 不在本次授权范围。

---

## 0. D-1…D-5 裁决

| # | 决定项 | 裁决 | 冻结要求 |
|---|---|---|---|
| D-1 | 本期范围 | **批准** | 只处理现有 `EV_*` 事件 + ViewModel 本地行；Engine `spdlog` 接入另立 T-6-B |
| D-2 | 缓存容量 | **批准** | 最多 10,000 行，超限淘汰最旧行；边界行同样计数 |
| D-3 | 工程切换历史 | **批准（附状态语义）** | 不因工程切换清空历史；在 native 工程切换提交时追加边界行，并按“完全成功 / 加载后失败”区分标记 |
| D-4 | 过滤 | **批准** | 大小写不敏感的子串匹配（`StringComparison.OrdinalIgnoreCase`）；只影响显示，不改缓存 |
| D-5 | generation 语义 | **批准（两阶段提交）** | 不得提前废弃旧代次；必须区分 native 工程切换是否已提交与整个 OpenProject 是否成功；加载失败与加载后 GL 失败分别处理 |

这五项已冻结；后续实现不得自行扩大或缩小语义。若发现必须偏离，停止并请求重新裁决。

---

## 1. 范围与功能分界

| 能力 | T-6-A（本期） | T-6-B（另行裁决） | 明确不做 |
|---|---|---|---|
| `EV_*` 事件显示 | ✔ | — | 不改事件生产/ABI |
| ViewModel 本地行显示 | ✔ | — | — |
| 容量上限/丢弃策略 | ✔ | — | 不做分段持久化 |
| 清空展示缓存 | ✔ | — | 不清除引擎日志/工程数据 |
| 文本过滤 | ✔ | — | 不做正则、级别、来源过滤 |
| 工程边界分段 | ✔ | — | 不做跨进程持久化 |
| 打开工程 generation 语义 | ✔（修复 + 门禁） | — | 不改 native generation 协议 |
| Engine `spdlog` 日志接入 | — | ✔ | 本期不新增任何日志 ABI |
| 统一 `EV_*` + Engine 日志流 | — | ✔ | 本期不宣称已统一 |
| 源端时间/序号/来源字段 | — | ✔（若 T-6-B 需要） | 不改 `EV_*` payload |
| 日志导出/持久化/折叠/跳转源码 | — | — | 不做 |

**边界声明**：T-6-A 完成后，managed Console 仍然主要显示 `EV_*` 事件与编辑器本地动作，
不是完整 Engine 日志 Console。任何 closure 记录必须原样写明这一点。

---

## 2. A1 — 数据模型与容量

### 2.1 原始缓存

- `MainViewModel.ConsoleLines` 继续作为**原始缓存**（`ObservableCollection<string>`），
  保留现有 gate 的断言面（`Count`、`Skip`、`Contains`）。
- 原始缓存只由 `LogToConsole` 追加；顺序 = 追加顺序。
- **容量 10,000 行**；超过时从最旧端删除，保留最新 10,000 行。
- 事件行、ViewModel 本地行、工程边界行**全部计入**上限。
- 10,000 是**行数上限**，不是内存字节上限；本期不引入单行截断或字节预算。
- 每次裁掉的行数累加到内部诊断计数 `ConsoleTrimmedCount`；该计数**不清空引擎日志**、
  不改变工程状态。清空操作重置该计数。
- 容量是进程级 Console 的属性，不是每工程独立。

### 2.2 显示视图

- UI 绑定一个**派生视图**（下称 `ConsoleView`），而不是直接绑定原始缓存。
- `ConsoleView` 是原始缓存按当前过滤文本的投影：
  - 原始缓存新增一行：若匹配当前过滤，追加到视图；
  - 原始缓存裁剪最旧行：若该行在视图中，移除；
  - 过滤文本变化：重建视图；
  - 清空：两者同时清空。
- 过滤**绝不修改**原始缓存；取消过滤后必须恢复完整视图（受容量约束）。

### 2.3 既有约束

- `ConsoleLines` 的类型与“可被外部 gate 只读枚举”的语义保持不变；
  新增视图不替换原始缓存。
- 不新增每行对象模型（没有级别/来源/序号字段可用），T-6-A 行仍是字符串。

---

## 3. A2 — 清空

| 规则 | 冻结值 |
|---|---|
| 触发 | 真实 `Clear` 按钮（AutomationId `ConsoleClearBtn`） |
| 影响 | 清空原始缓存 + 显示视图；重置 `ConsoleTrimmedCount`；保留当前过滤文本 |
| 不影响 | host 状态、project、revision、dirty、选择、事件订阅、host 计数器、引擎日志文件 |
| 在途事件 | 不清空/不取消已排队事件；清空后到达的事件正常追加，并受过滤约束 |
| 空缓存 | 按钮仍可用；重复清空幂等 |
| 删除历史 | 不保留已删除历史的备份；取消过滤也不会恢复已清空或因容量淘汰的行 |
| 过滤 | 清空后**保留**当前过滤条件（D-2/D-4） |
| 工程生命周期 | 打开/关闭工程**不会**自动清空（见 §5） |

“清空”是展示动作，不是会话动作；不得通过清空触发任何 native 调用。

---

## 4. A3 — 文本过滤

| 规则 | 冻结值 |
|---|---|
| 控件 | 真实 `TextBox`（AutomationId `ConsoleFilterBox`，Watermark `filter console`） |
| 匹配 | 大小写不敏感子串，实现固定使用 `StringComparison.OrdinalIgnoreCase`；输入先 `Trim()`；空串 = 不过滤 |
| 匹配对象 | 行的**消息文本**（去掉 `[HH:mm:ss.fff] ` 前缀）；不匹配时间戳 |
| 作用 | 只影响 `ConsoleView`；原始缓存不变 |
| 恢复 | 清空过滤框后恢复完整视图（受容量约束） |
| 生命周期 | 过滤文本在工程切换、清空、Play/Stop 后保持；进程重启不持久化 |
| 不支持 | 正则、级别过滤、来源/事件类型过滤（元数据不存在）、多条件组合 |

过滤与清空相互独立：清空保留过滤文本；过滤变化不改变原始缓存。

---

## 5. A4 — 时间、顺序与工程生命周期

### 5.1 时间与顺序

- 每行前缀 `[HH:mm:ss.fff]` 是 **UI 接收/生成时刻**（`DateTime.Now`），不是源端事件时间。
- 显示顺序 = 追加顺序；host 事件按 `Dispatcher` 投递顺序显示。
- 本地动作行与异步 `EV_*` 行之间**不承诺因果顺序**；测试不得断言“本地行必须先于/后于 EV 行”。
- T-6-A 不引入序号，也不声称跨线程的全序。

### 5.2 工程生命周期（D-3：分段保留）

边界行必须反映 **native 工程切换是否已提交**，而不是简单复制 `OpenProject` 的最终返回码。

| OpenProject 结果 | Console 行为 | 历史/代次 |
|---|---|---|
| Native 工程加载提交成功，后续流程也成功 | 追加 `[project] opened: <manifestPath>`（**恰好一次**） | 提交候选代次；历史保留 |
| Native 工程加载前失败（旧工程仍有效） | 不追加成功边界；沿用现有 `OPEN FAILED: …` 行 | 旧代次与旧历史继续有效 |
| Native 工程加载已提交，但后续 GL 初始化失败 | 追加 `[project] active: <manifestPath> — GL initialization failed`；不得伪装为完全成功 | **仍提交候选代次**；GL 失败照常上报，但不得回退到旧代次 |
| 调用成功但未收到 `EV_PROJECT_LOADED` | 协议违约：记录诊断，不写成功边界；测试必须失败 | 不提交候选代次；不得报告 PASS |

其余生命周期：

| 事件 | 行为 |
|---|---|
| 关闭工程 | 保留历史；沿用现有 `session closed` 行；bump 代次 |
| 工程间切换 | 按上表追加新边界，旧段保留 |
| 进程重启 | Console 从空开始（不持久化） |
| 容量 | 分段与普通行一起受 10,000 行上限约束；旧段可能被裁掉 |

边界行的位置：在 native 提交确认后、`[WARN]` 行之前追加。
`EV_PROJECT_LOADED` 是异步投递，可能晚于边界行与告警行出现，这是允许的。
边界行不得在 Prepare 阶段提前生成。

---

## 6. A5 — 打开工程的 generation 语义

### 6.1 已核实的时序事实

- `EngineHost::Submit` 在 owner 线程同步执行；非 owner 线程排队并**阻塞等待完成**
  （`bridge/src/EngineHost.cpp:245-296`）。
- `SubmitOpenProject` 的闭包在返回前同步调用 `EditorSession::OpenProject*`；
  成功时在闭包内发出 `EV_PROJECT_LOADED`（`EngineHost.cpp:947-998`；
  `EditorSession.cpp:189-311`，发出点 `:308`）。
- managed `EditorHostService.OpenProject` 在 C ABI 返回**之后**才 `BumpGeneration`
  （`editor_avalonia/Services/EditorHostService.cs:960-976`）。
- `MarshalEvent` 在回调到达时捕获当前 managed generation；posted 闭包再比对
  （`EditorHostService.cs:841-864`）。
- 因此成功打开时 `EV_PROJECT_LOADED` 以旧 generation 入队，随后被 guard 丢弃。`[I]`
  G2-06 的注释也承认排队的真实 native 事件会一并被丢弃（`Hrc2Gate.cs:172`）。
- 打开失败时 `EditorSession::OpenProjectInternal` 在替换场景/注册表之前返回
  （`EditorSession.cpp:240-274`），旧工程保持有效；managed 侧不 bump。

### 6.2 冻结的不变量

| # | 不变量 |
|---|---|
| G-1 | 成功打开工程必须投递**恰好一次**该工程的 `EV_PROJECT_LOADED`；不得计入 dropped |
| G-2 | 成功切换后，旧工程/旧会话的迟到事件仍必须被丢弃并计入 `DroppedLateEventCount` |
| G-3 | **未收到** `EV_PROJECT_LOADED` 的打开失败不得提交新代次；旧工程保持有效，旧代次不变 |
| G-3b | **已收到** `EV_PROJECT_LOADED` 但调用随后失败（GL 初始化失败）时，仍提交候选代次；不得回退到旧代次 |
| G-4 | 关闭/teardown 语义不变：抑制 teardown 事件并 bump 代次 |
| G-5 | `DroppedLateEventCount` 只统计 generation mismatch；不统计 teardown 抑制；不计入成功的 open 事件 |
| G-6 | 重复 open/close 不得重复订阅、不得重复投递同一事件 |
| G-7 | 调用成功但没有 `EV_PROJECT_LOADED` 是协议违约：必须诊断，不得提交候选代次，门禁不得报告 PASS |
| G-8 | 事件归属必须在 native 回调到达时、进入 `Dispatcher.Post` 之前判定；候选代次事件在尝试定案前不得作为已提交事件显示 |

### 6.3 冻结的两阶段提交（D-5）

**Phase 1 — Prepare**：

1. 为本次打开创建唯一 `openAttemptId`；
2. 记录 `activeGeneration`，并创建 `candidateGeneration = activeGeneration + 1`；
3. 打开意图设为 Pending；**不修改** `activeGeneration`，不提前废弃旧工程事件；
4. 同一时刻只允许一个待决尝试；重复/重入必须被拒绝或串行化。

**Phase 2 — Commit / Abort**：以 `EV_PROJECT_LOADED` 作为 native 工程切换已提交的证据，
而不是只看 `OpenProject` 的最终返回码。

| Native 事件与调用结果 | Generation 决策 | 事件处理 |
|---|---|---|
| 收到 `EV_PROJECT_LOADED`，调用成功 | **Commit** 候选代次 | 接受新工程事件；旧工程迟到事件随后被 guard 拒绝 |
| 未收到 `EV_PROJECT_LOADED`，调用在加载前失败 | **Abort**，保留旧代次 | 旧工程仍有效；不得误丢旧工程事件；不追加成功边界 |
| 收到 `EV_PROJECT_LOADED`，调用随后因 GL 初始化失败 | **Commit** 候选代次 | native 工程已切换；上报 GL 失败，但不得回退为旧代次；追加部分失败边界 |
| 调用成功但未收到 `EV_PROJECT_LOADED` | **Abort + ProtocolViolation** | 记录诊断；不写成功边界；门禁必须失败 |

实现约束：

- `EV_PROJECT_LOADED` 本身及其后收到的事件必须与同一 `openAttemptId` 关联；
- 候选事件的代次在回调到达时判定并随事件保存，不能在 Dispatcher 执行时重新读取“当前代次”；
- 候选事件在 commit 前不得投递为普通事件；commit 时按原顺序放行；
- Abort 时候选待决事件不得显示，并按 dropped 诊断计数；
- 边界行只能在 native 提交确认后生成，不能在 Prepare 阶段提前生成；
- 不允许：只把 `BumpGeneration` 提前；为 `EV_PROJECT_LOADED` 特判放行；
  把 guard 放宽成“接受最近 N 个代次”。

### 6.4 已核验的设计前提

- `EngineHost` 有独立 runtime 线程（`RuntimeLoop`）；`Submit` 在非 owner 线程排队并阻塞等待
  （`EngineHost.cpp:245-296,378-396`）。UI 线程在 `OpenProject` 期间阻塞，posted 事件不会在
  commit 之前执行。
- `SubmitOpenProject` 在 native 加载成功后才发出 `EV_PROJECT_LOADED`（`EditorSession.cpp:308`）。
- 加载前失败时旧工程未被替换（`EditorSession.cpp:240-274`），因此 Abort 必须保留旧代次。
- `EngineHost.cpp:984-990` 存在“native 已打开、随后 GL 失败”的路径，因此 Commit 不能依赖返回码。
- 打开期间旧工程是否可能发出事件：正常路径不会（UI 阻塞、native 打开闭包独占 runtime 线程）；
  实现仍需保证 pre-commit 事件使用旧代次，不能归入候选代次。

---

## 7. A6 — 失败与边界语义（T-6-A 冻结）

| 场景 | 行为 |
|---|---|
| 未知事件类型（`>= EV_COUNT`） | 不投影、不崩溃；不新增 Console 行；保持现状计入 `ConsumedEventCount` |
| malformed payload | 原样打印 payload；不解析、不校验 |
| 事件在非 UI 线程被消费 | 保留现有 `EVENT OFF UI THREAD` 行与计数 |
| host fault | 保留现有 `RUNTIME FAULTED: …` 行；不自动清空 |
| teardown 期间事件 | 继续被 `_suppressEvents` 抑制；不计数、不显示 |
| 过滤/清空期间发生 host 失败 | 失败行按正常规则追加；过滤可隐藏它，但原始缓存保留 |

---

## 8. A7 — UI 可测性

| 控件 | AutomationId | 用途 |
|---|---|---|
| Console 列表 | `ConsoleList`（已有） | 显示视图；UIA 读取行 |
| 过滤框 | `ConsoleFilterBox`（新增） | 输入过滤文本 |
| 清空按钮 | `ConsoleClearBtn`（新增） | 清空展示缓存 |

- 工具栏放在 Console Tab 顶部；不改变现有 Tab 结构。
- 真实窗口 gate（`--t6console`）与 UIA 测试都通过生产控件触发，不使用测试专用入口。
- 不允许用 VM 方法调用来替代用户可见的过滤/清空证据。

---

## 9. A8 — 测试矩阵

| ID | 类别 | 通过条件 | 层级 |
|---|---|---|---|
| T6-A-01 | 容量 | 追加 >10,000 行后 `ConsoleLines.Count == 10000`；最旧行消失；最新行存在；`ConsoleTrimmedCount` 正确 | VM + 真实 ListBox 绑定 |
| T6-A-02 | 清空 | 真实按钮清空原始缓存与视图；随后事件/日志仍正常显示；host/dirty/revision 不变 | 真实窗口 + UIA |
| T6-A-03 | 过滤 | 输入子串后视图只剩匹配行；`ConsoleLines` 不变；清空过滤恢复完整视图 | 真实窗口 + UIA |
| T6-A-04 | 过滤+清空 | 过滤状态下清空，视图为空且过滤文本保留；后续匹配行出现、不匹配行不出现 | 真实窗口 |
| T6-A-05 | 打开成功 | 边界行恰好一次；`EV_PROJECT_LOADED` 恰好一次；`DroppedLateEventCount` 不因该事件增加 | 真实窗口 gate |
| T6-A-06 | 工程切换 | 旧代次事件被丢弃且计数增加；新工程事件正常投递并显示 | gate（复用 `PostEventWithGeneration`） |
| T6-A-07 | 加载前失败 | 代次不变；无边界行；无新 `EV_PROJECT_LOADED`；旧工程事件仍有效；历史保留 | gate |
| T6-A-08 | 重复 open/close | 单次订阅；每个事件只显示一行；计数器单调且无重复投递 | gate |
| T6-A-09 | 异步顺序 | 多个 `EV_*` 的相对顺序保持；本地行与 EV 行不承诺先后 | gate |
| T6-A-10 | close/teardown | teardown 事件不显示；`DroppedLateEventCount` 不因抑制而增加 | gate |
| T6-A-11 | UIA 真实进程 | 过滤/清空经真实控件生效；#141 导入消息在清空后仍可出现 | UIA 进程测试 |
| T6-A-12 | 绑定 | ListBox 显示的是过滤视图；原始缓存始终保留 | 真实窗口 gate |
| T6-A-13 | 加载后 GL 失败 | 调用返回失败，但 `EV_PROJECT_LOADED` 已收到 → 候选代次仍提交；无回退；追加 `[project] active: … — GL initialization failed` | gate + 状态表单测 |
| T6-A-14 | 协议违约 | 调用成功但无 `EV_PROJECT_LOADED` → `ProtocolViolated`，不提交代次，门禁失败（不得报告 PASS） | tracker 单测 |
| T6-A-15 | 边界事件时序 | 加载提交点前、后及操作返回后投递事件，归属不受 Dispatcher 执行时序影响 | gate |
| T6-A-16 | 防重复打开 | 第二个打开意图被拒绝/串行化；代次与边界行不交错 | tracker 单测 + gate |
| T6-A-17 | 候选事件暂存 | 候选事件在尝试定案前不显示；commit 后按原顺序显示；abort 后不显示且计入 dropped | tracker 单测 + gate |

D-5 场景不得只检查 `ConsoleLines` 是否增长；必须同时断言 `Generation`、`NativeCommitted`、
`ProtocolViolated`、`DroppedLateEventCount` 与边界标记文本。

**回归**：

- Managed 构建 Debug/Release；
- `--prodpath`、`--t1ui`、`--hrc2`、`--m1import-host` + #141 UIA；
- `test_bridge`（native 事件与 generation 相关契约不被 managed 改动影响）；
- 既有 Console 文本断言 gate（`BootstrapTriggerGate`、`Hrc1Gate`、`Phase3Gate`、
  `ProductionPickGate`、`UiStateGate`、`ProductionPathGate`）不得被改写来迁就新行为。

**证据纪律**：

- Debug/Release 分开记录，不合并；
- 首次失败保留，不以重跑覆盖；
- 不宣称零警告/clean build；
- VM-only 测试不能替代 UIA 对过滤/清空的证据；
- native 证据不能替代 managed UI 证据。

---

## 10. A9 — 变更白名单

允许修改/新增：

| 文件 | 允许内容 |
|---|---|
| `editor_avalonia/ViewModels/MainViewModel.cs` | 原始缓存容量、视图、过滤/清空、边界行、内部诊断计数 |
| `editor_avalonia/Services/EditorHostService.cs` | open-intent generation 语义；不改公开 ABI/事件枚举 |
| `editor_avalonia/MainWindow.axaml` | Console 工具栏（过滤框/清空按钮）与 AutomationId |
| `editor_avalonia/MainWindow.axaml.cs` | 工具栏 handler；`--t6console` gate 与 `--t6ui-host` UIA 宿主注册 |
| `editor_avalonia/Services/T6ConsoleGate.cs`（新增） | T-6-A 真实窗口门禁 + `--t6ui-host` 启动宿主（只打开 scratch，不提供测试旁路） |
| `tests/EditorAvalonia.IntegrationTests/T6ConsoleUITests.cs`（新增） | UIA 过滤/清空/进程测试 |
| `tools/run_t6_console_gate.ps1`（新增） | 构建 + gate + UIA 运行器 |
| `docs/T-6-Console-GateA-Contract.md`、`docs/T-6-Closure-Record.md` | 契约与关闭记录 |

**禁止修改**（未经再次裁决）：

- `bridge/include/editor_bridge/capi.h`、`bridge/src/*`、任何 C ABI；
- `engine/*`（T-6-A 不需要引擎改动）；
- `editor_avalonia/Services/Hrc2Gate.cs` 及既有 gate 的断言语义；
- T-1 / T-2 / T-4 / F2-S1 / GPUProfiler / 音频 / DebugDraw 相关文件；
- `engine/src/Core/ConsolePanel.cpp`（ImGui Console 不在 T-6-A 范围）。

若实现中发现必须越界，停止并请求重新裁决，不得顺手修改。

---

## 11. A10 — 基线保护

- 实现前与关闭时各复算一次 T-2 八个关键文件哈希，全部 **MATCH**；
- `bridge/include/editor_bridge/capi.h` 必须保持
  `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886`；
- 3 个脏子模块与 `crashes/`（115 个产物）不得触碰；
- 不写 repo fixture；测试只使用系统临时目录 scratch，并在 finally 删除；
- 未获授权不执行 commit / push / tag / merge；
- 不执行 reset/clean 或任何丢弃工作树改动的命令。

---

## 12. A11 — T-6-B 延期声明

T-6-B（Engine `spdlog` 日志接入 managed Console）本期不实施、不设计细节、不新增 ABI。
将来立项时必须单独回答：

1. 日志条目字段（级别、分类、源端时间、来源、消息）；
2. Engine → EngineHost → managed 的传输方式（新 ABI / host 回调 / 文件尾随）；
3. 工作线程与 UI Dispatcher 的线程安全和顺序；
4. 高频日志的容量、背压、丢弃统计与 UI 响应性；
5. EngineHost 销毁、工程切换、窗口关闭时的订阅生命周期；
6. 与现有 stdout/file sink 的一致性要求。

在 T-6-B 立项前，任何文档不得声称 managed Console 已统一显示 Engine 日志。

---

## 13. Gate A 放行清单

| 项目 | 状态 |
|---|---|
| Gate 0 报告 | PASS（`docs/T-6-Console-Gate0-Scan.md`） |
| 五个决定 D-1…D-5 | **APPROVED WITH D-5 REQUIREMENTS（2026-10-29）** |
| D-5 状态表 | 已冻结（§6.3） |
| 功能分界表 | 已列出（§1） |
| 行为规则 | 已列出（§2–§7） |
| 测试矩阵 | 已列出（§9，含 D-5 场景 T6-A-13…17） |
| 变更白名单 | 已列出（§10） |
| 基线保护 | 已列出（§11） |
| T-6-B 延期声明 | 已列出（§12） |
| T-6-A 关闭记录 | `docs/T-6-Closure-Record.md`（`PASS — SCOPE-BOUND`） |
| 实现授权 | **T-6-A 已授权（Gate B）；T-6-B 未授权** |

T-6-A 实现必须严格落在 §10 白名单内；发现越界必须停止并重新裁决。
