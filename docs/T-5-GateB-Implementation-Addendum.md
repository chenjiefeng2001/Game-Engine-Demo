# T-5 Gate B — Implementation Addendum

**状态**：`IMPLEMENTED — Gate B implementation input`
**日期**：2026-10-31
**实施结果**：`T-5 Gate B — PASS — SCOPE-BOUND`，见 `docs/T-5-DebugDraw-GateB-Closure-Record.md`
**基线**：HEAD `74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**上位契约**：`docs/T-5-DebugDraw-GateA-Contract.md`（`PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND`）
**Gate 0 扫描**：`docs/T-5-DebugDraw-Gate0-Scan.md`

本记录只补充 Gate A 未展开的三个实现选择，不改变 Gate A 的非目标、权限边界或验收门禁。

---

## 0. B0 基线复核记录

### 0.1 工作区

- 基线 HEAD：`74ad0999b35b1783ea9e58667966d3e84917dea8`；分支 `avalonia`。
- 工作树存在既有 T-7 音频、编辑器/UI 改动和未跟踪文档；`tests/CMakeLists.txt` 已有 T-7 修改。
- `crashes/` 为既有未跟踪产物，保留不动。
- 子模块 `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` 显示为 modified；本阶段不执行子模块操作。`openal-soft` 的本地差异为 `D build/.empty`，不触碰。
- `engine/include/Engine/Rendering/DebugDraw`、`engine/src/Rendering/DebugDraw`、`tests/test_debugdraw` 在 B0 时不存在，未发现 T-5 目标文件既有修改。

### 0.2 可复用设施

| 设施 | 位置 | 结论 |
|---|---|---|
| 隐藏 GL 4.6 上下文 | `OpenGLGraphicsFactory::InitializePersistentHiddenContext` / `MakePersistentContextCurrent` / `UncurrentPersistentContext` | 可复用；必须由 owner thread 调用 |
| Offscreen FBO + 像素回读 | `OpenGLFramebuffer` | 可复用；`Bind()` 设置 viewport，`ReadColorPixel()` 读 attachment 0 |
| GLFW owner-thread 注册 | `Engine::RegisterGlfwOwnerThread()` | 测试必须先调用 |
| 日志 | `ENGINE_LOG_*` / `Logger` | 可直接使用；测试环境不依赖日志初始化 |
| 数学类型 | `Vec2/Vec3/Vec4/Quat/Mat4`（`MathTypes.h`） | POD；没有四元数旋转辅助函数，Collector 内部实现 |
| 测试骨架 | `tests/main.cpp` + GTest | 新 target 沿用既有约定 |

### 0.3 CMake 现状

- `engine/CMakeLists.txt` 使用 `file(GLOB ENGINE_SOURCES ...)` 与 `ENGINE_HEADERS`；当前没有 `src/Rendering/DebugDraw/*`、`src/Rendering/DebugDraw/OpenGL/*`、`src/Rendering/DebugDraw/Physics/*` 以及对应 include 子目录。
- B1/B2/B3 必须显式增加这些 GLOB 行；不修改既有 `EngineCore` 链接关系。
- `tests/CMakeLists.txt` 已有 T-7 修改；新增 `test_debugdraw` 只能在末尾追加，不重排既有 target。

---

## 1. 与 `IPrimitiveBatch` 的关系

**决定：v1 使用独立的 `OpenGL46DebugDrawRenderer`，不依赖 `IPrimitiveBatch` / `OpenGLPrimitiveBatch`。**

理由：

- Gate A 已冻结 GL 4.6 直接渲染路线；现有 RHI 绘制路径存在已知缺口（`GL46CommandList` 绘制为空实现、Vulkan 动态拓扑被忽略、D3D12 PSO 拓扑固定）。
- `OpenGLPrimitiveBatch` 的公开契约是通用图元批处理，不是 DebugDraw 的帧生命周期、深度双批次或 50k 预算契约；强行复用会把两套契约耦合。
- 允许复用底层 GL 资源与上下文辅助设施（`OpenGLGraphicsFactory`、`GladGLContext`、测试 FBO），但不修改 `IPrimitiveBatch`、`OpenGLPrimitiveBatch` 或 RHI 契约。

实现边界：

- Collector 只持有 `IRenderer` 抽象，不包含 GL 调用。
- GL Renderer 独立管理 shader、VAO、VBO 和状态保存/恢复。
- 若未来要复用 `OpenGLPrimitiveBatch`，必须另立契约变更，不得在 v1 内隐式切换。

---

## 2. Frustum 八个角点的顺序

**决定：角点顺序冻结为：**

```text
0: nearBottomLeft
1: nearBottomRight
2: nearTopRight
3: nearTopLeft
4: farBottomLeft
5: farBottomRight
6: farTopRight
7: farTopLeft
```

`Frustum(corners[8], color)` 接收上述世界空间角点；Renderer/Collector 不执行投影矩阵求逆，也不从 `Mat4` 隐式推断角点顺序。

连接规则（12 条线段、24 个顶点）：

```text
near: 0-1, 1-2, 2-3, 3-0
far:  4-5, 5-6, 6-7, 7-4
side: 0-4, 1-5, 2-6, 3-7
```

所有角点必须有限；NaN/Inf 或不足 8 个角点时整条命令拒绝。

---

## 3. duration 的时间语义

**决定：**

- `durationSeconds == 0`：仅当前帧绘制。
- `durationSeconds > 0`：从提交帧开始计时；在剩余时间大于零时继续绘制。
- `durationSeconds < 0`、NaN、Infinity：拒绝（`InvalidArgument`）。
- 下一帧开始时，根据 `BeginFrame(view, deltaTimeSeconds)` 的 `deltaTimeSeconds` 更新持久命令。
- `deltaTimeSeconds` 本身必须是有限且非负；NaN/Inf/负值拒绝。
- 超出预算时，完整丢弃导致超限的最新命令，不保留部分图元；持久命令和当前帧命令共用 50,000 顶点预算。

实现约定：

- 命令在提交帧渲染一次；跨帧保留从下一帧 `BeginFrame` 的 aging 开始。
- 持久命令的 `remainingSeconds` 在每帧 `BeginFrame` 减去 `deltaTimeSeconds`；`remainingSeconds <= 0` 时移除，不再提交。
- 持久命令不能被后续帧绕过预算；新命令提交时按 `persistentVertices + currentVertices` 检查容量。
- `durationSeconds > 0` 的命令在当前帧结束后进入持久集合；`durationSeconds == 0` 的命令当前帧结束后丢弃。

---

## 4. 实现落位（B1/B2/B3）

```text
engine/include/Engine/Rendering/DebugDraw/
    DebugDrawTypes.h
    IDebugDrawRenderer.h
    DebugDrawCollector.h
    DebugDraw.h
engine/include/Engine/Rendering/DebugDraw/OpenGL/
    OpenGL46DebugDrawRenderer.h
engine/include/Engine/Rendering/DebugDraw/Physics/
    PhysicsDebugDrawAdapter2D.h
    PhysicsDebugDrawAdapter3D.h
engine/src/Rendering/DebugDraw/
    DebugDrawCollector.cpp
    DebugDraw.cpp
engine/src/Rendering/DebugDraw/OpenGL/
    OpenGL46DebugDrawRenderer.cpp
engine/src/Rendering/DebugDraw/Physics/
    PhysicsDebugDrawAdapter2D.cpp
    PhysicsDebugDrawAdapter3D.cpp
tests/test_debugdraw/
    test_debugdraw.cpp
    DebugDrawGLFixture.h
```

CMake 只追加上述目录的 GLOB 和 `test_debugdraw` target；不重排既有 target，不修改既有物理/Box2D/Jolt 源文件。

---

## 5. 变更控制

本记录不改变 Gate A 契约。若实现发现 Gate A 条款不可行，必须停止并提交契约变更裁决，不得在本记录中静默放宽验收标准。
