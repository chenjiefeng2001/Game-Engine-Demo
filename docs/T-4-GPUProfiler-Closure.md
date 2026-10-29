# T-4 GPUProfiler — 关闭记录（方案 A：GL + Vulkan，不含 Tracy GPU）

**日期**：2026-10-29
**状态**：`T-4 — PASS — SCOPE-BOUND`
**范围**：后端无关 GPU 时间戳 profiler、Vulkan `VkQueryPool` 时间戳、
OpenGL 既有 `GL_TIMESTAMP` 路径验证、RenderGraph Pass 插桩入口、valid/降级语义。
**未执行**：Tracy GPU（T-4b 另行裁决）；未 commit/push/tag/merge；未改 T-1/T-2 语义。

---

## 1. 实际修改

| 文件 | 修改 |
|---|---|
| `engine/include/Engine/Core/RHI/IRHIDevice.h` | 新增 `SupportsGpuTimestamps()`（默认 false） |
| `engine/include/Engine/Core/RHI/GL46AZDODevice.h` / `engine/src/OpenGL/GL46Device.cpp` | GL46 能力查询：`GL_QUERY_COUNTER_BITS` + 当前上下文检查 |
| `engine/include/Engine/Core/RHI/VulkanIRHIDevice.h` / `engine/src/Vulkan/VulkanDevice.cpp` | Vulkan 能力查询：graphics queue family `timestampValidBits` + `timestampPeriod` |
| `engine/src/Vulkan/VulkanCommandList.cpp` | 每命令列表 `VkQueryPool`（`VK_QUERY_TYPE_TIMESTAMP`，512 slots）；`WriteTimestamp` / `ResolveTimestampSpan` / `ResetTimestamps`；`Begin()` 录制 reset；析构销毁 query pool |
| `engine/include/Engine/Core/RHI/GpuTimestampProfiler.h` | `SegmentResult.valid`；新增 `CreateCommandListGpuTimestampProfiler()` |
| `engine/src/RHI/CommandListGpuTimestampProfiler.cpp` | 新增后端无关 profiler：只依赖 `IRHICommandList` timestamp API + `IRHIDevice::SupportsGpuTimestamps()` |
| `engine/src/OpenGL/GL46GpuTimestampProfiler.cpp` | 旧 GL46 专用 factory 改为返回统一 profiler（旧类保留但不再由 factory 返回） |
| `engine/include/Engine/Rendering/RenderGraph.h` / `engine/src/Rendering/RenderGraph.cpp` | 新增 `Set/GetRenderGraphGpuTimestampProfiler`；`Execute` 与无 ScenePass 的 `ExecuteParallel` 在 Pass 边界插桩；移除旧 `GPUProfiler` hook |
| `engine/include/Engine/Core/RHI/GPUProfiler.h` | 标注旧 `RHI::GPUProfiler` 为 deprecated、无生产调用点；不再被 RenderGraph include |
| `tests/test_renderer/GL46DeviceTest.cpp` | 新增 `GLCommandListTimestampQuery`（真实 GL 上下文 + `glQueryCounter`） |
| `tests/test_renderer/GpuTimestampProfilerTest.cpp` | 新增 null-device、Vulkan 真实时间戳、maxSegments 拒绝 |
| `tests/CMakeLists.txt` | `test_renderer` 加入新测试源 |

---

## 2. 契约行为

- `IGpuTimestampProfiler` 是唯一后端无关 GPU 计时 API；GL46/Vulkan 共用同一实现。
- `Initialize` 只有在 `IRHIDevice::SupportsGpuTimestamps()` 为 true 且 `maxSegments > 0` 时成功；
  否则 `IsAvailable=false`，`BeginSegment` 返回 `UINT32_MAX`，`Resolve` 为空。
- `BeginSegment/EndSegment` 在命令录制期写入时间戳；`Resolve` 在 GPU 完成后读取；
  未闭合段不进入结果；无效段以 `valid=false` 保留（`gpuMs` 无意义）。
- `Resolve` 成功后对涉及的命令列表调用 `ResetTimestamps`，下一帧可复用查询环。
- 不再返回旧 stub 的 `0.5ms` 占位值。
- 查询池耗尽时返回 `UINT32_MAX`，不覆盖既有结果；`maxSegments` 之外的分段被拒绝。

---

## 3. 实测结果

### 3.1 构建

| 目标 | 配置 | 结果 |
|---|---|---|
| `EngineCore` | Debug | 成功；既有警告 C4005 / C4477 / LNK4006 |
| `EngineCore` | Release | 成功；同既有警告 |
| `test_renderer` | Debug | 成功 |
| `test_renderer` | Release | 成功 |

未做 clean rebuild；**不宣称零警告/零失败**。

### 3.2 测试

| 测试 | Debug | Release | 日志 |
|---|---|---|---|
| `GL46DeviceTest.GLCommandListTimestampQuery` | **PASS** | **PASS** | `build/t4_timestamp_tests_debug.log` / `build/t4_timestamp_and_rg_release.log` |
| `GpuTimestampProfilerTest.NullDeviceIsExplicitlyUnavailable` | **PASS** | **PASS** | 同上 |
| `GpuTimestampProfilerTest.VulkanTimestampIsRealOrExplicitlyUnavailable` | **PASS** | **PASS** | 同上 |
| `GpuTimestampProfilerTest.MaxSegmentsRejectsExtraSegment` | **PASS** | **PASS** | 同上 |
| `RenderGraphSG6`（3 项） | **3/3 PASS** | **3/3 PASS** | `build/t4_rendergraph_{debug,release}.log` |
| `test_renderer` 全量 | **18/18 PASS** | **18/18 PASS** | `build/t4_test_renderer_final_{debug,release}.log` |

- Vulkan 测试在真实 Vulkan 设备（headless）上创建 `VkQueryPool`，空命令列表提交、
  `WaitIdle` 后 `Resolve`；`valid=true` 且不等于旧 stub 的 `0.5ms`。
- GL 测试在真实 GL 上下文（本机 NVIDIA 驱动；测试夹具请求 3.3 core）上直接验证
  `glQueryCounter(GL_TIMESTAMP)` 的 `WriteTimestamp/ResolveTimestampSpan`。
- null-device 测试验证 `Initialize=false`、`IsAvailable=false`、`Resolve` 空。
- maxSegments=1 时第二次 `BeginSegment` 返回 `UINT32_MAX`，第一段仍可解析。

### 3.3 未运行

- 完整 CTest、完整 `test_bridge`/`test_content`：**NOT RUN**（本切片只跑 GPU/Render 定向与 `test_renderer` 全量）。
- Vulkan 生产应用路径：Vulkan 设备目前由 sandbox/tests 使用，生产编辑器使用 OpenGL；
  未在真实 Vulkan 窗口中运行 RenderGraph profiler。
- Tracy GPU：**NOT RUN**（方案 A 明确不含，留 T-4b）。

---

## 4. 已证明 / 未覆盖

### 4.1 已证明

- Vulkan `VkQueryPool` 时间戳真实写入与解析；`valid` 与 `gpuMs` 来自 GPU 查询。
- GL `GL_TIMESTAMP` 真实查询；GL 上下文能力检查与降级。
- 后端无关 profiler 的初始化、分段、解析、valid 标记与 maxSegments 拒绝。
- 无设备时显式不可用，不伪造数据。
- RenderGraph `Execute` 与无 ScenePass `ExecuteParallel` 的 Pass 边界插桩编译并通过现有 RenderGraph 回归。
- 旧 `RHI::GPUProfiler` 0.5ms stub 已从 RenderGraph 移除，且无生产调用点。

### 4.2 明确未覆盖

1. **GL 的 generic profiler 路径**：GL46 测试夹具的 `Initialize(nullptr)` 创建 stub，
   因此 generic profiler 的 GL 设备路径未直接测试；已用真实 GL 命令列表直接验证查询链。
2. **Vulkan 生产消费者**：`SetRenderGraphGpuTimestampProfiler` 只有 hook，没有生产调用方；
   未把 Vulkan 结果接入 PerformanceWindow。生产编辑器的 GL 路径仍使用既有
   `IRenderContext::GetGPUProfileFrame`（真实时间戳）。
3. **ScenePass 并行路径**：`ExecuteParallel` 的 ScenePass 分片路径未插桩；
   单线程 `Execute` 与无 ScenePass 并行路径已插桩。
4. **多帧 ring/latency**：`Resolve` 在 `WaitIdle` 后调用；未验证无等待的多帧样本年龄/延迟。
5. **Tracy GPU**：未接入；`PROFILE_GPU_*` 宏仍零调用。
6. **旧 stub 文件**：`engine/src/RHI/GPUProfiler.cpp` 与 `GPUProfiler.h` 保留但 deprecated、
   无调用点；未删除文件。
7. **Vulkan 局限性**：查询池为每命令列表 512 slots；未验证多队列/异步 compute 的跨队列时间戳。
8. **空 pass 时间戳**：Vulkan 测试用空命令列表，`gpuMs` 可能为 0；测试验证的是
   `valid` 与真实查询路径，不是性能数值大小。

---

## 5. 与其他事项的关系

- F2-S1 保持独立未关闭（方案 B：先完成 T-4；未改 Collider 序列化）。
- T-1/T-2 状态不变；未修改 RigidBody/物理/Content/导入语义。
- T-6 Console 等后续 P1 项未启动。

---

## 6. 工作区与 Git

- 未执行 commit / push / tag / merge / reset / clean / stash。
- 未触碰 `crashes/`、三个脏子模块、T-2 关闭记录、`p7_ui` worktree。
- 构建/测试日志在 `build/t4_*.log`；未清理任何既有失败/日志。

---

## 7. 结论

**T-4 — PASS — SCOPE-BOUND**：GL 与 Vulkan 的真实 GPU 时间戳路径、统一的
`IGpuTimestampProfiler`、valid/降级语义与 RenderGraph Pass 插桩入口已实现并验证；
Tracy GPU、Vulkan 生产消费者、ScenePass 并行插桩与多帧延迟留作明确未覆盖项。
