# P6-1B Gate B5-G4 — MeshComponent Runtime Integration Closure

**Status**: PASS — SCOPE-BOUND
**Branch**: avalonia
**HEAD**: `124cdd0`（实测；未提交、未推送；远端 0/0）
**Date**: 2026-11-07
**Precondition**: B5-G3 PASS — ACCEPTED（G3 基线 1160）

---

## 1. G4-A Preflight 结果

### 1.1 GUID / 资产身份（VERIFIED）

- 资产 GUID 唯一类型为 `Engine::ResourceGUID`（128-bit `high/low`；`ResourceGUID.h:28-77`）。
- 规范化表示：`ToHex()` 32 位小写 hex；`FromHex()` 接受大小写/连字符/花括号并归一化；Null = `{0,0}`。Scene/Manifest 均使用 `ToHex()` 持久化。
- `AssetMetaDb` 为 legacy/零生产消费者子系统，且 `ReadMetaFile()` 将所有类型强制为 Texture、不支持 Mesh/Material——**G4 不使用 AssetMetaDb**。真实资产身份源是 `ContentRegistry` + `RevisionStore` committed manifest。
- `ContentRegistry` 类型枚举已含 `Mesh/Material/Texture/Script`；`TypeOf()` 对缺失 GUID 回退 Texture（不可用于“缺失”判断），因此 resolver 先 `ContainsGuid()` 再取类型。

### 1.2 Committed current（VERIFIED）

- `RevisionStore` 是**项目级**提交点，不是 per-asset store：`Open()` 只读 `.engine-content/current`；`IsVersioned()` = current 有效；`Revision()`、`CommittedManifestPath()` 提供 committed identity/path。
- revision 目录只含 `manifest.json` / `scene.scene`；资产文件留在项目树中（部署上 content-addressed 不可覆盖，B3/B4 不覆盖旧路径）。
- staging/candidate 只在 `Publish()` 成功替换 `current` 后才成为 current；失败保留旧 current；`Open()` 不扫描 revision 目录（`RevisionStore.cpp:193-311`）。
- 类型/缺失校验：manifest 严格加载（`ContentRegistry::LoadManifest`）拒绝未知类型、Null/重复 GUID、路径越界；`Publish` 不做资产文件存在性检查，因此 resolver 必须自己读 bytes 并报错。

### 1.3 Runtime 所有权与注入缝隙（VERIFIED）

- `EditorSession` 私有拥有 `m_Root`/`m_Reg`/`m_Revisions`/`m_EditScene`；`m_Session->GetEditScene()` 已有 bridge-internal C++ seam。G4 仅新增 two C++-only getters（`GetProjectRootForRuntime`、`GetRevisionStoreForRuntime`），**未改 C ABI / capi.h / op 枚举 / 结构体**。
- `EngineHost` 拥有单例 `Application`、`OpenGLGraphicsFactory`、runtime thread；production 渲染在 `PumpOneFrame` 内、context current 上执行，且 Direct/RenderGraph 共用 `Application::m_Production3DMeshRenderer`。
- `MeshComponent` 在 preflight 时：未注册、无 GUID、无 Serialize/Deserialize、无资产加载路径；渲染只消费显式 `SetMesh()` 的 CPU 对象。

### 1.4 G1–G3 连接事实（VERIFIED）

- G2 `MeshGpuCache` key = `{identity, payloadDigest}`，owner/context 门控，payload digest 由 `ComputePayloadDigest` 提供；G2 key 生产路径原先固定 `identity=0`（注释标注 G4 接线）。
- G3 reader/resolver/binding/TextureGpuCache 已存在；但 preflight 发现 **G3 `SetMeshMaterialBinding/ClearMeshMaterialBindings/GetMeshMaterialBindingForTesting` 只有声明、无定义**（无调用者，故 G3 构建/测试未暴露）。G4 已补齐定义并接入（见 §5）。

---

## 2. 持久化身份合同（MeshComponent）

- 新增字段：`ResourceGUID m_MeshGuid`，默认 `Null`。
- 序列化键：`meshGuid`，值为 `ToHex()`（32 位小写 hex）。
- 缺失字段 → Null（unloaded，不是错误）；非法字符串/非字符串 → `Deserialize` 返回 false、GUID 保持 Null；显式 `"000…0"` → Null 且成功。**不会产生“GUID 不完整但看似有效”的 Component**。
- Scene format version 保持 **1**（无版本升级/迁移）；旧 Scene 无 `MeshComponent` 时行为不变。
- `GetComponentTypeName()` = `"Mesh"`；仅持久化 GUID，不序列化 GPU handle、CPU 指针、revision path、runtime status。
- 注册进既有 `ComponentRegistryGo`（`Register("Mesh", ...)`），未新建 registry；未注册 legacy `JsonSerializer`（V1 编辑器路径不使用它，避免双重 `type` 合同）。

---

## 3. Committed Mesh Resolver

新增 `Engine::Content::MeshAssetResolver`（public engine header）：

- 输入：`ProjectRoot` + `RevisionStore` + GUID。
- 每次 `Resolve` 校验 `IsVersioned()`；以 `CommittedManifestPath()` 严格加载当前 committed manifest（按 `Revision()` 缓存快照，revision 变化自动刷新）。
- 禁止：目录扫描、按文件名反推 GUID、读取 staging/candidate、猜测路径。
- 输出：`revision`、committed `assetPath`、`absolutePath`、完整 bytes、类型；失败状态：
  `NotCommitted / CommittedManifestInvalid / InvalidGuid / MissingAsset / UnsupportedAssetType / PathRejected / AssetBytesUnreadable`。
- 路径安全：所有读取经 `ProjectRoot::Resolve`（`..`/绝对路径/UNC/设备前缀/逃逸/symlink 检查）；字节大小设 512MiB 上限。
- committed 校验信任 `RevisionStore` 在 Open/Publish 时完成的 current→manifest/scene 哈希校验；运行期间由外部进程改写 committed 文件不在本 Gate 范围（同进程 session 内一致性已证明）。
- Material/Texture 辅助读取（`ReadProjectFile`/`AbsoluteProjectPath`）使用同一 root 边界，供 G3 binding 使用。

---

## 4. Runtime Load Pipeline 与原子切换

新增 `LoadMeshRuntimeAsset`（`engine/src/AssetImport/MeshRuntimeLoader.*`）：

`GUID → resolver → bytes → ReadMeshAssetJson → BuildMeshUploadInput → CPU Mesh → BuildMeshMaterialBinding (committed entries + TextureGpuCache) → MeshRenderer::EnsureGpuMesh → MeshGpuResourceKey`。

- **All-or-nothing**：任一步失败不返回 partial CPU/GPU/binding；失败阶段映射到 `MeshRuntimeStatus`：
  `ReaderFailure / InvalidAsset / MaterialBindingFailure / TextureBindingFailure / GPUUploadFailure / NotCommitted / MissingAsset / UnsupportedAssetType`。
- GPU key：`identity = MeshComponent::FoldGuid(GUID)`（稳定 64-bit fold），`payloadDigest = ComputePayloadDigest(实际 GPU payload)`；同一 GUID + 相同 payload 复用，内容变化产生新 digest；不以 CPU 地址作 key。
- Texture-only revision：mesh key 不变，binding 按新 revision 重建并覆盖同一 key，避免旧绑定复用。
- revision 在加载期间变化 → `NotCommitted`，不发布混合 revision。

新增 `Engine::MeshRuntimeScene`（RHI）：

- 每项目 GUID → `{revision, path, CPU mesh, GPU key, binding}` 运行时表；按 Scene 全量编排（含 children 收集）。
- 成功后才 `SetRuntimeState(Ready)` 并替换 Component CPU mesh；失败时保留旧有效 mesh（若存在）并记录明确 failure；失败按 revision 缓存，避免每帧重复 I/O。
- 新 revision 成功前旧 GPU/binding 保留；成功后移除旧 key。
- 多 Entity 共享同一 GUID：只加载一次，共享 CPU Mesh/GPU key/binding；最后一个引用移除后才 `RemoveMeshGpuResource`（binding + `Invalidate(key)`），不误删其他 Entity。
- Component 移除/GUID 修改/场景卸载由下一帧 Sync 对账；`Unload()` 在 GL owner thread 释放本表绑定与 GPU 资源。
- 失败缓存策略：同一 revision 内不自动重试（committed 内容不可变）；新 revision 到达后自动重试。

---

## 5. Renderer / EngineHost 接入

- G3 seam 修复：补齐 `SetMeshMaterialBinding` / `ClearMeshMaterialBindings` / `GetMeshMaterialBindingForTesting` 定义，新增 `EnsureGpuMesh`、`RemoveMeshGpuResource`、`GpuMeshCountForTesting`。
- `GetOrCreateGpuMesh(mesh, assetIdentity=0)`：生产 MeshComponent 路径传入 `GetAssetIdentity()`；payload digest 改为实际 GPU payload digest（vertex/index/span/flags）。
- `EngineHost`（C++ internal only）：
  - open project 成功后 `RefreshMeshRuntime()`（构造 resolver + texture cache + runtime scene）；
  - 每帧 `PumpOneFrame` 在 production render 前 `SyncMeshRuntime()`（context current、runtime thread）；
  - close/shutdown 在 context current 时 `TeardownMeshRuntime()`（清 binding/GPU/texture cache）；
  - 不新增任何 C ABI export/op/struct。
- `Application` 暴露 bridge-internal `GetProduction3DMeshRenderer()`；Direct 与 RenderGraph 仍调用同一个 `m_Production3DMeshRenderer->Render(...)`，无第二套 resolver/cache/binding。

---

## 6. 测试与验证证据

### 6.1 构建

- CMake Debug 全量：成功。
- CMake Release 全量：成功。
- `EditorBridge` Debug/Release：成功（含 EngineCore 链接）。
- `editor_avalonia` Debug/Release：`dotnet build` 成功（仅既有 warning）。

### 6.2 新增专项测试

| 测试文件 | 数量 | 覆盖 |
|---|---|---|
| `tests/test_content/MeshAssetResolverTest.cpp` | 5 | committed current bytes、unversioned/missing/non-mesh、staging 不可见、Publish 失败保留旧 current、同 GUID reimport、缺失 bytes/manifest、路径穿越 |
| `tests/test_content/MeshComponentSerializationTest.cpp` | 6 | Registry、GUID round-trip、malformed/explicit-null、Capture→Save→Cold Load、旧 Scene 兼容、多个 Entity 同 GUID、稳定重存 |
| `tests/test_assetimport/B5MeshRuntimeSceneTest.cpp` | 11 | CPU+GPU+binding Ready、多 Entity 复用/移除、Missing→新 revision 恢复、Reader/GPU/Binding 失败、Material/Texture fail-closed、revision 原子切换、texture-only revision、GUID 变更/Unload、GUID 改为缺失时安全停止绘制、非 Mesh 类型拒绝、Cold scene reload + reimport |
| `tests/test_bridge/test_mesh_runtime_host.cpp` | 2 | 真实 EngineHost：committed 项目 + Scene MeshComponent → PumpOneFrame → runtime map Ready；Direct 与 RenderGraph 选择下同一 key/digest。真实 EditorSession：SaveProject → 冷重启 → 恢复同一 GUID |

### 6.3 定向结果

- `test_assetimport`：**143/143 PASS**（G3 132 + 11）。
- `test_content`：**127 PASS / 1 SKIP**（128 = G3 117 + 11；SKIP 为既有 shaderc 条件项）。
- `test_bridge`：**115/115 PASS**（G3 113 + 2）。
- `test_assetimport_commit`：20/20 PASS。
- `test_viewport_resize`：5/5 PASS。
- G1/G2/G3 全部专项（B5MeshAssetReader/B5GpuMeshResource/B5MaterialTextureRuntime/B4）随 `test_assetimport` 全通过；B3/B4 commit fault matrix 随 `test_assetimport_commit` 全通过。

### 6.4 全量 inventory（Debug/Release 一致）

| 指标 | G3 基线 | G4 结果 | 差异 |
|---|---|---|---|
| discovered | 1160 | **1184** | +24（11+11+2） |
| executed | 1160 | **1184** | +24 |
| PASS | 1158 | **1182** | +24 |
| FAIL | 1（PICK-01） | **1（PICK-01）** | 0 |
| SKIP | 1（SDK 条件项） | **1（SDK 条件项）** | 0 |

- 唯一 FAIL：`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，断言仍为
  `tests/test_bridge/test_pick_transport.cpp(231)` / `(233)`（status HIT / entity_id ≥ 1，actual 0）。
- 唯一 SKIP：`ShaderCompileRule.SkippedWithoutVulkanOrShaderc`。
- 所有验证使用本次构建产生的二进制（解决 G3 stale-binary 暴露问题）。
- `ManifestStrictLoadTest.UnknownTypeIsRejected` 的 `"Bogus"` 负例继续通过，确认仍验证未知类型拒绝。

---

## 7. 所有权 / 生命周期

- MeshComponent 只持有持久化 GUID + 运行时状态 + CPU `shared_ptr<Mesh>`；不持有 VAO/VBO/IBO/Texture handle。
- GPU mesh 由 `MeshRenderer::MeshGpuCache` 拥有；纹理由 `EngineHost::TextureGpuCache` 拥有；binding 以 `shared_ptr<const MeshMaterialBinding>` 挂在 renderer map。
- 多 Entity 同 GUID：`MeshRuntimeScene` 单条目共享；移除一个 Entity 只减引用；最后一个引用消失后 invalidate key。
- Scene unload/CloseProject/Shutdown：`TeardownMeshRuntime` 在 context current 时释放 binding/GPU/texture；之后既有 `~MeshRenderer`/`~MeshGpuCache` 兜底。
- 无 double-delete：GPU 资源仅由 cache 失效；纹理仅由 TextureGpuCache 释放；测试覆盖 RemoveComponent/RemoveObject/Unload。

---

## 8. 状态矩阵

| 事项 | 状态 |
|---|---|
| GUID 作为唯一持久化 Mesh 身份（Scene 不存 path/handle/status） | **VERIFIED** |
| Resolver 只读 committed current，无扫描/猜测/staging | **VERIFIED**（API + fault/staging 测试） |
| Publish 失败保留旧 current | **VERIFIED** |
| 跨进程运行中发布（local RevisionStore 不自动刷新） | **UNKNOWN（不在 G4 scope；session 内 reimport 已验证）** |
| Reimport 同 GUID → 新 current/新 GPU payload | **VERIFIED** |
| 新 revision 失败不破坏旧有效资源；成功后原子切换 | **VERIFIED** |
| 多 Entity 引用/单 Entity 删除安全 | **VERIFIED** |
| Cold save/destroy/reload/re-resolve | **VERIFIED**（含真实 GL runtime） |
| Direct/RenderGraph 共用同一 renderer/runtime key | **VERIFIED**（host 测试检查两种选择下 key/digest 相同；像素证明属 G5） |
| Material/Texture missing/fail 分类状态 | **VERIFIED** |
| GPU upload failure 状态 | **VERIFIED**（注入 `AfterVertexArray`） |
| 生产 viewport 可见像素（含材质颜色/纹理） | **PARTIAL / DEFERRED — G5 门禁** |
| MeshComponent 其它渲染属性（color/textures）持久化 | **MISSING（不在 G4 范围；V1 既有局限）** |
| 子节点层级持久化/渲染 | **MISSING（V1 不序列化 hierarchy；Application 只渲染 root 列表）** |
| TextureGpuCache 淘汰策略（当前只清空，无 LRU） | **PARTIAL（有界于导入资源；G6/后续评估）** |
| GPUUpload 失败同 revision 自动重试 | **PARTIAL（按 revision 缓存，需新 revision 或重开项目重试）** |
| `m_MeshCache_RHI` pointer-key 例外隔离 | **UNCHANGED（G2 记录；未进入 G4 生产链路）** |

---

## 9. 文件清单（G4 增量）

**新增**

- `engine/include/Engine/Core/Content/MeshAssetResolver.h`
- `engine/src/Core/Content/MeshAssetResolver.cpp`
- `engine/include/Engine/Core/RHI/MeshRuntimeScene.h`
- `engine/src/Core/RHI/MeshRuntimeScene.cpp`
- `engine/src/AssetImport/MeshRuntimeLoader.h/.cpp`
- `tests/test_content/MeshAssetResolverTest.cpp`
- `tests/test_content/MeshComponentSerializationTest.cpp`
- `tests/test_assetimport/B5MeshRuntimeSceneTest.cpp`
- `tests/test_bridge/test_mesh_runtime_host.cpp`
- `docs/P6-1B-GateB5-G4-MeshComponent-Integration-Closure.md`

**修改**

- `engine/include/Engine/Core/GameObject/MeshComponent.h`（GUID/status/serialize/type name）
- `engine/src/Core/GameObject/ComponentRegistry_Go.cpp`（注册 "Mesh"）
- `engine/include/Engine/Core/RHI/MeshRenderer.h` / `engine/src/Core/RHI/MeshRenderer.cpp`（G3 seam 定义补齐、identity/payload key、runtime seam）
- `engine/src/AssetImport/MaterialRuntimeBinding.h/.cpp`（failure stage，additive）
- `engine/include/Engine/Application.h`（internal renderer accessor）
- `bridge/src/EditorSession.h`（internal committed-access getters）
- `bridge/src/EngineHost.h/.cpp`（resolver/runtime/texture cache 生命周期与每帧 Sync、测试查询 seam）
- `tests/CMakeLists.txt`（4 个测试 target 注册）

**未改动**：B1–B4 writer/digest/sidecar 合同、RevisionStore 原子性、Bridge C ABI（capi.h/ops/structs）、Scene version、`m_MeshCache_RHI`、第三方依赖、子模块、`crashes/`（115 项）。

---

## 10. 回滚

回滚 G4 新增文件与上述修改即可；无资产格式/版本迁移、无持久化 cache。回滚后 G3 基线 1160 应恢复；PICK-01 仍为唯一 FAIL。

---

## 11. 不在本 Gate（等待后续授权）

- B5-G5：Production Render End-to-End Pixel Proof（真实像素/材质可见性）。
- B5-G6：Integrated Regression / Closure。
- B6：AssetBrowser / Add to Scene / Inspector / 拖放 / Undo-Redo / 3D picking UI。
- 多材质完整渲染、PBR 扩展、Vulkan/D3D12 mesh runtime、层级编辑。
