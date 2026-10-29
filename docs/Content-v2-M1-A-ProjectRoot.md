# Content v2 M1-A：ProjectRoot 与路径规范化 — 交付记录

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.5（K4）与 §3.6 M1-A 行。
**执行日期**：2026-10-28。**状态**：实现与门禁已落地，等待裁决以关闭 M1-A；未开始 M1-B。
**未执行**：commit / push / tag / merge；未触碰 `third_party/d3d12ma`、`third_party/openal-soft`、
`third_party/vma` 的既有脏标记，未触碰未跟踪的 `crashes/`。

运行环境：Windows 10.0.29695 x64，MSVC 17.14（CMake VS 多配置生成器，`build/`），
NTFS 大小写不敏感卷，系统活动代码页 ACP=936（**不是** UTF-8）。

---

## 1. 实现清单（统一解析与统一 PathKey）

### 1.1 唯一路径解析入口

| 文件 | 内容 |
|---|---|
| `engine/include/Engine/Core/Content/ProjectPaths.h` | `PathError`、`PathResolution`、`CaseSensitivity`、`ProjectRootOptions`、`ProjectRoot` 公共契约 |
| `engine/src/Core/Content/ProjectPaths.cpp` | 规范化、根包含、链接检查、卷能力探测、`PathKey` 折叠的唯一实现 |
| `engine/include/Engine/Core/Content/Utf8Path.h` | **新增**：UTF-8 ↔ OS 原生路径转换（K4.2 的 IO 边界） |
| `engine/src/Core/Content/Utf8Path.cpp` | **新增**：`PathFromUtf8` / `PathToUtf8`（Windows 走 UTF-16 转换，POSIX 按字节） |

`ProjectRoot` 提供：`Create` / `Resolve` / `ResolveAny` / `ToProjectRelative` /
`ToCanonicalProjectPath` / `ComparisonKey` / `KeysEqual`。工程内所有路径判定都经由此处；
没有任何函数读取进程 CWD（`Create` 只接受显式绝对根）。

### 1.2 OS 边界与消费者

| 位置 | 变更 |
|---|---|
| `engine/src/Core/Content/ContentAsset.cpp:100,107` | 清单读写改用 `PathFromUtf8(path)`，不再让 MSVC 按 ACP 解释 UTF-8 |
| `engine/src/Core/Content/SceneSerializerV1.cpp:236,244` | 场景读写同上 |
| `bridge/src/EditorSession.cpp`（打开/保存/导入/改名/脚本读写） | 全部走 `m_Root`；文件打开与改名改用 `PathFromUtf8`/`PathToUtf8`；`RenameAsset` 的扩展名与目标路径不再经 ACP |
| `bridge/src/EditorSession.cpp:73-98` | `CanonicalizeRegistry`：可规范化 → 规范相对路径；不可规范化 → 保留作者形式 + 警告 |
| `bridge/src/EditorSession.cpp:268-298` | `SaveProject` 先对全部条目做规范形式校验，任一失败即**整体失败且不写任何文件** |
| `bridge/include/editor_bridge/capi.h:100-110,663-675` | ABI v1.6 追加 `EditorSession_/EngineHost_OpenProjectRooted`（additive，未改既有 op/结构） |
| `editor_avalonia/Native/EditorBridgeApi.cs`、`Services/EditorHostService.cs:377-379` | 托管侧 rooted 导出与 `OpenProjectRooted` |
| `editor_avalonia/MainWindow.axaml.cs:325-341` | 生产打开入口改为 `OpenProjectRooted(root, 相对清单, 相对场景)` |

---

## 2. 规范化与 PathKey 规则（已实现，逐条有门禁）

### 2.1 工程相对路径（K4.2）

- 接受：非空、`/` 或 `\` 分隔、重复分隔符折叠、尾分隔去除；**保留作者大小写**。
- 结构化拒绝：空路径；含 NUL（`InvalidCharacter`）；盘符（`AbsolutePath`，含 `C:foo`
  这种 drive-relative）；前导 `/`（`AbsolutePath`）；`//`/`\\`（`UncPath`）；
  `\\?\`、`\\.\`（`DevicePath`）；`.`（`DotSegment`）；`..`（`DotDotSegment`）。
- 规范化结果与输入分隔符变体无关（同一路径只有一种规范形式）。

### 2.2 显式 ProjectRoot（K4.1/K4.8）

- 必须非空、绝对、已存在、是目录；`fs::canonical` 后固定，会话内无 setter。
- `\\?\`/`\\.\` 根一律 `DevicePath`（不截断前缀“尽量打开”）。
- UNC 只允许作为显式 ProjectRoot；**内容路径中的 UNC 一律 `UncPath`**，
  不会退化成根包含检查（避免把形式非法误报成 `EscapesRoot`）。
- 相对根、drive-relative 根 → `RootNotAbsolute`；不存在 → `RootMissing`。

### 2.3 解析与包含（K4.3）

- `Resolve`：规范相对路径 + 根 → 绝对路径；不要求尾部已存在；越界 → `EscapesRoot`。
- `ResolveAny`：相对走 `Resolve`；绝对必须已在根内；UNC/设备前缀先按形式拒绝。
- `ToProjectRelative`：根内绝对 → 规范相对；根外 → `EscapesRoot`；相对输入 → `NotAbsolute`。
- 相似前缀（`<root>2/...`）在包含检查中判为根外（按路径分量而非字符串前缀比较）。

### 2.4 PathKey（K4.5）

- 分隔符先归一（保留 `//` 与 `/` 的命名空间区别，见下），再按**卷能力**折叠大小写。
- 卷能力由 `CreateFileW` + `GetFileInformationByHandleEx(..., FileCaseSensitiveInfo=23)`
  显式探测；查询不受支持/句柄不可得 → 按 Windows 语义判为不敏感并写诊断，**不按 OS 猜测**；
  POSIX 为大小写敏感。
- 不敏感卷的折叠 = Windows invariant locale 的 UTF-16 简单大写映射（1:1 映射才采用）；
  只折 ASCII 会让 `Ä/ä`、`Тест/тест` 逃过重复检测。非法 UTF-8 输入退回确定性的
  按字节 ASCII 折叠（字节数不变）。
- `//server/share/...` 与 `/server/share/...` 是两个命名空间，比较键不相等。

### 2.5 链接与长路径（K4.7/K4.8）

- 默认拒绝任何经过链接/junction 的路径（`LinkNotAllowed`），含尾部尚未创建的路径。
- `allowLinks=true` 时对**每个链接分量**解析 real path 并重新做包含检查：
  越界 → `LinkEscape`（诊断携带 real path）；环、悬空、不可读 reparse →
  `LinkCycle`（诊断携带组件）。
- Windows long path（`\\?\`）在 M1-A **明确拒绝**（`DevicePath`），不静默截断。

### 2.6 UTF-8 路径 IO（K4.2 的实现前提）

清单与场景文件原先用窄 `std::ifstream(std::string)` / `fs::path(std::string)` 打开；
在 ACP=936 的机器上，UTF-8 字节会被按 GBK 解释，非 ASCII 路径直接打不开。本次把所有
OS 边界改为 `PathFromUtf8`/`PathToUtf8`（`generic` 形式，保持 `/` 不变量）。
非法 UTF-8 输入产生空路径，调用方的 open/canonical 结构化失败——不做替换字符或 ACP 猜测。

---

## 3. 旧路径入口清单与迁移结果

| 入口 | 位置 | 现状 |
|---|---|---|
| `ProjectRoot` 全部方法 | `engine/src/Core/Content/ProjectPaths.cpp` | 唯一解析点；不读 CWD |
| `EditorSession_OpenProjectRooted` | `bridge/src/capi.cpp:231` | 生产入口：显式根 + 工程相对清单/场景 |
| `EditorSession_OpenProject`（legacy） | `bridge/src/EditorSession.cpp:163` | 兼容入口：在边界**一次性**捕获 CWD 为 legacy root（K4.4），之后不再读 CWD |
| `EngineHost_OpenProjectRooted` / `EngineHost_OpenProject` | `bridge/src/capi.cpp:858/842`，`EngineHost.cpp:934-973` | 分别委托 rooted / legacy 语义 |
| `SaveProject` / `ImportAsset` / `RenameAsset` / `ScriptRead` / `ScriptSave` / `Play` | `bridge/src/EditorSession.cpp` | 全部以 `m_Root` 解析；写路径锚定 root（测试断言 repo 源文件不被写） |
| 托管 `EditorHostService.OpenProjectRooted` | `editor_avalonia/Services/EditorHostService.cs:377` | 生产 UI 已迁移；`OpenProject`（legacy）保留给诊断门禁 |
| 托管 `MainWindow.OnOpenProject` | `editor_avalonia/MainWindow.axaml.cs:325` | 传显式 root；`SetCurrentDirectory` 仅服务尚未迁移的导入路径 |
| 托管 `MainWindow.OnImportAsset` | `MainWindow.axaml.cs:444-484` | **遗留**：目标目录仍由 CWD 拼接 → 归入 M1-E（代码内已标注） |
| 托管门禁 `BootstrapTriggerGate` / `D3ManagedPickGate` | `Services/*Gate.cs` | 使用 legacy `host.OpenProject`（CWD=仓库根）；属诊断门禁，不改变生产打开语义 |
| `tools/nhi1_native_teardown_probe` | `nhi1_native_probe.cpp:140` | 使用 legacy `EngineHost_OpenProject`；诊断工具，未迁移 |
| `FileSystem::s_BasePath` | `engine/src/Core/FileSystem.cpp:52,174,210` | 引擎级全局路径基底，仍以 CWD 兜底；**不在 K4 解析链上**（编辑器纹理加载使用解析后的绝对路径）；列为后续统一项 |

ABI/托管锁步：`ENGINE_HOST_ABI_VERSION = v1.6`（`(1<<16)|6`），托管
`EngineHostExpectedVersion` 同为 v1.6；`tests/test_bridge/test_editor_capi.cpp` 与
`test_pick_transport.cpp` 的版本钉已更新为 v1.6（仅追加导出，未改 op/结构）。

---

## 4. K4.9 自动化门禁

### 4.1 单元矩阵 — `tests/test_content/ProjectPathTest.cpp`（17 个用例）

| 契约 | 用例 |
|---|---|
| K4.2 规范形式唯一 | `LegalRelativePathsHaveUniqueCanonicalForm` |
| K4.2 分隔符归一 | `SeparatorVariantsNormalizeToForwardSlash` |
| K4.2 `.`/`..` 拒绝 | `DotSegmentsAreRejectedNotSilentlyDropped` |
| K4.8 盘符/UNC 作为内容路径拒绝 | `DriveAndUncFormsAreRejected` |
| K4.8 `\\?\`/`\\.\` 拒绝 | `DeviceAndLongPathPrefixesAreRejected` |
| K4.1 根创建规则 | `RootCreationRequiresExplicitAbsoluteDirectory` |
| K4.3 解析不要求尾部存在 | `ResolveAnchorsToRootWithoutRequiringExistingTail` |
| K4.3 绝对输入只在根内 | `ResolveAnyAcceptsOnlyAbsolutePathsInsideRoot` |
| K4.3 相似前缀 | `SimilarPrefixSiblingsAreOutsideTheRoot` |
| K4.2/K4.3 相对↔绝对往返 | `ProjectRelativeConversionRoundTrips` |
| K4.5 卷能力 | `PathKeysFollowProbedCaseCapability` |
| K4.5 UNC/rooted 命名空间 | `PathKeysKeepUncAndRootedNamespacesDistinct` |
| K4.5 非 ASCII 折叠 | `PathKeysFoldNonAsciiCaseByVolumeCapability` |
| K4.2 UTF-8 IO 边界 | `NonAsciiPathsUseUtf8AcrossOsBoundary` |
| K4.3 CWD 变更不漂移 | `ResolutionDoesNotDriftWhenCwdChanges` |
| K4.7 链接策略 | `LinksAreRejectedByDefaultAndRecheckedWhenAllowed`（本机实际执行，未 skip） |
| 结果结构一致性 | `SuccessfulResolutionsCarryNoErrorState` |

### 4.2 真实导出矩阵 — `tests/test_bridge/test_project_root.cpp`（8 个用例）

| 场景 | 用例 |
|---|---|
| 任意 CWD + 工程搬迁（临时副本）打开 | `RootedOpenIsIndependentOfProcessCwd` |
| 越界/相对根/缺失根/`..`/UNC/设备前缀 | `RootedOpenRejectsEscapesAndInvalidRoots` |
| 空指针参数 | `NullArgumentsRejectedOnRootedExports` |
| legacy 入口一次性捕获 CWD、切换 CWD 后读写不漂移 | `LegacyOpenCapturesCwdOnceAsExplicitRoot` |
| 导入/改名/脚本读写的根锚定 | `ContentOperationsStayAnchoredToProjectRoot` |
| 旧格式根外绝对条目 → 保留未解析 + 警告；保存 fail-closed 且不写文件 | `LegacyAbsoluteEntryStaysUnresolvedAndSaveFailsClosed` |
| 非 ASCII 根：清单/场景/脚本真实 IO | `NonAsciiRootOpensSavesAndScriptsThroughRealFiles`（`ProjectRootUtf8Test`） |
| `EngineHost_OpenProjectRooted` 同一契约 | `EngineHostRootedOpenFromForeignCwd` |

`EngineHost` 用例刻意独立成 `TEST`：`AcquireGlfwLifetime` 禁止 persistent（EngineHost）与
legacy（EditorSession）生命期在同一进程并存，这是生产约束，不是测试隔离缺陷。

---

## 5. 实际执行结果与日志

所有命令从仓库根运行（与 CTest 工作目录一致），日志为本地产物（未提交）。

| 构建 | 命令 | 退出码 | 日志 |
|---|---|---|---|
| Debug 第一方全量 | `cmake --build build --config Debug` | 0 | `build/m1a_build_debug_full.log` |
| Release 第一方全量 | `cmake --build build --config Release` | 0 | `build/m1a_build_release_full.log` |

**警告按实际数量记录**（上述两次全量编译，third_party 未重编）：

| 配置 | 警告 |
|---|---|
| Debug | 1× C4005（`VK_CHECK` 重定义）、2× C4477（`fprintf` `%zu` 与 `uint32_t`）、2× LNK4006（`RHI::IsBackendAvailable`/`GetDefaultBackend` 在 `RHIBackend.obj` 与 `RHIDeviceFactory.obj` 重复） |
| Release | 同上 5 条 |

全部来自 `EngineCore`，均为本次改动之前即存在的告警；本次修改的 Content/Bridge/测试
TU **没有新增警告**。托管 `editor_avalonia` 本阶段未改动，最近一次构建记录为 6 条
（2× NU1900、1× CS8602、3× CS0414），本次未重跑托管构建。

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `ProjectPathTest` | 17/17 | 17/17 | 包含于下行 |
| `test_content`（仓库根） | 72 通过 / **5 失败** / 1 skip（共 78） | 同左 | `build/m1a_test_content_debug.log`、`build/m1a_test_content_release.log` |
| `test_bridge`（全量） | 52/52 | 52/52 | `build/m1a_test_bridge_debug.log`、`build/m1a_test_bridge_release.log` |
| `test_pick_transport` | 9/9 | 9/9 | `build/m1a_test_pick_debug.log`、`build/m1a_test_pick_release.log` |

`test_content` 的 5 个失败是**既有**缺失 fixture，与 M1-A 无关，未修改用例、未降级：

- `ContentDogfood.DF03_Smoke_LoadRunTimerExpire`、`ContentDogfood.DF03_EntityCount_MultiScript`、
  `Dogfood05.LoadAndRun`、`Dogfood06.LoadAndRun`、`Dogfood07.LoadRunRestart`
- 原因：`assets/scenes/dogfood03/05/06/07.{scene,manifest.json}` 不在仓库中
  （`docs/Handoff-CI-Remaining-Failures.md` §2 已记录，`tracked=0 history=0`）。

**同环境补 fixture 的对照运行**（CI 同款生成器，临时副本，不写仓库资产）：

```
AssetBakery --dogfood-scenes <stage>/assets/scenes
cd <stage> && build/tests/{Debug,Release}/test_content.exe
```

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug（staged） | **77 通过 / 0 失败 / 1 skip** | `build/m1a_test_content_staged_debug.log` |
| Release（staged） | **77 通过 / 0 失败 / 1 skip** | `build/m1a_test_content_staged_release.log` |

skip 的 `ShaderCompileRule.SkippedWithoutVulkanOrShaderc` 是本机未启用 Vulkan/shaderc 的
既有显式跳过，与路径无关。

---

## 6. 与冻结契约的一致性说明（含明确偏差与延后项）

### 6.1 已对齐

- **K4.1**：根显式、绝对、会话内固定；打开失败不留下半成品场景（失败后实体计数 `-1`，
  同一 session 可重新打开）。
- **K4.2**：规范相对形式、`/`、保留大小写；UTF-8 现在真正贯穿 OS 边界。
- **K4.3**：先规范化再包含检查；越界结构化拒绝；无 CWD 回退、无静默改指向。
- **K4.4**：legacy 入口只在边界捕获一次 CWD；根内绝对路径在内存中规范化为相对；
  根外条目打开时保留作者形式 + 警告，**显式保存整体 fail-closed 且在任何写入前失败**；
  打开过程不改写磁盘。
- **K4.7**：默认拒绝链接；显式允许时逐分量 real path 复查并保留诊断。
- **K4.8**：UNC 仅允许作为显式根；`\\?\`/`\\.\` 明确拒绝，不截断。

### 6.2 需要裁决的解释与延后项（未被“声称完成”）

1. **K4.4“找不到则结构化失败”的适用范围**：实现把它解释为**根**找不到/不可用
   （`RootMissing`/`RootNotAbsolute`/`RootNotDirectory`）。**单个资产**路径不可解析时按
   K3/S09 的“缺失 GUID 资产保留”策略处理：警告 + 保留 + 显式保存 fail-closed。
   若契约意图是“单条旧绝对路径找不到也立即失败”，需要裁决并在 M1-B 一并调整。
2. **K4.5 重复 PathKey 加载失败**：`PathKey` 的定义、折叠与比较已实现并验证；
   “两条目同键 → Manifest 加载失败、禁止 last-wins”属于 M1-B 的严格事务加载，
   当前 `ContentRegistry::LoadManifest` 仍是 last-wins。
3. **K4.6 大小写-only rename**：未实现两阶段临时名；当前 `RenameAsset` 是单次
   `fs::rename` + 注册表失败回滚，属于 M1-C 范围。
4. **K4.8 UNC 作为根**：形式策略已实现并测试（内容路径 UNC 拒绝、根形式接受），但本机
   无可用共享，UNC 根的**实际打开**未执行。
5. **Windows long path**：按 K4.8 选择“明确拒绝”，不是截断后放行；长路径工程在 M1-A
   不可用，这是显式决定而非遗漏。
6. **保存的原子性**：`SaveProject` 在清单写失败时场景已写（代码内注明）；统一提交点与
   revision 属 M1-D，M1-A 只保证“规范校验失败时不写任何文件”。
7. **托管导入路径**：`OnImportAsset` 原以 CWD 拼目标目录（M1-E 的导入汇点范围）。
   裁决后经核查确认它会影响项目内资产目标，已在本阶段闭合（见 §8.3），不再延期。
8. **引擎全局 `FileSystem` 基路径**：仍以 CWD 兜底，供非编辑器子系统使用；编辑器内容
   加载已经由 `ProjectRoot` 解析为绝对路径。统一基路径不在 K4 门禁内，列为后续工作。

---

## 7. M1-A 出口自评（对照计划门禁原文）

| 门禁 | 结论 | 证据 |
|---|---|---|
| 全部强制路径规则都有自动化验证 | 是 | §4 的 17 + 8 个用例逐条对应 K4.1–K4.8；含 `\\?\`、UNC、相似前缀、链接越界/环/悬空、非 ASCII、旧格式 |
| Manifest 不依赖 CWD | 是 | 任意 CWD rooted open、legacy 一次性捕获、切换 CWD 后 Save/Import/Rename/Script 均锚定 root（bridge 用例） |
| 路径越界和大小写比较没有未定义行为 | 是 | 越界全部结构化；PathKey 在探测失败时显式回退并有诊断；非法 UTF-8 折叠确定性回退；UNC/rooted 命名空间不混同 |
| 相关测试的实际结果与日志已记录 | 是 | §5 的命令、退出码、计数与日志路径；既有失败单独归因，未追溯改写 |
| 任一边界语义仍未确定时不得声称完成 | 见 §6.2 | 语义均已**确定并写明**；其中 1–3 是延后到 M1-B/M1-C/M1-D 的实现项，4–5 是环境/契约决定，6–8 是明确的范围外遗留项 |

**自评结论**：M1-A 的实现、门禁与证据已就位；上述 §6.2 的 8 项均已有明确处置，
不存在“未决而默认放行”的路径语义。请裁决是否据此关闭 M1-A 并授权 M1-B。
在收到裁决前不推进 M1-B，且不执行任何 Git 写操作。

---

## 8. 裁决记录（2026-10-28）：M1-A ACCEPTED — SCOPE-BOUND

本节追加于报告级评审之后。§5 的实测计数、日志与 §6.2 的延后项保持原样，不因裁决改写；
Stage 0 冻结记录（`docs/Roadmap-Decision-Record-2026-10-28.md`）未改动。

### 8.1 K4.4 限界解释（批准）

| 情况 | 正确处理 | 禁止 |
|---|---|---|
| 无法确定有效的 `ProjectRoot` | 返回结构化失败 | 回退到 CWD 或猜测其他工程根 |
| 旧 Manifest 含根外绝对路径，无法安全解析为工程内资产 | 保留原条目为未解析状态并警告；显式保存时 fail-closed，且不写入任何文件 | 静默删除条目、擅自重写路径、把未经验证的路径当成有效资产 |

**限制（裁决明确）**：第二种处理**只**适用于“根外旧绝对路径”这类有边界的遗留数据保留，
不是所有无效路径的通用降级规则。违反路径结构约束的输入——禁止的 `.`/`..` 路径段、
盘符、UNC、`\\?\`/`\\.\` 设备前缀——仍须按 K4 结构化拒绝，不得转为警告后继续加载。
现有实现与门禁（`LegacyAbsoluteEntryStaysUnresolvedAndSaveFailsClosed`、
`RootedOpenRejectsEscapesAndInvalidRoots`）与该限制一致。

### 8.2 正式认定与风险台账

- 接受 M1-A 本地路径门禁的报告级结果；仓库根目录的 5 个失败继续按既有 fixture 缺失记录，
  临时副本的 77 通过只作补充证据，不覆盖仓库根目录实际运行结果。
- 不认定整个环境零失败、零警告：Debug/Release 第一方全量各 5 条既有警告
  （1×C4005、2×C4477、2×LNK4006）按原样保留。
- **UNC ProjectRoot 实测缺口（持续风险项）**：当前只验证了 UNC 的形式规则与 `PathKey`
  比较，未在真实 UNC 共享上执行端到端打开/保存。K4.8 允许 UNC 根，因此该能力
  **不得记为已实测通过**；在依赖 UNC 文件系统行为的事务提交验收（M1-D/M1-F）之前
  必须补测；完成前不得声称 UNC 环境的提交原子性已获证明。

### 8.3 `OnImportAsset` 边界判定与 M1-A 内闭合

裁决边界：只有当 CWD 残留不再决定工程根、项目内资产目标或 Manifest 路径时，才可延后到 M1-E。
核查结论：`OnImportAsset` 原以 `Directory.GetCurrentDirectory()` 拼出 `assets/gp01/...`
**项目内资产目标**，并用它计算传给 native 的相对路径——属于会决定资产目标的残留，
不能延期。本阶段已在 M1-A 范围内闭合：

- `MainWindow.OnOpenProject` 打开时记录显式工程根 `_projectRoot`；
- `OnImportAsset` 的目标目录与相对路径均由 `_projectRoot` 计算，**不再读取 CWD**；
- `Directory.SetCurrentDirectory` 仅保留给诊断 gate 的 legacy `host.OpenProject`
  （其语义以 CWD 为 legacy root），不参与生产打开、导入或 Manifest 解析。

验证：`dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug` 退出码 0；
12 条警告（4×NU1900、2×CS8602、6×CS0414），均位于既有文件，`MainWindow` 无新增警告。
UI 导入未做自动化端到端验证（如实记录，不据此声称已验证）。

### 8.4 关闭结论

M1-A：`ACCEPTED — SCOPE-BOUND`，残余项如上；M1-B 已获授权。
M1-B 启动时第一项检查（确认唯一数据来源）发现 `current`/Revision 的阶段顺序冲突，
已记录于 `docs/Content-v2-M1-B-Ordering-Conflict.md`；裁决结果为按冻结路线（选项 A）：
M1-B 只做严格候选加载与内存整体交换，`current`/Revision/`RecoveryRequired` 归 M1-D。
M1-B 实现与证据见 `docs/Content-v2-M1-B-Strict-Load.md`。

---

## 9. 附：本次改动文件（工作树，未提交）

生产代码：`engine/include/Engine/Core/Content/{ProjectPaths.h,Utf8Path.h}`、
`engine/src/Core/Content/{ProjectPaths.cpp,Utf8Path.cpp,ContentAsset.cpp,SceneSerializerV1.cpp}`、
`bridge/include/editor_bridge/capi.h`、`bridge/src/{capi.cpp,EditorSession.h,EditorSession.cpp,EngineHost.h,EngineHost.cpp}`、
`engine/include/Engine/Core/Content/SceneSerializerV1.h`、
`editor_avalonia/{MainWindow.axaml.cs,Native/EditorBridgeApi.cs,Services/EditorHostService.cs,Services/*Gate.cs,ViewModels/MainViewModel.cs}`。

测试与构建接线：`tests/test_content/ProjectPathTest.cpp`、`tests/test_bridge/test_project_root.cpp`、
`tests/test_bridge/test_editor_capi.cpp`、`tests/test_bridge/test_pick_transport.cpp`、
`tests/CMakeLists.txt`（`test_bridge` 增加 `EngineCore` 链接与 `engine/include` 头目录，
用于 UTF-8 公共辅助；不改变 C ABI）。

文档（Stage 0 冻结产物，保持原样）：`docs/Roadmap-Decision-Record-2026-10-28.md`、
`docs/Content-v2-M1-0-Source-Audit-Contract.md`、`docs/Engine-Foundation-F2-Charter.md`。
