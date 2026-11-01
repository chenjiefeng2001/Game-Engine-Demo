# T-6-B — Gate B6 Record · 最终回归 / Closure

**状态**：`T-6-B B6 — COMPLETE — T-6-B CLOSED`
**日期**：2026-11-01
**基线**：HEAD `e461bd4`（B5 提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B6

---

## 1. 变更范围（B6 白名单）

B6 为最终回归/closure slice，**未修改源代码**。仅执行全量回归并撰写本 closure record。

---

## 2. 全量回归结果（Debug + Release）

| 目标 | Debug | Release | 备注 |
|---|---|---|---|
| `test_core` | 71/71 | 71/71 | Vector3 9 / Matrix4 3 / StackAllocator 8 / Time 23 / LogRing 20 / LogRingIntegration 8 |
| `test_bridge` | 112/112 | 112/112 | B0 103 + B3 5 + B2 4 新增 |
| `test_pick_transport` | 9/9 | 9/9 | |
| `test_hrc` | 15/15 | 15/15 | |
| `T6BLoggingAbiTests` | 4/4 | 4/4 | managed Marshal 布局 + lockstep |
| `T6ConsoleUITests` | 1/1 | 1/1 | 真实窗口 UIA（9–10 s） |
| `AvaloniaEditor` 构建 | 0 error | 0 error | 6 既有 warning |

所有目标 0 失败。

---

## 3. 交付物总览（B0–B6）

### 3.1 Native

- `engine/include/Engine/Core/LogRing.h` / `engine/src/Core/LogRing.cpp`：4096 容量 ring、sequence、UTF-8 截断、溢出、poll cursor。
- `engine/include/Engine/Core/Log.h` / `engine/src/Core/Log.cpp`：`LogRingSink` 安装、`apply_all` 补齐、`GetLogRing()`。
- `bridge/include/editor_bridge/capi.h`：v1.9、`EngineLogRecordV1`（2392B）/`EngineLogPollStatsV1`（40B）、`EngineHost_PollEngineLogs`、static_assert。
- `bridge/src/capi.cpp`：live-host 注册表 + 薄转发。
- `bridge/src/EngineHost.h` / `EngineHost.cpp`：`PollEngineLogs` 实现。

### 3.2 Managed

- `editor_avalonia/Native/EditorBridgeApi.cs`：v1.9、常量/结构体/P/Invoke 镜像。
- `editor_avalonia/Services/EditorHostService.cs`：50 ms log timer、`PollEngineLogsOnce`、`StartLogPoll`/`StopLogPoll`、`EngineLogsPolled` 事件。
- `editor_avalonia/ViewModels/MainViewModel.cs`：`EngineLogDroppedCount`、`OnEngineLogsPolled` 格式化、`AppendConsoleLine`。

### 3.3 测试

- `tests/test_core/log/LogRingTest.cpp`：28 个新用例（B1）。
- `tests/test_bridge/test_editor_capi.cpp`：B2 布局 + B3 poll/lifecycle + B2 smoke（112 总）。
- `tests/EditorAvalonia.IntegrationTests/T6BLoggingAbiTests.cs`：managed Marshal 布局 + lockstep 反射（4 用例）。
- `tests/EditorAvalonia.IntegrationTests/T6ConsoleUITests.cs`：真实窗口 Console 交互（既有 + B5 验证）。

### 3.4 文档

- `docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN）
- `docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md`
- `docs/T-6-B-Console-Logging-GateB0-Baseline.md`
- `docs/T-6-B-Console-Logging-GateB1-LogRing-Record.md`
- `docs/T-6-B-Console-Logging-GateB2-Abi-Record.md`
- `docs/T-6-B-Console-Logging-GateB3-Poll-Record.md`
- `docs/T-6-B-Console-Logging-GateB4-Managed-Record.md`
- `docs/T-6-B-Console-Logging-GateB5-Production-Record.md`
- `docs/T-6-B-Console-Logging-GateB6-Closure.md`（本文件）

---

## 4. ABI 冻结状态

- `ENGINE_HOST_ABI_VERSION` = v1.9 (`((1 << 16) | 9)`)。
- `EngineLogRecordV1` = 2392B，alignof 8，冻结偏移（0/8/16/17/18/20/22/24/88/344）。
- `EngineLogPollStatsV1` = 40B，偏移 0/4/8/16/24/32。
- `EV_COUNT` = 14（不变），`RuntimeResult` = 76B（不变），pick packet = 32B（不变）。
- v1.8/v1.9 混搭在 `Create()` lockstep 处被拒绝（反射实测）。

---

## 5. 纪律与边界确认

- 未修改 T-6-A EV/generation/过滤/清空语义；
- 未触碰 3 个脏子模块（`third_party/d3d12ma`、`openal-soft`、`vma`）与 `crashes/`；
- 未修改 ConsolePanel.cpp / 任何 XAML；
- 未新增 op/status/结构体 beyond v1.9 additive；
- 所有 slice 均经用户明确授权后实施。

---

## 6. T-6-B 结论

**T-6-B 状态**：`COMPLETE — ALL GATES PASSED (B0–B6)`。

引擎日志全链路已落地：
native LogRing（4096 容量）→ spdlog sink 覆盖 → `EngineHost_PollEngineLogs`（v1.9 ABI）→
managed 50 ms timer → ConsoleLines（10k 上限、过滤、Clear 仅清显示）。

全量回归（Debug/Release）：native 207 + managed 4 + UIA 1，全部通过。
