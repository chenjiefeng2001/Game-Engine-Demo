# T-6-B — Gate B2 Record · ABI POD 冻结与 v1.9

**状态**：`T-6-B B2 — COMPLETE — LOG POD FROZEN — ABI v1.9 LOCKSTEP VERIFIED`
**日期**：2026-11-01
**基线**：HEAD `d23271a`（B1 提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN，§4.2–4.4）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B2
**Gate B 授权**：用户授权 B2；B3 及以后仍未授权。

---

## 1. 变更范围（B2 白名单）

修改：

- `bridge/include/editor_bridge/capi.h` — `ENGINE_HOST_ABI_VERSION` → v1.9；
  日志常量、`EngineLogRecordV1`、`EngineLogPollStatsV1`、`static_assert`、
  `EngineHost_PollEngineLogs` 声明；
- `bridge/src/capi.cpp` — live-host 注册表；`EngineHost_PollEngineLogs` 薄转发；
- `bridge/src/EngineHost.h` — `PollEngineLogs` 声明；
- `bridge/src/EngineHost.cpp` — `PollEngineLogs` 实现（布局投影，行为验证留 B3）；
- `editor_avalonia/Native/EditorBridgeApi.cs` — 版本常量 v1.9、常量/结构体/P/Invoke 镜像；
- `tests/test_bridge/test_editor_capi.cpp` — 布局与增量为 additive 的断言、
  poll 参数校验与 bootstrap smoke、旧版 pin 更新；
- `tests/test_bridge/test_pick_transport.cpp` — 旧版 pin 更新（仅版本号，布局断言不变）；
- `tests/EditorAvalonia.IntegrationTests/EditorAvalonia.IntegrationTests.csproj` —
  `Compile Link` 引入 `EditorBridgeApi.cs`（单一来源，无生产引用）。

新增：

- `tests/EditorAvalonia.IntegrationTests/T6BLoggingAbiTests.cs` — managed 侧
  `Marshal.SizeOf/OffsetOf` 断言 + 生产代码 lockstep 反射验证。

未修改：`engine/*`（B2 未触碰引擎代码）、`EditorSession`、ConsolePanel、XAML、
任何 T-6-A EV/generation/过滤/清空语义。

---

## 2. POD 布局（FROZEN，自然对齐）

`EngineLogRecordV1`（2392B，alignof 8）：

| 字段 | 偏移 | 类型 |
|---|---|---|
| `sequence` | 0 | `uint64_t` |
| `timestampUtcUs` | 8 | `int64_t` |
| `level` | 16 | `uint8_t`（0..5） |
| `flags` | 17 | `uint8_t` |
| `categoryLength` | 18 | `uint16_t` |
| `sourceLength` | 20 | `uint16_t` |
| `messageLength` | 22 | `uint16_t` |
| `category[64]` | 24 | `char` |
| `source[256]` | 88 | `char` |
| `message[2048]` | 344 | `char` |

`EngineLogPollStatsV1`（40B，alignof 8）：`structSize` 0 / `recordCount` 4 /
`nextSequence` 8 / `droppedTotal` 16 / `oldestSequence` 24 / `newestSequence` 32。

常量（`capi.h`，`EditorBridgeApi` 同值）：category 64 / source 256 / message 2048 /
record 2392 / batch 32 / buffer 76544；flags `0x01`/`0x02`/`0x04`。

对齐策略：两个结构体位于 `#pragma pack(pop)` **之后**，不使用 `pack(1)`；
`static_assert` 仅在 `#if defined(__cplusplus)` 内，C 消费者不受影响。
未出现 `pack(1)` 与自然对齐混用。

证据三层（同一组数字）：

1. 编译期 static_assert（`capi.h:746-771`）：size/alignof/全部 offset；
2. native 运行期 `EXPECT_EQ`（`EngineHostAbiTest.LogRecordLayoutIsFrozen`、
   `LogPollStatsLayoutIsFrozen`）；
3. managed `Marshal.SizeOf`/`Marshal.OffsetOf`
   （`T6BLoggingAbiTests.EngineLogRecordV1_MirrorMatchesFrozenLayout`、
   `EngineLogPollStatsV1_MirrorMatchesFrozenLayout`）。

---

## 3. 导出与 capi 薄转发

签名与契约 §4.4 完全一致。

校验顺序（`capi.cpp`）：`result`/`outBuffer`/`stats` 空指针 → host 非 live →
`capacity < 76544` → 转发 `EngineHost::PollEngineLogs`。失败统一
`INVALID_STATE` / `BOOTSTRAP`（销毁后为 `SHUTDOWN` stage）。

live-host 注册表：Create 成功后登记，Destroy 在 `delete` 前注销。目的使
"已销毁句柄"在 capi 层即被拒绝，不触碰已释放内存（契约"不得访问已释放内存"）。
既有导出未改行为，仅在 Create/Destroy 增加一次登记/注销。

`outBuffer` 被解释为 `EngineLogRecordV1[32]`，一次 poll 写
`recordCount * 2392` 字节，不存在部分写后伪成功。

---

## 4. EngineHost::PollEngineLogs（布局投影，行为留 B3）

`EngineHost.cpp:2937-3003`：

1. 参数/容量校验 → `RecordFailure`；
2. `m_ShutdownComplete`、`DISPOSED`、`FAULTED` → 拒绝（stage `SHUTDOWN`）；
3. `Log::GetLogRing()` 为空 → 拒绝（stage `BOOTSTRAP`）；
4. `static_assert`：`Engine::EngineLogRecord` 与 `EngineLogRecordV1` 的
   size/alignof 一致（B1 内部记录直接投影到 ABI 记录）；
5. `ring->Poll(afterSequence, records, capacity/2392, ringStats)`；
6. 回填 `stats` 全字段（`memset` 后逐字段写，`structSize = sizeof`）；
7. `SetResult(SUCCESS, BOOTSTRAP, 0, nullptr, m_State)`；返回 `count`。

B2 只证明：布局可用、导出可调用、bootstrap 日志可被取回、销毁后拒绝。
完整 poll 边界（`afterSequence` 推进、32 上限、溢出缺口、并发、生命周期矩阵）
按计划归 **B3**，本记录不宣称已验证。

---

## 5. ABI 版本与 lockstep

- `ENGINE_HOST_ABI_VERSION`：`((1 << 16) | 8)` → `((1 << 16) | 9)`；
  managed `EngineHostExpectedVersion` 同步为 9。
- 理由：新增导出 + 两个新 POD 属于 additive；若只留在 v1.8，v1.8 native 与
  v1.9 managed 混搭会在**首次 poll** 才以错误布局暴露，而不是在 `Create()` 失败。
- 旧版 pin 已更新（这是本次授权的 ABI 增量，不是放宽断言）：
  - `tests/test_bridge/test_editor_capi.cpp` `P2AbiVersionIncrementIsAdditiveOnly`；
  - `tests/test_bridge/test_pick_transport.cpp` `D3PickPacketLayoutIsStableInV16`。
  两处仍断言既有 op/status/`EV_COUNT=14`/`RuntimeResult=76B`/pick packet 32B 不变。

lockstep 证据（**生产代码**，非复制逻辑）：
`T6BLoggingAbiTests.V18MixedVersionIsRejectedByCreateLockstep` 加载
`editor_avalonia/bin/<Config>/net9.0/AvaloniaEditor.dll`，反射调用
`EditorHostService.VerifyEngineHostAbiVersion`：

- 传入 v1.9 → 通过；
- 传入 v1.8 → 抛 `HostOperationException`，消息含
  `ABI version mismatch` 与 `expected 0x00010009`。

即 v1.8/v1.9 混搭在 `Create()` 的 lockstep 处失败，而非运行期。

---

## 6. 实验对照（计划 §B2）

| # | 计划实验 | 覆盖 | 结果 |
|---|---|---|---|
| 1 | capi.h 新增两 POD 与导出 | `capi.h` §4.2–4.4；`LogPollValidationAndBootstrapSmoke` | PASS |
| 2 | static_assert size/align/offset | `capi.h:746-771`；`LogRecordLayoutIsFrozen`、`LogPollStatsLayoutIsFrozen` | PASS |
| 3 | capi/managed 同一常量与偏移 | 三层断言同一组数字 | PASS |
| 4 | Debug/Release 编译，无 ABI 混搭 | 见 §7（`0 error`，增量无新警告） | PASS |
| 5 | .NET 侧 `Marshal` 与 native 一致 | `T6BLoggingAbiTests` 2 个布局用例 | PASS |
| 6 | v1.9 + 旧 v1.8 在 Create lockstep 被拒 | `V18MixedVersionIsRejectedByCreateLockstep` | PASS |

通过条件：size/offset 两侧一致；无 pack(1) 混用；混搭在 Create 失败；
`EV_COUNT`/`RuntimeResult` 不变 —— 全部满足。

---

## 7. 测试结果（实际运行）

| 目标 | 配置 | 结果 | 日志 |
|---|---|---|---|
| `EditorBridge`（`test_bridge` 构建） | Debug | 0 error | `build/t6b_b2_build_debug.log` |
| `EditorBridge`（`test_bridge` 构建） | Release | 0 error | `build/t6b_b2_build_release.log` |
| `test_bridge` | Debug | **107/107**（B0 基线 103 + 4 新增） | `build/t6b_b2_test_bridge_debug.log` |
| `test_bridge` | Release | **107/107** | `build/t6b_b2_test_bridge_release.log` |
| `test_pick_transport` | Debug | **9/9** | `build/t6b_b2_test_pick_transport_debug.log` |
| `test_pick_transport` | Release | **9/9** | `build/t6b_b2_test_pick_transport_release.log` |
| `test_hrc`（sanity） | Debug | **15/15** | `build/t6b_b2_test_hrc_debug.log` |
| `test_hrc`（sanity） | Release | **15/15** | `build/t6b_b2_test_hrc_release.log` |
| `AvaloniaEditor` 构建 | Debug | 0 error / 6 warning（均既有） | `build/t6b_b2_managed_build_debug.log` |
| `AvaloniaEditor` 构建 | Release | 0 error | `build/t6b_b2_managed_build_release.log` |
| `T6BLoggingAbiTests` | Debug | **4/4** | `build/t6b_b2_managed_test_debug2.log` |
| `T6BLoggingAbiTests` | Release | **4/4** | `build/t6b_b2_managed_test_release2.log` |

Managed 既有警告：`NU1900`（NuGet 源不可达）、`CS0414` ×3
（`MainViewModel._viewportCamX/Y/Z`）、`CS8602`（`Phase2Gate.cs:90`）；
均为既有代码，B2 未新增警告。

`test_core` 未在 B2 重跑：B2 未改 `engine/*`；B1 的 71/71（Debug/Release）
仍然有效，全量回归按计划在 B6 执行。

首次 managed 运行（`t6b_b2_managed_test_debug.log`，3/3）只覆盖布局；
加入 lockstep 用例后的最终结果以 `debug2`/`release2`（4/4）为准。

---

## 8. 哈希（B2 结束）

| 文件 | SHA-256 | git blob |
|---|---|---|
| `bridge/include/editor_bridge/capi.h` | `8EE5CB14E3453750996215EA9DE57F6C3F7AE6D0A15456BE9B3B5E74AC047092` | `e7ed62d98e48de127fec1e3f5507b18d1cb21218` |
| `bridge/src/capi.cpp` | `166ABB83C9192C78351C52B487302B7FC4E757EF2B81BA38C6A9CB07A5994B9C` | — |
| `editor_avalonia/Native/EditorBridgeApi.cs` | `EA294CC3167CFF767F5EE5271D489975F84C1DE65406E9D0E13C579004C336BE` | — |

B0 基线 `capi.h` 为 `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886`
（git blob `630ed7bf6f79ee0c68fd13ff741cef96eb7571f3`）。B2 的变更是计划内、
经授权的 v1.9 additive 增量。

---

## 9. 纪律与边界确认

- 未使用 `pack(1)`，未与新 POD 的自然对齐混用；
- 未改 `EV_COUNT`（14）、`RuntimeResult`（76B）、既有 op/status 取值、
  pick packet（32B）——均有运行期断言；
- 未接 managed UI/Console、未改 T-6-A 语义、未新增 op；
- 未触碰 3 个脏子模块与 `crashes/`；
- 已知实现假设（契约未显式要求）：`outBuffer` 需 8 字节对齐。x64 MSVC 上
  非对齐访问本身可行，managed 侧传 CLR `byte[]`（实际对齐 ≥8）满足；如后续
  引入其他分配器需在 B3 记录中复核。
- `EngineHost::PollEngineLogs` 的完整语义（溢出缺口、并发、生命周期矩阵）
  尚未验证，B2 记录**不**声称通过。

---

## 10. B2 结论与停点

- POD 布局已在 C++/C# 两侧冻结并由三层断言固定；
- v1.9 增量满足 additive 约束，旧 pin 更新仅限版本号；
- 混搭版本在 `Create()` lockstep 处被生产代码拒绝（反射实测）；
- 导出已可调用，bootstrap 日志可被取回，销毁后拒绝访问。

**B2 状态**：`COMPLETE — ALL B2 PASS CONDITIONS MET`。
**下一停点**：等待用户明确授权 B3（EngineHost poll/bootstrap/生命周期）。
不得以 B2 通过自动进入。
