# P6-1B Gate B0 — Implementation Map (READ-ONLY)

**Status**: PASS — IMPLEMENTATION MAP ESTABLISHED  
**Branch**: avalonia  
**HEAD at investigation**: `124cdd0`  
**Remote**: origin/avalonia **0/0**（实测，非假定）  
**Date**: 2026-11-05  
**Authorization**: 只读映射；本次未修改任何生产源码/测试源码/CMake/ABI/序列化合同/第三方/历史记录，未执行任何 Git 提交。唯一新增工件 = 本文件。

工作区状态（实测）：3 个 dirty third_party 子模块与 `crashes/` 原样保留；本文件为唯一新增未跟踪产物。

---

## 1. 现实数据流（B0 实测航图）

### 1.1 导入提交链（bridge → content，B6/B3 复用主体）

`EditorSession::ImportAsset(path,type)` → `ImportAssetInternal`（`bridge/src/EditorSession.cpp:751-753, 769-994`）：

| 步骤 | 锚点 | 已确认行为 |
|---|---|---|
| RecoveryRequired 门 | :772-775 | 污染状态直接拒绝 |
| 类型门 | :782-786 | `type==0→AT::Texture, 1→AT::Script`，其余 fail-closed（B6 只改此阶梯） |
| 路径/通道 | :798-878 | 项目内 → 注册原路径；根外绝对路径 → 复制到 `assets/imported/{tex\|scripts}/<filename>`（:862-864 目录决定点）|
| 幂等/冲突 | :888-899 | 同路径且非 reattach → **直接返回既有 idx（publish 前快路径）**；同路径不同 GUID（reatatch 场景）→ 冲突拒绝 |
| 复制 | :901-927 | `CopyFileNoOverwrite`；目标目录 lazy 创建；己存在目标 → fail |
| 候选 | :929-945 | `candidate = m_Reg`（值拷贝）→ `Import/RegisterExplicit`；GUID 失败 → 删除已复制源文件并 fail |
| 场景字节 | :947-953 | `ReadCommittedScene(m_ScenePath,&committedScene)` —— **模型导入沿用此模式：sceneBytes=既有场景，无需触碰场景 schema** |
| 发布 | :954-969 | 单次 `m_Revisions.Publish(m_Root, manifestJson, committedScene)`；NotCommitted → 删复制文件 + fail；RecoveryRequired → `m_RecoveryRequired=true` |
| 生效 | :971-993 | `m_Reg=move(candidate)`; `m_AssetOrder` 追加; manifest 镜像 `WriteFileAtomic`（失败仅 warning `CommittedCleanupPending`）；`EV_ASSET_IMPORTED`（含 type int） |

### 1.2 RevisionStore 原子边界（`engine/src/Core/Content/RevisionStore.cpp:37-547`；`RevisionStore.h:71-88`）

- 唯一提交入口 `Publish(manifestJson, sceneBytes)`；无 begin/commit/rollback API —— 回滚即"候选失败路径"（失败通知 old `current` 保持权威，cpp:394-540）。
- write.lock（D-1）→ 双基线校验（D-2）→ 候选目录失败留洞、编号不永不重用（cpp:367-390 → 语义=failed candidates leave holes）。
- 测试注入点：`RevisionFaultPoint{…8 个时机…}` + `SetRevisionFaultHookForTesting`（RevisionStore.h:26-38）；`ContentFaultPoint::BeforeRenameSourceCleanup`（h:40-43）。
- B3 的 publish-failure 原子性测试**无需新增注入机制**（复用既有 fault hook 语义）。

### 1.3 注册表幂等语义（`engine/include/Engine/Core/Content/ContentAsset.h`）

- `ContentRegistry::Import(path,type)` 幂等（同路径→既有 GUID，:60-62）；`RegisterExplicit` 同 GUID 不同路径 → false（:64-66）；`ResolvePath` 未知 GUID → 空串（非崩溃，:70）。
- 场景仅存 GUID；路径为注册表解析信息（h:4-19 头注释，Phase A 契约）→ **Model 资产身份可直接遵循同一模型**。
- v1 红线（h:11-12）："**仅两种资产类型（Texture/Script）；无内容寻址/版本控制**" —— B6 扩类型时此枚举（:33 `enum AssetType {Texture=0, Script}` 同时扩展 `ToString(:35-37)`）。
- `SerializeManifest`/`LoadManifest`：候选严格校验后才整体交换（:88-94）。

### 1.4 AssetPipeline 现实（`engine/intclude/…/AssetPipeline.h:109-144`；`…/AssetPipeline.cpp`)

- Rule 五级 `PipelineStage` 平面枚举按阶段排序注册执行；`AssetRule` 纯虚面 `GetName/GetDescription/GetStage/GetMatch/Execute/NeedsRebuild`；上下文 `PipelineContext{entry, sourcePath, outputDir, intermediateDir=outputDir+"/.intermediate", metadata, generatedFiles, onProgress, cancelFlag}`；结果 `PipelineResult{success, errorMessage, outputFiles, lastStage, elapsedSeconds}`。
- `PipelineResult.errorMessage` 从 `ctx.metadata["validation_error"]` 复制（cpp:520-524）→ **B1/B2/B4 的失败诊断通道**。
- 真实调用者（生产）仅 `DependencyGraphPanel`（`engine/src/Editor/DependencyGraphPanel.cpp:330,339` —— BuildAll/BuildDirty）；`tests/test_content/ShaderCompileRuleTest.cpp` 是测试侧唯一调用者。→ B4 之前 TexturePreprocessRule 只会被 Editor 表面板按钮触发 —— **B4 的真实化效果需要所选驱动入口声明**（推荐接 B3 EditorSession 模型导入链，作为函数级直接调用而非依赖 pipeline BuildAll）。
- `TexturePreprocessRule`（cpp:121-147）现状 no-op：只 push `*.rgba8.png` 路径、不写文件 → B4 完全重写该规则；等价于替换而非新增，无需触碰后端。
- `ShaderCompileRule`（真实写盘+NeedsRebuild 时间戳）是 B4 参考样式。

### 1.5 运行时 Mesh 渲染收集（B5/B6 真实接入点）

- 生产帧收集：`Application::RenderProductionFrame`（`engine/src/Core/Application.cpp:938-959`）—— 从 `scene.GetObjects()` 按 `MeshComponent{m_Visible && HasMesh()}` 组装 `meshObjects`；**MeshComponent（CPU Mesh）是接入点，而非 MeshRendererComponent**。
- RenderGraph path pass（:1010-1028）：`m_Production3DMeshRenderer->SetCameraMatrices(...)` + `Render(meshObjects)`；Direct path（:1049 起，同型），两路共用 MeshRenderer。
- `MeshRenderer::UploadMesh`（`engine/src/Core/RHI/MeshRenderer.cpp:46-88`）消费 `Vertex3D{position,normal,texCoord,tangent}`（44B）+ `uint32` 索引；PASS：loc0/1/2/3 与既有顶点/实例缓存 `m_MeshCache`（MeshRenderer.h:267-276 CachedMeshData `vao/vbo/ibo indexCount`）。**无 submesh span 语义** → 网格组件多形态在被 B5 作 additive（spans + 循环 Draws同一个 VAO）。
- 每材质绑定：`MeshRenderer` 单 `std::shared_ptr<Shader>`（`SetShader`）；`meshObjects` 各对象绑定 MeshComponent 的内嵌 `Mesh`+`m_Color`（MeshComponent.h）→ **B5 的 runtime Mesh 加载欲接入即 MeshComponent。m_Mesh = imported asset**。

### 1.6 纹理运行时路径（B4 运行时 sRGB 接入点）

- 加载链：`SceneSerializerV1::InstantiateScene`/`EditorSession::BindEditSceneSpriteTextures`（bridge/src/EditorSession.cpp:1870-1895） → `SpriteComponent::SetTexture(m_TexMgr, path)` → `TextureManager::Load`（`engine/src/Core/RenderResources/TextureManager.cpp:11-25`）→ `IGraphicsFactory::CreateTexture(path)` → `OpenGLTexture`（stb as-is 解码、`channels=3→GL_RGB8/4→GL_RGBA8`、全线性、`GenerateTextureMipmap`，`engine/src/OpenGL/Resources/OpenGLTexture.cpp:16-53,105`）。
- colorspace 选择字段目前只存在于 meta 层（`AssetTypes.h TextureImportSettings.sRGB/isNormalMap`、AssetMetaDb）且**无消费方**（Engine 内 grep 0 hit）→ B4 runtime 接入点 = 给 `OpenGLTexture(CloneForPathInternal)`/`OpenGLGraphicsFactory::CreateTexture` 一条 additive 重载 path+色spacespec，不改既有默认（否则旧资产/ sandbox 默认行为违合同"旧资产默认行为不变"）。
- **注意（★ NOT YET 修）**：`stbi_set_flip_vertically_on_load(true)`（:27,101）与 glTF GLB 顶点/UV 无关但会 flip imported texture；preprocess 端不决定 flip——语义上属于**读取面翻转**，在 GL 场景中本引擎已按此约定（所有现有纹理同一行为，视觉一致性维持）；B4 端不更改该全局态（跨测试泄漏风险已知）→ 列入 UNKNOWN（U-3）。

### 1.7 ResourceManager 新类型接入（B5 全部接缝在每个装载路径上）

- `enum ResourceType{Unknown=0,Texture=1,Shader=2,AudioClip=3,Font=4,Mesh=5,Material=6}`（`engine/include/…/Resource.h:44-65`）——**Mesh=5/Material=6 已存在无实现**（，与 B0 一致）。
- 同步特化：`LoadByType<Texture|Shader|AudioClip>`（`ResourceManager.cpp:85-129`，仅此三特化；模式=Create/Load→`m_Registry.Register`→`SetGUID`）；异步 `ExecuteLoad` switch 只 Texture/Shader/AudioClip（`ResourceManager.cpp:429-470+`）；`ResourceTypeFor<T>()` 类型映射（`ResourceManager.h:569-575`）同三特化；`InitPools`（:68-79）同三池。
- B5 新增点：`MeshLoadData` 到 `AsyncLoadData.h`；`LoadByType<Mesh>` 特化 + `ExecuteLoad` case ResourceType::Mesh + `ResourceTypeFor<Mesh>` + `InitPools` 池；惰性 `Resource::PostLoad`/`ResolveDependencies`（Resource.h:94+）随 GPU 上传处直接使用 `IGraphicsFactory` 创建 buffers；与 `Renderer::ObjectVertex` 组表层无冲突。
- 现有Resource注册/引用：`ResourceRegistry::Register*`/`ResourceRef<T>`（`engine/include/…/Resources/ResourceRegistry.h`）→ B5 直接复用。

### 1.8 GUID/路径/安全工具（导入安全）

- GUID：128-bit UUID v4（`ResourceGUID`，与 AssetDatabase 同位兼容）；`EditorSession::ReattachAsset`（EditorSession.cpp:755-767）可修复丢失。
- 项目路径权威工具（F9/B3 安全约定）：`ProjectPaths`（`engine/include/Engine/Core/Content/ProjectPaths.h`）——`NormalizeProjectRelative/ProjectRoot::Resolve/ToProjectRelative/KeysEqual`（h:82-119 + 参数化拒绝 enum，即 `PathError::EscapesRoot/DotDotSegment/…`）→ **URI/路径校验直接复用；无第二套路径校验**。
- 幂等通道由 path key（`m_Root.KeysEqual`）保证；但**同路径快路径无 hash 重检**（→ 冲突 C-2）。

---

## 2. B0 新增模块 → 现有接口映射表（B1-B7 待授权实施界面）

| 新模块（建议源只落在 B1） | 使用的现有对方 | 不新增的（红线）|
|---|---|---|
| `AssetImport/` 目录（ImportResult/Diagnostic/IR/GltfParser/GlbContainerReader/ObjParser/MtlParser） | nlohmann::json 独 | 第三方解析器、RevisionStore 写入 |
| `MeshImportIR` 顶点面/材质/纹理引用 | `Vertex3D`（Mesh.h:17-22）1:1 对齐 | submesh semantic 新材质系统 |
| Validation + Caps | `PathError` enum + `ProjectRoot.Resolve`（ProjectPaths.h:82-119） | 新路径校验体系 |
| `MeshBuild`（IR→可提交 mesh 件） | `ContentRegistry::Import`（幂等）；`ContentAsset::Import`对象 | 第二套提交、写 AssetDatabase 直更 |
| 发布 | `EditorSession::ImportAssetInternal`（additive type 2/3）；`RevisionStore::Publish` 单入口 | 第二 publish 通道 |
| TexturePreprocess 真实化 | stb_image、`PipelineContext/generatedFiles/metadata`、`WriteFileAtomic` | 新解码库、伪成功状态 |
| Runtime MeshLoad | `ResourceType::Mesh=5`、`ResourceRegistry`、`UploadMesh` | 新渲染后端、renderer 重写 |
| Model 引用（B6） | `MeshComponent` + 场景内 JSON 字段 additive | **C ABI 扩展（needs authorization）** |

## 3. P6-1A 合同 → 代码锚点 逐条确认（B0 要求）

| 合同条款 | 锚点 | 状态 |
|---|---|---|
| §1 范围（gltf/glb/obj/mtl/PNG/JPEG 等输入面） | capi.h 扩 idemm；fallback；AssetTypes.h:112-114 扩展映射已存在 | CONFIRMED |
| §2 IR 形状 | 与 Vertex3D / uint32 索引/列主序 Mat4 兼容（Mesh.h:17-22；MathTypes.h:164-194） | CONFIRMED |
| §2 资产粒度 | **合同冻结：一个 primitive / OBJ mesh 组 = 一个 Mesh asset** —— 与本次 B3 规则 7"同源文件不拆多 asset" 记录为国陷 C-1 | CONFLICT（见 §4） |
| §2 节点烘焙/叶接面 | 生产帧收集链接 MeshComponent；节点烘焙后不保留：**可直接满足**（无 scene schema 变更，F8） | CONFIRMED |
| §3 解析器 zero-third | glTF JSON → nlohmann（已在 Revision/Content/Scene 依赖）；GLB/OBJ/MTL 手写；stb_image | CONFIRMED |
| §4 preprocess | no-op 规则孤立能力：锚点 AssetPipeline.cpp:121-147；P6-1A §4.1-4.8 全部可在此内替换 + Additive Runtime sRGB 点（OpenGLTexture.cpp:16-53+105 & AssetTypes sRGB bool 无消费） | CONFIRMED |
| §5 坐标恒等/单转换点 | glTF Y-up 右-handed米珩 golden；EditorCamera up=(0,1,0)（EditorCamera.cpp:210）；column-major；GL 默认 Cwick | CONFIRMED |
| §6 单 publish / 幂等 / GUID 稳定 | EditorSession 769-994 + ContentRegistry::Import + Publish 唯一入口 | CONFIRMED（幂等语义有 C-2 脱缺） |
| §6.3 hash 触发更新 | **现实现无 source-hash 快路径查看（：888-899）** → C-2 | CONFLICT |
| §6.5 类型阶梯 additive | AssetType（ContentAsset.h:33+ToString:35-37）；ImportAssetInternal :782-786 | CONFIRMED |
| §7 安全 | PathError/ProjectRoot Resolve; FileSize/caps **需新增集中常量** | CONFIRMED（caps 新增，无冲突） |
| §8 测试矩阵 / §10 无新 ABI | capi.h type 注释（:175-177）0/1；op 11 显通过既有 Dispatch；**B6 "entity→model 绑定" 若用 MeshComponent+scene JSON 需无 ABI（additive scene 字段）** | CONFIRMED-with-note（见 C-4） |

## 4. 合同与授权细节的冲突判定（B0 最重要交付物）

### C-1 资产粒度：合同 §2 vs 授权 B3 规则7/8 + B5 "Submesh 范围"

- 冻结在案（P6-1A §2）：**one primitive = one Mesh asset**（理由当年指 MeshRenderer 单材质；已列入台声 ASSET-04）
- 本次授权：7 一源文件对应一个 Mesh asset；8 单材质限制必须显式，不许静默丢靓；B5 需求中含 "Submesh 范围"
- **B0 建议（DECISION REQUIRED at B1 authorization）**：采纳新版语义为准并后续在 B1 一开更新已注册为 `AMENDED`：
  1. **一源文件 = 一个 Mesh asset**（多 primitive 折叠为**内部 primitive spans**，spans 不放大 OK）；
  2. Mesh 型新增 `Mesh::Span{firstIndex,indexCount,materialIndex}`（additive，默认单一 span 使既有程序生成 mesh 零行为差异）；
  3. 材质显式限制（推荐 **(a)**：源含多材质 → 最先材质作为渲染材质；**同时**每个源材质仍 **被表记入** Material 资产清单、且逐 primitive spans 材质索引入 ack 置信，加显式 warning "additional materials registered but not rendered"）—— 语义=不丢弃、不静默；备选 （b）严格 fail-closed 拒绝多材质源（对 demo glTF 伤真度高）。
  - 注册ants 键：`assets/imported/model/<stem>/<stem>.mesh`（不再 per.primitive 路径）；`…/<stem>.material_<r>` 保持。

### C-2 幂等快路径无 hash 重检（合同 §6.3 语义缺口）

- 锚点：EditorSession.cpp:888-899（同路径命中→直接 return existing idx；asset-level 发布不开启）。
- B3 需要补（授权范围内）：**在幂等分支加入 source hash（FNV-1a64 via `RevisionStore::HashBytes`）比对**；hash 相同 → 无操作；hash 变化 → 视 as 新（走 reimport）→（模型/纹理新增）以新 revision 发布。**item mapping: 'path 决定 GUID' 与 'delete+re-add hash triggering' 委由 B3 delegate Configuration Bootstrap**。

### C-3 纹理翻转全局态 （U-3）

`stbi_set_flip_vertically_on_load` 为全局态（`OpenGLTexture.cpp:27,101`）；B4 preprocess 不经过 GL。与 existing assets 完全兼容，v1 不动这个声明（== 运行时 feed path inGL 无改动）；列为 UNKNOWN（无风险）。

### C-4 B6 Model 绑定是否需 C ABI

- 当前 ABI 里没有"给实体赋 mesh/model"：仅 sprite/script（EditorSession assetAssign 通道）。
- **可无 ABI 完成**：托管端不赋值，模型导入→AssetBrowser 展示（需 type 字符串 additive 扩展）+（后续 gate）同理"assign model to entity" op。B0 结论：**B6 v1 通过 scene JSON additive 字段（`components[].data.modelGuid`/`meshPath`）**——场景 schema 是 v1 契约（F8 仅 position），加组件字段允许 additive（组件数据本身是 `data` 集合）；但**Schema v1 兼容性由 Contract 测试锁定**（SceneSerializerV1 相关 17 项继续保持）。无 C ABI 变更。此项仍由 B6 授权隐式确认。

## 5. 失败原子性边界（B3/B4 测试要删除的锁点矩阵）

| 失败点 | 半人工回滚动作 | current revision 影响 | GUID 影响 | 锚点 |
|---|---|---|---|---|
| 解析/验证失败（IR stage） | 无副作用（不写 registration/I/O） | 无 | 无 | Parser-only pass |
| mesh 构建失败 | 无 publish（**candidate 快照即构建后状态，工艺中失败还不足**） | 无（old current） | 无 | `ImportAsset` 试针道：x candidate 未解 publish 无包 |
| 纹理预处理失败 | 复制源在 copySource 路径删除 + 不 publish（既定链） | 无 | 无 | :913-919/941-943 作用复用 |
| `Publish` 失败=NotCommitted | 删除复制源 + 失败反馈 | old `current` 权威（cpp:394-547） | candidate 弃（discard，条条 out of `m_Reg`） | :954-969 |
| `Publish` 失败=RecoveryRequired | 删除复制源 + `m_RecoveryRequired=true` 阻断后续导入 | 未定（明示） | 未定（明示） | :958-963, :772-775 |
| 发布成功但 mirror 失败 | **不回滚**：warning CommittedCleanupPending（treatable state） | committed | committed | :977-984 |
| 幂等重导同 hash (C-2 after fix) | publish skip（无新 revision） | 不变 | same GUID | :892-895 + hash check |
| reattach 失败时复制源清理 | fs::remove + Fail | 无 | 无 | :933-935 |

## 6. 安全限制实现位置（B1 集中定义点）

- 全部 upper-bound 常量建议集中放 **新头 `engine/src/AssetImport/ImportLimits.h`**（同 P6-1A §7 表）；与 `ProjectPaths`（路径）+ `ImportDiagnostic`（error）组成安全层 Probe 的三个互补面。
- GLB chunk/JSON size 校验作业在 `GlbContainerReader`；Accessors/BufferView/URI 越界在 `GltfParser`；`queued checks` 位于 Parser-free helper（IR level）；无 caps 的 Content 主任 Enforced in `ContentRegistry`.
- Android 共享 no;（Windows-only demo）

## 7. 测试落点（B1-B7 执行时注册）

| Gate | 测试目标（建议） | CMake 注册点 |
|---|---|---|
| B1/B2/B4 | **新 target `test_assetimport`**（parser/IR/安全/纹理管线） | tests/CMakeLists.txt 新增（B1 授权时执行） |
| B3 | 并入 `test_content`（RevisionStore/幂等/原子性组）—— 复用 existing FaultPoint 预设定 | 既有 target |
| B5 | 并入 `test_renderer`/`test_content`；pixel readback 新用例建议 `test_debugdraw`（GL fixture 同型做法） | 既有 target(s) |
| B6 | `test_bridge`（editor 侧）+ **P4/Fx scene-Schema additive 兼容性应被**`test_io`/`test_content` 现有 binding 测试**自动锁** | 既有 target |
| B7 | 全量双配置 `tools/test_inventory.ps1` + E2E 证据文档 + GL 像素读回 | — |

禁用：**不得为 B7 拓觉专属全量数落 COLLECTION 指标 —— test_quantities**；因目标 target names registered 由 B3 确认后。**注：当 test_pick_transport 依旧含 PICK-01 作为 known RED in baseline；B6/B7 all pass 的定义除 PICK-01 例外于其他相关用例**。

## 8. UNKNOWN 清单

- U-1：`scene.GetObjects()` 全量 & `MeshComponent::HasMesh`（Application.cpp:945-959 集合既成文），但 `MeshComponent` 的 Engine 生命周期与新建 Mrh 交换时点 B5 需读实现（渲染帧资产挂载）。
- U-2：同一 mesh 跨 entity 的 GPU `m_MeshCache` key（=hash-of-mesh指针？uint64）生成方式按 MeshRenderer.cpp:46-88 Search 单查——B5 扩展 submesh span 时 cache 生命周期/复用语义需读完整实现。
- U-3：`stbi_set_flip_vertically_on_load` 全局态（C-3 已记录）。
- U-4：`AssetMetaDb::ReadMetaFile` type 解析 "simplified stub to Texture"（AssetMetaDb.cpp:180 注释）→ B6 Model 资产 meta 读回时是否强制扩此段，需在 B6 确认（无阻塞性：content registry 本为大家解码出入口）。
- U-5：`Assets/meta/mdb 扫描 .gltf/.glb/.obj`（F10）存在，但 EditorAssetBrowser 展示的当前 type 展示映射是否吃撑 Model 值 = B6 检查点（type strings additive per P6-1A §6.5 声明）。

## 9. 结论

- B0 逐条锚点闭合；**发现并量化 4 项 DECISION 剩余事务**（C-1/C-2 必须在 B1 批准文本中裁决，C-3 无阻塞，C-4 B6 再确认）。
- 无生产源码/测试/CMake/ABI/合同/第三方变更；未提交、未推送。
- 提议 B1 授权时的月度 rev 数据：见 §4 建议（a），由用户授权修正 AMENDED 后进入 B1。
EOF