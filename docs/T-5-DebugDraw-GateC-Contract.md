# T-5 Generic DebugDraw — Gate C Contract

**状态**：`T-5 Gate C — PASS — SCOPE-BOUND`（待用户最终验收）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**上位记录**：`docs/T-5-DebugDraw-GateA-Contract.md`、`docs/T-5-DebugDraw-GateB-Closure-Record.md`
**实现/测试/关闭**：`docs/T-5-DebugDraw-GateC-Implementation-Record.md`、`docs/T-5-DebugDraw-GateC-Test-Record.md`、`docs/T-5-DebugDraw-GateC-Closure-Record.md`

Gate C 是 Gate B 之后的 **真实帧接入切片**，不改变 Gate A 契约。用户选择的 Gate C 范围是：

> Sandbox + Legacy 集成：接入 RenderTest/PhysicsTest 与 legacy `ViewportPanel`，验证真实帧中的 ClearColor 顺序、可见像素、与 grid/ImGui 共存和状态隔离；不接 EngineHost/Bridge/生产 Editor。

---

## 1. 范围

### 1.1 允许

- `sandbox/src/RenderTest`：用 Gate B DebugDraw 服务替换旧的 `OpenGLPhysicsDebugDraw3D` 手动路径。
- `sandbox/src/PhysicsTest`：用 Gate B DebugDraw 服务 + `PhysicsDebugDrawAdapter2D` 替换旧的 `OpenGLPhysicsDebugDraw` 路径。
- `engine/include/Engine/Editor/ViewportPanel.h` / `engine/src/Editor/ViewportPanel.cpp`：增加**可选** Debug Render 回调；只属于 legacy 编辑器路径。
- `test_debugdraw` 增加真实帧顺序与集成门禁。
- 必要的 CMake 接线和 Gate C 文档。

### 1.2 非目标（仍不允许）

- EngineHost / Bridge / 生产 Avalonia Editor 接线。
- RenderGraph / RHI 多后端接入。
- 修复 `Box2DDebugDraw` transform/flags 缺陷。
- 修复 Jolt `DrawGeometry` / 碰撞体形状线框。
- 3D 文本、实体填充、CVar/Console 开关、TLS。
- 改变 Gate A 的 owner-thread、50k 预算、depth/X-Ray 或 API 语义。
- Git push；本轮只允许本地 commit（如需）。

---

## 2. 真实帧顺序契约

DebugDraw 的收集与提交必须分离：

```text
BeginFrame(view, deltaTimeSeconds)
  -> collect primitives (physics adapters or direct calls)

scene render / ClearColor
  -> 3D scene or 2D sprite pass完成

EndFrame()
  -> depth batch + X-Ray batch提交，随后 UI
```

强制要求：

- 不允许在场景 `ClearColor` 之前调用 `EndFrame()`；
- 不允许把旧实现的 `Clear/DebugDraw/Flush` 顺序原样保留；
- 3D RenderTest 在 `passIdx_Aux` 结束前调用 `EndFrame()`；
- 2D PhysicsTest 在 sprite batch `End()` 之后调用 `EndFrame()`；
- ViewportPanel 通过可选回调在场景渲染和 grid 之后、FBO unbind 之前调用调用方提供的提交逻辑；
- 回调/服务失败必须可观测；不得静默跳过。

---

## 3. 接入点

### 3.1 RenderTest 3D

- `RenderTestApp` 持有 `Rendering::DebugDraw::DebugDraw` 与 `PhysicsDebugDrawAdapter3D`。
- `JoltPhysicsWorld::SetDebugDraw(adapter)`；不再使用 `OpenGLPhysicsDebugDraw3D`。
- 物理更新循环不再调用 debug `Clear/DebugDraw/Flush`。
- 相机 VP 构造后：`BeginFrame` → `m_PhysicsWorld->DebugDraw()` → `EndFrame`，位于 `passIdx_Aux` 内、网格之后。
- 不修 Jolt 形状：可见内容由现有 Jolt DebugRenderer 输出决定；Gate C 只验证通用服务真实帧接入。

### 3.2 PhysicsTest 2D

- `PhysicsTestApp` 持有 `DebugDraw` 与 `PhysicsDebugDrawAdapter2D`。
- `SetDebugDraw(adapter)`；不再使用 `OpenGLPhysicsDebugDraw`。
- 在 sprite batch `End()` 之后、帧结束前执行 `BeginFrame` → `m_PhysicsWorld->DebugDraw()` → `EndFrame`。
- flags 通过 adapter 设置；实体回调只输出轮廓。

### 3.3 Legacy ViewportPanel

- 新增 `SetDebugRenderCallback(std::function<void(const float* viewProj, const float* cameraPos)>)`。
- 回调在 `m_SceneRenderCallback` 与 `EditorOverlay::DrawGrid` 之后、FBO unbind 之前执行。
- 回调不拥有 DebugDraw；调用方负责 Begin/collect/End 与生命周期。
- 生产 `EngineHost`/Bridge 不使用该回调；默认 nullptr，行为与之前一致。

---

## 4. Gate C 验收

| 门禁 | 要求 |
|---|---|
| 构建 | RenderTest、PhysicsTest Debug/Release 构建成功 |
| 顺序 | 新增测试证明“收集 → 场景 Clear → EndFrame”仍产生像素；EndFrame 不在 Clear 前 |
| 可见性 | 真实 FBO 中线段/图元产生非背景像素 |
| 状态隔离 | Debug 绘制前后关键 GL 状态恢复；后续 grid/UI 不受污染 |
| ViewportPanel | 回调只在场景/grid 之后、FBO unbind 前调用；默认无回调时行为不变 |
| 回归 | `test_debugdraw` 18+N；`test_pick`/`test_renderer`/`test_physics` 定向回归 |
| 边界 | 未修改 EngineHost/Bridge/生产 Editor、RenderGraph/RHI、Box2DDebugDraw/Jolt 实现 |

未验证项必须列入 Gate C 测试/关闭记录，不能用 sandbox 手动观察替代自动化像素门禁。

---

## 5. 当前裁决

Gate C 获用户授权，范围限定为 §1.1；不得据本记录扩展到 §1.2。
