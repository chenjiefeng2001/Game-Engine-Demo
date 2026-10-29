# Content v2 M1-F：全链故障注入与证据封存

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.6 M1-F、§3.7 V1–V6、§3.8 四态。
**状态**：`CLOSED — SCOPE-BOUND`（2026-10-29 复审）。RF-1/RF-2/RF-3 已补证；
RF-4（托管 UI 端到端）为 `UNVERIFIED — ACCEPTED SCOPE EXCEPTION`；
T-2 已按最终裁决关闭（见 `docs/T-2-Closure-Record.md`）。
**未执行**：commit / push / tag / merge；未触碰既有子模块脏标记与 `crashes/`。

环境：Windows 10.0.29695 x64，MSVC 17.14，Debug/Release；仓库根为 CTest 工作目录。

---

## 1. 新增故障注入用例（`tests/test_bridge/test_m1f_fault_matrix.cpp`，7 个）

| ID | 注入（真实外部条件） | 提交状态 | 恢复动作 | 结果 |
|---|---|---|---|---|
| F-01 (V1) | 保存时把 `.engine-content/revisions` 置为普通文件 | `NotCommitted`：无 `current`、旧 manifest 不变 | 修复目录后重试；dirty 保持、无 `EV_PROJECT_SAVED` | PASS |
| F-02 (V1) | 导入在源拷贝完成后、候选准备期阻断 revisions 目录 | `NotCommitted`：副本删除、注册表不变、无导入事件 | 修复目录后重试成功 | PASS |
| F-03 (V2/V4) | rename 提交成功后，用不含 `FILE_SHARE_DELETE` 的句柄占用旧源 | `CommittedCleanupPending`：`current` 指向新 revision，旧源保留 | 冷启动仍加载新 revision/GUID；旧源作为可恢复副本保留 | PASS |
| F-04 (V4/K4.6) | 大小写-only rename 后冷启动 | `Committed`：`current` 指向新路径 | 冷启动路径大小写与 GUID 一致，无临时名残留 | PASS |
| F-05 (V5) | 版本化工程从外部 CWD 打开 | `Committed`（不涉及新提交） | 路径/GUID/实体不漂移，CWD 下不生成目录 | PASS |
| F-06 (V6) | 旧格式打开后首次保存 | `Committed`：建立 `current` | 旧 manifest 仍 `version=1`、GUID 不变、冷启动一致 | PASS |
| F-07 | 缺失 GUID 的 reattach 在候选期阻断 revisions 目录 | `NotCommitted`：缺失 GUID 保持、副本删除 | 修复后显式 reattach 成功；冷启动警告消失 | PASS |

## 2. 既有门禁纳入 V1–V6 证据

| V | 关键证据（现有测试） |
|---|---|
| V1 提交前失败不被看成已提交 | `V1_SavePreCommitFailure...`、`V1_ImportPreCommitFailure...`、`CommitFailureLeavesOldSourceAndNoHalfUpdate`、`CommitFailureDoesNotReportImportSuccess`、`CandidateWriteFailureKeepsOldCurrent` |
| V2 提交后失败区分清理未完成 | `V2_RenameCleanupPending...`、`LegacyMirrorFailureIsCommittedCleanupPending`、`FailedCurrentSwap...`（RecoveryRequired 分支） |
| V3 重启只加载完整已提交版本 | `CorruptCurrentIsRecoveryRequired`、`ChecksumMismatchIsRecoveryRequired`、`MissingRevisionFileIsRecoveryRequired`、`CurrentLayoutMismatchIsRecoveryRequired`、`OrphanHigherRevisionIsNotPromoted`、`FailedCurrentSwapKeepsOldRevisionAndSkipsNumber`、`VersionedProjectMissingCurrentIsRecoveryRequired` |
| V4 rename 保持 GUID | `RenameCopiesCommitsAndPreservesIdentity`、`V4_CaseOnlyRenameSurvivesColdStart`、`V2_RenameCleanupPending...`、`RenameRejectsExistingTargetWithoutOverwrite` |
| V5 打开与 CWD 无关 | `V5_VersionedOpenFromForeignCwd`、`RootedOpenIsIndependentOfProcessCwd`、`LegacyOpenCapturesCwdOnceAsExplicitRoot`、`ResolutionDoesNotDriftWhenCwdChanges` |
| V6 旧格式/路径规则兼容 | `V6_LegacyReadableAndFirstPublishSchemaStable`、`LegacyProjectWithoutMetadataIsUnversioned`、`MissingCurrentIgnoresRevisionDirsAndStartsFresh`、`FirstPublishFailureKeepsLegacyCompatibility`、`MetadataWithoutVersionedMarkerIsLegacyCompatible` |

## 3. 指定边界的覆盖

| 边界 | 证据 |
|---|---|
| Import：源拷贝后 / 候选期失败 | F-02、`CommitFailureDoesNotReportImportSuccess` |
| Import：`current` 替换前失败 | F-02、`TargetConflictIsRejectedWithoutOverwrite`、`IllegalTargetPathsAreRejected` |
| Import：`current` 替换后失败 | `CommitFailureDoesNotReportImportSuccess`（替换不可判定 → RecoveryRequired，重启旧 revision） |
| Rename：提交前失败 | `RenameRejectsExistingTargetWithoutOverwrite`、`RenameRejectsRegisteredPathConflict`、`CommitFailureLeavesOldSourceAndNoHalfUpdate` |
| Rename：提交后清理失败 / 冷启动 | F-03 |
| Rename：大小写-only / 冷启动 | F-04、`CaseOnlyRenameUpdatesCaseAndKeepsGuid` |
| RevisionStore：写锁冲突 | `WriteLockBusyIsNotCommitted` |
| RevisionStore：陈旧基线 | `StaleSessionSaveIsRejected`、`StaleBaselineWriterIsRejected` |
| RevisionStore：替换结果不确定 | `FailedCurrentSwap...`（不可读 current → RecoveryRequired；重启确认旧 revision） |
| RevisionStore：回读不匹配 | **UNVERIFIED（不可确定性注入）**；代码路径存在，未在本次运行中触发 |
| 首次版本化：标记/current/中断 | F-06、`FirstPublishFailureKeepsLegacyCompatibility`、`FirstPublishFailureDoesNotMarkVersioned`、`MetadataWithoutVersionedMarkerIsLegacyCompatible`、`VersionedProjectMissingCurrentIsRecoveryRequired` |
| ScriptSave：替换失败/成功不改 revision | `ScriptSaveFailureKeepsOriginalFile`、`ScriptSaveDoesNotPublishRevision` |
| ScriptSave：临时写/回读校验阶段失败 | **UNVERIFIED（未单独注入；与替换失败共用 `WriteFileAtomic`）** |
| Reattach：显式身份/提交/冷启动 | F-07、`ReattachRequiresExplicitCall` |

## 4. 实际执行结果（本次运行）

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `FaultMatrixTest`（含 RF-1/RF-2/RF-3） | **14/14** | 14/14 | `build/m1f_test_matrix_debug.log` |
| `test_bridge` 全量 | **100/100** | **100/100** | `build/m1f_test_bridge_{debug,release}.log` |
| `test_content` | 92 通过 / 5 既有失败 / 1 skip | 同左 | `build/m1f_test_content_{debug,release}.log` |
| `test_pick_transport` | 9/9 | 9/9 | `build/m1f_test_pick_{debug,release}.log` |
| 完整 CTest | **16/17**（唯一失败 `test_content` 既有 fixture） | 同左 | `build/m1f_baseline_ctest_{debug,release}.log` |

- 5 个既有失败：`ContentDogfood.DF03_Smoke_LoadRunTimerExpire`、
  `ContentDogfood.DF03_EntityCount_MultiScript`、`Dogfood05/06/07`（缺失 fixture，见
  `docs/Handoff-CI-Remaining-Failures.md` §2），保持原状态。
- 构建警告：RF 补证后第一方全量重编（`build/m1f_build_{debug,release}_full_final.log`）
  Debug/Release 各 5 条既有警告（1×C4005、2×C4477、2×LNK4006）；
  M1-F 新增测试 TU 定向重编无编译/链接警告。
- 命令与退出码：`FaultMatrixTest` 全量 14/14（exit 0）；`test_bridge` 100/100（exit 0）；
  `test_content` exit 1（仅 5 个既有 fixture 失败）；CTest exit 8（16/17）；
  子进程崩溃用例断言 harness 退出码 90。
- 托管：M1-F 未运行托管 UI 端到端；`dotnet build` 证据沿用 M1-E（成功，增量 4×NU1900）。

## 5. RF-1–RF-3 补证（2026-10-29）

为保证注入作用于真实的 EditorBridge.dll，新增测试专用 C 导出
`EditorSession_SetRevisionFaultHookForTesting` / `SetContentFaultHookForTesting` /
`SetAtomicFaultHookForTesting`（ABI v1.8；生产默认空，不改变正常路径）。新增用例：

| ID | 注入 | 提交状态 / 结果 | 恢复动作 | 证据 |
|---|---|---|---|---|
| RF-1 | `current` 替换成功后、回读前把 current 恢复为旧基线 | `RecoveryRequired`；Save/Rename/Import/Play/Reload/ScriptSave 全部被拒；重启只见旧 revision | 显式恢复后才可继续；重启读旧 current | `RF1_ReadbackMismatchEntersRecoveryAndBlocksWrites` |
| RF-2a | 子进程在 `BeforeCurrentReplace` 终止 | `NotCommitted`：旧 current/旧 revision 完整 | 重启加载旧（10 实体，rev1） | `RF2_CrashBeforeCurrentReplaceKeepsOldRevision` |
| RF-2b | 子进程在 `AfterCurrentReplaceBeforeReadback` 终止 | `Committed`：current 指向新 revision | 重启加载新（11 实体，rev2） | `RF2_CrashAfterCurrentReplaceSeesNewRevision` |
| RF-2c | 首次发布、`BeforeCurrentReplace` 终止（标记已写） | `RecoveryRequired`：无 current、有 versioned 标记 | 重启拒绝回退旧格式 | `RF2_CrashFirstPublishBeforeCurrentIsRecoveryRequired` |
| RF-2d | 首次发布、候选校验后终止（标记未写） | `NotCommitted`：候选不晋升，旧格式可读 | 重启按旧格式打开（10 实体） | `RF2_CrashFirstPublishCandidateDoesNotPromote` |
| RF-2e | rename 提交后、旧源清理前终止 | `CommittedCleanupPending`：新 current 可用，旧源保留 | 重启加载新路径/GUID；旧源作为恢复副本 | `RF2_CrashDuringRenameCleanupKeepsNewRevision` |
| RF-3a | ScriptSave `BeforeTempWrite` 注入失败 | `NotCommitted`（脚本写失败）；原文件/current 不变 | 无临时文件保留 | `RF3_ScriptSaveStageFailuresKeepOriginal` |
| RF-3b | `AfterTempWriteBeforeVerify` 破坏临时文件 | `NotCommitted`（Verify 失败）；原文件/current 不变 | 临时文件清理 | 同上 |
| RF-3c | `BeforeReplace` 注入失败 | `NotCommitted`（替换失败）；原文件/current 不变 | 临时文件清理 | 同上 |

RF-1 的回读不一致用例同时验证了 `RecoveryRequired` 下的操作封锁；RF-2 使用独立
`m1f_crash_harness` 子进程以 `_Exit(90)` 终止，父进程重启校验，不依赖内存返回值。

## 6. 仍未验证 / 范围例外

1. **RF-4：托管 UI 端到端导入**：`UNVERIFIED — ACCEPTED SCOPE EXCEPTION`。没有自动化
   驱动 `MainWindow.OnImportAsset` 文件选择器 + 真实窗口的测试；EngineHost op 专项
   不能替代。已按 T-2 最终裁决接受该例外，UI 端到端验证登记为后续独立任务。
2. **物理断电级持久性**：RF-2 覆盖进程终止，不模拟设备断电后的目录项/数据持久性；
   不承诺断电级保证。
3. **Reattach 托管 UI**：不存在（K3 显式 C ABI 入口）。
4. **工具/离线路径**：`ScriptSandboxApp`、`AssetBakery` 未做故障注入，不属编辑器生产路径。

## 7. 结论

V1–V6 均有实际执行证据；四态判定均来自持久化 `current`/revision 与真实文件状态，
未使用内存返回值或最高 Revision 推断。RF-1（回读不一致）、RF-2（子进程边界终止）、
RF-3（ScriptSave 三阶段）已补证；`FaultMatrixTest` 14/14、`test_bridge` 100/100。

仍为 `UNVERIFIED` 的只有 RF-4（托管 UI 端到端导入）与物理断电级持久性：
前者已按范围例外接受（不视为 PASS），后者明确不承诺。T-2 已关闭为
`CLOSED — SCOPE-BOUND`；完整关闭记录见 `docs/T-2-Closure-Record.md`。

## 8. 本次改动（工作树，未提交）

- `tests/test_bridge/test_m1f_fault_matrix.cpp`（新增）
- `tests/test_bridge/M1FCrashHarness.cpp`（新增）
- `tests/CMakeLists.txt`
- `bridge/include/editor_bridge/capi.h`、`bridge/src/capi.cpp`（测试钩子，ABI v1.8）
- `engine/include/Engine/Core/Content/RevisionStore.h`、`engine/src/Core/Content/RevisionStore.cpp`
- `engine/include/Engine/Core/Content/AtomicFile.h`、`engine/src/Core/Content/AtomicFile.cpp`
- `docs/Content-v2-M1-F-Fault-Matrix.md`（本文件）
