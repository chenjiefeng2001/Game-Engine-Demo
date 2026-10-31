# T-6-B — Gate B1 Record · Native LogRing 原型

**状态**：`T-6-B B1 — COMPLETE — NATIVE LOGRING + SINK INSTALL — TEST_CORE 71/71 (DEBUG/RELEASE)`
**日期**：2026-11-01
**基线**：HEAD `ef220bf`（B0 基线记录提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B1
**Gate B 授权**：用户已授权 B1 源码实施；B2 及以后仍未授权。

---

## 1. 变更范围（仅 B1 白名单）

新增：

- `engine/include/Engine/Core/LogRing.h` — 记录布局、常量、`LogRing`；
- `engine/src/Core/LogRing.cpp` — ring、sequence、UTF-8 截断、poll；
- `tests/test_core/log/LogRingTest.cpp` — 28 个新用例。

修改：

- `engine/include/Engine/Core/Log.h` — `GetLogRing()`、ring/sink 静态成员前向声明；
- `engine/src/Core/Log.cpp` — `LogRingSink`、Init 安装/apply_all 补齐、Shutdown 释放；
- `tests/CMakeLists.txt` — test_core 增加 `test_core/log/LogRingTest.cpp`。

未修改：`capi.h`、`capi.cpp`、`EngineHost`、managed、`EditorSession`、ConsolePanel、
任何 T-6-A 语义。ABI 仍为 v1.8；`EV_COUNT=14`；`RuntimeResult=76B`。

---

## 2. 实现要点

| 项 | 实现 |
|---|---|
| 进程级 ring | `Log::Init` 在第一条日志前创建 `LogRing`（4096）并安装 `LogRingSink` |
| Sink 覆盖 | 默认 logger 构造即含 sink；`spdlog::apply_all` 补齐安装前已有 logger；`Log::Get` 复制默认 sinks 覆盖后续 category |
| 重复防护 | apply_all 按 sink 指针去重；category logger 继承同一 sink 实例 |
| sequence | 从 1 起单调递增；0 仅作 `afterSequence` sentinel |
| 溢出 | 保留最新、淘汰最旧；`droppedTotal` 进程级累计，poll 不清零 |
| 截断 | UTF-8 codepoint 边界；flags `0x01` category / `0x02` source / `0x04` message |
| 零填充 | 字段未使用空间每写入都填零（含槽位复用） |
| CR/LF | 尾部 CR/LF 去除，内部 CR/LF 转空格 |
| poll | `sequence > afterSequence`；`afterSequence==0` 从最旧；上限 32；空队列成功 0 |
| 布局 | `EngineLogRecord` static_assert：2392B / alignof 8 / 冻结偏移，与 A-10 一致 |
| Shutdown | 释放 sink 与 ring（Application/EngineHost 当前不调用） |

B1 内部命名 `Engine::EngineLogRecord`；B2 将在 `capi.h` 冻结 `EngineLogRecordV1`
并加同样的 static_assert，不改本布局。

---

## 3. 实验对照（计划 §B1）

| # | 计划实验 | 覆盖用例 | 结果 |
|---|---|---|---|
| 1 | Init 创建 ring 并在第一条日志前安装 | `BootstrapLogIsCapturedFromInit` | PASS |
| 2 | default / 已注册 / 后续 logger 全覆盖且不重复 | `PreExistingLoggerIsPatchedByApplyAll`、`DefaultLoggerLogCapturedExactlyOnce`、`CategoryLoggerInheritsRingSinkExactlyOnce`、`MacroLogCapturesSourceLocation` | PASS |
| 3 | 第一条日志 sequence=1 | `FirstPushGetsSequenceOneAndFrozenFields`、`BootstrapLogIsCapturedFromInit`（front==1） | PASS |
| 4 | 多线程/多 logger：sequence 单调、无重复、不越界 | `ConcurrentWritersKeepSequenceUniqueAndContiguous`（4×250） | PASS |
| 5 | 写满 4096 后溢出：保留最新、淘汰最旧、droppedTotal 增加 | `DefaultCapacityOverflowKeepsNewest4096`（4100 条，drop=4）、`OverflowRetainsNewestAndCountsDropped`、`PollAfterEvictedSequenceStartsAtOldestAndExposesGap` | PASS |
| 6 | 超长字段 UTF-8 边界截断 / flags / 未使用空间为零 | `MessageTruncationStopsAtUtf8Boundary`、`CategoryTruncationStopsAtUtf8Boundary`、`CategoryExactCapacityIsNotTruncated`、`SourceTruncationSetsFlag`、`CombinedTruncationFlagsAreAllSet`、`ReusedSlotUnusedSpaceIsZeroed` | PASS |
| 7 | 空消息 / 空 category/source 不崩溃 | `EmptyMessageAndFieldsAreAllowed`、`NewlinesAreNormalized`（全 CR/LF → 空消息） | PASS |
| 8 | 按 `afterSequence` 读取；空 ring 返回 0 | `PollAfterSequenceReturnsOnlyNewerRecords`、`PollEmptyRingReturnsSuccessZero`、`PollAtOrPastNewestEchoesCursor`、`PollBatchIsCappedAt32`、`PollHonorsMaxRecordsSmallerThanBatch`、`PollProbeWithoutBufferReportsRangeOnly`、`PollCursorCoversEverySequenceExactlyOnce` | PASS |

附加用例：`LevelIsClampedToCritical`（>5 钳到 5）、
`DuplicateMessagesAreNotDeduplicated`（重复日志按契约保留）、
`GetLogRingIsStableAcrossCalls`。

---

## 4. 测试结果（实际运行）

| 目标 | 配置 | 结果 | 日志 |
|---|---|---|---|
| `test_core` | Debug | **71/71**（Vector3 9 / Matrix4 3 / StackAllocator 8 / Time 23 / LogRing 20 / LogRingIntegration 8） | `build/t6b_b1_test_core_debug2.log` |
| `test_core` | Release | **71/71** | `build/t6b_b1_test_core_release2.log` |
| CTest `test_core` | Debug | **1/1 Passed** | `build/t6b_b1_ctest_debug.log` |
| CTest `test_core` | Release | **1/1 Passed** | `build/t6b_b1_ctest_release.log` |

构建：`cmake --build build --config Debug/Release --target test_core` 成功
（`build/t6b_b1_build_debug3.log`、`build/t6b_b1_build_release2.log`）。
本轮为增量构建，未观察到 warning；**不宣称**全量零警告。

旧用例 43/43 全部保持通过，未改写任何既有断言。

---

## 5. 纪律与边界确认

- 测试 scratch 使用系统临时目录并在 `TearDownTestSuite` 删除；未写入 repo fixture；
- 未触碰 3 个脏子模块与 `crashes/`；
- 未修改 T-6-A EV/generation/边界行/过滤/清空语义；
- 未接 Bridge/managed；未升 ABI；未新增导出；
- 失败保留首次记录：本轮无失败需要保留（B0 的 test_bridge scratch flake 属既有基线，未复现于 B1 范围）。

---

## 6. B1 结论与停点

- native LogRing 与 sink 安装满足契约 §3 与计划 §B1 通过条件；
- sink 写入只做短锁字段复制，不调用 managed/UI、不阻塞；
- `Log::GetLogRing()` 已为 B3 EngineHost poll 提供入口；
- 未开始 B2。

**B1 状态**：`COMPLETE — ALL B1 PASS CONDITIONS MET`。
**下一停点**：等待用户明确授权 B2（`capi.h` POD/ABI v1.9 与静态布局），
不得以 B1 通过自动进入。
