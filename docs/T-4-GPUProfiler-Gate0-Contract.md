# T-4 GPUProfiler — Gate 0 基线与 Gate A 契约提案

**日期**：2026-10-29
**状态**：`GATE 0 — READ-ONLY BASELINE VERIFIED; GATE A — APPROVED (方案 A); IMPLEMENTED`
**范围**：只读源码扫描与契约冻结；本文件不修改源码、不运行 GPU 测试、不 commit/push/tag/merge。
**前置**：T-1 `CLOSED — SCOPE-BOUND`；#141 `PASS`；F2-S1 按方案 B 保持独立未关闭。
**实施结果**：见 `docs/T-4-GPUProfiler-Closure.md`（`T-4 — PASS — SCOPE-BOUND`）。

---

## 1. Gate 0：现有事实（纠正文档中的“Stub”表述）

### 1.1 OpenGL 路径已有真实时间戳

`OpenGLContext` 已实现真实 `GL_TIMESTAMP` 查询环：

- `InitGPUQueries()` 检查 `GL_QUERY_COUNTER_BITS`，为 0 时不启用；
- `BeginGPUPass/EndGPUPass` 在录制期插入 `glQueryCounter(..., GL_TIMESTAMP)`；
- `SwapBuffers()` 在下一次交换时检查 `GL_QUERY_RESULT_AVAILABLE`，读取上一帧结果；
- `GetGPUProfileFrame()` 返回上一帧的 `GPUProfileFrame`（最多 32 Pass），
  包含 begin/end 与 `elapsedMs`；
- 被 `Application` 的 PerformanceWindow 与多个 sandbox 真实消费。

证据：`engine/src/OpenGL/OpenGLContext.cpp:50-79, 281-344`；
`engine/include/Engine/Core/IRenderContext.h:20-45, 80-95`；
`engine/src/Core/Application.cpp:1016-1028`；
sandbox 调用点 `sandbox/src/RenderTest/RenderTestApp.cpp:390-489` 等。

**结论**：OpenGL 并非“没有真实 GPU 计时”；文档中“GPUProfiler 30% / Stub”指的是
另一个未使用的旧类（§1.3），不能把两者混为一谈。

### 1.2 后端无关的分段 profiler 只实现了 GL

`IGpuTimestampProfiler`（`GpuTimestampProfiler.h:29-64`）提供：
`Initialize/Shutdown/BeginFrame/BeginSegment/EndSegment/Resolve/IsAvailable`；
当前唯一实现是 `GL46GpuTimestampProfiler`（`GL46GpuTimestampProfiler.cpp:29-123`），
由 `PhysicsBenchmark` 使用。它检查 GL 计数位，按分段持有命令列表引用，
`Resolve()` 前要求 GPU 已完成；失败返回 `UINT32_MAX` / 跳过未闭合段。

### 1.3 旧 `RHI::GPUProfiler` 是未被生产使用的 stub

`engine/src/RHI/GPUProfiler.cpp:24-85` 的 `GPUProfiler` 在 `EndFrame()` 直接填
`0.5ms` 占位值；它只通过 `RenderGraph.cpp:22-30` 的 `SetRenderGraphGPUProfiler`
暴露一个 hook，**没有任何调用点**；`Application` 不消费它。它是历史接口，
不得作为 GPU 计时证据。

### 1.4 Vulkan 没有时间戳实现

`IRHICommandList::WriteTimestamp/ResolveTimestampSpan/ResetTimestamps` 的默认实现是
no-op / `kInvalidTimestamp`（`IRHICommandList.h:248-266`）；Vulkan 命令列表没有覆盖。
`IRenderContext::GetGPUProfileFrame` 默认返回 false；Vulkan 后端没有等价实现。
Vulkan 设备目前主要由 sandbox/tests 使用（`RHIDeviceFactory.h` 说明生产调用为零），
但 `CreateRHIDevice(Backend::Vulkan,...)` 在测试中可用。

### 1.5 Tracy GPU 宏存在但零调用

`engine/include/Engine/Profiler.h:106-112` 定义了
`PROFILE_GPU_CONTEXT/PROFILE_GPU_ZONE/PROFILE_GPU_COLLECT`（Debug 下映射到 Tracy），
但 `engine/`、`sandbox/`、`tests/` 内**没有调用点**；Tracy GPU 数据尚未提交。
Tracy CPU zone 已有既存集成；Tracy 版本为 0.13.1。

---

## 2. Gap analysis

| 项目 | 现状 | T-4 缺口 |
|---|---|---|
| OpenGL 每 Pass 时间戳 | 真实实现；sandbox 手工 Begin/End；Application 读取上一帧 | 生产 RenderGraph 未自动插桩；无 validity/降级字段 |
| 后端无关 API | `IGpuTimestampProfiler` 只有 GL46 | 缺少 Vulkan 实现与统一消费方 |
| Vulkan 时间戳 | 无 | 需要 `VkQueryPool` / `WriteTimestamp` / 读回 / 失效处理 |
| 旧 stub | `RHI::GPUProfiler` 返回 0.5ms，无调用点 | 应废弃/删除或明确标记非证据；不得保留假数据路径 |
| Tracy GPU | 宏存在，零调用 | 是否在 T-4 内接入 `TracyGpuContext/Zone/Collect` 需裁决 |
| 数据有效性 | GL 只在 `GL_QUERY_RESULT_AVAILABLE` 后读取；无显式 `valid` 字段 | 需要显式 `valid`、样本年龄、不可用原因 |
| 降级 | GL 计数位为 0 时静默不启用；UI 可能显示空/0 | 需要 `IsAvailable=false` + UI “GPU timing unavailable”，**不得填假值** |
| 测试 | 无 GPU timestamp 专项测试；sandbox 仅消费 | 需要真实上下文有效数据 + 无上下文降级 + 资源释放测试 |

---

## 3. Gate A 契约提案

### 3.1 单一 API

以现有 `IGpuTimestampProfiler` 作为**唯一后端无关契约**；不为 T-4 另造第二套
Profiler 接口。现有 `GPUProfileFrame`/`GetGPUProfileFrame` 保留为兼容视图，
但 `IGpuTimestampProfiler` 是新增 GPU 计时能力的权威来源。

### 3.2 指标与语义

| 项 | 提案 |
|---|---|
| 分段 | `BeginSegment(cmd, name)` / `EndSegment(cmd, id)`，录制期调用；每段一对时间戳 |
| 结果 | `SegmentResult{name, gpuMs, valid}`；`valid=false` 表示该段无有效查询结果 |
| 聚合 | 每帧 `Resolve()` 返回已完成的上一帧分段；不把 CPU 时间当 GPU 时间；不填常量 |
| 可用性 | `IsAvailable()` 仅在设备/驱动支持时间戳且查询池创建成功时为 true |
| 不可用 | `Initialize` 返回 false；`BeginSegment` 返回 `UINT32_MAX`；`Resolve` 为空；UI 显示不可用 |
| 查询耗尽 | 超过 `maxSegments`/查询池上限时返回 `UINT32_MAX`，不覆盖未解析结果 |
| 生命周期 | `Initialize(device,maxSegments)` → 每帧 `BeginFrame` → 录制期 Begin/End → GPU 完成后 `Resolve` → `Shutdown`；查询池复用、无每帧分配 |
| 设备丢失/重置 | `Shutdown` 幂等；设备丢失后 `IsAvailable=false`，不崩溃、不返回旧数据冒充新帧 |

### 3.3 后端

- **OpenGL**：复用现有 `GL46GpuTimestampProfiler`/`GL46CommandList` 查询逻辑；
  保持 `GL_QUERY_COUNTER_BITS` 检查与 `GL_QUERY_RESULT_AVAILABLE` 读取。
- **Vulkan**：新增 `VkQueryPool`（`VK_QUERY_TYPE_TIMESTAMP`）、命令列表
  `WriteTimestamp`/`ResolveTimestampSpan`/`ResetTimestamps` 覆盖、读回与失效路径；
  仅在 `CreateRHIDevice(Backend::Vulkan,...)` 可用时启用。
- **D3D12/Metal**：T-4 不实现；`IsAvailable=false` 并按 §3.2 降级。

### 3.4 生产接线

- 生产 RenderGraph 的 Pass 边界插入 profiler Begin/End（或由现有 Pass 执行器统一包裹），
  使 `Application` 的 PerformanceWindow 获得真实每 Pass 数据；
- 不再使用 `RHI::GPUProfiler` 的 0.5ms 假数据；
- `PerformanceWindow` 在 `IsAvailable=false` 时显示明确不可用状态，不显示假 0.5ms。

### 3.5 测试

| 测试 | 判据 |
|---|---|
| GL 真实上下文 | 创建隐藏 GL 上下文，录制一个已知 pass（clear/draw），WaitIdle 后 `Resolve`；`IsAvailable=true`、至少一段 `valid=true`、`gpuMs >= 0` 且不是固定 0.5ms |
| GL 不支持降级 | 计数位 0/无上下文时 `Initialize=false`、`BeginSegment=UINT32_MAX`、`Resolve` 空、无崩溃 |
| Vulkan 真实设备 | 若 `CreateRHIDevice(Backend::Vulkan,...)` 成功：离屏命令列表录制、提交、fence 后 Resolve；`valid=true`；若设备不可用：`IsAvailable=false`，测试记 `BLOCKED`/skip，不冒充 PASS |
| 查询耗尽 | 超过 maxSegments 返回无效 id，不覆盖既有结果 |
| 生命周期 | 重复 Initialize/Shutdown、重复 Resolve 无崩溃/无泄漏（在可用工具范围内） |
| 生产接线 | PerformanceWindow 显示的 Pass 数与 RenderGraph 实际 pass 数一致；不可用时显示 unavailable |

> 不以“接口能被调用”作为通过；必须有真实时间戳结果或明确的不可用降级证据。

---

## 4. 范围选项（需要裁决）

### 方案 A（推荐）：GL + Vulkan 统一 API + 生产 RenderGraph 接线，不含 Tracy GPU

- 交付：`IGpuTimestampProfiler` 的 Vulkan 实现、GL 复用、生产 RenderGraph 插桩、
  validity/降级、测试与封存记录。
- 优点：对应 T-4 的“可验证 GPU 测量路径”；不引入 Tracy GPU 的第三方集成风险；
  可在 GL 上立即验证，Vulkan 视设备可用性记录 BLOCKED。
- 代价：Vulkan 设备在当前环境是否可用需先探测；若不可用，Vulkan 动态证据受限。

### 方案 B：A + Tracy GPU 接入

- 额外：`TracyGpuContext/Zone/Collect`（GL/Vulkan）与 Tracy 采集证据。
- 风险：需要 Tracy server/GPU 采集环境；验证成本高；应作为独立切片（T-4b）。

### 方案 C：仅 OpenGL 生产接线，Vulkan/Tracy 延后

- 交付：把现有 GL 计时接入生产 RenderGraph，删除/废弃旧 stub，完善 validity/降级。
- 优点：最小、可立即验证；对应“生产编辑器当前使用 GL”的事实。
- 代价：不满足路线的“Vulkan/GL + Tracy”完整表述；Vulkan GPU 计时仍为缺口。

### 不采用：保留旧 `RHI::GPUProfiler` 0.5ms stub 并称为“已实现”

- 与“可信测量”目标冲突；不得把假数据作为证据。

---

## 5. 推荐

推荐 **方案 A**：

1. 统一 `IGpuTimestampProfiler`，消除两套 profiler 的语义分叉；
2. GL 已有真实实现可复用，风险主要集中在 Vulkan 查询池与生产插桩；
3. Tracy GPU 作为 T-4b 单独验证，避免把第三方采集环境问题混入 T-4 主验收；
4. 对 Vulkan 设备不可用的环境，以 `IsAvailable=false` + `BLOCKED` 记录，不伪造数据。

---

## 6. 需要裁决的细节

1. 选 A / B / C；
2. 是否要求生产 RenderGraph 自动插桩，还是只交付 profiler API + sandbox 证据；
3. 是否删除/废弃旧 `RHI::GPUProfiler` 与 `SetRenderGraphGPUProfiler` hook；
4. Vulkan 设备不可用时，是否接受 `BLOCKED`（而不是要求必须在 CI/本机取得 Vulkan 时间戳）；
5. Tracy GPU 是否留到 T-4b。

在获得契约裁决前，不进入实现。

---

## 7. 本轮未做

- 未修改 GPU/RHI/RenderGraph 源码；未运行 GPU benchmark 或 timestamp 测试；
- 未 commit/push/tag/merge；未触碰 `crashes/`、脏子模块、T-2 关闭记录。
