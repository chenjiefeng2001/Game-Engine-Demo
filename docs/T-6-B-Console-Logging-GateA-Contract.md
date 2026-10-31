# T-6-B — Engine Logging Integration · Gate A Contract

**状态**：`T-6-B GATE A — PASS — CONTRACT FROZEN — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：HEAD `2ad43c7`，分支 `avalonia`，相对 `origin/avalonia` ahead 13
**Gate 0**：`docs/T-6-B-Console-Logging-Gate0-Scan.md`（PASS — read-only）
**Gate A 裁决**：2026-10-31 用户批准 A-1～A-12；本文件正式封存。
**Gate B**：`NOT AUTHORIZED`（源码实施授权仍需单独明确）
**原始草案**：`docs/T-6-B-Console-Logging-GateA-Contract-Draft.md`（完整保留）
**本冻结轮次**：未修改源码/测试/构建；未构建或运行测试；未执行 Git 写操作。

---

## 0. Final Adjudication Record（FROZEN）

A-1～A-12 保留原编号；本表为最终裁决，取代下方草案中的提案值。

| ID | 最终裁决 | 冻结要求 |
|---|---|---|
| **A-1** | 传输方式 | 新增 `EngineHost_PollEngineLogs` 导出；**不**新增 `EngineHost_Fetch` operation。 |
| **A-2** | Bootstrap 起点 | 从 `Log::Init` 成功安装 ring sink 后开始保留；`Log::Init` 之前的日志不在承诺范围。 |
| **A-3** | 日志级别 | 显示所有实际到达 sink 的级别；不修改 spdlog 现有 level filter，已被上游过滤的日志不恢复。 |
| **A-4** | Category / 重复 | 显示全部 category，包括 `[EditorBridge]`；Engine 日志与 EV/VM 行重复接受为已知行为。 |
| **A-5** | Console 行模型 | 保持字符串行；显示 level/category/source/message，不引入结构化 `ConsoleLines`。 |
| **A-6** | 时间/顺序 | Engine 日志使用源端 Unix epoch UTC 微秒；sequence 是消费顺序的权威标识，不依赖时间戳排序。 |
| **A-7** | Native ring | 4096 条；溢出保留最新、淘汰最旧未消费记录；`droppedTotal` 为进程生命周期累计值。 |
| **A-8** | Poll | 每次最多 32 条；managed 轮询周期 50 ms；空队列成功返回 0 条；一个 managed consumer。 |
| **A-9** | 字段容量 | message 2048 B、category 64 B、source 256 B；UTF-8；截断在字符边界并置 flags。 |
| **A-10** | ABI | EngineHost ABI v1.9；固定布局 `EngineLogRecordV1` 共 2392 B；`EV_COUNT` 与 `RuntimeResult` 布局不变。 |
| **A-11** | Legacy / Clear | Legacy backend 排除；Console Clear 仅清显示，不清 native ring；不自动去重/黑名单/默认级别过滤。 |
| **A-12** | Gate B | 按 §7 门禁实施；本次只冻结契约，不授权源码实施。 |

---

## 1. 范围与术语

### 1.1 术语

| 术语 | 定义 |
|---|---|
| Engine 日志流 | Engine `spdlog` 产生的日志进入 T-6-B LogRing 的单向流 |
| LogRing | Engine 侧固定容量、线程安全、有界内存的日志环形缓冲 |
| Poll | managed 通过 `EngineHost_PollEngineLogs` 批量读取 LogRing，不通过回调 |
| 源端时间 | `spdlog::log_msg.time` 对应的 Unix epoch UTC 微秒，不是 UI 接收时间 |
| 日志丢弃 | ring 满时最旧未消费记录被覆盖；`droppedTotal` 累计 |
| Runtime pump | `EditorHostService.PumpOnce` / `EngineHost_PumpOneFrame`，驱动帧与物理 |
| Log pump | T-6-B 新增的独立日志轮询 timer，只 poll ring，不推进 runtime |

### 1.2 范围内

- Engine 默认 logger 与所有 category logger 的日志接入；
- 有界 native LogRing、sink 生命周期、sequence/丢弃统计；
- `EngineHost_PollEngineLogs` ABI 与 managed P/Invoke；
- managed 日志轮询、Console 行映射、容量/过滤兼容；
- host 初始化/关闭/销毁、工程切换、窗口关闭时的日志轮询生命周期；
- Debug/Release、真实窗口/UIA 与回归验收。

### 1.3 范围外

- 日志导出、持久化、折叠/去重、跳转源码、正则搜索；
- 替换 stdout/file sink 或改变 `engine.log` 格式/轮转；
- 把 Engine 日志塞入现有 `EV_*` 事件；
- 修改 T-6-A 的 D-5 generation、边界行、过滤/清空语义；
- Legacy `LegacySessionChannel` 的日志能力；
- 独立 Legacy logger 迁移、非 spdlog 日志框架收集/Bridge 事件重构；
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
             EngineHost_PollEngineLogs(afterSequence, batch)
                         |
                         v
               EditorHostService LogPump (50 ms)
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

### 3.1 安装与覆盖

- LogRing 是**进程级静态对象**，在 `Log::Init` 创建并安装到默认 logger 的 sink 列表。
- 安装必须发生在 `Log::Init` 的第一条日志（“Log system initialized”）之前；否则该条
  bootstrap 日志不能被 T-6-B 捕获。
- `Log::Get(category)` 在创建 category logger 时复制默认 logger 的 sinks；因此
  category logger 会继承 LogRing sink。
- 如果存在 LogRing 安装前已创建的 logger，`Log::Init`/安装代码必须通过
  `spdlog::apply_all` 补齐，不能只挂 default logger。
- 后续通过现有 logger 注册机制创建的 logger 必须接入同一 sink；不得重复注册导致重复记录。
- `PanelBridgeSink` 的“只挂 default”缺陷不得复现；T-6-B 必须覆盖 default + category。
- LogRing 不持有 EngineHost/Bridge 指针；因此 EngineHost 销毁不会产生 sink UAF。

### 3.2 记录字段

正式冻结记录名 `EngineLogRecordV1`；字段见 §4.2。每条记录包含：

| 字段 | 语义 |
|---|---|
| `sequence` | 单调递增；第一条为 1；0 仅作为 poll 的 `afterSequence` sentinel |
| `timestampUtcUs` | 源端时间，Unix epoch UTC 微秒；来自 `spdlog::log_msg.time` |
| `level` | 0 Trace, 1 Debug, 2 Info, 3 Warn, 4 Error, 5 Critical |
| `category` | `msg.logger_name`；默认 `"Engine"` |
| `source` | `file:line`；不含函数名；可为空 |
| `message` | 格式化后的 payload；去除尾部 CR/LF，内部 CR/LF 转空格 |
| `flags` | category/source/message 截断标记 |

### 3.3 截断与编码

- 字符串一律 UTF-8。
- 长度字段表示实际有效字节数，不包含 NUL terminator。
- 未使用的数组空间填零；**不要求** NUL terminator。
- 超过上限时截断到上限，并在对应 flags 位置位；不得截断到半个 UTF-8 codepoint；
  实现按完整 codepoint 边界截断。
- 空 message 合法；空 category/source 允许，但标准 logger 应至少提供 category。

### 3.4 容量与溢出

正式冻结：

```text
Native Ring Capacity : 4096 records
Poll Batch Maximum   : 32 records
Poll Interval        : 50 ms
Message Capacity     : 2048 bytes
Category Capacity    : 64 bytes
Source Capacity      : 256 bytes
```

行为：

- 保留最新记录，淘汰最旧的未消费记录。
- `sequence` 按接收顺序单调递增；不依赖时间戳排序。
- ring 溢出时累计 `droppedTotal`；该计数是进程生命周期累计值，不因 poll 清零。
- Poll 返回记录数量与累计 `droppedTotal`。
- 单次 poll 不得超过 32 条。
- 空队列返回成功、记录数量为零。
- 一个 managed poll consumer，避免多个消费者竞争同一队列。
- Ring 内部同步实现由 Gate B 根据现有线程模型选择，但必须保证多生产者写入安全、
  有界内存和 sink 不阻塞。

### 3.5 生命周期

| 事件 | 行为 |
|---|---|
| `Log::Init` | 创建/安装 LogRing；幂等，不重复添加 sink |
| `Log::Shutdown` | 当前 Application/EngineHost 不调用；若未来调用，ring 可停止/清空 |
| EngineHost 初始化 | 不创建/销毁 LogRing；首次 poll 从最旧可读 sequence 开始 |
| EngineHost 销毁 | LogRing 继续存在；host 不持有 sink 指针，无 detach/UAF |
| 进程退出 | 允许 ring 未清空；不持久化 |

---

## 4. Bridge ABI 契约

### 4.1 版本与不变项

- `ENGINE_HOST_ABI_VERSION`：v1.8 → **v1.9**。
- `EditorBridgeApi.EngineHostExpectedVersion` 同步为 v1.9；`Create()` lockstep 必须拒绝
  v1.8/v1.9 混搭。
- `EditorBridge_GetVersion`（session ABI）保持不变；T-6-B 不改变 `EditorSession` ABI。
- `EV_COUNT` 保持 14；不新增 `EV_*` 类型。
- `RuntimeResult` 布局保持不变（仍 76B）。
- 不新增 `EngineHost_Fetch` operation；日志只走新导出。

### 4.2 `EngineLogRecordV1`（FROZEN）

```c
#define ENGINE_HOST_LOG_CATEGORY_BYTES 64
#define ENGINE_HOST_LOG_SOURCE_BYTES 256
#define ENGINE_HOST_LOG_MESSAGE_BYTES 2048
#define ENGINE_HOST_LOG_RECORD_BYTES 2392
#define ENGINE_HOST_LOG_POLL_BATCH 32

typedef struct EngineLogRecordV1 {
    uint64_t sequence;          /* offset 0  */
    int64_t  timestampUtcUs;    /* offset 8  */
    uint8_t  level;             /* offset 16 */
    uint8_t  flags;             /* offset 17 */
    uint16_t categoryLength;    /* offset 18 */
    uint16_t sourceLength;      /* offset 20 */
    uint16_t messageLength;     /* offset 22 */
    char     category[64];      /* offset 24 */
    char     source[256];       /* offset 88 */
    char     message[2048];     /* offset 344 */
} EngineLogRecordV1;            /* size 2392, alignof 8 */
```

固定布局约束：

- 自然对齐 8 字节；不得依赖编译器默认 packing 恰好一致。
- 必须使用 `static_assert` 检查：
  - `sizeof(EngineLogRecordV1) == 2392`
  - `alignof(EngineLogRecordV1) == 8`
  - 所有字段偏移与上表一致。
- 不使用 `#pragma pack(1)`；结构体自然布局必须满足上述大小/偏移。
- 字符串 UTF-8；长度字段为实际有效字节数，不含 NUL terminator。
- 未使用的数组空间填零；不要求 NUL terminator。
- 截断必须在 UTF-8 字符边界进行。
- 跨 ABI 只传 POD；不得传递 C++ 对象、异常或需要 managed 释放的 native 字符串指针。

`flags`：

| Bit | 含义 |
|---|---|
| `0x01` | Category truncated |
| `0x02` | Source truncated |
| `0x04` | Message truncated |
| 其他 | v1 保留，必须为 0 |

Level 映射：

| Native level | 数值 |
|---|---|
| Trace | 0 |
| Debug | 1 |
| Info | 2 |
| Warn | 3 |
| Error | 4 |
| Critical | 5 |

`timestampUtcUs` 使用 Unix epoch UTC 微秒；`sequence` 是记录消费顺序的权威标识。

### 4.3 `EngineLogPollStatsV1`（FROZEN）

```c
typedef struct EngineLogPollStatsV1 {
    uint32_t structSize;        /* offset 0  */
    uint32_t recordCount;       /* offset 4  */
    uint64_t nextSequence;      /* offset 8  */
    uint64_t droppedTotal;      /* offset 16 */
    uint64_t oldestSequence;    /* offset 24 */
    uint64_t newestSequence;    /* offset 32 */
} EngineLogPollStatsV1;         /* size 40, alignof 8 */
```

约束：

- `structSize` 由 native 填写为 `sizeof(EngineLogPollStatsV1)`；managed 必须校验。
- `recordCount` 为本次 poll 写入的记录数（0..32）。
- `nextSequence` 为本次返回最后一条的 sequence；无记录时回显 `afterSequence`。
- `oldestSequence`/`newestSequence` 为 ring 当前可读范围；空 ring 时两者均为 0。
- `droppedTotal` 为进程生命周期累计值，poll 不清零。
- 必须有 `static_assert` 检查 offset/size/alignof。

### 4.4 `EngineHost_PollEngineLogs`（FROZEN）

```c
EDITOR_BRIDGE_API int32_t EngineHost_PollEngineLogs(
    EngineHostHandle h,
    uint64_t afterSequence,
    uint8_t* outBuffer,
    int32_t capacity,
    EngineLogPollStatsV1* stats,
    RuntimeResult* result);
```

语义：

- `afterSequence == 0`：从 ring 当前最旧可读记录开始；
- 否则只返回 `sequence > afterSequence` 的记录；
- 若 `afterSequence` 早于 `oldestSequence`，从最旧可读记录开始，并通过
  `droppedTotal`/`oldestSequence` 暴露缺口；
- `capacity` 必须至少为 `32 * 2392 = 76544` 字节；不足返回错误；
- 每次最多返回 32 条；返回值是记录数量（0..32），0 表示当前无记录，不是错误；
- `stats` 必填；`result` 必填；成功时 `result.status = SUCCESS`；
- 空队列返回成功、`recordCount = 0`；
- 调用线程：creation thread / managed UI 线程；不要求 runtime thread，不要求工程已打开；
- 不修改 `ConsoleLines`、不调用 `PumpOneFrame`、不改变 generation；
- host 已销毁/句柄无效时返回错误，不得访问已释放内存；
- API 使用当前 EngineHost 调用约定与现有 Host handle 类型。

`outBuffer` 按 `EngineLogRecordV1[32]` 解释；一次 poll 的字节数为
`recordCount * 2392`。不得部分写后伪成功。

---

## 5. Managed 接入契约

### 5.1 Log Pump 生命周期

| 操作 | 预期行为 |
|---|---|
| Host Initialize | 初始化 Engine Log Poll |
| Poll 正常运行 | 每 50 ms 最多消费 32 条 |
| Project Open | 继续运行 |
| Project Close | 继续运行 |
| Project Switch | 继续运行 |
| Host Dispose | 先停止 timer 并等待当前 poll 完成 |
| Native Shutdown | 停止所有后续 poll |

- `EditorHostService` 在 host 可用且 managed 日志订阅端已挂接后启动日志 timer；
  若 host 先创建，LogRing 保留积压，首次 poll 从 `afterSequence=0` 取回，
  不得在订阅前丢弃 bootstrap/早期日志。
- 日志 timer 是独立 `DispatcherTimer`（50 ms），不调用 `PumpOneFrame`/`RuntimeTick`，
  不计入 `PumpInvocations`/`PumpFailures`/runtime frame 统计。
- 必须覆盖 `PumpOwnershipGate` 等价断言：日志 timer 不得增加 runtime pump 计数或推进帧。
- 必须有取消与完成等待机制；Host Dispose 后不得继续调用 native export。
- 不新增 Native 音频/日志线程；不改变 T-6-A 事件生命周期。

### 5.2 Cursor、批量与丢弃

- managed 维护 `_engineLogCursor`，初始为 0；
- 每次 poll 最多 32 条，使用 `stats.nextSequence` 更新 cursor；
- 一个 managed consumer；不得并发启动第二个 poll；
- 若 `oldestSequence > cursor + 1`，说明 LogRing 已覆盖未读记录；累计
  `EngineLogDroppedCount`，按缺口数计数，不伪造缺失行；
- 每次 poll 到 UI 线程后一次性追加本批；不得每行单独 Dispatcher.Post；
- UI 线程不可用/Disposed 时丢弃本批并计数，不阻塞 native。

### 5.3 Console 行映射

冻结格式：

```text
[HH:mm:ss.fff] [LEVEL] [category] message (source:line)
```

- 第一个 `[HH:mm:ss.fff]` 使用源端时间（`timestampUtcUs` 转换到本地时区）；
- `LEVEL` 使用 `TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` 稳定名；
- `category` 直接来自记录；
- `message` 已截断的消息；若 `0x04` 置位，追加 `[truncated]` 或等价标记；
- `source` 可选；没有 source 时省略括号；
- 不得在 Engine 日志行前再叠加 UI 接收时间；必须新增
  `AppendEngineLogLine`/等价入口，不能直接交给现有 `LogToConsole` 再产生第二个时间戳。
- T-6-A 过滤仍按第一个 `] ` 前缀后文本匹配；level/category 会进入过滤文本，
  这是已知行为，不新增级别过滤。

### 5.4 容量、清空与重复

- `ConsoleLines` 仍为 10,000 行上限；Engine 日志行与 EV/本地/边界行一起计数。
- `ConsoleTrimmedCount` 表示 UI 行淘汰；另有 `EngineLogAcceptedCount` /
  `EngineLogDroppedCount`，不得混用。
- Console Clear 是 **presentation-only**：删除当前显示行，不清空 native LogRing；
  尚未 poll 的日志可能在下一次 poll 后重新出现；不新增 native 清空导出。
- 过滤只影响 `ConsoleView`，不改变 `ConsoleLines` 或 native LogRing。
- v1 显示全部实际到达 sink 的级别与全部 category，包括 `[EditorBridge]`；
  与 EV/VM 行重复是已知行为。
- 不增加自动去重、黑名单或默认级别过滤；未来若需要真正丢弃 pending native logs，
  另立契约。
- 既有 Console 文本/计数 gate 不得改写来迁就新增日志行；若新增日志导致既有断言
  冲突，必须记录并请求裁决，不能放宽断言。

### 5.5 Generation 与 T-6-A 隔离

- Engine 日志不进入 `OpenAttemptTracker`；
- 不增加/减少 `DroppedLateEventCount`；
- `CloseProject` 的 `_suppressEvents=1` **不抑制** Engine 日志；host 存活期间日志继续；
- Project Open/Close/Switch 不改变 Engine 日志通道；
- T-6-A 的 EV 事件、边界行、过滤/清空行为保持不变。

---

## 6. 失败、背压与可观测性

| 场景 | 冻结要求 |
|---|---|
| ring 满 | 保留最新、淘汰最旧未消费；`droppedTotal` 增加；sink 不阻塞 |
| managed 未及时 poll | 用 sequence 缺口/`droppedTotal` 计数；不伪造缺失行 |
| 超长 category/source/message | UTF-8 边界截断；对应 flags 置位 |
| malformed/空 payload | 原样或空消息入 ring；不崩溃 |
| poll buffer 太小 | 返回错误 + 可读诊断；不得部分写后伪成功 |
| host 未创建/已销毁 | 返回错误；不得访问已释放对象 |
| poll 期间 host teardown | 停止 log timer；未消费批次丢弃并计数 |
| log timer 失败 | 记录一次诊断/计数；不得无限静默重试 |
| UI Disposed | 丢弃本批并计数；不得从后台触碰 UI |
| file/stdout sink | 保持原格式与轮转；LogRing 是附加 sink |

**禁止**：

- 在 spdlog worker/调用线程中调用 managed/UI、等待 Dispatcher、等待 runtime pump；
- 用无界队列承载日志；
- 把 ring 丢弃描述为“完整显示”；
- 用 ViewModel 注入/假日志替代真实 EngineHost/Engine 日志证据。

---

## 7. Gate B 验收门禁

以下为实施验收要求；本轮不实施。

| 门禁 | 验收内容 |
|---|---|
| Native Ring | 多线程写入、4096 容量、溢出淘汰、sequence、`droppedTotal` |
| ABI | `EngineLogRecordV1` 2392B、字段偏移/`alignof`/调用约定、UTF-8 字段 |
| Bootstrap | `Log::Init` 到首次 poll 之间的日志可取回；溢出有 dropped 证据 |
| Logger Coverage | default/category/后续注册 logger 均被捕获，且无重复 sink |
| Poll | 32 条上限、空队列、部分读取、cursor/dropped 语义 |
| Managed | Engine 日志不受 Project generation 影响；T-6-A 语义不变 |
| Lifecycle | Host 初始化、Dispose、Shutdown 和 timer 竞态；无 post-dispose export |
| UI | 真实 Console 中 level/category/source/message 可见 |
| UIA | 真实 `EditorSession`/Engine 日志经过真实窗口消费路径 |
| Regression | Debug/Release、`test_bridge`、`test_hrc`、全量 CTest |

测试矩阵：

| ID | 场景 | 层级 |
|---|---|---|
| T6B-01 | 真实 project open/编辑操作产生的 Engine 日志进入 Console，字段正确 | EngineHost + Console |
| T6B-02 | bootstrap 日志从 `Log::Init` 起可取回；溢出有 dropped 计数 | 真实进程 |
| T6B-03 | default + category + 后续注册 logger 都进入 ring，无重复 | native + managed |
| T6B-04 | 多线程写入、4096 溢出、sequence 单调、`droppedTotal` 正确 | native |
| T6B-05 | Poll 32 上限、空队列、部分读取、cursor 前进正确 | native + managed |
| T6B-06 | 工程 Open/Close/Switch 期间日志不受 generation 影响；T-6-A 边界行仍恰好一次 | gate |
| T6B-07 | Clear 只清显示；未 poll 日志可能再次出现；ring 未被清空 | 真实窗口 |
| T6B-08 | Host Dispose/Shutdown 停止 timer、无 post-dispose export、无 late callback/UAF | process/UIA |
| T6B-09 | 截断/空消息/超长字段/malformed 的 flags/长度正确，不崩溃 | native |
| T6B-10 | ABI v1.8/v1.9 lockstep 拒绝；v1.9 正常 | test_bridge |
| T6B-11 | Legacy 不显示 Engine 日志，且不崩溃/不伪装成功 | gate |
| T6B-12 | Debug/Release、真实窗口/UIA、test_bridge/test_hrc/CTest 回归 | build/test |

证据纪律：

- Debug/Release 分开记录；
- 真实窗口/UIA 与 native 证据分开；
- 不把 F2-B/T-1/T-5/T-6-A 的既有通过数计为 T-6-B 新增；
- 不以 VM-only 测试替代真实 Console 可见性；
- ViewModel 测试注入不能替代真实 Engine 日志证据；
- 失败保留首次记录，不用重跑覆盖；
- 每个测试先 drain 到当前 `newestSequence` 再执行自己的日志动作，不依赖 ring 初始为空；
  没有 test-only reset API；
- 既有 Console 文本断言 gate（BootstrapTriggerGate、Hrc1Gate、Phase3Gate、
  ProductionPickGate、UiStateGate、ProductionPathGate）不得被改写；新增日志若造成
  冲突，按失败处理并请求裁决。

---

## 8. 变更白名单（若 Gate B 获授权）

允许修改/新增：

| 文件 | 允许内容 |
|---|---|
| `engine/include/Engine/Core/Log.h`、`engine/src/Core/Log.cpp` | LogRing/sink 安装与生命周期 |
| `engine/include/Engine/Core/LogRing.h`、`engine/src/Core/LogRing.cpp`（新增） | 有界 ring、sequence、截断、poll |
| `bridge/include/editor_bridge/capi.h` | `EngineLogRecordV1`/`EngineLogPollStatsV1`、新导出、ABI v1.9 |
| `bridge/src/capi.cpp` | `EngineHost_PollEngineLogs` 转发 |
| `bridge/src/EngineHost.h/.cpp` | poll 方法/LogRing 读取；不改变 EV/session 语义 |
| `editor_avalonia/Native/EditorBridgeApi.cs` | P/Invoke、版本常量、POD 结构镜像 |
| `editor_avalonia/Services/EditorHostService.cs` | log timer、cursor、计数、host 生命周期 |
| `editor_avalonia/ViewModels/MainViewModel.cs` | Engine 日志行入口、Console 容量/过滤兼容 |
| `editor_avalonia/Services/T6BLoggingGate.cs`（新增） | 真实窗口/gate |
| `tests/EditorAvalonia.IntegrationTests/T6BConsoleLoggingUITests.cs`（新增） | UIA 真实控制台日志 |
| `tools/run_t6b_logging_gate.ps1`（新增） | 构建 + gate + UIA |
| `tests/test_core/*`、`tests/test_bridge/*`、`tests/test_hrc/*`、`tests/CMakeLists.txt` | ring/ABI/生命周期测试 |
| `docs/T-6-B-*` | 契约、实现、测试、关闭记录 |

禁止修改（未经再次裁决）：

- `bridge/src/EditorSession.cpp` 的 EV 事件语义；
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

## 10. Gate A 最终状态

| 项目 | 状态 |
|---|---|
| Gate 0 | `PASS — READ-ONLY` |
| Gate A | `PASS — CONTRACT FROZEN — SCOPE-BOUND` |
| 独立 native 日志通道 | 已批准 |
| Native Ring 4096 | 已冻结 |
| Poll Batch 32 / 50 ms | 已冻结 |
| Message 2048 B | 已冻结 |
| EngineHost ABI v1.9 | 已冻结 |
| Bootstrap 从 `Log::Init` | 已冻结 |
| 全级别 / 全 Category | 已批准 |
| `[EditorBridge]` 重复 | 已接受 |
| Legacy backend | 明确排除 |
| T-6-A | 保持 CLOSED |
| Gate B | 本次尚未授权源码实施 |

---

## 11. 裁决历史

- 原始草案完整保留于
  `docs/T-6-B-Console-Logging-GateA-Contract-Draft.md`。
- 2026-10-31 最终裁决保留 A-1～A-12 原编号，并取代草案中的以下提案值：
  - 草案 `EngineLogEntry`（packed，2404B，int32 level，flags bit0 message/bit1 category/bit2 source）
    → 最终 `EngineLogRecordV1`（自然对齐，2392B，uint8 level/flags，uint16 lengths，
    flags 0x01 category/0x02 source/0x04 message）。
  - 草案 poll 100 ms → 最终 50 ms。
  - 草案 `EngineLogPollStats`（未冻结布局）→ 最终 `EngineLogPollStatsV1`（40B 固定布局）。
  - 草案 A-2～A-12 的待裁决状态 → 本文件 §0 最终裁决。
- 原草案的提案表、旧 ABI 字段与 100 ms 计时仍保留在 Draft 文件中，仅作历史证据，不再生效。

---

## 12. 本冻结轮次未做

- 未修改 `Log`、`EngineHost`、`EditorSession`、`EditorHostService`、`MainViewModel`、
  `capi.h` 或任何测试；
- 未构建、未运行测试、未启动编辑器；
- 未新增 ABI、日志 sink、log timer 或测试；
- 未 commit / push / tag / merge；
- 未清理脏子模块或 `crashes/`。

Gate A 已完成裁决。Gate B 的实现授权仍需单独明确；进入 Gate B 后严格限定为
Engine 日志通道、native LogRing、managed poll、Console 显示和对应门禁，
不扩大到 Legacy backend 或 T-6-A generation 重构。
