# Content v2 M1-C：Rename 两阶段 + ScriptSave 可靠写入 — 交付记录

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.6 M1-C 行（K2/K2.10、K3、K1 共享原语）。
**状态**：实现与专项门禁已落地；2026-10-28“启动 M1-D”指令后按 scope-bound 关闭；
M1-C 的过渡提交点已在 M1-D 被替换为 revision + `current`（见
`docs/Content-v2-M1-D-Revision-Commit.md`）。
**未执行**：commit / push / tag / merge；未触碰既有子模块脏标记与 `crashes/`。

---

## 1. 实施内容

| 位置 | 内容 |
|---|---|
| `engine/include/Engine/Core/Content/AtomicFile.h:37` | 新增 `WriteFileAtomic` / `CopyFileNoOverwrite` |
| `engine/src/Core/Content/AtomicFile.cpp` | 同目录临时文件 → 写入 → flush/fsync → 回读校验 → 原子替换（Windows `MoveFileExW` + `WRITE_THROUGH`；POSIX `rename`/`link`）；失败保留目标 |
| `engine/src/Core/Content/ContentAsset.cpp:90` | `SaveManifest` 改为原子写入（M1-C 的提交原语；M1-D 将替换为 revision/current） |
| `bridge/src/EditorSession.cpp:709` | `RenameAsset` 改为 **copy → Manifest 原子提交 → 删除旧源**；不再先物理移动唯一副本 |
| `bridge/src/EditorSession.cpp:931` | `ScriptSave` 改为同一原子替换原语；失败保留原文件 |
| `bridge/src/EditorSession.cpp:1251` | `CommitManifestCandidate`：Rename 与 SaveProject 共用的 **K1 提交 seam**（M1-C 最小实现 = Manifest 原子替换；M1-D 替换其内部为 revision/current） |
| `bridge/src/EditorSession.cpp:249` | `SaveProject` 在 RecoveryRequired 时拒绝，并重试待清理旧源 |
| `bridge/src/EditorSession.cpp:973/1079` | `Play`/`Reload` 在 RecoveryRequired 时拒绝 |
| `bridge/src/EditorSession.h` | `IsRecoveryRequired`、`m_PendingCleanup`、`m_RecoveryRequired` |

### 1.1 Rename 行为（K2.1–K2.9）

1. **前置校验**：工程已打开、manifest 路径存在、资产索引有效、裸文件名、源为非目录文件、
   目标父目录存在、目标规范路径在根内；目标与任一**其他已注册资产**在 `PathKey` 下冲突 →
   拒绝（无任何磁盘动作）。
2. **no-op**：规范路径与旧路径完全相同 → 成功返回，不写磁盘、不置 dirty、不发事件。
3. **大小写-only（K4.6）**：`PathKey` 相同但大小写不同 → 两阶段临时名
   （`old → .m1c_case_<n><ext> → new`，临时名做注册表冲突检查）；随后提交 manifest；
   GUID 不变。临时名失败 → 恢复旧名；恢复失败 → `RecoveryRequired`。
4. **普通 rename**：目标已存在 → 显式冲突，不覆盖（即使内容相同）；
   `CopyFileNoOverwrite` 把源复制到目标目录临时文件并做大小/内容校验，再以不覆盖方式发布；
   **旧源在提交前始终保留**。
5. **提交**：构造候选注册表（GUID 不变、路径替换），`SaveManifest` 原子替换 Manifest；
   失败 → 删除新副本、旧源与旧 Manifest 不变、注册表不变（`NotCommitted`）。
6. **清理**：提交成功后删除旧源；删除失败 → 旧源保留为可恢复副本，警告
   `CommittedCleanupPending`，不回滚；下一次 Rename/Save 前重试删除（幂等）。
7. **GUID/Scene**：Manifest 只替换 GUID 的路径，Scene 文件不写（GUID 引用不变）；
   测试对比 rename 前后 Scene 字节完全一致。

### 1.2 ScriptSave（K2.10）

同一 `WriteFileAtomic`：临时文件 → flush → 回读校验 → 原子替换；失败保留原文件。
Windows 下用只读属性注入替换失败，验证原文未变、无临时文件残留。

### 1.3 RecoveryRequired

`m_RecoveryRequired` 在“大小写-only 准备/提交失败且旧名无法恢复”时置位；
`SaveProject`、`Play`、`Reload`、`ScriptSave`、`RenameAsset` 均拒绝并给出结构化错误。
M1-C 未注入该路径（崩溃/替换结果不可判定属 M1-D 的 `current` 状态机），只实现守卫。

---

## 2. 专项门禁 — `tests/test_bridge/test_rename_script.cpp`（9 个用例）

| 规则 | 用例 |
|---|---|
| copy → 提交 → 删除旧源；GUID/Scene 不变；重开仍同 GUID | `RenameCopiesCommitsAndPreservesIdentity` |
| 目标文件已存在（未注册）→ 拒绝、内容不被覆盖、Manifest 不变 | `RenameRejectsExistingTargetWithoutOverwrite` |
| 目标与其他注册资产 PathKey 冲突 → 拒绝、双文件完整 | `RenameRejectsRegisteredPathConflict` |
| 同路径 → no-op，不提交、不 dirty | `RenameToSamePathIsNoOpWithoutCommit` |
| 大小写-only（Windows 实际走两阶段）| `CaseOnlyRenameUpdatesCaseAndKeepsGuid` |
| 清理失败 → CommittedCleanupPending，旧源保留；释放占用后重试成功 | `CleanupFailureIsCommittedCleanupPending`（Windows） |
| Manifest 替换失败 → 旧源保留、新副本删除、注册表/Manifest/临时文件零半更新 | `CommitFailureLeavesOldSourceAndNoHalfUpdate`（Windows） |
| ScriptSave 失败保留原文件、无临时文件 | `ScriptSaveFailureKeepsOriginalFile`（Windows） |
| ScriptSave 原子替换成功、GUID 不变 | `ScriptSaveReplacesAtomically` |

---

## 3. 实测结果

构建（第一方全量，third_party 未重编）：

| 配置 | 退出码 | 警告 | 日志 |
|---|---|---|---|
| Debug | 0 | 5 条（1×C4005、2×C4477、2×LNK4006，均为既有 EngineCore） | `build/m1c_build_debug_full.log` |
| Release | 0 | 5 条（同上） | `build/m1c_build_release_full.log` |

测试（Debug / Release 均一致）：

| 套件 | 结果 | 日志 |
|---|---|---|
| `RenameScriptTest` | 9/9（Debug/Release） | `build/m1c_test_rename_{debug,release}.log` |
| `test_bridge`（全量） | **62/62** | `build/m1c_test_bridge_{debug,release}.log` |
| `test_content` | 84 通过 / 5 既有失败 / 1 skip | `build/m1c_test_content_{debug,release}.log` |
| `test_pick_transport` | 9/9 | `build/m1c_test_pick_{debug,release}.log` |
| 完整 CTest | **16/17 通过**；唯一失败 `test_content`（既有 fixture） | `build/m1c_baseline_ctest_{debug,release}.log` |

`test_content` 的 5 个失败仍为 `assets/scenes/dogfood03/05/06/07` 缺失，与 M1-C 无关；
未改用例、未降级。

---

## 4. 退出门禁自评（M1-C 行）

| 门禁 | 结论 | 证据 |
|---|---|---|
| 目标被占用 | 是：未注册文件 / 已注册路径均显式拒绝，不覆盖 | 两个冲突用例 |
| 提交前失败 | 是：旧源与旧 Manifest 不变，新副本删除，注册表不变 | `CommitFailure...`、冲突用例 |
| 指针交换失败 | 以本轮“Manifest 原子替换”为过渡提交点验证失败路径；`NotCommitted` 保持旧状态 | `CommitFailure...` |
| 清理失败 | 是：`CommittedCleanupPending`、旧源保留、可重试、不回滚 | `CleanupFailure...` |
| GUID 保持 | 是：rename 前后与重开工程后 GUID 不变；Scene 字节不变 | `RenameCopies...`、`CaseOnly...` |
| 无半更新 | 是：Manifest/注册表/文件/临时产物逐项断言 | 全部失败用例 |
| ScriptSave 可靠写入 | 是：原子替换；失败保留原文件、无临时残留 | 两个 ScriptSave 用例 |

---

## 5. 明确延后 / 限制（不得据此声称通过）

1. **K1 `current` 指针与 Revision 尚未实现**（按 2026-10-28 裁决归 M1-D）。
   M1-C 的 rename 提交点是 **Manifest 文件原子替换**，不是 `current` 交换；
   因此 **K1.6「以 current 判定提交」在 M1-C 不成立**。M1-D 必须把该原语收口为
   唯一生产提交路径（revision + 双文件 + current）。
   **（M1-D 已实现：`CommitManifestCandidate` 已删除，SaveProject/Rename 均走
   `RevisionStore::Publish`；见 `docs/Content-v2-M1-D-Revision-Commit.md`。）**
2. `RecoveryRequired` 的触发路径（提交状态不可判定）未做故障注入；当前只在
   大小写-only 恢复失败时置位。M1-D 的 `current` 状态机负责完整判定。
3. `m_PendingCleanup` 仅会话内保存；进程重启后的清理重试/事务记录属 M1-D。
4. `SaveProject` 仍是“先场景、后 Manifest”的两文件写；M1-D 的单一提交点负责整体原子性。
5. 大小写-only 两阶段只在大小写不敏感卷上实际生效；大小写敏感卷上同名不同大小写
   是普通 rename（K4.6 的例外在此无意义）。
6. K2.9 路径消费者：仓库内 Scene 以 GUID 引用（已用字节对比证明不变）；
   其他外部消费者不存在，未做额外迁移。

---

## 6. 本次改动文件（工作树，未提交）

- `engine/include/Engine/Core/Content/AtomicFile.h`（新增）
- `engine/src/Core/Content/AtomicFile.cpp`（新增）
- `engine/src/Core/Content/ContentAsset.cpp`
- `bridge/src/EditorSession.{h,cpp}`
- `tests/test_bridge/test_rename_script.cpp`（新增）
- `tests/CMakeLists.txt`

M1-B 关闭与基线回归记录见 `docs/Content-v2-M1-B-Strict-Load.md` §4.1。
