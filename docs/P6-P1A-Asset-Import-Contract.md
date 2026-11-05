# P6-P1A — Static Mesh Import Contract (FROZEN)

**Status**: FROZEN — 契约冻结；实现（P6-1B）在本合同批准后启动  
**Branch**: avalonia  
**Date**: 2026-11-05  
**Precondition**: P6-0 完成（`docs/P6-0-Baseline-Stabilization-Record.md`）；P5 交付远端同步 0/0  
**Scope rule**: 本合同冻结 glTF/GLB/OBJ/MTL **静态网格导入**；Skinning/Animation/Draco/Meshopt 明确非目标（§10）。

---

## 0. 现状调查结论（P6-1A 调查，逐条已verify）

对既有代码的现实检验（合同必须建立在事实上，非假设）：

| # | 事实 | 锚点 |
|---|---|---|
| F1 | **导入=注册+提交；运行时解析为懒加载**。`EditorSession::ImportAssetInternal`（bridge/src/EditorSession.cpp:769-994）做路径校验→外部源复制到 `assets/imported/{tex\|scripts}/`→幂等冲突检查→`candidate.Import(target,type)`→`m_Revisions.Publish(m_Root, manifestJson, sceneBytes)`；不存在 PNG→GPU 的导入期转换（纹理在 `SceneSerializerV1` 绑定 sprite 时经 `TextureManager::Load`→`OpenGLTexture`（stb）懒解码） | EditorSession.cpp:751,769-994,1870-1895 |
| F2 | **`Content::AssetType` 只有 Texture=0 与 Script=1**；`EditorSession::ImportAsset(type)` 对 0/1 之外的值 fail-closed。mesh 类型必须**三阶梯同步**扩展：`Content::AssetType`（ContentAsset.h:33）、ImportAssetInternal 的 int 映射（:782-786）、`ResourceType`（Mesh=5 已存在） | ContentAsset.h; EditorSession.cpp:782-786 |
| F3 | **AssetPipeline 与 RevisionStore 互不感知**。规则（Validation/Preprocess/Compile/Optimize/Package 五个 PipelineStage 枚举排序；`AssetRule` 基类 GetStage/Execute/NeedsRebuild）直接 `FileSystem::WriteFile`；`TexturePreprocessRule` 为 no-op 占位（push `*.rgba8.png` 但不写文件 — AssetPipeline.cpp:121-147） | AssetPipeline.h:69-144; AssetPipeline.cpp |
| F4 | **`RevisionStore::Publish(manifestJson, sceneBytes)` 是唯一提交入口**：write.lock → 双基线校验 → 候选目录（失败留洞不重用编号）→ 双文件.atomic 写+严格校验 → 原子替换 `current` + 读回确认；失败旧 `current` 保持权威。哈希 = FNV-1a 64 (`HashBytes`) | RevisionStore.h:71-88; RevisionStore.cpp:37-547 |
| F5 | 运行时 Mesh 现状：`Vertex3D {Vec3 position; Vec3 normal; Vec2 texCoord; Vec3 tangent}`（44B）、`uint32` 索引、**无 submesh**；GL 属性布局 loc0-3 已含 tangent；`Mesh::RecalculateNormals()` 会生成正交切线（含退化 UV 回退）；`MeshRendererComponent::Mesh` 是 GPU 侧（VAO+IndexCount）与 CPU 侧 `Mesh` 分离 | Mesher.h:17-22; MeshRenderer.cpp:46-88 |
| F6 | 纹理通道路径：stb `channels=0` as-is 解码 → 3 通道 GL_RGB8、4 通道 GL_RGBA8，**全线性；无 GL_SRGB8_ALPHA8**；`GenerateTextureMipmap` 上传后生成（不可变存储只分配 1 mip——已知 quirk）；加载前 `stbi_set_flip_vertically_on_load(true)` | OpenGLTexture.cpp:27-105 |
| F7 | 坐标系：引擎**右-handed、+Y up、米制**（EditorCamera lookAt up=(0,1,0)；Jolt 重力 -Y 透传无 up 重映射；CreatePlane 法线 +Y）；**front face=CCW**（CreateCube/CreatePlane 顶点序与索引 cross 积验证；未显式设 glFrontFace → GL 默认 CCW）；矩阵**列主序**（`data[col*4+row]`），`mvp=proj*view*model` | EditorCamera.cpp:210; Mesh.cpp:121-228; MathTypes.h:164-194; JoltPhysicsWorld.cpp |
| F8 | 场景序列化 v1 仅 `{name, position:[x,y,z]}`（无 rot/scale）——TRS 契约扩展是 **P6-2 EDITOR-01** 的独立任务，本合同不触碰场景 schema | SceneSerializerV1.cpp:29-51 |
| F9 | 可复用解析基础：`nlohmann/json.hpp`（third_party，已在 RevisionStore/ContentAsset/场景/组件序列化全链使用）；二进制读 `FileStream`（可 seek/GetSize）、`FileSystem::ReadFile(std::vector<uint8>)`、`Content::AtomicFile`/`WriteFileAtomic`；stb_image 已用于引擎纹理。**仓库中无 assimp/tinygltf/cgltf/tinyobj 任何解码器** | RevisionStore.cpp:13; AtomicFile.cpp:53 |
| F10 | glTF/GLB/OBJ 已prenomials出现在扩展映射（`AssetTypeFromExtension`→Model；`AssetMetaDb` 扫描集含 .obj/.fbx/.gltf/.glb）但**无语义背后**（type 判断仅"简化"registered 到 Texture stub） | AssetTypes.h:112-114; AssetMetaDb.cpp:22-180 |

---

## 1. 范围（Scope）

**在范围（v1 静态导入）**：

| 格式 | 内容 |
|---|---|
| `.gltf` / `.glb` | glTF 2.0 静态模型：几何、PBR metallic-roughness 材质、纹理、节点烘培 |
| `.obj` | 静态几何（v/vt/vn/f/o/g/usemtl/mtllib、三角化、顶点去重） |
| `.mtl` | 基础材质（Kd/Ks/Tf/tr/d/illum/map_*） |
| PNG/JPEG | 纹理输入端（POST-preprocess 后登记） |

**明确支持面（glTF 必收）**：positions/normals/UV0/indices/primitive(submesh 语义)/多 mesh 多 primitive/节点层次与变换/法线贴图/遮挡贴图/emissive/alphaMode/metallicFactor/roughnessFactor/baseColorFactor+Texture。

**明确不支持、必须显式报告（不得静默成功）**：Draco/Meshopt 压缩、Skinning（skin/joint/weights）、Animation 输入、KTX2、自定义 extensions、sparse accessors（v1 拒绝）、多 UV 集（UV1+）、vertex color 非必需支持（记录为丢弃警告）。

**不在范围**：见 §10。

## 2. 数据契约（Canonical Import IR）

中间表示 C++ 结构（新增于 engine 侧、仅导入进程域、不进运行时）：

```
ImportMeshData
  vertices[] : { Vec3 position; Vec3 normal; Vec2 uv0; Vec3 tangent; bool tangentComputed; }
  indices[]  : uint32
  primitives[] : { firstIndex, indexCount, materialIndex }
ImportMaterialData
  name; baseColorFactor[4]; metallicFactor; roughnessFactor;
  alphaMode = OPAQUE|MASK|BLEND; alphaCutoff;
  textures[] : { role = BaseColor|Normal|Occlusion|Emissive|MetallicRoughness, sourceIndex }
ImportTextureRef
  sourceIndex; semantic; declaredColorSpace = sRGB|Linear|Auto
ImportNodeTransform        // 仅烘焙输入；导出后不保留层级
  localMatrix[16] (column-major, engine convention)
ImportSceneReport          // 必须生成的机器可读审核面
  warnings[](非支持项、丢弃项、烘焙跳过节点), error, ok
```

**资产粒度（冻结决策）**：v1 一个 **glTF primitive / OBJ mesh 组 = 一个 Mesh asset**；
material/text 独立 asset。理由：`MeshRenderer` 单材质绑定 + Mesh 无 submesh（F5）。
运行时 submesh 支持为后续独立项（Gap: ASSET-04，新登记）。
节点变换在导入期**烘焙进顶点**（见 §5）；层级不被保留，被跳过的非几何节点记入 warnings。

**运行时转换目标**：IR → `Mesh`（现有 Vertex3D 直接兼容 F5）→ 经 `ResourceManager`
新 `MeshLoadData` + `LoadByType<Mesh>` 分支（additive；沿用 async 状态机；
`Mesh : Resource` 注册 GUID 与 EditorAssetDatabase 的 GUID 语义一致）。

## 3. 解析器契约

| 主题 | 契约 |
|---|---|
| 依赖策略 | **零新第三方**：glTF JSON 用 nlohmann；GLB 容器手写（12B header + chunk 校验）；OBJ/MTL 手写行解析；纹理解码用已有 stb_image。禁 assimp/tinygltf/cgltf/tinyobj 新增 |
| glTF 几何 | accessor 逐字段校验（type/componentType/count/byteStride/bufferView 越界全拒）；indices 必须 uint8/16/32 全支持；INTERLEAVED bufferView v1 拒绝并报告；normal 缺失→可由 RecalculateNormals 生成并标记 `tangentComputed`；tangent 缺失→生成（与 F5 算法一致）或标 warning+`tangentComputed` |
| glTF 材质 | PBR metallic-roughness 全支持；KHR_materials_* 扩展：未注册扩展名必须出现在 ImportSceneReport.warnings（**静默忽略扩展 = 违约**）；未支持材质模型（specular/clearcoat/iridescence/volume…）→ 拒绝该 primitive 或降级（记 warning）为金属-粗糙近似值 |
| GLB 容器 | header magic/version/length 校验；JSON chunk 必须首、BIN 必须第二；chunk 对齐（4B）与长度越界全拒；GLB 内嵌 buffer/bufferView 校验同 glTF 文件路径 |
| glTF URI | 仅同目录相对 URI；拒绝：绝对路径、`..`、`file:`、`http(s):`、`data:`（v1 拒绝 data: URI 并报告——后续单独授权再放行）、任何脱离导入根的解析 |
| OBJ/MTL | v/vt/vn/f/o/g/usemtl/mtllib；`f` 支持每顶点三段与混合省略；多边形一律三角化（fan）并计入 warning 注明；负索引合法处理；材质字典键大小写不敏感；未知 MTL 键 → warning 列表；Kd/Ks/Ke/Tf/tr/d 归一化 |
| 统一失败语义 | 解析失败返回结构化 error（文件级+行/accessor 级诊断），**不产生部分成功资产、不注册 GUID、不发 revision** |

## 4. 纹理预处理契约（替换 no-op）

`TexturePreprocessRule` 从占位符改写为真实实现，且成为 Mesh/材质纹理链的**必需阶段**：

1. 解码 PNG/JPEG（stb，兼容现有 8-bit 路径）。
2. 校验维度、字节数、拒零尺寸/超大图（上限 16384px，可配置）。
3. 颜色空间分类（写入 meta 的 `colorspace` 字段）：
   - **sRGB**：BaseColor、Emissive；
   - **Linear**：Normal、MetallicRoughness、Occlusion、自定义遮罩。
   - `declaredColorSpace == Auto` 时按语义路由；冲突（如 BaseColor 被声 Linear）→ warning + 按语义路由胜出。
4. 输出标准化资产：8-bit、按输入通道规整 RGBA/RGB（Alpha 支持保留），输出文件落在 pipeline `outputDir`，**真的写文件**并 push `ctx.generatedFiles`。
5. Mipmap：v1 输出仅 level0 数据；**mip 生成仍在 GL 上传层**（F6）。Normal map 的 mip 归一化重构记录为已知限制（Gap: ASSET-05；若在此前未接，不得声称"normal mip 已处理"）。
6. 不支持格式（KTX2/DDS 压缩/16-bit PNG 等）→ `PipelineResult { success=false, errorMessage="UNSUPPORTED ..." }`，**禁伪成功**。
7. 稳定资产 ID 与 Hash：物理内容 hash 复用 `RevisionStore::HashBytes`（FNV-1a 64）写入 meta；meta 内 `md5Hash` 字段照旧由 EditorAssetDatabase 填。
8. **运行时 sRGB 接入（additive，v1 必做）**：`OpenGLTexture` 上传根据 meta `colorspace` 选择 `GL_SRGB8_ALPHA8 / GL_SRGB8`（sRGB 类）或维持 GL_RGBA8/GL_RGB8（linear）；采样端在 shader 不做 pow，交给硬件 sRGB 采样。**无此步，sRGB 标记无意义**。

## 5. 坐标系与朝向契约（单一转换点）

| 决策 | 内容 |
|---|---|
| 引擎约定（权威） | 右-handed、+Y up、米制、CCW front face、列主序矩阵（F7） |
| glTF → 引擎 | **恒等映射**（glTF 本身右-handed Y-up 米 CCW）。**禁止**在 mesh/node/renderer 三处重复转换；所有变换烘培位于 import 阶段一次性完成 |
| OBJ → 引擎 | 单一约定：OBJ 按 **Y-up 米制** 导入（OBJ 无单位/朝向定义，此为导入约定并写进 meta `upAxis=y`,`unit=meter`）；不提供 Z-up 开关（后续作为导入设置 additive 演进） |
| 节点变换 | 节点 local→world 烘焙进顶点（`ImportNodeTransform`，列主序 Mat4 乘法一次完成）；节点**不保留**；被烘焙的层级与动画/相机/灯光节点（无几何）全体写入 warnings |
| 单位 | 米制；`scaleFactor` 仅作用于导入（ModelImportSettings 已有字段），after-bake 不再二次缩放 |

## 6. GUID、Reimport 与原子提交契约

正式机制（全部 additive，不改动现有 Texture/Script 路径）：

1. **类型阶梯**：`Content::AssetType` 增加 `Model=2, Material=3`（顺序在枚举尾部
   additive）；`ImportAssetInternal` 类型 int 映射相应扩展；`ResourceType::Mesh/Material`
   已有（F2/F4）。所有现有 ABI/解析维持兼容（ ignorant 老调用仅得到新类型拒绝，与 0/1 一致 fail-closed）。
2. **导入流（v1 真实链路）**：
   ```
   EditorSession::ImportAsset(path, type∈{Model})
     → 路径/幂等校验（现有逻辑复用）
     → 解析（Format Parser → IR）
     → 纹理引用收集（路径/内嵌）→ TexturePreprocessRule 真实产出
     → Mesh/Material/Texture 注册单候选（ContentRegistry candidate）
        logical path 约定：
          assets/imported/model/<stem>/<stem>_m<n>p<k>        (mesh)
          assets/imported/model/<stem>/<stem>_mat<r>          (material)
          assets/imported/model/<stem>/tex/<texname>          (纹理，不与现有 tex 目录冲突)
     → 单次 ImportSceneReport 审核（警告可见）
     → m_Revisions.Publish(manifest, scene) **一次**
   ```
   - **任何一步失败 → 取消候选（目录留洞、编号不重用）、维持旧 revision**（F4 语义复用、无新增机制）。
   - 资产管道 BuildAll/BuildDirty 仍可独立运行于 pipeline 视角（校验/重建），
     但**注册表提交的唯一权威路径是 RevisionStore::Publish**（禁止 pipeline 侧直接改写 AssetDatabase 提交态，roadmap §4.2）。
3. **GUID 稳定性**：首次导入分配 `ResourceGUID::Create()`；**logical path 幂等检查命中**（现有路径-key 查重 :888-899）→ **same GUID**；Source hash（FNV-1a 64，复用 `HashBytes`）变化 → new revision + same GUID；hash 未变 → no-op 不发 revision；依赖（材质→纹理、mesh→材质）变化 → AssetDatabase 依赖边刷新 + 相关资产 touch。
4. **Reimport 全或无**：失败中途 资产不替换现有有效资产；重开工程后 GUID/引用从 manifest 恢复（F4 读路径只认 current）。
5. **材质 schema**：冻结于 meta `importSettings.model{Primitives[], Materials[], Warnings<report Fanout>}` + 单独 `.mat`-like JSON material asset（BaseColor factor 等字段名与 IR 1:1；roundtrip 测试锁定）。

## 7. 安全边界（解析器硬约束）

| 攻击面 | 约束 |
|---|---|
| 目录穿越 | glTF URI/OBJ mtllib/map_* 仅允许导入根内相对路径；`NormalizeProjectRelative + ProjectRoot.Resolve` 权威判定（复用 F9 工具） |
| 越界访问 | accessor/bufferView/offset+length 全范围 64-bit 上溢检查；越界即拒 |
| 资源上限 | 默认：单文件 ≤ 512MB 原始字节；buffers ≤ 64；images ≤ 64；meshes ≤ 128；primitives ≤ 1024；递归节点深度 ≤ 64；顶点总数 ≤ 16M（可配置常量集中定义） |
| 整数溢出 | 全部长度/偏移经 `checked` 计算（uint64 通道） |
| 畸形输入 | 拒绝并返回显式 diagnostics；禁"尽力而为"部分解析 |
| GLB chunk | 越界/乱序/长度对齐错 → 拒绝 |

## 8. 测试矩阵（最低集）

| 组 | 用例要点 |
|---|---|
| Parser 单元 | 合法最小 glTF(2.0)、GLB（两者）、OBJ/MTL；全字段校验失败矩阵（type/componentType/bounds/stride/URI）；GLB chunk 边界 |
| 几何 | 顶点/索引/法线/切线/UV/submesh 拆分、顶点去重统计、负索引、三角化 warning |
| 材质 | 与 IR 1:1 往返、alphaMode 三态、纹理 role 集合 |
| 纹理 | 解码、colorspace 路由、no-op 移除后的真实文件产物、UNSUPPORTED 语义 |
| 原子性 | 每个候选步注入失败（复用 `RevisionFaultPoint` 风格 hook）→ 保留旧 revision、无 GUID 残留 |
| Reimport | 同 GUID 复用、hash 变更→新 revision、依赖失效、重启恢复 |
| 安全 | 穿越/越界/大文件/递归深度/URI scheme |
| E2E | 导入 AssetBrowser 可见 → assertNotNull Entry→ Viewport 显示 → 保存/重开工程后引用有效 |

## 9. 验收（P6-1 通过标准，与 roadmap §四一致）

1. 外部 glTF（内含 baseColor 纹理）与 OBJ/MTL 样例：
   导入 → Blueprint：AssetBrowser 可见 → Editor viewport 显示 → 材质和纹理
   **无手工临时配置**（meta 完整）→ 关停/重开工程资产仍有效。
2. 全部 §8 组在 Debug+Release 有稳定数字与测试名（运行工具 `tools/test_inventory.ps1`）。
3. `TexturePreprocessRule` no-op 行为删除（必须真实产出文件）， auditing 报告必须存在。
4. **PICK-01（pick transport FAIL）修复移交 P6-2**，不属于 P6-1 验收，但 P6-1 结算时必须在台账表明当前状态（迁移自由度）。

## 10. 非目标（P6-1 内）

- Skinning/animation import（GPU/CPU skinning 位于 ANIM-01，P6-5）
- Draco/Meshopt、KTX2、sparse accessors、data: URI（显式拒绝即可）
- 多 UV 集/vertex color 完整支持
- submesh/runtime 多材质渲染（ASSET-04 新登记后续项）
- 节点层级保留/可编辑、LOD、碰撞体贴入（generateColliders 后续项）
- **任何 EngineHost/EditorBridge C ABI 变更**（导入侧由 native 发散；托管 AssetBrowser
  经现有 asset ops 呈现新类型字符串即可——渲染资产管理 additively 扩展 type 枚举字符串）

## 11. 状态词汇（交付词汇沿用 P6 统一规则）

`IMPLEMENTED`（代码）/`VERIFIED`（§8 相应测试+运行证据）/`PASS`（§9 全收口）/
`PARTIAL`/`BLOCKED`/`DEFERRED — EXPLICIT DECISION`。glTF Parser 完成≠导入 PASS。
EOF
