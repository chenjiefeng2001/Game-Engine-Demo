# T-5 Generic DebugDraw — Gate C Closure Record

**状态**：`T-5 Gate C — PASS — SCOPE-BOUND`（待用户最终验收）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateC-Contract.md`
**实现**：`docs/T-5-DebugDraw-GateC-Implementation-Record.md`
**测试**：`docs/T-5-DebugDraw-GateC-Test-Record.md`

> Gate C 限定为 Sandbox + Legacy 集成。生产 EngineHost/Bridge/Editor 接线、Jolt 形状提取和 Box2D 缺陷修复仍是独立任务。

---

## 1. 交付摘要

| 接入点 | 交付 | 结果 |
|---|---|---|
| RenderTest 3D | DebugDraw 服务 + `PhysicsDebugDrawAdapter3D`；场景/grid 之后 Begin/collect/End | 构建/启动冒烟通过 |
| PhysicsTest 2D | DebugDraw 服务 + `PhysicsDebugDrawAdapter2D`；sprite 之后 Begin/collect/End | 构建/启动冒烟通过 |
| Legacy ViewportPanel | 可选 `SetDebugRenderCallback`，场景回调与 grid 之后调用 | 回调顺序测试通过 |
| 帧顺序 | 收集早于场景 Clear、提交晚于场景渲染 | 新增 FBO 像素门禁通过 |

未修改 DebugDraw 核心 API/渲染器语义；未修改 `Box2DDebugDraw`、`JoltPhysicsWorld`、`JoltDebugRenderer`、RenderGraph/RHI、EngineHost/Bridge/生产 Editor。

---

## 2. 验收映射

| Gate C 要求 | 证据 | 结论 |
|---|---|---|
| RenderTest/PhysicsTest Debug/Release 构建 | `build_t5_gatec_debug.log` / `build_t5_gatec_release.log` | PASS |
| 收集 → Clear → EndFrame 仍可见 | `CollectionBeforeSceneClearRendersAfterClear` | PASS |
| 真实 FBO 中线/图元产生像素 | 既有 GL 图元/线段/深度测试 + 新顺序测试 | PASS |
| 状态隔离 | `GLStateIsRestored` | PASS |
| ViewportPanel 回调顺序 | `ViewportPanelDebugCallbackRunsAfterScene` | PASS（回调层） |
| 默认无回调不改行为 | callback 成员默认空；ViewportPanel 现有代码路径不变 | PASS（代码/构建） |
| `test_debugdraw` 回归 | 20/20 Debug + Release | PASS |
| 定向回归 | 组合 CTest 4/4 Debug + Release | PASS |
| 边界未扩张 | 未改 EngineHost/Bridge/Editor、RenderGraph/RHI、Box2D/Jolt | PASS |

---

## 3. 结果数字

- `test_debugdraw`：20/20 Debug、20/20 Release。
- 组合 CTest：4/4 Debug、4/4 Release（`test_debugdraw`、`test_pick`、`test_renderer`、`test_physics`）。
- `RenderTest` / `PhysicsTest`：Debug、Release 构建成功。
- 沙盒启动冒烟：各运行 4 秒，timeout exit 124；无启动崩溃、无 DebugDraw 初始化失败日志。
- `EngineCore` 每次构建保留 2 条既有 LNK4006；未宣称零警告。

---

## 4. 残余限制

1. 未运行完整 CTest 全量套件；只覆盖 4 个组合目标。
2. 未运行 ASan/TSan。
3. 沙盒只做启动冒烟；没有自动截图/像素验收。真实像素证据来自 `test_debugdraw` FBO 测试。
4. 2D 真实 Box2D 路径仍受 `Box2DDebugDraw` transform/flags 既有缺陷影响；本阶段未修。
5. Jolt `DrawGeometry` 仍是空实现；RenderTest 的碰撞体形状线框仍不完整；本阶段未修。
6. Legacy ViewportPanel 回调测试使用隐藏 GL 上下文 + 独立 ImGui context，不覆盖完整编辑器 UI 布局/交互。
7. 未接生产 EngineHost/Bridge/Avalonia Editor；未加 UI 开关或 ABI。
8. 未测量性能、未做多显示器/DPI/长稳验证。
9. 本轮未 push；Gate C 改动尚未提交到 Git（除非用户另行要求）。

---

## 5. 未授权事项

| 工作项 | 状态 |
|---|---|
| EngineHost / Bridge / 生产 Editor 接线 | 未授权、未实施 |
| RenderGraph / RHI 多后端 | 未授权、未实施 |
| `Box2DDebugDraw` transform/flags 修复 | 独立缺陷任务，未实施 |
| Jolt `DrawGeometry` / 碰撞体形状线框 | 独立任务，未实施 |
| Git push | 未授权、未执行 |
| 完整 CTest / ASan / TSan / 性能基准 | 未执行 |

---

## 6. 最终裁决建议

```text
T-5 Gate C — PASS — SCOPE-BOUND
T-5 v1 Sandbox + Legacy Integration — CLOSED — SCOPE-BOUND（待用户最终确认）
```

不建议将 Gate C 表述为：

- “生产编辑器已接入”；
- “Jolt 碰撞体形状已可视化”；
- “沙盒视觉完整验收”；
- “全量测试通过”；
- “零警告”。
