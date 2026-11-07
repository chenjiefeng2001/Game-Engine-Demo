# P6-1B Gate B3 — Import Commit Integration Record

**Status**: PASS — SCOPE-BOUND（导入提交链接入；Mesh+Material 原子组；C-2 hash 幂等；8/8 fault points；未触发生产渲染/展示）  
**Branch**: avalonia  
**Date**: 2026-11-05/06  
**Precondition**: B1/B2 PASS（未提交工作区累积）

---

## 0. 第一项交付：B2 文件清单纠错

`docs/P6-1B-GateB2-ObjMtl-Record.md` §0 已补正：**B2 实际新增 7、修改 4**（原记录"新增 5"为计数笔误）。以 git 实测为准；实现语义与 PASS 结论不变。

## 1. 文件级变更清单（B3 实测）

| 修改 | 文件 | 内容 |
|---|---|---|
| M | `engine/include/Engine/Core/Content/ContentAsset.h` | AssetType **additive** 扩 `Mesh=2, Material=3`（Texture=0/Script=1 数值与行为不变）；`ToString` 全值；新增 `TryFromString`（未知串严格拒绝） |
| M | `engine/src/Core/Content/ContentAsset.cpp` | LoadManifest 用 `TryFromString` 替换 if/else（语义等价，未知类型失败不变） |
| M | `bridge/src/EditorSession.cpp` | ImportAssetInternal：type 映射 +2（Mesh）、**type=3 fail-closed**；Mesh 分支分流 `ImportModelAsset`；Mesh reattach 拒绝（fail-closed，记录决策） |
| M | `bridge/src/EditorSession.h` | 私有方法声明 `ImportModelAsset`（非 ABI） |
| M | `bridge/src/capi.cpp` | 引入 `Log.h` include（诊断支撑；**C ABI 零变更**） |
| M | `bridge/CMakeLists.txt` | bridge 私有 include 引向 `engine/src`（AssetImport 头文件面） |
| M | `engine/CMakeLists.txt` | GLOB 增加 `src/AssetImport/*.cpp`（B1 已加），新增无 |
| M | `tests/CMakeLists.txt` | 注册 `test_assetimport_commit`（链接 EditorBridge+EngineCore；EditorBridge.dll POST_BUILD staging；TIMEOUT 300） |
| **新增** | `engine/src/AssetImport/ImportDigest.h/.cpp` | C-2 输入摘要（FNV-1a64 组合；source+parser-read deps 确定性编码） |
| **新增** | `engine/src/AssetImport/MeshAssetWriter.h/.cpp` | Mesh/Material 资产 JSON 确定性序列化（写侧；读侧 B5） |
| **新增** | `tests/test_assetimport/B3DigestAndEmissiveTest.cpp` | B3-E emissiveFactor ×4 + B3-C digest ×3 |
| M | `tests/test_bridge/test_editor_capi.cpp` | （返回性 debug 过程中重新核对，最终**零逻辑改动**——仅注释/字符串恢复原位） |
| **新增** | `tests/test_assetimport_commit/TestImportCommit.cpp` | 17 tests（happy ×5、C-2 ×3、控制组 ×1、fault matrix ×8） |

## 2. B3 冻结语义（决策记录）

| ID | 决策 |
|---|---|
| D-B3-1 | **AssetType 数值**：Mesh=2、Material=3 additive；既有 type 数值/序列化不变；type=3 是**内部登记类型**（ImportAsset(type=3) 对用户 fail-closed："bad type"） |
| D-B3-2 | **资产粒度**：一源文件 = 一 Mesh asset（C-1 方案 a）。primitive spans 序列化于 mesh 资产 JSON 内部；材质槽关联 = span.materialIndex |
| D-B3-3 | **内容寻址命名**：mesh 资产 `<stemDir>/<base>.<digest8>.mesh`；material 资产 `<stemDir>/<base>.m<slot>.<mhash8>.material.json`。重导**永不覆写旧 revision 引用的物理字节**（名字承载内容指纹）；未引用 staging 文件为明确记录的遗留（清理策略non-B3） |
| D-B3-4 | **stemDir**：工程内源 = 源文件目录；根外源 = `assets/imported/model/<base>_<abspathHash8>/`；仅根外源物理拷贝 `<base>.<srchash8><ext>`（`CopyFileNoOverwrite`；TargetExists 视为已完成 same-content） |
| D-B3-5 | **Mesh GUID 稳定性**：digest 不变 → content-addressed meshPath 一致 → 注册表精确命中 → 复用既有 GUID、**不产生新 revision**；digest 变 → 词序最小的 `stemDir/<base>.` 前缀 Mesh 条目 GUID 复用（`Unregister + RegisterExplicit` 跨路径重绑——绕过 ContentRegistry "同 GUID 不同路径拒绝" 的方式记录为决策实现，语义 = GUID 稳定 + 路径演进） |
| D-B3-6 | **Material GUID 稳定性**：同 slot 词序最小前缀复用，同机制 |
| D-B3-7 | **多材质 warning**：IR.materials.size()>1 → 显式 warning"v1 renders the first material; ALL materials are registered"（全量注册，slot 关联保留） |
| D-B3-8 | **提交组一次 Publish**：Mesh + N Material 进同一 candidate；ReadCommittedScene 携带旧场景字节；Publish 一次；失败 → 不换 m_Reg、不发事件、不写 mirror |
| D-B3-9 | **B3-E emissiveFactor**：GltfParser 解析 `materials[].emissiveFactor`（3 numbers 严格校验，非法 → GLTF_VALUE_INVALID error）；缺省 [0,0,0] 兼容 |
| D-B3-10 | **摘要覆盖范围**：source + parser-read deps（OBJ: mtllib；glTF: external buffer URIs）。**明确不含**纹理图像 URI（解码为 B4/B5；材料 JSON 里纹理仅 URI 记录）——记录为摘要未覆盖面 |
| D-B3-11 | **路径键归一**：digest 的 pathKey = 小写斜杠化依赖 URI；sourcePathKey = 小写化 fwd 输入。目标：本会话/本机确定性；不声明跨机器 |
| D-B3-12 | **Mesh reattach** 拒绝（fail-closed）："GUID stability 由 digest 保证；用正常重导" |

## 3. C-2 幂等语义验证

| 场景 | 断言 | 结果 |
|---|---|---|
| 同路径、同 bytes 重导（工程内） | same idx + same GUID + revision 不变 | **PASS** |
| 同路径工程内、源 bytes 变化 | same GUID（前缀复用）+ 新 revision | **PASS** |
| **依赖文件变化（MTL 改动）** | OBJ 主文件不变、MTL 变 → **新 revision**（摘要覆盖依赖） | **PASS** |
| 根外源 bytes 重导 | same idx（fast-path by content-addressed copy） | **PASS** |

## 4. B3-D 原子性：8/8 RevisionFaultPoint 矩阵

| Point | 注入 | 断言（均含 events=0、session 不污染） | 结果 |
|---|---|---|---|
| 0 BeforeCandidateManifestWrite | atomic fail (json) | rc=-1、NotCommitted、current bytes 不变 | PASS |
| 1 AfterManifestWrite | atomic fail (scene.scene) | 同上 | PASS |
| 2 AfterSceneWrite | hook 删除候选 scene.scene | 同上 | PASS |
| 3 AfterCandidateValidation | atomic fail (versioned marker) | fresh：current 不出现 | PASS |
| 4 BeforeMarkerWrite | atomic fail (versioned) | fresh：current 不出现 | PASS |
| 5 AfterMarkerWrite | atomic fail (current) | fresh：current 不出现 | PASS |
| 6 BeforeCurrentReplace | atomic fail (current) | 旧 current bytes 不变 | PASS |
| 7 AfterCurrentReplaceBeforeReadback | hook 回写 baseline | RecoveryRequired + baseline 等值 | PASS |

每个 case 在失败路径 **零成功事件**（EV counter delta 0）。

## 5. B3 过程发现并已修复的实现缺陷（本 Gate 主线 2 项）

1. **`ImportAssetInternal` 尾部缺 `return idx;`**（编辑事故 off-end UB → 垃圾值 53 上抛 → 87 个横跨 test_bridge 的假失败）。定位链：测试 idx=53 vs 引擎 idx=33 → capi 打印 → 单文件 diff 复查封口。修复后 test_bridge **113/113** 回绿；全部 fault/happy 恢复。
2. `test_assetimport_commit` 在 ctest 环境的**fixture 复用缺陷**：`RepoRoot()=fs::current_path()`（ctest CWD=build/tests）→ `fs::copy assets/gp01` 失败。修复：`LocateRepoAssetsRoot()` 向上搜索；**数据驱动/GL-free Control 测试化**。

## 6. 测试坐标（B3 全量证据）

| 配置 | test_assetimport | test_assetimport_commit | test_bridge | 全量（test_inventory）|
|---|---|---|---|---|
| Debug | **76/76** | **17/17** | **113/113** | **1101 发现=1101 执行；1099 PASS / 1 FAIL / 1 SKIP** |
| Release | **76/76** | **17/17** | **113/113** | 同上一致 |

- 相对 B2 基线 1077 → **+24**（parser +7 B3-E/B3-C、commit +17）
- **PICK-01 复核**：唯一 FAIL = `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`（同断言行 231/233）——与 B1/B2 基线完全一致，**零新增回归**
- SKIP 唯一 = ShaderCompileRule 环境条件（同前）
- EngineCore/EditorBridge Debug+Release **0 error**（LNK4006×2 仍为 P5 在案既有项）
- 未提交、未推送；dirty 子模块 ×3 与 crashes/ 原样

## 7. 已知限制 / 后续 Gate 记账

| 项 | 说明 | Gate |
|---|---|---|
| 纹理图像不在摘要 | 材料资产仅存 URI；解码 (B4) 后应并入 digest 范围（升级 D-B3-10）| B4 |
| Mesh 资产 JSON 读侧 | B5 需要 reader（B3 只写）——须保持与 writer 字节级兼容 | B5 |
| Unregister+RegisterExplicit 跨路径重绑 | 语义记录 D-B3-5；后续可考虑 ContentRegistry 增设显式 rebind API（合同级变更，非 B5 默认） | 后续 |
| staging 清理策略 | content-addressed 未引用文件零清理（合同允许遗留） | P6-7 |
| ctest CWD 特异性 | fixture 需 `LocateRepoAssetsRoot()`；其他 GL 依赖测试模式已知不可假设 cwd | 既有 |

## 8. 摘要输入编码记录（正文要求）

- 骨架：`gd-import-digest-v1;<sourcePathKey>:<srcHash16>|dep:<depPathKey>:<depHash16>[|dep:…]`
- 单散列 = `RevisionStore::HashBytes` = FNV-1a 64（0xcbf29ce484222325 / 0x100000001b3）；最终散列再一次 FNV-1a 64 over skeleton 的 UTF-8 bytes；输出 16 位小写 hex。
- 路径键：小写、正斜杠、依赖保持其相对形式；外部绝对路径仅在合规解析之后参与。
- 与 `ImportFaultPoint` 无耦合（pure function，可在测试中逐字节重算）。
EOF
