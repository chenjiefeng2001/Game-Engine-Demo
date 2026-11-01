# T-6-B — Gate B4 Record · Managed 50 ms Timer / Console 映射

**状态**：`T-6-B B4 — COMPLETE — MANAGED LOG POLL TIMER + CONSOLE MAPPING`
**日期**：2026-11-01
**基线**：HEAD `34cddbf`（B3 提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN，§5.1、A-8/A-11）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B4
**Gate B 授权**：用户授权 B4；B5 及以后仍未授权。

---

## 1. 变更范围（B4 白名单）

修改：

- `editor_avalonia/Services/EditorHostService.cs`（`EngineHostChannel`）：
  - 新增 `_logTimer`（`DispatcherTimer`，50 ms）、`_logCursor`、`_logActive`、`_logLock`；
  - `StartLogPoll()` / `StopLogPoll()` / `PollEngineLogsOnce()` / `MarshalPtrToRecord`；
  - `EngineLogsPolled` 事件；`LogCursor` 属性；
  - `Create()` 成功后调用 `StartLogPoll()`；`Dispose()` 调用 `StopLogPoll()`。
- `editor_avalonia/Services/EditorHostService.cs`（`EditorHostService`）：
  - 转发 `EngineLogsPolled` 事件、`StartEngineLogPoll()` / `StopEngineLogPoll()`。
- `editor_avalonia/ViewModels/MainViewModel.cs`：
  - `EngineLogDroppedCount` 属性；
  - 构造函数订阅 `_host.EngineLogsPolled`；
  - `OnEngineLogsPolled`：更新 dropped 计数，格式化记录追加到 `ConsoleLines`；
  - `ReadFixedString` / `FormatTimestamp` 辅助；
  - `AppendConsoleLine` 抽取（`LogToConsole` 改为调用它），避免引擎日志双重时间戳；
  - `using System.Collections.Generic;` / `using System.Text;`。

未修改：`capi.h`/`capi.cpp`/`EngineHost` 源码、`engine/*`、ConsolePanel XAML、
任何 T-6-A 语义。ABI 仍为 v1.9。

---

## 2. 实现要点

| 项 | 实现 |
|---|---|
| Timer 隔离 | `_logTimer` 与 `_pump` 完全独立：不同 DispatcherTimer、不同 Interval（50 ms vs 33 ms）、不同回调；log timer 不调用 `PumpOneFrame`/`RuntimeTick`，不增 `_pumpInvocations` |
| 单次 poll 上限 | `PollEngineLogsOnce` 调用 `EngineHost_PollEngineLogs(_host, _logCursor, buffer, 76544, ...)`；native 侧最多 32 条 |
| 游标 | `_logCursor` 仅在 poll 成功后推进到 `stats.NextSequence`；Clear 不重置游标 |
| 格式 | `[utc time] [LEVEL] [category] message (source:line)` + `[truncated: ...]`（flags 映射） |
| 10k 上限 | 复用 `LogToConsole` 的 cap 逻辑（`ConsoleCapacity=10_000`，`ConsoleTrimmedCount`） |
| Clear 语义 | `ClearConsole()` 仅清 `ConsoleLines`/`ConsoleView`/`ConsoleTrimmedCount`；native ring 与 `_logCursor` 不动；未 poll 的日志在下次 tick 出现 |
| dropped 计数 | `EngineLogDroppedCount` 累加 `stats.DroppedTotal` 增量 |
| 生命周期 | `Create()` 末尾 `StartLogPoll()`；`Dispose()` 调用 `StopLogPoll()`（停 timer、置 `_logActive=false`）；timer tick 检查 `_logActive`/`_host` |
| UI 线程 | DispatcherTimer 在 UI 线程触发；poll 同步执行；无跨线程 ConsoleLines 访问 |

---

## 3. 实验对照（计划 §B4）

| # | 计划实验 | 实现/覆盖 | 结果 |
|---|---|---|---|
| 1 | EditorHostService 启动 50 ms log timer，host 初始化后启动，dispose 停止 | `StartLogPoll` in Create, `StopLogPoll` in Dispose | PASS（编译 + UI 测试） |
| 2 | timer 不调用 PumpOneFrame/RuntimeTick，不增 PumpInvocations | log timer 回调仅 `PollEngineLogsOnce` | PASS（代码审查） |
| 3 | 每 tick 最多 32 条，UI 线程追加 | native 侧 clamp 32 + DispatcherTimer | PASS |
| 4 | 格式 `[source time] [LEVEL] [category] message (source:line)` | `OnEngineLogsPolled` 格式化 | PASS |
| 5 | flags → `[truncated]` | flags 映射 category/source/message | PASS |
| 6 | ConsoleLines 10k 上限、ConsoleTrimmedCount、EngineLogDroppedCount | `AppendConsoleLine` cap + dropped 计数 | PASS |
| 7 | Clear 仅清显示，native ring 不动，未 poll 日志下次出现 | ClearConsole 不触 cursor/ring | PASS（代码审查） |
| 8 | 保留 T-6-A 既有前缀语义 | LogToConsole 仍用于 EV/错误等 | PASS |
| 9 | Project Open/Close/Switch 不打断 timer | timer 独立于 project 状态 | PASS（代码审查） |
| 10 | Dispose 停 timer 并等待当前 poll 完成 | StopLogPoll 同步停 timer；poll 同步执行 | PASS（代码审查） |

通过条件：
- Console 可见 level/category/source/message —— PASS（格式化实现）；
- timer 与 runtime pump 完全隔离 —— PASS；
- Clear 不影响 native ring —— PASS。

---

## 4. 测试结果（实际运行）

| 目标 | 配置 | 结果 | 日志 |
|---|---|---|---|
| `AvaloniaEditor` 构建 | Debug | 0 error / 6 warning（既有） | `build/t6b_b4_managed_build_debug.log` |
| `AvaloniaEditor` 构建 | Release | 0 error | `build/t6b_b4_managed_build_release.log` |
| `EditorAvalonia.IntegrationTests` T6ConsoleUITests | Debug | **1/1**（启动 editor 8 s，无崩溃） | `build/t6b_b4_ui_test.log` |
| `EditorAvalonia.IntegrationTests` T6BLoggingAbiTests | Debug | **4/4** | `build/t6b_b4_managed_test.log` |

native 测试未在 B4 重跑：B4 未改 native；B3 的 112/112 仍有效。

---

## 5. 纪律与边界确认

- 未改 `capi.h`/`capi.cpp`/`EngineHost` 源码、`engine/*`；
- 未改 T-6-A EV/generation/过滤/清空语义；
- 未新增 ABI 导出；ABI 仍 v1.9；`EV_COUNT=14`、`RuntimeResult=76B`；
- 未触碰 3 个脏子模块与 `crashes/`；
- `EngineLogDroppedCount` 为 managed 累计值，Clear 不重置（与 native droppedTotal 语义一致）。

---

## 6. B4 结论与停点

- 50 ms 引擎日志 poll timer 已接入 managed 侧，与 runtime pump 完全隔离；
- Console 映射包含 level/category/source/message/timestamp/truncated flags；
- Clear 仅清显示，native ring 与游标不变；
- UI 测试通过（editor 启动无崩溃）。

**B4 状态**：`COMPLETE — ALL B4 PASS CONDITIONS MET`。
**下一停点**：等待用户明确授权 B5（真实窗口 / UIA / 高频压测）。
不得以 B4 通过自动进入。
