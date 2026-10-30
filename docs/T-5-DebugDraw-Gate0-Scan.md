# T-5 Generic DebugDraw — Gate 0 只读现状扫描

**状态**：`T-5 GATE 0 — PASS — READ-ONLY COMPLETE`（用户 2026-10-31 裁定；依据本报告摘要）
**日期**：2026-10-31
**基线**：HEAD `74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**Gate A 引用**：后续裁决已冻结于 `docs/T-5-DebugDraw-GateA-Contract.md`（2026-10-31，`T-5 Gate A — PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND`）。本扫描仍保持只读快照语义；如与契约冲突，以 Gate A 契约为准。
**Gate B 引用**：实施与定向验证已完成，状态 `T-5 Gate B — PASS — SCOPE-BOUND`；证据与限制见 `docs/T-5-DebugDraw-GateB-Closure-Record.md`。本扫描不因 Gate B 实施而改写 Gate 0 历史结论。
**Gate C 引用**：Sandbox + Legacy 集成已完成；后续 D1/D3 已完成，最终状态 `T-5 v1 — PASS — SCOPE-BOUND / CLOSED`（用户 2026-10-31 裁决）；见 `docs/T-5-DebugDraw-v1-Final-Closure-Record.md`。

**方法**：只读源码、测试、CMake 与已存文档；未修改源码、未构建、未运行测试、未启动图形/音频设备、未执行 Git 写入、未触碰子模块。

**证据边界**：

- 本报告是静态源码扫描，不含运行时验证。凡涉及“实际不可见/不会绘制”的判断，都来自调用顺序与 API 语义推导，必须由 Gate B 的真实像素证据确认。
- 工作树在扫描时已包含 T-7 音频改动与既有编辑器/UI 改动；本扫描没有清理或重置它们。三个子模块显示为 modified：`third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma`；`openal-soft` 的本地差异仅为 `D build/.empty`，未触碰。
- 本文不是 Gate A 契约；第 5 节只是待裁决项与建议，冻结需另立文档。

---

## 0. 结论摘要

1. **通用 DebugDraw 系统不存在。** 仓库只有物理专用接口 `IPhysicsDebugDraw`（2D）与 `IPhysicsDebugDraw3D`（3D），以及两个 OpenGL 实现。没有面向动画、AI、音频、编辑器或任意子系统的公共调试绘制 API。
2. **现有调试绘制的唯一真实消费面是 sandbox。** `engine/src` 内部没有 `SetDebugDraw` 调用点；`bridge/`、`editor_avalonia`、生产 `EngineHost` 路径均没有 DebugDraw 调用链。`Architecture-Runtime-Integration-Inventory.md` 也把 debug draw 标为 `PARTIAL — 仅在 sandbox`。
3. **3D 物理调试绘制在 Debug 配置下才存在，且形状线框实际被丢弃。** `JPH_DEBUG_RENDERER` 只对 Debug 定义；`JoltDebugRenderer::DrawGeometry` 是空实现，`CreateTriangleBatch` 返回空 batch。Jolt 的 `DrawBodies` 中，多数形状经 `Shape::Draw -> DrawGeometry` 表示；这部分不可见。可见的主要是 `DrawWireBox`/`DrawCoordinateSystem` 产生的 AABB 与坐标轴线（部分直接走 `DrawLine` 的形状除外），而不是碰撞体形状线框。
4. **2D/3D 现有实现都直接使用 OpenGL 立即/半立即绘制。** 它们依赖当前 GL 上下文、自己的 shader/VAO/VBO，不经过 RenderGraph、RHI command list 或生产渲染器；没有上下文有效性检查、设备丢失处理或统一的状态恢复。
5. **RHI/RenderGraph 不是当前可用的最小落点。** RenderGraph 仍处于 `SG6 FAIL / ISOLATED`，生产渲染维持直连路径；RHI 的线拓扑支持在三个后端上不一致（GL46 命令列表绘制为空实现、Vulkan 动态拓扑调用被忽略、D3D12 PSO 拓扑类型固定为三角形且后端休眠）。把 DebugDraw 做成 RenderGraph Pass 会把一个未验证的调试功能绑到已知隔离的渲染图上。
6. **生产编辑器没有 3D 视口可以挂调试绘制。** 生产 `EngineHost` 的 `RenderProductionFrame` 是 2D 正交精灵管线；`EditorHostService` 明确声明 EngineHost 模式不提供 viewport/gizmo/picking。Legacy `ViewportPanel` 有 3D 场景回调和网格覆盖层，但不属于生产 Avalonia 编辑器的渲染路径。
7. **编辑器覆盖层只做了网格。** `EditorOverlay::DrawGrid` 可用；`DrawAxisIndicator` 是带“未来迭代”注释的空实现。没有通用线/盒/球/文字覆盖层。
8. **现有“显示碰撞体/线框”开关大多没有接到渲染器。** `GeometryDebugFrameData.showAABB/showOBB/showOcclusionBounds` 只有 PerformanceWindow UI 写入，没有渲染器消费者；`r_wireframe` 命令明确是 `pending renderer integration`；`_3DTestApp` 的 `showColliders` 只画了一个硬编码盒子，不是物理碰撞体。
9. **测试覆盖为零。** `tests/` 中没有 DebugDraw、EditorOverlay、OpenGLPhysicsDebugDraw* 或 JoltDebugRenderer 的测试；`test_physics` 不覆盖调试绘制。现有 `test_pick` 的隐藏 GL 上下文 + FBO + 像素回读设施可以复用为 DebugDraw 像素门禁。
10. **因此 T-5 应新建渲染层服务，而不是继续扩展物理接口或 RenderGraph。** 建议 v1 范围：OpenGL 4.6、仅线段类图元（线/线框盒/线框球/轴/视锥）、显式帧生命周期、所有者线程收集、固定顶点预算、深度开关；生产编辑器接线、3D 文本、实体填充、RHI 多后端与 RenderGraph Pass 全部列为非目标。

---

## 1. 现状清单

### 1.1 物理专用接口

| 接口 | 位置 | 作用 | 主要实现 | 备注 |
|---|---|---|---|---|
| `IPhysicsDebugDraw` | `engine/include/Engine/Core/Physics/IPhysicsDebugDraw.h:42-63` | 2D 多边形/圆/线段/变换/点 | `OpenGLPhysicsDebugDraw` | 与 Box2D `b2Draw` 的 flags 位掩码对应 |
| `IPhysicsDebugDraw3D` | `engine/include/Engine/Core/Physics/IPhysicsDebugDraw3D.h:16-52` | 3D 线/三角/球/盒/胶囊/圆柱/文本/轴 | `OpenGLPhysicsDebugDraw3D` | `DrawText3D` 无实现；solid 模式大多无实现 |
| `IPhysicsWorld::SetDebugDraw/DebugDraw` | `engine/include/Engine/Core/Physics/IPhysicsWorld.h:84-94` | 2D 世界调试绘制入口 | `Box2DPhysicsWorld`；`Bare2DPhysicsWorld` | 接口持有裸指针，不管理生命周期 |
| `IPhysicsWorld3D::SetDebugDraw/DebugDraw` | `engine/include/Engine/Core/Physics/IPhysicsWorld3D.h:120-122` | 3D 世界调试绘制入口 | `JoltPhysicsWorld` | Jolt 路径仅在 Debug 有效 |
| `JoltDebugRenderer` | `engine/include/Engine/Jolt/JoltDebugRenderer.h:22-65` | JPH::DebugRenderer → `IPhysicsDebugDraw3D` 适配 | `engine/src/Jolt/JoltDebugRenderer.cpp` | 16 个 TLS 线程缓冲；Jolt 全局单例约束 |
| `Box2DDebugDraw` | `engine/src/Box2D/Box2DDebugDraw.h` / `.cpp` | b2DebugDraw 回调 → `IPhysicsDebugDraw` | `Box2DPhysicsWorld::DebugDraw` | 私有桥接头 |

### 1.2 实际调用链

#### 2D（Box2D）— sandbox 路径

```text
PhysicsTestApp::Init
  -> new OpenGLPhysicsDebugDraw(glContext)                    // PhysicsTestApp.cpp:242
  -> m_PhysicsWorld->SetDebugDraw(m_DebugDraw.get())          // PhysicsTestApp.cpp:244

PhysicsTestApp::Render
  -> m_DebugDraw->SetViewProjection(camera.GetViewProjectionMatrixPtr())
  -> IPhysicsWorld::DebugDraw()                               // PhysicsTestApp.cpp:448-449
     -> Box2DPhysicsWorld::DebugDraw()                        // Box2DPhysicsWorld.cpp:547-551
        -> b2World_Draw(world, &b2DebugDraw)
           -> Box2DDebugDraw callbacks
              -> IPhysicsDebugDraw::Draw*/DrawSegment
                 -> OpenGLPhysicsDebugDraw::SubmitLines
                    -> GL UseProgram / BufferSubData / DrawArrays
```

同类调用还出现在 `AudioPhysicsSandboxApp.cpp:610-612,1050-1052` 与 `CollisionAudioTestApp.cpp:233-234,405-406`。`Bare2DPhysicsWorld` 是另一条 2D 路径：`Bare2DPhysicsWorld::DebugDraw()` 直接遍历内部 body 并调用 `m_DebugDraw`（`engine/src/Core/Physics/Bare2DPhysicsWorld.cpp:1192-1267`）。

#### 3D（Jolt）— sandbox 路径

```text
RenderTestApp::InitPhysics
  -> new OpenGLPhysicsDebugDraw3D(glContext)                  // RenderTestApp.cpp:223
  -> m_PhysicsWorld->SetDebugDraw(m_PhysicsDebugDraw.get())   // RenderTestApp.cpp:224

物理累积循环（每个 1/60 步）
  -> m_PhysicsDebugDraw->Clear()                              // RenderTestApp.cpp:363
  -> JoltPhysicsWorld::DebugDraw()                            // RenderTestApp.cpp:364
     -> m_DebugDraw->Clear()
     -> JPH::BodyManager::Draw(...)
        -> Shape::Draw -> JoltDebugRenderer::DrawGeometry     // 当前为空实现
        -> DrawWireBox / DrawCoordinateSystem -> DrawLine
     -> m_DebugDraw->Flush()
  -> m_PhysicsWorld->Step(...)                                // RenderTestApp.cpp:368

渲染循环稍后
  -> ctx->ClearColor(...)                                     // RenderTestApp.cpp:391
  -> m_PhysicsDebugDraw->SetViewProjection(vp)                // RenderTestApp.cpp:451
```

**静态顺序问题（需 Gate B 运行时确认）**：`Clear/DebugDraw/Flush` 在物理更新循环中、场景 `ClearColor` 之前执行；`OpenGLContext::ClearColor` 会立即 `glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)`（`engine/src/OpenGL/OpenGLContext.cpp:85-93`）。按此顺序，刚提交的调试线段会被随后场景清除掉，渲染帧中不应出现它们；同时调试绘制使用的是 Step 之前的变换（比场景晚一步）。这不是“已证明的视觉缺陷”，而是调用链直接推出的高风险结论。

#### 生产编辑器 / 旧编辑器

```text
editor_avalonia / EditorHostService
  -> EngineHost::PumpOneFrame
     -> Application::RenderProductionFrame
        -> SceneRenderer::Render(scene)                       // Application.cpp:749
        -> 可选 production pick pass                          // Application.cpp:770-831
        -> 无 DebugDraw

legacy EngineEditor / ViewportPanel
  -> ViewportPanel::OnUpdate
     -> ViewportPanel::Render3DScene
        -> m_SceneRenderCallback(...)                          // ViewportPanel.cpp:208
        -> EditorOverlay::DrawGrid(...)                        // ViewportPanel.cpp:210-214
        -> 无 DebugDraw
```

`EditorHostService` 的 EngineHost 通道明确不提供 viewport/gizmo/picking（`editor_avalonia/Services/EditorHostService.cs:412-413, 696-735`）；`RenderProductionFrame` 是 2D 正交精灵渲染（`engine/src/Core/Application.cpp:680-834`）。因此生产编辑器当前既没有 3D 场景视口，也没有调试绘制入口。

### 1.3 通用图元 / 编辑器辅助现状

| 能力 | 位置 | 现状 | 与 DebugDraw 的关系 |
|---|---|---|---|
| `IPrimitiveBatch` | `engine/include/Engine/Core/RHI/IPrimitiveBatch.h` | 支持 Triangles/Lines/Points/TriangleStrip/LineStrip；`PrimitiveVertex` 含 pos/normal/uv/color | 是当前最接近“通用线渲染”的底层；但只被 mesh/text/HUD/particle/decal 使用，不是 DebugDraw API |
| `OpenGLPrimitiveBatch` | `engine/src/OpenGL/Resources/OpenGLPrimitiveBatch.{h,cpp}` | GL 实现；累积顶点+索引，`Flush` 单次 `DrawElements`；容量溢出自动 Flush | DebugDraw 的 GL 后端可复用其批处理与容量策略，但需自己的 shader/坐标约定 |
| `EditorOverlay` | `engine/include/Engine/Editor/EditorOverlay.h` / `engine/src/Editor/EditorOverlay.cpp` | 无限网格 + LOD；`DrawAxisIndicator` 为空实现（`:225-236` 注释“未来迭代”） | 只有网格，不提供通用调试图元 |
| ImGuizmo / ViewportPanel | `engine/src/Editor/ViewportPanel.cpp:344,471` | 编辑器变换 Gizmo、网格、视图模式 | 独立于 DebugDraw；不能替代场景调试线框 |
| `GeometryDebugFrameData` | `engine/include/Engine/Core/GeometryDebug.h:24-65` | `showAABB/showOBB/showOnlyCulled/showOcclusionBounds` 等标志；PerformanceWindow 只写 UI | **没有渲染器消费者**；搜索全仓仅 `PerformanceWindow.cpp` 写/读该结构 |
| `HelperToggles.showColliders` | `engine/include/Engine/Core/HelperToggles.h:19`；`_3DTestApp.cpp:717-728` | sandbox 内画一个硬编码 16 顶点盒子 | 不是物理碰撞体；不能作为碰撞体调试已工作的证据 |
| `r_wireframe` | `engine/src/Core/ConsoleCommandRegistry.cpp:431-439` | 控制台命令仅输出“pending renderer integration” | 与 DebugDraw 无关；尚未接线 |
| `ViewMode::Wireframe` | `engine/src/OpenGL/OpenGLContext.cpp:223-237` | 使用 `glPolygonMode(GL_LINE)` | 是场景多边形线框，不是调试图元服务 |

### 1.4 RenderGraph / RHI / 后端

| 层 | 事实 | 证据 | 对 T-5 的含义 |
|---|---|---|---|
| RenderGraph 状态 | `SG6 FAIL / ISOLATED`，生产渲染不走 RenderGraph | `docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md` §SG6（约 `:693-728`）；`docs/Avalonia-Editor-Migration-v1.md:221` | 不应把 DebugDraw 绑定到 RenderGraph Pass |
| RenderGraph API | `AddPass` / `AddScenePass` 存在，但生产 `engine/src` 中只有 `DeferredLightingPass` 注释/实现，无真实 AddPass 调用 | `engine/include/Engine/Rendering/RenderGraph.h:147-161`；`engine/src/Rendering/DeferredLightingPass.cpp` | 基础设施存在但未验证；新 Pass 无生产先例 |
| RHI 拓扑枚举 | `PrimitiveTopology::LineList/LineStrip` 存在 | `engine/include/Engine/Core/RHI/RHITypes.h:131-135` | 接口层“看起来支持” |
| GL46 command list | `SetVertexBuffer`/`SetIndexBuffer`/`SetPrimitiveTopology`/`Draw`/`DrawIndexed` 均为空实现（compute-only） | `engine/src/OpenGL/GL46CommandList.cpp:103-110` | 不能用 RHI 的 GL 后端渲染线 |
| Vulkan | PSO 使用 `desc.topology`；但 command list 的 `SetPrimitiveTopology` 明确忽略请求（注释：拓扑在 PSO 中固化），且没有 DebugDraw PSO | `engine/src/Vulkan/VulkanPipelineState.cpp:158-159`；`engine/src/Vulkan/VulkanCommandList.cpp:461-478` | 需要新建专用 PSO，不是现有路径的零成本扩展 |
| D3D12 | `SetPrimitiveTopology` 支持 LINELIST，但 PSO 的 `PrimitiveTopologyType` 固定为 `TRIANGLE`；D3D12 本身“完整但休眠、无沙盒验证” | `engine/src/D3D12/D3D12Device.cpp:179-187, 518`；`docs/D3D12-Integration-Analysis.md` §一/§二 | 不能承诺 D3D12 线框图元 |
| 后端工厂 | 生产默认 OpenGL；D3D12 休眠；Vulkan 可用性取决于 loader | `engine/src/RHI/RHIDeviceFactory.cpp:26-55`；`engine/src/Core/RHI/RHIBackend.cpp` | v1 明确 GL-only 最符合现状 |

### 1.5 生产 / 沙盒 / 编辑器边界矩阵

| 消费者 | 是否使用 DebugDraw | 证据 | 结论 |
|---|---|---|---|
| `sandbox/RenderTest` | 3D Jolt debug | `RenderTestApp.cpp:220-225, 362-366, 440-453` | 唯一 3D 接线；调用顺序有清除/时序风险 |
| `sandbox/PhysicsTest` | 2D Box2D debug | `PhysicsTestApp.cpp:242-244, 447-450` | 2D 接线完整，帧尾叠加 |
| `sandbox/AudioPhysicsSandbox` | 2D Box2D debug | `AudioPhysicsSandboxApp.cpp:610-612, 1050-1052` | 同上 |
| `sandbox/CollisionAudioTest` | 2D Box2D debug | `CollisionAudioTestApp.cpp:233-234, 405-406` | 同上 |
| `engine/src/Core` | 无 | 无 `SetDebugDraw` 调用 | 引擎没有生命周期 owner |
| `bridge/` | 无 | 全目录搜索无 DebugDraw/PhysicsDebugDraw | 生产桥接无调试绘制 ABI |
| `editor_avalonia` | 无 | 全目录搜索无 DebugDraw | 生产编辑器无入口 |
| `engine/src/Editor` | 仅网格 | `ViewportPanel.cpp:210-214`；`EditorOverlay.cpp:225-236` | legacy 编辑器只有网格 |
| `tests/` | 无 | 全目录搜索无 DebugDraw/EditorOverlay/PhysicsDebugDraw | 零测试 |

---

## 2. 缺口清单

### 2.1 已具备（可直接复用/保留）

| 能力 | 状态 | 说明 |
|---|---|---|
| 2D 线框/实心圆/多边形/线段/点 | **已具备** | `OpenGLPhysicsDebugDraw`；sandbox 2D 路径可用；但仅 GL、仅沙盒 |
| 3D 线/线框盒/球/胶囊/圆柱/轴 | **部分具备** | `OpenGLPhysicsDebugDraw3D` 的线框图元存在；Jolt 适配器未把它们真正接出来（形状走 `DrawGeometry`） |
| Jolt TLS 缓冲思路 | **已具备（仅 Jolt 内部）** | 16 个 per-thread 线缓冲；可作为未来收集器参考，但不能直接当通用线程安全契约 |
| 线批处理 | **已具备** | `IPrimitiveBatch`/`OpenGLPrimitiveBatch` 支持 Lines，单次 `DrawElements`，容量溢出安全 |
| 编辑器网格与 LOD | **已具备** | `EditorOverlay::DrawGrid`，legacy ViewportPanel 已接线 |
| GL 像素回读测试设施 | **已具备** | `tests/test_pick` 的隐藏持久上下文 + FBO + `ReadPixels`，可复用做 DebugDraw 像素门禁 |

### 2.2 部分具备（有雏形但不可当作契约）

| 能力 | 状态 | 缺口 |
|---|---|---|
| 3D 物理形状线框 | **部分具备 → 实际缺失** | `JoltDebugRenderer::DrawGeometry` 空实现；`CreateTriangleBatch` 返回空 batch；可见形状线框不成立 |
| 调试绘制线程安全 | **部分具备** | 仅 Jolt 内部 TLS；`OpenGLPhysicsDebugDraw3D::m_Lines` 非线程安全；2D 无锁；没有所有者线程定义 |
| 2D debug flags | **部分具备** | `IPhysicsDebugDraw::SetFlags` 存在，但 `Box2DDebugDraw::Create` 硬编码 shapes/joints；`Bare2DPhysicsWorld` 忽略 flags |
| 深度控制 | **部分具备** | 当前完全依赖调用方 GL 状态；没有 per-command depth flag，也没有 X-Ray pass |
| 投影矩阵传递 | **部分具备** | 只有 `SetViewProjection(float*)`；没有 view/proj 分离、viewport、DPR、相机位置/距离等契约 |
| 调试绘制生命周期 | **部分具备** | 构造/析构有 GL 资源删除；但依赖外部裸指针、无引擎 shutdown 钩子、无设备丢失重建 |
| 调试开关 | **部分具备** | sandbox 自己维护 `m_ShowCollisionShapes`/`m_DebugDrawEnabled`；引擎没有 CVar/UI 统一入口 |

### 2.3 尚未具备

| 能力 | 状态 | 说明 |
|---|---|---|
| 通用 DebugDraw 公共 API | **缺失** | 没有 `Engine::DebugDraw` 命名空间或等价服务 |
| 任意子系统调用入口 | **缺失** | 动画/AI/音频/编辑器/脚本都无法不依赖物理接口地画调试图元 |
| 持久化时长（duration） | **缺失** | 没有跨帧图元生命周期管理 |
| 3D 文本 | **缺失** | `OpenGLPhysicsDebugDraw3D::DrawText3D` 是空实现；Jolt mass/sleep 文本不可见 |
| 视锥/距离裁剪 | **缺失** | 没有 debug draw distance / frustum cull |
| 顶点预算与溢出策略 | **缺失** | 无上限、无告警、无统计；Jolt 缓冲只增不减容量 |
| 统一颜色类型 | **缺失** | `DebugColor`、`Vec4`、Jolt `Color` 三套并存 |
| 后端抽象 | **缺失** | 没有 `IDebugDrawBackend`；GL-only |
| Vulkan / D3D12 支持 | **缺失** | 无 line PSO、无测试、无命令列表路径 |
| RenderGraph DebugDraw Pass | **缺失** | 且当前不应作为首个落点 |
| 生产编辑器接线 | **缺失** | EngineHost 无 3D 视口；legacy ViewportPanel 只有网格 |
| 设备丢失/上下文重建 | **缺失** | 两个 GL debug renderer 都没有 `glfwGetCurrentContext` 检查、无重建入口 |
| 统一测试门禁 | **缺失** | 零 DebugDraw 测试；无像素/状态隔离/线程/生命周期门禁 |
| CLI/CVar 开关 | **缺失** | `r_wireframe` NYI；`GeometryDebugFrameData` 无消费者；`showColliders` 只是 sandbox 硬编码 |

---

## 3. 风险清单

### 3.1 生命周期风险

1. **裸指针所有权，无 owner。** `IPhysicsWorld::SetDebugDraw(IPhysicsDebugDraw*)` 与 `IPhysicsWorld3D::SetDebugDraw(IPhysicsDebugDraw3D*)` 只保存裸指针，不延长寿命、不置空、不参与 shutdown。调用方必须先销毁 debug renderer 再销毁物理世界，否则下一次 `DebugDraw()` 是悬空指针访问。
2. **GL 资源析构依赖“当前上下文”。** `OpenGLPhysicsDebugDraw::~OpenGLPhysicsDebugDraw`（`:55-59`）与 `OpenGLPhysicsDebugDraw3D::~OpenGLPhysicsDebugDraw3D`（`:58-62`）无条件调用 `DeleteProgram/DeleteVertexArrays/DeleteBuffers`；没有 `glfwGetCurrentContext()` 检查。若窗口/上下文先销毁，析构就是未定义行为。`EditorOverlay::Shutdown` 通过 `shared_ptr<VertexArray>` 析构删除 GL 资源，也有同类前提（`EditorOverlay.cpp:63-75`）。
3. **Jolt 全局单例约束。** `JPH::DebugRenderer` 构造函数断言 `sInstance == nullptr`，析构断言 `sInstance == this`（`third_party/JoltPhysics/Jolt/Renderer/DebugRenderer.cpp:72-83`）。每个 `JoltPhysicsWorld` 都会惰性创建 `JoltDebugRenderer`；若两个 world 同时启用 DebugDraw，Debug 构建会触发断言并破坏全局单例。`JoltPhysicsWorld::Shutdown` 也没有重置 `m_DebugRenderer`。
4. **重复头文件。** `engine/include/Engine/OpenGL/OpenGLPhysicsDebugDraw3D.h` 与 `engine/src/OpenGL/Resources/OpenGLPhysicsDebugDraw3D.h` 内容几乎完全相同（49/50 行），后续修改极易只改一份。这是维护性风险，不是当前功能缺陷。
5. **无设备丢失/重建路径。** 没有上下文失效检测、资源重建、`Shutdown/Reinitialize` 契约；任何 swapchain/context 重建都会让旧 GL 名称失效，而现有实现不会报告，只会静默画错或触发 GL 错误。
6. **无“停止调试绘制”统一路径。** sandbox 通过成员析构隐式停止；引擎没有 `SetDebugDraw(nullptr)` 的 shutdown 序列，也没有在物理世界 `Shutdown()` 中通知调试器。

### 3.2 线程风险

1. **Jolt TLS 上限 16 且取模回绕。** `MAX_THREADS = 16`，`GetThreadIndex()` 用全局 `std::atomic` + `thread_local` 取模 16（`JoltDebugRenderer.cpp:22-26`）。第 17 个线程会与第 1 个线程共享 bucket；若并行写入，会产生 vector 数据竞争/损坏。`JobSystem` 默认线程数是 `核心数 - 2`（`engine/src/Core/JobSystem.cpp:39-46`），在 >18 核机器上可超过 16。
2. **`Flush()` 无同步。** `JoltDebugRenderer::Flush` 在调用线程遍历所有 16 个 bucket（`JoltDebugRenderer.cpp:105-112`），没有屏障/锁；`Clear()` 同样清所有 bucket。若任何 worker 仍在写，属于数据竞争。当前调用顺序是 Step 完成后主线程 Flush，但这只是调用约定，不是接口保证。
3. **3D GL renderer 的 `m_Lines` 非线程安全。** `DrawLine` 直接 `push_back`（`OpenGLPhysicsDebugDraw3D.cpp:89-92`）；没有 TLS、锁或所有者线程断言。任何多线程绘制都会竞争。
4. **2D 路径无任何同步。** `OpenGLPhysicsDebugDraw` 的 `SubmitLines` 直接操作 GL/VAO/VBO；2D 物理若在工作线程 Step，而渲染线程绘制，就是跨线程 GL 调用 + 数据竞争。
5. **没有定义“谁是 owner thread”。** 当前隐含“调用线程 = GL 线程”，但没有检查、没有文档化、没有错误返回。T-5 若不先冻结线程模型，测试无法给出稳定断言。

### 3.3 渲染后端风险

1. **GL-only 实现与状态污染。** 两个 GL debug renderer 都直接绑定 program/VAO/VBO，并启用 `GL_BLEND`；`OpenGLPhysicsDebugDraw3D::SubmitLines` 结束后只把 VAO/Program 置 0，不恢复之前的 program/VAO/blend/depth 状态（`:68-87`）。若在 ImGui 或后续 sprite pass 之前调用，可能破坏后续渲染。
2. **2D 每图元一个 draw call。** `OpenGLPhysicsDebugDraw::SubmitLines` 每次 `DrawArrays`（`:160-194`）；`DrawPolygon`/`DrawCircle`/`DrawSolidCircle`/`DrawSegment` 各自提交。大量碰撞体时 draw call 线性膨胀。
3. **3D 单次 draw call 但无预算。** `m_Lines` 无上限，`SubmitLines` 一次上传并绘制；碰撞体多、球/胶囊细分高时，CPU 端顶点量和 GPU 端线段量都无界。
4. **RHI 线路径不完整。** `GL46CommandList` 的绘制是空实现；Vulkan 的动态拓扑请求被忽略；D3D12 PSO 拓扑类型固定为三角形。因此不能把“RHI 有 LineList 枚举”误判为“线框已可用”。
5. **RenderGraph 隔离。** SG6 已判定 47.5× 单 pass 开销、拓扑序问题等；生产渲染不走 RenderGraph。新增 DebugDrawPass 会把调试功能与一个未验证/隔离的编排器绑定，测试和回滚成本都会放大。
6. **生产 OpenGL 与 RHI 是两套路径。** 生产 `Application` 使用 `OpenGLContext`/`IGraphicsFactory` 直连；`GL46Device` 主要服务 compute 测试。DebugDraw 若在两个路径各写一份，会重演现有双栈分裂。

### 3.4 性能风险

| 风险 | 机制 | 影响 |
|---|---|---|
| CPU 每帧重新细分球/胶囊/圆柱 | `DrawSphere` 3 个环 × `segments` 段；胶囊两个球 + 4 条竖线；圆柱 3 个环 + 8 条竖线 | 调试对象多时 CPU 耗时线性增长 |
| 2D 每图元一个 draw call | 每次 `SubmitLines` 一次 `DrawArrays` | draw call 数量可达数千 |
| 无顶点/线段预算 | 3D `m_Lines` 无上限；Jolt 每个 body 都绘制 | 极端场景显存/带宽/帧时间不可控 |
| 无距离/视锥裁剪 | 所有 body 都画 | 大世界调试不可用 |
| 无缓存/实例化 | 每帧重建顶点 | 无法复用静态调试几何 |
| TLS 缓冲容量只增不减 | `clear()` 保留 capacity | 峰值后长期占用内存 |
| Jolt 每帧遍历全部 body | `BodyManager::Draw` 对每个有效 body 调用 | 与物理对象数量线性相关 |

### 3.5 正确性与集成风险

1. **3D 形状线框实际缺失。** 见 §0.3 与 §1.2；`DrawGeometry` 空实现是根因。当前可能“看起来有调试绘制”，但看到的是 AABB/坐标轴，不是形状。
2. **RenderTest 调用顺序导致调试线被清掉。** 见 §1.2 的静态推导；需 Gate B 用真实帧确认。若成立，现有 3D sandbox 的“Show Collision Shapes”实际上看不到线段。
3. **2D solid 回调的变换处理可疑。** `Box2DDebugDraw::DrawSolidPolygonImpl` 接收 `transform`，但直接使用局部 `vertices[i]`，没有做 `b2TransformWorldPoint`（`Box2DDebugDraw.cpp:66-80`）；线框版本有变换（`:51-64`）。`DrawSolidCircleImpl` 同样忽略 `transform` 与 `center` 的局部偏移（`:89-96`），而 Box2D 官方 sample 会把 center 折进 transform（`third_party/box2d/samples/draw.c:1238-1250`）。按 Box2D 回调语义，实心填充可能画在错误位置。需在 Gate B 用已知形状验证。
4. **2D flags 不生效。** `Box2DDebugDraw::Create` 固定 `drawShapes=true, drawJoints=true, drawBounds=false, drawMass=false`（`:10-29`），不读取 `IPhysicsDebugDraw::GetFlags()`；`Bare2DPhysicsWorld::DebugDraw` 也无视 flags，始终画形状 + AABB + 接触点（`:1192-1267`）。
5. **2D 投影高度硬编码。** `OpenGLPhysicsDebugDraw::m_HeightOffset = -2.0f`（`OpenGLPhysicsDebugDraw.h:81-82`），2D 物理被映射到 3D 的 y=-2 平面；没有相机/视口/编辑器配置。若相机不在该平面附近，调试图形会离屏。
6. **3D shader 无编译/链接检查。** `OpenGLPhysicsDebugDraw3D` 构造时创建 shader/program，但不检查 `GL_COMPILE_STATUS`/`GL_LINK_STATUS`，失败时无日志（`:32-55`）；2D 版本有检查（`OpenGLPhysicsDebugDraw.cpp:65-99`）。失败会退化为“没有输出但不报错”。
7. **3D `DrawText3D` 空实现。** `OpenGLPhysicsDebugDraw3D.cpp:178-180`；Jolt 的 mass/sleep 文本因此不可见。
8. **3D solid 模式大多空实现。** `DrawTriangle(wireframe=false)`、`DrawBox(wireframe=false)`、`DrawCapsule(wireframe=false)`、`DrawCylinder(wireframe=false)` 直接返回（`:94-100, 120-122, 137-139, 156-158`）。不能把接口存在当作实心调试能力。
9. **2D `m_ViewProjDirty` 未使用。** `OpenGLPhysicsDebugDraw.h:78-79` 设置但提交路径没有读取；无实际影响，但说明该实现没有完整的“脏状态”契约。
10. **覆盖层开关与渲染器脱节。** `GeometryDebugFrameData` 只有 UI；`r_wireframe` NYI；`_3DTestApp` 的 `showColliders` 是硬编码盒子。任何“已有开关”都不能作为 DebugDraw 已接线的证据。

---

## 4. 最小实施方案（建议，不构成授权）

### 4.1 建议范围（v1）

**目标**：为引擎提供最小、可测试、OpenGL-only 的通用调试线段服务，先在不依赖生产编辑器的场景中证明正确性。

建议包含：

1. **新公共 API**：放在渲染层（建议 `engine/include/Engine/Rendering/DebugDraw.h`），不放在 `Core/Physics`；物理接口保持原样。
2. **收集器 + 渲染器分离**：
   - `DebugDrawCollector`：只存 CPU 端图元，不碰 GL；负责帧生命周期、duration、预算、统计。
   - `OpenGLDebugDrawRenderer`：拥有 GL 资源；只负责把收集到的线段批次上传/绘制。
   - 这样 CPU 收集可以用 fake backend 单测，GL 像素测试只覆盖渲染器。
3. **图元范围（v1）**：
   - `Line(from, to, color, depthTest)`
   - `Box(center, halfExtents, rotation, color, depthTest)`（线框）
   - `Sphere(center, radius, color, segments, depthTest)`（线框三环或经纬线）
   - `Axes(transform, length)`（RGB 三轴）
   - `Frustum(viewProj, color)`（12 条边）
   - 可选 `Point`；**不做** 3D 文本、实体三角面、材质/光照。
4. **帧契约**：
   - `BeginFrame(const DebugView&)`：设置 VP、相机位置、viewport、dt；清空本帧瞬态命令（保留 duration 命令）。
   - `EndFrame()`：提交本帧；由渲染器在 3D 场景 pass 末尾调用 `Render()`（或 `EndFrame()` 直接渲染，Gate A 决定）。
   - `duration == 0` 表示仅本帧；`duration > 0` 表示跨帧存续，按显式 dt 递减。
5. **线程模型（v1 建议）**：**所有者线程收集**。`DebugDrawCollector::BeginFrame/Line/EndFrame` 必须由同一 owner thread 调用；其他线程调用返回错误/断言（Debug），不做静默 TLS。理由：现有唯一真实需求（物理 Step 后绘制）可在主线程完成；先把契约做对，再评估 TLS。
6. **深度策略（v1 建议）**：每条命令带 `depthTest`；渲染器做两个批次：先 depth-on，再 depth-off（X-Ray）。若 Gate A 认为两 pass 过度，可先只支持 depth-on，但接口保留 flag。
7. **预算/溢出（v1 建议）**：硬上限（例如 65,536 个线段顶点）；超限丢弃**最新**命令，统计 `droppedLines`，每帧最多告警一次；绝不扩容到无界。
8. **GL 后端要求**：
   - GL 4.6 core；显式检查 shader 编译/链接；
   - 保存/恢复 program、VAO、blend、depth 状态（不要污染后续 pass）；
   - 检查 `glfwGetCurrentContext()`；无上下文时返回失败/跳过，不调用 GL；
   - 构造/析构与上下文生命周期绑定；提供 `Initialize/Shutdown`，析构不假设上下文仍存在（可要求调用方先 `Shutdown`）。
9. **接入点（v1）**：
   - 先在 sandbox（RenderTest/PhysicsTest）用新 API 替换或并列验证；
   - legacy `ViewportPanel::Render3DScene` 的 `SceneRenderCallback` 之后、`EditorOverlay::DrawGrid` 之前可作为可选接入点；
   - **不接生产 EngineHost / RenderProductionFrame**，除非另有 UI/bridge 授权。
10. **物理适配（v1）**：
    - 保留 `IPhysicsDebugDraw`/`IPhysicsDebugDraw3D`；
    - 可新增 `PhysicsDebugDrawAdapter2D/3D`，把物理回调转发到新收集器；
    - **不承诺** Jolt 形状线框立即正确：`DrawGeometry` 缺失是独立问题，需单独任务（实现 geometry 解码或从 `IPhysicsWorld3D` 增加形状枚举）。v1 的物理可视化只应承诺“适配器转发已有的 DrawLine/DrawBox 等调用”，而不是“碰撞体形状已完整显示”。

### 4.2 建议 API 轮廓（仅示意，Gate A 冻结）

```cpp
namespace Engine::DebugDraw {

struct View {
    Mat4    viewProj;
    Vec3    cameraPosition;
    float   viewportWidth = 0.0f;
    float   viewportHeight = 0.0f;
    float   dt = 0.0f;
};

struct Stats {
    uint32  submittedLines = 0;   // 成功入队的线段数
    uint32  droppedLines   = 0;   // 预算/非法参数丢弃
    uint32  drawCalls      = 0;   // 后端实际 draw call
    uint32  vertexCount    = 0;   // 后端上传顶点数
    bool    budgetWarning  = false;
};

class Collector {
public:
    bool BeginFrame(const View& view);
    void Line(const Vec3& a, const Vec3& b, const Vec4& color,
              float duration = 0.0f, bool depthTest = true);
    void Box(const Vec3& center, const Vec3& halfExtents,
             const Quat& rotation, const Vec4& color,
             float duration = 0.0f, bool depthTest = true);
    void Sphere(const Vec3& center, float radius, const Vec4& color,
                int32 segments = 16, float duration = 0.0f,
                bool depthTest = true);
    void Axes(const Mat4& transform, float length = 0.5f);
    void Frustum(const Mat4& viewProj, const Vec4& color);
    Stats EndFrame();             // 收集结束，返回统计
    void Clear();
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool Initialize(IGraphicsFactory& factory, GladGLContext& gl) = 0;
    virtual void Shutdown() = 0;
    virtual bool Render(const Collector& collector) = 0;
};

} // namespace Engine::DebugDraw
```

> 说明：上面是形状建议，不是已冻结契约。Gate A 需要决定：是否提供全局 free-function 门面、颜色类型统一到 `Vec4` 还是保留 `DebugColor`、`Render` 与 `EndFrame` 的调用顺序、duration 的时间源、线程模型是 owner-thread 还是 TLS。

### 4.3 测试矩阵（建议 Gate B 必须覆盖）

| ID | 层级 | 场景 | 断言 | 是否必须 |
|---|---|---|---|---|
| T5-U01 | CPU | 空帧 | 无线段、无 draw call、统计为 0 | 必须 |
| T5-U02 | CPU | Line/Box/Sphere/Axes/Frustum 生成 | 顶点数/线段数符合公式；坐标正确 | 必须 |
| T5-U03 | CPU | duration=0 vs >0 | 0 当帧后消失；>0 按 dt 递减、到期清除 | 必须 |
| T5-U04 | CPU | 预算溢出 | 不越界、丢弃最新、`droppedLines` 正确、每帧一次告警 | 必须 |
| T5-U05 | CPU | 非法参数（NaN/Inf、零长度） | 拒绝/丢弃，不进入后端 | 必须 |
| T5-U06 | CPU | owner thread 违规 | Debug 断言/错误返回；Release 有明确失败语义 | 必须（若选 owner-thread） |
| T5-U07 | CPU | TLS 多线程（若 Gate A 选 TLS） | N 线程并发写入无丢失/无竞争；>16 线程不别名 | 条件必须 |
| T5-GL01 | GL 像素 | 已知 VP + 单条水平线 | 指定像素颜色/位置正确；背景不受影响 | 必须 |
| T5-GL02 | GL 像素 | 线框盒 | 12 条边可见，内部不填充 | 必须 |
| T5-GL03 | GL 像素 | depth on/off | 被遮挡线段在 depth-off 可见，在 depth-on 不可见 | 必须（若实现双 pass） |
| T5-GL04 | GL 状态隔离 | Debug 绘制后画 sprite/ImGui 参考几何 | 后续 pass 像素不受污染；GL 状态恢复 | 必须 |
| T5-GL05 | GL 生命周期 | 创建/销毁/重建 renderer | 无 GL 错误、无泄漏、重复 Shutdown 安全 | 必须 |
| T5-GL06 | GL 无上下文 | 无 current context 调用 | 返回失败/跳过，不调用 GL，不崩溃 | 必须 |
| T5-AD01 | 物理适配 | Box2D 回调 → adapter | 线框/实心多边形世界坐标正确；flags 生效 | 必须（若接 2D） |
| T5-AD02 | Jolt 适配 | JoltDebugRenderer → collector | DrawLine/DrawTriangle 转发；`DrawGeometry` 行为被显式测试并记录 | 必须（若接 3D） |
| T5-INT01 | 集成 | sandbox/legacy viewport 帧 | 已知相机下调试线出现在预期像素；不被场景 Clear 抹掉 | 必须 |
| T5-INT02 | 集成 | 与 grid/ImGui 共存 | 网格/UI 正常，无状态污染 | 必须 |
| T5-SMOKE01 | 真实进程 | 真实窗口 + GPU | 线可见、可开关、无崩溃 | 建议 |

测试设施建议：

- CPU 测试使用 fake `IRenderer`/记录型 backend，不创建 GL。
- GL 像素测试复用 `tests/test_pick/test_pick.cpp` 的 `RegisterGlfwOwnerThread` + `InitializePersistentHiddenContext` + `OpenGLFramebuffer` + `glReadPixels` 模式；新增独立 `test_debugdraw` target，不塞进 `test_renderer`。
- Jolt 适配器测试需注意 `JPH_DEBUG_RENDERER` 只在 Debug 定义（`engine/CMakeLists.txt:123-127`）；测试 target 若要直接包含 `JoltDebugRenderer`，需在 Debug 配置同样定义，或在 Release 只测桩类的“不可用”语义。
- 不把“函数返回非零”当断言；必须断言像素/计数/状态。

### 4.4 明确不做（v1 非目标）

1. 不接 RenderGraph Pass；不在 SG6 隔离状态下新增图编排依赖。
2. 不做 Vulkan / D3D12 / Metal 后端；不承诺跨后端线框。
3. 不做 3D 文本（ImGui 屏幕投影属于后续切片）。
4. 不做实体三角面/实心填充/材质/光照；v1 只做线段类图元。
5. 不做 GPU instancing、GPU 端调试几何、间接绘制。
6. 不做任意线程 TLS 收集（除非 Gate A 明确选择 TLS；v1 建议 owner-thread）。
7. 不做生产 `EngineHost`/`RenderProductionFrame` 接线、不做 Bridge/Editor ABI、不做编辑器 UI 开关。
8. 不做 CVar/Console 命令接入（`p.showColliders` / `r.debugDrawDistance` 等属于后续）。
9. 不修 Jolt 形状几何提取；不把“物理碰撞体完整可视化”作为 v1 验收条件。
10. 不继续扩展 T-6-B、F2-S1 或音频（T-7 已关闭）。
11. 不重构/删除现有物理调试接口；只做适配与迁移评估。
12. 不做性能优化承诺（无基准数据前不宣称帧率影响）。

### 4.5 建议实施顺序（若后续获批）

1. **Gate A**：冻结 API、线程、帧生命周期、深度、预算、后端与测试矩阵。
2. **A 步：CPU 收集器 + fake backend 测试**（T5-U01..U06）。
3. **B 步：OpenGL 线段 renderer + 像素/状态隔离测试**（T5-GL01..GL06）。
4. **C 步：sandbox/legacy viewport 接入 + 集成测试**（T5-INT01..02）。
5. **D 步：物理适配**（Box2D 优先；Jolt 先转发已有线，不改 `DrawGeometry`）。
6. **E 步：评估 Jolt 形状几何与生产编辑器接线**，作为独立任务重新授权，不自动并入 T-5。

---

## 5. Gate A 待裁决项（需要冻结的接口契约与验收标准）

### 5.1 决策项

| ID | 决策项 | 选项 | 建议 | 理由 |
|---|---|---|---|---|
| D5-1 | API 归属 | Core/Physics / Core/RHI / Rendering | **Rendering/DebugDraw** | 调试绘制是横切渲染能力，不是物理能力；物理接口保持独立 |
| D5-2 | 颜色/数学类型 | 新增 Color / 复用 DebugColor / 复用 Vec4 | **复用 `Vec3`/`Vec4`/`Mat4`/`Quat`** | 避免第四套颜色类型；`DebugColor` 留给物理 2D 接口兼容 |
| D5-3 | 线程模型 | owner-thread only / TLS any-thread | **v1 owner-thread only** | 现有真实需求可在主线程完成；先冻结正确性，TLS 作为显式扩展 |
| D5-4 | 帧生命周期 | 每帧显式 Begin/End / 仅 Flush | **`BeginFrame(view)` → 收集 → `EndFrame()`/`Render()`** | 明确 duration、预算、统计和渲染时机；避免“调用即绘制”的隐式状态 |
| D5-5 | 深度策略 | 仅 depth-on / depth-on + X-Ray | **两条批次，per-command `depthTest`** | 线框调试最常用 X-Ray；实现成本低；Gate A 可决定 v1 是否强制 |
| D5-6 | 预算与溢出 | 无上限 / 固定上限 | **固定顶点上限，丢弃最新，统计+一次性告警** | 防止调试功能拖垮帧；可测试 |
| D5-7 | 图元范围 | 仅线 / 线+线框体 / 全功能 | **线 + 线框盒/球/轴/视锥** | 覆盖调试主需求；文本/实体后置 |
| D5-8 | 后端范围 | GL-only / RHI 三后端 | **GL 4.6 only，其他后端显式 Unsupported** | 现状 GL 是唯一生产路径；RHI 线路径未完成 |
| D5-9 | 集成点 | RenderGraph / 直接渲染阶段 | **3D 场景 pass 末尾、UI 之前直接渲染** | RenderGraph SG6 隔离；直接路径可测、可回滚 |
| D5-10 | 物理接入边界 | 改造物理接口 / 适配器 | **保留接口，新增 adapter** | 避免破坏 T-1/T-7 已冻结契约；Jolt 形状问题独立 |
| D5-11 | Release 语义 | Debug-only / 全配置 | **通用服务全配置；Jolt 物理适配 Debug-only** | `JPH_DEBUG_RENDERER` 现状决定；Release 不得静默假装有物理线框 |
| D5-12 | 生命周期 API | 构造即初始化 / 显式 Initialize/Shutdown | **显式 Initialize/Shutdown + 上下文当前检查** | 支持上下文重建与可观测失败 |
| D5-13 | 失败语义 | 静默跳过 / 返回 false/状态 | **可观测失败（bool/状态/日志）** | 避免“资源失败但看起来正常”；符合项目既往门禁纪律 |
| D5-14 | 测试门禁 | 单测 / 单测+像素 | **CPU 单测 + GL 像素 + 状态隔离 + 生命周期** | 现有 `test_pick` 设施可复用；防止假接线 |
| D5-15 | 生产接线 | 立即接 EngineHost / 暂不接 | **暂不接；单独授权** | 生产编辑器当前无 3D 视口；Bridge/UI 改动不在 T-5 范围 |

### 5.2 建议的冻结契约（A 级验收标准）

建议 Gate A 至少冻结以下条款：

1. **API 契约**
   - 公共头文件位置、命名空间、类型（`View`/`Stats`/`Collector`/`IRenderer`）。
   - 图元列表及参数语义（世界空间、右手系、颜色 alpha 预乘与否、`duration` 单位）。
   - `depthTest` 默认值与两 pass 顺序。
   - `BeginFrame`/`EndFrame`/`Render` 的调用顺序和可重入性。
2. **线程契约**
   - owner thread 的定义与违规行为（Debug 断言/Release 错误码/日志）。
   - 是否允许在物理回调/JobSystem worker 中调用（v1 建议禁止）。
   - 若选 TLS：线程上限、溢出策略、Flush 同步点。
3. **生命周期契约**
   - `Initialize` 必须在 current GL context 下调用；失败返回 false 并记录原因。
   - `Shutdown` 必须幂等；调用后不得再触碰 GL。
   - 上下文重建时必须显式 Shutdown/Initialize；不做隐式重建。
   - 不拥有窗口/上下文；不延长物理世界/物理 debug 对象的寿命。
4. **渲染契约**
   - 只在 3D 场景 pass 末尾、UI 之前渲染；不得在 `ClearColor` 之前提交。
   - 必须保存/恢复 GL 状态；不得改变后续 pass 的 blend/depth/program/VAO。
   - 线宽、混合、深度函数、sRGB/线性空间需明确。
5. **预算契约**
   - 最大线段顶点数；溢出丢弃策略；统计字段；告警频率。
6. **后端契约**
   - GL 4.6 支持；其他后端 `IsSupported() == false`，不静默降级。
   - 与 `IPrimitiveBatch`/`OpenGLPrimitiveBatch` 的关系：复用或独立实现，必须二选一并在契约中写明。
7. **物理适配契约**
   - 现有 `IPhysicsDebugDraw`/`IPhysicsDebugDraw3D` 不变。
   - `PhysicsDebugDrawAdapter*` 的转换规则（2D 高度映射、flags、色彩）。
   - Jolt 形状线框是否属于 T-5：建议 **不属于**；单独建 Jolt 形状提取任务。
8. **验收证据**
   - CPU 单测、GL 像素、状态隔离、生命周期、预算、线程、集成测试全部通过。
   - 记录真实构建配置、GL 版本/驱动、测试命令与警告数量；不得宣称“零警告”，若存在既有警告需原样记录。
   - 不把 sandbox 手动观察当作自动化门禁；不把 `glGetError` 为空当作像素正确。

### 5.3 建议的 Gate B 启动条件

在用户单独批准 Gate B 之前，不应修改源码。建议 Gate A 冻结后再明确：

- 是否允许新增 `test_debugdraw` target 与 `tests/CMakeLists.txt` 改动；
- 是否允许修改 `sandbox` 接线；
- 是否允许在 legacy `ViewportPanel` 增加可选接入点；
- 是否允许修复 `Box2DDebugDraw` 的 transform/flags（这属于现有 2D debug 的缺陷修复，需单独列项，不可夹带）；
- 是否允许修改 `JoltPhysicsWorld`/`JoltDebugRenderer`（若修 `DrawGeometry`，属于独立 Jolt 适配任务）；
- 生产 `EngineHost`/Bridge/Editor 接线必须另立任务与授权。

---

## 6. 附录

### 6.1 关键源码位置

| 文件 | 关键位置 | 用途 |
|---|---|---|
| `engine/include/Engine/Core/Physics/IPhysicsDebugDraw.h` | `:42-63` | 2D 调试接口 |
| `engine/include/Engine/Core/Physics/IPhysicsDebugDraw3D.h` | `:16-52` | 3D 调试接口 |
| `engine/include/Engine/OpenGL/OpenGLPhysicsDebugDraw.h` | `:24-83` | 2D GL 实现声明 |
| `engine/src/OpenGL/Resources/OpenGLPhysicsDebugDraw.cpp` | `:151-194` | VP 设置、逐图元提交 |
| `engine/include/Engine/OpenGL/OpenGLPhysicsDebugDraw3D.h` | `:15-48` | 3D GL 实现声明（重复头之一） |
| `engine/src/OpenGL/Resources/OpenGLPhysicsDebugDraw3D.h` | `:15-48` | 3D GL 实现声明（重复头之二） |
| `engine/src/OpenGL/Resources/OpenGLPhysicsDebugDraw3D.cpp` | `:68-87, 89-200` | 线批提交、图元实现、文本空实现 |
| `engine/src/Jolt/JoltDebugRenderer.cpp` | `:22-26, 41-46, 79-88, 99-112` | TLS、绘制收集、DrawGeometry 空实现、Clear/Flush |
| `engine/src/Jolt/JoltPhysicsWorld.cpp` | `:577-597` | 3D DebugDraw 调用链 |
| `engine/src/Box2D/Box2DDebugDraw.cpp` | `:10-29, 51-80, 89-96` | b2DebugDraw 桥接、flags 固定、solid transform |
| `engine/src/Box2D/Box2DPhysicsWorld.cpp` | `:543-551` | 2D SetDebugDraw/DebugDraw |
| `engine/src/Core/Physics/Bare2DPhysicsWorld.cpp` | `:1192-1267` | Bare2D 调试绘制（无 flags） |
| `engine/include/Engine/Core/RHI/IPrimitiveBatch.h` | 全文 | 通用批处理接口 |
| `engine/src/OpenGL/Resources/OpenGLPrimitiveBatch.cpp` | `:125-180` | GL 批处理 Flush/资源 |
| `engine/include/Engine/Editor/EditorOverlay.h` | `:25-80` | 网格/轴覆盖层声明 |
| `engine/src/Editor/EditorOverlay.cpp` | `:168-236` | DrawGrid；DrawAxisIndicator 空实现 |
| `engine/src/Editor/ViewportPanel.cpp` | `:152-219` | legacy 3D 视口渲染与网格接线 |
| `engine/src/Core/Application.cpp` | `:680-834` | 生产 2D 精灵帧 + pick pass |
| `engine/include/Engine/Application.h` | `:86-117` | production render API 与 pick 开关 |
| `engine/include/Engine/Core/GeometryDebug.h` | `:24-65` | 仅 UI 使用的几何调试标志 |
| `engine/src/Core/ConsoleCommandRegistry.cpp` | `:431-439` | `r_wireframe` NYI |
| `engine/src/OpenGL/OpenGLContext.cpp` | `:85-93, 223-237` | ClearColor、ViewMode Wireframe |
| `engine/src/OpenGL/GL46CommandList.cpp` | `:103-110` | GL RHI 绘制空实现 |
| `engine/src/Vulkan/VulkanCommandList.cpp` | `:461-478` | 动态拓扑忽略 |
| `engine/src/D3D12/D3D12Device.cpp` | `:179-187, 518` | D3D12 拓扑/PSO 限制 |
| `tests/test_pick/test_pick.cpp` | `:45-113, 171-198` | 隐藏 GL 上下文 + FBO 像素读取设施 |
| `tests/CMakeLists.txt` | `:21-309` | 现有测试 target；无 DebugDraw 测试 |

### 6.2 相关既有文档

- `docs/DebugRenderer-2.0-Analysis-Report.md` — 2026-07-08 的通用 DebugDraw 缺口分析与 27h 路线图；其“RenderGraph Pass”与“JoltDebugRenderer 无需重写”的假设需要按当前 SG6 隔离状态和 DrawGeometry 空实现重新裁决。
- `docs/Next-Phase-Implementation-Plan.md` §1.1 — 旧的 4 天 DebugDraw 计划；其中 RenderGraph Pass / CVar / 3D 文本属于后续切片，不应整体照搬。
- `docs/Architecture-Runtime-Integration-Inventory.md` — 明确 debug draw 为 `PARTIAL`、仅 sandbox。
- `docs/Physics-Subsystem-Summary.md` — 物理接口/实现清单；把 `IPhysicsDebugDraw3D` 视为已完备会高估现状（形状/文本/Release 均受限）。
- `docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md` §SG6 — RenderGraph `FAIL as-is` / 隔离裁决。
- `docs/D3D12-Integration-Analysis.md` — D3D12 “完整但休眠、无沙盒验证”。

### 6.3 本次未做

- 未编译任何 target，未运行任何测试或 sandbox。
- 未进行像素验证、GL 状态验证、线程竞争验证、设备丢失模拟。
- 未修改源码、CMake、子模块、测试或 Git 历史。
- 未对 `openal-soft`、`d3d12ma`、`vma` 做任何操作。
- 未开始 T-6-B、F2-S1 或任何音频后续项。

---

**Gate 0 裁决**：`T-5 GATE 0 — PASS — READ-ONLY COMPLETE`。后续 **Gate A 契约冻结**已于 2026-10-31 记录于 `docs/T-5-DebugDraw-GateA-Contract.md`（`T-5 Gate A — PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND`）；**Gate B** 随后获授权并完成定向验证，见 `docs/T-5-DebugDraw-GateB-Closure-Record.md`。本扫描正文仍为 Gate 0 时点的只读快照。
