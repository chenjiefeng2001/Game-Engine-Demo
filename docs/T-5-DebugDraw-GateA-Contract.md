# T-5 Generic DebugDraw — Gate A Contract

**状态**：`T-5 GATE A — PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND`
**日期**：2026-10-31
**基线**：HEAD `74ad0999b35b1783ea9e58667966d3e84917dea8`，分支 `avalonia`
**Gate B**：`AUTHORIZED`（2026-10-31 用户授权；实施结果见下方记录）
**实施状态**：`T-5 Gate B — PASS — SCOPE-BOUND`（待用户最终验收）
**Gate A 冻结轮次**：未修改源码、未构建、未运行测试、未执行 Git 写入。
**本契约条款未改变**；后续实施、测试与残余限制以关闭记录为准。

**输入**：

- `docs/T-5-DebugDraw-Gate0-Scan.md`（Gate 0 只读扫描）
- `docs/DebugRenderer-2.0-Analysis-Report.md`（历史分析；其中与当前源码不符的假设见 §6）
- 用户 2026-10-31 Gate A 逐项裁决
- `docs/T-5-GateB-Implementation-Addendum.md`（实现补充）
- `docs/T-5-DebugDraw-Implementation-Record.md`（实现记录）
- `docs/T-5-DebugDraw-Test-Record.md`（测试记录）
- `docs/T-5-DebugDraw-GateB-Closure-Record.md`（关闭记录）

**证据边界**：

- Gate A 裁决基于用户提供的 Gate 0 扫描摘要与第 5 节原文；用户未独立读取本地报告文件，Gate 0 的独立复核仍待后续进行。
- 本契约冻结设计范围与验收要求；Gate B 于 2026-10-31 另行授权。实施结果、测试证据与残余限制见关闭记录，本契约条款未改变。
- 本契约不追溯修改 `docs/DebugRenderer-2.0-Analysis-Report.md`；该文档中与当前源码不符的假设由 §6 明确取代。

---

## 0. Formal Adjudication Record

| ID | 最终裁决 | 冻结要求 |
|---|---|---|
| **D5-1** | Rendering/DebugDraw | 通用调试绘制归属 Rendering；物理接口保持独立。 |
| **D5-2** | 复用现有数学类型 | 位置使用 `Vec3`，颜色使用 `Vec4`，变换使用 `Mat4` / `Quat`；不新增 Color 类型。`DebugColor` 保留在既有物理接口中。 |
| **D5-3** | Owner-thread only | v1 禁止 TLS、worker 收集和任意线程提交。 |
| **D5-4** | 显式帧生命周期 | `BeginFrame(view, deltaTime)` → 收集图元 → `EndFrame()`。`EndFrame()` 完成批次提交和渲染，不再要求调用方另行 `Render()`。 |
| **D5-5** | Depth + X-Ray 双批次 | 每条命令有 `depthTest`，默认 `true`。先绘制深度测试批次，再绘制关闭深度测试的 X-Ray 批次。 |
| **D5-6** | 固定顶点预算 | 每帧最多 50,000 个顶点；超限丢弃最新的完整命令，不截断图元；统计丢弃情况，并在初始化周期内只告警一次。 |
| **D5-7** | 五类图元 | Line、WireBox、WireSphere、Axis、Frustum。全部转换为线段；不提供填充图元或文本。 |
| **D5-8** | OpenGL 4.6 only | v1 只支持 GL 4.6。其他后端及不符合版本条件的上下文明确返回 Unsupported，不静默降级。 |
| **D5-9** | 直接渲染阶段 | 3D 场景绘制完成后、UI 绘制之前执行；不接 RenderGraph，不在 `ClearColor` 之前绘制。 |
| **D5-10** | 物理适配器 | 保留 `IPhysicsDebugDraw` / `IPhysicsDebugDraw3D`；增加适配层，不改原接口。 |
| **D5-11** | 通用服务全配置 | 通用 DebugDraw 在 Debug/Release 均可用；Jolt 物理适配保持 Debug-only，不得在 Release 假称提供物理线框能力。 |
| **D5-12** | 显式 Initialize/Shutdown | 初始化必须有当前且符合要求的 GL 上下文；Shutdown 幂等；上下文重建必须显式 Shutdown/Initialize。 |
| **D5-13** | 失败必须可观测 | API 返回状态；重要失败记录日志。不能用静默跳过伪装成功。 |
| **D5-14** | 完整测试门禁 | CPU 单测、GL 像素测试、GL 状态隔离、生命周期、预算和线程测试。 |
| **D5-15** | 暂不接生产编辑器 | 不接 EngineHost、Bridge、生产 Editor；生产接线必须单独授权。 |

---

## 1. 范围与术语

### 1.1 术语

| 术语 | 定义 |
|---|---|
| 通用 DebugDraw | 不依赖物理接口、面向渲染层的调试线段服务 |
| Owner Thread | 调用 `Initialize()` 且持有当前 GL 4.6 上下文的线程 |
| Renderer | 后端实现，负责 GL 资源、批次提交与关闭；v1 为 OpenGL 4.6 Renderer |
| Collector | 调用方使用的统一收集入口；持有 Renderer 抽象，不向公共 API 泄漏 GL 细节 |
| 帧窗口 | `BeginFrame()` 与 `EndFrame()` 之间的唯一有效收集区间 |
| 组合图元 | WireBox、WireSphere、Axis、Frustum 等由多条线段展开而成的图元 |
| 持久命令 | `durationSeconds > 0`、跨帧保留的图元 |
| 预算 | 每帧最多 50,000 个展开后顶点，持久命令与当前帧命令共用 |

### 1.2 v1 范围内

- 通用 DebugDraw 公共 API 与 CPU 收集器。
- OpenGL 4.6 后端 Renderer。
- Line、WireBox、WireSphere、Axis、Frustum 五类图元。
- 显式帧生命周期、owner-thread 约束、固定顶点预算、depth/X-Ray 双批次、统计与失败状态。
- CPU 单测与 GL 像素/状态隔离测试。
- 物理适配层的边界契约；2D/3D 适配实现可在 Gate B 授权后纳入。

### 1.3 v1 范围外

- RenderGraph Pass；RHI 多后端；Vulkan / D3D12 / Metal。
- 3D 文本、实体填充图元、材质/光照、GPU instancing。
- 任意线程 TLS、worker 收集、跨线程命令队列。
- EngineHost / Bridge / 生产 Editor 接线。
- 修复 Jolt `DrawGeometry` 或 Jolt 碰撞体形状提取。
- 修复 `Box2DDebugDraw` 既有 transform/flags 缺陷。
- CVar / Console / Editor UI 开关。
- T-6-B、F2-S1 或音频后续项。

---

## 2. 冻结的 API 契约

### 2.1 归属与类型

- 逻辑路径：`Engine/Rendering/DebugDraw/`
- 命名空间：`Engine::Rendering::DebugDraw`
- 主要类型：`View`、`Stats`、`Collector`、`IRenderer`
- GL 实现：独立的 OpenGL 4.6 Renderer；GL 调用不得泄漏到通用 Collector。
- 公共 API 不暴露 `GladGLContext` 或其他具体后端类型；调用方只依赖统一服务/Collector 与 `IRenderer` 抽象。

`IRenderer` 负责后端初始化、批次提交和关闭；`Collector` 作为统一服务入口持有并驱动 `IRenderer`，调用方不直接操作 GL Renderer。

### 2.2 调用顺序与状态规则

冻结顺序：

```text
Initialize();

BeginFrame(view, deltaTimeSeconds);

Line(...);
WireBox(...);
WireSphere(...);
Axis(...);
Frustum(...);

EndFrame();

Shutdown();
```

- `BeginFrame` 到 `EndFrame` 是唯一有效的收集窗口。
- `EndFrame()` 完成批次提交和渲染；调用方不得再调用额外的 `Render()`。
- 未初始化、帧未开始、重复 `BeginFrame`、重复 `EndFrame`、错误线程调用都必须返回明确状态。
- `Shutdown()` 可重复调用；重复调用必须安全。

### 2.3 图元与参数语义

| 图元 | 参数语义 |
|---|---|
| `Line` | 两个世界空间端点 |
| `WireBox` | 世界空间中心、半尺寸、旋转四元数 |
| `WireSphere` | 世界空间中心、正半径 |
| `Axis` | 世界空间原点、方向四元数、正长度；X/Y/Z 轴采用红/绿/蓝 |
| `Frustum` | 八个世界空间角点，不从矩阵中隐式推断角点顺序 |

统一约束：

- 世界空间、右手坐标系。
- 颜色为线性空间 RGBA，使用 straight alpha，分量范围为 `[0,1]`。
- `durationSeconds` 单位为秒；`0` 表示仅当前帧，正值表示跨帧保留，负值非法。
- 组合图元必须整体接受或整体丢弃，不能只绘制一部分。
- WireSphere v1 使用固定细分：三个正交大圆，每个 16 段，共 48 条线段；不开放动态细分配置。
- 全部图元转换为线段；不提供填充图元或文本。
- `View` 使用项目现有 `Mat4` 表达场景视图投影关系，不引入第二套矩阵约定。

### 2.4 深度与 X-Ray

`depthTest=true` 是默认行为。

每帧绘制顺序固定：

1. 深度测试批次：使用场景已有深度缓冲。
2. X-Ray 批次：关闭深度测试，使被遮挡线段也可见。

约束：

- 两批次均不得写入深度缓冲。
- 颜色混合采用 straight-alpha 语义。
- Renderer 必须保存并恢复自己修改过的 GL 状态，至少覆盖：
  - Blend enable、blend factors 和 blend equations；
  - Depth-test enable、depth function 和 depth write mask；
  - 当前 Program、VAO、相关 Buffer binding；
  - Line width；
  - Renderer 修改过的其他 GL 状态。
- 不允许影响后续 UI 或其他 Render 阶段的 GL 状态。
- 线宽固定为 `1.0`，v1 不提供自定义粗线。

### 2.5 预算与统计

固定预算为 50,000 顶点/帧，组合图元展开后计数。

超限行为：

- 丢弃导致超限的最新完整命令。
- 不清除此前成功收集的命令。
- 后续仍可尝试提交较小命令。
- 记录 `droppedCommands`、`droppedVertices`、`acceptedVertices`、`capacityVertices`。
- 每次 `Initialize()` 生命周期内最多输出一次预算溢出告警。
- 持久命令与当前帧命令共用这一预算，避免持久图元绕过容量约束。

### 2.6 线程与生命周期

Owner Thread 定义为：调用 `Initialize()` 且持有当前 GL 4.6 上下文的线程。

v1 约束：

- `Initialize`、`BeginFrame`、图元收集、`EndFrame`、`Shutdown` 均须在 Owner Thread 执行。
- 物理回调或 JobSystem worker 不允许直接调用。
- 未来跨线程需求须通过独立设计处理；v1 不得偷偷引入 TLS。
- 非 Owner Thread 调用必须返回明确失败状态。

`Shutdown` 约束：

- 重复调用是安全的。
- 必须在 Owner Thread、有效且当前的 GL 上下文下释放 GL 资源。
- 若上下文不满足要求，返回失败状态，不得继续执行无效 GL 调用。
- 不拥有窗口或上下文。
- 不延长 Physics World 或 Physics Debug 对象的生命周期。
- 上下文重建不做隐式恢复；必须显式 `Shutdown`/`Initialize`。

### 2.7 失败语义

API 必须返回可观测状态，不能仅返回“成功/无操作”。状态至少要能区分：

- 未初始化；
- 帧未开始 / 帧已结束；
- 重复开始 / 重复结束；
- 错误线程；
- 参数非法（含负 duration、非正半径/长度、NaN/Inf 等）；
- 后端不支持（非 GL 4.6 或后端不匹配）；
- GL 资源/上下文失败；
- 预算丢弃（属于统计事件，不是后端失败）。

重要失败必须记录日志；不得用静默跳过伪装成功。

### 2.8 物理适配边界

物理接口保持不变，适配器只负责把已有物理调试输出转换成通用线段命令。

2D 适配：

- 使用明确的 `physicsToWorld` 变换矩阵。
- 默认 Identity 表示物理 XY 平面映射到世界 XY 平面、Z 为零。
- 尊重既有 flags 的开关语义。
- 实体填充不属于 v1；涉及实体形状的回调只输出轮廓。

3D 适配：

- 转换现有线段及可表达为轮廓的调试输出。
- 不实现 Jolt `DrawGeometry` 的几何提取和绘制。
- 不修改 `JoltPhysicsWorld` 或 `JoltDebugRenderer` 的既有行为。

特别冻结：Jolt 碰撞体形状线框修复不是 T-5 v1 的验收项，应独立立项，避免把当前已知空实现混入通用 DebugDraw。

---

## 3. T-5 v1 验收门禁

进入 Gate B 后，以下项目必须全部提供自动化证据：

| 门禁 | 必须验证的行为 |
|---|---|
| CPU 图元测试 | 五类图元参数校验、顶点展开、duration 生命周期 |
| CPU 预算测试 | 达到上限、超限丢弃、组合图元原子性、统计计数 |
| 线程测试 | Owner Thread 成功，非 Owner Thread 明确失败 |
| 生命周期测试 | 未初始化、重复初始化、帧顺序错误、Shutdown 幂等、上下文缺失 |
| GL 像素测试 | 线段和五类图元实际产生正确像素；不能只依赖 `glGetError` |
| 深度测试 | 被遮挡图元在 depth-on 和 X-Ray 模式下表现不同 |
| 状态隔离测试 | 绘制前后的关键 GL 状态一致 |
| 后端测试 | GL 4.6 支持；其他后端明确 Unsupported |
| 物理适配测试 | 适配输出符合映射规则；不把 Jolt `DrawGeometry` 空实现误判为已支持 |

所有证据必须记录真实构建配置、GL 版本、驱动、测试命令、结果及警告数量。存在既有警告时原样记录；不得将 `glGetError` 无错误或 sandbox 手动观察作为自动化验收替代品。

---

## 4. Gate B 授权与实施边界

Gate B 于 2026-10-31 获用户授权；实际实施严格限于以下允许项。未授权项仍未授权，不得据本契约扩张。

| 工作项 | 当前状态 |
|---|---|
| 新增 `test_debugdraw` target、修改 `tests/CMakeLists.txt` | Gate B 已授权 — 已实施 |
| 通用 DebugDraw API、OpenGL 4.6 Renderer、物理 Adapter | Gate B 已授权 — 已实施 |
| Sandbox DebugDraw 接线 | 未授权 — 未实施 |
| Legacy `ViewportPanel` 可选接入 | 未授权 — 未实施 |
| 修复 `Box2DDebugDraw` transform/flags | 独立缺陷项 — 未实施 |
| 修复 Jolt `DrawGeometry` | 独立任务 — 未实施 |
| EngineHost / Bridge / 生产 Editor 接线 | 未授权 — 未实施 |
| RenderGraph / RHI 多后端接入 | T-5 v1 非目标 — 未实施 |

---

## 5. 当前阶段结论

| 阶段 | 裁决 |
|---|---|
| T-5 Gate 0 | `PASS — READ-ONLY COMPLETE`（依据用户提供的扫描报告） |
| T-5 Gate A | `PASS — ARCHITECTURE CONTRACT FROZEN — SCOPE-BOUND` |
| T-5 Gate B | `AUTHORIZED — PASS — SCOPE-BOUND`（待用户最终验收） |
| 源码修改 | 已实施 T-5 新增模块与必要 CMake；未改既有物理/Box2D/Jolt/RenderGraph/RHI |
| 构建 / 测试 | `test_debugdraw` Debug/Release 18/18；定向回归见测试记录 |
| Git 写入 | 未执行 |
| 本地报告文件 | Gate 0/契约/实施记录由助手写入；用户未独立核验 |

---

## 6. 被取代的历史假设

旧 `docs/DebugRenderer-2.0-Analysis-Report.md` 中以下三个假设与当前源码不符，以本次 Gate 0 结果和 Gate A 裁决为准：

1. **不依赖 RenderGraph。** 该报告假设“RenderGraph 已有基础设施可直接加 DebugDrawPass”；当前 RenderGraph 仍为 `SG6 FAIL / ISOLATED`，生产渲染不走该路径。T-5 v1 使用直接渲染阶段。
2. **不把 Jolt 形状绘制视为已有能力。** 该报告假设“JoltDebugRenderer 无需重写”；当前 `JoltDebugRenderer::DrawGeometry` 是空实现，`CreateTriangleBatch` 返回空 batch。Jolt 碰撞体形状线框修复独立立项，不属于 T-5 v1 验收。
3. **不采用任意线程 TLS 与 RenderGraph Pass 的捆绑方案。** 该报告的 P0/P1 方案把 TLS、duration、RenderGraph Pass 捆绑推进；T-5 v1 冻结为 owner-thread、显式帧生命周期、GL 4.6 单后端，TLS 与 RenderGraph 均不在 v1 范围。

### 6.1 构建配置事实（D3 观察）

2026-10-31 的 D3 修复确认：Jolt 的 `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE` 为 `ON`，`JPH_DEBUG_RENDERER` 通过 Jolt target 传播到 Release（`build/engine/EngineCore.vcxproj` Release 包含该宏）。因此 D5-11 中“Jolt 物理适配保持 Debug-only”的措辞与当前实际构建不一致。

本契约不追溯改写 D5-11；D3 修复按实际构建在 Debug/Release 均可运行，并记录于 `docs/T-5-DebugDraw-D3-Jolt-DrawGeometry-Fix.md`。该配置事实是否升级为契约修订，需用户裁决。

---

## 7. 变更控制

- 本契约冻结后，任何范围、线程模型、图元集合、预算、深度策略、后端或验收门禁的变更，必须单独裁决并更新本文件。
- Gate B 实现不得夹带 §1.3 列出的非目标；不得顺手修复 Box2D 或 Jolt 的既有缺陷。
- 本契约不构成对 `docs/T-5-DebugDraw-Gate0-Scan.md` 之外源码事实的独立验证。
- Gate B 已于 2026-10-31 单独授权并完成定向验证；后续范围变更仍需单独裁决，不得以本次授权扩张到非目标。
