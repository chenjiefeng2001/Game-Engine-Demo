# Content v2 M1-E：导入服务汇点与 GUID 身份 — 交付记录

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.6 M1-E 行（K2/K3/K4/RevisionStore）。
**状态**：`CLOSED — SCOPE-BOUND`（2026-10-29 复审）。M1-F 已获授权并关闭；T-2 已按
最终裁决关闭（`docs/T-2-Closure-Record.md`）。托管 UI 端到端导入为
`UNVERIFIED — ACCEPTED SCOPE EXCEPTION`；Reattach UI/可配置目录/内容去重保留为限制。
**未执行**：commit / push / tag / merge；未触碰既有子模块脏标记与 `crashes/`。

---

## 1. E-1 导入入口盘点与收敛

| 调用方 | 入口 | 收敛结果 |
|---|---|---|
| 托管 UI `MainWindow.OnImportAsset` | 文件选择器 → `Vm.ImportAssetFromPath(src)` → EngineHost op 11 | UI **不再自行拷贝**；只把源绝对路径交给 native 统一服务 |
| 托管门禁 `Phase3Gate` | `Vm.ImportAssetFromPath(工程相对路径)` → 同一 op | 走同一服务的“工程内登记”模式 |
| C ABI | `EditorSession_ImportAsset(path, type)` | 统一服务入口（工程相对登记 / 外部源拷贝两种模式） |
| EngineHost | op 11 `ENGINE_HOST_OP_IMPORT_ASSET` | 委托 `EditorSession::ImportAsset`，无独立实现 |
| 显式 reattach | `EditorSession_ReattachAsset(missingGuid, sourcePath, type)`（ABI v1.7 新增） | 指定缺失 GUID 的显式导入；无内容匹配自动挂接 |
| `ScriptSandboxApp` | 直接 `ContentRegistry::Import` + `SaveManifest` | **开发工具/离线路径**，不属编辑器导入服务；保持原样并记录 |
| `AssetBakery` | 直接写 fixture manifest | **离线 fixture 生成器**，不属编辑器导入服务；保持原样 |

编辑器生产路径（UI/门禁/C ABI/EngineHost）全部进入 `EditorSession::ImportAssetInternal`；
没有任何旁路注册或旁路 Manifest 写入。

## 2. E-2 路径与身份规则（复用 K4/K3）

`ImportAssetInternal` 的两种模式：

1. **工程内登记**：输入为工程相对路径（或根内绝对路径）且文件已存在 → 校验/规范化后登记。
2. **外部源拷贝**：输入为根外绝对路径 → 读取源文件，拷贝到默认导入目录
   `assets/imported/tex/<filename>` 或 `assets/imported/scripts/<filename>`。

共同规则：

- 目标路径经 `ProjectRoot::ToProjectRelative` / `NormalizeProjectRelative` 校验；
  目标越界、`.`/`..`、UNC、`\\?\`/`\\.\`、盘符相对 → 结构化拒绝（K4）。
- 目标与任一既有资产在 `PathKey` 下冲突 → 拒绝，不覆盖；目标物理文件已存在 → 拒绝。
- 新导入（新路径）**总是分配新 GUID**；内容相同不合并（K3.2）。
- 同一已注册路径再次登记 → 幂等返回既有 GUID/索引（K3.6），不产生新 revision。
- Rename 保持 GUID（M1-C/M1-D 已实现）。
- 缺失资产保留原 GUID；`ReattachAsset` 是唯一恢复入口，必须显式给出
  缺失 GUID 与源文件；普通导入同内容文件不会自动挂接（K3.5）。

## 3. E-3 统一提交

- 导入成功后，候选注册表 + `SerializeManifest` + **已提交 Scene**
  （`ReadCommittedScene`）交给 `RevisionStore::Publish`；与 SaveProject/Rename 同一提交路径。
- 不存在“扫描最大 revision”推断；不存在绕过 RevisionStore 写权威 Manifest。
- 失败：
  - `NotCommitted`：复制的新文件删除（尽力）、注册表/内存/旧 current 不变；
  - `RecoveryRequired`：置位会话守卫并停止后续保存/Play/Rename/Import；
  - 成功后旧路径 manifest 仅作非权威镜像；镜像失败告警 `CommittedCleanupPending`，
    不回滚已提交 revision。
- 导入使用已提交 Scene：未保存的场景编辑不会被导入顺手提交，仍保持 dirty。

## 4. E-4 ScriptSave

- `ScriptSave` 继续使用 `WriteFileAtomic`（temp write → verify → replace）；
- 不写 Manifest、不发布 revision、不发 Saved；成功仅 MarkDirty；
- 失败保留原文件（M1-C 用例 + 本阶段 `ScriptSaveFailureKeepsOriginalFile`）；
- 本阶段新增：`ScriptSaveDoesNotPublishRevision` 断言 `current` 字节不变。

## 5. E-5 自动化门禁 — `tests/test_bridge/test_import_service.cpp`（8 个用例）

| 验证项 | 用例 |
|---|---|
| 源拷贝 + revision 提交 + 重启一致 | `SourceImportCopiesCommitsAndSurvivesRestart` |
| 同内容多次新导入 → 不同 GUID | `SameContentDifferentFilesGetDistinctGuids` |
| 目标 PathKey 冲突 → 拒绝且不覆盖 | `TargetConflictIsRejectedWithoutOverwrite` |
| 根外/非法目标路径 → 拒绝 | `IllegalTargetPathsAreRejected` |
| 源缺失 → 无假提交 | `MissingSourceFailsWithoutCommit` |
| 缺失资产只在显式 reattach 后恢复；内容匹配不自动挂接 | `ReattachRequiresExplicitCall` |
| ScriptSave 不发布 revision | `ScriptSaveDoesNotPublishRevision` |
| 导入提交失败 → 不报告成功、旧 current 保留 | `CommitFailureDoesNotReportImportSuccess`（Windows） |
| EngineHost op 11 与无头导入共用统一服务 | `ProjectRootEngineHostTest.SourceImportViaHostOpUsesUnifiedService` |

已有 `RenameScriptTest`、`RevisionCommitTest`、`ProjectPathTest` 等继续保留。

## 6. 实测结果

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `ImportServiceTest` | 8/8 | 8/8 | `build/m1e_test_import_debug.log` |
| EngineHost 导入专项 | 1/1 | 1/1 | `build/m1e_test_host_import_debug.log` |
| `test_bridge`（全量） | **86/86** | **86/86** | `build/m1e_test_bridge_{debug,release}.log` |
| `test_content` | 92 通过 / 5 既有失败 / 1 skip | 同左 | `build/m1e_test_content_{debug,release}.log` |
| `test_pick_transport` | 9/9 | 9/9 | `build/m1e_test_pick_{debug,release}.log` |
| 完整 CTest | **16/17**（唯一失败为既有 fixture） | 同左 | `build/m1e_baseline_ctest_{debug,release}.log` |

托管：`dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug` 成功；本次增量构建记录 4×NU1900（既有 NuGet 源告警），无编译错误。UI 导入未做自动化端到端运行（仅编译与 native 服务门禁）。
ABI：`ENGINE_HOST_ABI_VERSION = v1.7`（只追加 `EditorSession_ReattachAsset`），
托管 `EngineHostExpectedVersion` 同步 v1.7；`test_editor_capi`/`test_pick_transport` 版本钉已更新。

构建警告：Debug/Release 第一方全量重编各 5 条（1×C4005、2×C4477、2×LNK4006，均为既有 EngineCore）；日志 `build/m1e_build_{debug,release}_full.log`。

## 7. 明确限制 / 延后项

1. **默认导入目录固定**为 `assets/imported/tex|scripts`；UI 不选择目标目录。
   目标目录可配置化留待后续。
2. **绝对源路径**（根外）作为拷贝源被接受；UNC/设备前缀/盘符相对源被拒绝；
   目标路径始终必须在 `ProjectRoot` 内。
3. **同路径重复导入**幂等返回既有 GUID，不做内容更新/覆盖；覆盖确认与更新语义留待后续。
4. **Reattach 只有 C ABI 显式入口**，M1-E 未提供托管 UI 按钮；这是显式恢复操作，
   不允许自动内容匹配。
5. `ScriptSandboxApp`/`AssetBakery` 仍直接使用 `ContentRegistry` 写 manifest：它们是
   工具/离线路径，不属于编辑器导入服务；不得被当作生产导入证明。
6. 导入使用已提交 Scene；未保存场景编辑保持 dirty。
7. 资产文件存在性不参与 revision 发布校验（K3 允许缺失资产保留 GUID）；
   Manifest 结构与 GUID 身份完整校验。
8. 无内容哈希去重：同内容不同路径各自新 GUID（K3.2）。

## 8. M1-E 退出门禁自评

| 门禁 | 结论 | 证据 |
|---|---|---|
| 生产导入入口收敛、无旁路写入 | 是（编辑器路径） | §1；UI 不再拷贝；EngineHost/CAPI 同一服务 |
| K4 路径规则 + K3 GUID 契约在真实调用链成立 | 是 | §2、8 个专项用例 |
| 所有 Manifest 修改经过 RevisionStore | 是（编辑器路径） | §3；bridge 无直接 `SaveManifest` |
| ScriptSave 临时写/校验/替换有测试 | 是 | §4；M1-C + 本阶段用例 |
| 构建/测试/警告/失败按实记录 | 待 census 填入 | §6 |
| 事务失败提供提交状态/恢复动作/证据 | 是 | NotCommitted/RecoveryRequired 用例；限制 §7 |

## 9. 本次改动文件（工作树，未提交）

- `bridge/src/EditorSession.{h,cpp}`（统一导入服务、Reattach）
- `bridge/src/capi.cpp`、`bridge/include/editor_bridge/capi.h`（ABI v1.7 追加导出）
- `editor_avalonia/MainWindow.axaml.cs`（UI 改为源路径导入）
- `editor_avalonia/ViewModels/MainViewModel.cs`（注释/入口语义）
- `editor_avalonia/Native/EditorBridgeApi.cs`（v1.7 锁步）
- `tests/test_bridge/test_import_service.cpp`（新增）
- `tests/test_bridge/test_project_root.cpp`（绝对源语义更新）
- `tests/test_bridge/test_editor_capi.cpp`、`test_pick_transport.cpp`（版本钉）
- `tests/CMakeLists.txt`
