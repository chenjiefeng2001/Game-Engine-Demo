# P4-B Production 3D Viewport — Gate A Contract (FROZEN)

**状态**：`FROZEN — C1 COMPLETE`
**日期**：2026-11-01
**分支**：`avalonia`
**HEAD**：`b10e0cf84442602932d365c2445ce49bcaf8324a`
**前置**：`docs/P4-B-Production-3D-Viewport-Gate0-Scan.md`（PASS — READ-ONLY COMPLETE）
**Gate A 裁决**：8 项 APPROVED/FROZEN/CONDITIONAL/DEFERRED + D-P4B-9 补充

---

## 0. 冻结声明

本合同冻结 P4-B Production 3D Viewport 的架构决策。C2～C6 实施必须遵循本合同。
未在本合同中授权的修改不得执行。

---

## 1. Architecture Ownership (D-P4B-1)

### 1.1 渲染架构

**方案 A：复用 Engine Renderer，增加 3D 渲染能力。**

```
Avalonia Editor
      |
      v
EditorHostService
      |
      v
EngineHost
      |
      v
Runtime Thread
      |
      v
OpenGL 4.6 Context
      |
      v
Engine Renderer
      |
      v
Presentation FBO
      |
      v
RGBA Readback
      |
      v
ViewportImage
```

### 1.2 禁止事项

- Avalonia 自行创建第二个 GL Context。
- Legacy ImGui ViewportPanel 作为生产渲染入口。
- 新建与 Engine Renderer 平行的 Editor GL Renderer。
- GPU 资源由两个模块重复拥有。
- 引入 RenderGraph / SG6。

### 1.3 保留

- FBO Readback + Bitmap Presentation 方式（先验证性能，不提前改造）。
- 现有 2D Sprite 渲染行为（RenderProductionFrame 不变）。

---

## 2. EngineHost GL Context Lifecycle (D-P4B-2)

### 2.1 所有权矩阵

| 资源或操作 | Owner |
|---|---|
| GL Context | EngineHost |
| GL 操作 | Runtime Thread |
| FBO / Texture / VAO / VBO | Native Renderer |
| Camera 矩阵更新 | Runtime Thread |
| UI 输入采集 | Avalonia UI Thread |
| Bitmap Presentation | Avalonia UI Thread |

### 2.2 线程契约

- UI 线程负责 UI 和输入采集。
- Runtime Thread 负责所有 GL 操作。
- Camera 输入通过线程安全的命令传递到 Runtime Thread。
- 不允许 UI 线程直接调用 GL。

### 2.3 不变约束

- Context 所有权不因新增 3D Viewport 而改变。
- EngineHost 仍然是唯一 GL context owner。

---

## 3. Camera Authority and Matrix Convention (D-P4B-3)

### 3.1 数据权威

**Native EditorCamera 为唯一数据权威。**

Managed 侧仅采集输入，不自行维护 View/Projection 矩阵。

### 3.2 复用与不复用

| 复用 | 不复用 |
|---|---|
| 相机数学逻辑 | ImGui 输入依赖 |
| Orbit / Fly 计算 | Legacy FBO 生命周期 |
| View Matrix | Legacy SceneRenderCallback |
| Perspective Projection | Legacy ViewportPanel 资源所有权 |
| Camera 参数校验 | |

### 3.3 新增

生产 Viewport Camera 生命周期实例（非每帧重建），与 legacy ViewportPanel 解耦。

### 3.4 矩阵约定（待 C1 细化）

- 坐标系：右手坐标系。
- View/Projection 矩阵内存布局：需冻结（column-major / row-major）。
- FOV 单位：度。
- Near/Far Plane 合法范围：需定义。
- Viewport resize 时 Aspect Ratio 更新规则。
- 零尺寸和非法尺寸行为：Suspended，不创建资源。

---

## 4. Production 3D Rendering API (D-P4B-4)

### 4.1 新增独立入口

```
RenderProductionFrame(...);      // 保留现有 2D 行为（不变）
RenderProduction3DFrame(...);    // 新增 3D Scene Rendering
```

### 4.2 Renderer 结构

```
Engine Renderer
    |
    +-- SceneRenderer (现有 Sprite Batch, 2D)
    |
    +-- SceneRenderer3D (新增)
           |
           +-- Geometry Submission
           +-- Depth Testing
           +-- Camera View / Projection
           +-- 3D Scene Pass
```

### 4.3 要求

- 复用现有 Engine Renderer 所在模块。
- 不让 Sprite SceneRenderer 通过条件分支承担 3D 职责。
- 保留 2D 生产渲染行为（RenderProductionFrame 不变）。
- 3D 路径拥有正确的 Depth Attachment。
- FBO Resize 时同步处理 Color / Depth Attachment。
- Depth Buffer 必须在每个有效 3D Frame 开始时正确清除。
- 3D Rendering 在 EngineHost Runtime Thread 执行。

### 4.4 后端

OpenGL 4.6 为 v1 唯一后端。不接 RenderGraph SG6，不引入 RHI 多后端。

---

## 5. Scene Geometry Source (D-P4B-9)

### 5.1 C0 审计结论

生产 `SceneRenderer` 仅处理 `SpriteComponent`（2D）。`MeshComponent` 和 `MeshRendererComponent` 存在于 Engine 中，但**不集成** `CollectRenderCommands` 或 `RenderProductionFrame`。

`MeshRenderer` 类（`engine/include/Engine/Core/RHI/MeshRenderer.h`）提供 3D mesh 渲染能力（Forward/Deferred、Shadow、SSAO 等），但仅在 legacy/standalone 路径使用，未接入 production render path。

### 5.2 选项裁决

| 选项 | 说明 | 裁决 |
|---|---|---|
| A | 使用已有 Mesh/Model Component 和资产资源 | 部分可用（组件存在但未接入 production path） |
| B | 创建最小生产 StaticMesh 数据路径 | **必须** |
| C | 仅绘制硬编码测试三角形 | 不接受作为生产验收 |

### 5.3 约束

- 优先复用现有 Scene Entity、Transform 和 Mesh 资源。
- 如果现有 Scene 没有可用的 3D Mesh Component，明确新增最小 StaticMesh 能力。
- 不扩展完整模型导入器。
- 不重构 T-2 Content Pipeline。
- 不引入完整 PBR Material Editor。
- 不把测试专用三角形当成真实场景能力。

### 5.4 最低要求

至少要有一个真实 Scene Entity 能够提交 3D Geometry，并通过实际 Engine Renderer 绘制。

---

## 6. ABI and Input Transport (D-P4B-5)

### 6.1 优先复用

优先使用现有 EngineHost 命令通道传递 Camera 输入。

### 6.2 条件 additive ABI

如果现有通道无法表达 Camera 输入，则新增 additive ABI：

- 建议 EngineHost ABI 升级到 v1.10。
- 不修改现有 `RuntimeResult`。
- 不修改 `EV_COUNT`。
- 不修改 `EngineLogRecordV1`。
- 不修改 `EngineLogPollStatsV1`。
- 不改变 Pick Packet。
- 增加 Native / Managed Lockstep 测试。

传递内容：Mouse Delta、Mouse Button State、Wheel Delta、Camera Movement State、Input Sequence。
**不传递** Managed 计算的 View/Projection Matrix。

### 6.3 当前状态

C0 未证明现有 ABI 已有通用鼠标/键盘/Camera 输入传递接口。C2 实施前需定向核查。

---

## 7. Viewport Pixel / DPI Contract (D-P4B-6)

### 7.1 转换公式

```
PixelWidth  = Round(LogicalWidth  × RenderScaling)
PixelHeight = Round(LogicalHeight × RenderScaling)
```

### 7.2 职责分配

| 步骤 | 负责方 |
|---|---|
| 布局（逻辑尺寸） | Avalonia |
| 获取 RenderScaling | Managed（从 Visual Root / TopLevel） |
| 转换为物理像素 | Managed（一次转换） |
| Render Target 使用物理像素 | Native |
| Camera Aspect Ratio 使用 framebuffer 尺寸 | Native |
| Fetch 返回物理像素数据 | Native |
| Bitmap 使用匹配的 DPI 元数据 | Managed |

### 7.3 边界

- RenderScaling 改变时重新提交 Resize。
- 零尺寸时进入 Suspended，不创建零尺寸 FBO。
- 避免逻辑尺寸乘 DPI 两次。
- 避免 FBO 已使用物理像素但 Bitmap 又按物理尺寸直接占据 UI 布局。

### 7.4 测试矩阵

| Scaling | 逻辑尺寸 | 预期像素尺寸 |
|---|---|---|
| 100% | 800×600 | 800×600 |
| 125% | 800×600 | 1000×750 |
| 150% | 800×600 | 1200×900 |
| 200% | 800×600 | 1600×1200 |

---

## 8. Viewport Lifecycle and Dock Integration

### 8.1 生命周期状态

```
Create → Initialize → Resize → Render → Suspend → Resume → Dispose
```

### 8.2 场景行为

| 场景 | 预期 |
|---|---|
| 正常显示 | 可以提交帧 |
| Window Resize | 重建或调整 Render Target |
| Dock 隐藏 | 停止无效帧工作 |
| Dock 恢复 | 恢复正常渲染 |
| Float | 不重复拥有 GL 资源 |
| Redock | 不重复初始化 Renderer |
| Dispose | 安全释放 |
| App Shutdown | 不存在后台帧调用 |

### 8.3 尺寸未确定时

允许 Viewport 处于未就绪状态，不创建 0×0 资源。

---

## 9. T-5 DebugDraw Integration (D-P4B-7)

### 9.1 接入时机

C5，必须在真实 3D Scene Rendering 完成后。

### 9.2 渲染顺序

```
Scene Geometry
      |
      v
Scene Depth Buffer
      |
      v
T-5 Depth-Test Batch
      |
      v
T-5 X-Ray Batch
      |
      v
Presentation FBO
      |
      v
Readback
```

### 9.3 T-5 契约遵守

- GL 4.6
- Owner Thread（EngineHost runtime thread）
- BeginFrame / EndFrame
- 50,000 顶点预算
- Depth-On / X-Ray 批次
- GL 状态保存/恢复

### 9.4 不修改

- T-5 DebugDraw API（已关闭，不重开）
- Grid/Coordinate Axes 优先通过 T-5 实现

---

## 10. 3D Picking Exclusion (D-P4B-8)

v1 不包含 3D Picking。

原因：
- 生产 Pick Pass 默认关闭。
- 3D Picking 涉及对象身份、ID Attachment、深度关系与选择模型。
- 不是建立基本 3D Viewport 的前置条件。

现有 Pick Transport 保持原状。未来独立建立 3D Picking 任务。

---

## 11. Explicit Non-Goals

| 项目 | 说明 |
|---|---|
| T-5 DebugDraw API 修改 | 不授权 |
| T-6 Console 重开 | 不纳入 |
| RenderGraph SG6 恢复 | 保持隔离 |
| Vulkan / D3D12 | v1 非目标 |
| Transform Gizmo | 后续候选 |
| Material Editor | 非目标 |
| Animation Timeline | 非目标 |
| 多 Viewport | 非目标 |
| Full Model Import | 后续候选 |
| Jolt DebugRenderer | 独立任务 |
| Git Push / Release | 不授权 |

---

## 12. Test Acceptance Matrix

| 类别 | 必须覆盖 |
|---|---|
| Camera Unit Tests | View / Projection / Orbit / Pan / Zoom |
| Viewport Lifecycle | Initialize / Resize / Suspend / Resume / Dispose |
| GL Rendering | Scene / Depth / Framebuffer |
| GL State Isolation | 不影响后续 UI 渲染 |
| DebugDraw | 5 种图元、Depth、X-Ray |
| Input | Focus / Capture / Deactivate |
| Dock | Hide / Show / Float / Redock |
| DPI | 逻辑尺寸与 framebuffer 尺寸映射 |
| ABI | 如有扩展，Native/Managed lockstep |
| UIA | 真实生产窗口和控件路径 |
| 像素验证 | FBO readback，非仅 UIA 控件存在 |

---

## 13. C1 状态

| 项目 | 状态 |
|---|---|
| C0 Gate 0 | `PASS — READ-ONLY COMPLETE` |
| D-P4B-1..8 | `APPROVED — SCOPE-BOUND` |
| D-P4B-9 Scene Geometry Source | `APPROVED — Option B 必须` |
| Gate A Contract | `FROZEN` |
| C2～C6 源码实施 | 尚未授权 |
| 本轮源码修改 | 未执行 |
| 构建 / 测试 | 未执行 |
| Git 写入 | 未执行 |

---

## 14. 后续阶段依赖

```
C0 (PASS) → C1 (FROZEN) → C2 (3D Geometry + Renderer)
                                    ↓
                              C3 (Camera + Viewport Lifecycle + DPI)
                                    ↓
                              C4 (Camera Controls)
                                    ↓
                              C5 (T-5 DebugDraw Integration)
                                    ↓
                              C6 (Regression + Real Window + Closure)
```

C3、C4 可在架构允许的情况下并行开发，但最终验收依赖两者完成。
