# T-5 Generic DebugDraw — Test Record

**状态**：`T-5 Gate B — PASS — SCOPE-BOUND`（测试记录）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateA-Contract.md`
**实现**：`docs/T-5-DebugDraw-Implementation-Record.md`

---

## 1. 环境

| 项目 | 值 |
|---|---|
| OS | Windows 10.0.29695（x64） |
| 构建系统 | CMake 3.30.3 / Visual Studio 17 2022 |
| MSVC | 19.44.35228.0 |
| 配置 | Debug / Release |
| GPU / GL | NVIDIA GeForce RTX 3070 Laptop GPU；OpenGL 4.6.0 NVIDIA 621.01（来自 `test_renderer` 运行输出） |
| 测试框架 | GoogleTest（`tests/main.cpp`） |

本文只报告实际执行的结果。未执行的项目在 §5 明确列出；不得把未执行项当作通过。

---

## 2. T-5 门禁结果

### 2.1 `test_debugdraw` — 18 个用例

| 配置 | 直接运行 | CTest |
|---|---:|---:|
| Debug | 18/18 PASSED | 1/1 PASSED |
| Release | 18/18 PASSED | 1/1 PASSED |

覆盖用例：

- `DebugDrawCpuTest.LifecycleAndFrameState`
- `DebugDrawCpuTest.WrongThreadIsRejected`
- `DebugDrawCpuTest.FivePrimitiveVertexCounts`
- `DebugDrawCpuTest.PrimitiveGeometryIsExpandedCorrectly`
- `DebugDrawCpuTest.InvalidArgumentsAreRejected`
- `DebugDrawCpuTest.DurationExpiresAtBeginFrame`
- `DebugDrawCpuTest.BudgetDropsWholeCommands`
- `DebugDrawCpuTest.BudgetAcceptsExactlyCapacity`
- `DebugDrawCpuTest.BudgetIncludesPersistentCommands`
- `DebugDrawCpuTest.BackendFailuresAreObservable`
- `DebugDrawAdapterTest.TwoDimensionalMappingAndFlags`
- `DebugDrawAdapterTest.ThreeDimensionalAdapterForwardsLinePrimitives`
- `DebugDrawGLTest.ContextSupportAndLifecycle`
- `DebugDrawGLTest.MissingContextIsReported`
- `DebugDrawGLTest.ShutdownWithoutContextFailsThenRetries`
- `DebugDrawGLTest.LineAndAllPrimitivesProducePixels`
- `DebugDrawGLTest.DepthAndXRayBatchesDiffer`
- `DebugDrawGLTest.GLStateIsRestored`

### 2.2 Gate A 门禁映射

| Gate A 门禁 | 执行证据 | 结果 |
|---|---|---|
| CPU 图元测试 | 五类图元展开数量、球体半径、轴颜色、参数校验 | PASS |
| CPU 预算测试 | 50,000 精确边界、超限整命令丢弃、组合图元原子性、持久命令共享预算 | PASS |
| 线程测试 | 主线程成功；非 Owner Thread `BeginFrame` 返回 `WrongThread` | PASS |
| 生命周期测试 | 未初始化、重复初始化、帧顺序错误、Shutdown 幂等、无上下文初始化和关闭重试 | PASS |
| GL 像素测试 | 线段 + 五类图元均产生非背景像素；线段 red 像素断言 | PASS |
| 深度测试 | 深度缓冲清 0 时 depth batch 不可见；同一线段在 X-Ray batch 可见 | PASS |
| 状态隔离测试 | blend/depth/program/VAO/array-buffer/line-width 关键状态恢复 | PASS |
| 后端测试 | GL 4.6 支持；无 current context 明确失败；fake renderer `Unsupported` 可观测 | PASS（见 §5 限制） |
| 物理适配测试 | 2D 映射/flags；3D 线段/图元数量；`DrawText3D` 不产生几何 | PASS（见 §5 限制） |

---

## 3. 定向回归

| 目标 | Debug | Release | 备注 |
|---|---:|---:|---|
| `test_pick` | 8/8 PASSED | 8/8 PASSED | 隐藏 GL context / FBO / 像素读取既有门禁 |
| `test_renderer` | 18/18 PASSED | 18/18 PASSED | RenderGraph / GPU 时间戳 / 蒙皮像素既有门禁 |
| `test_physics` | 71/71 PASSED | 71/71 PASSED | 物理运行时既有门禁；未覆盖 adapter 与 Box2D path |
| 组合 CTest（上述 3 个 + `test_debugdraw`） | 4/4 PASSED | 4/4 PASSED | `ctest_t5_debug.log` / `ctest_t5_release.log` |

未观察到这些目标的新增失败。RenderGraph 的已知 `SG6 FAIL/ISOLATED` 状态未由 T-5 改变；T-5 不接入 RenderGraph。

---

## 4. 构建警告

每次 `EngineCore` 构建出现 **2 条既有 LNK4006**：

- `Engine::RHI::IsBackendAvailable`
- `Engine::RHI::GetDefaultBackend`

均来自 `RHIBackend.obj` 与 `RHIDeviceFactory.obj` 的重复定义，是既有状态，不是 T-5 新增。Debug/Release 的 `test_debugdraw` 构建日志均记录 2 条；没有宣称零警告。

构建日志：

- `build_t5_engine_debug.log`
- `build_t5_test_debug.log`
- `build_t5_test_debug_release.log`
- `build_t5_regression_debug.log`
- `build_t5_regression_release.log`

测试日志：

- `test_debugdraw_debug.log`
- `test_debugdraw_release.log`
- `test_pick_t5_regression.log`
- `test_renderer_t5_regression.log`
- `test_physics_t5_regression.log`
- `ctest_t5_debug.log`
- `ctest_t5_release.log`

---

## 5. 未执行 / 未覆盖

以下项目没有执行，不能视为通过：

1. 完整 `ctest` 全量套件；只运行了 T-5 与 §3 定向目标。
2. ASan / TSan；未运行内存/线程 sanitizer。
3. 低版本 GL 上下文（< 4.6）运行时测试；`CheckContextSupport` 的低版本分支仅代码审查，没有真实低版本上下文证据。
4. 真实 sandbox 接线、legacy `ViewportPanel`、EngineHost / Bridge / 生产 Editor 接线；均未授权且未实施。
5. 真实 Jolt `DrawGeometry` / 碰撞体形状线框；未修复，未测试。
6. 真实 Box2D `b2World_Draw` → adapter 端到端；2D 适配器只做直接调用测试，既有 `Box2DDebugDraw` transform 缺陷可能影响真实输出。
7. 全量 GL 状态矩阵（alpha blend factors 等未单独断言；实现保存/恢复，但测试只检查关键子集）。
8. 像素级每图元精确几何位置；除线段 red 像素与深度/X-Ray 行为外，其余图元主要断言“产生可见非背景像素”。
9. 多显示器 / DPI / 生产窗口环境；只在隐藏离屏 GL 上下文测试。
10. 性能基准；未测量 debug draw 对帧时间的影响。

---

## 6. 结论

T-5 v1 的定向门禁在 Debug/Release 均通过；相关既有回归没有新增失败。结合 §5 的明确限制，本测试记录支持 `T-5 Gate B — PASS — SCOPE-BOUND`，不支持“全量回归通过”或“多后端兼容”表述。
