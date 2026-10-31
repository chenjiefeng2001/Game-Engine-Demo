# T-6-B — Engine Logging Integration · Gate 0 Scan

**状态**：`T-6-B GATE 0 — PASS — READ-ONLY SCAN COMPLETE`
**日期**：2026-10-31
**基线**：HEAD `2ad43c7`，分支 `avalonia`，相对 `origin/avalonia` ahead 13
**范围**：只读核对 Engine spdlog → EngineHost → managed Console 的现状、线程/生命周期、
ABI 与 generation 边界；不实现日志接线。
**Gate A**：`NOT WRITTEN / NOT AUTHORIZED`。本文件只形成 Gate A 的决策输入。
**本轮未做**：未修改源码/测试/构建配置；未构建或运行测试；未 commit / push / tag / merge；
未清理脏子模块或 `crashes/`。

**输入**：
- `docs/T-6-Console-GateA-Contract.md`（T-6-A FROZEN，§12 列出 T-6-B 必须回答的六个问题）
- `docs/T-6-Closure-Record.md`（T-6-A PASS，T-6-B DEFERRED）
- `docs/T-6-Console-Gate0-Scan.md`（原始 Console/日志边界）
- 当前源码（HEAD `2ad43c7`）

---

## 0. 结论摘要

T-6-A 的 managed Console 已具备容量、清空、过滤、工程边界与两阶段 generation 语义，
但它只显示 `EV_*` 与 ViewModel 本地行；Engine `spdlog` 日志仍然完全不可见。

本轮只读核查确认四个硬事实：

1. **没有日志传输通道**：`capi.h` 没有 Engine 日志回调/查询 API，`EV_COUNT=14` 不含日志事件；
   EngineHost 的现有回调只服务编辑器操作事件。
2. **启动期日志早于 managed 回调**：`EngineHost_Create` 内部完成 runtime bootstrap 与
   `Log::Init`，managed 侧只能在 `EngineHost_Create` 返回后才注册事件回调；纯回调方案
   无法捕获 bootstrap 日志。
3. **日志来自两种线程模型**：默认 logger 是同步 `spdlog::logger`，category logger 是
   `spdlog::async_logger`；同一条日志可能来自 runtime 线程、调用线程或 spdlog worker。
4. **现有事件语义不适合全局日志**：`OpenAttemptTracker` 会把 `EV_PROJECT_LOADED` 之后的事件
   作为候选代次排队，`_suppressEvents` 在 CloseProject 后抑制事件；Engine 日志是进程级诊断，
   不应被工程 generation 丢弃，也不应在工程关闭后静默消失。

因此本轮建议：Gate A 应优先裁决**独立日志通道**，而不是直接把日志塞入现有 `EV_*` 流。
推荐方向是“**有界 native 日志环形缓冲 + managed poll/fetch**”；是否采纳、容量、ABI 细节
留待 Gate A，不在本轮冻结。

---

## 1. 范围与既有边界

### 1.1 T-6-B 范围内（候选）

- Engine `spdlog` 日志（默认 logger + category logger）进入 managed Console；
- 日志条目的级别、分类/来源、源端时间、消息；
- EngineHost 生命周期、工程切换、窗口关闭时的订阅/停止行为；
- 高频日志的容量、背压、丢弃统计与 UI 响应性；
- 与现有 stdout/file sink 的一致性；
- Debug/Release 与真实窗口/UIA 验收。

### 1.2 明确不在本轮

- 日志导出、持久化、折叠/去重、跳转源码；
- 级别/来源/正则过滤（是否扩展 T-6-A 过滤模型由 Gate A 裁决）；
- 替换或重定向现有 stdout/file sink；
- 修改 T-6-A 已冻结的 EV 事件、边界行与 D-5 generation 规则；
- legacy `LegacySessionChannel` 的日志能力（生产默认是 EngineHost）；
- T-1/T-2/T-4/T-5/T-7/F2-S1 的既有范围。

T-6-A 契约 §12 明确要求 T-6-B 回答六项：日志字段、传输方式、线程安全与顺序、
高频容量/背压/丢弃统计、EngineHost 销毁/工程切换/窗口关闭的订阅生命周期、
与 stdout/file sink 的一致性（`docs/T-6-Console-GateA-Contract.md:330-342`）。

---

## 2. Engine 日志栈事实

### 2.1 级别与入口

| 事实 | 证据 |
|---|---|
| 级别 `Trace/Debug/Info/Warn/Error/Critical/Off` | `engine/include/Engine/Core/Log.h:63-71` |
| `ENGINE_LOG_*` 使用 category logger（`Log::Get(category)`） | `Log.h:314-366` |
| `LOG_*` / `Log::Info` 等使用默认 logger `"Engine"` | `engine/include/Engine/ConsoleLog.h:62-78`；`Log.cpp:80-108` |
| `Log::Get` 在 logger 不存在时从默认 logger 复制**当时的 sinks** | `engine/src/Core/Log.cpp:257-283` |
| 默认 logger 实际构造为 `std::make_shared<spdlog::logger>`（普通 logger） | `Log.cpp:204-207` |
| category logger 构造为 `std::make_shared<spdlog::async_logger>`（异步 logger） | `Log.cpp:271-276` |

**线程含义**：

- 默认 logger 的 sink 调用发生在调用 `Log::Info` / `LOG_*` 的线程；
- category logger 的 sink 调用发生在 spdlog 线程池 worker；
- 因此一个日志 sink 不能假设固定线程，也不能在 sink 内直接做 UI 调用或阻塞等待。

### 2.2 Sinks 与格式

`Log::Init` 创建默认 logger 的三个 sink
（`engine/src/Core/Log.cpp:173-221`）：

| Sink | 级别 | 格式/用途 |
|---|---|---|
| `stdout_color_sink_mt` | `Log::Level::Trace` | `[%H:%M:%S.%e] [%l] [%n] %v` |
| `rotating_file_sink_mt` | `Log::Level::Debug` | 5 MB × 3 备份；`[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%s:%#] %v` |
| `MemoryHistorySink` | `Trace` | 2048 条环形；仅供崩溃 dump，不是可订阅实时流 |

补充事实：

- `MemoryHistorySink` 用 `formatter_->format(msg)` 存格式化文本，不保留独立的 category/source
  字段；时间戳取 `std::chrono::system_clock::now()`（sink 时刻），不是 `msg.time`
  （`Log.cpp:126-134`）。
- `MemoryHistorySink` 只有 `DumpToFile`/`GetCount`/`GetCapacity`，没有游标/批量读取接口
  （`Log.cpp:50-153`）。
- `spdlog::init_thread_pool(8192, 1)`；category async logger 使用
  `async_overflow_policy::block`（`Log.cpp:180,271-276`）。
- `spdlog::flush_every(std::chrono::seconds(1))`（`Log.cpp:213`）。
- `PanelBridgeSink` 是 Engine 自带 ImGui Console 的 sink，只在首次 `OnImGui` 时挂到
  `spdlog::default_logger()`，且把级别粗化为 0/1/2、channel 固定 `"Engine"`
  （`engine/src/Core/ConsolePanel.cpp:134-160`）。它不能作为 managed Console 的实现，
  也不能解决 category logger 的覆盖问题。

### 2.3 生命周期

| 事实 | 证据 |
|---|---|
| `Application::InitializeHeadless` 调用 `Log::Init()` | `engine/src/Core/Application.cpp:120-128` |
| `Application::Shutdown` **不调用** `Log::Shutdown` | `Application.cpp:148-159` |
| `EngineHost::InitializeRuntimeResources` 创建 headless `Application` 并初始化 | `bridge/src/EngineHost.cpp:541-551` |
| bootstrap 命令在 runtime 线程执行 `InitializeRuntimeResources` | `EngineHost.cpp:802-837` |
| `EngineHost::TeardownRuntimeResources` 重置 `Application`，但不调用 `Log::Shutdown` | `EngineHost.cpp:605-668` |
| `Log` 是进程级静态状态；`Log::Init` 幂等 | `Log.cpp:164-175,228-240` |

**含义**：

- EngineHost 创建/销毁不等于 spdlog 创建/销毁；logger、sink、线程池跨 host 生命周期存在。
- 任何捕获 EngineHost 指针的日志 sink 必须在 host 销毁前显式 detach，否则存在 late callback /
  use-after-free 风险。
- 由于 `Log::Init` 在 `EngineHost_Create` 内部（runtime bootstrap）执行，且 managed 回调在
  `EngineHost_Create` 返回后才注册，**bootstrap 日志不能通过“回调注册后”的通道自然捕获**。
- 如果要求包含 bootstrap 日志，必须使用在 `Log::Init` 阶段就存在的 native 缓冲/历史，
  或明确把“host 创建前的日志”排除出 T-6-B 范围。

---

## 3. EngineHost → managed Console 现状

### 3.1 创建与回调顺序

```text
managed EditorHostService.Create()
  -> EngineHost_Create()                       [capi.cpp:850-874]
       -> EngineHost::Initialize()             [EngineHost.cpp:712-...]
            -> runtime thread starts           [EngineHost.cpp:750-761]
            -> bootstrap command               [EngineHost.cpp:802-837]
                 -> InitializeRuntimeResources
                      -> new Application
                      -> Application::InitializeHeadless
                           -> Log::Init()      [Application.cpp:125]
  <- EngineHost_Create returns
  -> EngineHost_SetEventCallback()             [EditorHostService.cs:906-912]
```

因此：

- `Log::Init()` 的 “Log system initialized” 以及 Application/subsystem bootstrap 日志，
  都发生在 managed callback 注册之前；
- 现有 `EV_*` 事件都来自 `EditorSession` 的编辑/运行时操作，不是 bootstrap 日志；
- 若 T-6-B 只挂一个“callback 收到后才存在”的 sink，启动日志无法显示。

### 3.2 现有事件 ABI

| 事实 | 证据 |
|---|---|
| `EditorEventCallback` 只有 `eventType + payload const char*` | `bridge/include/editor_bridge/capi.h:53-56` |
| `EngineHostEventCallback` 有 `eventType + generation + payload` | `capi.h:683-686` |
| `EV_*` 共 13 个业务事件，`EV_COUNT=14` | `capi.h:67-90` |
| `EngineHost_SetEventCallback` 是唯一事件注册入口 | `capi.h:718-719` |
| Bridge 头没有任何 Engine Log 回调或日志查询 API | `capi.h` 全文 |
| `EditorBridgeApi` 的 `EvCount=14` | `editor_avalonia/Native/EditorBridgeApi.cs:12-28` |
| EngineHost ABI 当前为 v1.8，managed 期望版本相同 | `capi.h:438`；`EditorBridgeApi.cs:62` |
| ABI lockstep 在 `Create()` 处校验 | `EditorHostService.cs:875-882,892-893` |

事件回调是同步调用；`EngineHost::ForwardEvent` 在 callback mutex 下调用 managed delegate
（`EngineHost.cpp:1700-1712`）；`EditorSession::Emit` 在 `m_EventMutex` 下同步调用
（`EditorSession.cpp:1538-1541`）。这是编辑操作事件通道，不是高吞吐日志通道。

### 3.3 Managed 事件处理与 generation

| 事实 | 证据 |
|---|---|
| native 回调 `OnNativeEvent` 先进入 `OpenAttemptTracker.Admit` | `EditorHostService.cs:1046-1054` |
| `EV_PROJECT_LOADED` 之后的事件作为候选代次入队 | `EditorHostService.cs:247-277` |
| `MarshalEvent` 在 UI Dispatcher 上再次校验 generation；不匹配计入 `DroppedLateEventCount` | `EditorHostService.cs:1060-1076` |
| `CloseProject` → `BeginTeardown` 设置 `_suppressEvents=1` | `EditorHostService.cs:1078-1084` |
| `BeginRuntime`（打开成功后）才清除 `_suppressEvents` | `EditorHostService.cs:922-942` |
| `MainViewModel.OnNativeEvent` 的 switch 没有 default；未知事件只增加 `ConsumedEventCount`，不显示 | `MainViewModel.cs:1453-1537` |
| `RequiresOpenProject` 对未知类型默认 `true` | `MainViewModel.cs:1445-1451` |

**含义**：如果把日志作为新的 `EV_*` 类型直接复用：

- 工程打开期间、`EV_PROJECT_LOADED` 之后的日志会被当作候选代次事件排队；
- 工程关闭后 `_suppressEvents=1`，日志会被静默丢弃；
- `RequiresOpenProject` 默认会把未知日志事件当成“需要打开工程”，在无工程时忽略并清投影；
- `switch` 没有 default，新增类型如果不改 managed 分支就不会显示；
- 日志是进程级诊断，不应被工程代次/边界行语义错误归属。

这些不是“改一个 case”就能安全解决的；需要 Gate A 明确日志是否属于 generation 作用域。

### 3.4 现有 Console 数据模型

| 事实 | 证据 |
|---|---|
| `ConsoleLines` 是 `ObservableCollection<string>`，每行字符串 | `MainViewModel.cs:35-55` |
| 容量 10,000，超限丢最旧；事件/本地/边界行都计数 | `MainViewModel.cs:1699-1718` |
| 每行前缀 `[HH:mm:ss.fff]` 是 UI 接收时间，不是源端时间 | `MainViewModel.cs:1701` |
| 过滤只匹配去掉第一个 `] ` 前缀后的文本，`OrdinalIgnoreCase` | `MainViewModel.cs:1736-1744` |
| 没有级别/来源/序号/事件类型字段 | T-6-A 契约 §2.1；`MainViewModel.LogToConsole` |
| 当前 T6ConsoleGate 覆盖容量/清空/过滤/边界/generation，不覆盖 Engine 日志 | `editor_avalonia/Services/T6ConsoleGate.cs`；T-6-A closure §3 |

### 3.5 现有生产日志信号

`EditorSession` 已经在真实编辑路径写 Engine 日志，可作为 T-6-B 的生产信号：

- `[EditorBridge] project loaded`：`EditorSession.cpp:306`
- `[EditorBridge] project saved`：`:354`
- `[EditorBridge] entity created/deleted/renamed`：`:392,453,484`
- `[EditorBridge] component added/removed/prop set`：`:615,639,735`
- `[EditorBridge] asset imported/renamed`：`:987,1068`
- `[EditorBridge] PLAY/RELOAD/STOP`：`:1406,1440,1461`

这些日志和 `EV_*` / ViewModel 本地行可能描述同一操作，存在**重复/回声**风险：
例如打开工程可能同时出现 Engine `[EditorBridge] project loaded`、`EV ProjectLoaded` 和
VM `[project] opened: ...`。Gate A 必须决定是否全部显示、过滤 `[EditorBridge]`、还是
把 EditorBridge 日志改为独立 category。

---

## 4. 缺口与风险登记

| 编号 | 缺口/风险 | 分类 | 证据与影响 |
|---|---|---|---|
| T6B-G1 | 没有 Engine 日志传输通道/ABI | 实现缺口 | `capi.h` 无日志 API；managed 无 spdlog 引用 |
| T6B-G2 | bootstrap 日志早于 managed 回调 | 时序缺口 | `EngineHost_Create` 内 `Log::Init`；callback 在 Create 后注册 |
| T6B-G3 | 默认 logger 同步、category logger 异步 | 线程风险 | `Log.cpp:204-207,271-276`；sink 来源线程不固定 |
| T6B-G4 | 分类 logger 复制创建时 sinks | 覆盖缺口 | `Log.cpp:269-279`；后挂 sink 不保证覆盖已存在 category |
| T6B-G5 | 无 Log 关闭/host sink 生命周期机制 | 生命周期风险 | `Application::Shutdown` 不调 `Log::Shutdown`；host destroy 不等于 log destroy |
| T6B-G6 | generation/tracker 会错误处理全局日志 | 语义风险 | `OpenAttemptTracker.Admit` / `_suppressEvents` |
| T6B-G7 | 关闭工程后日志被静默抑制 | 行为缺口 | `BeginTeardown` → `_suppressEvents=1` |
| T6B-G8 | 高频日志无 native 背压/丢弃计数 | 性能风险 | spdlog 队列 8192 block；UI 10k 行；无日志 drop 统计 |
| T6B-G9 | 行模型是字符串，无 level/category/source | 产品/契约缺口 | T-6-A §2.1；`ConsoleMessage` 只处理第一前缀 |
| T6B-G10 | Engine UI 日志与 EV/本地行重复 | UX 风险 | `EditorSession` 日志 + EV + VM 本地行 |
| T6B-G11 | 新增事件/API 会触发 ABI lockstep | ABI 风险 | EngineHost v1.8；`EV_COUNT=14`；`EngineHostExpectedVersion` |
| T6B-G12 | `EngineHost_Fetch` 需要 session 且在 runtime 线程执行 | 传输设计约束 | `EngineHost.cpp:2386-2433`；`RequireSessionForEdit` |
| T6B-G13 | legacy backend 无 Engine 日志路径 | 范围缺口 | `EditorHostService` 默认 EngineHost；legacy 仅 HRC-1 gate |
| T6B-G14 | MemoryHistorySink 不是实时流 | 复用限制 | 2048 条、仅 DumpToFile、sink 时间 |
| T6B-G15 | 文件尾随不能保证实时/顺序 | 备选限制 | rotating 5MB×3；flush 1s；无进程内序号 |
| T6B-G16 | 没有 Engine 日志测试/门禁 | 证据缺口 | T6ConsoleGate 只测本地行/EV；无日志传输测试 |

---

## 5. 字段与容量映射（候选）

### 5.1 可用字段

spdlog `log_msg` 提供：

| 字段 | 现状 | T-6-B 候选映射 |
|---|---|---|
| `level` | `Trace..Critical` | UI `[TRACE]`…`[CRITICAL]`；默认显示阈值待裁决 |
| `logger_name` | 默认 `"Engine"` 或 category 名 | 来源/category 列；EditorBridge 日志目前属 `"Engine"` |
| `source` | file/line/function | `file:line` 后缀；可截断/只显示 basename |
| `time` | spdlog source time | 源端时间 `[HH:mm:ss.fff]`；与 T-6-A UI 时间的关系待裁决 |
| `thread_id` | spdlog thread id | 可选诊断字段，不默认显示 |
| `payload` | 未格式化消息 | 主消息；限制 max length |
| sequence | 当前不存在 | T-6-B 若用 ring，应显式分配单调序号，供丢弃/顺序诊断 |

### 5.2 容量候选

| 层 | 现状/候选 | 决策点 |
|---|---|---|
| spdlog async queue | 8192，block | 不应让日志 sink 阻塞 worker；ring 满时丢弃 |
| native log ring | 建议固定条目/字节上限 | 容量、丢弃最旧/最新、drop counter |
| managed Console | 10,000 行 | 引擎日志与 EV/本地行共用还是单独计数 |
| 单条消息 | 无上限 | 建议 max bytes，超长截断并计数 |
| 轮询批量 | 无 | 每 UI tick 最多 drain N 条，避免 UI 卡顿 |

### 5.3 显示格式候选

```text
[HH:mm:ss.fff] [LEVEL] [category] message (file:line)
```

- 保留 T-6-A 的首个 `[HH:mm:ss.fff]` 前缀，使现有 `ConsoleMessage` 仍能剥离时间戳；
- 若以后要“只按消息过滤”，需要调整 `ConsoleMessage`，不能静默改变 T-6-A 过滤语义；
- 源端时间与 UI 接收时间不能同时伪装成同一个字段；Gate A 必须选一种或明确两者。

---

## 6. 传输方案比较

| 方案 | 做法 | 优点 | 风险/代价 |
|---|---|---|---|
| **A. 独立 native ring + managed poll（推荐方向）** | Engine 侧有界日志 ring；managed pump 通过新 Fetch op/export 批量取 | 无 native→managed per-line callback；可背压、批量、丢弃计数；结构化字段；不受 EV generation 影响 | 新 ABI/op/结构；需 Log sink 生命周期与 ring 设计；轮询延迟 |
| B. 独立日志 callback | native 日志 sink 入 ring，runtime 线程批量回调 managed | 无需 UI 轮询；可批量 | callback 生命周期/线程/重入风险；仍需 ring；委托 pinning |
| C. 复用现有 `EV_*` 回调 | 新增 `EV_ENGINE_LOG` + 文本 payload | 最小 ABI 面；复用 Dispatcher/marshal | generation/tracker/_suppressEvents 语义错误；每行跨 ABI；无结构化字段；EV_COUNT/lockstep；bootstrap 日志丢失 |
| D. 文件尾随 `logs/engine.log` | managed 读文件 | 无 ABI/Engine 改动 | 轮转/1s flush/解析/共享/无序号/非实时；不能作为生产 Console 主路径 |
| E. 复用 `MemoryHistorySink` | 给现有 2048 环加读取游标 | 复用已有 sink | 仅供 crash dump；格式化文本、sink 时间、无 category；容量小；改动 crash 路径风险 |

**建议**：Gate A 优先评估 A；如果要求更小的 v1 且接受“文本、无 bootstrap、generation 特判”，
才考虑 C。D 只可作为离线/诊断补充，不应作为 T-6-B 主链路。B 与 A 功能相近，但 callback
生命周期比 poll 更难证明。

---

## 7. 推荐 Gate A 方向（非冻结）

若用户选择推荐方向，Gate A 应冻结以下内容：

1. **传输**：独立 Engine 日志通道；优先有界 native ring + managed 批量 poll/fetch；
   ABI 从 `ENGINE_HOST_ABI_VERSION v1.8` 升到 v1.9（或明确的新 op），managed
   `EngineHostExpectedVersion` 同步。
2. **日志来源**：覆盖默认 logger `"Engine"` 与 category logger；sink 必须在 category
   logger 创建前安装，或显式遍历注册表补齐；不能重复 `PanelBridgeSink` 只挂 default 的缺陷。
3. **生命周期**：ring/sink 跨 host 的存活与 detach 规则；host destroy 后不得有 late callback；
   `Log::Shutdown` 未被 Application/host 调用的事实必须写入契约。
4. **bootstrap 日志**：明确是否包含 `EngineHost_Create` 期间的日志；若包含，必须使用
   Log::Init 阶段存在的 native ring/历史；若不包含，在契约中写明起始边界。
5. **generation**：Engine 日志是进程级诊断，不进入 `OpenAttemptTracker`，不因工程切换/
   close 被 generation 丢弃；T-6-A 的 EV 规则保持不变。
6. **重复策略**：决定是否显示 `[EditorBridge]` 日志（与 EV/VM 行重复）；若过滤，明确
   过滤依据是 category 还是 message 前缀，不能依赖脆弱的字符串猜测。
7. **容量/背压**：native ring 容量、单条上限、批量大小、drop 最旧/最新、drop counter、
   UI 10k 行与日志的关系；禁止在 spdlog worker 上阻塞等待 UI。
8. **字段/时间**：level、category、source、source timestamp、message、sequence 的 ABI 表示；
   UI 行格式；与 T-6-A `[HH:mm:ss.fff]`/过滤语义的兼容。
9. **失败/负路径**：ring 溢出、malformed/超长消息、无 session、host teardown、legacy backend、
   non-UI 线程、未知 level；必须可观测，不能静默。
10. **验收**：真实 EditorHost/EngineHost 路径、真实 `EditorSession` 日志（如 project loaded/
    entity created/PLAY）、真实窗口/UIA、Debug/Release、test_bridge/CTest 回归；
    不得用 test-only 日志入口替代生产证据。

### 7.1 建议的测试矩阵（Gate A 冻结，不在本轮执行）

| ID | 场景 | 证据层级 |
|---|---|---|
| T6B-01 | 真实 project open 产生的 Engine 日志出现在 Console，级别/category/时间/消息正确 | native + managed + 真实窗口 |
| T6B-02 | bootstrap 日志是否出现符合契约（包含则可见，不包含则明确不承诺） | 真实进程 |
| T6B-03 | entity create/delete/rename 产生多条日志，顺序与 drop counter 一致 | EngineHost + Console |
| T6B-04 | Play/Stop 日志与 EV Play/Stop 共存，重复策略符合契约 | 真实窗口 |
| T6B-05 | 高频日志超过 native ring/Console 容量：UI 不卡死，drop/trim 计数正确 | 真实进程 + 性能上限 |
| T6B-06 | 工程 close/open 期间日志不被错误 generation 丢弃；T-6-A 边界行仍恰好一次 | gate |
| T6B-07 | host shutdown/destroy 后无 late callback/UAF；ring 生命周期安全 | process/UIA |
| T6B-08 | 超长消息/未知 level/无 session/teardown 的失败路径可观测 | gate |
| T6B-09 | Debug/Release 双配置；全量 CTest 回归 | build/test |
| T6B-10 | legacy backend 的行为符合“排除”或“空日志”契约 | gate |

测试应优先触发 `EditorSession` 的真实日志与真实编辑操作，不使用 test-only 的
`PostEventWithGeneration` 伪日志作为 T-6-B 通过证据。

---

## 8. Gate A 必须裁决的问题

| # | 问题 | 推荐方向 |
|---|---|---|
| Q1 | 是否把 Engine 日志作为独立通道，而非新 `EV_*`？ | 独立通道；避免 generation/EV 语义冲突 |
| Q2 | 传输用 ring+poll、callback 还是文件尾随？ | ring+poll；文件尾随仅补充 |
| Q3 | 是否包含 bootstrap 日志？ | 若要“完整 Console”则包含；否则明确从 callback 后开始 |
| Q4 | 日志是否受工程 generation 影响？ | 不受；进程级诊断，只有 host 生命周期限制 |
| Q5 | 是否显示 `[EditorBridge]` 日志（与 EV/本地行重复）？ | 明确重复策略；推荐过滤或独立 category |
| Q6 | 级别/来源/时间在 Console 中如何映射？ | level + category + source timestamp + message；是否结构化行由范围决定 |
| Q7 | native ring/Console 容量、丢弃与 UI 响应性？ | 有界 ring + 批量 drain + drop counter |
| Q8 | ABI 采用新 export 还是 `EngineHost_Fetch` op？ | 两者都可；必须 v1.9、lockstep、无 `RuntimeResult` 布局变化 |
| Q9 | legacy backend 是否支持？ | 推荐 T-6-B 只覆盖 EngineHost；legacy 明确空/不承诺 |
| Q10 | 验收是否要求真实窗口/UIA/Debug+Release？ | 是；不得只做 VM 单测 |

---

## 9. Gate 0 自检

| 条件 | 结果 | 依据 |
|---|---|---|
| Engine spdlog logger/sink/线程/生命周期已追踪 | PASS | §2 |
| EngineHost → managed Console 事件路径已追踪 | PASS | §3 |
| ABI 与 generation 边界已核对 | PASS | §3.2/§3.3 |
| 级别/来源/时间/容量映射已列出 | PASS | §5 |
| 异步/项目切换/关闭生命周期已列出 | PASS | §3.3/§4 |
| Gate A 方案与问题已列出 | PASS | §7/§8 |
| 未开始日志实现 | PASS | 无源码/测试/构建/Git 改动 |

**Gate 0 结论**：`COMPLETE`。T-6-B Gate A 尚未编写、尚未批准；本轮不进入日志接线。

---

## 10. 本轮明确未执行

- 未构建 Debug/Release，未运行任何测试，未启动 `editor_avalonia`；
- 未修改 `Log`、`EngineHost`、`EditorSession`、`EditorHostService`、`MainViewModel`、
  `capi.h` 或任何测试；
- 未新增 Bridge ABI、事件类型、日志 sink 或 gate；
- 未修改 T-6-A 契约/关闭记录，未 commit / push / tag / merge；
- 未触碰脏子模块、`crashes/` 或既有工作区内容。
