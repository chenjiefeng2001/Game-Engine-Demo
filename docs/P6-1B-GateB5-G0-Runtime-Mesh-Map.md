# P6-1B Gate B5-G0 — Runtime Mesh Read-Only Map

**Status**: COMPLETE — READ-ONLY MAP — STOPPED  
**Branch**: avalonia  
**HEAD**: `124cdd0`（实测）  
**Date**: 2026-11-06  
**Precondition**: B1–B4 PASS；全量基线 1114 discovered / 1114 executed（1112 PASS / 1 FAIL=PICK-01 / 1 SKIP）  
**Authorization**: 只读核查；本 Gate 未修改任何源码/测试/CMake/ABI/合同；仅新增本文件。

---

## 0. 工作区保护状态（实测）

| 项 | 值 |
|---|---|
| branch | `avalonia` |
| HEAD | `124cdd02e2e84e1eda5f7d20ad2b94d8c89a8a0c` = `124cdd0` |
| ahead/behind | **0 / 0**（未推送任何本地提交） |
| dirty 子模块 | `third_party/d3d12ma`（1d86c11）、`third_party/openal-soft`（9fb7bb38）、`third_party/vma`（3aa9212）——原样保留 |
| crashes/ | **115** 项，原样保留 |
| 本 Gate 唯一新增 | `docs/P6-1B-GateB5-G0-Runtime-Mesh-Map.md`（此前不存在，未覆盖） |
| 未执行 | build / test / git 写操作（本 Gate 为静态事实核查） |

---

## 1. Mesh 资产字节格式与 Reader 现状

### 1.1 写入方（B3/B4，唯一写路径）

`EditorSession::ImportModelAsset` 调用 `Engine::AssetImport::SerializeMeshAssetJson`（`bridge/src/EditorSession.cpp:1376`）与 `SerializeMaterialAssetJson`（`:1451`）。

写出器：`engine/src/AssetImport/MeshAssetWriter.cpp`（`formatVersion = 1`，`MeshAssetWriter.h:12`）。

**Mesh 资产是 JSON 文档**（非二进制、无自定义头部/魔数），路径规则（B3 决策 D-B3-3/4）：
```
<stemDir>/<base>.<digest8>.mesh                    // Mesh
<stemDir>/<base>.m<slot>.<mhash8>.material.json    // Material（每源材质一个）
<stemDir>/tex/<sanit>.<role>.<pngHash8>.tex.png    // B4 Texture（规范化 PNG）
<stemDir>/tex/<...>.tex.png.tex.meta.json          // B4 sidecar（colorspace/role/ruleVersion）
```
- 工程内源：`stemDir` = 源文件所在项目目录；根外源：`assets/imported/model/<base>_<absPathHash8>/`。
- `digest8` = `gd-import-digest-v2`（B4）前 8 hex；`mhash8` = material JSON 内容 hash；`pngHash8` = 规范化 PNG 字节 hash —— 均为内容寻址，重导不覆盖旧字节。
- `format`/`formatVersion` 字段存在于 JSON 内部（`MeshAssetWriter.cpp:30-32`），**不是**文件头。

### 1.2 Mesh JSON 实际字段（读侧必须消费）

| 字段 | 类型/来源 | 说明 |
|---|---|---|
| `format` | `"mesh"` | 区分 Mesh/Material |
| `formatVersion` | `1` | 版本门 |
| `sourceFile` | 源相对路径 | 诊断用 |
| `vertices[]` | 对象数组 | 每项：`position[3]`、`normal[3]`、`uv0[2]`、`tangent[3]`、`flags`（位：normal=1,tangent=2,uv=4，`MeshImportIR.h`） |
| `indices[]` | uint32（JSON number） | 全局索引缓冲，顺序 = 顶点索引 |
| `primitives[]` | 对象数组 | `firstIndex`、`indexCount`、`materialIndex`（可为 null=sentinel 0xFFFFFFFF）、`nodeName`、`sourceTransform[16]`（列主序） |
| `materialNames[]` | 源材质名 | **仅名称，不含 GUID / 资产路径** |
| `imageUris[]` | `{uri, mimeType}` | 诊断信息（非运行时定位） |

**关键事实**：写入端**不使用 44B `Vertex3D` 内存镜像**，而是逐字段 JSON 数字；读侧需按字段重建 `Vertex3D`（`position/normal/texCoord(uv0)/tangent`，`sizeof=44`，`engine/include/Engine/Core/Renderer/Mesh.h:17-22`）。`flags` 无 `Vertex3D` 对应位，B5 读取器须映射（退化 normal/tangent 处理需合同裁决，见 §11）。

**索引**：`uint32`，数量 = `indices.size()`，按数组序编码（JSON），非二进制 offset。

**Material JSON**（`MeshAssetWriter.cpp:79-106`）：`slot/name/baseColorFactor[4]/metallicFactor/roughnessFactor/emissiveFactor[3]/alphaCutoff/alphaMode` + 五个纹理槽 `{present, imageIndex, assetPath?}`（`assetPath` = B4 解析后的 `.tex.png` 资产路径；`MeshAssetWriter.cpp:20-27`）。

### 1.3 Reader 现状

- 全仓检索 `Deserialize/ParseMesh/LoadMeshAsset/kMeshAssetFormatVersion` 消费端：**不存在任何 Mesh asset reader**（`MeshAssetWriter.h:14-16` 仅有 `Serialize*` 声明；`rg` 仅命中写入方与无关的 `SceneSerializer/ViewportSerializer/Skinning/BlendTree` Deserialize）。
- **状态：`MISSING`（NOT IMPLEMENTED）**。B3 记录 §7 已登记“B5 需要 reader（B3 只写）”。
- 读写字节兼容性：写入格式自洽且确定性可测（B3/B4 确定性测试），但**读侧尚未存在**，不可能验证“已具备读取能力”。不得由“导入成功”推断运行时可读。

### 1.4 运行时定位 Mesh 资产的可用信息（B5-G1 前置判断）

- 注册表（ContentRegistry/manifest）持有 `{guid, path, type=Mesh|Material|Texture}`；路径即上述内容寻址文件。
- Mesh JSON 内部**没有** Material 资产路径/GUID，只有 `materialNames[]`；槽→材质需在注册表中按 `<stemDir>/<base>.m<slot>.` 前缀解析（导入规则 D-B3-6 的命名约定），或另行授权扩展写入器。**B5-G3 绑定前必须冻结此规则。**
- 引擎运行时（非 bridge）当前**没有** RevisionStore 读取入口；`current` 由 `EditorSession`（bridge）读取。运行时加载 API 由谁提供（引擎侧 path 级 reader vs bridge 传路径）未定，属 B5-G1 合同事项。

---

## 2. `m_MeshCache` 语义（`engine/src/Core/RHI/MeshRenderer.cpp`）

| 问题 | 事实 | 证据/状态 |
|---|---|---|
| 声明 | `std::unordered_map<uint64, CachedMeshData> m_MeshCache;` | `MeshRenderer.h:271-279` — VERIFIED |
| Key | `reinterpret_cast<uint64>(mesh.get())` —— **CPU Mesh 对象指针**，非 GUID/路径/revision/hash | `MeshRenderer.cpp:46-50` — VERIFIED |
| Value | **仅 GPU handles**：`vao/vbo/ibo`（shared_ptr<VertexArray/VertexBuffer/IndexBuffer>）+ `indexCount`；无 CPU 数据 | `MeshRenderer.h:273-277` — VERIFIED |
| 创建 | 懒上传：`UploadMesh`（`MeshRenderer.cpp:46-88`）在首帧 draw 时 `GenBuffers/GenVertexArrays/BufferData`，缓存写入 `:85` | VERIFIED |
| 复用 | 命中 key 直接返回（`:50-52`），每帧 `Render` 主线 `:284-298` 先 `find`，miss 时 `UploadMesh` | VERIFIED |
| 失效 | **无失效/eviction/clear**：`rg m_MeshCache` 仅 find/insert；`MeshRenderer::~MeshRenderer` 无清理（`MeshRenderer.cpp:29-32`），cache entry 随后随 shared_ptr 析构释放。**reimport/revision 变化无任何键或世代关联** | VERIFIED（无失效机制） |
| 重导判断 | 旧 revision 是否可复用：**无法判断**——key 为对象指针，与 revision 无关；B5 若重建 Mesh 对象将产生新 key（旧 entry 泄漏至 renderer 销毁） | VERIFIED（设计缺口） |
| 销毁 | 依赖 `shared_ptr` 析构；GL delete 在 `OpenGLVertexArray/VertexBuffer/IndexBuffer` 析构中**无 context 守卫**（`OpenGLVertexArray.cpp:12-14`、`OpenGLVertexBuffer.cpp:12-14`、`OpenGLIndexBuffer.cpp:13-15`）；正常 Shutdown 保证 context current 后才析构 Application（见 §4） | VERIFIED（析构路径）/ PARTIAL（任意路径无守卫） |
| 跨 Scene/Project/Renderer | Cache 属 `MeshRenderer` 实例（`Application` 持有 `shared_ptr<MeshRenderer>`）；`Application::Shutdown/reset` 后销毁 → 不跨 Project；不跨 Renderer | VERIFIED |
| 多 MeshComponent 同 Mesh | 指针相同 → 共享同一 cache entry 与 GPU handles | VERIFIED |
| 失败状态 | `UploadMesh`：`!mesh || !IsValid()` 返回 0（不缓存）；调用点 `if (id==0) continue;` 不绘制。但 `CreateVertexBuffer/IndexBuffer/VertexArray` 返回值**未检查**（部分创建回滚未知） | PARTIAL / UNKNOWN（回滚未证明） |
| RHI 变体 | `m_MeshCache_RHI`（`MeshRenderer.h:289-297`，`MeshRenderer.cpp:773-813`）仅 `Render_RHI` 使用；**生产路径（Application.cpp:1027/1057）调用 `Render`（GL legacy）** | VERIFIED（生产用 GL 缓存） |

> 结论：缓存 key/世代语义**不足以支撑 revision-aware 运行时加载**。B5 必须决定缓存身份（推荐 `Mesh asset path（含 digest8）` 或 `GUID+digest`），并引入失效点；本 Gate 不修改。

---

## 3. MeshComponent 生命周期与 Scene 持久化

| 项 | 事实 | 状态 |
|---|---|---|
| 定义 | `engine/include/Engine/Core/GameObject/MeshComponent.h:15-55`；持 `std::shared_ptr<Mesh>`（CPU）、`m_Color/m_Visible/m_NormalMap/m_HeightMap/…` | VERIFIED |
| 资产身份 | **无 GUID / 无路径字段**；仅内存 `Mesh` 对象 | VERIFIED — 持久化缺口 |
| 序列化 | `Component::Serialize` 默认空实现（`Component.h:82-84`）；MeshComponent **未 override** `Serialize/Deserialize`，也**未 override `GetComponentTypeName`**（`Component.h:116` 默认 nullptr） | VERIFIED |
| Scene v1 持久化 | `SceneSerializerV1` 仅序列化 `GetComponentTypeName()!=nullptr` 的组件（`SceneSerializerV1.cpp:134-143`）；`ComponentRegistryGo` 仅注册 Camera/Collider/RigidBody/Skinning（`ComponentRegistry_Go.cpp:28-36`）→ **MeshComponent 既不写入也不恢复** | VERIFIED — MISSING |
| 遗留 YAML 序列化 | `SceneSerializer.h:72-73` 声明 `SerializeMeshComponent/DeserializeMeshComponent`，**全仓无实现**（死声明） | VERIFIED |
| 创建/替换/移除 | `SetMesh` 仅替换 shared_ptr（`MeshComponent.h:21-23`）；无卸载钩子；GPU 资源由 renderer cache 按旧指针持有 | VERIFIED |
| Missing asset | 无资产引用，故无“missing”语义；无 Mesh 的 Component 由 `HasMesh()` false 跳过 | VERIFIED |
| GPU 所有权 | Component 只持有 CPU Mesh；GPU 由 `MeshRenderer::m_MeshCache` 拥有 | VERIFIED |
| Direct/RenderGraph 收敛 | 两条生产路径都从 `scene.GetObjects()` 收集 `MeshComponent`（`Application.cpp:944-951`），同一 `m_Production3DMeshRenderer` 调 `Render(meshObjects)`（RG pass `Application.cpp:1025-1027`；Direct `:1055-1057`） | VERIFIED（B0 结论在 HEAD 仍成立） |
| Runtime 加载路径 | **不存在**：无任何代码从 `.mesh`/GUID 构造 Mesh 并 `SetMesh`（`SetMesh` 仅见 sandbox 程序化网格） | MISSING |

---

## 4. OpenGL context / GPU 资源所有权 / 线程

| 项 | 事实 | 状态 |
|---|---|---|
| Context 创建 | `EngineHost::InitializeGraphics` 创建 `OpenGLGraphicsFactory(true)` + `InitializePersistentHiddenContext`（`bridge/src/EngineHost.cpp:322-350`） | VERIFIED |
| Owner thread | 持久 context 在**创建线程**建立，随后 `Uncurrent`；`m_RuntimeThread = std::jthread(&EngineHost::RuntimeLoop)`（`:753`）在 **runtime 线程** `MakePersistentContextCurrent` 后执行渲染/资源操作（bootstrap 命令 `:806`；每帧 pump） | VERIFIED |
| GL 资源创建 | 生产 draw 在 runtime 线程；`MeshRenderer::UploadMesh` → `m_Factory.CreateVertexBuffer/IndexBuffer/VertexArray` → GL Gen/BufferData | VERIFIED |
| 销毁顺序（正常） | Shutdown 向 runtime 线程投递命令：`MakeRuntimeContextCurrent()` → `TeardownRuntimeResources()`（session/FBO/Application 依次 reset，MeshRenderer/cache 随 Application 析构，GL delete 在 context current 下）→ `UncurrentPersistentContext()`（`EngineHost.cpp:1595-1615`；teardown `:607-668`） | VERIFIED |
| FAULTED 路径 | 先 `MakePersistentContextCurrent` 再 teardown，失败降级记录（`:1498-1520` 注释与代码） | VERIFIED |
| Deferred deletion | 无统一 deferred 机制；依赖“context current 的 teardown 命令”这一约定 | PARTIAL（仅生命周期约定） |
| GL 资源析构守卫 | `OpenGLVertexArray::~` / `~OpenGLVertexBuffer` / `~OpenGLIndexBuffer` 直接 `Delete*`，**不检查 context/线程**；若在错误时机（如异常路径）析构会 UB | VERIFIED（无守卫）/ 风险项 |
| 上传失败回滚 | `CreateVertexBuffer/IndexBuffer/VertexArray` 返回值未检查；`UploadMesh` 无 try/rollback；失败时 `id=0` 依赖调用点跳过，但部分创建的对象可能未释放（shared_ptr 析构释放已创建部分） | PARTIAL |
| Direct/RG 线程 | 两者均同一 runtime 线程同一 renderer 实例 | VERIFIED |

---

## 5. 从 Committed Asset 到 Renderer 的调用链

| 环节 | 现状 | 证据 |
|---|---|---|
| Committed Mesh Asset（`current` manifest → 路径） | 注册表/manifest 存在；引擎侧无 reader | bridge `EditorSession`（RevisionStore）— bridge 内 IMPLEMENTED；engine runtime 侧 **MISSING** |
| Asset/Revision Read（runtime） | 无引擎运行时入口（`current` 仅 bridge 读取） | **MISSING** |
| Mesh Reader（JSON→CPU `Mesh`） | 不存在 | **MISSING** |
| CPU Mesh Representation | `Engine::Mesh` 存在（`Mesh.h:26-52`） | IMPLEMENTED（构造面） |
| GPU Resource Creation | `MeshRenderer::UploadMesh` 在 runtime 线程懒上传（指针键 cache） | **IMPLEMENTED**（GL 路径） |
| MeshComponent | 可持有 CPU Mesh，但无资产引用/无持久化/无加载器 | **PARTIAL**（仅内存注入） |
| Direct Renderer | `Application.cpp:1055-1057` → `MeshRenderer::Render` | IMPLEMENTED |
| RenderGraph Renderer | `Application.cpp:1011-1028` pass → 同一 `Render` | IMPLEMENTED |
| Texture（B4 产物）运行时消费 | sidecar 由 `OpenGLGraphicsFactory::CreateTexture` 读取并选 sRGB/Linear 内部格式（`OpenGLGraphicsFactory.cpp:374-395`；`OpenGLTexture.cpp:39-45/117-122`） | IMPLEMENTED（路径级） |
| Material runtime binding | Material JSON 无引擎侧 reader；`MeshRenderer` 使用 `MeshComponent.m_Color/…`，不读材质资产 | **MISSING** |

**断点（准确）**：
1. `current → Mesh asset path → Mesh JSON reader → CPU Mesh` 整段缺失（B5-G1 职责）。
2. `MeshComponent ↔ Mesh asset（GUID/路径）` 关联缺失（B5-G4/B6 职责；当前无字段、无持久化）。
3. `Material/Texture asset binding` 缺失（B5-G3；Mesh JSON 仅 materialNames，材质→纹理 assetPath 已有，但无 reader）。
4. 缓存身份与 revision 无关联（§2）。

---

## 6. B4 兼容性约束（本 Gate 复核结论）

- Digest schema = `gd-import-digest-v2`（`ImportDigest.h:20-35`）。B4 texture dependency identity + bytes hash 已进入 digest（color/data 分列；GLB 内嵌 `glbimg<docIndex>`；外部 normalized URI）。
- 同一图片 color/data 双用途 = **两个独立 Texture 资产身份**（不同 `role/pngHash8` 路径），不得在运行时合并或让后写覆盖。
- 运行时必须以实际 sidecar metadata 决定上传格式（`colorspace: sRGB|linear`）；**不得**再对 sRGB 字节做 CPU gamma，避免双重转换。
- Texture 资产路径携带 `.tex.png` 规范化输出；material JSON 的 `assetPath` 指向它。
- 任何 B5 读取器必须拒绝 `formatVersion` 非 1 或缺失字段（不得把损坏资产渲染成空 Mesh）。

---

## 7. 阻塞项与需裁决事项（进入 B5-G1 之前）

| # | 事项 | 影响 | 现状/建议 |
|---|---|---|---|
| B5-G0-A | Mesh JSON 无 Material 资产路径/GUID（仅 names） | G3 绑定需前缀扫描约定或写入器扩展（改动 B3 输出格式） | 需裁决：接受前缀约定（不修改 B3 格式）或授权 additive 字段 |
| B5-G0-B | 无 runtime revision/asset 读取 API（`current` 仅 bridge） | G1 读取器入口归属 | 需裁决：引擎侧 path reader（bridge 提供 path）vs bridge 传字节 |
| B5-G0-C | 顶点 `flags`（normal/tangent/uv 有效性）无 `Vertex3D` 表达 | G1 需定义映射（缺失法线=零/重建？） | 建议 G1 合同记录：缺失即零向量 + 标记，不擅自重建 |
| B5-G0-D | 缓存 key 为指针、无失效 | 重导后必须不复用旧 GPU | G1 决策缓存身份（推荐 path/digest）+ 失效点 |
| B5-G0-E | MeshComponent 无资产引用/无序列化 | G4/B6 全部场景加载不可能 | B5-G4 单独授权 additive 字段；B6 持久化 |
| B5-G0-F | GL 资源析构无 context 守卫 | 异常路径存在 UB 风险 | 记录为已知风险；G2 实现时保证 teardown 路径，不扩改全局资源层 |
| B5-G0-G | 多材质 v1 首材质策略 | 渲染 source 用首材质；全部 span/material 保留 | G3 按 B3/C-1 决策执行并显式报告 |
| B5-G0-H | UploadMesh 部分失败回滚未验证 | 资源泄漏风险 | G2 测试覆盖并给证据（不预设实现） |

**未发现**：无法在现有 candidate 原子表达等 B3 级阻塞在本 Gate 无新增（B5 不涉及提交链）。

---

## 8. 推荐的 B5-G1 实施切分（仅建议，未获批准）

1. **G1 合同冻结**：读取器入口/归属（B5-G0-B）、flags 映射（C）、缓存身份（D）、material 前缀解析（A）。
2. **Reader（无 GL）**：`formatVersion` 严格、offset/count/索引越界校验；round-trip 与 B3 writer 字节确定；失败 `MISSING` 语义。
3. **GPU 创建**（G2）：真实 context、VAO/VBO/EBO、失败回滚测试（H）、context-current 约束（F）。
4. **绑定/缓存**（G2/G3）：revision-aware key + 失效；纹理经 B4 sidecar 路径加载；首材质策略。
5. **MeshComponent 资产关联**（G4，需授权 additively）与 GL 像素读回（G5）。
6. **回归**（G6）：以 B4 后 1114 为基线。

---

## 9. 证据索引（关键锚点）

| 事实 | 锚点 |
|---|---|
| Mesh/Material JSON 写格式 | `engine/src/AssetImport/MeshAssetWriter.cpp:29-106`; `MeshAssetWriter.h:12-16` |
| 导入侧写资产/命名/单次 Publish | `bridge/src/EditorSession.cpp:1376,1451,1300-1470` |
| `Vertex3D` 44B | `engine/include/Engine/Core/Renderer/Mesh.h:17-22` |
| `m_MeshCache` 声明/取值 | `engine/include/Engine/Core/RHI/MeshRenderer.h:271-279`; `engine/src/Core/RHI/MeshRenderer.cpp:46-88` |
| 生产收集链（MeshComponent） | `engine/src/Core/Application.cpp:944-951` |
| Direct / RenderGraph 同一 `Render` | `Application.cpp:1025-1027`（RG）、`:1055-1057`（Direct） |
| MeshComponent（无序列化/无 GUID） | `engine/include/Engine/Core/GameObject/MeshComponent.h:15-55`; `Component.h:82-84,116` |
| Scene v1 仅序列化 contract 组件 | `engine/src/Core/Content/SceneSerializerV1.cpp:134-143`; `ComponentRegistry_Go.cpp:28-36` |
| GL context owner thread / teardown 顺序 | `bridge/src/EngineHost.cpp:322-350,753,806,1498-1520,1595-1615,607-668` |
| GL 资源析构无守卫 | `engine/src/OpenGL/Resources/OpenGLVertexArray.cpp:12-14`; `OpenGLVertexBuffer.cpp:12-14`; `OpenGLIndexBuffer.cpp:13-15` |
| B4 sRGB/Linear 上传选择与 sidecar 消费 | `engine/src/OpenGL/OpenGLGraphicsFactory.cpp:374-395`; `engine/src/OpenGL/Resources/OpenGLTexture.cpp:39-45,117-122` |
| B4 digest v2 | `engine/src/AssetImport/ImportDigest.h:20-35` |

---

## 10. Gate 结论

- Mesh 资产字节格式**可安全读取的前提已具备**（格式自洽、版本字段、内容寻址、确定性），但 **reader 不存在（MISSING）**。
- `m_MeshCache` **不具备 revision/lifecycle 语义**（指针键、无失效、无世代）——B5-G1/G2 必须先冻结缓存身份。
- GPU 资源可在正确 GL 所有权上下文中创建/销毁（正常与 FAULTED 路径均有 context-current 保障），但资源析构函数本身无守卫（风险记录）。
- MeshComponent 与资产/持久化链路整体为 `MISSING`，属 B5-G4/B6 授权范围。
- **本 Gate 无代码/构建/提交变更**；仅新增本 Map。

`P6-1B Gate B5-G0 — COMPLETE — READ-ONLY MAP — STOPPED`
