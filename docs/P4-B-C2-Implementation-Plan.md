# P4-B C2 — Implementation Plan & Test Matrix

**状态**：`PLAN FROZEN — AWAITING SOURCE AUTHORIZATION`
**日期**：2026-11-01
**分支**：`avalonia`
**HEAD**：`b10e0cf84442602932d365c2445ce49bcaf8324a`
**前置**：`docs/P4-B-Production-3D-Viewport-GateA-Contract.md` (FROZEN)
**类型**：规划文档（未修改源码、未构建、未运行测试、未执行 Git 写入）

---

## 1. C2 目标

在不破坏现有 2D Presentation 和 EngineHost 单一 GL Context 所有权的前提下，
为生产 Renderer 增加真正的 3D Scene Rendering 能力。

**核心产出**：至少一个真实 Scene Entity 能提交 3D Geometry，并通过实际 Engine Renderer 绘制，
具有正确的 Model Transform、View/Projection、Depth Testing 和 Color/Depth FBO。

---

## 2. C2-0：Mesh / Scene 数据路径定向复核

### 2.1 现状确认

| 组件 | 文件 | CollectRenderCommands 覆盖 | 生产路径集成 |
|---|---|---|---|
| `SpriteComponent` | `engine/src/Core/GameObject/SpriteComponent.cpp` | ✅ 是 | ✅ RenderProductionFrame |
| `MeshComponent` | `engine/include/Engine/Core/GameObject/MeshComponent.h` | ❌ 否（默认空） | ❌ 否 |
| `MeshRendererComponent` | `engine/include/Engine/Core/GameObject/MeshRendererComponent.h` | ❌ 否（默认空） | ❌ 否 |
| `MeshRenderer` (RHI) | `engine/include/Engine/Core/RHI/MeshRenderer.h` | N/A（独立类） | ❌ 否 |
| `SceneRenderer` (sprite batch) | `engine/src/Core/RHI/SceneRenderer.cpp` | ✅ Sprite only | ✅ RenderProductionFrame |

**结论**：生产 Scene 无 3D Geometry 提交路径。`MeshComponent`/`MeshRendererComponent` 存在但不集成。
`MeshRenderer` 提供 3D 渲染能力但仅在 legacy/standalone 路径使用。

### 2.2 最小实现边界（D-P4B-9）

- **Option B 确认**：创建最小生产 StaticMesh 数据通路。
- 优先复用现有 `Mesh`（`Vertex3D` 格式）、`TransformComponent`、`GameObject`。
- 不扩展完整模型导入器，不重构 T-2 Content Pipeline。
- 不引入完整 PBR Material Editor。
- 不把测试专用三角形当成真实场景能力。

### 2.3 需要新增的最小组件

| 组件 | 说明 | 复用 |
|---|---|---|
| `StaticMeshComponent` (或复用 `MeshComponent`) | 持有 `Mesh` + `Transform`，覆盖 `CollectRenderCommands` | 复用 `MeshComponent` |
| `SceneRenderer3D` (或扩展 `SceneRenderer`) | 收集 3D mesh commands，提交 geometry | 新增或扩展 |
| `RenderProduction3DFrame` | Application 方法，3D 渲染入口 | 新增 |

**决策**：复用 `MeshComponent`（已有 `GetMesh()`/`HasMesh()`），新增 `CollectRenderCommands` 覆盖。
不创建重复的组件体系。

---

## 3. C2 细节澄清

### 3.1 矩阵约定

| 项目 | 值 | 证据 |
|---|---|---|
| 内存布局 | Column-major（OpenGL convention） | `Mat4` identity at indices 0,5,10,15；`Mat4Multiply` 计算 `out[col*4+row]` |
| 乘法约定 | `out = a * b`（column-major） | `MathTypes.h:209-219` |
| Clip Space | OpenGL NDC [-1,1]（Z ∈ [-1,1]） | `PerspectiveCamera::WorldToClip` 返回 NDC |
| View Matrix | `glm::lookAt` 风格（right-handed） | `ViewportPanel.cpp:202-204` |
| Projection Matrix | `glm::perspective` / `glm::ortho` | `ViewportPanel.cpp:195-201` |

**结论**：Mat4 是标准 OpenGL column-major。View/Projection 使用 glm 约定。
Managed 侧（如有矩阵计算）必须使用相同约定。C3 Camera 权威在 Native，managed 不计算矩阵。

### 3.2 Camera Near/Far Plane

| 参数 | PerspectiveCamera 默认 | ViewportPanel (legacy) | 建议 v1 |
|---|---|---|---|
| FOV | 45° | 60° | 45°（PerspectiveCamera 默认） |
| Near | 0.1 | 0.1 | 0.1 |
| Far | 100.0 | 1000.0 | 100.0（PerspectiveCamera 默认） |
| Aspect | 16/9 | fbW/fbH | framebuffer 宽高比 |

**边界行为**：
- Near ≤ 0 → 拒绝（NaN/Inf 风险）
- Far ≤ Near → 拒绝
- Aspect ≤ 0 → 使用 1.0（或拒绝）
- 零尺寸 viewport → Suspended，不更新 camera

### 3.3 StaticMesh Vertex 格式

| 字段 | 类型 | 说明 |
|---|---|---|
| position | Vec3 (float32 ×3) | 世界/局部坐标 |
| normal | Vec3 (float32 ×3) | 法线 |
| texCoord | Vec2 (float32 ×2) | UV |
| tangent | Vec3 (float32 ×3) | 切线（可选） |

- Index 格式：`uint32`（`std::vector<uint32>`）
- Transform：`TransformComponent`（GameObject 持有），提供 `GetWorldMatrixData()`
- 资源所有权：Mesh 数据由 `MeshComponent`/`MeshRendererComponent` 持有；GPU 资源由 Renderer 缓存管理

### 3.4 Material（v1 最小）

| 项目 | v1 方案 |
|---|---|
| Shader | 基础 3D shader（position + normal + texCoord，单光源 + 环境光） |
| 颜色来源 | MeshRendererComponent.TargetMaterial.BaseColor 或 MeshComponent.m_Color |
| 纹理 | v1 可选（无纹理时使用纯色） |
| 光照 | 单方向光 + 环境光（复用 MeshRenderer 的 Light 结构） |

**不引入**：PBR、多光源、阴影、SSAO、GBuffer（这些是 MeshRenderer 已有但 v1 不启用）。

### 3.5 DPI

遵循 Gate A D-P4B-6：
- Managed 计算 `PixelWidth = Round(LogicalWidth × RenderScaling)`
- Native 使用物理像素
- Camera Aspect Ratio 使用 framebuffer 尺寸
- Bitmap 使用匹配的 DPI 元数据

### 3.6 输入 ABI（D-P4B-5）

**C0 审计结论**：现有 capi.h 中无 EngineHost 级别的 viewport 输入方法。
EditorSession 级别的 `ViewportMouseMove/ViewportMouseButton/ViewportScroll/ViewportKeyDown` 存在但属于 legacy 路径。

**C2 结论**：C2 不需要输入 ABI（C2 只做 Geometry + Renderer）。输入 ABI 在 C4 中裁决。
C2 不升级 ABI。

---

## 4. C2 切片实施计划

### C2-0：Mesh/Scene 数据路径定向复核

**目标**：确认最小 StaticMesh 数据路径的实现边界。

**工作**：
- 检查 `MeshComponent` 是否需要 `CollectRenderCommands` 覆盖
- 检查 `GameObject::CollectRenderCommands` 是否需要 MeshComponent 分支
- 检查 `Scene::CollectRenderCommands` 是否需要 3D mesh 收集
- 检查 `MeshRenderer` 与 `SceneRenderer` 的关系（是否需要在 production path 中实例化 `MeshRenderer`）

**交付**：边界确认记录（可合并到 C2-1）

### C2-1：StaticMesh 提交路径

**目标**：真实 Entity 可提供 Geometry。

**工作**：
- 为 `MeshComponent`（或新增 `StaticMeshComponent`）添加 `CollectRenderCommands` 覆盖，提交 3D mesh 数据到 RenderQueue
- 确保 `TransformComponent` 的 world matrix 正确传递
- 确保 Mesh 资源（vertices/indices）可被 production renderer 访问

**验收**：
- Scene 中的 MeshComponent Entity 能被 `scene.CollectRenderCommands()` 收集
- RenderQueue 包含 3D mesh commands

### C2-2：SceneRenderer3D

**目标**：Geometry、Transform、Shader。

**工作**：
- 创建 `SceneRenderer3D`（或扩展 `SceneRenderer`）用于 3D mesh 渲染
- 接收 Camera View/Projection 矩阵
- 提交 geometry（VAO/VBO/IBO）
- 应用 Model Transform
- 基础光照（单方向光 + 环境光）

**验收**：
- 3D mesh 正确绘制
- Model Transform 正确应用
- View/Projection 正确

### C2-3：Color/Depth FBO

**目标**：深度正确、Resize 正确。

**工作**：
- 扩展 presentation FBO 为 Color + Depth Attachment
- Resize 时重建 Color + Depth
- 零尺寸时 Suspended（不创建资源）
- Depth Buffer 在每个 frame 开始时 Clear

**验收**：
- 深度遮挡正确
- Resize 后 Color/Depth 同步更新
- 无资源泄漏

### C2-4：生产渲染接线

**目标**：新增 `RenderProduction3DFrame`。

**工作**：
- 在 `Application` 中添加 `RenderProduction3DFrame(IRenderContext&, Scene&, targetWidth, targetHeight, EditorCamera&)`
- 在 `EngineHost::PumpOneFrame` 中调用（或新增独立 pump 路径）
- 保持 `RenderProductionFrame`（2D）不变
- 确保 3D 渲染在 runtime thread 执行

**验收**：
- 生产 path 能渲染 3D 场景
- 2D sprite 渲染回归通过

### C2-5：自动化验证

**目标**：Debug/Release、GL 像素测试、2D 回归。

**工作**：
- GL 像素测试：验证 3D mesh 渲染输出
- 2D 回归：`RenderProductionFrame` 行为不变
- Camera 矩阵正确性测试
- Depth 测试

**验收**：
- Debug/Release 构建通过
- 像素测试通过
- 2D 回归通过

---

## 5. C2 退出条件

必须同时满足：

1. ✅ 至少一个真实 Scene Entity 能提交 StaticMesh。
2. ✅ 生产 Renderer 能绘制真实 3D Geometry。
3. ✅ Model Transform 正确。
4. ✅ View / Projection 正确。
5. ✅ 深度遮挡正确。
6. ✅ Color / Depth Attachment 生命周期正确。
7. ✅ Resize 不发生资源泄漏。
8. ✅ 原有 2D Sprite 渲染回归通过。
9. ✅ Debug / Release 定向测试通过。

硬编码测试三角形、Sandbox 单独显示或手工观察不能替代这些门禁。

---

## 6. C2 不包含的内容

| 不包含 | 原因 |
|---|---|
| T-5 DebugDraw 接入 | C5 阶段 |
| Camera 控制（Orbit/Pan/Zoom） | C4 阶段 |
| Viewport 生命周期（Dock/Float/Redock） | C3 阶段 |
| ABI 输入传输 | C4 阶段（D-P4B-5 条件裁决） |
| DPI 转换 | C3 阶段（D-P4B-6） |
| 3D Picking | DEFERRED |
| RenderGraph SG6 | 隔离状态 |

---

## 7. C2 授权范围

授权包含：
- Engine 渲染相关源码修改（Application, SceneRenderer, MeshComponent 等）
- Renderer 测试
- 构建配置修改（如需要）

不包含：
- C3 Camera Controls
- C5 DebugDraw 接入
- Bridge ABI 升级（除非单独裁决）
- Git 写入（commit 需单独授权）

---

## 8. 当前状态

| 项目 | 状态 |
|---|---|
| C0 | `PASS — READ-ONLY COMPLETE` |
| C1 | `FROZEN` |
| C2 计划 | `FROZEN — AWAITING SOURCE AUTHORIZATION` |
| C2 源码实施 | `NOT YET AUTHORIZED` |
| 本轮源码修改 | 未执行 |
| 构建 / 测试 | 未执行 |
| Git 写入 | 未执行 |

下一步：等待用户授权 C2 源码实施。
