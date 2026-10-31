# T-6-B — Engine Logging Integration · Gate A Contract

**状态**：`T-6-B GATE A — CONTRACT PROPOSED — AWAITING ADJUDICATION`
**日期**：2026-10-31
**基线**：HEAD `2ad43c7`，分支 `avalonia`，相对 `origin/avalonia` ahead 13
**Gate 0**：`docs/T-6-B-Console-Logging-Gate0-Scan.md`（PASS — read-only）
**Gate B**：`NOT AUTHORIZED`
**本轮未做**：未修改源码/测试/构建；未构建或运行测试；未执行 Git 写操作。

---

## 0. Formal Adjudication Record（Proposed）

| ID | 提案裁决 | 冻结要求 |
|---|---|---|
| **L-1** | 独立日志通道 | Engine 日志走独立日志流，不复用 `EV_*`，不改变 T-6-A 的事件/generation 语义。 |
| **L-2** | 有界 native ring | Engine 侧安装一个有界、线程安全的日志环形缓冲；sink 只入 ring，不调用 managed/UI。 |
| **L-3** | managed poll | managed 侧按固定周期批量 poll ring；每次批量入 UI，不采用逐行 native callback。 |
| **L-4** | 进程级诊断 | Engine 日志不属于工程 generation；不进入 `OpenAttemptTracker`，不因工程切换/关闭被 generation 丢弃。 |
| **L-5** | ABI 加法 | 新增 `EngineHost_PollEngineLogs` 导出与固定布局结构；EngineHost ABI v1.8 → v1.9；`EV_COUNT` 与 `RuntimeResult` 布局不变。 |
| **L-6** | 结构化字段 | 每条日志包含 sequence、源端时间、level、category、source、message 与截断 flags。 |
| **L-7** | 有界背压 | ring 满时丢弃最旧条目并累计 dropped 计数；sink 不阻塞 spdlog worker/调用线程。 |
| **L-8** | 全量显示策略（待确认） | v1 默认显示所有级别与 category；`[EditorBridge]` 日志与 EV/VM 行重复作为已知行为。若要求去重，必须另行冻结过滤规则。 |
| **L-9** | bootstrap 日志 | 从 `Log::Init` 起的 ring 内容可被首次 poll 取到；ring 已溢出的部分按 dropped 计数，不承诺无限回放。 |
| **L-10** | Managed 轮询 | 新增独立日志轮询 timer（非 runtime pump），host 存活期间运行，Dispose/Shutdown 前停止。 |
| **L-11** | 验收边界 | 必须覆盖真实 EngineHost、真实 `EditorSession` 日志、真实窗口/UIA、Debug/Release 与回归；不得用 test-only 日志入口替代生产证据。 |
| **L-12** | 范围排除 | legacy backend、日志导出/持久化/折叠/跳转源码、Collider/T-1/T-2/T-4/T-5/T-7/F2-S1 均不在 T-6-B v1 范围。 |

---

## 1. 范围与术语

### 1.1 术语

| 术语 | 定义 |
|---|---|
| Engine 日志流 | Engine `spdlog` 产生的日志进入 T-6-B 日志 ring 的单向流 |
| LogRing | Engine 侧固定容量、线程安全的日志环形缓冲 |
| Poll | managed 通过 `EngineHost_PollEngineLogs` 批量读取 ring，不通过回调 |
| 源端时间 | `spdlog::log_msg.time`，即日志产生时刻，不是 UI 接收时刻 |
| 日志丢弃 | ring 满时最旧条目被覆盖；或 managed 读到的 sequence 存在缺口 |
| Runtime pump | `EditorHostService.PumpOnce` / `EngineHost_PumpOneFrame`，驱动帧与物理，不属于日志轮询 |
| Log pump | T-6-B 新增的独立日志轮询 timer，只 poll ring，不推进 runtime |

### 1.2 范围内

- Engine 默认 logger `"Engine"` 与所有 category logger 的日志接入；
- 有界 native ring、sink 生命周期、sequence/丢弃统计；
- `EngineHost_PollEngineLogs` ABI 与 managed P/Invoke；
- managed 日志轮询、Console 行映射、容量/过滤兼容；
- host 创建/关闭/销毁、工程切换、窗口关闭时的日志轮询生命周期；
- Debug/Release、真实窗口/UIA 与回归验收。

### 1.3 范围外

- 日志导出、持久化、折叠/去重、跳转源码、正则搜索；
- 替换 stdout/file sink 或改变 `engine.log` 格式/轮转；
- 把 Engine 日志塞入现有 `EV_*` 事件；
- 修改 T-6-A 的 D-5 generation、边界行、过滤/清空语义；
- legacy `LegacySessionChannel` 的日志能力；
- 把日志当作工程内容、Revision 或持久化资产；
- T-1/T-2/T-4/T-5/T-7/F2-S1 相关工作。

---

## 2. 架构与数据流

```text
LOG_* / Log::Info / ENGINE_LOG_*
          |
          v
  spdlog logger (default = sync; category = async)
          |
          +--> stdout sink        (unchanged)
          +--> rotating file sink (unchanged)
          +--> MemoryHistorySink  (unchanged, crash dump)
          +--> T-6-B LogRing sink (new, bounded, thread-safe)
                         |
                         v
             EngineHost_PollEngineLogs(after_sequence, batch)
                         |
                         v
               EditorHostService LogPump (100 ms)
                         |
                         v
               MainViewModel EngineLogLine (UI thread)
                         |
                         v
                     ConsoleView / ConsoleLines
```

约束：

- LogRing sink 只复制结构化字段入 ring；不得调用 managed 回调、UI、Dispatcher 或阻塞等待。
- managed 只能通过 poll 读取；native 不向 managed 直接推送日志。
- Console 仍由 UI 线程更新；所有 `ConsoleLines` 变更必须在 UI 线程。

---

## 3. Engine 侧 LogRing

### 3.1 安装位置

- LogRing 是 **进程级静态对象**，在 `Log::Init` 创建并安装到默认 logger 的 sink 列表。
- 安装必须发生在 `Log::Init` 的第一条日志（“Log system initialized”）之前；否则该条
  bootstrap 日志不能被 T-6-B 捕获。
- `Log::Get(category)` 在创建 category logger 时复制默认 logger 的 sinks；因此
  category logger 会继承 LogRing sink。
- 如果存在 LogRing 安装前已创建的 logger，`Log::Init`/安装代码必须通过
  `spdlog::apply_all` 补齐，不能只挂 default logger。
- `PanelBridgeSink` 的“只挂 default”缺陷不得复现；T-6-B 必须覆盖 default + category。
- LogRing 不持有 EngineHost/Bridge 指针；因此 EngineHost 销毁不会产生 sink UAF。

### 3.2 条目字段

每条 ring entry 固定布局，包含：

| 字段 | 类型/上限 | 语义 |
|---|---|---|
| `sequence` | uint64 | 单调递增；0 保留给“从最旧开始”的 poll 语义 |
| `timestamp_us` | int64 | 源端时间，Unix epoch 微秒（UTC）；来自 `msg.time` |
| `level` | int32 | 0 Trace, 1 Debug, 2 Info, 3 Warn, 4 Error, 5 Critical |
| `category` | UTF-8，64 bytes | `msg.logger_name`；默认 `"Engine"` |
| `source` | UTF-8，256 bytes | `file:line`；可选截断；不含函数名 |
| `message` | UTF-8，2048 bytes | 格式化后的 payload；去掉尾部 CR/LF |
| `flags` | uint32 | bit0 message truncated；bit1 category truncated；bit2 source truncated；其余为 0 |

不在 v1 ring 中保存 thread id；如果后续需要，必须新增字段/ABI，不得复用 flags。

### 3.3 截断与编码

- 字符串一律 UTF-8；缓冲末尾必须 NUL 终止，长度字段不含 NUL；内容最多为
  `上限-1` 字节。
- 超过上限时截断到 `上限-1` 字节，并在对应的 flags 位置位；不得截断到半个 UTF-8
  序列（实现按完整 codepoint 边界截断）。
- `message` 的尾部 `\r`/`\n` 在入 ring 前去除；消息内部 CR/LF 转为空格，保证一条
  日志对应一行 UI 文本。
- 空消息合法；空 category/source 允许，但标准 logger 应至少提供 category。
- 第一条分配的 sequence 为 1；0 仅用于 poll 的 `after_sequence` sentinel。

### 3.4 容量与丢弃

提案冻结值：

| 参数 | 提案值 |
|---|---|
| ring entries | 4096 |
| category bytes | 64 |
| source bytes | 256 |
| message bytes | 2048 |
| 单条最大 bytes | 约 2404（含字段与 NUL 预留） |
| 批量 poll entries | 32 |

行为：

- ring 满时覆盖最旧条目；`dropped_total` 增加；
- sink 不等待、不阻塞；ring 锁只保护短拷贝；
- sequence 按接收顺序分配；被覆盖条目的 sequence 不再可读，poll 的
  `oldest_sequence`/`newest_sequence` 与 `dropped_total` 用于诊断缺口；
- 溢出不改变 file/stdout sink 的行为。

### 3.5 生命周期

| 事件 | 行为 |
|---|---|
| `Log::Init` | 创建/安装 LogRing；幂等，不重复添加 sink |
| `Log::Shutdown` | 若未来调用，停止/清空 ring；当前 Application/EngineHost 不调用 |
| EngineHost 创建 | 不创建/销毁 LogRing；首次 poll 从最旧可读 sequence 开始 |
| EngineHost 销毁 | LogRing 继续存在；host 不持有 sink 指针，无 detach/UAF |
| 进程退出 | 允许 ring 未清空；不持久化 |

---

## 4. Bridge ABI 契约

### 4.1 版本

- `ENGINE_HOST_ABI_VERSION`：v1.8 → **v1.9**。
- `EditorBridgeApi.EngineHostExpectedVersion` 同步为 v1.9；`Create()` lockstep 必须拒绝
  v1.8/v1.9 混搭。
- `EditorBridge_GetVersion`（session ABI）保持不变；T-6-B 不改变 `EditorSession` ABI。
- `EV_COUNT` 保持 14；不新增 `EV_*` 类型。
- `RuntimeResult` 布局保持不变（仍 76B）。

### 4.2 新增结构

以下为提案布局；Gate A 可通过则冻结，未通过前不写源码。

```c
#define ENGINE_HOST_LOG_CATEGORY_BYTES 64
#define ENGINE_HOST_LOG_SOURCE_BYTES 256
#define ENGINE_HOST_LOG_MESSAGE_BYTES 2048
#define ENGINE_HOST_LOG_BATCH_ENTRIES 32
#define ENGINE_HOST_LOG_ENTRY_BYTES 2404   /* pack(1) 计算值 */

#pragma pack(push, 1)
typedef struct EngineLogEntry {
    uint64_t sequence;          /* 0 */
    int64_t  timestamp_us;      /* 8 */
    int32_t  level;             /* 16 */
    uint32_t category_length;   /* 20 */
    uint32_t source_length;     /* 24 */
    uint32_t message_length;    /* 28 */
    uint32_t flags;             /* 32 */
    char     category[64];      /* 36 */
    char     source[256];       /* 100 */
    char     message[2048];     /* 356 */
} EngineLogEntry;               /* 2404 */

typedef struct EngineLogPollStats {
    uint32_t struct_size;       /* sizeof(EngineLogPollStats) */
    uint32_t entry_count;
    uint64_t oldest_sequence;
    uint64_t newest_sequence;
    uint64_t dropped_total;
    uint64_t next_sequence;     /* 本次返回最后一条的 sequence；无条目时回显 after */
} EngineLogPollStats;
#pragma pack(pop)
```

约束：

- `EngineLogEntry` 固定 2404B；`EngineLogPollStats` 固定 40B；两者不得改变字段顺序/大小。
- `flags` 除 bit0/1/2 外必须为 0；新增位需 ABI 版本变更。
- `level` 只允许 0..5；`Off` 不进入 ring。
- `category/source/message` 均 UTF-8、NUL 终止，长度不含 NUL。

### 4.3 新增导出

```c
EDITOR_BRIDGE_API int32_t EngineHost_PollEngineLogs(
    EngineHostHandle h,
    uint64_t after_sequence,
    uint8_t* outBuffer,
    int32_t capacity,
    EngineLogPollStats* stats,
    RuntimeResult* result);
```

语义：

- `after_sequence == 0`：从 ring 当前最旧可读条目开始；
- 否则只返回 `sequence > after_sequence` 的条目；
- 若 `after_sequence` 早于 `oldest_sequence`，从最旧可读条目开始，并通过
  `dropped_total`/`oldest_sequence` 暴露缺口；
- 空 ring 时 `oldest_sequence = newest_sequence = 0`；
- 每次最多返回 32 条；`capacity` 必须至少为
  `ENGINE_HOST_LOG_BATCH_ENTRIES * ENGINE_HOST_LOG_ENTRY_BYTES`；
- `stats` 必填；`result` 必填；成功时 `result.status = SUCCESS`；
- 返回值为写入的 entry count（0..32）；0 表示当前无新条目，不是错误；
- 调用线程：creation thread / managed UI 线程；不得要求 runtime thread，
  不得要求工程已打开；
- 不修改 `ConsoleLines`、不调用 `PumpOneFrame`、不改变 generation；
- host 已销毁/句柄无效时返回错误，不得访问已释放内存。

### 4.4 与 `EngineHost_Fetch` 的关系

本提案选择**新导出**，而不是新增 `EngineHost_Fetch` op，原因：

- 日志轮询应独立于 runtime command queue，避免每 100ms 增加一个 runtime Submit；
- 日志在工程未打开时也应可读；`Fetch` 当前会经过 `RequireSessionForEdit`
  并提交到 runtime thread（`EngineHost.cpp:2386-2433`）；
- 新导出可以只读 ring，不触碰 GL/session 状态。

如果 Gate A 选择 `EngineHost_Fetch` op 而非新导出，必须重新冻结 op 编号、容量、
线程与 ABI 语义；当前草案不采用。

---

## 5. Managed 接入契约

### 5.1 Poll 生命周期

- `EditorHostService` 在 `EngineHost_Create` 成功后（host 可用）启动日志轮询；
- **启动时机**：必须在 managed 日志订阅端（ViewModel/Console）已挂接之后启动；
  若 host 先创建，ring 保留积压，订阅后首次 poll 从 `after_sequence=0` 取回；
  不得在订阅前丢弃 bootstrap/早期日志。
- 使用独立的 `DispatcherTimer`（提案 100 ms），不调用 `PumpOneFrame`/`RuntimeTick`；
- 不把日志轮询计入 `PumpInvocations`/`PumpFailures`/runtime frame 统计；
- 必须覆盖 `PumpOwnershipGate` 等价断言：日志 timer 不得增加 runtime pump 计数或
  推进帧；日志 timer 只是诊断读取。
- `CloseProject` 不停止日志轮询；host 仍存活时日志继续显示；
- `Dispose`/`Shutdown`/`EngineHost_Destroy` 前停止日志轮询，并丢弃未消费缓冲；
- 日志轮询失败（ABI/句柄/poll 错误）必须记录一次可观测诊断，不得静默无限重试；
- 日志轮询不得在日志回调中递归调用自身或 runtime pump。

### 5.2 Cursor 与批次

- managed 维护 `_engineLogCursor`，初始为 0（从 ring 最旧条目开始）；
- 每次 poll 批量最多 32 条；使用 `stats.next_sequence` 更新 cursor；
- 若 `oldest_sequence > cursor + 1`，说明 ring 已覆盖未读条目；累计
  `EngineLogDroppedCount`，按缺口数计数，不伪造缺失行；
- 每次 poll 到 UI 线程后一次性追加本批；不得每行单独 Dispatcher.Post；
- 若 UI 线程不可用/Disposed，丢弃本批并计数，不阻塞 native。

### 5.3 行映射

提案格式：

```text
[HH:mm:ss.fff] [LEVEL] [category] message (source:line)
```

- 第一个 `[HH:mm:ss.fff]` 使用**源端时间**（由 `timestamp_us` 转换到本地时区）；
- `LEVEL` 使用 `TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` 稳定名；
- `category` 直接来自 ring；
- `message` 已截断的消息；若 `MESSAGE_TRUNCATED`，追加 `…` 或 `[truncated]`；
- `source` 可选；没有 source 时省略括号；
- 不得在引擎日志行前再叠加 UI 接收时间；需要新增 `AppendEngineLogLine`/等价入口，
  不能直接把源端行交给现有 `LogToConsole` 再产生第二个时间戳。
- T-6-A 过滤仍按第一个 `] ` 前缀后文本匹配；日志的 level/category 会进入过滤文本，
  这是已知行为，不新增级别过滤。

### 5.4 Console 容量与计数

- `ConsoleLines` 仍为 10,000 行上限；Engine 日志行与 EV/本地/边界行一起计数；
- `ConsoleTrimmedCount` 继续表示 UI 行淘汰；Engine 日志另有
  `EngineLogAcceptedCount`/`EngineLogDroppedCount`，不得混用；
- 若 Engine 日志量导致 EV/本地行被淘汰，必须由 drop/trim 计数可见；不得静默。
- 清空/过滤语义保持 T-6-A：清空展示缓存，不清 ring；过滤只影响视图。
- 既有 Console 文本/计数 gate 不得改写来迁就新增日志行；若新增日志导致既有断言
  冲突，必须记录并请求裁决，不能放宽断言。

### 5.5 重复与 generation

- 提案 v1 **显示所有 Engine 日志**，包括 `[EditorBridge]`；因此与 EV/VM 行可能重复；
  这是显式已知行为，不是缺陷伪装。
- 若 Gate A 要求去重，必须冻结过滤规则（category、category+message、或把
  `EditorSession` 日志改为独立 category），不能靠脆弱的 UI 字符串猜测。
- Engine 日志不进入 `OpenAttemptTracker`；不增加/减少 `DroppedLateEventCount`；
- `CloseProject` 的 `_suppressEvents=1` **不抑制** Engine 日志；日志轮询在 host 存活期间继续；
- `EV_PROJECT_LOADED`、边界行与 T-6-A 测试保持原样。

---

## 6. 失败、背压与可观测性

| 场景 | 冻结要求 |
|---|---|
| ring 满 | 覆盖最旧；`dropped_total` 增加；sink 不阻塞 |
| managed 未及时 poll | 用 sequence 缺口/`dropped_total` 计数；不伪造缺失行 |
| 超长 category/source/message | UTF-8 边界截断；对应 flags 置位 |
| malformed/空 payload | 原样或空消息入 ring；不崩溃 |
| poll buffer 太小 | 返回错误 + 可读诊断；不得部分写后伪成功 |
| host 未创建/已销毁 | 返回错误；不得访问已释放对象 |
| poll 期间 host teardown | 停止 log pump；未消费批次丢弃并计数 |
| log pump 失败 | 记录一次诊断/计数；不得无限静默重试 |
| UI Disposed | 丢弃本批并计数；不得从后台触碰 UI |
| file/stdout sink | 保持原格式与轮转；LogRing 是附加 sink |

**禁止**：

- 在 spdlog worker/调用线程中调用 managed/UI、等待 Dispatcher、等待 runtime pump；
- 用无界队列承载日志；
- 把 ring 丢弃描述为“完整显示”；
- 用 test-only 日志注入替代真实 EngineHost/Engine 日志证据。

---

## 7. 测试矩阵（Gate B 执行）

| ID | 场景 | 通过条件 | 层级 |
|---|---|---|---|
| T6B-01 | 真实 Engine 日志显示 | 真实 `EditorSession` / Engine 日志经 host 进入 Console；level/category/message 正确 | EngineHost + Console |
| T6B-02 | bootstrap 日志 | 首次 poll 的可读内容与 L-9 一致；溢出时有 dropped 证据 | 真实进程 |
| T6B-03 | default + category logger | `LOG_INFO` 与 `ENGINE_LOG_INFO("Category",...)` 都进入 ring/Console | native + managed |
| T6B-04 | 源端时间/来源 | `timestamp_us` 与 source 映射正确；不使用 UI 时间伪装 | native + Console |
| T6B-05 | 高频/容量 | ring 满、UI 10k 行、drop/trim 计数正确；UI 不卡死 | 真实进程/上限 |
| T6B-06 | 工程切换 | 日志不因 generation 被丢弃；T-6-A 边界行仍恰好一次 | gate |
| T6B-07 | CloseProject | host 仍存活时日志继续；`_suppressEvents` 不抑制日志 | gate |
| T6B-08 | Host Dispose/Destroy | log pump 停止；无 late callback/UAF；未消费批次有计数 | process/UIA |
| T6B-09 | 截断/边界 | 超长字段按 UTF-8 截断，flags 正确；空/ malformed 不崩溃 | native |
| T6B-10 | ABI | v1.8 native + v1.9 managed（及反向）在 Create lockstep 拒绝；v1.9 正常 | test_bridge |
| T6B-11 | legacy | legacy 不显示 Engine 日志，且不崩溃/不伪装成功 | gate |
| T6B-12 | 回归 | Debug/Release、真实窗口、UIA、test_bridge/test_hrc/CTest 通过 | build/test |

证据纪律：

- Debug/Release 分开记录；
- 真实窗口/UIA 与 native 证据分开；
- 不把 F2-B/T-1/T-5/T-6-A 的既有通过数计为 T-6-B 新增；
- 不以 VM-only 测试替代真实 Console 可见性；
- 失败保留首次记录，不用重跑覆盖；
- ring 是进程级状态：每个测试必须先 drain 到当前 `newest_sequence` 再执行自己的
  日志动作，不能依赖“测试开始时 ring 为空”，也没有 test-only reset API；
- 既有 Console 文本断言 gate（BootstrapTriggerGate、Hrc1Gate、Phase3Gate、
  ProductionPickGate、UiStateGate、ProductionPathGate）不得被改写；新增日志若造成
  冲突，按失败处理并请求裁决。

---

## 8. 变更白名单（若 Gate B 获授权）

允许修改/新增：

| 文件 | 允许内容 |
|---|---|
| `engine/include/Engine/Core/Log.h`、`engine/src/Core/Log.cpp` | LogRing/sink 安装与生命周期 |
| `engine/include/Engine/Core/LogStream.h`、`engine/src/Core/LogStream.cpp`（新增） | 有界 ring、sequence、截断、poll |
| `bridge/include/editor_bridge/capi.h` | `EngineLogEntry`/`EngineLogPollStats`、新导出、ABI v1.9 |
| `bridge/src/capi.cpp` | `EngineHost_PollEngineLogs` 转发 |
| `bridge/src/EngineHost.h/.cpp` | poll 方法/日志 ring 读取；不改变 EV/session 语义 |
| `editor_avalonia/Native/EditorBridgeApi.cs` | P/Invoke、版本常量、结构镜像 |
| `editor_avalonia/Services/EditorHostService.cs` | log pump、cursor、计数、host 生命周期 |
| `editor_avalonia/ViewModels/MainViewModel.cs` | Engine 日志行入口、Console 容量/过滤兼容 |
| `editor_avalonia/Services/T6BLoggingGate.cs`（新增） | 真实窗口/gate |
| `tests/EditorAvalonia.IntegrationTests/T6BConsoleLoggingUITests.cs`（新增） | UIA 真实控制台日志 |
| `tools/run_t6b_logging_gate.ps1`（新增） | 构建 + gate + UIA |
| `tests/test_core/*`、`tests/test_bridge/*`、`tests/CMakeLists.txt` | ring/ABI/生命周期测试 |
| `docs/T-6-B-*` | 契约、实现、测试、关闭记录 |

禁止修改（未经再次裁决）：

- `bridge/src/EditorSession.cpp` 的 EV 事件语义（除非 L-8 去重裁决要求独立 category）；
- T-6-A 的 `OpenAttemptTracker`/边界行/过滤/清空行为；
- `engine/src/Core/ConsolePanel.cpp`（ImGui Console 不在 T-6-B 范围）；
- T-1/T-2/T-4/T-5/T-7/F2-S1 相关文件；
- `assets/` fixture、场景格式、RevisionStore；
- 任何第三方子模块或 `crashes/`。

---

## 9. 基线保护

- `capi.h` 将因新导出/结构/版本而改变；T-2 的 8 文件哈希基线必须记录
  “T-6-B 是唯一有意变更”，其余 7 个保持 MATCH；不得静默改写旧记录。
- 3 个脏子模块与 `crashes/` 不得触碰。
- 不写 repo fixture；测试使用系统临时目录 scratch 并在 finally 删除。
- 未获授权不执行 commit / push / tag / merge；不执行 reset/clean。
- 构建警告按实际记录，不宣称零警告/clean build。

---

## 10. 待裁决问题

| # | 问题 | 本草案默认 |
|---|---|---|
| A-1 | 传输：新导出 vs `EngineHost_Fetch` op | 新导出 `EngineHost_PollEngineLogs` |
| A-2 | 是否包含 bootstrap 日志 | 包含首次 poll 可读的 ring 内容；不承诺超出容量回放 |
| A-3 | 级别转发 | 全部 Trace..Critical 进入 ring/Console；不设 UI 级别过滤 |
| A-4 | `[EditorBridge]` 重复 | 全部显示；重复作为已知行为 |
| A-5 | 行模型 | 继续字符串行；不引入结构化 ConsoleLines |
| A-6 | 时间语义 | Engine 日志用源端时间；EV/VM 行保持 T-6-A UI 时间 |
| A-7 | 容量 | ring 4096、batch 32、message 2048、category 64、source 256 |
| A-8 | log pump 周期/所有权 | `EditorHostService` 独立 100ms timer；非 runtime pump |
| A-9 | legacy | 不接入；显式空/不承诺 |
| A-10 | ABI | v1.9；`EV_COUNT` 与 `RuntimeResult` 不变 |
| A-11 | 去重/级别过滤 | 不在 v1；如需要，另立契约/扩展 |
| A-12 | 关闭条件 | 所有 T6B-01..12 有实际证据，且无生产范围外改动 |

---

## 11. 关闭条件

T-6-B 的关闭记录必须包含：

1. Engine LogRing/sink 安装、default + category 覆盖证据；
2. `EngineHost_PollEngineLogs` ABI 与 v1.9 lockstep 证据；
3. 真实 `EditorSession` Engine 日志进入 Console 的真实窗口/UIA 证据；
4. 容量、截断、drop/trim 计数、UI 响应性；
5. 工程切换/Close/Dispose/host destroy 生命周期证据；
6. Debug/Release、`test_bridge`/`test_hrc`/CTest 回归；
7. 既有/新增证据分界、已知限制（重复、bootstrap 溢出、legacy 排除）；
8. 未修改范围外生产代码的核对。

---

## 12. 本轮未做

- 未修改 `Log`、`EngineHost`、`EditorSession`、`EditorHostService`、`MainViewModel`、
  `capi.h` 或任何测试；
- 未构建、未运行测试、未启动编辑器；
- 未新增 ABI、日志 sink、log pump 或测试；
- 未 commit / push / tag / merge；
- 未清理脏子模块或 `crashes/`。
