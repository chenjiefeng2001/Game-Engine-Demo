# T-5 Generic DebugDraw — Implementation Record

**状态**：`T-5 Gate B — PASS — SCOPE-BOUND`（实现记录）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateA-Contract.md`
**实现补充**：`docs/T-5-GateB-Implementation-Addendum.md`
**测试记录**：`docs/T-5-DebugDraw-Test-Record.md`
**关闭记录**：`docs/T-5-DebugDraw-GateB-Closure-Record.md`

---

## 1. 交付范围

本阶段交付 T-5 v1 通用 DebugDraw 服务，全部为新增文件：

| 文件 | 作用 |
|---|---|
| `engine/include/Engine/Rendering/DebugDraw/DebugDrawTypes.h` | `Status`/`View`/`Vertex`/`FrameGeometry`/`Stats`/预算与球体常量 |
| `engine/include/Engine/Rendering/DebugDraw/IDebugDrawRenderer.h` | 后端抽象 `IRenderer` |
| `engine/include/Engine/Rendering/DebugDraw/DebugDrawCollector.h` | CPU 收集器声明 |
| `engine/include/Engine/Rendering/DebugDraw/DebugDraw.h` | 对外统一服务入口 |
| `engine/include/Engine/Rendering/DebugDraw/DebugDrawStats.h` | 统计类型的稳定包含点 |
| `engine/include/Engine/Rendering/DebugDraw/OpenGL/OpenGL46DebugDrawRenderer.h` | OpenGL 4.6 后端声明 |
| `engine/include/Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter2D.h` | 2D 物理适配器声明 |
| `engine/include/Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter3D.h` | 3D 物理适配器声明 |
| `engine/src/Rendering/DebugDraw/DebugDrawCollector.cpp` | 参数校验、图元展开、帧状态、duration、预算、统计 |
| `engine/src/Rendering/DebugDraw/DebugDraw.cpp` | 服务生命周期、Renderer 所有权、错误日志 |
| `engine/src/Rendering/DebugDraw/OpenGL/OpenGL46DebugDrawRenderer.cpp` | GL 4.6 shader/VAO/VBO、depth/X-Ray、状态隔离 |
| `engine/src/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter2D.cpp` | `IPhysicsDebugDraw` → 通用线段 |
| `engine/src/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter3D.cpp` | `IPhysicsDebugDraw3D` → 通用线段 |
| `tests/test_debugdraw/test_debugdraw.cpp` | CPU、适配器、GL 像素/状态/生命周期门禁 |
| `tests/test_debugdraw/DebugDrawGLFixture.h` | 隐藏 GL 4.6 上下文 + FBO 测试夹具 |

构建接线：

- `engine/CMakeLists.txt` 增加 `src/Rendering/DebugDraw/*` 等 GLOB 与对应头文件 GLOB。
- `tests/CMakeLists.txt` 增加独立 `test_debugdraw` target 与 `add_test`；未重排既有 target。
- 未修改 `IPhysicsDebugDraw`、`IPhysicsDebugDraw3D`、`Box2DDebugDraw`、`JoltPhysicsWorld`、`JoltDebugRenderer`、RenderGraph、RHI 或生产编辑器。

---

## 2. API 与调用链

```text
DebugDraw::Initialize(std::unique_ptr<IRenderer>)
  -> IRenderer::Initialize()
  -> Collector::Initialize(renderer*)

DebugDraw::BeginFrame(view, deltaTimeSeconds)
  -> Collector::BeginFrame()
     -> age persistent commands
     -> clear current frame + stats

DebugDraw::Line/WireBox/WireSphere/Axis/Frustum(...)
  -> Collector validation
  -> line expansion
  -> atomic budget check
  -> current command list

DebugDraw::EndFrame()
  -> Collector merges persistent + current
  -> IRenderer::Render(view, FrameGeometry)
     -> depth batch then X-Ray batch
     -> restore GL state
  -> persistent commands commit after successful render

DebugDraw::Shutdown()
  -> Collector::Shutdown()
  -> IRenderer::Shutdown()
```

`EndFrame()` 完成批次提交与实际绘制；调用方不调用额外的 `Render()`。

---

## 3. Collector 行为

### 3.1 图元展开

| 图元 | 线段 | 顶点 |
|---|---:|---:|
| `Line` | 1 | 2 |
| `WireBox` | 12 | 24 |
| `WireSphere` | 48（3 个正交大圆 × 16 段） | 96 |
| `Axis` | 3 | 6 |
| `Frustum` | 12 | 24 |

- `WireBox` 使用中心、半尺寸、四元数旋转；四元数有限且非零，内部归一化。
- `Axis` 使用固定红/绿/蓝三轴，长度必须为正。
- `Frustum` 的角点顺序与边连接冻结在 `docs/T-5-GateB-Implementation-Addendum.md` §2。
- 所有输入在提交时校验有限值、正尺寸、颜色范围 `[0,1]`、duration 非负；组合图元整体接受或整体丢弃。

### 3.2 帧状态与 Owner Thread

- Owner Thread 在 `Collector::Initialize()` 记录为调用线程；所有 `BeginFrame`/图元/`EndFrame`/`Shutdown` 都在该线程校验。
- 未初始化、帧未开始、重复 `BeginFrame`、`EndFrame` 无匹配 `BeginFrame`、错误线程均返回明确 `Status`。
- `BeginFrame` 对 `deltaTimeSeconds` 做有限且非负校验，对 `View.viewProjection` 做 16 个元素有限校验。
- `EndFrame` 在 Renderer 失败时丢弃本帧 current 命令并返回失败；不把失败帧带入持久集合。

### 3.3 duration

- `durationSeconds == 0`：仅当前帧。
- `durationSeconds > 0`：当前帧渲染后进入持久集合；下一帧 `BeginFrame` 按 dt 递减，`remaining <= 0` 移除。
- 负值、NaN、Infinity：`InvalidArgument`。
- 持久命令与当前帧命令共用 50,000 顶点预算。

### 3.4 预算与统计

- 预算：`kMaxVerticesPerFrame = 50000`；按展开后顶点计数。
- 超限：完整丢弃最新命令，保留此前已接受命令，后续较小命令仍可尝试。
- `Stats`：`acceptedVertices`、`acceptedCommands`、`droppedCommands`、`droppedVertices`、`capacityVertices`、`budgetWarning`。
- 预算告警每次 `Initialize` 生命周期最多输出一次；`budgetWarning` 标记当前帧是否发生丢弃。

---

## 4. OpenGL 4.6 Renderer

### 4.1 初始化

- 检查 `glfwGetCurrentContext()`；无 current context 返回 `BackendFailure`。
- 解析 `GL_VERSION`，要求 >= 4.6；检查 `GL_CONTEXT_PROFILE_MASK` 包含 core profile；不满足返回 `Unsupported`。
- 编译/链接 DebugDraw 专用 shader，检查 compile/link 状态与 uniform location。
- 创建 VAO/VBO，配置位置（`Vec3`）和颜色（`Vec4`）顶点属性；检查 GL error。

### 4.2 绘制

- `Vertex` 直接上传为 `GL_LINES`；线宽固定 `1.0`。
- 固定顺序：depth-on 批次 → X-Ray 批次。
- 两批次均 `DepthMask(GL_FALSE)`；depth batch 使用 `GL_LEQUAL`，X-Ray batch 关闭 `GL_DEPTH_TEST`。
- 颜色使用 straight-alpha 混合；不改变 framebuffer sRGB 配置。

### 4.3 GL 状态隔离

`GLStateSnapshot` 保存并恢复：

- Blend enable / RGB+alpha factors / RGB+alpha equations；
- Depth test enable / depth function / depth write mask；
- Current program / VAO / `GL_ARRAY_BUFFER` binding；
- Line width。

不改变 viewport、scissor、framebuffer、color mask、cull 等未由 Renderer 修改的状态。`GLStateIsRestored` 门禁验证了关键子集。

### 4.4 生命周期失败语义

- `Initialize` 重复调用返回 `AlreadyInitialized`。
- `Shutdown` 重复调用返回 `Ok`。
- 无 current context 时 `Shutdown` 返回 `BackendFailure`，不执行删除调用；上下文恢复后可再次 `Shutdown`。
- `DebugDraw::Shutdown` 在 Renderer 关闭失败时保留 Renderer，供上下文恢复后重试；Collector 已关闭，因此不可继续绘制。
- 不拥有窗口/上下文，不延长物理对象生命周期。

---

## 5. 物理适配器

### 5.1 2D

`PhysicsDebugDrawAdapter2D` 实现既有 `IPhysicsDebugDraw`：

- `physicsToWorld` 将物理 XY 映射到世界点；默认 Identity（Z=0）。
- `DrawPolygon`/`DrawCircle` 输出轮廓线段；`DrawSolidPolygon`/`DrawSolidCircle` 不填充。
- `DrawSegment` 受 `DebugDraw_Shape | DebugDraw_Joint` flags 控制。
- `DrawTransform` 受 `DebugDraw_Joint` 控制；`DrawPoint` 受 `DebugDraw_COM` 控制。
- 不修改 `Box2DDebugDraw`；其实体回调 transform 既有缺陷不在本阶段修复。

### 5.2 3D

`PhysicsDebugDrawAdapter3D` 实现既有 `IPhysicsDebugDraw3D`：

- `DrawLine`/`DrawTriangle` 转换为线段；`DrawSphere`/`DrawBox` 转发通用线框图元。
- `DrawCapsule`/`DrawCylinder` 生成轮廓环与竖线；不填充。
- `DrawAxes` 使用矩阵列方向生成 RGB 三轴。
- `DrawText3D` 明确 no-op，不伪装文本能力。
- `Clear`/`Flush` 不控制通用服务帧生命周期；实际提交由 `DebugDraw::EndFrame()` 完成。
- 不实现 Jolt `DrawGeometry`；不修改 `JoltPhysicsWorld` / `JoltDebugRenderer`。

---

## 6. 边界与未做

- 未接 sandbox；未接 legacy `ViewportPanel`；未接 EngineHost / Bridge / 生产 Editor。
- 未接 RenderGraph / RHI 多后端；只支持 GL 4.6。
- 未修复 `Box2DDebugDraw` transform/flags 缺陷。
- 未修复 Jolt `DrawGeometry` / 碰撞体形状线框。
- 未实现 3D 文本、实体填充、GPU instancing、CVar/Console 开关、任意线程 TLS。
- 未做完整全量 CTest；只做 T-5 定向门禁与相关回归（见测试记录）。
- 未执行 ASan/TSan、低版本 GL 上下文、真实 sandbox/Jolt 形状路径验证。
