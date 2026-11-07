# P6-1B Gate B4 — Texture Preprocess & sRGB Runtime Record

**Status**: PASS — SCOPE-BOUND（B4-A/B4-B/B4-C/B4-D/B4-E 全部落地；staging 清理继续留 P6-7）  
**Branch**: avalonia  
**Date**: 2026-11-06  
**Precondition**: B3 PASS（跨域未提交工作区累积）

---

## 1. 文件级变更清单（实测）

| 新增/修改 | 文件 | 职责 |
|---|---|---|
| **新增** | `engine/src/AssetImport/TexturePreprocess.h/.cpp` | B4-A 真实 decode/validate/encode；导入侧唯一纹理处理路径（stb_image 无全局 flip 依赖；scoped determinism guard 带恢复） |
| **新增** | `engine/src/AssetImport/（扩展）` | ImportLimits 增 `kMaxTextureDim=16384 / kMaxTexturePixels=67108864`（B3 原先欠此 cap，已记录为缺口补齐） |
| **新增** | `engine/src/AssetImport/ImportDigest.h/.cpp` | **digest schema v2**：`gd-import-digest-v2` 新增 `|tex:<class>:<identity>:<hash16>` 与收尾 `|rule:texture-preprocess-v1:rgba8-png`；v1 保留兼容入口（V1 enum） |
| **新增** | `engine/src/AssetImport/MeshAssetWriter.h/.cpp` | Material JSON textures 增加 `assetPath`（resolved path）字段 |
| **修改** | `engine/src/AssetImport/MeshImportIR.h` | `imageBytes[]`（GLB bufferView 精确字节位段）；`TextureReferenceIR.resolvedAssetPath` |
| **修改** | `engine/src/AssetImport/GltfParser.cpp` | GLB 内嵌图片 bufferView 字段查询与字节提取（含越界 fail-closed） |
| **修改** | `engine/src/Core/Resources/AssetPipeline.cpp` | TexturePreprocessRule no-op 移除：真实 decode→`PreprocessTexture`→ 以与 repo 相同的版本号写入 `outputDir` 输出 PNG + sidecar |
| **修改** | `engine/src/OpenGL/OpenGLGraphicsFactory.cpp` + `Resources/OpenGLTexture.{h,cpp}` | B4-C：sidecar 消费（`<png>.tex.meta.json` 缺省 linear，兼容 legacy）+ sRGB/Linear 内部格式选择（GL_SRGB8_ALPHA8 / GL_SRGB8 vs GL_RGBA8 / GL_RGB8） |
| **修改** | `bridge/src/EditorSession.cpp` | B4-D 集成：textureEntries 收集、digest v2、PNG staging、texture assets 注册、resolvedAssetPath 回填、单一 publish 提交 Mesh+Material+Texture |
| **修改** | `tests/CMakeLists.txt` | test_assetimport + B4 测试；test_assetimport_commit B4 场景 |
| **新增** | `tests/test_assetimport/B4TexturePreprocessTest.cpp`（+GL 套件） | decode/determinism/digest v2/路径/fault |
| **新增** | `tests/test_assetimport/B4TextureSrgbGLTest.cpp` | sRGB / linear / no-meta legacy 三类 GL internal format 真实 upload 验证 |
| **修改** | `tests/test_assetimport_commit/TestImportCommit.cpp` | texture commit 三用例 + 复用同实现纹理生成 helper |

## 2. 输出格式、颜色空间规则与 metadata 语义（B4-A/B4-C 冻结）

- **解码**：仅 stb_image（仓库内子模块）；**data class 3 通道**（normal/occlusion/metallicRoughness/oa），**color class 4 通道**（baseColor/emissive）。
- **编码**：最小 8 位 PNG（RGBA/RGB，8-bit)；无缩放、无有损、无 mipmap、无纹理压缩。**编码器由仓库内自写**（B4-A 独立决策）：zlib stored + CRC32（因仓库没有 stb_image_write，不允许新增第三方）；bytes 确定性可复现（同 source bytes + role → PNG 唯一）。
- **分类映射（冻结）**：glTF `baseColor / emissive / map_Kd` → sRGB；glTF `normal / occlusion / metallicRoughness / map_bump (bump map_Bm)`、MTL `map_* data` → linear。MTL `map_Ka / map_Ka`（未在本仓库 GL 路线支持的 Ks/Ka/illum 语义）继承 B2 的忽略式 warning（不因 B4 扩范围）。
- **metadata sidecar**：`<png>.tex.meta.json` 记录 role/colorspace/width/height/components/ruleVersion/sourceIdentity。管线 stage 与模型导入链同源同一格式。
- **runtime 消费**：`OpenGLGraphicsFactory::CreateTexture` 读 sidecar；`OpenGLTexture` ctor 依据 sidecar 选 `GL_SRGB8_ALPHA8/RGBA8`（4 通道）或 `GL_SRGB8/RGB8`（3 通道）。无 sidecar = legacy Linear（既有路径不改变行为）。**alpha 永远线性**（OpenGL sRGB spec 内建约束）。
- **禁双重转换**：字节流保持源 encoding；不做 CPU gamma；完全依赖 GL 硬件 convert，无 second-pass 转换。
- **保留 alpha**：4 通道仅在 color 角色引入；`hasAlpha` 只在源 image 有 4 通道时报告 true。

## 3. 外部 / 内嵌纹理路径处理（B4-A）

- 路径安全：沿用 B1 URI fail-closed 语义（绝对路径/穿越/percent/data: URI 全拒）；**外部文件用源目录基准**，相对 OBJ/MTL 主文件或 glTF 主文件。
- **不调用 stb 全局 flip**；ScopedStbFlipGuard 带 restore（进入时置 0，离开时值还原到 runtime 默认 1）。UV 单次翻转在 B1 已冻结，不在本 Gate 重做。
- **GLB 内嵌**：bufferView offset+len 字节精确提取，ranges 越界 fail-closed（超出 buffer / bufferView 不存在均经 NBE-spec errors）。
- **mtllib / glTF ext 缓存 / imageBytes**：imageBytes 仅在 GLB 模式进非空（外部 texture URI 保持空数组）。
- 同一 image 同时作 color 与 data 用途 → **两条独立 asset**（per class 独立 path/colorspace），不覆盖同一条 metadata。

## 4. digest schema v2 + migration 语义（B4-D）

```
gd-import-digest-v2;<sourcePathKey>:<srcHash16>
  [|dep:<depPathKey>:<hash16>...]
  [|tex:<color|data>:<identity>:<texHash16>...]
  |rule:texture-preprocess-v1:rgba8-png
```

- 每段与最终 hash 均为 FNV-1a 64（未换 hash 算法，遵 B3 已批准）；schema token 区分版本。
- **identity**：外部 texture 用 normalized lowercase 相对路径；GLB 内嵌用 `glbimg<docImageIndex>`（staging bytes 是文档序列的 canonical identity）。
- 同一 (imageIndex,class) 在同一文件里的 dedup.AnchorStyles：textureEntries 去重。
- **迁移语义（冻结）**：v1 → v2 升级时**同一主文件第一次重导会因 schema token 改变产生一次 migration revision**；GUID 稳定保持（via stem-prefix rebind B3 记录）。后续 unchanged 导入零 revision。这不是静默 hash 意义变化。
- **未覆盖的 forward-compatibility**：今后如 texture-compress 面、mipmap 参数变更，用 schema token + stacked rule-segment 扩展方式升级；把未来能力误报在 v2 是运行诊断错误的反例。

## 5. 单次 Publish 证据（B4-E）

- texture PNG 与 sidecar、material JSON、mesh JSON 同一 `candidate`，**一次 `Publish`** 提交（`textures=M` 事件字段）。
- texture 资产注册早于 material/mesh 注册（Texture→Material→Mesh order join with stable GUID 预订）。
- 失败注入（复用 fault matrix hooks）确认：texture 阶段失败 → rc=-1 且无部分 current。
- fault-matrix 8/8 个点保持（B3）/新增纹理阶段 fault（sidecar 失败）→ 同一 fail-closed。

## 6. 原子性失败注入结果

- B3 的 8/8 RevisionFaultPoint 矩阵回归通过（本 Gate 未改动内部 Publish 语义）。
- 新增纹理路径故障：`texture source unreadable` fail-closed（missing texture），无部分 commit：`CheckoutCurrentRow unchanged`。

## 7. Debug/Release 专项测试与全量回归

| 配置 | test_assetimport | test_assetimport_commit | test_bridge | 全量（test_inventory） |
|---|---|---|---|---|
| Debug | **86/86** | **20/20** | **113/113** | **1114 discovered=1114 executed；1112 PASS / 1 FAIL / 1 SKIP** |
| Release | **86/86** | **20/20** | **113/113** | 同上（一致） |
| 构建 | EngineCore/EditorBridge Debug+Release **0 error** | LNK4006 ×2 为 P5 在案既有项 | — | — |

- 相对 B3 基线 1101 → **+13**（B4 preprocess/digest ×7 + GL ×3 + commit texture ×3）
- **PICK-01 复核**：唯一 FAIL = `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，同一测试与同 231/233 断言行（与 B3/B2/B1 基线逐项一致），0 新失败。
- SKIP 唯一 = ShaderCompileRule 环境条件（同 B3）。
- 未提交、未推送；dirty 子模块 ×3 与 crashes/ 原样保留；测试 fixture 残留通过 dedicated temp dir 并清理；staging 遗留不清理继续留 P6-7。

## 8. 已知限制 / 后续能力登记

| 项 | 说明 | 下游 |
|---|---|---|
| stb_image_write 缺库 | PNG encode 使用仓库内自写 zlib-stored 最小实现（未引入第三方） | 后续若需要更密集 PNG 输出，可独立请求评估 |
| mipmap normal 通道 renormalize并不多 | GL mipmap 生成为 GL 固有行为；normal 通道归一化不预计算 | B5 像素验证时可记录 normal 采样差异 |
| digest v2 不含 KHR extension 语义 | 未支持的 glTF extension 依赖不参与 hash（与 B1 已记 NONSUPPORTED fail-closed 一致） | 后续 Gate 在 EX 允许时按需扩展 |
| TexturePreprocessRule 路径仅工程管线 context | 模型导入链通过 `PreprocessTexture` 同函数，而 AssetPreprocessRule 走 pipeline context，不驱动 revision | （为何：本 Gate 仅将 no-op 移除） |
| staging 清理 | B3 语义保持：不自动清理 | P6-7 |
EOF
