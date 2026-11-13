# P6-1B B5 Runtime Mesh — Final Integrated Closure (G6)

**Status**: PASS — INTEGRATED REGRESSION — B5 CLOSED — SCOPE-BOUND
**Branch**: avalonia
**HEAD**: `5de8f1a`（G5 完成时按用户明确要求提交并推送；G6 本身未提交/未推送）
**Date**: 2026-11-07
**Precondition**: B5-G5 PASS — PRODUCTION PIXEL VERIFIED — SCOPE-BOUND

---

## 1. Executive Closure

B5 的 Runtime Mesh 交付已完成并通过 G6 集成回归：

- 资产导入（OBJ/MTL/glTF/GLB）→ committed RevisionStore → 稳定 Mesh GUID。
- committed-only Runtime resolver → CPU Reader → GPU Cache → Material/Texture binding。
- MeshComponent GUID 持久化 + Scene v1 兼容 + 冷启动恢复。
- Direct / RenderGraph 生产 Renderer 的真实 presentation FBO 像素验证。
- G5 发现的 3-channel PNG 缺陷修复已纳入正式回归，并由 G6 独立复验。

G0～G5 独立结算文档保留原文，不被本文件覆盖。

| Gate | 状态 | 文档 |
|---|---|---|
| B5-G0 | COMPLETE — READ-ONLY MAP | `docs/P6-1B-GateB5-G0-Runtime-Mesh-Map.md` |
| B5-G1 | PASS — SCOPE-BOUND | `docs/P6-1B-GateB5-G1-Mesh-Reader-Closure.md` |
| B5-G2 | PASS — SCOPE-BOUND | `docs/P6-1B-GateB5-G2-GPU-Mesh-Resource-Closure.md` |
| B5-G3 | PASS — SCOPE-BOUND | `docs/P6-1B-GateB5-G3-Material-Texture-Runtime-Closure.md` |
| B5-G4 | PASS — SCOPE-BOUND | `docs/P6-1B-GateB5-G4-MeshComponent-Integration-Closure.md` |
| B5-G5 | PASS — PRODUCTION PIXEL VERIFIED | `docs/P6-1B-GateB5-G5-Production-Viewport-Pixel-Closure.md` |
| B5-G6 | PASS — INTEGRATED REGRESSION — B5 CLOSED | 本文件 |

---

## 2. Gate Ledger

| Gate | 核心实现 | 专项测试 |
|---|---|---|
| G0 | 只读地图：`docs/P6-1B-GateB5-G0-Runtime-Mesh-Map.md` | — |
| G1 | `engine/src/AssetImport/MeshAssetReader.{h,cpp}` | `tests/test_assetimport/B5MeshAssetReaderTest.cpp` |
| G2 | `MeshGpuResource`、`MeshUploadAdapter`、`MeshRenderer` GPU cache 接入 | `B5GpuMeshResourceTest.cpp` |
| G3 | Material/Texture reader、slot resolver、`MaterialRuntimeBinding`、`TextureGpuResource` | `B5MaterialTextureRuntimeTest.cpp`、`B4TextureSrgbGLTest.cpp` |
| G4 | `MeshComponent.meshGuid`、`MeshAssetResolver`、`MeshRuntimeLoader`、`MeshRuntimeScene`、EngineHost Sync | `MeshAssetResolverTest.cpp`、`MeshComponentSerializationTest.cpp`、`B5MeshRuntimeSceneTest.cpp`、`test_mesh_runtime_host.cpp` |
| G5 | Production pixel harness / FBO readback | `tests/test_mesh_pixel/B5ProductionPixelTest.cpp` |
| G6 | 集成回归 + 3-channel PNG 复验 + 关闭文档 | 本文件与 G6 新增回归 |

---

## 3. Final Runtime Architecture（实际生产调用链）

```text
Source OBJ/GLTF/GLB
  → EditorSession::ImportModelAsset (bridge/src/EditorSession.cpp)
  → MeshUploadAdapter / TexturePreprocess / MeshAssetWriter / Material writer
  → RevisionStore::Publish (single committed revision)
  → Scene MeshComponent.meshGuid (ResourceGUID, ToHex, Scene v1)

Runtime frame (EngineHost runtime thread, GL context current)
  → EngineHost::PumpOneFrame
  → EngineHost::SyncMeshRuntime
  → MeshRuntimeScene::Sync
  → MeshAssetResolver::Resolve        (committed current manifest only)
  → ReadMeshAssetJson                 (G1)
  → BuildMeshUploadInput              (G2 adapter)
  → MeshRenderer::EnsureGpuMesh
  → MeshGpuCache::Acquire             (identity + GPU payload digest)
  → BuildMeshMaterialBinding          (G3)
  → MaterialRuntimeBinding            (Material/Texture slot resolution)
  → TextureGpuCache::Acquire          (identity/content/upload semantics)
  → MeshRenderer::SetMeshMaterialBinding

  → Application::RenderProduction3DFrame
      ├─ Direct branch: MeshRenderer::Render
      └─ RenderGraph branch: RenderGraph Execute → MeshRenderer::Render
  → presentation FBO (800×600, RGBA8)
  → EngineHost::ReadPresentationPixels / GL glReadPixels
```

关键符号：`EditorSession::ImportModelAsset`、`RevisionStore::Publish`、`MeshComponent::m_MeshGuid`、`MeshRuntimeScene::Sync`、`MeshAssetResolver::Resolve`、`MeshAssetReader`、`MeshUploadAdapter`、`MeshGpuCache`、`MaterialRuntimeBinding`、`TextureGpuCache`、`MeshRenderer::Render`、`Application::RenderProduction3DFrame`、`OpenGLFramebuffer`、`EngineHost::ReadPresentationPixels`。

---

## 4. Asset and Revision Contracts

- **Mesh identity**：`ResourceGUID`（128-bit，Scene 持久化 `ToHex`）；不持久化 GPU handle/path/status。
- **Committed current**：运行时只通过 `RevisionStore::CommittedManifestPath()` + 严格 `ContentRegistry::LoadManifest` 解析；无目录扫描、无 staging、无名称猜测。
- **Digest v2**：B3/B4 导入 digest 与路径命名合同未改；不得与 Mesh GPU payload digest 混用。
- **Mesh GPU identity**：`identity = GUID fold`；`payloadDigest = FNV-1a(实际 GPU 顶点/索引/span/flags)`；内容变化产生新 key。
- **Material slot**：`<stemDir>/<base>.m<slot>.<hash>.material.json` 前缀唯一匹配；与材质名称无关。
- **Texture identity**：asset path identity + encoded content digest + upload semantics（role/colorspace/ruleVersion）；color/data 双用途独立。
- **Same-content reimport**：路径/digest 命中 fast path；GUID 不变、revision 不变、runtime identity/digest 不变。
- **Changed-content reimport**：GUID 稳定、revision 增加、GPU payload digest/纹理路径更新；Scene GUID 无需修改。
- **Publish 原子性**：G3/B4 的 fault matrix 仍通过；失败不替换 current。

---

## 5. GPU Ownership / Cache

- **Mesh GPU**：`MeshGpuResource` 由 `MeshRenderer::m_GpuMeshCache` 独占；VAO/VBO/IBO 生命周期绑定 resource；`Acquire` 命中/回滚/owner-thread/context-current 门控保持。
- **Texture GPU**：`TextureGpuCache` 由 EngineHost 持有；key 含 content digest + upload semantics；binding 以 `shared_ptr` 持纹理。
- **Cache key**：不使用 CPU 对象地址；Mesh 使用 GUID fold + payload；Texture 使用 path/content/semantics。
- **Invalidation**：`RemoveMeshGpuResource` 同时移除 binding 并 invalidate GPU；多 Entity 同 GUID 最后引用移除时才失效。
- **Owner/context**：GPU 创建/失效/清除均在 EngineHost runtime thread + current context；违反返回 fail-closed。
- **Shutdown**：CloseProject/Shutdown 的 `TeardownMeshRuntime` 清 binding/GPU/texture cache；随后 `~MeshGpuCache`/`~MeshRenderer` 兜底。
- **历史例外**：非生产 `m_MeshCache_RHI` 仍为 pointer-key；未在 B5 清理，也不影响生产链路。

---

## 6. Production Pixel Evidence（G5，G6 复验）

G5 文档记录完整数值与断言；G6 重新执行当前二进制确认仍成立：

- OBJ + MTL + external texture（sRGB）→ Direct/RG readback；背景 `(13,13,18)`，interior `(19,2,1)` ±2。
- GLB embedded color/data（同一 PNG source 双用途）→ 两条独立 texture asset；RGBA8/SRGB8_ALPHA8 与 RGB8；readback `(8,8,51)` ±2。
- sRGB 数值：`188,64,32` → linear `0.5029/0.0513/0.0144` → ambient `(19,2,1)`；linear-factor 对照逐 probe ≤2。
- Alpha：`(230,140,40,128)` → alpha 128 原样输出，无额外 gamma；opaque source alpha 255。
- Multi-span：两个 primitive span 均被 whole-range draw 覆盖；首材质策略一致；左右 probe 同首材质颜色。
- Reimport：same-content revision/key 不变；changed-content 新 revision/payload digest/texture path/新像素。
- Failure：Missing/Invalid/Reader/Material/Texture/Decode/GPU upload 均 fail-closed，object probe 保持背景，GPU mesh count 0；恢复后 Ready。
- Cold host restart：GUID/current 重解析 + 重新上传 + Direct/RG 像素复现。

---

## 7. Regression Matrix

### 7.1 构建

- CMake **Debug** 全量：成功（EngineCore/EditorBridge/tests）。
- CMake **Release** 全量：成功。
- `editor_avalonia` **Debug / Release** `dotnet build`：成功（仅既有 NU1900/CS0414 warning）。

### 7.2 专项测试（Debug/Release 均本次二进制）

| Target | 结果 |
|---|---|
| `test_assetimport` | **148 / 148 PASS**（G5 145 + 3 个 G6 3-channel 回归） |
| `test_assetimport_commit` | 20 / 20 PASS |
| `test_content` | 127 PASS + 1 SKIP（既有 shaderc） |
| `test_bridge` | 115 / 115 PASS |
| `test_mesh_pixel` | **9 / 9 PASS**（Direct/RG/OBJ/GLB/sRGB/Linear/Alpha/span/reimport/failure/cold） |
| `test_pick_transport` | 8 PASS / 1 FAIL（PICK-01） |

### 7.3 G6 3-channel PNG 专项（新增 + G5 回归）

- `TextureAssetReaderTest.RgbPngDecodesToRgba8`：3ch 合法；全部 CPU pixel 90/120/200/255；sidecar linear/components3；alpha=255。
- `TextureAssetReaderTest.RejectsInvalidAndUnsupportedChannelLayouts`：garbage PNG fail-closed；gray+alpha（2ch）拒绝。
- `AssetTextureGLTest.LinearRgbDataUsesRgb8InternalFormat`：真实 GL 内部格式 = `GL_RGB8`。
- `AssetTextureGLTest.LinearRgbDataReadbackPreservesChannels`：GL `GetTexImage` 采样 (0,0) = `90/120/200`。
- `TextureGpuCacheGLTest.ColorAndDataUseSeparateCacheEntries`：同 content、不同 upload semantics → 2 entries；`GL_SRGB8_ALPHA8` vs `GL_RGBA8`。
- GLB 测试：同一 embedded PNG 产生 basecolor/normal 两条不同 committed 路径与 sidecar，runtime Ready 且无覆盖。
- 全量 inventory 已包含以上测试（无脱管 target）。

### 7.4 Full Inventory（Debug/Release 一致）

| 指标 | G5 基线 | G6 结果 | 差异 |
|---|---|---|---|
| discovered | 1195 | **1198** | +3（G6 3-channel 回归） |
| executed | 1195 | **1198** | +3 |
| PASS | 1193 | **1196** | +3 |
| FAIL | 1（PICK-01） | **1（PICK-01）** | 0 |
| SKIP | 1（SDK） | **1（SDK）** | 0 |
| native targets | 21 | **21** | 0 |
| ctest entries | 22 | **22** | 0 |

- discovered == executed（两配置）。
- Debug/Release 测试名称集合一致；无未执行测试。
- 唯一 FAIL：`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，仍为
  `tests/test_bridge/test_pick_transport.cpp(231)` / `(233)`。
- 唯一 SKIP：`ShaderCompileRule.SkippedWithoutVulkanOrShaderc`（本机无 Vulkan/shaderc 条件）。

### 7.5 G6 回归裁定

`PASS — BASELINE-PRESERVED — NO NEW REGRESSIONS`

---

## 8. Known Limitations / Scope Exceptions

| 项目 | 状态 |
|---|---|
| PICK-01 | 已知原始失败，保持原样；G6 未修复/未排除 |
| SDK shaderc/Vulkan SKIP | 条件性 SKIP，保持原样 |
| 非生产 `m_MeshCache_RHI` pointer-key | 范围外例外；不得宣称生产 cache 全面清理 |
| GL wrapper 析构无全局 context/thread guard | 既有框架风险；Mesh 生产路径有 owner/context 门控与 shutdown 证据 |
| 多材质完整渲染 | v1 首材质策略；仅注册全部材质 slot |
| Linear data shader consumer | normal 有 shader 消费；occlusion/metallicRoughness/emissive 当前 shader 不消费（NOT SUPPORTED BY CURRENT SHADER CONTRACT） |
| Normal map mipmap renormalization | 未实现，按 B4 既有合同 |
| Vulkan / D3D12 Mesh rendering | 本阶段不支持 |
| 3D picking | deferred（PICK-01 保持） |
| Editor AssetBrowser / Inspector / Add-to-Scene | B6 工作，不属于 B5 |
| Alpha blending | headless 3D 未启用 blend；仅验证 alpha 数据输出 |
| Production camera | 无绝对相机 API；G5 像素依赖默认 EditorCamera |
| 跨进程发布 | local RevisionStore 不自动刷新；同 session 内 reimport 已验证 |
| Cold restart | 同进程 EngineHost teardown/recreation，不是 OS 进程重启 |
| Texture cache eviction | 无 LRU；随 teardown 清空 |

已解决的历史限制：3-channel linear PNG runtime 拒绝问题由 G5 修复并纳入 G6 回归；G5 旧文档保留原始历史表述，本文件记录后续状态。

---

## 9. Workspace and Git State

- Branch `avalonia`；HEAD `5de8f1a`；upstream `origin/avalonia`，ahead/behind **0/0**。
- G5 交付已在 G6 开始前按用户明确要求提交并推送；**G6 未 commit / push / merge / rebase**。
- G6 仅新增回归与文档修改（未提交）：
  - `engine/src/OpenGL/Resources/OpenGLTexture.h`（test-only `GetRendererIDForTesting`）
  - `tests/test_assetimport/B4TextureSrgbGLTest.cpp`
  - `tests/test_assetimport/B5MaterialTextureRuntimeTest.cpp`
  - `docs/P6-1B-GateB5-Runtime-Mesh-Closure.md`
- dirty submodules ×3（d3d12ma/openal-soft/vma）保持。
- `crashes/` 115 项保持未跟踪、未清理。
- 无 reset / clean / stash / 历史改写 / 第三方依赖修改。

---

## 10. Final Decision

**P6-1B B5 — CLOSED — COMPLETE — PRODUCTION PIXEL VERIFIED — SCOPE-BOUND**

- G0～G5 交付完整；G6 集成回归无新增失败。
- B5 Runtime Mesh 生产链路已由真实 FBO 像素证据封存。
- 3-channel PNG 修复已正式回归。
- 后续 Editor 用户工作流（AssetBrowser / Add to Scene / Inspector / UIA）从 B6-G0 只读地图开始，需独立授权。

**STOP.** 不进入 B6-G0 或任何 B6 实现。
