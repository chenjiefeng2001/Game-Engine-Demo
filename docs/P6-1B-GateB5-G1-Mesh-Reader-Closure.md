# P6-1B Gate B5-G1 — CPU Mesh Asset Reader Closure

**Status**: PASS — SCOPE-BOUND  
**Branch**: avalonia  
**HEAD**: `124cdd0`（实测；G1 未提交、未推送，远端 0/0）  
**Date**: 2026-11-06  
**Precondition**: B5-G0 COMPLETE（Map 已交付）；B4 基线 1114 discovered

---

## 1. 授权范围与边界

**实施**：CPU-only Reader（资产字节输入）、必要 CPU 数据类型、专项测试、本记录。  
**未改动**：Mesh writer、`m_MeshCache`、GL 资源、`MeshComponent`、Scene Serializer、Bridge/C ABI、Material writer/reader（G3）、任何 GL/线程路径。  
**新增文件**：
- `engine/src/AssetImport/MeshAssetReader.h`
- `engine/src/AssetImport/MeshAssetReader.cpp`
- `tests/test_assetimport/B5MeshAssetReaderTest.cpp`

**修改文件**：
- `engine/src/AssetImport/ImportDiagnostic.h` — 新增 3 个稳定 code：`MESH_ASSET_PARSE_FAIL` / `MESH_ASSET_UNSUPPORTED_VERSION` / `MESH_ASSET_INVALID`
- `tests/CMakeLists.txt` — test_assetimport 源列表增加 B5 测试文件（最小构建注册）

## 2. Reader API（CPU-only）

```cpp
namespace Engine::AssetImport {
struct MeshAssetVertex { float position[3], normal[3], uv0[2], tangent[3];
                         uint8_t flags; };  // kVertexFlag* bits
struct MeshAssetSpan   { uint32_t firstIndex, indexCount, materialIndex;
                         std::string nodeName; float sourceTransform[16]; };
struct MeshAssetImageRef { std::string uri, mimeType; };
struct MeshAsset { uint32_t formatVersion; std::string sourceFile;
                   vector<MeshAssetVertex> vertices; vector<uint32_t> indices;
                   vector<MeshAssetSpan> primitives;
                   vector<std::string> materialNames;
                   vector<MeshAssetImageRef> images; };
struct MeshAssetReadResult { bool ok; std::string error; MeshAsset mesh; };
MeshAssetReadResult ReadMeshAssetJson(const std::vector<uint8_t>& bytes);
}
```

- 输入 = 资产**字节**；不读文件系统、不解析 `.mesh` 路径、不接触注册表/revision（定位入口属 G2/G4 决策，B5-G0-B）。
- 失败返回 `ok=false` 且 `mesh` 保持空（无部分可用对象）；错误格式 `"<CODE>: detail"`。

## 3. v1 字段映射（与 writer 实际输出逐项对齐）

| 资产字段 | Reader 输出 | 校验 |
|---|---|---|
| `format` | — | 必须 `"mesh"`，否则 PARSE_FAIL |
| `formatVersion` | `formatVersion` | 必须 == 1（`kMeshAssetFormatVersion`）；缺失/非整数/其它值 → UNSUPPORTED_VERSION |
| `sourceFile` | `sourceFile` | 必须为 string |
| `vertices[] {position[3],normal[3],uv0[2],tangent[3],flags}` | 逐字段 → `MeshAssetVertex` | 数组精确长度；每数 finite（拒绝 NaN/Inf/float 溢出）；flags 仅允许已定义位 0x7，未知位拒绝 |
| `indices[]` | `indices` uint32 | 非负整数 ≤ UINT32_MAX；每值 < vertexCount；总数 ≤ 16M×3；总数必须为 3 的倍数（v1 仅三角形） |
| `primitives[] {firstIndex,indexCount,materialIndex,nodeName,sourceTransform}` | `MeshAssetSpan` | indexCount>0；first+count ≤ indices（64 位域求和）；span 不得与前一 span 重叠；materialIndex null→0xFFFFFFFF，否则 < materialNames.size()；nodeName string；transform 恰 16 个 finite 数 |
| `materialNames[]` | `materialNames` | string 数组；≤ kMaxMaterials |
| `imageUris[] {uri,mimeType}` | `images` | 对象仅含这两键且均为 string；≤ kMaxImages |
| 根对象其它字段 | — | 未知顶层字段拒绝（v1 格式封闭；版本升级走 formatVersion） |

**Flags 决策（G0-C 落实）**：顶点有效性位保存在**独立 CPU 字段**（`MeshAssetVertex::flags`），**不塞入 44B `Vertex3D`**；G2/G4 的运行时映射点再决定缺省法线/切线处理。Reader 已用专项断言证明 flags 原样往返（OBJ/glTF 两种来源）。

## 4. 测试证据（新增 23 项）

| 组 | 用例 |
|---|---|
| 正向往返 | OBJ 单 primitive（含 vt/vn/flags）；OBJ 多 primitive + 材质槽 + nodeName/sourceTransform；glTF 三角形；GLB 内嵌 image mimeType；空 Mesh 结构合法；重复读取确定性 |
| 负向（全部 fail-closed 且无部分对象） | 空字节；坏 JSON；format 错误；formatVersion=2；缺 vertices；未知顶层字段；float 溢出 1e40；float 类型错误（"nan" 字符串）；未知 flag 位；索引越界；span 超界；span count=0；materialIndex 越界；transform 长度错误；索引非 3 倍数；materialNames 元素类型错误；image 缺 mimeType |

**数据模型往返完整性**：vertices（含 flags）/indices/spans（firstIndex,indexCount,materialIndex,nodeName,sourceTransform）/materialNames/imageUris/sourceFile 逐项断言相等（`ExpectSameMesh`）。

## 5. 构建与回归

| 项 | Debug | Release |
|---|---|---|
| EngineCore | 0 error | 0 error |
| `test_assetimport` | **109/109 PASS** | **109/109 PASS** |
| 全量 `tools/test_inventory.ps1` | **1137 discovered = 1137 executed；1135 PASS / 1 FAIL / 1 SKIP** | 同上（一致） |

- B4 基线 1114 → **+23**（全部来自本 Gate 新增 reader 测试；计数增量与测试名已在源文件可枚举）。
- **PICK-01 单独核对**：唯一 FAIL = `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，断言仍为 `test_bridge/test_pick_transport.cpp(231)`（`packet.status == HIT`）与 `(233)`（`entity_id >= 1`）——与 B1–B4 基线完全一致，**零漂移、无新增失败**。
- SKIP 唯一 = `ShaderCompileRule.SkippedWithoutVulkanOrShaderc`（既有 SDK 条件性，同 B3/B4）。
- LNK4006×2 为 P5 在案既有项，非本 Gate 新增。

## 6. 已知限制 / 交接

| 项 | 说明 | 后续 |
|---|---|---|
| materialNames-only | Mesh JSON 无 Material 资产路径/GUID；槽→资产映射属 **G3 前置裁决（B5-G0-A）**；G1 Reader 不猜路径 | G3 |
| 无资产定位 | Reader 只收字节；`current`/revision→path→bytes 的入口属 G2/G4 | G2/G4 |
| 空 Mesh | 结构合法（并接受）；`Mesh::IsValid()` 语义与 GPU 跳过逻辑属 G2/G4 | G2/G4 |
| 3 的倍数约束 | v1 writer 仅输出三角形索引；若未来格式变化须 bump formatVersion | 格式升级 |
| flags→Vertex3D 映射 | 本 Gate 不映射；缺省法线/切线策略 G2/G4 冻结 | G2/G4 |

**G1 结论**：B3/B4 实际写出的 Mesh v1 数据可被 Reader 完整、确定性、fail-closed 地恢复；flags 与 metadata 往返保留。G1 PASS — SCOPE-BOUND。

**未提交、未推送（远端 0/0）**；dirty 子模块 ×3 与 `crashes/`（115）原样保留；未开始 G2 及后续。
