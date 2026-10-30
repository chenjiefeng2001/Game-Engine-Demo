# Jolt DebugRenderer Residual Capabilities

**状态**：`DEFERRED — SEPARATE JOLT TASK`
**日期**：2026-10-31
**关联**：`docs/T-5-DebugDraw-D3-Jolt-DrawGeometry-Fix.md`

---

## 1. 已修复（D3，不属于残余）

- `JoltDebugRenderer` 现在复用 `JPH::DebugRendererSimple` 的 triangle batch / `DrawGeometry`。
- `JoltPhysicsWorld::DebugDraw()` 现在调用 `JoltDebugRenderer` 的 TLS `Clear/Flush`。
- 真实 Jolt sphere `DebugDraw` 门禁在 Debug/Release 均通过（`test_debugdraw` 22/22）。
- Jolt `DrawGeometry` 形状绘制已不再是空实现；它作为 D3 独立任务完成，不是 T-5 v1 的扩展。

---

## 2. 残余能力（暂缓，不阻塞 T-5 v1）

| 项目 | 当前状态 | 说明 |
|---|---|---|
| `DrawText3D` | no-op | Jolt 文本不显示；通用 DebugDraw v1 不提供 3D 文本 |
| LOD camera position | 未接入 | `DebugRendererSimple` 未收到相机位置，当前使用首个 LOD |
| solid 模式 | 线框化 | `DebugTriangle` 走 3 条线；T-5 不提供实体填充 |
| 其他 Jolt DebugRenderer 扩展 | 未审计 | 未承诺全部 Jolt 调试能力 |

---

## 3. 建议后续分析

1. 明确相机位置由谁提供、在 `JoltPhysicsWorld::DebugDraw()` 的哪个生命周期设置。
2. 评估 `DrawText3D` 是否复用现有 3D 文本/ImGui 投影设施。
3. 评估 solid 模式是否需要新的实体三角形调试路径；不得把它夹带进 T-5 v1。
4. 盘点 Jolt DebugRenderer 其他回调与当前 adapter 的覆盖范围。

---

## 4. 边界

- 不阻塞 T-5 v1；不改变已冻结的通用 DebugDraw API。
- 未获授权前不修改 `third_party/JoltPhysics` 或引擎 Jolt 包装器。
- 任何后续实现应单独建立契约、测试与关闭记录。
