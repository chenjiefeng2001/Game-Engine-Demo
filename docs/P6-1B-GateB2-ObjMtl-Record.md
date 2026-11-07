# P6-1B Gate B2 — OBJ/MTL Parser → Shared Import IR Record

> **§0 文件清单纠错（B3 开工时补正，2026-11-05）**：原记录"新增 5 / 修改 4"计数笔误。
> 以 `git status`/实际文件树为准，B2 实际清单为：
> **新增 7**：`engine/src/AssetImport/AssetFileReader.h`、`AssetFileReader.cpp`、`ObjParser.h`、`ObjParser.cpp`、`MtlParser.h`、`MtlParser.cpp`、`tests/test_assetimport/ObjMtlParserTest.cpp`；
> **修改 4**：`engine/src/AssetImport/ImportLimits.h`（+kMaxMaterials）、`ImportDiagnostic.h`（+14 码）、`MeshImportIR.h`（+additive emissiveFactor）、`tests/CMakeLists.txt`（测试源接入）。
> 本纠错不改变已冻结实现语义与 PASS 结论，不重做 B1/B2 审计。

**Status**: PASS — SCOPE-BOUND（OBJ/MTL 与 glTF/GLB 汇入同一 MeshImportIR；未触发布局/提交链/运行时）  
**Branch**: avalonia  
**Base**: 124cdd0（B0/B1 未提交工作区之上，未提交未推送）  
**Date**: 2026-11-05  
**Authorization**: B2（OBJ/MTL parser + 共享 IR + 一致性测试）；未做 B3-B6、ABI、序列化合同、第三方、历史记录变更

---

## 1. 文件级变更清单

| 新增/修改 | 文件 | 职责 |
|---|---|---|
| 新增 | `engine/src/AssetImport/AssetFileReader.h/.cpp` | 共享 URI 校验（data:/scheme/traversal/percent/special-char 拒绝）+ 同目录相对读取 helper（与 glTF 路径等语义；glTF 内部实现保持不动，B3+ 统一） |
| 新增 | `engine/src/AssetImport/ObjParser.h/.cpp` | OBJ 解析：token 化、v/vt/vn 流、面角 `i/k//jk` 变体、负/相对索引、(o,g,usemtl) 分段 span、fan 三角化、(v,vt,vn) 有序 map 去重、per-vertex attribute flags |
| 新增 | `engine/src/AssetImport/MtlParser.h/.cpp` | in-memory .mtl 解析：newmtl/Kd/Ke/d/Tr/map_Kd/map_Ke/bump 族/未知字段 warning/重复材质 first-wins |
| 修改 | `engine/src/AssetImport/ImportLimits.h` | 新增 `kMaxMaterials=4096`（安全上限集中点扩展） |
| 修改 | `engine/src/AssetImport/ImportDiagnostic.h` | 新增 14 个 OBJ/MTL 稳定 code（OBJ_SYNTAX/OBJ_INDEX_OUT_OF_RANGE/OBJ_FACE_DEGENERATE/OBJ_SMOOTH_IGNORED/OBJ_POLYGON_TRIANGULATED/OBJ_NO_FACES/MTL_SYNTAX/MTL_LIB_MISSING/MTL_MATERIAL_MISSING/MTL_DUPLICATE_MATERIAL/LIMIT_MATERIALS 等） |
| 修改 | `engine/src/AssetImport/MeshImportIR.h` | MaterialIR 增加 additive 默认零 `emissiveFactor[3]`（合同 §2 "Emissive"字段完备化；glTF 路径默认零、行为不变，glTF 解析端补齐推迟到 B3 授权） |
| 修改 | `tests/CMakeLists.txt` | `test_assetimport` 源接入 ObjMtlParserTest.cpp |
| 新增 | `tests/test_assetimport/ObjMtlParserTest.cpp` | 27 个 OBJ/MTL 测试（positive/security/determinism/共享 IR 不变量） |

**未触碰**：`GltfParser.cpp`（B1 文件安然）、 rebellion 忍 Reader 之外的桥/内容链/运行时/资产管线/ABI/third_party/历史记录。

## 2. 冻结的实现语义（B2 决定 - D12 系列）

| # | 决定 | 说明 |
|---|---|---|
| D12 | **(v,vt,vn) 元组有序 map 去重**：跨文件唯一顶点；output 由 face 首见次序驱动（无哈希序依赖）。元组含 vt/vn 的 presence 语义 | OBJ 独立索引流安全组合 |
| D13 | **分段规则 = 相邻 face 的 (o,g,usemtl) 元组对比**：变化即新 span（不动源顺序；隔离回溯重入同名组的新 span）。无 usemtl → `MATERIAL_NONE` sentinel `0xFFFFFFFF` + 一次性 warning | 源顺序保留；一个文件一个 Mesh asset（C-1） |
| D14 | **多边形 fan 三角化**（preserve CCW 顶点顺序）+ `OBJ_POLYGON_TRIANGULATED` warning | 合同 §4.4 |
| D15 | **UV 翻转 v' = 1-v 在 OBJ 解析端同样单次应用**（B1 冻结协议） | 一致变换单点 |
| D16 | **负索引** `i_abs = count + idx + 1`；`<=0` / `>count` 均错（OBJ_INDEX_OUT_OF_RANGE；`0` 为非法索引类型） | 合同失效 |
| D17 | **mtllib 失败→warning(MTL_LIB_MISSING)+继续**（faces 继续跑）；**usemtl 未定义→合成材质槽+warning**；重复材质名→first-wins+warning | 不静默丢失材质语义 |
| D18 | **map_* URI 仅同目录相对；data:、绝对、`..`、percent 全拒**；图片不读盘，uri 记录入 images[]（first-reference dedupe），B4 负责解码 | 合同 §7 |
| D19 | `Kd` 4 分量(含 alpha)、`d`/`Tr`（1-Tr）映射 alpha；`Ke`/`map_Ke`→emissive；`Ks/Ns/Ni/illum/Tf/Ka/map_Ks` 等 → `MTL_FIELD_IGNORED` warning（dedupe per directive） | 合同 v1 材质模型边界 |
| D20 | **`s` 平滑组、`l`/`p` line/point primitives** → 单次 warning 后忽略（dedupe）；未知指令 warn once per token | Fail-inform |
| D21 | **MaterialIR 新增 emissiveFactor[3]**（additive）：MTL Ke 落盘；glTF 侧默认零（B1 已冻语义不放宽；contract 完备化缺口，B3/B4 授权后补 glTF 解析） | 合同 §2 |
| D22 | OBJ 顶点上限 cap/vCount/vtCount/vnCount 全部 `kMaxTotalVertices`；spans 上限 `kMaxPrimitives`；材料 `kMaxMaterials`;图片 `kMaxImages`；文件 512MB | 合同 §7 |
| D23 | `vt` 的第 3 分量 w 忽略（记录语义）；面角 `i/`/`1//`/`i/j/`（空段）→OBJ_SYNTAX error | 统一失效语义 |

## 3. IR 一致性证明（合同联动）

- `OUT->materials` = mtlMaterials（文件序 first-wins）+ missing synthetics（first-use order）；`out->images` = 各 mtllib first-reference 序列（imageIndex remap 后覆盖 base offsets）。
- `OBJ↔glTF同一 IR`：SharedIrInvariants test 三顶点/索引 footprint 相等（1e-6）。
- primitive 顺序 = face 首见次序（GroupAndMaterialSegmentation：3-span 源序）。
- shared `MeshPrimitiveIR`：firstIndex/indexCount/materialIndex/nodeName/sourceTransform 全入。

## 4. 测试坐标

| 配置 | test_assetimport | 全量（tools/test_inventory.ps1） |
|---|---|---|
| Debug | **69/69 PASS** | **1077 发现=1077 执行；1075 PASS / 1 FAIL / 1 SKIP**（FAIL 唯一 = `PICK-01`，对照 LastTest.log 一致；SKIP = ShaderCompileRule 环境条件）|
| Release | **69/69 PASS** | 同上（一致） |
| 构建 | EngineCore Debug/Release **0 error 0 新告警**（LNK4006×2 为 P5 在案既有项） | — |

PICK-01 复核：`tests/test_bridge/test_pick_transport.cpp(231/233)` 二断言失败，与 B1 基线逐项一致（无新 FAIL）。

## 5. B2 过程发现并修复的实现缺陷（主 5 项记录在案）

1. MtlParser 尾行（无 `\n`）死循环（`pos<=size` 后无推进）。
2. MTLLIB 材质合并处状态字段命名不一致（`s.materials` vs `s.mtlMaterials`）——编译捕获修正。
3. `ParseFaceCorner` 对 `"1/"`/`"1//"`/`"1/j/"` substring 越界 throw 风险 → 前置 guard 全覆盖。
4. MtlParser `continue` 通道绕过 `HasError` 终检（水位漂移）→ 循环顶部统一断点。
5. `s` 平滑警告只走 pass1；pass2 已无此入口 → 汇聚到 pass1 dedupe 执行。
6. OBJ NO-FACES 的检测（indices 空）为独立 code OBJ_NO_FACES。

## 6. 不变式审计：不做的事（B2 边界复核）

- 未改 EditorSession/ContentRegistry/RevisionStore/导入链（base 端二进制点）。
- 无 GUID/publish/复制/重导实现（B3 范围）。
- 未动运行时 Mesh/MeshComponent/MeshRenderer/GL、TexturePreprocessRule、B5/B6。
- 未做 C ABI/序列化合同/third_party/历史记录修改。
- 未 commit/未 push（远端 0/0）；3 dirty 子模块 + crashes/ 原样。
EOF
