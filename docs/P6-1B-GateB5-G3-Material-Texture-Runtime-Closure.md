# P6-1B Gate B5-G3 — Material / Texture Runtime Binding Closure

**Status**: PASS — SCOPE-BOUND
**Branch**: avalonia
**HEAD**: `124cdd0`（实测；未提交、未推送；远端 0/0）
**Date**: 2026-11-06
**Precondition**: B5-G2 PASS（测试基线 1149 discovered）

---

## 1. 范围与结论

本 Gate 只做三件事：

1. 严格读取 B3/B4 已提交的 Material 资产（`*.material.json`，formatVersion 1）与 Texture 资产（B4 PNG + sidecar）。
2. 按 **已冻结的 writer 命名规则** 把 mesh 的 material slot 解析到唯一 Material 资产，并把 texture 引用接入既有 GL 上传路径。
3. 在 `MeshRenderer` 上增加 additive 的 material binding map 与首材质消费路径（`u_ObjectColor` / baseColor / normal），不改变公共 Shader ABI、不改变绘制拓扑、不进入 G5 像素门禁。

结果：**PASS — SCOPE-BOUND**。像素级最终证明仍归 B5-G5；本 Gate 的证据边界是 reader/binding/cache/接线与全量回归。

**合同红线保持不变**：

- 未修改 B3/B4 writer、Mesh JSON schema、digest v2、RevisionStore 原子性；
- 未修改公共 Shader ABI / Bridge C ABI / MeshComponent / Scene 序列化；
- 未修改 G2 `MeshGpuCache` 的 key、owner/context 门控与失败语义；
- 未做 CPU gamma，未做双重 sRGB；纹理内部格式仍由 B4 sidecar 经 `OpenGLGraphicsFactory::CreateTexture` 决定；
- 未 commit / push / reset / clean / stash。

---

## 2. 设计决定

### 2.1 Slot → Material 路径（确定性映射，不改 writer）

- Mesh 资产路径：`<stemDir>/<base>.<digest8>.mesh`。
- Material 资产路径（B3 writer 冻结）：`<stemDir>/<base>.m<slot>.<mhash8>.material.json`。
- Resolver 只使用**已提交注册表条目**（type == `"Material"`）做 `<base>.m<slot>.` 前缀 + `8hex + ".material.json"` 形状匹配；**从不使用 material name 猜路径**。
- missing / ambiguous 分别 fail-closed；其他目录、其他 stem、非 Material 类型不参与匹配。

### 2.2 Texture 引用与 GPU 上传

- Material reader 只接受 writer 写出的 `baseColorTexture/normalTexture/occlusionTexture/emissiveTexture/metallicRoughnessTexture` 结构（`present/imageIndex/assetPath`），未知字段拒绝。
- Binding 层要求 `present == true` 时 `assetPath` 非空且注册表中存在唯一 `"Texture"` 条目。
- CPU 侧使用新增 `TextureAssetReader`（PNG + sidecar）校验：format/ruleVersion/role/colorspace/尺寸/components；缺 sidecar 时按 B4 legacy 规则（linear）。
- GPU 侧使用新增 `TextureGpuCache::Acquire` → `IGraphicsFactory::CreateTexture(absPath)`，即 B4 已验证的 sRGB/linear 内部格式路径。
- Cache key = `{identity(path), contentDigest(PNG+sidecar bytes), uploadSemantics(role/colorspace/ruleVersion)}`；命中尺寸/语义守卫；非 owner thread 或非当前 GL context 一律拒绝；上传失败不发布条目。

### 2.3 首材质策略（C-1 amendment 保持）

- 所有 slot 的 Material 资产均被解析与保留（`MeshMaterialBinding::slots` 连续覆盖全部 slot）。
- **v1 仍只用 slot 0 绘制**；`slotCount > 1` 产生显式 warning，记录在 binding 中。
- slot 0（首材质）不可读/非法/其纹理解析失败 = **fail-closed**，不返回部分 binding。
- slot ≥ 1 的资产失败只产生 warning，不阻止 slot 0 渲染。
- Mesh 侧 primitive spans 保持连续覆盖整个 index buffer，因此既有整段 `DrawIndexed` 与“按 span 顺序绘制全部材质”在 v1 语义下等价；本 Gate **未改绘制拓扑**。

### 2.4 Renderer 消费（additive）

- `MeshRenderer` 新增 `m_MeshMaterialBindings`：key = G2 `MeshGpuResourceKey`，value = `shared_ptr<const MeshMaterialBinding>`。
- 新增 `SetMeshMaterialBinding / ClearMeshMaterialBindings / GetMeshMaterialBindingForTesting`。
- `ApplyMeshMaterialBinding` 在两条生产颜色路径绘制前调用：
  - `u_ObjectColor` = `baseColorFactor`；
  - baseColor 纹理 → unit 0 + `u_Texture=0/u_HasTexture=1`；无纹理则 `u_HasTexture=0`；
  - normal 纹理 → unit 1 + `u_NormalMap=1/u_HasNormalMap=1`；无纹理则 `u_HasNormalMap=0`。
- Shader 已存在这些 uniform（B5-G3 preflight 实测），因此无 Shader ABI 变更；未绑定 material 的 mesh 完全走原 `MeshComponent` 路径。
- 高度图/位移、shininess 等仍沿原 `MeshComponent` 路径；材质没有对应字段，不做隐式合成。

---

## 3. 文件与测试

| 类型 | 文件 | 说明 |
|---|---|---|
| 新增 | `engine/src/AssetImport/MaterialAssetReader.h/.cpp` | 严格 Material v1 JSON reader（未知字段/非有限浮点/alphaMode/纹理引用校验） |
| 新增 | `engine/src/AssetImport/TextureAssetReader.h/.cpp` | PNG+sidecar CPU reader（B4 语义、RGBA8、legacy linear、stbi flip 与运行时一致） |
| 新增 | `engine/src/AssetImport/MeshMaterialResolver.h/.cpp` | 由 mesh 路径 + registry 唯一解析 slot → Material 路径 |
| 新增 | `engine/src/AssetImport/MaterialRuntimeBinding.h/.cpp` | Slot 解析 + Material/Texture 读取 + 可选 GPU cache 装配 + 首材质策略/warnings |
| 新增 | `engine/include/Engine/Core/RHI/TextureGpuResource.h` / `engine/src/Core/RHI/TextureGpuResource.cpp` | 纹理 GPU resource + identity/content/upload-semantics cache（owner/context 门控） |
| 修改 | `engine/include/Engine/Core/RHI/MeshRenderer.h` / `engine/src/Core/RHI/MeshRenderer.cpp` | binding map + 两条颜色路径 apply；未改 draw 接口 |
| 修改 | `engine/src/AssetImport/ImportDiagnostic.h` | 新增 MATERIAL/TEXTURE reader 与 binding 诊断码 |
| 修改 | `engine/src/AssetImport/MeshAssetWriter.h` | `kMaterialAssetFormatVersion = 1` |
| 新增 | `tests/test_assetimport/B5MaterialTextureRuntimeTest.cpp` | 11 个 G3 测试（CPU reader/resolver/binding + 真实 GL cache/binding） |
| 修改 | `tests/CMakeLists.txt` | 注册 G3 测试；`test_viewport_resize` 增链 `EngineCore`（修正既有 include 传播缺口） |
| 修改 | `tests/test_content/ManifestStrictLoadTest.cpp` | 未知类型样例 `"Mesh"` → `"Bogus"`（Mesh/Material 自 B3 起为合法类型） |

**禁止面**：B3/B4 writer、Mesh JSON schema、digest v2、RevisionStore、公共 Shader ABI、Bridge C ABI、MeshComponent/Scene、G2 cache 语义均未改动。

---

## 4. 验证证据

### 4.1 构建

- `cmake --build build --config Debug`：成功（全量）。
- `cmake --build build --config Release`：成功（全量）。

### 4.2 定向测试

- `test_assetimport`：**132/132 PASS**（Debug 与 Release 均实测）；其中 G3 新增 11 个，覆盖：
  - Material reader 正/负（未知字段、版本、alphaMode）；
  - Texture reader sRGB/legacy/role-colorspace 冲突/尺寸不一致/空数据；
  - Resolver 唯一/缺失/歧义/非法 mesh 路径/其他 stem 不匹配；
  - Binding 首材质 + CPU 纹理解析、首材质缺失 fail-closed、未注册纹理 fail-closed、后续 slot 非法仅 warning；
  - 真实 GL：TextureGpuCache sRGB 内部格式、命中复用、语义冲突拒绝、Invalidate；binding 经 cache 取得 sRGB GPU 纹理。

### 4.3 全量回归（双配置一致）

- **1160 discovered = 1160 executed**（G2 基线 1149 + G3 新增 11）。
- **1158 PASS / 1 FAIL / 1 SKIP**（Debug 与 Release 相同）。
- 唯一 FAIL（零漂移，PICK-01）：`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`
  - `tests/test_bridge/test_pick_transport.cpp(231)`（status HIT）
  - `tests/test_bridge/test_pick_transport.cpp(233)`（entity_id ≥ 1，actual 0）
- 唯一 SKIP：`ShaderCompileRule.SkippedWithoutVulkanOrShaderc`（既有 SDK 条件项）。

### 4.4 过程中发现并修正的两处测试/构建卫生问题（非 G3 功能面）

1. `test_viewport_resize` 仅链接 `EditorBridge`，无法获得 `EngineCore` 的 PUBLIC include 目录，增量全量构建时报 `Engine/Application.h` 缺失。已在 `tests/CMakeLists.txt` 增链 `EngineCore`（仅 include/链接传播，不改测试语义）。
2. `test_content` 长期未重建，`ManifestStrictLoadTest.UnknownTypeIsRejected` 仍把 `"Mesh"` 当未知类型；而 B3 起 Mesh/Material 已是合法注册类型。已将该负例改为真正未知的 `"Bogus"`。此前基线未暴露该问题属于 stale binary，不属于 G3 回归。

---

## 5. 已知边界（G5 前）

- 本 Gate 未做像素级验证：uniform/纹理接线通过代码路径与 binding/cache 测试证明，最终可见结果仍以 B5-G5 像素门禁为准（记录为 **SCOPE-BOUND**，不升格为像素 PASS）。
- material normal 纹理存在时覆盖 `MeshComponent::m_NormalMap`；height/parallax 仍归组件路径。
- v1 多材质只渲染 slot 0（warning 显式记录）；span→材质切换与第二材质绘制归后续 Gate。
- 纹理 identity 当前为路径 fold；GUID 直连属 Asset Resolver Gate。
- Binding 装配未接入 EditorSession/EngineHost 的生产调用点（本 Gate 只提供引擎层 API 与测试）；生产接线归后续 Gate，避免提前改变运行时行为。

---

## 6. 回滚

仅需回滚本 Gate 新增/修改文件；无数据迁移、无资产格式变化、无 cache 持久化。回滚后 G2 基线（1149）应恢复；PICK-01 预期仍为唯一 FAIL。
