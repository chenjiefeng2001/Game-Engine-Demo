# T-5 Generic DebugDraw — Gate C Test Record

**状态**：`T-5 Gate C — PASS — SCOPE-BOUND`（测试记录）
**日期**：2026-10-31
**基线**：`74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**契约**：`docs/T-5-DebugDraw-GateC-Contract.md`
**实现**：`docs/T-5-DebugDraw-GateC-Implementation-Record.md`

---

## 1. 环境

| 项目 | 值 |
|---|---|
| OS | Windows 10.0.29695（x64） |
| 构建系统 | CMake 3.30.3 / Visual Studio 17 2022 |
| MSVC | 19.44.35228.0 |
| 配置 | Debug / Release |
| GPU / GL | NVIDIA GeForce RTX 3070 Laptop GPU；OpenGL 4.6.0 NVIDIA 621.01 |
| 测试框架 | GoogleTest（`tests/main.cpp`） |

---

## 2. 自动化门禁

| 目标 | Debug | Release |
|---|---:|---:|
| `test_debugdraw` 直接运行 | 20/20 PASSED | 20/20 PASSED |
| 组合 CTest（`test_debugdraw`/`test_pick`/`test_renderer`/`test_physics`） | 4/4 PASSED | 4/4 PASSED |

Gate C 新增用例：

- `DebugDrawGLTest.CollectionBeforeSceneClearRendersAfterClear`
  - 在收集完成后执行场景 ClearColor/Depth；
  - `EndFrame` 后仍有红色线段像素；
  - 证明 DebugDraw 不是立即绘制，不再被场景 Clear 抹掉。
- `DebugDrawGLTest.ViewportPanelDebugCallbackRunsAfterScene`
  - 使用实际 `ViewportPanel` + 隐藏 GL 上下文 + FBO + 独立 ImGui context；
  - 验证 scene callback → debug callback 顺序；
  - 验证 debug callback 收到非空 viewProj/cameraPos；
  - `ViewportPanel` 清理在 fixture 之前完成。

既有 Gate B 18 用例继续通过（CPU、预算、duration、线程、生命周期、适配器、GL 像素、depth/X-Ray、状态隔离）。

---

## 3. 构建

| 目标 | Debug | Release |
|---|---:|---:|
| `RenderTest` | PASSED | PASSED |
| `PhysicsTest` | PASSED | PASSED |
| `test_debugdraw` | PASSED | PASSED |

构建日志：

- `build_t5_gatec_debug.log`
- `build_t5_gatec_release.log`
- `build_t5_gatec_test_debug.log`

警告：每次 `EngineCore` 构建保留 **2 条既有 LNK4006**（`RHI::IsBackendAvailable` / `RHI::GetDefaultBackend` 重复定义）；没有宣称零警告。

---

## 4. 沙盒启动冒烟

在各自资源目录运行 4 秒，由 `timeout` 终止：

| 目标 | 退出码 | 观察 |
|---|---:|---|
| `RenderTest.exe`（Debug） | 124（timeout） | Jolt + ImGui 初始化完成；无 DebugDraw 初始化失败日志 |
| `PhysicsTest.exe`（Debug） | 124（timeout） | 物理运行与碰撞回调持续输出；无崩溃/致命日志 |

日志：

- `gatec_RenderTest_smoke.log`
- `gatec_PhysicsTest_smoke.log`

**限制**：这是启动/运行冒烟，不是视觉像素验收；未自动确认沙盒窗口中的调试线颜色/像素。自动化像素证据来自 `test_debugdraw` 的 FBO 门禁，而不是 sandbox 截图。

---

## 5. 未执行 / 未覆盖

1. 完整 CTest 全量套件；只运行 4 个组合目标。
2. ASan/TSan。
3. Gate C 阶段未做真实 Box2D `b2World_Draw` → adapter 像素验证；后续 D1 已修复 `Box2DDebugDraw` transform/flags 并增加真实世界坐标门禁（见 `docs/T-5-DebugDraw-D1-Box2D-DebugDraw-Fix.md`）。
4. Gate C 阶段未验证 Jolt `DrawGeometry` / 碰撞体形状线框；后续独立 D3 任务已修复并加 Debug/Release 真实 Jolt 门禁（`docs/T-5-DebugDraw-D3-Jolt-DrawGeometry-Fix.md`）。
5. 生产 EngineHost/Bridge/Avalonia Editor 接线与 UI 开关。
6. 真实 legacy `ViewportPanel` 编辑器 UI 的完整交互；测试只覆盖回调顺序，不覆盖实际 ImGui 编辑器布局。
7. 多显示器/DPI、性能基准、长时间运行。
8. Git push。

---

## 6. 结论

Gate C 的自动化门禁与沙盒构建通过；新增顺序测试覆盖了 Gate B 扫描发现的真实帧风险，新增回调测试覆盖了 legacy `ViewportPanel` 接入点。结合 §5 的限制，本测试记录支持 `T-5 Gate C — PASS — SCOPE-BOUND`，不支持“沙盒视觉完整验收”或“全量回归通过”的表述。
