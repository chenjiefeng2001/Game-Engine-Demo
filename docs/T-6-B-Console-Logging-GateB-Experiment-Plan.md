# T-6-B — Engine Logging Integration · Gate B Experiment Plan

**状态**：`T-6-B GATE B — EXPERIMENT PLAN — READY — SOURCE IMPLEMENTATION NOT AUTHORIZED`
**日期**：2026-10-31
**基线**：HEAD `b3dc804`（T-6-B Gate 0 / Gate A 文档提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（`PASS — FROZEN`）
**Gate B 授权**：本轮**未授权源码实施**；本文件只准备实验切片、探针与验收方法。
**本轮未做**：未修改源码/测试/构建；未构建或运行测试；未执行 Git 写操作。

---

## 0. 实验目标

用最小、可独立验证的切片证明 Gate A 契约可实施，并把风险尽早暴露在 native/ABI 层，
而不是先做 UI 或先接生产编辑器。

五个待验证假设：

| ID | 假设 | 失败即停止 |
|---|---|---|
| H1 | `Log::Init` 在第一条日志前安装 LogRing sink，default/category/后续 logger 均被覆盖且无重复 | 出现 bootstrap 缺失或 category 漏接 |
| H2 | 4096 条 ring、32 条 batch、50 ms poll 在正常编辑日志下保持有界内存、可观测 drop | ring 溢出不可观测或 sink 阻塞 |
| H3 | `EngineLogRecordV1` 2392B/自然对齐与 `EngineLogPollStatsV1` 40B 在 C++/C# 两侧布局一致 | static_assert/offset/size 不匹配 |
| H4 | 独立 50 ms log timer 不推进 runtime pump，Project Open/Close/Switch 不丢弃 Engine 日志 | timer 影响帧/`PumpInvocations`，或日志被 generation 丢弃 |
| H5 | 真实 Console 显示 level/category/source/message；Clear 仅清显示；重复日志是已知行为 | UI 行不可见、Clear 清 native ring 或现有 gate 冲突 |

---

## 1. 实验边界

### 1.1 包含

- Engine default logger + category logger + 后续注册 logger；
- 进程级有界 LogRing sink；
- `EngineLogRecordV1` / `EngineLogPollStatsV1` / `EngineHost_PollEngineLogs`；
- EngineHost 生命周期与 managed 50 ms log timer；
- Console 行映射、容量、Clear/过滤兼容；
- Debug/Release、真实窗口/UIA、`test_bridge`/`test_hrc`/CTest。

### 1.2 不包含

- Legacy backend 日志迁移/重构；
- T-6-A EV/generation/边界行变更；
- 日志导出/持久化/折叠/跳转源码/正则；
- 替换 stdout/file sink；
- 其他日志框架；
- T-1/T-2/T-4/T-5/T-7/F2-S1 相关工作。

---

## 2. 实验切片

### B0 — 基线/护栏（只读）

**目标**：冻结实验起点，避免用旧二进制或混搭 ABI 做实验。

- 记录 HEAD、分支、脏子模块、`crashes/` 状态；
- 记录 `ENGINE_HOST_ABI_VERSION` 当前为 v1.8、`EV_COUNT=14`、`RuntimeResult=76B`；
- 记录 `test_bridge`/`test_hrc`/`test_core` 当前基线；
- 记录 `capi.h` 在实验前哈希；
- 不修改源码。

**通过条件**：所有基线可复现；没有未解释的工作树差异。

### B1 — Native LogRing 原型（Engine 内部）

**目标**：先证明 ring 本身正确，不接 ABI、不接 managed。

实验：

1. 在 `Log::Init` 创建进程级 `EngineLogRing`，并在第一条日志前安装 sink；
2. 覆盖 default logger、已注册 category logger、后续 `Log::Get` 创建的 logger；
3. 写入 1 条日志断言 sequence=1；
4. 多线程/多 logger 写入，断言 sequence 单调、无重复、ring 不越界；
5. 写满 4096 后再写，断言保留最新、淘汰最旧未消费、`droppedTotal` 增加；
6. 超长 category/source/message：UTF-8 边界截断、flags 正确、未使用空间为零；
7. 空消息、空 category/source 不崩溃；
8. 读接口按 `afterSequence` 返回记录，空 ring 返回 0 条。

**通过条件**：

- ring 有界内存；写入路径不阻塞、不调用 UI/managed；
- sequence/dropped/truncate/UTF-8 全部符合契约；
- 先 default、后 category、再动态注册 category 均被捕获且不重复；
- 不存在“只在 default logger 上挂 sink”的漏接。

**产物**：`tests/test_core`（或等价 Engine 单测）日志/结果；不接 Bridge。

### B2 — ABI 与静态布局

**目标**：在写 EngineHost 逻辑前先锁死 POD 布局。

实验：

1. 在 `capi.h` 新增 `EngineLogRecordV1`、`EngineLogPollStatsV1` 与
   `EngineHost_PollEngineLogs`；
2. `static_assert`：
   - `sizeof(EngineLogRecordV1) == 2392`，`alignof == 8`；
   - `offsetof(sequence)==0`、`timestampUtcUs==8`、`level==16`、`flags==17`、
     `categoryLength==18`、`sourceLength==20`、`messageLength==22`、
     `category==24`、`source==88`、`message==344`；
   - `sizeof(EngineLogPollStatsV1)==40`，字段偏移 0/4/8/16/24/32；
3. `capi.h`/managed P/Invoke 镜像使用同一常量/偏移；
4. 编译 Debug/Release，确认无 ABI 混搭；
5. 用 `.NET` 侧测试/反射断言 `Marshal.SizeOf` 与 native 一致；
6. `ENGINE_HOST_ABI_VERSION` 升到 v1.9；旧 v1.8 必须在 `Create()` lockstep 被拒绝。

**通过条件**：

- C++/C# 两侧 size/offset 全部一致；
- 不出现 pack(1) 与自然对齐混用；
- v1.8/v1.9 混搭在 Create 处失败，而不是运行期才失败；
- `EV_COUNT`、`RuntimeResult` 不变。

### B3 — EngineHost Poll / Bootstrap / 生命周期

**目标**：验证新导出与 host 生命周期，不先接 UI。

实验：

1. EngineHost 初始化后第一次 poll（`afterSequence=0`）取回 Log::Init 之后的日志；
2. 不打开工程也能 poll；打开/关闭/切换工程不破坏 poll；
3. ring 空时返回 0 条且 status=SUCCESS；
4. `afterSequence` 推进、部分读取、32 条上限；
5. 人为制造 ring 溢出，验证 `droppedTotal`/缺口；
6. EngineHost Dispose/Shutdown 后不调用 poll；句柄无效返回错误；
7. 多线程日志写入 + poll 并发，无数据竞争/崩溃；
8. host 初始化前/后的日志边界符合 A-2。

**通过条件**：

- Bootstrap 日志可从 ring 取回（或按契约明确溢出范围）；
- poll 不经过 runtime command queue，不改变帧/generation；
- Dispose 后无 late native 调用；
- 无 UAF、无死锁、无无界内存。

### B4 — Managed Poll 与 Console 行

**目标**：验证 50 ms timer、Console 映射、容量/Clear/过滤兼容。

实验：

1. `EditorHostService` 新增独立 50 ms log timer；host 初始化且订阅端挂接后启动；
2. timer 不调用 `PumpOneFrame`/`RuntimeTick`，不增加 `PumpInvocations`；
3. poll 每批最多 32 条，UI 线程一次性追加；
4. 行格式 `[source time] [LEVEL] [category] message (source:line)`；
5. 截断 flags 映射到 `[truncated]`；
6. `ConsoleLines` 10k 上限、`ConsoleTrimmedCount`、`EngineLogDroppedCount`；
7. Clear 只清显示，native ring 不清；未 poll 日志可能再次出现；
8. 过滤仍按 T-6-A 首前缀后文本；不新增级别过滤；
9. Project Open/Close/Switch 不改变日志 timer；
10. Dispose 先停 timer 并等待当前 poll 完成，之后不再调用 native。

**通过条件**：

- Console 可见 level/category/source/message；
- timer 与 runtime pump 计数完全隔离；
- Clear/Filter 语义与 Gate A 一致；
- 既有 Console gate 不被改写；若冲突，保留失败并请求裁决。

### B5 — 真实窗口 / UIA / 高频

**目标**：生产路径证据，不用 ViewModel 注入替代。

实验：

1. 真实 `EditorSession`/Engine 日志（如 project loaded、entity created、PLAY）经真实
   EngineHost 进入真实 Console；
2. UIA 读取真实 Console 行，断言字段/文本；
3. 多轮 open/close/switch，日志持续，T-6-A 边界行仍恰好一次；
4. 真实编辑循环制造可观测日志量，验证 ring/Console drop/trim 与 UI 响应；
5. host Dispose/Shutdown 后 UIA 进程不崩溃；
6. Legacy backend 不显示 Engine 日志且不崩溃。

**通过条件**：

- 真实 Engine 日志证据，不使用 test-only 注入；
- UIA 真实控件可见；
- 高频不造成 UI 卡死/进程崩溃；
- 失败保留首次记录。

### B6 — 回归与实验收口

**目标**：确认没有跨阶段污染。

- Debug/Release 构建；
- `test_core`/`test_bridge`/`test_hrc`/`test_physics`/`test_content` 定向；
- 全量 CTest；
- 既有 Console gate、`BootstrapTriggerGate`、`Hrc1Gate`、`Phase3Gate`、
  `ProductionPickGate`、`UiStateGate`、`ProductionPathGate` 不得改写；
- 记录实际警告；不宣称零警告/clean build；
- 记录 ABI/hash、脏子模块、`crashes/`；
- 形成 Gate B 测试记录与关闭记录（若后续授权）。

---

## 3. 实验顺序与停点

```text
B0 baseline
  -> B1 native LogRing (stop if sequence/overflow/truncate fails)
     -> B2 ABI layout (stop if size/offset/lockstep fails)
        -> B3 EngineHost poll/bootstrap/lifecycle (stop if host UAF/timer race)
           -> B4 managed timer/Console (stop if PumpOwnershipGate or T-6-A gates break)
              -> B5 real window/UIA/high-frequency (stop if production evidence missing)
                 -> B6 regression/closure
```

每个切片都必须先给出实际测试结果，再进入下一片；不得用后续切片掩盖前一片失败。

---

## 4. 关键探针

| 探针 | 观测 |
|---|---|
| sequence gap | native ring 的 sequence 与 poll cursor 对比 |
| droppedTotal | ring 溢出与 managed 未读缺口的累计计数 |
| sink coverage | default/category/动态 logger 是否都进入 ring，是否重复 |
| bootstrap | `Log::Init` 文本与首次 poll 可见性 |
| ABI layout | C++ `static_assert` + C# `Marshal.SizeOf`/offset 断言 |
| timer isolation | `PumpInvocations`/`PumpFailures`/frame 计数不变 |
| generation | Project Open/Close/Switch 前后日志不丢、`DroppedLateEventCount` 不变 |
| UI presentation | 真实 Console 行文本/level/category/source |
| Clear | 清 UI 后 native ring `droppedTotal`/内容不变 |
| lifecycle | Dispose 后无 poll、无 crash、无 late export |

---

## 5. 预期文件（授权后）

- Engine：`Log.h/.cpp`、新增 `LogRing.h/.cpp`；
- Bridge：`capi.h`、`capi.cpp`、`EngineHost.h/.cpp`；
- Managed：`EditorBridgeApi.cs`、`EditorHostService.cs`、`MainViewModel.cs`；
- Tests：`test_core` ring、`test_bridge` ABI/host、`test_hrc` lifecycle、
  managed `T6BLoggingGate.cs`、UIA `T6BConsoleLoggingUITests.cs`；
- Tools：`tools/run_t6b_logging_gate.ps1`；
- Docs：Gate B 测试记录、关闭记录。

禁止：Legacy 迁移、ConsolePanel、T-6-A generation/EV 修改、Collider/T-1/T-2/T-4/T-5/T-7/F2-S1。

---

## 6. 风险与缓解

| 风险 | 缓解 |
|---|---|
| `Log::Init` 首条日志早于 sink | 先建 ring/sink 再写第一条日志；B1 探针验证 |
| category logger 复制 sinks 顺序 | `apply_all` 补齐 + 动态 logger 探针 |
| 默认 logger 同步/category 异步 | ring 只用短锁复制；sink 不做 UI/阻塞 |
| ring 溢出不可观测 | `droppedTotal` + sequence gap；B1/B3 压测 |
| log timer 影响 runtime pump | 独立 timer + `PumpOwnershipGate` 等价断言 |
| ABI 自然布局漂移 | `static_assert` + C# offset/size 断言 + v1.9 lockstep |
| Clear 误清 native ring | presentation-only；B4/B5 明确断言 |
| 既有 Console gate 冲突 | 保留失败，不改写 gate；请求裁决 |
| 高频日志拖慢 UI | 32/batch、50ms、10k 行、drop/trim 计数；B5 压力探针 |
| host 销毁后 late poll | cancel + completion wait；B3/B4/B5 lifecycle |

---

## 7. Gate B 启动清单

在获得源码实施授权后，按顺序确认：

1. 基线 HEAD/工作树/ABI/hash 已记录；
2. B1 native ring 单测目标与命令已确定；
3. B2 ABI 静态断言目标与 C# 镜像测试已确定；
4. B3 EngineHost poll/bootstrap 测试环境已确定；
5. B4 managed timer 与 Console gate 已确定；
6. B5 UIA 运行器/真实窗口环境已确定；
7. 既有 gate 不得改写、失败保留、Debug/Release 分开记录。

**当前停点**：实验计划已准备；等待用户明确授权 Gate B 源码实施（B1 起）。
