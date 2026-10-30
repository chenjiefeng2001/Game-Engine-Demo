# T-5 D1 — Box2DDebugDraw Transform/Flags Fix

**状态**：`T-5 D1 — PASS — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**上游记录**：`docs/T-5-DebugDraw-GateC-Closure-Record.md`

---

## 1. 范围

本项只修复 Gate C 遗留的 2D 物理调试桥接缺陷：

- `Box2DDebugDraw::DrawSolidPolygonImpl` 未把局部顶点应用世界 transform；
- `Box2DDebugDraw::DrawSolidCircleImpl` 未把局部 center 折进世界 transform；
- `Box2DDebugDraw::Create` 硬编码 shapes/joints，忽略 `IPhysicsDebugDraw::GetFlags()`。

**不包含**：Jolt `DrawGeometry`/碰撞体形状线框、生产 Editor/Bridge、RenderGraph/RHI 多后端、填充图元、性能优化。

---

## 2. 修改

文件：`engine/src/Box2D/Box2DDebugDraw.cpp`

1. `Create()` 从 `target.GetFlags()` 映射：
   - `DebugDraw_Shape` → `draw.drawShapes`
   - `DebugDraw_Joint` → `draw.drawJoints`
   - `DebugDraw_AABB` → `draw.drawBounds`
   - `DebugDraw_COM` → `draw.drawMass`
2. `DrawSolidPolygonImpl` 对每个顶点调用 `b2TransformWorldPoint(transform, vertex)`。
3. `DrawSolidCircleImpl` 对 center 调用 `b2TransformWorldPoint(transform, center)` 后传给 `IPhysicsDebugDraw`。

不修改 `IPhysicsDebugDraw`、`Box2DPhysicsWorld`、`Box2DDebugDraw` 的公开接口或 Box2D 第三方代码。

---

## 3. 新增真实路径门禁

文件：`tests/test_debugdraw/test_debugdraw.cpp`

`DebugDrawAdapterTest.Box2DWorldDrawUsesWorldTransformAndFlags`：

- 创建真实 `Box2DPhysicsWorld`；
- 在 `(5,2)` 放 box、在 `(-5,2)` 放 circle；
- 通过 `Box2DPhysicsWorld::SetDebugDraw(PhysicsDebugDrawAdapter2D)` 走真实 `b2World_Draw`；
- 断言输出顶点落在两个 body 的世界位置范围；
- 将 adapter flags 设为 `DebugDraw_None`，再次绘制，断言 0 顶点。

该门禁在没有修复时会在世界位置上失败（box/circle 会落在 origin 附近），因此不是“只测 API 返回”。

---

## 4. 结果

| 目标 | Debug | Release |
|---|---:|---:|
| `test_debugdraw` | 21/21 PASSED | 21/21 PASSED |
| 组合 CTest（debugdraw/pick/renderer/physics） | 4/4 PASSED | 4/4 PASSED |
| `test_physics` | 71/71 PASSED | 71/71 PASSED |

构建保留 2 条既有 LNK4006（`RHI::IsBackendAvailable` / `RHI::GetDefaultBackend`）；未宣称零警告。

---

## 5. 全量验证基线（与 D1 独立）

`ctest --test-dir build -C Debug/Release --timeout 300`：

- 18 个 target，17 个通过；
- `test_content` 失败 5 个用例：
  - `ContentDogfood.DF03_Smoke_LoadRunTimerExpire`
  - `ContentDogfood.DF03_EntityCount_MultiScript`
  - `Dogfood05.LoadAndRun`
  - `Dogfood06.LoadAndRun`
  - `Dogfood07.LoadRunRestart`
- 根因：测试请求的 `assets/scenes/dogfood03/05/06/07.manifest.json` 在 HEAD 中不存在；不是 T-5 改动引入，也未在 D1 修复。

D1 之后复跑：`ctest_full_t5_d1_debug.log`、`ctest_full_t5_d1_release.log`；结果一致（17/18 target，`test_content` 仍因同一缺失 manifest 失败）。

---

## 6. 残余限制

- 真实 2D 调试绘制现在通过 adapter 走通用 DebugDraw；但栅格化视觉仍只由 `test_debugdraw` 的 FBO 门禁覆盖，sandbox 只有启动冒烟。
- `Box2DDebugDraw` 的 solid capsule 仍是近似轮廓（两个圆 + 线），不是精确 capsule 填充。
- Jolt `DrawGeometry` / 碰撞体形状线框已由后续 D3 任务修复，见 `docs/T-5-DebugDraw-D3-Jolt-DrawGeometry-Fix.md`。
- 未接生产 EngineHost/Bridge/Editor；未做 ASan/TSan/性能基准。
- D1 改动未提交、未 push；等待用户决定提交范围。
