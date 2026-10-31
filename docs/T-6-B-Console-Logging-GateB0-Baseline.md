# T-6-B — Gate B0 Baseline Record

**状态**：`T-6-B B0 — COMPLETE — BASELINE RECORDED; TEST_BRIDGE DEBUG FLAKE RETAINED`
**日期**：2026-10-31
**基线**：HEAD `09deac9`，分支 `avalonia`，相对 `origin/avalonia` ahead 15
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN）
**Gate B**：`NOT AUTHORIZED`（B0 只做基线；未开始源码实现）
**本轮未做**：未修改源码/测试/构建；未 commit / push / tag / merge；
未清理脏子模块或 `crashes/`。

---

## 1. 工作树与 ABI 基线

| 项目 | 观测值 |
|---|---|
| HEAD | `09deac9 docs(t-6-b): prepare gate B experiment plan` |
| 分支 | `avalonia`，ahead 15 |
| 脏子模块 | `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` |
| 未跟踪 | `crashes/` |
| `capi.h` SHA-256 | `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886` |
| `capi.h` git hash-object | `630ed7bf6f79ee0c68fd13ff741cef96eb7571f3` |
| `ENGINE_HOST_ABI_VERSION` | `((1 << 16) \| 8)` = v1.8 |
| managed expected version | v1.8（`EditorBridgeApi.EngineHostExpectedVersion`） |
| `EV_COUNT` | 14（`capi.h:89`；managed `EvCount=14`） |
| `RuntimeResult` | 76B（`tests/test_bridge/test_editor_capi.cpp:99`；`EngineHostAbiTest.ResultLayout` 通过） |

当前未存在 T-6-B 符号：

- `EngineLogRecordV1`：无；
- `EngineLogPollStatsV1`：无；
- `EngineHost_PollEngineLogs`：无；
- `LogRing`/`EngineLogRing`：无。

`capi.h` 哈希与 T-6-A 关闭记录中的 T-2 基线一致；T-6-B B0 没有改动该文件。

---

## 2. 测试基线

测试二进制来自当前源码（HEAD 只有文档提交，源码未变）；本轮未重新构建。

| 目标 | 配置 | 结果 | 日志 |
|---|---|---|---|
| `test_bridge` | Debug（首次） | **102/103**；失败 `EditorBridgeTest.T1FEndToEndPhysicsSaveReopenPlayStopReplay`，`remove_all: The process cannot access the file because it is being used by another process: "gp01_editor_scratch/gate_p3"` | `build/t6b_b0_test_bridge_debug.log` |
| `test_bridge` | Debug（重试） | **103/103** | `build/t6b_b0_test_bridge_debug_retry.log` |
| `test_bridge` | Release | **103/103** | `build/t6b_b0_test_bridge_release.log` |
| `test_hrc` | Debug | **15/15** | `build/t6b_b0_test_hrc_debug.log` |
| `test_hrc` | Release | **15/15** | `build/t6b_b0_test_hrc_release.log` |
| `test_core` | Debug | **43/43** | `build/t6b_b0_test_core_debug.log` |
| `test_core` | Release | **43/43** | `build/t6b_b0_test_core_release.log` |

### 2.1 `test_bridge` Debug 首次失败的处置

首次失败发生在 `T1FEndToEndPhysicsSaveReopenPlayStopReplay` 的 scratch teardown：
`MakeScratchFacing()` 的 `remove_all("gp01_editor_scratch/gate_p3")` 被另一个进程/句柄占用。
同一测试在 fresh process 重试后 **103/103**，Release **103/103**。

处置：

- 保留首次失败日志与失败计数，不删除/覆盖；
- 归类为既有 `test_bridge` scratch 隔离/占用问题，不是 T-6-B 日志通道失败；
- 本轮没有修改测试夹具来掩盖该失败；
- B0 结论标记为 **COMPLETE with retained flake**，不是 clean 103/103 Debug。

T-1-B 关闭记录已记录过同类 scratch 锁现象；B0 不把它升级为 T-6-B 阻塞项，但会作为
Gate B 回归的已知基线噪声保留。

### 2.2 测试计数与旧基线文件的差异

`docs/Evidence-Baseline.md` 仍记录 `test_core=20`、`test_bridge=44`、`test_physics=54`
等旧计数；当前实际为 `test_core=43`、`test_bridge=103`、`test_hrc=15`。
本轮 B0 只记录实际运行值，不修改全局 Evidence-Baseline；该表的陈旧问题仍作为独立
基线维护项，不计入 T-6-B。

---

## 3. B0 结论

- ABI 仍是 v1.8，`EV_COUNT=14`，`RuntimeResult=76B`，`capi.h` 哈希未变；
- `test_hrc` / `test_core` Debug/Release 全绿；
- `test_bridge` Debug 首次 102/103（scratch 锁），重试 103/103；Release 103/103；
- 首次失败已保留并归类为既有 fixture 噪声；
- 未开始 B1；未修改源码/测试/构建；未执行 Git 写入。

**B0 状态**：`COMPLETE — BASELINE RECORDED`（附 `test_bridge` Debug scratch flake）。
**下一停点**：等待用户明确授权 Gate B 源码实施（从 B1 native LogRing 开始）。
