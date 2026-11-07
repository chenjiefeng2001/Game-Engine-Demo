# P6-1B Gate B5-G5 — Production Render End-to-End Pixel Proof Closure

**Status**: PASS — PRODUCTION PIXEL VERIFIED — SCOPE-BOUND
**Branch**: avalonia
**HEAD**: `124cdd0`（实测；未提交、未推送；远端 0/0）
**Date**: 2026-11-07
**Precondition**: B5-G4 PASS — ACCEPTED（G4 基线 1184）

---

## 1. G5-A Preflight 结果

| 事实 | 证据 |
|---|---|
| 模型导入入口 | `EditorSession_ImportAsset(path, 2)` → `ImportModelAsset`；OBJ/GLTF/GLB 唯一入口（`EditorSession.cpp:859-879`, `:1099+`） |
| 提交 | 一次 import 内 Mesh/Material/Texture staging + 单个 `RevisionStore::Publish`；同内容 fast path 不发布新 revision |
| GUID 获取 | 返回值为 session asset index；`EditorSession_GetAssetGuid` 得 32-hex mesh GUID；Material/Texture 通过枚举 `.material.json` / `.tex.png` 后缀获得 |
| Scene 绑定 | `EditorSession::GetEditScene()`（bridge-internal C++）+ native `MeshComponent::SetMeshGuid` + `EditorSession_SaveProject` |
| Runtime | `EngineHost` 独占 `EditorSession`/`Application`/persistent GL context；`PumpOneFrame` 内 runtime thread + context current；`SyncMeshRuntime` 在 production render 前执行 |
| Renderer | `Application::m_Production3DMeshRenderer` 单实例；Direct 与 RenderGraph 共用；RG 选择 = flag + presentation FBO id + device/queue |
| FBO | presentation FBO 800×600，color `GL_RGBA8` + D24S8 + R32I；clear `(0.05,0.05,0.07)` → RGBA bytes (13,13,18,255) |
| Readback | `EngineHost::ReadPresentationPixels` → GL `GL_RGBA/GL_UNSIGNED_BYTE`，bottom-left origin，RGBA 顺序 |
| 色彩编码 | presentation FBO 非 sRGB；全仓库未启用 `GL_FRAMEBUFFER_SRGB`；`3d_lit.frag` 不做 gamma encode → readback 为 shader 线性输出量化值 |
| 相机 | 默认 `EditorCamera`：focus(0,0,0)、distance 10、yaw 45°、pitch −30°、FOV 60°、near .1/far 1000；无绝对相机控制 API |
| DebugDraw | 生产路径恒绘制原点三轴（无关闭开关）；本 Gate probe 避开中心轴向区域 |
| 发现的真实缺陷 | `TextureAssetReader` 误把 stb `channels_in_file==4` 当硬条件，导致 B4 3-channel linear data PNG（normal/occlusion/metallicRoughness）在 runtime 被拒绝。G5 修复（见 §13） |

停止条件检查：生产 Renderer 可在不改公共 C ABI、不绕过 `MeshRuntimeScene` 的情况下输出到 presentation FBO 并 readback；未触发 BLOCKED。

---

## 2. 测试 Harness 与生产调用链

新增 `tests/test_mesh_pixel/`：

- `PixelHarness.h`：确定性 PNG/OBJ/MTL/GLB 生成、临时 ProjectRoot、独立 `EditorSession` import/save、`PixelHost`（EngineHost create/open/pump/readback/query）、probe 与像素断言工具。
- `B5ProductionPixelTest.cpp`：9 个端到端测试。

实际链路（每个测试均相同）：

`OBJ/GLB source` → `EditorSession_ImportAsset(type=2)` → staged Mesh/Material/Texture + texture sidecar → `RevisionStore::Publish` → `EditorSession_SaveProject`（Scene 内 MeshComponent.meshGuid）→ 关闭 standalone session → `EngineHost_OpenProjectRooted` → `PumpOneFrame` → `SyncMeshRuntime` → committed-only `MeshAssetResolver` → `MeshRuntimeLoader` → G1 Reader → G2 Adapter/Cache → G3 Material/Texture binding → production `MeshRenderer`（Direct / RenderGraph）→ presentation FBO → `ReadPresentationPixels` → probes。

未手工构造 GPU mesh、未直接 `MeshGpuCache`/VAO 绕过 Runtime、未读取测试临时 Mesh 作为输入。

固定 harness 参数：

- viewport 800×600（EngineHost presentation 默认）。
- 背景 clear `(13,13,18,255)`；背景 probe：`(5,5) (794,5) (5,594) (794,594)`（GL bottom-left）。
- 相机 facing quad 位于原点平面，法线朝向相机；默认相机下 N·L < 0 → 只受 ambient `(0.15,0.15,0.20)`，spec ≈ 0，输出可数值预测。
- 单 quad probe：`u,v = ±3.2` → `(234,134) (566,134) (234,466) (566,466)`。
- 多 span probe：左右 quad 中心 `u=±3, v=0` → `(244,300) (556,300)`。

---

## 3. Fixture A — OBJ + MTL + External Texture（END-TO-END VERIFIED）

资产：

- camera-facing quad（half=4），4 顶点、2 三角形、显式 normals + UV。
- MTL 材質 `MatRed` + `map_Kd pixel_quad.png`（4×4 RGBA，encoded `(188,64,32,255)`）。
- import 后 committed sidecar：`role=basecolor, colorspace=sRGB, components=4`；Material JSON `baseColorTexture.assetPath` 指向 committed texture。
- mesh GUID 来自 import 返回值；Scene 保存该 GUID。

结果（Debug/Release）：

- Runtime status = Ready；Direct frame 中 quad 覆盖 >120k 且 <260k 像素；背景 probe 精确保持 clear；4 个 interior probe 均匹配 `(19,2,1,255)` ±2。
- RenderGraph frame：`Application::LastProductionUsedRenderGraph() == true`；同 probes/背景匹配 ±2；Direct 与 RG probe 差 ≤2。
- FBO 目标确认：readback 来自 presentation FBO（EngineHost 断言 draw target == presentation FBO；readback 显式绑定该 FBO）。
- 覆盖/背景联合排除：clear-only、旧帧残留、FBO 未绑定、index 丢失、winding 消失均会使 probe/coverage 断言失败。

证据等级：**END-TO-END VERIFIED**。

---

## 4. Fixture B — GLB + Embedded Images（END-TO-END VERIFIED）

GLB：

- 一个 buffer/BIN chunk；POSITION/NORMAL/TEXCOORD_0 float accessors + uint16 indices；node/scene 完整。
- 两个 embedded image（`bufferView` + `mimeType image/png`），**同一 PNG 字节**用于 baseColor 与 normal。
- material：`baseColorTexture` + `normalTexture`（linear data），`baseColorFactor=[1,1,1,1]`，OPAQUE。

import 结果：

- Mesh GUID 成功；Material 1 个；Texture **2 个独立 committed asset**：
  - `img0.basecolor.*.tex.png`：sRGB，components=4；
  - `img0.normal.*.tex.png`：linear，components=3；
- 两条路径不同，证明同一 source image 的 color/data 两种用途未被合并、未互相覆盖 metadata。
- Runtime status Ready；Direct/RG 均产生 quad 覆盖；interior probes 匹配 `(8,8,51,255)` ±2；背景 probe 保持 clear；RG flag true。

证据等级：**END-TO-END VERIFIED**（GLB、embedded B4 image extraction、color/data 分类、runtime binding）。

---

## 5. sRGB BaseColor 数值分析（FBO PIXEL VERIFIED）

测试：`SrgbDecodeMatchesLinearFactorMaterial`

- Texture encoded byte：`188,64,32,255`。
- sRGB→linear：
  - 188/255 = 0.7373 → **0.5029**
  - 64/255 = 0.2510 → **0.0513**
  - 32/255 = 0.1255 → **0.0144**
- Shader：camera-facing quad 下仅 ambient：
  - R = 0.15 × 0.5029 = 0.0754 → byte 19
  - G = 0.15 × 0.0513 = 0.0077 → byte 2
  - B = 0.20 × 0.0144 = 0.0029 → byte 1
- FBO：`GL_RGBA8`，`GL_FRAMEBUFFER_SRGB` 关闭，shader 无 gamma encode → readback 期望 **(19,2,1,255)**，tolerance ±2。
- 对照 fixture：无纹理材质 `Kd` 直接写上述 linear 值；其 readback 与 sRGB texture 路径逐 probe 差 ≤2。若上传按 raw linear 解释，tensor 值 0.737/0.251/0.125 会给出约 **(28,10,6)**，显著超出 tolerance → 该对照排除“仅检查 sidecar metadata”的伪证明。
- 同时验证：committed texture path、sidecar sRGB、`TextureGpuCache` 上传使用 B4 的 `GL_SRGB8_ALPHA8`（B4/G3 GL 测试）以及 production shader 实际采样。

证据等级：**GL UPLOAD VERIFIED + FBO PIXEL VERIFIED**。

---

## 6. Linear Data Texture（GL UPLOAD VERIFIED; shader consumer 限制）

- GLB normal texture：sidecar `role=normal, colorspace=linear, components=3`；与 basecolor 资产路径独立；runtime binding 成功（Ready 且画面正常）。
- 新增 B4 GL 测试 `LinearRgbDataUsesRgb8InternalFormat`：3-channel linear PNG → `GL_RGB8`（B4 冻结合同：4ch → GL_RGBA8，3ch → GL_RGB8；授权文字中的 “GL_RGBA8” 仅适用于 4ch data；本 Gate 以 B4 合同为准并记录）。
- 新增 `TextureAssetReaderTest.RgbPngDecodesToRgba8`：3ch PNG → reader 输出 RGBA8（alpha=255）。
- Normal shader 确实消费 `u_NormalMap`；本 fixture 使用 flat normal `(128,128,255)`，重建 normal 近似原 normal，像素输出与无 normal 的 ambient 期望一致。**不宣称 normal map 产生了可区分的视觉变化**。
- Occlusion / metallicRoughness / emissive textures：import/reader/upload/binding 存在，但当前 production shader **不消费** → 标记 **NOT SUPPORTED BY CURRENT SHADER CONTRACT**（未增加 PBR/material 系统）。
- Alpha：sRGB 纹理的 alpha 按 GL 规范保持线性；RGBA8 readback 中 128 保持 128，无额外 gamma。

---

## 7. Alpha（FBO PIXEL VERIFIED）

- Fixture：encoded `(230,140,40,128)`；期望 ambient `(30,10,1,128)` ±2；opaque version `(230,140,40)` → alpha 255。
- 结果：alpha=128 与 255 均按 shader `FragColor.a = base.a` 原样输出；无 CPU gamma；背景 alpha 255。
- Headless production 3D path **未启用 blend**（与现有 renderer 合同一致）；本测试验证的是 alpha 数据保留/输出，不是 alpha 合成/排序。
- 若未来启用 blend，需另开合同；本 Gate 未改变。

---

## 8. Multi-span 与首材质策略（FBO PIXEL VERIFIED）

Fixture：OBJ 两个 camera-facing quad（左右分离），两个 `usemtl`：

- committed mesh JSON：`primitives[0] = {firstIndex:0,indexCount:6,materialIndex:0}`；`primitives[1] = {firstIndex:6,indexCount:6,materialIndex:1}`；`materialNames` 2 项。
- Session registry：2 个 Material asset；第一个 textured red `(230,20,20)`。
- 渲染结果：左右两个 probe 都匹配首材质 ambient 期望 `(30,0,0,255)` ±2；覆盖 ~49k（断言 30k–100k）；背景 probe clear；Direct/RG 一致。
- 这同时证明 whole-index-range draw 覆盖两个 span，未丢失后续索引/跨 span 读取；v1 首材质策略不变（第二材质未扩大为完整多材质渲染）。
- 多材质 warning 在 G3/G4 单元测试已覆盖；本 Gate 未改策略。

---

## 9. Reimport / Cache（FBO PIXEL VERIFIED）

测试：`ReimportKeepsGuidAndUpdatesPixels`

1. 初始 import + SaveProject：revision R1；记录 mesh GUID、texture path、runtime `identity/digest`、probes、coverage。
2. 同内容重复 import（相同 OBJ/PNG）：
   - GUID 保持；
   - **revision 不变**（fast path 未 Publish）；
   - 新 host 中 runtime `identity` 与 `payloadDigest` 均与 R1 相同；
   - probes 差 ≤1、coverage 完全相等（像素稳定）。
3. 修改几何（half 4→4.2）与 PNG 颜色（→`(40,200,80)`）后 import：
   - GUID 保持；
   - revision 增加；
   - runtime `identity` 不变、`payloadDigest` 变化（geometry/payload 变化）；
   - committed texture path 变化（新 asset；旧 asset 继续保留，符合 B3/B4 不可覆盖/不清理合同）；
   - Direct/RG probes 匹配新颜色 ambient `(1,22,4,255)` ±2；背景 clear。
4. 每个阶段都是 host teardown/recreate 后 readback，包含 cold runtime restart 路径。

---

## 10. Failure（FBO PIXEL VERIFIED — no fake image）

| 场景 | Runtime status | 像素结果 |
|---|---|---|
| GUID 不存在 | MissingAsset | object probe 仍为 clear；非背景仅 Debug axes（<20k）；背景 probe clear |
| committed Mesh bytes 非法 | ReaderFailure | 同上 |
| Material asset 从 committed manifest 移除 | MaterialBindingFailure | 同上 |
| Texture asset 从 committed manifest 移除 | TextureBindingFailure | 同上 |
| committed Texture bytes 损坏（decode fail） | TextureBindingFailure | 同上 |
| GPU upload 注入失败（AfterVertexArray） | GPUUploadFailure | 同上 |

- GPU failure 后重置注入并重建 host：status → Ready，quad 正常显示预期颜色。
- 失败场景均未出现“旧 revision/随机资产”伪画面；未绘制 partial mesh/binding。生产 renderer 的 `GpuMeshCountForTesting()==0`（失败不发布 GPU resource）；GPU 失败恢复后为 1。
- 未观察到 host teardown/recreate 失败或后续测试被污染。

---

## 11. Cold Runtime Restart / Saved Scene

- 所有 pixel 测试均为：standalone `EditorSession` import + `SaveProject` → 销毁 session → `EngineHost` 创建/打开 → render → 关闭 host。
- Reimport 测试在 host 完全 teardown 后重建，再重新解析 committed current、重新上传 GPU、重新绑定 Material/Texture，并从新 host 读回像素；Direct 与 RenderGraph 都验证。
- Scene 文件只含 `MeshComponent.meshGuid`（无 GPU handle）；cold load 由 `SceneSerializerV1` 恢复 GUID，由 Runtime resolver 重新取 current。
- 限制：不是**操作系统进程重启**，而是同进程内完整 `EngineHost` teardown/recreation；本 Gate 明确记录该边界（授权允许）。

---

## 12. Direct / RenderGraph 双路径

- 同一 host、同一 session、同一 Scene、同一 GUID、同一 committed assets、同一 `MeshRuntimeScene` 条目、同一 `MeshGpuCache`/`TextureGpuCache`/binding。
- Direct：`Application::RenderProduction3DFrame` Direct branch；RG：flag + presentation FBO id 分支。
- 证据：
  - RG 测试断言 `LastProductionUsedRenderGraph() == true`（不是 fallback）；
  - RG readback 与 Direct 在 interior probes 差 ≤2、背景相同、覆盖区域同量级；
  - RenderGraph Execute 后 FBO 重新绑定到 presentation id（既有 SG6 修复保持）；readback 明确绑定 presentation FBO，不读取 default framebuffer；
  - 未出现 RG 绘到旧 FBO 或 clear-only。

---

## 13. 本 Gate 暴露并修复的生产缺陷

**缺陷**：`TextureAssetReader::ReadTextureAsset` 在 stb 解码后校验 `dc == 4`（`dc = channels_in_file`）。3-channel PNG（B4 normal/occlusion/metallicRoughness 的标准输出）在 `req_comp=4` 解码时 `channels_in_file` 仍为 3，因而被误判 `decoded layout mismatch`，runtime `TextureBindingFailure`；G3 测试未覆盖 3ch PNG。

**修复**：只校验 `dw/dh` 与请求一致；`req_comp=4` 已保证输出 RGBA8。无资产格式/digest/B4 合同变化。

**回归**：
- `TextureAssetReaderTest.RgbPngDecodesToRgba8`（reader 层）；
- `AssetTextureGLTest.LinearRgbDataUsesRgb8InternalFormat`（GL 上传层）；
- G5 GLB embedded normal + pixel suite（runtime 端到端）。

该修复是 G5 允许的“有真实证据、最小、不改合同”的生产修复。

---

## 14. 生产文件变更清单（G5）

**新增**

- `tests/test_mesh_pixel/PixelHarness.h`
- `tests/test_mesh_pixel/B5ProductionPixelTest.cpp`
- `docs/P6-1B-GateB5-G5-Production-Viewport-Pixel-Closure.md`

**修改**

- `engine/src/AssetImport/TextureAssetReader.cpp`（3ch 解码修复）
- `engine/src/AssetImport/MaterialRuntimeBinding.cpp`（failure stage 诊断包含 stage/原因；不影响成功路径/合同）
- `engine/include/Engine/Application.h` / `engine/src/Core/Application.cpp`（`LastProductionUsedRenderGraph` 观测，additive）
- `bridge/src/EngineHost.h/.cpp`（C++-only G5 test seams：RG 观测、GPU failure 注入、runtime error/GpuMeshCount 查询；未改 C ABI/capi.h/op/struct）
- `tests/CMakeLists.txt`（`test_mesh_pixel` target + CWD/timeout）
- `tests/test_assetimport/B4TextureSrgbGLTest.cpp`（RGB PNG helper + GL_RGB8 回归）
- `tests/test_assetimport/B5MaterialTextureRuntimeTest.cpp`（RGB PNG helper + reader 回归）

**未改**：B3/B4 writer/schema、B4 digest v2、RevisionStore Publish 合同、Bridge C ABI、Vertex3D/Shader ABI、B0 多材质策略、Direct/RG 架构、G2 `m_MeshCache_RHI`。未 commit/push；dirty submodules ×3 与 `crashes/`（115）保留。

---

## 15. 构建与回归

- CMake Debug 全量：成功。
- CMake Release 全量：成功。
- `EditorBridge` Debug/Release：成功。
- `editor_avalonia` Debug/Release `dotnet build`：成功（仅既有 warning）。
- `test_mesh_pixel`：**9/9 PASS**（Debug 与 Release）。
- `test_assetimport`：**145/145 PASS**（G4 143 + 2 个 RGB/sRGB regression）。
- `test_bridge`：115/115；`test_content`：127 PASS + 1 SKIP（既有 shaderc）；`test_assetimport_commit`：20/20。
- G1 Reader / G2 GPU cache / G3 Material-Texture / G4 MeshComponent-resolver 专项均随上述 target 全通过。

全量 inventory（Debug/Release 一致）：

| 指标 | G4 基线 | G5 结果 | 差异 |
|---|---|---|---|
| discovered | 1184 | **1195** | +11（9 pixel + 2 assetimport regression） |
| executed | 1184 | **1195** | +11 |
| PASS | 1182 | **1193** | +11 |
| FAIL | 1（PICK-01） | **1（PICK-01）** | 0 |
| SKIP | 1（SDK 条件项） | **1（SDK 条件项）** | 0 |
| native targets | 20 | **21** | +1（test_mesh_pixel） |
| ctest entries | 21 | **22** | +1 |

唯一 FAIL：`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，断言仍为 `tests/test_bridge/test_pick_transport.cpp(231)` / `(233)`，零漂移。唯一 SKIP：`ShaderCompileRule.SkippedWithoutVulkanOrShaderc`。所有验证使用本次构建二进制。

---

## 16. 证据等级矩阵

| 项目 | CODE | GL UPLOAD | DRAW PATH | FBO PIXEL | END-TO-END |
|---|---|---|---|---|---|
| OBJ+MTL+texture | VERIFIED | VERIFIED (sRGB8_ALPHA8) | Direct+RG executed | VERIFIED | VERIFIED |
| GLB embedded color+data | VERIFIED | VERIFIED (sRGB8_ALPHA8 / RGB8) | Direct+RG executed | VERIFIED | VERIFIED |
| sRGB numeric | VERIFIED | VERIFIED | executed | VERIFIED (±2 + linear-factor control) | VERIFIED |
| Linear normal data | VERIFIED | VERIFIED (GL_RGB8) | executed | normal flat ≈neutral | partial visual differentiation; upload/binding VERIFIED |
| Occlusion/metallic/emissive | parsed/bound | reader/binding路径 | shader 不消费 | — | **NOT SUPPORTED BY CURRENT SHADER CONTRACT** |
| Alpha | VERIFIED | VERIFIED | executed | VERIFIED (128/255) | alpha output VERIFIED; blending NOT IN SCOPE |
| Multi-span/first material | VERIFIED | VERIFIED | executed | VERIFIED | VERIFIED |
| Reimport same/changed | VERIFIED | VERIFIED | Direct+RG | VERIFIED | VERIFIED |
| Missing/invalid/GPU failure | VERIFIED | — | no fake draw | background VERIFIED | VERIFIED (failure按合同) |
| Cold host restart | VERIFIED | VERIFIED | Direct+RG | VERIFIED | VERIFIED (same-process teardown/recreate) |

---

## 17. 已知限制 / 未验证项

- **不是 OS 进程重启**：cold restart 是 EngineHost teardown/recreation（授权允许），非跨进程。
- **相机**：无绝对相机 API；像素几何基于默认 EditorCamera（focus origin/distance10/yaw45/pitch−30/FOV60），测试依赖该默认值；若默认相机改变，probe 坐标/期望需同步。
- **Debug axes**：production 3D 恒绘制原点三轴；probe 刻意避开中心；未增加关闭开关。
- **Occlusion/metallicRoughness/emissive**：无 shader consumer；未增加 PBR；pixel proof 明确不宣称。
- **Normal**：consume 路径存在，但 flat neutral fixture 不产生可区分视觉差异；仅证明采样/绑定不破坏输出。
- **Alpha blending**：headless 3D 未启用 blend；验证的是 alpha 通道保留/输出，不是合成。
- **GPU failure 重试**：同一 revision 内失败按 G4 合同缓存；恢复需新 revision 或重新打开 host（本测试用 host 重建）。
- **Texture cache eviction**：无 LRU；仅随 teardown 清理。
- **FBO encoding**：`GL_RGBA8` 线性；若未来启用 `GL_FRAMEBUFFER_SRGB` 或改 shader gamma，期望值/tolerance 必须重算。
- **层级**：production 3D 只渲染 root objects；G5 fixtures 均为 root。
- **B4 授权的 GL_RGBA8 表述**：3-channel data 实际按 B4 冻结合同为 `GL_RGB8`；本 Gate 记录合同优先，未改实现。

---

## 18. 工作区保护

- branch `avalonia`，HEAD `124cdd0`，远端 0/0。
- 未执行 reset/clean/stash/commit/push/merge/rebase。
- dirty submodules ×3（d3d12ma/openal-soft/vma）未触碰。
- `crashes/` 115 项未触碰。
- G1～G4 全部未提交产物保留。

---

## 19. 停止

G5 交付完成，停止。不进入 B5-G6、B6-G0、AssetBrowser/Inspector/Managed UI 工作；等待下一道授权。
