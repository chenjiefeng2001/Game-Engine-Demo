# T-5 Generic DebugDraw — Gate B Closure Record

**状态**：`T-5 Gate B — PASS — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateA-Contract.md`（`PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND`）
**实现补充**：`docs/T-5-GateB-Implementation-Addendum.md`
**实现记录**：`docs/T-5-DebugDraw-Implementation-Record.md`
**测试记录**：`docs/T-5-DebugDraw-Test-Record.md`
**后续 Gate C**：`docs/T-5-DebugDraw-GateC-Contract.md`（Sandbox + Legacy 集成，`T-5 Gate C — PASS — SCOPE-BOUND`，待用户最终验收）

> 本记录建议将 T-5 v1 封存为 `CLOSED — SCOPE-BOUND`；最终关闭裁决由用户确认。
> 本记录不把未执行的验证项包装成通过。

---

## 1. 交付摘要

T-5 v1 已交付独立的通用 DebugDraw 服务：

- `Engine/Rendering/DebugDraw/` 公共 API：`View`、`Stats`、`Collector`、`IRenderer`、`DebugDraw`。
- CPU Collector：五类线段图元、参数校验、duration、50,000 顶点预算、owner-thread、帧状态机、统计。
- OpenGL 4.6 Renderer：独立 shader/VAO/VBO、depth-on + X-Ray 双批次、GL 状态保存/恢复、无上下文/失败状态。
- 2D/3D Physics Adapter：保留既有物理接口，只转换为通用线段；不改 Box2D/Jolt 既有实现。
- 独立 `test_debugdraw` target：CPU、适配器、GL 像素、深度/X-Ray、状态隔离、生命周期门禁。

未接入 sandbox、legacy `ViewportPanel`、EngineHost、Bridge、生产 Editor、RenderGraph 或 RHI 多后端。

---

## 2. Gate A 验收映射

| Gate A 要求 | 交付 | 证据 | 结论 |
|---|---|---|---|
| D5-1 Rendering/DebugDraw 归属 | `Engine/Rendering/DebugDraw` | 文件结构 / 头文件 | 满足 |
| D5-2 复用 Vec3/Vec4/Mat4/Quat | 公共类型直接复用 | `DebugDrawTypes.h` | 满足 |
| D5-3 Owner-thread only | Collector + Renderer 双方线程检查 | `WrongThreadIsRejected` | 满足 |
| D5-4 显式 Begin/End 生命周期 | `BeginFrame` → 收集 → `EndFrame` | `LifecycleAndFrameState` | 满足 |
| D5-5 depth + X-Ray 双批次 | 两批次固定顺序，默认 depthTest=true | `DepthAndXRayBatchesDiffer` | 满足 |
| D5-6 50k 顶点预算与统计 | 整命令丢弃、统计字段、单次告警 | Budget 用例 | 满足 |
| D5-7 五类线段图元 | Line/WireBox/WireSphere/Axis/Frustum | 数量/几何/像素用例 | 满足 |
| D5-8 GL 4.6 only | 版本/core/context 检查；其他后端 fake Unsupported | GL 上下文用例 | 满足（见 §4） |
| D5-9 直接渲染阶段 | 独立 GL Renderer，无 RenderGraph | 实现记录 | 满足（未接生产帧） |
| D5-10 物理适配器 | 2D/3D adapter，既有接口未改 | Adapter 用例 | 满足（见 §4） |
| D5-11 通用服务全配置 | Debug/Release 均构建、运行 | 测试记录 | 满足 |
| D5-12 显式 Initialize/Shutdown | 幂等 shutdown、上下文检查、失败保留重试 | 生命周期用例 | 满足 |
| D5-13 失败可观测 | `Status` + 日志；无静默假成功 | CPU/GL 失败用例 | 满足 |
| D5-14 完整测试门禁 | CPU/GL/状态/生命周期/预算/线程 | `test_debugdraw` 18 用例 | 满足（见 §4） |
| D5-15 暂不接生产编辑器 | 未改 EngineHost/Bridge/Editor | 目录/文件检查 | 满足 |

---

## 3. 测试与构建结果

| 目标 | Debug | Release |
|---|---:|---:|
| `test_debugdraw` 直接运行 | 18/18 PASSED | 18/18 PASSED |
| `test_debugdraw` CTest | 1/1 PASSED | 1/1 PASSED |
| `test_pick` 定向回归 | 8/8 PASSED | 8/8 PASSED |
| `test_renderer` 定向回归 | 18/18 PASSED | 18/18 PASSED |
| `test_physics` 定向回归 | 71/71 PASSED | 71/71 PASSED |
| 组合 CTest（4 个目标） | 4/4 PASSED | 4/4 PASSED |

GL 环境：NVIDIA GeForce RTX 3070 Laptop GPU，OpenGL 4.6.0 NVIDIA 621.01。

构建警告：每次 `EngineCore` 构建保留 **2 条既有 LNK4006**（`RHI::IsBackendAvailable` / `RHI::GetDefaultBackend` 重复定义）；没有新增 T-5 警告，也没有宣称零警告。

---

## 4. 范围限制与残余风险

1. **未执行完整 CTest 全量套件。** 只执行 T-5 与 §3 定向目标；不能表述为“全量回归通过”。
2. **未执行 ASan/TSan。** 线程模型只通过 owner-thread 功能门禁验证，没有 sanitizer 证据。
3. **低版本 GL 运行时未验证。** `CheckContextSupport` 的 < 4.6 分支为代码审查；真实测试使用 GL 4.6。
4. **真实 Box2D/Jolt 路径未端到端验证。** Adapter 只做直接调用测试；`Box2DDebugDraw` 既有 transform/flags 缺陷未修，真实 `b2World_Draw` 输出可能受影响。Jolt `DrawGeometry` 未修，3D 碰撞体形状线框仍不属于 T-5 v1。
5. **未接生产/沙盒。** Sandbox、legacy `ViewportPanel`、EngineHost/Bridge/Editor 接线均未授权、未实施。
6. **像素断言范围有限。** 线段有 red 像素断言与位置行为；其余图元主要断言“产生可见非背景像素”，没有逐图元精确几何覆盖。GL 状态测试覆盖关键子集，未逐一断言所有 alpha blend factors。
7. **性能未基准化。** 未测量 50k 顶点或大量图元对帧时间的影响；只保证有固定预算。
8. **RenderGraph 现状未改变。** 既有 `SG6 FAIL / ISOLATED` 不由 T-5 修复或重新解释；T-5 未接入 RenderGraph。

---

## 5. 未授权事项（保持未授权/独立任务）

| 工作项 | 状态 |
|---|---|
| Sandbox DebugDraw 接线 | 未授权、未实施 |
| Legacy `ViewportPanel` 接线 | 未授权、未实施 |
| EngineHost / Bridge / 生产 Editor 接线 | 未授权、未实施 |
| `Box2DDebugDraw` transform/flags 修复 | 独立缺陷任务，未实施 |
| Jolt `DrawGeometry` / 碰撞体形状线框 | 独立任务，未实施 |
| RenderGraph / RHI 多后端接入 | T-5 v1 非目标，未实施 |
| Git commit / push / tag / merge / reset / clean / stash | 未执行 |

既有脏文件、子模块状态与 `crashes/` 均原样保留。

---

## 6. 建议的后续动作

1. 由用户对 T-5 v1 做最终验收；确认后可将状态封存为 `CLOSED — SCOPE-BOUND`。
2. 若需要生产可见性，另立任务处理 sandbox/legacy 或 EngineHost/Bridge 接线；不得夹带进 T-5 关闭。
3. 若需要真实物理碰撞体线框，另立 Jolt 形状提取任务，并单独处理 `Box2DDebugDraw` transform/flags 缺陷。
4. 若需要“全量回归通过”或 sanitizer 结论，另立验证任务，运行完整 CTest/ASan/TSan 并记录真实结果。
5. 若需要性能结论，另立基准任务；当前只有固定预算与功能门禁，没有性能承诺。

---

## 7. 最终裁决建议

基于已执行的定向证据，建议：

```text
T-5 Gate B — PASS — SCOPE-BOUND
T-5 v1 — CLOSED — SCOPE-BOUND（待用户最终确认）
```

不支持以下表述：

- “全量测试通过”；
- “所有渲染后端支持”；
- “Jolt 碰撞体形状已可视化”；
- “生产编辑器已接线”；
- “零警告”；
- “性能已优化”。
