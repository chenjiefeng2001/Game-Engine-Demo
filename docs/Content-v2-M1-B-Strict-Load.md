# Content v2 M1-B：Manifest 严格事务加载 — 交付记录

**范围**：`docs/Roadmap-Decision-Record-2026-10-28.md` §3.6 M1-B 行，按 2026-10-28
裁决选项 A（冻结路线）实施。
**状态**：实现与门禁已落地；2026-10-28“回归开发基线、继续推进实施”指令后按
scope-bound 关闭，M1-C 已启动（见 `docs/Content-v2-M1-C-Rename-Script-Save.md`）。
**未执行**：commit / push / tag / merge；未触碰既有子模块脏标记与 `crashes/`。

---

## 1. 范围裁决（选项 A）

2026-10-28 的裁决确认：M1-B 只做 Manifest 的**严格候选加载与内存整体交换**；
`current` 指针、Revision、`RecoveryRequired` 的读取与判定属于 M1-D（K1 明确把
`current` 物理形态留给 M1-D）。裁决前的冲突证据见
`docs/Content-v2-M1-B-Ordering-Conflict.md`。

因此本阶段**未**实现：`current` 读取、Revision 单调性、孤立候选升级判定、
`RecoveryRequired` 状态机。这些将随 M1-D 的提交原语一并落地。

---

## 2. 实现

| 位置 | 内容 |
|---|---|
| `engine/include/Engine/Core/Content/ProjectPaths.h:75` | 公开 `PathKey(path, mode)`：分隔符归一 + 按卷能力折叠大小写；`ProjectRoot::ComparisonKey` 复用同一实现 |
| `engine/src/Core/Content/ProjectPaths.cpp:278` | `PathKey` 实现（调用既有 `ComparisonKeyFor`） |
| `engine/include/Engine/Core/Content/ContentAsset.h:46` | `ManifestLoadOptions{ root, allowLegacyAbsolute, caseMode, warnings }` |
| `engine/include/Engine/Core/Content/ContentAsset.h:91` | `LoadManifest(path, options = {})` 严格契约 |
| `engine/src/Core/Content/ContentAsset.cpp:106` | 重写 `LoadManifest`：完整候选校验后 `*this = std::move(candidate)`；任一条目失败原表不变 |
| `bridge/src/EditorSession.cpp:178-190` | 打开流程改用严格加载（root + legacy 允许 + 根的卷能力）；删除加载后的 `CanonicalizeRegistry` 二次迁移 |
| `sandbox/src/AssetBakery/main.cpp:306-314` | 修正既有 df03 fixture：三个 GUID 改为三个不同路径（同 PathKey 多 GUID 违反 K1.4/K4.5） |

### 2.1 加载校验顺序（任一失败 → 整体失败，旧表不变）

1. 文件可打开；JSON 可解析（损坏/截断 → 失败）。
2. 顶层为对象；`version` 存在、整数、等于 1；`assets` 存在且为数组。
3. 每条资产：是对象；`guid`/`path`/`type` 均存在且为字符串。
4. GUID 可解析且非空；以 `ToHex()` 规范形式做重复检测（大小写、连字符变体视为同一身份）。
5. `type` 只能是 `Texture` / `Script`（未知类型拒绝，不再降级为 Texture）。
6. 路径：
   - 有根：`ToCanonicalProjectPath`；成功 → 规范相对路径；
     仅当 `allowLegacyAbsolute` 且错误为 `EscapesRoot`（根外旧绝对路径）→ 保留作者原样并追加警告；
     `.`/`..`、UNC、设备前缀、盘符相对等**结构违规一律整体失败**，不转警告。
   - 无根：`NormalizeProjectRelative`；非法 → 失败（绝对/UNC/设备路径同样拒绝）。
7. `PathKey` 重复检测：有根用根探测的卷能力；无根用 `options.caseMode`。
   分隔符变体恒为同一键；大小写差异只在大小写不敏感卷上算冲突。
8. 用 `RegisterExplicit` 写入候选表（GUID 冲突防护的第二道检查）。
9. 全部通过后一次性交换到 `this`。

### 2.2 会话层事务

`EditorSession::OpenProjectInternal` 的顺序保持：根 → 清单（严格加载）→ 场景解析 →
实例化 → 成功后才提交 `m_Root/m_Reg/m_EditScene/m_Bindings/...`。
清单或场景任一步失败都不会修改旧会话；`m_Warnings` 也只在成功提交时重建。

---

## 3. 门禁矩阵

### 3.1 单元门禁 — `tests/test_content/ManifestStrictLoadTest.cpp`（12 个用例）

| 规则 | 用例 |
|---|---|
| 合法清单、分隔符归一、GUID 连字符变体 | `AcceptsValidManifestAndCanonicalizesPaths` |
| 损坏 JSON / 截断 | `CorruptJsonLeavesRegistryUnchanged` |
| 版本缺失/非整数/不支持 | `MissingOrInvalidVersionIsRejected` |
| 半条目、非字符串 GUID、非对象条目 | `HalfEntryFailsWithoutPartialSwap` |
| 未知类型 | `UnknownTypeIsRejected` |
| 重复 GUID（精确/大小写/连字符/同路径） | `DuplicateGuidFailsIncludingCaseAndDashVariants` |
| 重复 PathKey（分隔符恒冲突；大小写按卷能力） | `DuplicatePathKeyFollowsSeparatorsAndVolumeMode` |
| 结构违规路径（`.`/`..`/空/盘符相对/UNC/设备） | `StructuralPathViolationsRejectWholeManifest` |
| 根外旧绝对路径：仅允许时保留 + 警告；关闭则失败 | `LegacyAbsoluteOutsideRootKeptOnlyWhenAllowed` |
| 根内绝对路径 → 规范相对 | `AbsolutePathInsideRootCanonicalizes` |
| 缺失物理资产保留 GUID（K3/S09） | `MissingPhysicalAssetKeepsGuid` |
| 失败不污染既有表（“两好一坏”） | `FailureKeepsExistingEntriesIntact` |

### 3.2 会话门禁 — `tests/test_bridge/test_project_root.cpp`

`StrictLoadFailureLeavesExistingSessionUsable`：

- 先成功打开 GP01（10 实体、33 资产）；
- 重复 GUID 清单 → 打开失败；实体/资产计数不变；
- 损坏清单 → 打开失败；计数不变；
- 损坏场景（清单已解析）→ 打开失败；计数不变；
- 旧会话仍可 `CreateEntity` 并 `SaveProject` 成功。

另有两项既有门禁继续覆盖 K4.4 与 UTF-8：
`LegacyAbsoluteEntryStaysUnresolvedAndSaveFailsClosed`、
`NonAsciiRootOpensSavesAndScriptsThroughRealFiles`。

---

## 4. 实际执行结果

构建（第一方全量，third_party 未重编）：

| 配置 | 命令 | 退出码 | 警告 | 日志 |
|---|---|---|---|---|
| Debug | `cmake --build build --config Debug` | 0 | 5 条（1×C4005、2×C4477、2×LNK4006，全部 EngineCore 既有） | `build/m1b_build_debug_full.log` |
| Release | `cmake --build build --config Release` | 0 | 同上 5 条 | `build/m1b_build_release_full.log` |

测试（从仓库根运行，与 CTest 工作目录一致）：

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `ManifestStrictLoadTest` | 12/12 | 12/12 | 包含于 `test_content` 日志 |
| `ProjectPathTest` | 17/17 | 17/17 | 同上 |
| `test_content`（仓库根） | **84 通过 / 5 失败 / 1 skip**（共 90） | 同左 | `build/m1b_test_content_{debug,release}.log` |
| `test_bridge` | 53/53 | 53/53 | `build/m1b_test_bridge_full_{debug,release}.log` |
| `test_pick_transport` | 9/9 | 9/9 | `build/m1b_test_pick_{debug,release}.log` |

`test_content` 的 5 个失败仍是既有缺失 fixture（dogfood03/05/06/07），与 M1-B 无关，
未修改用例；补 fixture 后的对照见下。

**staged 对照**（临时副本 + CI 同款 `AssetBakery --dogfood-scenes`）：

| 阶段 | Debug | Release |
|---|---|---|
| 首次运行（生成器修复前） | 87 通过 / **2 失败** / 1 skip | 同左 |
| 生成器修复并重生成后 | **89 通过 / 0 失败 / 1 skip** | 同左 |

首次 staging 失败的是 `ContentDogfood.DF03_Smoke_LoadRunTimerExpire` 与
`DF03_EntityCount_MultiScript`：原 df03 清单让三个 GUID 指向同一 `test.png`。
该 fixture 依赖旧加载器“同路径多 GUID”的容错，违反 K1.4/K4.5；**修复的是生成器
（改为三个不同且存在的路径），不是放宽加载器**。原始失败与修复后结果均保留在日志中。

### 4.1 开发基线回归（2026-10-28，M1-B 关闭前）

在 M1-B 之后运行完整 CTest（17 个 target）：

| 配置 | 结果 | 日志 |
|---|---|---|
| Debug | **16/17 通过**；唯一失败 `test_content`（5 个既有缺失 fixture） | `build/m1b_baseline_ctest_debug.log` |
| Release | 同上 | `build/m1b_baseline_ctest_release.log` |

`test_content` 的 5 个失败与 §4 相同，未改写成通过；其余 target（含 `test_renderer`
像素、`test_hrc`、`test_viewport_resize`、`test_e2e`）全部通过。

---

## 5. M1-B.2 退出门禁自评（按选项 A 重述）

| 门禁 | 结论 | 证据 |
|---|---|---|
| `current` 是唯一已提交版本依据，无候选自动晋升 | **M1-B 范围内不适用**（按裁决归 M1-D）；M1-B 不存在任何候选晋升路径：只在内存候选校验通过后交换 | §1、§2 |
| Manifest 完整校验后才进入可见已加载状态 | 是 | §2.1；12 个单元用例 |
| 加载失败不留半新半旧、不破坏已有会话 | 是 | 注册表候选交换 + 会话级 `StrictLoadFailureLeavesExistingSessionUsable` |
| 重复 GUID 与重复 PathKey 有可执行失败测试 | 是 | `DuplicateGuidFailsIncludingCaseAndDashVariants`、`DuplicatePathKeyFollowsSeparatorsAndVolumeMode` |
| 合法缺失资产保留原 GUID | 是 | `MissingPhysicalAssetKeepsGuid`；加载器不检查物理存在性 |
| 无法证明有效提交状态的场景有 `RecoveryRequired` 判定 | **延后 M1-D**（当前不存在提交状态/Revision，无法在本阶段判定） | 见 §1 |
| Debug/Release 构建、测试、警告、失败如实记录 | 是 | §4；既有 5 失败保持原状，未追溯改写 |

**自评**：按裁决选项 A，M1-B 的实现与门禁已就位；`current`/Revision/`RecoveryRequired`
三项由 M1-D 承担，不因 M1-B 关闭而视为已验证。请裁决是否关闭 M1-B 并授权 M1-C。

---

## 6. 残余风险与明确延后项

1. **保存仍非单一事务**：`SaveProject` 先写场景再写清单；清单写失败时场景已落盘。
   单一提交点与 `current` 属 M1-D；本阶段只保证“规范校验失败时零写入”。
2. **跨文件引用一致性**：加载器校验清单内部身份/路径一致性；Manifest↔Scene 的
   revision 级全量校验属 M1-D（K1.4 含 Scene 解析）。
3. **导入时的活动会话冲突**：`Import`/`RegisterExplicit` 仍按精确路径判重；
   大小写-only 的目标冲突、覆盖复制与 GUID 分配属 M1-E。
4. **无根默认策略**：直接调用 `LoadManifest` 而不传 `root` 时，绝对路径一律拒绝；
   EditorSession 通过 `root` + `allowLegacyAbsolute` 保留 K4.4 兼容。
5. **UNC 实际行为**：M1-A 遗留的 UNC 根端到端实测缺口仍有效；在依赖 UNC 文件系统
   行为的事务提交验收（M1-D/M1-F）前必须补测。

---

## 7. 本次改动文件（工作树，未提交）

- `engine/include/Engine/Core/Content/{ContentAsset.h,ProjectPaths.h}`
- `engine/src/Core/Content/{ContentAsset.cpp,ProjectPaths.cpp}`
- `bridge/src/EditorSession.cpp`
- `sandbox/src/AssetBakery/main.cpp`
- `tests/test_content/ManifestStrictLoadTest.cpp`（新增）
- `tests/test_bridge/test_project_root.cpp`
- `tests/CMakeLists.txt`
