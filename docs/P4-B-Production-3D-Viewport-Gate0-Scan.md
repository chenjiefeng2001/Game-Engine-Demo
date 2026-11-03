# P4-B Production 3D Viewport — Gate 0 Scan

**状态**：`PASS — READ-ONLY COMPLETE`
**日期**：2026-11-01
**分支**：`avalonia`
**HEAD**：`b10e0cf84442602932d365c2445ce49bcaf8324a`
**审计类型**：只读（未修改源码、未构建、未运行测试、未执行 Git 写入）

---

## 1. 当前基线检查

| 项目 | 值 |
|---|---|
| 分支 | `avalonia` |
| HEAD | `b10e0cf` (docs(t-6-b): close gate B with full regression (B6)) |
| 工作区 | 3 个脏子模块 + `crashes/` (115) + 4 个未跟踪文档（P0/P1/P2/P3 交付物） |
| T-5 关闭记录 | `docs/T-5-DebugDraw-v1-Final-Closure-Record.md` — PASS (SCOPE-BOUND / CLOSED) |
| T-6 关闭记录 | `docs/T-6-Closure-Record.md` — 已更新为 T-6 COMPLETE (P1) |
| T-7 关闭记录 | `docs/T-7-B-Closure-Record.md` — PASS (SCOPE-BOUND / CLOSED) |
| UI-H2 关闭记录 | `docs/UI-H2-P2-Closure-Record.md` — PASS (SCOPE-BOUND / CLOSED) |
| F2-S1 关闭记录 | `docs/F2-S1-Closure-Record.md` — CLOSED (SCOPE-BOUND) |
| P3 交付裁决 | `READY WITH ACCEPTED EXCEPTIONS` |

所有关闭记录与当前 HEAD 一致。无未提交的源码修改（仅文档和历史遗留 dirty）。

---

## 2. Renderer 现状扫描

### 2.1 生产渲染链路

生产 Editor 使用 **EngineHost runtime thread** 上的 **hidden OpenGL context**（`OpenGLGraphicsFactory`），
通过 `Application::RenderProductionFrame` 渲染到 **presentation FBO**，然后 readback 为 RGBA 像素，
经 ABI Fetch 传输到 managed 侧，解码为 `WriteableBitmap`，绑定到 `ViewportImage`。

链路：

```
managed DispatcherTimer (33ms)
  → EditorHostService.PumpOnce()
    → EngineHost_PumpOneFrame (C ABI)
      → runtime thread: MakeRuntimeContextCurrent
        → Application::RenderProductionFrame(context, scene, w, h)
          → InitProductionRenderResources (lazy: sprite shader + SceneRenderer + OrthographicCamera)
          → scene.CollectRenderCommands(probeQueue)
          → fit OrthographicCamera to sprite AABB (+10% margin)
          → SceneRenderer::Render(scene)  [sprite batch, 2D]
        → unbind presentation FBO
        → service pick request
      → UncurrentPersistentContext
    → managed: TryFetchPresentationFrame → decode → vertical flip → ViewportImage.Source
```

### 2.2 OpenGL Context 所有权

| 项目 | 实现 |
|---|---|
| 创建者 | `EngineHost::InitializeGraphics` → `OpenGLGraphicsFactory::InitializePersistentHiddenContext(64,64)` |
| 持有者 | `EngineHost`（`m_GraphicsFactory` unique_ptr, `m_RenderContext` unique_ptr） |
| 销毁者 | `EngineHost::TeardownGraphics` |
| 线程 | runtime thread（`m_RuntimeThread = std::jthread(&EngineHost::RuntimeLoop, this)`） |
| make-current | `MakeRuntimeContextCurrent()` / `UncurrentPersistentContext()` |

**单一 Context 所有权**：EngineHost 是唯一 owner。Avalonia 侧不创建/持有 GL context。

### 2.3 Frame Lifecycle

- `EngineHost::PumpOneFrame(dt, result)` → `Submit(...)` → runtime thread 执行。
- Runtime thread: make context current → tick session → clear+bind presentation FBO → `RenderProductionFrame` → unbind → service pick → uncurrent.
- Managed side: `DispatcherTimer` (33ms) 调用 `PumpOnce`，然后 `TryFetchPresentationFrame` readback。

### 2.4 Render Target

- `m_PresentationFbo`：production presentation target（与 64×64 probe FBO 独立）。
- `m_OffscreenFbo`：64×64 探针 FBO（用于 graphics state 验证）。
- FBO 创建：`InitProductionRenderResources` 或 `ResizeViewportFBO`。
- Readback：`ReadPresentationPixels` → `glReadPixels` → RGBA bytes → FetchPresentationFrame ABI。

### 2.5 Scene Renderer

- `SceneRenderer`（`engine/src/Core/RHI/SceneRenderer.cpp`）：sprite batch renderer。
- `SetCamera(OrthographicCamera*)`，`SetShader(shared_ptr<Shader>)`。
- `Render(Scene&, defaultTexture)`：收集 RenderCommands，按 texture 批处理，绘制。
- **仅支持 2D sprite 渲染**（SpriteComponent），使用 OrthographicCamera。

### 2.6 Camera

| 类 | 用途 | 状态 |
|---|---|---|
| `OrthographicCamera` | Production path（sprite AABB 适配） | ✅ 存在 |
| `PerspectiveCamera` | 通用透视相机 | ✅ 存在 |
| `EditorCamera` | 3D 编辑器相机（orbit/fly） | ✅ 存在（legacy ViewportPanel 使用） |
| `m_ProductionCamera` | Production path 的 OrthographicCamera | ✅ 存在 |

**关键发现**：生产渲染路径 (`RenderProductionFrame`) 使用 `OrthographicCamera` 适配 2D sprites，
**不渲染 3D 场景**。`EditorCamera`（perspective）仅在 legacy `ViewportPanel`（ImGui）中使用。

### 2.7 资源管理

- Shader：`m_ProductionSpriteShader`（shared_ptr，lazy init）
- SceneRenderer：`m_ProductionSceneRenderer`（shared_ptr，lazy init）
- Camera：`m_ProductionCamera`（unique_ptr，每帧重建）
- Pick shader/scene renderer：lazy init（pick pass 默认关闭）
- FBO：`m_PresentationFbo`（unique_ptr，resize 时重建）

### 2.8 线程模型

- **Runtime thread**（EngineHost 内）：所有 GL 操作、session tick、render、pick。
- **Managed/UI thread**（Avalonia）：pump timer (33ms)、frame fetch、bitmap decode、UI binding。
- 通信：C ABI（EngineHost_PumpOneFrame, EngineHost_FetchPresentationFrame）。
- **GL 操作仅在 runtime thread**，UI 线程不直接调用 GL。

### 2.9 Resize

- Managed: `EditorHostService.SetViewportSize(w, h)` → `EngineHost_SetViewportSize` (C ABI) → `EngineHost::SetViewportSize` → `ResizeViewportFBO`。
- Resize 时重建 presentation FBO。
- 零尺寸（w==0 或 h==0）返回 false，不分配资源。

### 2.10 DPI

- 当前生产路径：managed 传入 logical 尺寸（w, h），native 直接使用。
- **未见 explicit DPI scaling**：未发现 device pixel ratio 转换。
- `ViewportFramePresenter` 处理 vertical flip（GL→Avalonia），但未处理 DPI。

---

## 3. Editor 现状扫描

### 3.1 ViewportPanel (Legacy ImGui)

- `engine/src/Editor/ViewportPanel.cpp`：legacy 3D viewport，使用 ImGui FBO readback。
- `Render3DScene()`：MRT FBO（color + R32I ID），使用 `EditorCamera`（perspective/orthographic），调用 `m_SceneRenderCallback`（外部注册）。
- `CopyReadbackPixels`：readback for Avalonia（Phase 7）。
- **这是 legacy 路径**，Avalonia production 不使用此 ViewportPanel。

### 3.2 Avalonia Viewport

- `MainWindow.axaml:125-155`：`ViewportHost` Grid → `ViewportBorder` → `ViewportImage` (Image, Stretch=None, Center)。
- `ViewportFramePresenter`：presents fetched frame into WriteableBitmap，vertical flip。
- `EditorHostService`：`TryFetchPresentationFrame()`、`SetViewportSize()`、`GetViewportSize()`。

### 3.3 生产 Viewport 的当前状态

生产 Avalonia Editor 的 ViewportImage 显示的是 **2D sprite 渲染**（`RenderProductionFrame`），不是 3D 场景。

- 场景内容：SpriteComponent（2D sprites）。
- 相机：OrthographicCamera 自动适配 sprite AABB。
- 无 3D 相机控制（orbit/pan/zoom）。
- 无深度测试（sprite batch）。
- 无 3D picking（pick pass 默认关闭）。

### 3.4 关键发现

**生产 Editor 当前没有 3D 场景渲染能力。** RenderProductionFrame 仅渲染 2D sprites。
ViewportPanel (legacy) 有 3D 渲染能力（EditorCamera + SceneRenderCallback），但 Avalonia production 路径不使用它。

---

## 4. T-5 接入条件

### 4.1 T-5 通用 DebugDraw 契约

- `DebugDraw::Initialize(context)` / `Shutdown()`
- `BeginFrame(viewMatrix, projectionMatrix, deltaTime)` / `EndFrame()`
- Owner thread：GL context owner thread
- GL 4.6 context 检查
- GL 状态保存/恢复
- 50,000 顶点预算
- Depth-On / X-Ray 批次

### 4.2 生产接入可行性

- T-5 需要 GL 4.6 context → EngineHost runtime thread 已有 ✅
- T-5 需要 BeginFrame/EndFrame 调用 → 需在 RenderProductionFrame 中插入 ✅（可扩展）
- T-5 需要 View/Projection 矩阵 → 当前 production path 使用 OrthographicCamera（2D），无 3D View/Projection
- **阻塞**：T-5 DebugDraw 需要 3D View/Projection 矩阵，但生产 path 无 3D 相机

### 4.3 结论

T-5 可在生产 path 中接入，但**前提**是生产 path 具备 3D 相机和 View/Projection 矩阵。
当前 production path 仅 2D，需先扩展为 3D 才能接入 T-5。

---

## 5. 可复用的 Engine 渲染能力

| 能力 | 位置 | 可复用性 |
|---|---|---|
| OpenGLGraphicsFactory (hidden context) | `engine/src/OpenGL/OpenGLGraphicsFactory.cpp` | ✅ 直接复用 |
| SceneRenderer (sprite batch) | `engine/src/Core/RHI/SceneRenderer.cpp` | ⚠️ 仅 2D sprite |
| EditorCamera (orbit/fly) | `engine/src/Editor/EditorCamera.cpp` | ✅ 可复用（3D 相机） |
| PerspectiveCamera | `engine/src/Core/Renderer/PerspectiveCamera.cpp` | ✅ 可复用 |
| OrthographicCamera | `engine/src/Core/Renderer/OrthographicCamera.cpp` | ✅ 可复用 |
| ViewportPanel (legacy 3D) | `engine/src/Editor/ViewportPanel.cpp` | ⚠️ legacy ImGui 路径，不可直接用于 production |
| OpenGLFramebuffer | `engine/src/OpenGL/OpenGLFramebuffer.cpp` | ✅ 可复用 |
| DebugDraw (T-5) | `engine/src/Core/DebugDraw.cpp` | ✅ 可复用（需 GL 4.6 context + View/Proj） |
| Grid/Overlay | `ViewportPanel` 内部 | ⚠️ 依赖 legacy ViewportPanel |

---

## 6. 多线程及尺寸同步风险

| 风险 | 说明 | 严重度 |
|---|---|---|
| GL context 单一所有权 | EngineHost 是唯一 owner，UI 线程不直接调用 GL ✅ | 低 |
| Resize 资源重建 | `ResizeViewportFBO` 在 runtime thread 执行，managed 传入尺寸 ✅ | 低 |
| 零尺寸处理 | `targetWidth==0 \|\| targetHeight==0` 返回 false ✅ | 低 |
| DPI 未处理 | managed 传 logical 尺寸，native 直接使用，无 device pixel ratio 转换 | 中 |
| 3D 相机矩阵线程安全 | EditorCamera 未被生产 path 使用，无并发风险 | 低 |
| Dock Hide/Show | managed pump timer 仍运行，但 `PumpOneFrame` 可能失败（无 renderable） | 中 |
| Float/Redock | ViewportImage 重新绑定，frame fetch 仍正常 | 低 |

---

## 7. 必须新增和禁止修改的模块

### 必须新增（P4-B 范围内）

| 模块 | 说明 |
|---|---|
| 3D 相机管理 | 生产 path 需要 EditorCamera 或 PerspectiveCamera 集成 |
| 3D 场景渲染 | RenderProductionFrame 需扩展为支持 3D geometry |
| Viewport 控制器 | Orbit/Pan/Zoom 输入处理 |
| DebugDraw 生产接入 | 在 RenderProductionFrame 中调用 T-5 BeginFrame/EndFrame |
| DPI 转换 | logical → device pixel ratio |

### 禁止修改

| 模块 | 原因 |
|---|---|
| RenderGraph / SG6 | 隔离状态，独立工程 |
| T-5 DebugDraw API | 已关闭，不重开 |
| Bridge ABI（现有结构体） | 除非 C1 证明必要 |
| EngineHost GL context 所有权 | 已冻结，单一 owner |
| T-6 Console | 不纳入 |

---

## 8. 最小实现方案

基于 C0 发现，最小可行方案：

1. **C1**: 冻结 Camera 契约（EditorCamera → View/Projection 矩阵），冻结 Viewport 生命周期。
2. **C2**: 在 EngineHost runtime thread 中创建 3D-capable render target（扩展 RenderProductionFrame 或新增 RenderProduction3DFrame）。
3. **C3**: 集成 EditorCamera，渲染真实 3D 场景（至少一个 mesh + grid + axes）。
4. **C4**: 添加 camera 控制（orbit/pan/zoom）。
5. **C5**: 在 production render path 中接入 T-5 DebugDraw（BeginFrame/EndFrame）。
6. **C6**: 真实窗口 + UIA + 像素验证。

### 关键决策点

- **RenderProductionFrame 是否扩展为 3D**：当前仅 2D sprites。需要新增 3D 渲染路径或扩展 SceneRenderer 为 3D。
- **Camera 数据权威**：建议 EditorCamera 为唯一权威，managed 侧仅发送输入事件，native 侧计算 View/Projection。
- **ABI 扩展必要性**：Camera 参数可通过现有 EngineHost_SetViewportSize + 新增 Camera 参数 ABI，或通过 scene state 间接控制。需 C1 裁决。

---

## 9. Gate A 待裁决事项

| # | 事项 | 选项 | 建议 |
|---|---|---|---|
| 1 | 渲染架构 | A: 复用 Engine Renderer / B: Editor 独立 | A（首选） |
| 2 | GL Context 所有权 | 单一 owner (EngineHost) | 保持现有 |
| 3 | Camera 数据权威 | Native EditorCamera / Managed 计算 | Native（避免双数学） |
| 4 | 3D 场景渲染 API | 扩展 RenderProductionFrame / 新增方法 | 需 C1 裁决 |
| 5 | ABI 扩展 | 复用现有 / 新增 additive | 优先复用，必要时 additive |
| 6 | DPI 处理 | logical 直传 / device pixel ratio | 需 C1 裁决 |
| 7 | T-5 接入时机 | C3 后 / C5 | C5（先有 3D 场景） |
| 8 | 3D Picking | v1 包含 / 延后 | 延后（独立决定） |

---

## 10. C0 结论

**`PASS — READ-ONLY COMPLETE`**

### 三个关键事实确认

1. **生产 Engine Renderer 是否已经能够真实渲染 3D Scene？**
   ❌ **否**。`RenderProductionFrame` 仅渲染 2D sprites（SpriteComponent），
   使用 OrthographicCamera 适配 sprite AABB。无 3D geometry 渲染。

2. **生产 Editor 是否能够安全地获得并呈现 Renderer 输出？**
   ✅ **是**。EngineHost runtime thread → presentation FBO → readback → ABI Fetch → managed decode → ViewportImage。
   链路已验证（T6ConsoleUITests + HRC-3）。

3. **GL Context、渲染线程和 Viewport 生命周期能否形成单一、可验证的所有权关系？**
   ✅ **是**。EngineHost 是唯一 GL context owner，runtime thread 是唯一 GL 调用线程。
   Resize/Dispose 有明确路径。Dock/Float/Redock 有生命周期管理。

### C0 阻塞项

- **无 BLOCKED**。生产 3D 场景渲染缺失是功能缺口，不是架构阻塞。
  RenderProductionFrame 可扩展为 3D，GL context 和线程模型已就绪。

### 建议的下一步

进入 C1（Architecture & Contract Freeze），裁决 Gate A 的 8 个待裁决事项，
特别是 3D 场景渲染 API 和 Camera 数据权威。
