# P6-1B Gate B1 — glTF/GLB Parser & Import IR Record

**Status**: PASS — PARSER + IR IMPLEMENTED; 42/42 (Debug & Release); FULL BASELINE 1050 DISCOVERED = 1050 EXECUTED / 1048 PASS / 1 FAIL (PICK-01, unchanged) / 1 SKIP  
**Branch**: avalonia  
**Base**: 124cdd0 (B0)  
**Date**: 2026-11-05  
**Authorization**: B1 (glTF/GLB parser + IR + test target)；未做 OBJ/MTL、提交链、运行时 Mesh、GL 路径、C ABI、TexturePreprocessRule  
**Amendment applied**: `docs/P6-P1A-AMENDED-Granularity.md`（C-1 方案 (a)；C-2 语义将于 B3 enforcement）

---

## 1. 文件级变更清单

| 新增/修改 | 文件 | 内容 |
|---|---|---|
| 新增 | `engine/src/AssetImport/ImportLimits.h` | 合同 §7 上限常量集中点（512MB/64 buffers/64 images/128 meshes/1024 primitives/depth 64/16M vertices/65536 nodes） |
| 新增 | `engine/src/AssetImport/ImportDiagnostic.h` | ImportSeverity/ImportDiagnostic + 30 个稳定 code 常量 |
| 新增 | `engine/src/AssetImport/MeshImportIR.h` | 合同 §2 IR：ImportVertex/MeshPrimitiveIR/MaterialIR/TextureImageIR/TextureReferenceIR/MeshImportIR/ImportResult |
| 新增 | `engine/src/AssetImport/GlbContainerReader.h/.cpp` | GLB 2.0 容器读取（magic/version/length/chunk 对齐/边界/序） |
| 新增 | `engine/src/AssetImport/GltfParser.h/.cpp` | glTF 2.0 JSON → IR：buffers/URI/accessor/材质/纹理/场景遍历/节点烘焙 |
| 新增 | `tests/test_assetimport/TestSupport.h` | fixture builder（nlohmann-driven；GLB packer；临时目录；IR 比对） |
| 新增 | `tests/test_assetimport/GltfParserTest.cpp` | 12 positive/behaviour 测试 |
| 新增 | `tests/test_assetimport/SecurityAndDeterminismTest.cpp` | 30 security/bad-input/determinism 测试 |
| 修改 | `engine/CMakeLists.txt` | GLOB 增加 `src/AssetImport/*.cpp`（最小构建配置改动） |
| 修改 | `tests/CMakeLists.txt` | 注册 `test_assetimport`（链接 COMMON_LIBS；include engine/src；TIMEOUT 180） |

未触碰：`bridge/src/*`、`ContentRegistry`、`RevisionStore`、`EditorSession`、`Mesh/MeshComponent/MeshRenderer`、GL 绘制、`AssetPipeline`、third_party 子模块、任何 ABI/序列化合同/历史记录。

## 2. 实现语义决定（记录为 B1 冻结事实）

| # | 决定 | 依据 |
|---|---|---|
| D1 | **UV 翻转（v' = 1-v）在解析时进行** —— 单一转换点 | 引擎运行时 `stbi_set_flip_vertically_on_load(true)`（OpenGLTexture.cpp:27,101，B0 U-3/C-3）；导入端抵消，使 GLV 方向语义与 glTF 顶底正确对应 |
| D2 | `data:` URI（buffer 与 image）一律拒绝（GLB 内嵌 bufferView 图像允许记录不解码） | 合同 §7 v1 URI 规则；B4 负责内嵌图像解码 |
| D3 | IR `images[]` 只收集被材质引用的图像，并按 shader 文档顺序；材质纹理引用 remap 到 images[] 索引 | 合同 §2 资源引用稳定 |
| D4 | 逐 primitive 扩展名字段（extensions）→ error（v1 无受支持 primitive 扩展）；`extensionsRequired` 未知 → error；`extensionsUsed` 未 required → warning | 合同 §3 "明确报告不静默" |
| D5 | `COLOR_*` 顶点色 → warning 丢弃（VERTEX_COLOR_DROPPED）；`TEXCOORD_n (n!=0)` → error | 合同 §1 支持面 |
| D6 | interleaved byteStride（v1 视角）→ error；sparse accessor → error；normalized 整型 accessor → error（float 视角仅 FLOAT 5126） | 合同 §3/§4.7 限制 |
| D7 | 索引组件 5121/5123/5125 全支持；无索引 primitive 生成顺序索引；索引值≥posCount → error | roadmap §4.3 |
| D8 | 节点变换：`matrix`（16 列主序）或 `T*R*S`（glTF 四元数 JSON 序 [x,y,z,w]）→ `world = parent*local`，顶点/法线烘焙（法线归一化，退化警告） | 合同 §5 单一转换点 |
| D9 | JSON 整数字段接受 signed/unsigned（非负）；JSON pointer 违规（child index 等）→ error | 工程兼容性（多产编码器整型编码不一致）|
| D10 | 场景选择：`scene` 默认 或 scenes[0]；多场景跳过 warning；camera/light/empty 节点 warning | 合同 §1 |
| D11 | 幂等基数：primitive 在 traversal 下逐实例推送（多节点同一 mesh → 独立实例）；**primitive 顺序与材质槽关联完全保留**（span materialIndex + sourceTransform + nodeName 记录） | C-1 修订（待 B3 spans 落地） |

## 3. 合同逐项对照（IR vs P6-P1A）

| 合同条款 | 落点 | 状态 |
|---|---|---|
| §2 ImportVertex（pos/normal/uv0/tangent + valid flags） | MeshImportIR.h ImportVertex | ✅ |
| §2 primitives（firstIndex/indexCount/materialIndex）+ sourceTransform/nodeName（B3 spans 需要的关联） | MeshPrimitiveIR | ✅ |
| §2 materials（PBR metal-roughness 全字段 + alphaMode/cutoff + 5 类纹理引用） | MaterialIR | ✅ |
| §2 节点烘焙/层级不保留 | parser 内一次性 bake（D8/D11） | ✅ |
| §2 zero-new-dependency | nlohmann 仅 | ✅ |
| §3 accessor type/componentType/count/stride/bounds 校验 | ResolveAccessor 全覆盖 | ✅ |
| §3 URI 安全（同目录相对；拒绝 data:/scheme/traversal/%/特殊字符） | ValidateUri + ReportUriFailure | ✅ |
| §4 (preprocess 部分未做，B4 范围) | — | (B4) |
| §5 坐标恒等 / 单转换点 / 列主序 | 烘焙函数 + 单元测试验证（旋/缩/位移链） | ✅ |
| §7 安全上限（512MB/64/64/128/1024/64/16M/65536） | ImportLimits.h 实施于解析 | ✅ |
| §8 测试矩阵（parser/几何/材质/原子性部分） | 42 测试（其余组在后续 Gate） | 部分（Gate-by-Gate） |

## 4. 测试坐标

| 配置 | test_assetimport | 全量（tools/test_inventory.ps1） |
|---|---|---|
| Debug | **42/42 PASS** | 1050 发现 = 1050 执行；**1048 PASS / 1 FAIL**（PICK-01，行为不变）/ **1 SKIP**（ShaderCompileRule 环境条件） |
| Release | **42/42 PASS** | 同上（一致） |

新增失败/跳过：无。构建：EngineCore Debug+Release 0 error（仅**既有** LNK4006 ×2 警告照旧，P5 收口已在案）。

## 5. 开发期间发现并修复的实现缺陷（过程事实）

1. `ReportUriFailure` switch 缺 `case Ok` → 合法 URI 误报 INVALID（首个大失败来源）。
2. `GetU32` 失败时 out 未重置 → TANGENT 缺失时误指 POSITION accessor。
3. `ResolveAccessor` 形参与局部变量重名（C2082）。
4. `ParserState` 初版无 buffers 所有权 → 全链带 buffers 参数的错误构型。
5. GLB padding/length 语义：chunk length 必须含 padding（Reader 校验严格，fixture 重建）。
6. nlohmann 正整数被存 number_integer（fixture 侧 unsigned 显式化 + parser GetU32 integer 宽容）。

## 6. 停止声明与下一步边界

按授权停止：**不进入 B2**（OBJ/MTL parser）。

待授权事项：
- B2（OBJ/MTL → 同一 IR；含三角化/顶点去重/MTL 语义）
- B3（Model/Workflow 提交链接入：类型阶梯、hash 幂等、reimport 原子性验证 + C-2 enforcement + mesh/Material assets 单次 publish）
- B5 前置读：`m_MeshCache` 复用/生命周期语义（U-2，B0 遗留）
EOF
