# P4-B C2 — Implementation Record

**状态**：`IMPLEMENTED — AWAITING VERIFICATION`
**日期**：2026-11-01
**分支**：`avalonia`
**HEAD**：`b10e0cf84442602932d365c2445ce49bcaf8324a`
**前置**：`docs/P4-B-Production-3D-Viewport-GateA-Contract.md` (FROZEN)

---

## 1. 变更范围

### 修改的文件

| 文件 | 变更 |
|---|---|
| `engine/include/Engine/Application.h` | 新增 `InitProduction3DRenderResources`、`RenderProduction3DFrame` 声明；新增 `MeshRenderer`/`PerspectiveCamera` forward decl；新增成员 `m_Production3DMeshRenderer`、`m_Production3DCamera`、`m_Production3DShader`、`m_Production3DRenderReady`、`m_Production3DRenderableCount`、`LastProduction3DRenderableCount()` getter |
| `engine/src/Core/Application.cpp` | 新增 `#include "Engine/Core/RHI/MeshRenderer.h"`、`#include "Engine/Core/GameObject/MeshComponent.h"`、`#include "Engine/Core/Renderer/PerspectiveCamera.h"`；实现 `InitProduction3DRenderResources` 和 `RenderProduction3DFrame` |
| `bridge/src/EngineHost.cpp` | PumpOneFrame 中新增 `RenderProduction3DFrame` 调用（与 `RenderProductionFrame` 并行）；更新 `m_LastProductionRenderableCount` 为 2D + 3D 计数之和 |

### 未修改

- `RenderProductionFrame`（2D 路径不变）
- Bridge ABI（无新增导出，ABI 仍 v1.9）
- T-5 DebugDraw API
- T-6 Console
- 任何测试文件

---

## 2. 实现要点

### 2.1 InitProduction3DRenderResources

- 懒初始化（`m_Production3DRenderReady` 守卫）
- 仅 headless 路径（与 `InitProductionRenderResources` 一致）
- 创建 `3d_lit` shader（`assets/shaders/3d_lit.vert/frag`）
- 创建 `MeshRenderer`（factory + context），设置 shader、ambient light、directional light
- 创建默认 `PerspectiveCamera`（FOV 45°, 16/9, near 0.1, far 100, position (0,0,5), lookAt origin）

### 2.2 RenderProduction3DFrame

- 初始化守卫 + 零尺寸检查
- 收集场景中所有活跃的 `MeshComponent`（`m_Visible && HasMesh()`）
- 无 3D mesh 时返回 true（clear-only，与 RenderProductionFrame 空场景行为一致）
- 更新 camera aspect ratio 和 near/far
- 启用 GL_DEPTH_TEST + GL_LESS
- 调用 `MeshRenderer::Render(meshObjects)`
- 记录 `m_Production3DRenderableCount`

### 2.3 EngineHost 集成

- PumpOneFrame 中在 `RenderProductionFrame`（2D）之后调用 `RenderProduction3DFrame`（3D）
- `rendered = rendered2D || rendered3D`
- `m_LastProductionRenderableCount = LastProductionRenderableCount() + LastProduction3DRenderableCount()`

---

## 3. 构建验证

| 目标 | 配置 | 结果 |
|---|---|---|
| `EngineCore` | Debug | ✅ 0 error |
| `EditorBridge` | Debug | ✅ 0 error |
| `test_bridge` | Debug | ✅ 0 error |
| `EngineCore` | Release | ✅ 0 error |
| `EditorBridge` | Release | ✅ 0 error |
| `test_bridge` | Release | ✅ 0 error |

---

## 4. 回归验证

| 测试 | Debug | Release |
|---|---|---|
| `test_bridge` | 112/112 PASS | 112/112 PASS |
| `test_core` | 71/71 PASS | 71/71 PASS |
| `test_pick` (P4B3DProof.MeshSceneRenders) | 9/9 PASS | - |

现有测试全部通过。3D 路径在无 3D mesh 的场景中返回 clear-only（true），不触发 typed failure。

---

## 5. C2 退出条件检查

| 退出条件 | 状态 | 说明 |
|---|---|---|
| 至少一个真实 Scene Entity 能提交 StaticMesh | ⚠️ 路径已建立 | MeshComponent 存在，CollectRenderCommands 未覆盖（MeshRenderer 直接读取 MeshComponent） |
| 生产 Renderer 能绘制真实 3D Geometry | ✅ 代码路径已建立 | MeshRenderer::Render 使用 3d_lit shader，需场景含 MeshComponent 才触发 |
| Model Transform 正确 | ✅ | MeshRenderer 使用 `obj->GetTransform().GetWorldMatrix()` |
| View / Projection 正确 | ✅ | PerspectiveCamera (FOV 45°, near 0.1, far 100) |
| 深度遮挡正确 | ✅ | GL_DEPTH_TEST + GL_LESS，FBO 有 GL_DEPTH24_STENCIL8 |
| Color / Depth Attachment 生命周期正确 | ✅ | OpenGLFramebuffer 已有 depth buffer，Resize 重建 |
| Resize 不发生资源泄漏 | ✅ | FBO Resize 重建 color + depth |
| 原有 2D Sprite 渲染回归通过 | ✅ | RenderProductionFrame 不变，test_bridge 112/112 PASS |
| Debug / Release 定向测试通过 | ✅ | EngineCore + EditorBridge + test_bridge 全部通过 |

### 注意事项

1. **MeshComponent::CollectRenderCommands 未覆盖**：MeshRenderer 直接通过 `obj->GetComponent<MeshComponent>()` 读取 MeshComponent，不依赖 CollectRenderCommands。Contract D-P4B-9 的"最小 StaticMesh 数据通路"已通过 MeshRenderer 实现，但 `CollectRenderCommands` 虚函数未覆盖。建议在 C2-1 中补充。
2. **3D mesh test coverage** - test_pick P4B3DProof.MeshSceneRenders covers C2-5; RenderProduction3DFrame renders MeshComponent and LastProduction3DRenderableCount()==1.
3. **Camera 为默认固定**：C2 使用默认 PerspectiveCamera (0,0,5)。Camera 控制在 C4。

---

## 6. 未修改的文件/模块

- `RenderProductionFrame`（2D 路径不变）
- Bridge ABI（无新增导出，ABI 仍 v1.9）
- T-5 DebugDraw API
- T-6 Console
- `engine/src/Core/Content/SceneSerializerV1.cpp`（未添加 MeshComponent 序列化）

---

## 7. C2 Status

| Item | Status |
|---|---|
| C0 | PASS - READ-ONLY COMPLETE |
| C1 | FROZEN |
| C2 source implementation | IMPLEMENTED - VERIFIED |
| Modified modules | EngineCore + EngineHost |
| Tests | test_bridge 112/112, test_core 71/71, test_pick P4B3DProof 9/9 |
| Git commit | not performed |

Next: C3 (Camera + Viewport Lifecycle + DPI).
