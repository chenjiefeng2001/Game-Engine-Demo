# T-5 Generic DebugDraw — Gate C Implementation Record

**状态**：`T-5 Gate C — PASS — SCOPE-BOUND`（实现记录）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateC-Contract.md`
**Gate A/B**：`docs/T-5-DebugDraw-GateA-Contract.md`、`docs/T-5-DebugDraw-GateB-Closure-Record.md`

---

## 1. 交付范围

Gate C 只做真实帧接入，未改变 Gate B DebugDraw API/渲染器/适配器的语义。

### 1.1 RenderTest 3D

文件：`sandbox/src/RenderTest/RenderTestApp.h` / `.cpp`

- 移除旧 `OpenGLPhysicsDebugDraw3D` 成员与 `Clear/DebugDraw/Flush` 物理循环路径。
- 新增 `Rendering::DebugDraw::DebugDraw m_DebugDraw` 与 `PhysicsDebugDrawAdapter3D`。
- `InitPhysics()` 用 `OpenGL46DebugDrawRenderer` 初始化服务；失败记录日志，不启用调试绘制。
- `JoltPhysicsWorld::SetDebugDraw(adapter)`；析构时先清空物理世界裸指针，再关闭服务。
- 在 `passIdx_Aux` 内、网格之后：
  1. 由当前 camera 计算 VP；
  2. `BeginFrame(view, dt)`；
  3. `m_PhysicsWorld->DebugDraw()`（adapter 收集线段）；
  4. `EndFrame()` 实际提交 depth/X-Ray 批次。
- 不再在物理更新循环、场景 `ClearColor` 之前提交调试线。

### 1.2 PhysicsTest 2D

文件：`sandbox/src/PhysicsTest/PhysicsTestApp.h` / `.cpp`

- 移除旧 `OpenGLPhysicsDebugDraw` 成员。
- 新增 `DebugDraw m_DebugDraw` 与 `PhysicsDebugDrawAdapter2D`。
- 初始化时使用 `OpenGL46DebugDrawRenderer`；失败记录日志。
- flags 设置迁移到 adapter：`Shape | Joint | COM`。
- F1 开关现在切换 `SetDebugDraw(adapter/nullptr)`；F2～F5 通过 adapter 切换 flags。
- 在 sprite batch `End()` 之后执行 `BeginFrame` → `Box2DPhysicsWorld::DebugDraw()` → `EndFrame()`；不再依赖旧 renderer 的 `SetViewProjection` + 即时提交。
- 析构时清空物理世界裸指针、销毁 adapter、关闭服务。

### 1.3 Legacy ViewportPanel

文件：`engine/include/Engine/Editor/ViewportPanel.h`、`engine/src/Editor/ViewportPanel.cpp`

- 新增可选 `SetDebugRenderCallback(std::function<void(const float* viewProj, const float* cameraPos)>)`。
- 回调在 `m_SceneRenderCallback` 和 `EditorOverlay::DrawGrid` 之后、FBO unbind 之前执行。
- 回调不拥有 DebugDraw；调用方负责 `BeginFrame`/收集/`EndFrame` 和生命周期。
- 默认 callback 为空，旧行为不变；生产 `EngineHost`/Bridge 不使用该回调。

---

## 2. 帧顺序

最终顺序：

```text
BeginFrame(view, dt)
  -> collect primitives

scene render / ClearColor / sprite or mesh pass
  -> EndFrame()
     -> depth batch
     -> X-Ray batch
     -> restore GL state

UI / grid / editor overlay
```

新增测试 `CollectionBeforeSceneClearRendersAfterClear` 明确验证：收集后、`EndFrame` 前执行场景 Clear，线段仍能在 `EndFrame` 后可见。这覆盖 Gate B 扫描中发现的 RenderTest 旧顺序风险。

---

## 3. 生命周期与边界

- RenderTest/PhysicsTest 的 `DebugDraw` 在窗口上下文存在时初始化/关闭；成员声明顺序保证 service 在窗口之前析构。
- 物理世界只持有 adapter 裸指针；析构时显式 `SetDebugDraw(nullptr)`，避免 service 销毁后物理世界仍持有悬空指针。
- 未修改 `Box2DDebugDraw`、`JoltPhysicsWorld`、`JoltDebugRenderer`。
- 未接 EngineHost/Bridge/生产 Avalonia Editor；未接 RenderGraph/RHI 多后端。
- Gate C 阶段未修复 Jolt `DrawGeometry` 与碰撞体形状线框；后续独立 D3 任务已修复（`docs/T-5-DebugDraw-D3-Jolt-DrawGeometry-Fix.md`）。
- 2D 真实路径的 `Box2DDebugDraw` transform/flags 缺陷已在独立 D1 任务中修复（`docs/T-5-DebugDraw-D1-Box2D-DebugDraw-Fix.md`）；Gate C 本身只替换渲染后端。

---

## 4. 构建与测试

| 项目 | Debug | Release |
|---|---:|---:|
| `test_debugdraw` | 20/20 PASSED | 20/20 PASSED |
| 组合 CTest（debugdraw/pick/renderer/physics） | 4/4 PASSED | 4/4 PASSED |
| `RenderTest` 构建 | PASSED | PASSED |
| `PhysicsTest` 构建 | PASSED | PASSED |

新增门禁：

- `DebugDrawGLTest.CollectionBeforeSceneClearRendersAfterClear`
- `DebugDrawGLTest.ViewportPanelDebugCallbackRunsAfterScene`

沙盒启动冒烟（4 秒后由 `timeout` 终止，exit 124 属预期）：

- `RenderTest.exe`：Jolt/ImGui 初始化完成，无 DebugDraw 初始化失败日志。
- `PhysicsTest.exe`：物理运行、碰撞回调持续输出，无崩溃/致命日志。

构建警告：每次 `EngineCore` 构建保留 2 条既有 LNK4006（`RHI::IsBackendAvailable` / `RHI::GetDefaultBackend`）；无零警告声明。
