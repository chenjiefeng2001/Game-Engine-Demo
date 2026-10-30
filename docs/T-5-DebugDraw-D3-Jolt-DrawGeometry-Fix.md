# T-5 D3 — Jolt DrawGeometry / Shape Wireframe Fix

**状态**：`T-5 D3 — PASS — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**上游记录**：`docs/T-5-DebugDraw-D1-Box2D-DebugDraw-Fix.md`、`docs/T-5-DebugDraw-GateC-Closure-Record.md`

---

## 1. 根因

Jolt 3D 调试形状此前实际不可见，有两个独立原因：

1. `JoltDebugRenderer::CreateTriangleBatch` 返回空 batch，`DrawGeometry` 是空实现，因此 Jolt 的 `Shape::Draw -> DrawGeometry` 几何被丢弃。
2. `JoltPhysicsWorld::DebugDraw()` 调用的是 `IPhysicsDebugDraw3D` adapter 的 `Clear/Flush`，而不是 `JoltDebugRenderer` 的 TLS `Clear/Flush`。adapter 的 `Clear/Flush` 是 no-op，所以即使 `DrawLine` 收集了 TLS 线，也没有提交到通用 DebugDraw 服务。

---

## 2. 修复

文件：

- `engine/include/Engine/Jolt/JoltDebugRenderer.h`
- `engine/src/Jolt/JoltDebugRenderer.cpp`
- `engine/src/Jolt/JoltPhysicsWorld.cpp`

实现：

- `JoltDebugRenderer` 改为继承 `JPH::DebugRendererSimple`；删除空的 `CreateTriangleBatch` / `DrawGeometry` 覆盖，复用 Jolt 自带的三角形批处理与 `DrawGeometry`。
- 保留现有 `DrawLine` TLS 收集、`DrawTriangle` → 3 条线、`DrawText3D` 转发；wireframe 模式由 Jolt `DebugRendererSimple` 调用 `DrawLine`，因此形状轮廓进入 adapter。
- `JoltPhysicsWorld::DebugDraw()` 在 Debug 路径改为：
  - `m_DebugRenderer->Clear()`
  - `m_PhysicsSystem.DrawBodies(...)`
  - `m_DebugRenderer->Flush()`
- 未修改 `third_party/JoltPhysics` 任何文件；未修改 `IPhysicsDebugDraw3D` 接口。

---

## 3. 测试

新增 `DebugDrawAdapterTest.JoltWorldDrawProducesShapeGeometry`：

- 创建真实 `JoltPhysicsWorld`；
- 创建 static sphere body；
- 通过 `PhysicsDebugDrawAdapter3D` + FakeRenderer 执行 `world.DebugDraw()`；
- 断言线段顶点数 > 100（仅 AABB/坐标轴远远达不到该数量）；
- 修复前实测 `0` 顶点，修复后通过。

结果：

| 目标 | Debug | Release |
|---|---:|---:|
| `test_debugdraw` | 22/22 PASSED | 22/22 PASSED |
| `test_physics` | 71/71 PASSED | 71/71 PASSED |
| RenderTest / PhysicsTest 构建 | PASSED | PASSED |
| 组合 CTest（debugdraw/pick/renderer/physics） | 4/4 PASSED | 4/4 PASSED |

全量 CTest：17/18 target 通过；`test_content` 仍因 `assets/scenes/dogfood03/05/06/07.manifest.json` 在 HEAD 中缺失而失败（既有问题，不是 D3 引入）。日志：`ctest_full_t5_jolt_debug.log`、`ctest_full_t5_jolt_release.log`。

构建保留 2 条既有 LNK4006；未宣称零警告。

---

## 4. 重要构建观察（契约偏差）

Gate A 的 D5-11 曾写“Jolt 物理适配保持 Debug-only”。实际构建证据不同：

- Jolt 的 `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE` 默认为 `ON`；
- `JPH_DEBUG_RENDERER` 通过 Jolt target 传播到 `EngineCore` 的 Release 构建（`build/engine/EngineCore.vcxproj` Release 包含该宏）；
- `test_debugdraw` 的 Release 也运行并通过 Jolt 形状门禁（22 tests，不是仅 Debug）。

本次修复没有改变 Jolt 的构建选项；这是对既有配置的事实记录。D5-11 的“Debug-only”措辞与当前构建不一致，需由用户决定是否修订契约；在此之前不把该观察包装成“已冻结条款”。

---

## 5. 残余限制

- `DrawText3D` 仍是 no-op；Jolt 文本不显示。
- `DebugRendererSimple` 的 solid 模式通过 `DrawTriangle` 走线框；T-5 本身不提供实体填充。
- 未设置 camera position，Jolt LOD 使用首个 LOD；当前调试形状未做距离 LOD 选择。
- 未接生产 EngineHost/Bridge/Editor；未做 ASan/TSan、真实沙盒视觉、性能基准。
- D3 改动未提交、未 push；D1 已提交为 `454f136`。
