# T-6 Console — Gate 0 只读扫描

**状态**：`Gate 0 — COMPLETE（只读）`；`Gate A — NOT RELEASED`；`T-6 实现 — NOT STARTED`
**日期**：2026-10-29
**性质**：只读代码与基线扫描。没有构建、没有运行测试、没有启动编辑器、
没有 UIA、没有 Git 写操作、没有修改任何源文件或已有记录。
**未创建**：`docs/T-6-Console-GateA-Contract.md`（Gate A 尚未放行）。

本文件回答三件事：

1. 当前工作区基线是什么（G0-1）；
2. `日志/事件生产者 → Engine Core → Bridge/EngineHost → Managed ViewModel → Avalonia Console`
   这条链路实际存在哪些段、缺哪些段（G0-2）；
3. 现有 Console 与日志/事件能力的边界在哪里，哪些是 Gate A 必须先裁决的问题（G0-3）。

---

## 1. G0-1 工作区与基线

| 项目 | 观测值 | 证据 |
|---|---|---|
| 分支 | `avalonia` | `git branch --show-current` |
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8` | `git rev-parse HEAD` |
| HEAD 提交 | `feat: commit T-2 Content v2 M1-A~F, T-1 RigidBody C/D/E/F, T-4 GPUProfiler` | `git log -1` |
| 暂存区 | 空（0 行） | `git diff --cached --stat` 无输出 |
| `git status --porcelain` | **4 行** | 见下 |
| 已跟踪修改 | 3 个脏子模块：`third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` | `git status --porcelain` |
| 未跟踪 | `?? crashes/`，**115 个产物** | `ls crashes \| wc -l` = 115 |
| 其它改动 | 无（工作树在本次扫描前已被外部 commit 收敛） | `git status --porcelain` 仅上述 4 行 |

T-2 八个关键文件哈希复算（`sha256sum`，2026-10-29 本轮）：

| 文件 | 当前 SHA-256 | 对照 `docs/RF4-Managed-Import-Stage0-Baseline.md` |
|---|---|---|
| `engine/src/Core/Content/RevisionStore.cpp` | `d03bbe87…68b1f2` | **MATCH** |
| `engine/include/Engine/Core/Content/RevisionStore.h` | `aa5c2d83…4f1ec0` | **MATCH** |
| `engine/src/Core/Content/AtomicFile.cpp` | `b7d109df…461eb9` | **MATCH** |
| `engine/src/Core/Content/ProjectPaths.cpp` | `0abfd679…857036` | **MATCH** |
| `engine/src/Core/Content/Utf8Path.cpp` | `dd67d404…521121` | **MATCH** |
| `bridge/include/editor_bridge/capi.h` | `02dbce31…b2886` | **MATCH** |
| `docs/T-2-Closure-Record.md` | `355c513f…62222c` | **MATCH** |
| `tests/test_bridge/test_revision_commit.cpp` | `8c8fd870…e71b3a` | **MATCH** |

基线说明：

- Stage 0（`e9acfb5`）之后，HEAD 已由外部 commit 前进到 `74ad099`；
  本轮不假设历史，只记录当前实际值。
- `capi.h` 哈希与 T-2 基线一致：**#141 与 T-1/T-4 均未改动 Bridge ABI 头**。
- 三个脏子模块与 `crashes/` 115 个产物保持原样，本轮未触碰。

---

## 2. G0-2 现有 Console 与事件链路

### 2.1 Managed Console 的现有形态

| 事实 | 位置 |
|---|---|
| 数据源是 `ObservableCollection<string>`，进程内无上限 | `editor_avalonia/ViewModels/MainViewModel.cs:35` |
| 唯一写入入口 `LogToConsole`，加本地时间前缀 `[HH:mm:ss.fff]` | `editor_avalonia/ViewModels/MainViewModel.cs:1663-1666` |
| Console 页只有一个 `ListBox`（`ConsoleList`），没有工具栏/过滤/清空/搜索 | `editor_avalonia/MainWindow.axaml:209-214` |
| 没有任何 `ConsoleLines.Clear/Remove` 调用；工程关闭不清空 | 全目录检索无匹配；`ResetSession` 只追加 `"session closed"`（`MainViewModel.cs:703-720`） |
| 行内容为单一字符串：无级别、无来源/频道、无事件类型字段、无序号 | `LogToConsole` 本身 |
| 时间戳是 **UI 接收时刻** 的 `DateTime.Now`，不是 native 事件时间 | `MainViewModel.cs:1665` |
| 没有自动滚动、容量裁剪、折叠/去重、导出 | XAML 与 VM 均无对应代码 |

结论：现有 managed Console 是**纯文本追加列表**，不是结构化日志视图。

### 2.2 事件生产者与 ABI

| 事实 | 位置 |
|---|---|
| 生产者为 `EditorSession::Emit`，在 `std::recursive_mutex` 下**同步**调用回调 | `bridge/src/EditorSession.cpp:1538-1541` |
| 事件枚举 `EV_PROJECT_LOADED=1 … EV_COMPONENT_CHANGED=13`，`EV_COUNT=14` | `bridge/include/editor_bridge/capi.h:67-90` |
| payload 是分号分隔字符串（如 `idx=<i>;type=<t>;action=<add\|remove\|set>`） | `capi.h:68-89` |
| 事件发出点覆盖 open/save/create/move/delete/rename/assign/script/asset/play/component | `EditorSession.cpp:308,356,393,430,455,485,537,583,612,636,732,989,1070,1407,1462` |
| 事件没有序号、级别、时间戳字段；`EV_COUNT` 仅用于边界检查 | `capi.h:67-90` |

事件是“编辑操作通知”，不是“日志流”：没有 level/category，也没有“错误日志”通道。

### 2.3 Bridge / EngineHost 传输

| 事实 | 位置 |
|---|---|
| EngineHost 建立运行时会话时注册 native 回调，携带 `m_Generation` | `bridge/src/EngineHost.cpp:498-500` |
| `ForwardEvent` 在回调锁内取回调并调用，同一事件同步转发 | `bridge/src/EngineHost.cpp:1700-1712` |
| teardown/重建路径显式清空回调 | `EngineHost.cpp:609,1414`；`EditorSession.cpp:55` |
| `EngineHost_SetEventCallback` 是唯一事件注册入口；bridge 头中**没有日志回调 API** | `bridge/include/editor_bridge/capi.h`（全文无 Log 接口） |

### 2.4 Managed 宿主接缝（EngineHost 生产后端）

| 事实 | 位置 |
|---|---|
| 启动时把 `OnNativeEvent` 固定为 native 回调 | `editor_avalonia/Services/EditorHostService.cs:699-707` |
| `OnNativeEvent` **忽略 native generation**，读取 managed `_generation` | `EditorHostService.cs:841-842` |
| `MarshalEvent` 先在 `_suppressEvents` 时丢弃，再 `Dispatcher.UIThread.Post` | `EditorHostService.cs:848-864` |
| posted 闭包内比较 captured/managed generation；不一致只累加 `_droppedLateEvents` 并 `return` | `EditorHostService.cs:855-859` |
| 通过后累加 `_acceptedEvents` 并 `RaiseEvent` | `EditorHostService.cs:861-862` |
| `NativeEvent` 是公开事件；legacy 后端直接同步 `NativeEvent?.Invoke` | `EditorHostService.cs:333,592-593,872-875` |
| `OpenProject`/`OpenProjectRooted` 在 **native 调用之后** 才 `BumpGeneration()` | `EditorHostService.cs:960-976` |
| `CloseProject` 走 `BeginTeardown`：置 `_suppressEvents=1` 并 bump generation | `EditorHostService.cs:866-870` |
| 计数器对外可见，但**不进入 Console** | `EditorHostService.cs:657-658`；VM 未引用 |

由此得到两条明确的时序结论：

1. **所有 EngineHost 事件都被重新投递到 UI 线程**，即使事件本身是在 UI 线程的 API 调用中同步发出的。
   因此一个本地动作日志（例如 `entity created: …`）可能先于它的 `EV EntityCreated` 行出现。`[I]`
2. **打开工程时的事件会被 generation guard 丢弃**：`EV_PROJECT_LOADED` 在 native open 内同步发出，
   managed 侧捕获的是 bump 前的 generation；`OpenProject` 返回前 bump，posted 闭包随后判定不一致。
   G2-06 门禁的注释也承认“此前排队的真实 native 事件同样带旧 generation，会一并被丢弃”
   （`editor_avalonia/Services/Hrc2Gate.cs:172`）。该丢弃只体现在计数器，Console 无提示。`[I]`
   （本轮不运行程序，因此这是代码路径推断，不是实测。）

### 2.5 Managed ViewModel 分发

| 事实 | 位置 |
|---|---|
| 构造函数订阅 `_host.NativeEvent` / `RuntimeFaulted` | `MainViewModel.cs:594-596` |
| `OnNativeEvent` 先检查 UI 线程，再检查 host 是否可用 | `MainViewModel.cs:1419-1439` |
| 13 个事件类型逐个 `LogToConsole("EV …: " + payload)`，并触发刷新 | `MainViewModel.cs:1441-1502` |
| `switch` **没有 default 分支**：未知/新增事件类型静默落空 | `MainViewModel.cs:1441-1502` |
| 循环外的本地日志：打开失败、保存、创建、删除、脚本、Play/Stop、PICK 等 | 例：`673,697,758,807,821,878,882,894,944,978,984,1027` |
| `Dispose` 退订事件并 `ResetSession`，但不清空 Console | `MainViewModel.cs:1693-1699` |

### 2.6 引擎日志链路（与 managed Console **不连通**）

| 事实 | 位置 |
|---|---|
| 引擎日志基于 spdlog：级别 `Trace..Off` | `engine/include/Engine/Core/Log.h:63-71` |
| 按 category 取 logger（`Log::Get(name)`），从默认 logger **复制当时的 sinks** | `engine/src/Core/Log.cpp:257-283` |
| 异步线程池 + 队列 | `Log.cpp:180` |
| sinks：stdout、rotating file、内存环形历史（2048 条） | `Log.cpp:42,187-207` |
| 内存历史只提供崩溃 dump，不是可订阅的实时流 | `Log.cpp:50-153` |
| managed 编辑器不读取 `engine.log`、不重定向 stderr、不引用 spdlog | `editor_avalonia` 全目录无匹配 |
| bridge/EngineHost 没有日志回调 ABI | `bridge/include/editor_bridge/capi.h` |

**结论：引擎侧日志（Physics/Audio/Renderer/Scripting 等）目前完全不出现在 managed Console。**
Managed Console 只显示 `EV_*` 事件与 ViewModel 自己生成的行。

### 2.7 Engine 自带 ImGui Console（仅作对照，不是 managed 路径）

| 事实 | 位置 |
|---|---|
| ImGui Console 有自己的 10000 条环形缓冲，含 level/channel/时间/重复计数 | `engine/src/Core/ConsolePanel.cpp:42-74,79-99` |
| `PanelBridgeSink` 在 `OnImGui` 首次调用时挂到 **`spdlog::default_logger()`** | `ConsolePanel.cpp:134-160` |
| 提供 Info/Warn/Error/Fatal、Channels、AutoScroll、Clear、Collapse、正则搜索 | `ConsolePanel.cpp:166-213,229-250` |
| channel 写死 `"Engine"`（spdlog 桥）或 `"Console"`（内建） | `ConsolePanel.cpp:128,152` |
| “General” checkbox 存在但过滤条件从未使用；channels 只匹配 Graphics/Physics/Network/AI/Audio | `ConsolePanel.cpp:179-188,238-243` |
| 分类 logger 在 bridge sink 注册**之后**创建才会复制到该 sink（`Log::Get` 复制创建时的 sinks） | `Log.cpp:269-279` |

Managed `editor_avalonia` 不实例化 `ConsolePanel`，因此上述能力不能算作 managed Console 的现状。

### 2.8 现有测试与门禁的覆盖

| 覆盖 | 位置 |
|---|---|
| Native 事件契约：创建/删除/重命名/组件/资产/Play 等 | `tests/test_bridge/test_editor_capi.cpp:627,727,749,917,1340,1406,1535,1732,1813,1849` |
| Managed UI 线程 marshal | `Hrc2Gate.cs:146-157`（G2-05） |
| Managed generation guard（只断言计数器） | `Hrc2Gate.cs:159-187`（G2-06） |
| ConsoleLines 增长/末尾文本（bootstrap 失败可观测） | `BootstrapTriggerGate.cs:169-171,216-218,299-318` |
| Console 文本包含断言（Hrc1 / Phase3 / Pick / UiState / ProdPath） | `Hrc1Gate.cs:43,58`；`Phase3Gate.cs:839`；`ProductionPickGate.cs:117,478-479`；`UiStateGate.cs:99,332-334`；`ProductionPathGate.cs:496-498` |
| #141 真实 UIA 读取 ConsoleList 文本 | `tests/EditorAvalonia.IntegrationTests/ManagedImportE2ETests.cs:398` |

**没有任何测试**覆盖 managed Console 的过滤、清空、容量、滚动、折叠、级别显示或“丢弃事件可见性”。

---

## 3. G0-3 已知边界

| # | 边界 | 现状 | 证据 |
|---|---|---|---|
| B-1 | 引擎日志不进入 managed Console | 无 ABI、无文件读取、无 stderr 重定向 | §2.6 |
| B-2 | 事件无级别 | `EV_*` 只有 type + payload | `capi.h:67-90` |
| B-3 | 无来源/频道 | 行内只有 VM 手写前缀 | `MainViewModel.cs:1443-1501` |
| B-4 | 无事件时间 | 只有 UI 接收时刻 | `MainViewModel.cs:1665` |
| B-5 | 无序号/因果链 | 事件结构中没有 sequence | `capi.h:67-90` |
| B-6 | 无事件类型名称映射 | Console 只显示 `EV <name>` 文本；未匹配类型静默 | `MainViewModel.cs:1441-1502` |
| B-7 | 容量无上限 | `ObservableCollection` 只增不减 | `MainViewModel.cs:35` |
| B-8 | 无清空/过滤/搜索/折叠 | XAML 只有 ListBox | `MainWindow.axaml:209-214` |
| B-9 | 工程关闭不清空 | `ResetSession` 只追加 “session closed” | `MainViewModel.cs:703-720` |
| B-10 | generation 丢弃不可见 | 只有 `DroppedLateEventCount` 计数器 | `EditorHostService.cs:657,855-859` |
| B-11 | teardown 期间事件被静默抑制 | `_suppressEvents` 直接 return | `EditorHostService.cs:852` |
| B-12 | legacy 与 EngineHost 投递语义不同 | legacy 同步调用；EngineHost 永远 Post | `EditorHostService.cs:592-593,848-864` |
| B-13 | payload 是不透明字符串 | Console 原样打印，不解析 | `MainViewModel.cs:1444-1501` |
| B-14 | 打开工程的 `EV_PROJECT_LOADED` 预期被丢弃 | bump 在 native open 之后 | `EditorHostService.cs:960-976`；`Hrc2Gate.cs:172`（`[I]`） |
| B-15 | ImGui Console 频道过滤不完整（仅对照） | “General” 未参与判断；spdlog 行固定 `Engine` | `ConsolePanel.cpp:179-188,238-243` |
| B-16 | 引擎日志分类 sink 依赖创建顺序（仅对照） | `Log::Get` 复制创建时 sinks | `Log.cpp:269-279` |

以上均为**现状事实**；不构成 T-6 的实现决定。

---

## 4. Gate A 必须先裁决的问题

以下问题 Gate 0 不回答，只列出；每个都会改变实现范围与验收方式。

| # | 问题 | 可选方向与影响 |
|---|---|---|
| Q-1 | T-6 是否只做 **现有 `EV_*` 流的 managed 呈现**，还是要接入引擎日志？ | 只做事件：不改 Bridge ABI。接入引擎日志：需要新的、被单独授权的日志通道（Bridge ABI 或 host 侧日志回调或文件尾随），本轮路线明确禁止未经授权新增 ABI。 |
| Q-2 | 条目是继续 `string`，还是结构化记录（时间、级别、来源、事件类型、序号、payload）？ | 结构化会改动 ConsoleLines 类型与所有使用它的 gate（多处按字符串 Contains/Count 断言）。 |
| Q-3 | 时间语义用 **native 事件时间** 还是 **UI 接收时间**？ | native 时间需要扩展事件 ABI 或 payload；UI 时间保持现状但无法反映真实发生时刻与滞后。 |
| Q-4 | 过滤维度是什么（级别/来源/事件类型/文本搜索），在 VM 还是 View 层做？ | VM 层会改变 `ConsoleLines` 的对外语义与现有 gate；View 层需引入 CollectionView。 |
| Q-5 | 容量与清空语义：环形上限多少？Clear 是清视图缓存还是清模型？工程切换是否清空？是否持久化/导出？ | 上限与清空会影响 `BootstrapTriggerGate`/`ProductionPickGate` 的“count before/after”断言。 |
| Q-6 | 丢弃可见性：generation 丢弃、teardown 抑制、未知事件类型，是否必须在 Console 出现一条诊断？ | 若要求可见，需要 VM/宿主新增诊断行，不能只靠计数器；同时要定义去重/限流。 |
| Q-7 | 失败语义：host fault、off-UI-thread 违规、malformed payload 如何呈现？ | 现状 fault 走 `RUNTIME FAULTED` 行；off-UI-thread 行已存在；malformed payload 不检查。 |
| Q-8 | legacy 后端是否在 T-6 范围内？两种投递语义是否必须统一？ | 现状生产默认 EngineHost；legacy 仍被部分 gate 使用，统一需要额外改动。 |
| Q-9 | 验收方式：真实 Avalonia 进程 + UIA 断言哪些行为（过滤/清空/容量/滚动/丢弃计数）？Debug/Release 是否都必须？ | 需冻结测试清单；不得用 VM 单测替代 UI 行为证据。 |
| Q-10 | 与 T-1 managed 物理观测的关系 | T-1-B 的范围例外已关闭；除非 T-6 明确承载运行时状态，不得用 Console 证据重开/替代 T-1-B。 |

---

## 5. Gate 0 通过条件自检

| 条件 | 结果 | 依据 |
|---|---|---|
| 现有生产链路已追踪清楚 | **PASS** | §2.1–§2.5 覆盖 producer→EngineHost→seam→VM→ListBox |
| 日志/事件能力边界有代码证据 | **PASS** | §2.6–§2.8、§3 均为文件:行证据 |
| 基线状态已记录 | **PASS** | §1（HEAD/分支/工作树/115 产物/8 哈希 MATCH） |
| 未决问题已列出 | **PASS** | §4（Q-1…Q-10） |
| 未开始实现 | **PASS** | 本轮仅只读；无源文件改动、无构建/运行 |

**Gate 0 结论：COMPLETE。** Gate A 契约尚未编写、尚未批准；在得到裁决前不进入 T-6 实现。

---

## 6. 本轮明确未执行

- 未构建（Debug/Release 均未跑），未运行任何测试套件，未启动 `editor_avalonia`。
- 未做 UIA、未做真实进程交互、未采集运行时 Console 行为。
- 未修改任何源码、文档、测试或 `docs/T-6-Console-GateA-Contract.md`（不存在）。
- 未执行 commit / push / tag / merge / reset / clean；未触碰脏子模块与 `crashes/`。
- 未触碰 T-1、T-2、T-4、F2-S1、GPUProfiler、音频、DebugDraw 相关工作。

---

## 7. 证据索引

| 事实 | 文件:行 |
|---|---|
| ConsoleLines / LogToConsole | `editor_avalonia/ViewModels/MainViewModel.cs:35,1663-1666` |
| Console ListBox | `editor_avalonia/MainWindow.axaml:209-214` |
| ResetSession 不清空 | `MainViewModel.cs:703-720` |
| NativeEvent 订阅/退订 | `MainViewModel.cs:594-596,1693-1699` |
| OnNativeEvent 分发（无 default） | `MainViewModel.cs:1419-1503` |
| Emit 同步回调 | `bridge/src/EditorSession.cpp:1538-1541` |
| EV 枚举与 payload | `bridge/include/editor_bridge/capi.h:67-90` |
| EngineHost 回调注册/转发 | `bridge/src/EngineHost.cpp:498-500,1700-1721,609,1414` |
| Managed marshal 与 generation guard | `editor_avalonia/Services/EditorHostService.cs:841-864` |
| bump 时机 | `EditorHostService.cs:866-870,960-976` |
| 丢弃/接受计数器 | `EditorHostService.cs:657-658`；`Hrc2Gate.cs:159-187` |
| 引擎日志级别与 sinks | `engine/include/Engine/Core/Log.h:63-71`；`engine/src/Core/Log.cpp:42,180,187-207,257-283` |
| ImGui Console 过滤/清空/容量 | `engine/src/Core/ConsolePanel.cpp:42-74,134-160,166-213,229-250` |
| Native 事件测试 | `tests/test_bridge/test_editor_capi.cpp:627,727,749,917,1340,1406,1535,1732,1813,1849` |
| Managed Console 断言 | `Hrc2Gate.cs:146-187`；`BootstrapTriggerGate.cs:169-171,216-218,299-318`；`Hrc1Gate.cs:43,58`；`Phase3Gate.cs:839`；`ProductionPickGate.cs:117,478-479`；`UiStateGate.cs:99,332-334`；`ProductionPathGate.cs:496-498` |
| #141 UIA Console 读取 | `tests/EditorAvalonia.IntegrationTests/ManagedImportE2ETests.cs:398` |
| T-2 哈希基线 | `docs/RF4-Managed-Import-Stage0-Baseline.md:63-72` |
