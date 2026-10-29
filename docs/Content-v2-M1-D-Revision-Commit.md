# Content v2 M1-D：单一提交点 + Revision — 交付记录

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.6 M1-D 行（K1、§3.8 四态词汇）。
**状态**：`CLOSED — SCOPE-BOUND`（2026-10-29 复审）。D-1 写锁、D-2 基线复验/不确定结果判定、Unversioned 边界均为 `PASS — SCOPE-BOUND`；M1-E 已获授权。
**未执行**：commit / push / tag / merge；未触碰既有子模块脏标记与 `crashes/`。

---

## 1. 物理布局与读取规则

```
<ProjectRoot>/.engine-content/current
<ProjectRoot>/.engine-content/revisions/<N>/manifest.json
<ProjectRoot>/.engine-content/revisions/<N>/scene.scene
```

`current`（JSON，单文件）：

```json
{ "format": 1, "revision": 1,
  "manifest": "revisions/1/manifest.json",
  "scene": "revisions/1/scene.scene",
  "manifestHash": "…", "sceneHash": "…" }
```

- **读取端只认 `current`**：不扫描 revision 目录决定当前版本；更高 revision 的孤立目录
  不参与加载、不自动晋升。
- `current` 缺失 **且没有 `versioned` 标记** → `Unversioned`：按旧格式读取（K4.4 兼容）。
  `Unversioned` 只是旧格式兼容模式，不是第五种事务状态。
- `current` 缺失 **但 `versioned` 标记存在** → **RecoveryRequired**：
  版本化工程的事务元数据丢失，不得回退到旧 Manifest/Scene 镜像。
- `current` 存在但损坏、路径不符、引用缺失或校验和不符 → **RecoveryRequired**。
- 首次发布在 `current` 之前写 `versioned` 标记；若 `current` 失败则回滚标记，
  使项目仍可作为旧格式打开；标记回滚失败 → RecoveryRequired。
- 校验和：FNV-1a 64（非加密哈希）；覆盖 manifest 与 scene 字节。

## 2. 写入与提交（K1.1–K1.7）

| 步骤 | 实现 |
|---|---|
| 候选准备 | 新 revision 目录写入 `manifest.json` + `scene.scene`（`WriteFileAtomic`：临时文件 + flush + 回读校验 + 原子替换）；旧 `current` 不变 |
| 全量校验（K1.4） | 对候选文件做严格 Manifest 加载（版本/字段/规范 GUID/重复 GUID/PathKey/路径结构）与 Scene 解析；任一失败 → `NotCommitted`，候选目录**保留在隔离区**，编号不复用 |
| 写互斥（D-1） | 每次发布持有项目级跨进程写锁 `<root>/.engine-content/write.lock`（Windows 不共享 `CreateFileW`；POSIX `flock`）；锁内执行基线复验 → 候选写/校验 → 最终复验 → current 替换 |
| 基线复验（D-2） | 打开时记录 `current` 字节哈希；发布前与指针替换前各重读一次并比对。`current` 已变化/意外出现 → `NotCommitted`（stale baseline），禁止旧状态覆盖 |
| 提交点（K1.2/K1.5） | `current` 单次原子替换（`MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)` / POSIX `rename`）；替换前不存在任何半提交 |
| 结果判定（D-2） | 替换成功后回读 current 确认指向本次 revision；替换失败时重读 current：等于新值 → committed，等于基线 → NotCommitted，无法读取/两者都不是 → RecoveryRequired |
| 提交后（K1.6/K1.7） | 只有 current 替换成功才更新内存 revision、清 Dirty、发 `EV_PROJECT_SAVED`；失败保留旧 current 与旧内存状态 |
| 提交后清理 | 旧路径文件仅作非权威镜像；镜像写失败 → `CommittedCleanupPending` 警告，不回滚已提交 revision |
| Revision 编号（K1.3） | 由 `m_NextRevision` 分配（Open 时 current+1，成功后递增）；失败候选目录保留（隔离），编号不复用；允许空洞 |

`RevisionStore`（`engine/include/Engine/Core/Content/RevisionStore.h`）是唯一实现；
SaveProject 与 Rename 都走 `RevisionStore::Publish`，**M1-C 的过渡提交 seam 已删除**。

### 2.1 SaveProject

1. 规范化注册表（K4.4）；
2. `CaptureScene` → 序列化 Scene；`SerializeManifest` → Manifest；
3. `Publish`（候选写盘 → 校验 → current）；
4. 成功后交换内存注册表、清 Dirty、事件；旧 manifest/scene 路径写入非权威镜像；
5. 镜像失败只记 `CommittedCleanupPending`。

### 2.2 Rename

1. 物理 prepare（M1-C：copy 或大小写两阶段）；
2. 读取**已提交 Scene**（有 current 读 revision 内场景；无 current 读旧场景文件），
   不把未保存的编辑吞进 rename 的 revision；
3. 候选注册表 + `SerializeManifest` → `Publish`；
4. 失败：删除新副本 / 恢复旧大小写，旧 current 与旧源不变（`NotCommitted`）；
5. 成功后旧 manifest 路径写非权威镜像；旧源清理失败 → `CommittedCleanupPending`。

### 3.1 `current` 变化/替换失败的状态判定（复审精确化）

| 回读证据 | 判定 |
|---|---|
| `current` 仍等于原提交基线 | `NotCommitted` |
| `current` 已指向另一个有效提交（证明本次基线已过期） | `NotCommitted`；不得覆盖新提交 |
| `current` 明确指向本次新提交 | `Committed` |
| `current` 无法读取、损坏，或其值既不符合原基线也不符合本次提交 | `RecoveryRequired` |

`FailedCurrentSwapKeepsOldRevisionAndSkipsNumber` 的 `RecoveryRequired` 原因是
**运行时回读被独占句柄阻塞、无法证明替换结果**；释放句柄并重启后确认 `current`
仍指向旧的完整 revision。若运行时已明确读到 `current` 等于原基线，则判定
`NotCommitted`，不会仅因替换 API 失败而保留 `RecoveryRequired`。

## 3.2 四态映射（§3.8）

| 状态 | 触发 | 行为 |
|---|---|---|
| `NotCommitted` | 候选写/校验失败、写锁被占用、stale baseline、current 替换失败且旧值可确认 | 旧 current 权威；旧源/旧注册表/旧内存状态保留 |
| `Committed` | current 替换成功 | 新 revision 权威；清 Dirty、发 Saved |
| `CommittedCleanupPending` | current 已替换，旧路径镜像或旧源清理失败 | 不回滚；警告；可重试清理 |
| `RecoveryRequired` | 启动时 current 损坏/引用缺失/校验失败；版本化工程缺 current；大小写-only 恢复失败；提交结果无法判定 | 打开失败或会话内 `Save/Play/Reload/ScriptSave/Rename` 守卫 |

## 4. 门禁

### 4.1 端到端 — `tests/test_bridge/test_revision_commit.cpp`（11 个用例）

| 规则 | 用例 |
|---|---|
| 保存发布 revision + current；旧路径被破坏后重启仍从 current 加载 | `SavePublishesCurrentAndRestartUsesRevision` |
| current 非 JSON → RecoveryRequired，不回退旧格式 | `CorruptCurrentIsRecoveryRequired` |
| revision 文件校验和不符 → RecoveryRequired | `ChecksumMismatchIsRecoveryRequired` |
| 更高 revision 孤立目录不晋升 | `OrphanHigherRevisionIsNotPromoted` |
| 无 current 时忽略 revision 目录、从旧格式打开并重新分配 | `MissingCurrentIgnoresRevisionDirsAndStartsFresh` |
| rename 的 revision 使用已提交 Scene，未保存编辑仍 dirty | `RenamePublishesCommittedSceneNotLiveEdits` |
| current 替换失败且无法判定 → RecoveryRequired；重启只看到旧完整 revision | `FailedCurrentSwapKeepsOldRevisionAndSkipsNumber`（Windows） |
| current 已提交但旧路径镜像失败 → CommittedCleanupPending | `LegacyMirrorFailureIsCommittedCleanupPending`（Windows） |
| 候选 revision 写失败 → NotCommitted，旧 current 保留 | `CandidateWriteFailureKeepsOldCurrent` |
| 两个会话基于同一旧 revision 提交 → 后提交者 stale baseline 拒绝 | `StaleSessionSaveIsRejected` |
| 版本化工程删除 current → RecoveryRequired（不回退旧镜像） | `VersionedProjectMissingCurrentIsRecoveryRequired` |
| 合法旧格式工程（无元数据）→ Unversioned 打开 | `LegacyProjectWithoutMetadataIsUnversioned` |
| 首次发布失败 → 不写 versioned，仍可旧格式打开；重试建立 current | `FirstPublishFailureKeepsLegacyCompatibility` |
| current 路径不符 revision 布局 → RecoveryRequired | `CurrentLayoutMismatchIsRecoveryRequired` |
| current 引用缺失的 revision 文件 → RecoveryRequired | `MissingRevisionFileIsRecoveryRequired` |

M1-C 的 9 个 rename/script 用例继续通过（提交语义已切换到 revision/current）。

### 4.2 RevisionStore 直测 — `tests/test_content/RevisionStoreTest.cpp`（3 个用例）

| 规则 | 用例 |
|---|---|
| Publish 生成 current，重开可读已提交 Scene | `PublishCreatesCurrentAndReadsCommittedScene` |
| 非法 Scene 候选 → 不替换 current；候选隔离；重开/重启后编号不复用 | `InvalidSceneCandidateKeepsCurrentAndSkipsNumber` |
| 非法 Manifest 候选 → current 不变 | `InvalidManifestCandidateKeepsCurrent` |
| 两个 RevisionStore 基于同一旧基线 → 后提交者 stale 拒绝 | `StaleBaselineWriterIsRejected` |
| 版本化工程缺 current → RecoveryRequired | `VersionedProjectMissingCurrentIsRecoveryRequired` |
| 无 versioned 标记的元数据目录仍按旧格式兼容 | `MetadataWithoutVersionedMarkerIsLegacyCompatible` |
| 首次发布失败不写 versioned 标记 | `FirstPublishFailureDoesNotMarkVersioned` |
| 写锁被占用 → NotCommitted（非 RecoveryRequired） | `WriteLockBusyIsNotCommitted`（Windows） |

## 5. 实测结果

| 项目 | 结果 |
|---|---|
| `RevisionCommitTest` | 15/15（Debug/Release） |
| `RevisionStoreTest` | 8/8（Debug/Release） |
| `RenameScriptTest` | 9/9（Debug/Release） |
| `test_bridge` 全量 | **77/77**（Debug/Release） |
| `test_content`（含 RevisionStoreTest） | **92 通过 / 5 既有失败 / 1 skip**（两配置） |
| `test_pick_transport` | 9/9（两配置） |
| 完整 CTest | **16/17 通过**；唯一失败 `test_content` 既有 fixture |

构建警告：见 §5.1（第一方全量，third_party 未重编）。日志：
`build/m1d_build_{debug,release}_full.log`、`build/m1d_test_{bridge,content,pick}_{debug,release}.log`、
`build/m1d_baseline_ctest_{debug,release}.log`。

### 5.1 构建警告（实测）

- Debug 第一方全量重编（D-1/D-2 修复后）：5 条（1×C4005、2×C4477、2×LNK4006，
  均为既有 EngineCore）。
- Release 第一方全量重编：5 条（同上）。
- 日志：`m1d_build_{debug,release}_full.log`。

## 5.2 复审结论（2026-10-29）

D-1、D-2、`versioned` 边界、失败候选编号不复用均按 `PASS — SCOPE-BOUND` 接受；
M1-D 关闭。以下 §6 限制在关闭后继续保留，不因阶段关闭而删除：

## 6. 明确限制 / 延后项

1. **校验和是 FNV-1a 64**，非加密哈希；用于发现截断/损坏，不提供抗篡改保证。
2. **没有自动恢复入口**：`current` 损坏时打开失败并报 `RecoveryRequired`；恢复方式是
   人工恢复 `current`/revision 文件（K1.8 的“显式恢复”）。未提供 UI/API 的恢复流程。
3. **revision 不回收**：成功提交的旧 revision 与失败候选目录都保留（隔离/供人工恢复）；
   GC/清理策略未实现。
4. **写锁为每次发布粒度**：两个会话可同时打开；后提交者会在基线复验时被拒绝。
   绕过锁协议的外部写入仍可能造成极窄竞态；锁内最终复验只能缩小窗口，不能约束不遵守
   协议的写入者。
5. **提交结果不确定 → RecoveryRequired**：替换失败后若无法读取/判定 current，
   同一会话被禁止继续保存；重新打开后以持久化 current 判定实际状态。
6. **旧路径镜像非权威**：镜像失败记 `CommittedCleanupPending`；重启只认 current。
7. **资产文件存在性/校验和不参与 revision 发布校验**：K3 允许缺失资产保留 GUID；
   K1.4 的“引用文件”按 revision 的 manifest/scene 解释。
8. **Rename 的 revision 只提交已提交 Scene**；未保存的场景编辑保持 dirty，需显式 SaveProject。
9. `.engine-content/` 已加入 `.gitignore`，避免工程元数据进入仓库。
11. **Unversioned 边界**：`versioned` 标记存在而 current 缺失 → RecoveryRequired；
    标记不存在（含首次发布失败残留的候选目录）→ 仍按合法旧格式打开。
10. **活动会话中的大小写-only 重复路径**（可经 Import 在大小写不敏感卷上产生）会让
    SaveProject 在候选严格校验阶段返回 `NotCommitted`；导入期去重/冲突处理属 M1-E。

## 7. 本次改动文件（工作树，未提交）

- `engine/include/Engine/Core/Content/RevisionStore.h`（新增）
- `engine/src/Core/Content/RevisionStore.cpp`（新增）
- `engine/include/Engine/Core/Content/ContentAsset.h`
- `engine/src/Core/Content/ContentAsset.cpp`
- `bridge/src/EditorSession.{h,cpp}`
- `tests/test_bridge/test_revision_commit.cpp`（新增）
- `tests/test_content/RevisionStoreTest.cpp`（新增）
- `tests/test_bridge/test_rename_script.cpp`
- `tests/CMakeLists.txt`
- `.gitignore`
