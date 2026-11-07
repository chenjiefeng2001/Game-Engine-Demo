# P6-1B Gate B5-G2 — GPU Mesh Resource + Cache Closure

**Status**: PASS — SCOPE-BOUND  
**Branch**: avalonia  
**HEAD**: `124cdd0`（实测；未提交、未推送，远端 0/0）  
**Date**: 2026-11-06  
**Precondition**: B5-G1 PASS（基线 1137 discovered）

---

## 1. G2-A Preflight 核查结论

### 1.1 Vertex3D / VertexInput 合同

- `struct Vertex3D { Vec3 position; Vec3 normal; Vec2 texCoord; Vec3 tangent; }`（`engine/include/Engine/Core/Renderer/Mesh.h:17-22`）。
- **sizeof = 44B**，offset：position=0、normal=12、texCoord=24、tangent=32（G2 代码以 `static_assert` 固化，`MeshGpuResource.cpp:15-18`）。
- VAO attribute 合同（既有 `MeshRenderer::UploadMesh` 与 shader layout 一致）：
  `{0,3,44,0} {1,3,44,12} {2,2,44,24} {3,3,44,32}`（attribute location / component count / stride / offset）；3d_lit/pbr_lit shader 使用 location 0..3。
- 所有现有 Vertex3D 生产者均为 engine `Mesh`（程序化 Create*、sandbox）。**生产 GL 路径此前没有“缺失属性”语义**（字段恒在）。
- G1 flags（normal/tangent/uv 有效性位）**保留在资源 metadata**（`MeshGpuResource::VertexFlags()`），**不写入 44B 顶点**、不塞入任何现有字段；不因数值为零推断缺失；不合成 normal/tangent。数值按导入 IR 原样上传（缺失即零向量——与既有无 fallback 的引擎行为一致，G4 若需渲染语义再裁决）。legacy `MeshComponent` 路径（其 Mesh 恒为程序化完整顶点）标记 flags=0x07。

### 1.2 GL Resource API 失败语义

- `IGraphicsFactory::CreateVertexBuffer(float*,uint32)→shared_ptr<VertexBuffer>`、`CreateIndexBuffer(uint32*,uint32)→shared_ptr<IndexBuffer>`、`CreateVertexArray()→shared_ptr<VertexArray>`（`IGraphicsFactory.h:84-92`）。**返回非空不保证 GL 成功**（GL Gen 失败无错误面）；本 Gate 以空返回 + 注入 seam 表达失败，并在发布前做句柄/计数终验（`va->GetIndexBuffer()==ibo && ibo->GetCount()==indices.size()`）。
- 实际 GL 句柄存在于 `OpenGLVertexArray/VertexBuffer/IndexBuffer` 的私有 `m_RendererID`；销毁在其析构函数（**无 context/thread 守卫**——G0 已记录，本 Gate 以 owner/context 门控与 shutdown 顺序保证正确调用点）。
- 为验证“临时句柄零泄漏”，三个 GL wrapper 增加 `static int64_t LiveCount()` 计数器（additive、engine 内部；见 §8 测试）。

### 1.3 Cache 调用链与资源限制

- 旧 GL cache：`MeshRenderer::m_MeshCache`，key=`reinterpret_cast<uint64>(mesh.get())`，value=VAO/VBO/IBO，无失效；生产 Direct/RenderGraph 同 renderer 同 `Render()`。**本 Gate 已完全移除该 GL pointer-key cache**；`m_MeshCache_RHI`（非生产 `Render_RHI` 路径）保持不动并记录。
- GL 上传数量/字节限制（**内部 GPU 预算，不复用 CPU ImportLimits**）：`kMaxGpuMeshVertexBytes=256MiB`、`kMaxGpuMeshIndexBytes=256MiB`、`kMaxGpuMeshDrawCount=0x7FFFFFFF (GLsizei)`；`Create*Buffer` 的 uint32 size 参数另有 `≤0xFFFFFFFF` 检查（`MeshGpuResource.h:49-53`）。

## 2. 文件级变更清单

| 新增/修改 | 文件 | 内容 |
|---|---|---|
| **新增** | `engine/include/Engine/Core/RHI/MeshGpuResource.h` | GPU resource/key/cache/预算常量/失败注入 seam |
| **新增** | `engine/src/Core/RHI/MeshGpuResource.cpp` | 校验、临时创建+回滚、缓存、owner-thread/context 门控、digest、清理 |
| **新增** | `engine/src/AssetImport/MeshUploadAdapter.h/.cpp` | G1 `MeshAsset` → Renderer `Vertex3D`/span/flags 的唯一集中转换（无第二解析器、无 reinterpret_cast） |
| 修改 | `engine/include/Engine/Core/RHI/MeshRenderer.h` / `engine/src/Core/RHI/MeshRenderer.cpp` | 删除 pointer-key GL cache；改用 `MeshGpuCache`；`GetOrCreateGpuMesh`；`ClearGpuResources()`；析构时 Clear |
| 修改 | `engine/include/Engine/Core/Renderer/Mesh.h` / `engine/src/Core/Renderer/Mesh.cpp` | 新增惰性 `GetContentHash()`（FNV-1a over vertex+index bytes）；`SetVertices/SetIndices/RecalculateNormals` 失效哈希 |
| 修改 | `OpenGLVertexArray/VertexBuffer/IndexBuffer`（各 .h/.cpp） | additive `LiveCount()` 观测计数（仅为 G2 回滚证据） |
| 修改 | `tests/CMakeLists.txt` | test_assetimport 增加 G2 测试源 |
| **新增** | `tests/test_assetimport/B5GpuMeshResourceTest.cpp` | 12 项 G2 测试（CPU adapter/GL/cache/回滚/线程） |

**禁止项核查**：未改 Material/Texture reader、MeshComponent、Scene Serializer、Bridge/C ABI、Vulkan/D3D12、writer、import limits、Scene persistence。

## 3. CPU → GPU 转换（集中 adapter）

`BuildMeshUploadInput(const MeshAsset&)`（`MeshUploadAdapter.h`）：
- position/normal/uv0/tangent → `Vertex3D` 逐字段映射；`Vertex3D` 布局/stride 由 `sizeof/offsetof` 表达（不手写常数）。
- indices/primitive spans 原样复制（firstIndex/indexCount/materialIndex 含 sentinel 0xFFFFFFFF）。
- `vertexFlags` 与顶点一一对应复制；**不上传**、不改变 44B 布局。
- 计算 `payloadDigest`（GPU 消费数据 + spans + flags）；空网格/非法 payload 返回明确 error（不进入 GL）。
- Direct/RenderGraph 使用同一 `MeshRenderer`/`GetOrCreateGpuMesh`/cache，无第二套映射。

## 4. GPU 资源与所有权

`MeshGpuResource` 持有 `shared_ptr<VertexArray/VertexBuffer/IndexBuffer>` + `vertexCount/indexCount/vertexBytes/indexBytes/spans/vertexFlags/key`；仅 `MeshGpuCache` 可构造；`IsValid()` 要求三句柄非空。GL handle 绝不出现在 `MeshComponent` / Managed 侧。

## 5. Cache Identity 与生命周期

- **Key** = `{identity:u64, payloadDigest:u64}`（`MeshGpuResourceKey`）。identity 由调用方提供稳定资产身份（G4 接 GUID 折叠；legacy 路径=0）；**不使用对象地址**。
- **payloadDigest** = FNV-1a-64 over：`vertexCount|Vertex3D bytes|indexCount|uint32 index bytes|spanCount|spans(firstIndex,indexCount,materialIndex)|flagCount|flags`（`MeshGpuCache::ComputePayloadDigest`，`MeshGpuResource.cpp`）。与 B4 `gd-import-digest-v2` **职责分离**（后者含纹理依赖/预处理身份）；本 digest 仅几何 GPU 载荷。
- **碰撞处理**：命中时除 key 相等外再校验 `VertexCount/IndexCount/SpanCount` 与入参一致；不一致按碰撞 fail-closed（返回 nullptr），不复用错误资源。FNV-64 理论碰撞作为已记录限制（industry-standard content hash + 尺寸守卫）。
- **复用/变化**：同 identity+digest → 同 shared_ptr；同 identity+新 digest → 新资源（旧条目保留，仍可被旧引用持有）；不同 identity+同 payload → 独立条目（隔离）。
- **Invalidation/Eviction**：`Invalidate(key)`、`InvalidateIdentity(identity)`、`Clear()`；全部要求 **context current + owner thread**，否则返回 false 并保留资源（不遗留仅释放 CPU map 的路径）。`Clear()` 幂等。Renderer 析构调用 `m_GpuMeshCache.Clear()`。
- 不做 Scene 引用计数/revision pinning（G4/B6 范围）；staging 清理仍留 P6-7。

## 6. GL Owner Thread 与 Shutdown

- 创建/失效/清理前检查 `glfwGetCurrentContext()!=nullptr`；首次成功 Acquire 记录 owner thread，此后跨线程请求**直接拒绝且不调用 GL**（测试见 §8）。
- 生产所有权沿用 G0：runtime thread 为 GL owner；正常 Shutdown 在 runtime 线程 `MakeRuntimeContextCurrent → TeardownRuntimeResources（Application/renderer 析构 → cache Clear）→ Uncurrent`；FAULTED 路径同样先 current 再 teardown（`bridge/src/EngineHost.cpp:1595-1615` / `:1498-1520`）。
- GL wrapper 析构无守卫的既有风险未改造全局框架；本 Gate 的资源仅在上述受控路径释放，并用 LiveCount 在测试中证明无泄漏。**记录为已知限制**。

## 7. 上传失败原子性矩阵

流程：`Create VAO → Create VBO → Create IBO → configure attributes → validate → publish`。
注入 seam（`MeshGpuUploadStage`，生产默认 None）：AfterVertexArray/AfterVertexBuffer/AfterIndexBuffer/AfterAttributeConfig。任一步失败：
- 临时 shared_ptr 在函数作用域结束释放（context current 下 GL delete）；
- 不写 cache、不返回资源、不破坏旧条目；
- 后续重试成功。

实测（Debug/Release 均含）：每阶段注失败后 `cache.Size()==0`、`LiveTotal()` 回到基线（VAO/VBO/IBO 计数）；旧条目场景：`Size` 保持 1、旧资源仍 `IsValid`、LiveCount 不增长；解除注入后重试成功。真实 GL 校验：VBO 字节（`GetBufferSubData` 与 CPU memcmp 相等）、IBO 字节、VAO 四属性（size/type/stride/offset 全部断言）、`GetIndexBuffer()->GetCount()`。

## 8. 专项测试（新增 12 项）

| 组 | 用例 |
|---|---|
| CPU adapter | 字段/flag 映射（44B 且 flags 不在布局内）；缺失 normal 不合成；多 span 保留；`ValidatePayload` 拒绝越界索引/非 3 倍/空/span 零长/span 越界/flags 尺寸错；`Mesh::GetContentHash` 内容寻址（同内容同 hash、SetIndices 变更、改回一致） |
| 真实 GL | VBO/IBO 字节正确；VAO 4 属性 layout 与 Vertex3D 合同一致；索引计数；cache 复用（同 key 同对象）；不同 identity 隔离；同 identity 变 payload 新资源旧保留 |
| 生命周期 | Invalidate 释放句柄回基线；Clear 幂等；失败注入 4 阶段零泄漏；新版本失败旧条目完好；非 owner 线程拒绝且无 GL；非法 payload 在 GL 前拒绝 |

## 9. 构建与回归（Debug/Release 一致）

| 项 | Debug | Release |
|---|---|---|
| EngineCore / EditorBridge | 0 error | 0 error |
| `test_assetimport` | **121/121 PASS** | **121/121 PASS** |
| 全量 `test_inventory.ps1` | **1149 discovered = 1149 executed；1147 PASS / 1 FAIL / 1 SKIP** | 同上 |
| editor_avalonia（dotnet） | Build succeeded（NU1900 既有网络 warning） | — |

- G1 基线 1137 → **+12**（全部为本 Gate 新测试，名称可在源文件枚举）。
- **PICK-01 单独核对**：唯一 FAIL = `EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`，断言仍为 `test_bridge/test_pick_transport.cpp(231)`（HIT）与 `(233)`（entity_id≥1）——零漂移、无新增失败。
- SKIP 唯一仍为 `ShaderCompileRule.SkippedWithoutVulkanOrShaderc`（既有 SDK 条件项）。

## 10. 工作区状态与限制

- **未提交、未推送（远端 0/0）**；dirty 子模块 ×3 与 `crashes/` 115 项原样；G1 产物保留。
- `m_MeshCache_RHI`（`Render_RHI`）仍为 pointer-key，但**非生产路径**（Application 仅调用 `Render`）；登记为后续若启用 RHI 生产路径时必须处理。
- 多 span 仅保存/暴露（`Spans()`），生产渲染仍整段绘制一次（首材质 v1 策略；per-span 绘制属 G3/G5）。
- 资产定位（GUID→current→path→bytes）与 MeshComponent 资产字段不在本 Gate（G4）。
- GL wrapper 析构无 context 守卫为既有框架限制（本 Gate 不改造全局框架）。
- 性能：内容哈希惰性一次/ Mesh；每帧仅 key 查找。

**G2 结论**：CPU MeshAsset → 可复用 GPU Mesh Resource 的转换、上传回滚、内容身份缓存与受控清理均已实现并有真实 GL 证据；pointer-key 生产 GL cache 已移除。G2 PASS — SCOPE-BOUND。

**停止**：不进入 B5-G3/G4/G5/G6/B6，等待下一授权。
