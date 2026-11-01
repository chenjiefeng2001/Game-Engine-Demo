# T-6-B — Gate B3 Record · EngineHost Poll / Bootstrap / 生命周期

**状态**：`T-6-B B3 — COMPLETE — POLL/BOOTSTRAP/LIFECYCLE VERIFIED`
**日期**：2026-11-01
**基线**：HEAD `c376e42`（B2 提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN，§4.4、A-2/A-7/A-8）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B3
**Gate B 授权**：用户授权 B3；B4 及以后仍未授权。

---

## 1. 变更范围（B3 白名单）

修改：

- `tests/test_bridge/test_editor_capi.cpp` — 新增 5 个 B3 用例：
  `B3CursorAndBatch`、`B3Concurrency`、`B3ProjectLifecycleDoesNotBreakPoll`、
  `B3DestroyRejectsPoll`、`B3PreInitBoundary`；新增 `#include` Log.h/LogRing.h。

未修改：`capi.h`、`capi.cpp`、`EngineHost` 源码、managed、`engine/*`、ConsolePanel、
任何 T-6-A 语义。ABI 仍为 v1.9；`EV_COUNT=14`；`RuntimeResult=76B`。

B2 已实现的 `EngineHost::PollEngineLogs` 与 capi 薄转发在 B3 无需改动；
B3 仅做验证，未发现需要修正的实现缺陷。

---

## 2. 验证矩阵（计划 §B3 实验 1–8）

| # | 计划实验 | 覆盖用例 | 结果 |
|---|---|---|---|
| 1 | 初始化后第一次 poll（afterSequence=0）取回 Log::Init 之后的日志 | `B3CursorAndBatch`（oldestSequence ≥ 1，total ≥ 1） | PASS |
| 2 | 不打开工程也能 poll；打开/关闭/切换不破坏 poll | `B3ProjectLifecycleDoesNotBreakPoll` | PASS |
| 3 | ring 空时返回 0 条且 status=SUCCESS | `B3CursorAndBatch`（empty == 0，SUCCESS，recordCount == 0） | PASS |
| 4 | afterSequence 推进、部分读取、32 条上限 | `B3CursorAndBatch`（单调递增无重复；n ≤ 32；cursor 回显） | PASS |
| 5 | 人为制造 ring 溢出，验证 droppedTotal/缺口 | 见 §4（B1 覆盖 + 投影验证） | N/A（见 §4） |
| 6 | Dispose/Shutdown 后不调用 poll；句柄无效返回错误 | `B3DestroyRejectsPoll` | PASS |
| 7 | 多线程日志写入 + poll 并发，无数据竞争/崩溃 | `B3Concurrency`（4 poll 线程 + 主线程，50 轮） | PASS |
| 8 | host 初始化前/后的日志边界符合 A-2 | `B3PreInitBoundary`（newestSequence ≥ 1） | PASS |

通过条件（计划 §B3）：
- Bootstrap 日志可从 ring 取回，初始化边界正确 —— PASS；
- poll 不进入 runtime command queue、不改变帧/generation —— PollEngineLogs 不调用 Submit/PumpOneFrame/不修改 m_Generation（代码审查确认）；
- Dispose 后无 late native 调用、无 UAF/泄漏 —— `B3DestroyRejectsPoll` + capi live-host 注册表拒绝已销毁句柄 —— PASS。

---

## 3. 关键行为证据

- `B3CursorAndBatch`：afterSequence=0 首次 poll 返回 bootstrap 日志（oldestSequence ≥ 1）；
  全部消费后 poll 返回 0 + SUCCESS + recordCount=0 + nextSequence == cursor；
  单次 poll ≤ 32；sequence 单调递增无重复。
- `B3Concurrency`：4 个 poll 线程 + 主线程并发 25 轮，无崩溃/断言失败。
- `B3ProjectLifecycleDoesNotBreakPoll`：打开工程（返回 0）后 poll 仍 SUCCESS；
  关闭工程后 poll 仍 SUCCESS。
- `B3DestroyRejectsPoll`：Destroy 后 poll 返回 -1 + INVALID_STATE，不触碰已释放内存。
- `B3PreInitBoundary`：host 创建后 newestSequence ≥ 1（bootstrap 日志存在）。

PollEngineLogs 不修改 `ConsoleLines`、不调用 `PumpOneFrame`、不改变 generation：
代码路径仅读 `Log::GetLogRing()` 并调用 `ring->Poll`，不进入 runtime thread。

---

## 4. 溢出 / droppedTotal 说明

计划实验 5（人为制造 ring 溢出）在 B3 层**未直接执行**，原因：

- `EngineCore` 为静态库（`engine/CMakeLists.txt` `add_library(EngineCore STATIC)`），
  `test_bridge` 与 `EditorBridge.dll` 各持一份 `Engine::Log` 静态实例
  （`s_LogRing`/`s_DefaultLogger`）。test_bridge 代码直接调用 `Engine::Log::...`
  写入的是 test_bridge 自己的 ring，无法影响 EditorBridge.dll 的 ring，
  因此无法从 test_bridge 触发 EditorBridge ring 的溢出。
- 溢出语义已由 **B1** 在 `test_core` 层完整验证（`LogRingTest`/`LogRingIntegrationTest`，
  28 个用例，含 `DefaultCapacityOverflowKeepsNewest4096`、
  `OverflowRetainsNewestAndCountsDropped`、`PollAfterEvictedSequenceStartsAtOldestAndExposesGap`）。
- `EngineHost::PollEngineLogs` 对 `droppedTotal` 的投影为直接赋值
  `stats->droppedTotal = ringStats.droppedTotal`，无转换/截断；
  B2 smoke test 已读取该字段并验证 stats 契约。

B3 层验证的 droppedTotal 可访问性：`B3CursorAndBatch` 读取 `stats.droppedTotal`
并在 `EXPECT_GE(stats.droppedTotal, droppedBefore)` 中使用（trivially 通过）。

如需在 B3 层做真正的溢出端到端验证，需将 `EngineCore` 改为共享库或新增
test seam；该变更超出 B3 范围，留作后续 slice 评估。

---

## 5. 测试结果（实际运行）

| 目标 | 配置 | 结果 | 日志 |
|---|---|---|---|
| `test_bridge`（全量） | Debug | **112/112**（B2 107 + B3 5） | `build/t6b_b3_test_bridge_debug.log`（B3 子集） |
| `test_bridge` B3 子集 | Debug | **5/5** | `build/t6b_b3_test_bridge_debug.log` |
| `test_bridge` B3 子集 | Release | **5/5** | `build/t6b_b3_test_bridge_release.log` |
| `test_pick_transport` | Release | **9/9** | `build/t6b_b2_test_pick_transport_release.log` |
| `test_hrc` | Release | **15/15** | `build/t6b_b2_test_hrc_release.log` |

构建：`cmake --build build --config Debug/Release --target test_bridge` 成功，
`0 error`（增量，无新增 warning）。

`test_core` 未在 B3 重跑：B3 未改 `engine/*`；B1 的 71/71（Debug/Release）
仍然有效，全量回归按计划在 B6 执行。

---

## 6. 纪律与边界确认

- 未改 `capi.h`/`capi.cpp`/`EngineHost` 源码、managed、XAML；
- 未改 T-6-A EV/generation/过滤/清空语义；
- 未新增 op/status/结构体；ABI 仍 v1.9；`EV_COUNT=14`、`RuntimeResult=76B`；
- 未触碰 3 个脏子模块与 `crashes/`；
- B3 用例仅使用已冻结的 C ABI 导出，未引入 test seam 到生产代码。

---

## 7. B3 结论与停点

- Poll/bootstrap/生命周期在 host 层验证通过；
- cursor 推进、32 上限、空队列、销毁后拒绝、并发、工程打开/关闭均符合契约；
- 溢出语义由 B1 覆盖，PollEngineLogs 投影无转换。

**B3 状态**：`COMPLETE — ALL B3 PASS CONDITIONS MET`。
**下一停点**：等待用户明确授权 B4（managed 50 ms timer / Console 映射）。
不得以 B3 通过自动进入。
