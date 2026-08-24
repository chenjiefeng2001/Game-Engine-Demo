# GPU 物理 v3.0 架构演进方案 — Simulation Domain 化

> **日期**: 2026-08-23
> **状态**: 取代 `GPU-Physics-v2.5-Architecture-Upgrade.md` 中的 Phase A-D 路线
> **依据**: v2.5 外部架构评审（置信度 95/100）+ 本仓库代码实审（GPUPhysicsEngine.cpp / GL46ComputeShaders.inl / IRHICommandList.h / OpenGLContext.cpp / PhysicsBenchmark/main.cpp）
> **核心理念**: 收敛 **State → Compute → Snapshot** 计算语义，暂停"补全工业组件清单"

---

## 目录

1. [为什么是 v3.0：三大语义转向](#一为什么是-v30三大学义转向)
2. [v2.5 关键设计缺陷修正](#二v25-关键设计缺陷修正)
3. [五阶段路线图总览](#三五阶段路线图总览)
4. [Phase 0 — 真实 GPU Benchmark Baseline](#phase-0--真实-gpu-benchmark-baseline)
5. [Phase 1 — Shader 单一真理源](#phase-1--shader-单一真理源)
6. [Phase 2 — Simulation Contract + Ping-Pong Snapshot](#phase-2--simulation-contract--ping-pong-snapshot)
7. [Phase 3 — Execution Completion Token](#phase-3--execution-completion-token)
8. [Phase 4 — Spatial Hash 碰撞内核](#phase-4--spatial-hash-碰撞内核)
9. [Scope Guards — 明确排除项](#九scope-guards--明确排除项)
10. [v2.5 → v3.0 差异对照表](#十v25--v30-差异对照表)
11. [风险登记表](#十一风险登记表)

---

## 一、为什么是 v3.0：三大语义转向

### 1.1 从 "Physics Backend" 到 "Simulation Domain"

物理不再是拥有同步 `Step()`/`Raycast()` API 的独立模块，而是降化为一个**仿真计算域**：

```
输入 State_n (immutable) ──▶ Compute(dt) ──▶ 输出 State_{n+1} (immutable snapshot)
```

在本仓库的落地形态：

```cpp
// 唯一的数据契约（纯数据，无行为）
struct SimulationSnapshot {
    RHI::IRHIBuffer* stateBuffer;   ///< Flip 后对消费者只读
    uint32_t         particleCount;
    uint32_t         frameIndex;
    float            simulationTime;///< 固定步长累计时间（用于插值 alpha）
};

// 线性纯函数接口 —— 不是 Graph，不是 Node 编辑器
class ISimulationDomain {
public:
    virtual ~ISimulationDomain() = default;
    /// 录制式：向 ctx 录入 compute 命令，返回完成后的快照句柄
    virtual SimulationSnapshot Step(const SimulationSnapshot& input,
                                    float fixedDt,
                                    RHI::IRHICommandList& cmd) = 0;
};
```

### 1.2 RenderGraph 保持"渲染域专用"

现有 `RenderGraph.cpp`（505 行，AddPass/Compile/ExecuteParallel/DumpGraph 功能齐全但零接线）**维持现状不动**。它只解决 Transient Resource 复用、Async Compute 派发与自动 Barrier —— 物理仿真包含跨帧长生命周期状态与 CPU 回读需求，强行塞入会倒置职责。未来若需集成，快照 buffer 以**外部资源导入**方式进入渲染图，而非成为 graph 节点。

### 1.3 同步语义统一为 Execution Model

Fence / Wait / Poll 从 GPUPhysicsEngine 中剥离，沉淀为 RHI 层通用 `CompletionToken`（Phase 3）。GPU 物理、未来的 GPU Culling、资产异步解压共享同一套异步等待逻辑。

---

## 二、v2.5 关键设计缺陷修正

### 缺陷 D1（严重）：GPU 端固定步长累加器存在跨工作组竞态

v2.5 附录 E 的着色器设计：

```glsl
if (gl_GlobalInvocationID.x == 0) {          // ← 全局仅一个 invocation 为 true，
    global.u_Accumulator += ...;             //   但其余工作组的执行不受任何同步约束
}
barrier();                                   // ← barrier() 仅同步【本工作组】内线程！

while (global.u_Accumulator >= global.u_FixedDt && steps < maxSubSteps) { ... }
```

**问题**：单个 Dispatch 内多个工作组之间没有执行同步原语（`barrier()` 语义限于 workgroup）。除 0 号工作组外，其它工作组读取 `u_Accumulator` 时序不确定 → 各工作组解出的 substep 数可能不同 → **粒子按内存分布被撕裂成不同时间步长的孤岛**，且结果不可复现。

**v3.0 修正**：固定步长决策回到 CPU（`FixedTimestepAccumulator.h` 已实现且经过验证），每 substep 一次 Dispatch，shader 内只积分一步：

```cpp
FixedTimestepAccumulator& acc = GetAccumulator();
uint32_t steps = acc.Advance(dt);            // 含 spiral-of-death 截断
for (uint32_t s = 0; s < steps && s < kMaxSubStepsPerFrame; ++s) {
    RecordIntegratePass(cmd, m_Bufs[m_Write]);   // 单步积分
    RecordCollidePass(cmd);
    m_Bufs.Flip();                                // 每 substep 翻转
}
m_LastAlpha = acc.GetAlpha();                // 渲染插值系数
```

Dispatch 开销在 GL46 上为微秒级（≤4 次/帧可忽略），换取的是**完全确定的执行语义**。GPU 端累加器仅在引入跨 Dispatch 全局同步机制（atomic + 多 pass 或 GL 4.6 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` 配合独立 Dispatch）后才有意义——列入远期探索，非 MVP。

### 缺陷 D2：ReadbackParticles 内嵌 glFinish（C4 瓶颈确认）

`GPUPhysicsEngine.cpp:148-173`：回读路径先 `gl.Finish()` 再读持久映射 + `glGetNamedBufferSubData` 交叉验证 —— 整条管线每帧全排空。v3.0 将其整体替换为 Phase 3 的三缓冲异步回读环，`gl.Finish()` 从该文件中清零。

### 缺陷 D3：Assets 着色器与运行时脱节

| 事实 | 位置 |
|------|------|
| 运行时编译的是 `.inl` 内嵌字符串 | `GL46ComputeShaders.inl`（162 行），由 `GL46Device::CompileComputeShaders()` 按 `"gpu_physics_integrate"/"gpu_physics_collide"` 哈希注册 |
| `assets/shaders/gpu_physics_*.glsl` 从未被加载 | 且 uniform 名不匹配：`u_DeltaTime/u_Stiffness` vs 引擎设置的 `u_Dt/u_Restitution` |

→ Phase 1 单一真理源改造的直接动因。

---

## 三、五阶段路线图总览

```
【当前节点】HEAD 66dd93f（边界碰撞 + 半隐式欧拉 MVP，Ring0-3 PASS）
     │
     ├──► Phase 0: 真实 GPU Benchmark Baseline          (~2 天)
     │      修复 PhysicsBenchmark 占位循环；
     │      将 OpenGLContext 已有 GL_TIMESTAMP 查询提升至 RHI 层
     │      产出: 分段耗时基线 CSV (Upload/Integrate/Barrier/Collide/Sync)
     │
     ├──► Phase 1: Shader 单一真理源                      (~1.5 天)
     │      废弃 .inl 手写内嵌 → 构建期由 assets/shaders 生成嵌入头；
     │      修复 uniform 名漂移 (u_DeltaTime→u_Dt)
     │
     ├──► Phase 2: Simulation Contract + Ping-Pong        (~3 天)
     │      SimulationSnapshot 契约 + 双缓冲乒乓 +
     │      CPU FixedTimestepAccumulator 驱动 substep（修正 D1）
     │
     ├──► Phase 3: Execution Completion Token             (~2 天)
     │      RHI Fence 抽象 + 通用 GpuReadbackRing<T>；
     │      清零 GPUPhysicsEngine 内 gl.Finish()（修正 D2）；
     │      语义禁止同帧等待
     │
     └──► Phase 4: Spatial Hash 碰撞内核                   (~3.5 天)
            ClearCount → Hash → PrefixScan → Scatter → 26-Cell Collide
            （线性 pass 序列，非 DAG）；暴力版保留为参考实现做冲量 diff

总计 ≈ 12 人天（v2.5 估算 ~17 人天，因砍掉 RenderGraph 集成与 GPU 累加器而收缩）
```

依赖关系：Phase 0 → 1 可并行；2 依赖 1（新着色器走新管线）；3 独立于 4，但 4 的性能验证依赖 3 的异步计时。

---

## Phase 0 — 真实 GPU Benchmark Baseline

> 评审置信度 99%。"在没有基准之前不做架构微优化"。当前 `PhysicsBenchmark/main.cpp:431-435` 的 GPU 循环体是注释掉的占位，测得的是空循环。

### 目标

拿到 65536 粒子下 `O(N²)` collide 的**真实**分段开销，为后续所有优化提供对照基线。

### 改造清单

| # | 任务 | 文件 | 说明 |
|---|------|------|------|
| P0.1 | RHI 时间戳查询接口 | `engine/include/Engine/Core/RHI/IRHICommandList.h` | 新增 `virtual void WriteTimestamp() = 0`（默认空实现，向后兼容）；`GL46CommandList` 用 `glQueryCounter(..., GL_TIMESTAMP)` 实现 |
| P0.2 | 时间戳解析 | `GL46Device` / 新增 `IGpuTimestampHeap` | 复用 `OpenGLContext::InitGPUQueries/BeginGPUPass/EndGPUPass`（`OpenGLContext.cpp:281-340`）已验证的模式：query 对象池 + `GetQueryObjectui64v` 解析 |
| P0.3 | Benchmark 真实录制 | `sandbox/src/PhysicsBenchmark/main.cpp` | 替换 :429-444 占位段：`cmdList->Begin()` → `gpuEngine->Update()` → `cmdList->End()` → 队列提交；每 pass 前后 WriteTimestamp |
| P0.4 | 分段指标落盘 | 同上 | CSV 新增列：`Upload_us, Integrate_us, Barrier01_us, Collide_us, Sync_us, TotalGpu_us`；粒子规模扫描 `{4096, 16384, 65536}` |

### 验收标准

- [x] `gpuComputeMs` 反映真实 GPU 时间（三路交叉验证：probe 墙钟 ≈ segmented 分段和 ≈ throughput GPU-ts，偏差 <5%）
- [x] 65536 粒子 collide 分段耗时确立：**110.7 ms**（Release / RTX 3070 Laptop）—— 显著高于 v2.5 预估的 ">8ms"，空间哈希 Phase 4 的实际加速比潜力 **~100x**
- [x] 无 GL 上下文环境下优雅降级（沿用现有 stub 模式约定）

### ✅ 实施完成记录（2026-08-23）

**基线数据**（Release，NVIDIA RTX 3070 Laptop GPU，OpenGL 4.6.0，`docs/benchmarks/gpu-physics-baseline.csv`）：

| 粒子数 | Integrate ms | Barrier01 ms | Collide O(N²) ms | Throughput ms/帧 | 备注 |
|--------|-------------|--------------|------------------|------------------|------|
| 1,024 | 0.019 | 0.0015 | 0.737 | 0.699 | |
| 4,096 | 0.059 | 0.0013 | 3.127 | 3.147 | |
| 16,384 | 0.626 | 0.0013 | 13.279 | 14.274 | |
| 65,536 | 8.160 | 0.0014 | **110.665** | 118.188 | Phase 4 对照锚点 |
| 262,144 | — | — | ~1267 (probe) | guard 生效 | 慢层级护栏触发 |

观察项：Collide 严格 ∝ N²；Integrate@64K 的 8.16ms 异常偏高（纯积分应 <0.5ms），疑为相邻 collide pass 的 SSBO 缓存污染，待 Phase 2 双缓冲分离读写后复核。

**实施产出**：

| 文件 | 变更 |
|------|------|
| `IRHICommandList.h` | 新增 `WriteTimestamp() / ResolveTimestampSpan() / ResetTimestamps()`（默认 no-op，后端零影响） |
| `GL46CommandList.cpp/.h` | 512 槽时间戳查询环实现（复用 OpenGLContext 已验证模式） |
| `GpuTimestampProfiler.h` + `GL46GpuTimestampProfiler.cpp` | RHI 接口 + GL46 工厂实现，分段剖析器 |
| `GPUParticle.h` / `GPUPhysicsEngine.cpp` | Update 拆分为 `RecordIntegrate/RecordCollide`（行为等价，Ring 测试 9/9 PASS，vy=-1.6303 与历史值一致）；为 Phase 2 Step 契约铺路 |
| `PhysicsBenchmark/main.cpp` | 真实 GL 上下文 + 分段剖析 + 吞吐口径 + 自适应帧预算 + 慢层级护栏 + `--counts` 子集参数 |

**⚠️ 附带发现的引擎缺陷**：`GL46Queue::WaitIdle()` 为空实现（GL46SwapChain.cpp:35），任何依赖它做同步的调用方都会得到虚假的墙钟数据。Benchmark 已改走 `GL46Device::WaitIdle()`（真 glFinish）。建议后续修复或在 `IRHICommandQueue` 接口文档中显式标注该语义。

---

## Phase 1 — Shader 单一真理源

> 评审置信度 100%。Source of Truth 混乱是灾难源头；`.inl` 切断编译管线、热重载、静态分析与反射。

### 方案选型

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| A. 运行时 VFS 读 `.glsl` | 热重载天然支持 | 引入启动期文件依赖；发布包需携带 shader | 否决（MVP 阶段） |
| B. 构建期脚本生成嵌入头 | 零运行时依赖；assets 即唯一源；diff 可审计 | 改 shader 需重构建 | ✅ 采用 |

### 改造清单

1. **新增工具** `tools/embed_shaders.py`：扫 `assets/shaders/*.comp.glsl` → 生成 `engine/src/OpenGL/GL46ShaderSources.g.h`（`constexpr std::string_view` + 内容 hash 注释头，标注"generated, do not edit"）。
2. **CMake 集成**：`add_custom_command` 挂在 EngineCore 之前；shader 变更触发重新生成。
3. **删除** `GL46ComputeShaders.inl`，`GL46Device::CompileComputeShaders()` 改 include 生成头。全仓库 grep `R"GLSL` 应零命中。
4. **统一 uniform 契约**：以运行时契约为准（`u_Dt / u_Gravity / u_Restitution / u_Damping / u_ParticleCount / u_BoxMin / u_BoxMax`），重写 assets 下两个 compute 文件使其成为真正的源（当前 assets 版本的 `u_DeltaTime/u_Stiffness` 属于死代码漂移）。
5. **头部注释约定**：每个 shader 文件头部声明其绑定的 C++ 结构与布局契约，供后续反射元数据提取。

### 验收标准

- [x] `rg 'R"GLSL|s_IntegrateCS|s_CollideCS' engine/src --glob "!*.g.h"` 零命中
- [x] 修改 `assets/shaders/*.comp.glsl` → 构建 → 行为变化生效（传播实验：积分着色器临时 ×0.5 重力因子，Ring2 vy 从 -1.6303 精确变为 -0.8152；恢复后回归 -1.6303）
- [x] Ring0-Ring3 全部 PASS（9/9，行为等价迁移）

### ✅ 实施完成记录（2026-08-23）

**方案偏差说明**：生成器采用**纯 CMake 脚本**（`tools/embed_shaders.cmake`）而非文档原定的 Python —— 构建循环零外部解释器依赖，CI 容器无需预装 Python；`string(MD5)` 提供内容完整性注释。

| 文件 | 变更 |
|------|------|
| `tools/embed_shaders.cmake` | 新增：扫描 `*.comp.glsl` → 生成注册表头；幂等写（内容不变跳过）；fail-fast（目录空/分隔符冲突/写入失败即致命错误） |
| `assets/shaders/gpu_physics_{integrate,collide}.comp.glsl` | 新增为唯一真理源：完整绑定契约头（PSO 名/SSBO 布局/Uniform 表/C++ 配对结构）；uniform 漂移修复（旧 `.glsl` 的 `u_DeltaTime/u_Stiffness` 死代码清除）；旧 `.glsl` 两文件删除 |
| `engine/src/OpenGL/GL46ShaderSources.g.h` | 生成产物（GENERATED, DO NOT EDIT）：`constexpr string_view kCompute[]` + `Find(name)`；相对路径 md5 注释保证跨机器字节一致、可安全提交 |
| `engine/src/OpenGL/GL46Device.cpp` | include 切换至生成头；`CompileComputeShaders()` 改表驱动（新增 compute 着色器 = 加源文件 + 表加一行） |
| `GL46ComputeShaders.inl` | **删除** |
| `engine/CMakeLists.txt` | `add_custom_command` 生成链接线（DEPENDS 着色器文件 + 生成器脚本自身） |

**工程注意事项**：
- Windows PowerShell 5.1 的 `Set-Content`/`Out-File` 默认 ANSI 编码会静默损坏 UTF-8 中文注释 —— 编辑含中文字符的着色器请使用编辑器工具或显式 `UTF8Encoding`
- GLSL 编译发生在运行时（设备初始化），语法错误的故障表现为设备 stub 回退而非构建失败；构建期 fail-fast 覆盖生成阶段问题

---

## Phase 2 — 固定步长 + Ping-Pong 双缓冲（v3.3 工程增量版）

> **重定版说明（2026-08-23）**：本章节撤回 v3.0 初版的 `SimulationSnapshot / ISimulationDomain` 全引擎抽象设计。
> Phase 2 的正确定位是 **GPU Physics 自身的工程演进**，而非引入新的计算哲学。契约范围收敛为
> GPUPhysicsEngine 的局部接口与生命周期约束；"任何架构必须由本项目实际约束证明其必要性"
> （Session 纠偏结论）。原抽象设计若未来多仿真域真实出现互依需求时可再评估，当前不做。

### 四个子阶段

```text
Phase 2
├── 2A Fixed Timestep      — 接入既有 FixedTimestepAccumulator（帧率解耦 + 螺旋保护）
├── 2B Ping-Pong Buffer    — src/dst 分离双缓冲（消除 compute 读 写竞争）
├── 2C Multi-step Gate     — Ring4/Ring5 测试门（swap 完整性/确定性/螺旋钳制/帧分组等价）
└── 2D Async Readback/Fence — 独立子阶段（见 Phase 3 收敛说明），不上升为引擎级抽象
```

### 局部契约（仅此模块）

```text
生命周期 : Initialize → (Reset / Update* / Render*) → Shutdown
状态     : GetStateBuffer() 恒指向最近完成步的稳定快照（消费者只读）
时间     : Update(frameDt) → Accumulator → 0..maxSubSteps 个固定子步
步语义   : 子步 = Integrate(cur→nxt) + Collide(nxt'→cur)，每步后稳定槽位不变
求解     : collide 由原位更新（Gauss-Seidel）改为快照求解（Jacobi）⇒ 与线程调度顺序无关
```

### 改造清单（已实施）

| # | 任务 | 文件 |
|---|------|------|
| 2A.1 | `GPUPhysicsConfig` 增加 `fixedDt / maxSubSteps`；成员接入 `FixedTimestepAccumulator` | `GPUParticle.h` / `GPUPhysicsEngine.cpp` |
| 2A.2 | `Update(frameDt)` 改为累加器驱动 0..N 子步；暴露 `GetLastFrameSubsteps/GetAlpha/GetSimulationTime` | 同上 |
| 2B.1 | 单 SSBO → 双缓冲（VRAM +8MB@64K）；`CreateParticleBuffers()`；`GetStateBuffer/GetScratchBuffer` | `GPUPhysicsEngine.cpp` |
| 2B.2 | GL46CommandList 多 SSBO 绑定支持（1 → 4 槽） | `GL46CommandList.cpp` |
| 2B.3 | 着色器切双绑定（binding0=readonly src, binding1=dst）+ collide 无条件写回 | `assets/shaders/*.comp.glsl`（单一真理源直接生效） |
| 2C.1 | Ring4_PingPongSwapIntegrity：7 步槽位稳定断言 + 物理健全性 | `GPUPhysicsRingTest.cpp` |
| 2C.2 | Ring5_Determinism_BitExact：同序列两次运行 FNV 哈希逐位一致 | 同上 |
| 2C.3 | Ring5_SpiralGuard_ClampsSubSteps：dt=1s 钳制至 maxSubSteps | 同上 |
| 2C.4 | Ring5_FrameGrouping_Equivalence：30×(1/30) ≡ 60×(1/60) bit-exact | 同上 |

### ✅ 实施完成记录（2026-08-23）

**测试**：13/13 PASS（Ring4/Ring5×3 新增全过；确定性哈希 `a4012e32...` 与帧分组哈希 `d215e71e...` 各自逐位一致）。

**性能回归对照 Phase 0 基线**（Release / RTX 3070 Laptop，`docs/benchmarks/gpu-physics-phase2-pingpong.csv`）：

| 指标 | 基线 | Ping-Pong 后 | 变化 | 归因 |
|------|------|--------------|------|------|
| 64K collide | 110.7 ms | 118.2 ms | +7% | src/dst 分离增加 ~50% SSBO 流量；O(N²) 计算密集型吸收大部分 |
| 64K throughput | 118.2 ms | 121.2 ms | +2.5% | — |
| 4K collide | 3.13 ms | 4.12 ms | +32% | 小层级带宽敏感；绝对值仍 <5ms |

结论：+7%@64K 是消除读 写竞争、获得 Jacobi 确定性语义的合理代价，且为 Phase 4 空间哈希的前置正确性基础。

## Phase 3 — 异步回读 / Fence（v3.3 重定版：按需触发的局部子阶段）

> **重定版说明（2026-08-23）**：撤回"引擎级 Execution Completion Token 抽象"的优先级定位。
> Fence 不应为架构完整性而提前建设。当前状态评估：
> - `ReadbackParticles` 的同步路径（内含 glFinish）目前仅被 **测试与 Benchmark** 使用，
>   不在帧循环热路径上 —— 无真实停顿受害者
> - 碰撞事件查询（gameplay 消费场景）尚不存在消费方
>
> **触发条件（满足其一才启动本阶段）**：
> 1. GPU 物理进入实时渲染循环且每帧需要回读
> 2. 出现第一个真实的 gameplay 碰撞事件查询需求
>
> 届时实施范围收敛为：GPUPhysicsEngine 局部三缓冲回读环（glFenceSync 轮询语义，
> 永不阻塞消费），必要时才将 Fence 句柄提升至 IRHIDevice 表面。
> 原 GpuReadbackRing\<T\> 通用化设计保留为参考形态。

### 语义红线（保留不变）

| 规则 | 强制手段 |
|------|---------|
| Gameplay 禁止同帧等待 | 回读 API 不提供阻塞 Consume |
| 状态/事件查询允许 1~2 帧延迟 | 三槽环结构性保证 |
| 零延迟因果场景留在 GPU 端 | 不做 CPU 回读 |

---

## Phase 4 — Uniform Spatial Hash 碰撞内核

> **状态（2026-08-23）：4A-4F 全部实施完成，第一版实测为【条件性负结果】—— 如实记录，不做虚假宣告。**
> 严格限定为最小空间加速结构验证，未引入 BVH/radix-sort/通用框架（Scope 守住）。

### 实施（4B-4D）

计数排序管线（仅依赖 atomicAdd —— atomicExch 存在跨编译器重载解析问题）：

```text
H1 ClearCounts → H2 Count(atomicAdd) → H3 Single-Workgroup PrefixScan
→ H4 Scatter → H5 Collide27Cell(Jacobi)
```

- 新增内核 5 个（单一真理源 .comp.glsl，表驱动注册）
- `GPUCollisionBackend { BruteForce, SpatialHash }` 双路径，默认 BruteForce（既有行为零变化）
- 网格自适应：cellSize 从 2.0 起、总量超 65536 时自动放大（单工作组扫描约束）

### 4E 正确性门禁（全部通过）

| 门禁 | 结果 |
|------|------|
| Ring7_TwoParticleBitExact（金标准：单接触对无顺序歧义） | ✅ PASS |
| Ring7_OneStepEquivalence（256 粒子密集布局） | ✅ PASS |
| ├─ 候选修正计数 diff | **fixDiff=0** |
| ├─ brute vs hash 终态 | **bit-exact**（maxDp=0） |
| ├─ vs 独立 CPU 真-Jacobi 实现 | **bit-exact**（双路径均 0.0000） |
| └─ mass-COM shift | 3.7e-09 |
| Ring7_SelfDeterministic（30 步自漂移） | ✅ PASS |

实施中发现并修复的真 bug：初版内核距离计算基于演化中的自身位置（实为 Gauss-Seidel），导致两路径顺序敏感发散 ~2.6 units —— 修复为基于快照位置的真 Jacobi 后归零。

### 4F 性能实测（Release / RTX 3070 Laptop / 自由沉降稳态）

| 粒子数 | BruteForce | SpatialHash | 结论 |
|--------|-----------|-------------|------|
| 4,096 | 3.69 ms | **2.59 ms** | hash 快 1.4× |
| 8,192 | 7.55 ms | 16.37 ms | hash 慢 2.2× |
| 16,384 | 15.49 ms | 31.78 ms | 慢 2.1× |
| 32,768 | 32.55 ms | 163.43 ms | 慢 5.0× |
| 65,536 | 120.47 ms | ~685 ms（guard 触发） | 慢 5.7× |

**归因分析**：brute 内核的全量顺序遍历对 GPU 缓存极其友好（j 连续递增）；spatial 的 27-cell 遍历产生 `sortedIdx[k] → particles[j]` 双重随机访存，在自由沉降形成的致密平板堆积下（局部 cell 占用数十粒子），内存延迟完全吞掉了 O(N²)→O(N·k) 的算术收益。小规模/稀疏场景 hash 占优，与该模型一致。

**决策（依据"由实测决定下一步"约定）**：
1. **默认后端维持 BruteForce**（spatial 保留为双路径，正确性已被 Ring7 锁定）
2. 空间哈希优化的**前置条件**已明确：需消除双重随机访存（如 sort-粒子数据本身而非仅索引、或共享内存 tile 缓存）—— 属局部内核优化，待有真实需求时再做
3. GPU Physics 能力链至此闭环（MVP→timestep→Ping-Pong→确定性→scalability 实证→benchmark 设施），按路线图转入 **Scripting 产品化推进**（独立主线）

| Benchmark | 未前置 | Phase 0 前置，全程对照 | 先测量后优化 |
| 工时 | ~17 人天 | ~12 人天 | 砍 RG 集成 + GPU 累加器 |

---

## 十一、风险登记表

| 风险 | 等级 | 缓解措施 | 触发条件 |
|------|------|---------|---------|
| R1 过度抽象复发（SimulationGraph 化） | 高 | SG1 硬性排除；ISimulationDomain 仅 3 个方法，超出即评审 | 第二个 Domain 出现前 |
| R2 回读停顿回归 | 中 | SG3 API 层面杜绝阻塞；Ring6 帧时间方差监控 | 任何人往 consume 路径加 wait |
| R3 显存翻倍 | 低 | Ping-pong 仅 8MB@64K；Phase 4 grid buffer +3MB | 百万级粒子时复核 |
| R4 确定性破坏（多 substep + 双缓冲引入时序依赖） | 中 | Ring5 bit-exact 测试为合入门禁 | 任何 kernel 改动 |
| R5 构建期 shader 生成的工具链脆弱（Python 依赖） | 低 | 脚本零第三方依赖；生成失败即构建失败（fail-fast），不静默回退旧 .inl | CI 环境 |

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v3.23 | 2026-08-24 | **DF09 Multi-Scene / Reference Integrity 完成**：八道门禁 R1-R8 一次通过（共享资产 GUID 稳定 / Unregister+RegisterExplicit 重命名原语且场景文件字节级不变 / 持久层零路径泄漏 / 双场景独立 Save / 跨进程恢复 / 失败局部化+warning 点名实体 / 三方身份一致 / rename 后无 GUID 漂移）；DL-02 确认为 Editor 实现细节未随规模爆炸；M5 维持不启动，下一步为 Vertical Slice；全项目 **96/96** + Gate I1(21)+I2+I3 全绿 |
| v3.22 | 2026-08-24 | **DF08 Long-session DX 完成**：Golden Scenario 全序列 headless 化（Clean Start→Import→分配→Play→脚本热改+Reload→场景中途变更→Capture/Save→冷启动→Load→二次修改→幂等回写）；核心数据丢失探针 S2/X3 钉死 CaptureScene 活场景契约；发现 DL-02（mid-session 新增实体 binding 依赖编辑器维护表，Editor Workflow 观察项）；Undo/Redo 弱信号入账不启动；全项目 **94/94** + Gate I1(20)+I2+I3(DF01-08) 全绿 |
| v3.21 | 2026-08-24 | **Evidence Integrity Gate 建立（I1/I2/I3 全绿）**：审计发现 7 项证据链缺陷并全部整改——M4 DXGoldenGateTest 从未编译（冻结声明不可复现，已修复后首次真实 PASS）、DF06 无测试且其 Lua 脚本从未被执行过、test_e2e/test_job 对漂移 JobSystem API 编写等；新建 tools/integrity_gate.ps1 常驻门禁 + docs/Evidence-Baseline.md 权威基线；权威计数 **91/91**（新增 test_job 4 + test_e2e 6），test_core/test_ecs 显式排除登记 |
| v3.20 | 2026-08-24 | **Dogfood-07 Tower Defense Lite 完成（可维护性维度）**：10 实体 + 11 个脚本调参常量 + 手动 spawnTimer；三类成本（重复代码/数据表达/事件查询）均有信号但未达 P0 阈值，全部 deferred 维持；新增 DX09 Timer/Scheduler 观察；**修复测试基建缺陷**——Dogfood05Test.cpp 此前从未编译，现 DF05+DF07 独立测试入列，全项目 **75/75 PASS** |
| v3.19 | 2026-08-23 | **Dogfood-06 Production Efficiency Test 完成**：15 实体多类型敌人（Grunt/Tank/Scout/Boss/Minion）+ 近战攻击系统 + HP 机制 + 击退 + 攻击冷却；全项目 **73/73 PASS**。验证了冻结 API 在复杂战斗场景下的稳定性；未暴露新的 P0 缺口 |
| v3.17 | 2026-08-23 | **M4 Developer Experience v1 FREEZE**：DX Golden Gate 五道门禁全过（空场景/导入驱动/Save+Reload 行为变更/错误隔离/跨进程恢复）；ScriptSandbox 升级为编辑器宿主（Hierarchy+Inspector+AssetBrowser+Console+HUD overlay）；正式声明 M006/M004/M002/M003 deferred |
| v3.16 | 2026-08-23 | **M4 DX 部分完成 + ScriptSandbox pimpl 重构启动**：M005/M001 已实现并测试通过；ScriptSandbox 升级为编辑器宿主；nlohmann↔spdlog 宏冲突导致构建阻断，pimpl 隔离方案已启动待完成；正式确立 "No Capability Without Evidence" 原则 |
| v3.11 | 2026-08-23 | **Dogfood-01 完成：Content-Only 小游戏 + Missing Capability Ledger**。D1-D6 全过（零 C++ 改动/场景数据实体/冻结 API 玩法/清单 GUID/跨进程可玩/Victory 目标）。产出 Ledger M001-M006 六条真实缺口（P0=UI文本+Entity查询，P1=CWD+Input.pressed），确认 Scripting v1 核心循环已完备。附带发现并修复内容侧 GUID 路径重复问题 |
| v3.10 | 2026-08-23 | **Editor Workflow v1 完成 + Restart-Play GOLDEN GATE PASS（真·跨进程）+ Editor Workflow v1 FREEZE**：ScriptSandbox 升级为编辑器宿主（Hierarchy/Inspector/Save/Load/Console 四面板）；HandleAdopt 领养机制；test_content 13/13；全项目四套件 61 测试全绿 |
| v3.9 | 2026-08-23 | **Content Pipeline 垂直切片完成（Ring8-12+R14-lite，11/11 PASS）**：ContentRegistry（GUID 身份/幂等导入/冲突拒绝/清单持久化）+ SceneSerializerV1（快照模型/语义等价 round-trip/对抗性失败契约）；附带修复 test_renderer 既有损坏与 ASan DLL 部署问题 |
| v3.8 | 2026-08-23 | **SG6 RenderGraph 质检完成：判定 FAIL as-is**。五问实测（乱序声明数据链静默损坏/拓扑序死代码/47.5× 每帧重建开销/伪指针屏障/瞬态登记未闭环）；六项条件化重构清单 F1-F6 已文档化；RenderGraph 隔离待重构，生产渲染维持直连路径；附带修复 test_renderer 既有编译损坏（GL46DeviceTest） |
| v3.7 | 2026-08-23 | **Sandbox Demo 完成 + Scripting v1 API FREEZE**：ScriptSandbox.exe（GLFW 单向输入适配器 / WASD 驱动渲染可见 / F5 热重载 / ImGui Console）；G8 真实资产冒烟门禁；test_scripting 23/23 + physics 回归 16/16；v1 契约面/拒绝面/保证/版本规则正式声明 |
| v3.6 | 2026-08-23 | **Scripting Gameplay Vertical Slice 完成（API Contract v2）**：分域布局（log/time/input/entity/transform）+ 句柄制实体桥接 + Console Execute 原语；全链路 G1-G7 门禁 22/22 PASS（含输入驱动位移、Reload×句柄存活、错误隔离）；发现并修复 Reload 域缺失/fixture 泄漏两个真问题 |
| v3.5 | 2026-08-23 | **GPU Physics 冻结（v1.x FROZEN）+ Scripting 主线 S1-S5 MVP 完成**：Lua 5.4 vendored；ScriptInstance 生命周期/持久状态/热重载；Engine.* API 边界；指令预算 + 沙箱隔离（test_scripting 15/15，physics 回归 16/16）。RenderGraph SG6 待办（最小真实渲染验证，不扩张） |
| v3.4 | 2026-08-23 | **Phase 4 实施完成（4A-4F）**：Uniform Grid 计数排序管线（atomicAdd-only）+ Ring7 等价门禁（双路径 bit-exact、COM 3.7e-09）；发现并修复 Gauss-Seidel→真 Jacobi 语义偏差；性能实测为条件性负结果（16K+ 慢于 brute，随机访存主导），默认后端维持 BruteForce，优化前置条件已明确记录 |
| v3.3 | 2026-08-23 | **Phase 2 实施完成 + Session 纠偏落地**：撤回 SimulationDomain/CompletionToken 抽象优先级；Phase 2 收敛为工程增量（固定步长接入 / Ping-Pong 双缓冲 / Ring4-Ring5 多步验证门，13/13 PASS）；Fence 降级为按需触发的局部子阶段；新增 SG6（RenderGraph 走真实渲染路径验证）/ SG7（空间哈希由基线数据决策） |
| v3.2 | 2026-08-23 | **Phase 1 实施完成**：Shader 单一真理源落地（纯 CMake 生成器 + `.comp.glsl` 契约头 + 表驱动注册）；`.inl` 双轨制废除；uniform 漂移修复；传播实验实证链路贯通 |
| v3.1 | 2026-08-23 | **Phase 0 实施完成**：真实 GPU 基线确立（64K collide=110.7ms，加速比潜力 ~100x）；发现 GL46Queue::WaitIdle 空实现缺陷；Update 拆分为 RecordIntegrate/RecordCollide |
| v3.0 | 2026-08-23 | 基于 v2.5 外部评审重构路线：Benchmark 前置、Shader 单一真理源、Simulation Domain 契约、CompletionToken、D1 缺陷修正；取代 v2.5 Phase A-D |
| v2.5 | 2026-07-18 | 初版架构升级方案（其中 GPU 累加器与 RenderGraph 集成两项被 v3.0 修订） |

---

## 🔒 Productization Milestone 1 — COMPLETE / FROZEN

> **日期**: 2026-08-23
> **声明**: 本轮架构扩张正式收尾，进入稳定积累期。

### 冻结清单

| 子系统 | 状态 | 契约版本 |
|--------|------|---------|
| GPU Physics | 🔒 FROZEN | v1.x |
| Scripting | 🔒 FROZEN | API v2.1 |
| Content Pipeline | 🔒 FROZEN | SceneSerializerV1 + ContentRegistry v1 |
| Editor Workflow | 🔒 FROZEN | v1 |
| Dogfood-01 | ✅ PASS | Content-Only 小游戏 D1-D6 全过 |
| RenderGraph | ❌ FAIL as-is / ISOLATED | 重启门槛 F1-F6 |

### 最终测试状态

```text
test_scripting   23/23
test_physics     16/16
test_content     13/13
test_renderer     9/9
─────────────────────
TOTAL           61/61
```

### Deferred Backlog（存在即正确工程状态，不产生清空冲动）

| ID | 能力 | 优先级 | 重启触发条件 |
|----|------|--------|-------------|
| M006 | CWD 锚定 | P1 (maintenance) | 独立基础设施修复，不影响 Gameplay API |
| M004 | Input.pressed() | P1 | 出现 `if Engine.input.pressed("Space")` 的真实 gameplay 需求 |
| M002 | 组件数据序列化 | P1 | 实体需要携带半径/颜色等非 Transform 数据时 |
| M003 | Prefab | P3 | 同类实体 >10 时 |
| SG6-F1~F6 | RenderGraph 重构 | — | 真实渲染路径出现复杂 pass dependency 时 |

### 下一步启动原则

不从"工业引擎还应该有什么"开始。从**"我要做什么游戏？它实际卡在哪里？"**开始。
让 Ledger 条目自然从真实游戏中产生——如果三周后不需要 Prefab，就不做。

---

## 附：Session 纠偏原则（2026-08-23 起）

> **任何架构都必须由本项目的实际约束证明其必要性。**
>
> - 不因"Unreal 有"而采纳，也不因"太像 Unreal"而拒绝
> - 工业化 ≠ 模仿具体实现；RHI/RenderGraph/JobSystem 等组件可以有，边界与组合方式由本项目问题定义
> - AI 辅助开发的两种漂移同样危险：把行业惯例当必然性（做成 Unreal 复制品），以及为避免雷同而制造新抽象（过度架构）
> - 检验标准始终是：测试、benchmark、真实使用场景、明确的模块边界

---

## 附：GPU Physics 冻结声明（v3.5 · 2026-08-23）

**GPU Physics v1.x 状态：FUNCTIONAL → VALIDATED → BASELINED → `FROZEN`**

```text
GPU Physics v1.x
├── MVP / RHI / GL46 后端
├── Shader 单一真理源
├── 固定步长 + Ping-Pong + 真 Jacobi
├── bit-exact 确定性（Ring5/Ring7 锚定）
├── Spatial Hash（实验路径，非默认）+ BruteForce（默认）
├── CPU 参考实现（仲裁基准）
└── Benchmark 设施 + 分段基线 CSV
```

**冻结范围**：不再开发 Spatial Hash v2 / broad-phase 优化 / 异步回读 / 更复杂求解器。

**重启触发条件**（满足其一）：新 workload 实际出现；profiling 证明随机访存成为产品瓶颈；真实产品场景需要。

**主线路径移交：Scripting 产品化**（S1-S5 MVP 已于同日实施完成，见修订历史 v3.5）。

---

## Scripting 主线 — S1-S5 MVP（2026-08-23 实施）

> 范围收敛：只回答一个问题——**一个游戏脚本如何安全、稳定、高效地调用 Engine API？**
> 明确不做：多语言运行时 / 反射系统 / GC 桥 / 行为树 / 可视化脚本 / 全局执行抽象。

| # | 目标 | 实现 | 验收测试 |
|---|------|------|---------|
| S1 | 唯一运行时 = Lua 5.4（vendored `third_party/lua/src`，MIT） | `LuaEngine::Init/RunFile/RunString/CallFunction*` | S1_Runtime_Basics |
| S2 | 稳定 API 边界：全局表 `Engine.*`（log/log_warn/log_error/time_now/random/api_version），脚本禁止直触 C++ internals | `ScriptAPI::RegisterAll` | S2_EngineAPIBoundary |
| S3 | 生命周期契约：OnCreate → OnUpdate(dt)* → OnFixedUpdate(fixedDt)* → OnDestroy；未定义回调安全空操作 | `ScriptInstance` | S3_Lifecycle_CallbackOrderAndCounts |
| S4 | 状态语义：Reload 时 `_PERSIST` 表保留、其余全局干净重建（先恢复持久表再执行新代码，使 `_PERSIST = _PERSIST or {}` 兼容模式自然成立） | `ScriptInstance::Reload` + `LuaEngine::ResetGlobalState` | S4_Reload_PreservesPersistState + S4b_CleanWipe |
| S5 | 错误隔离：语法错误优雅失败 / 运行时错误 pcall 捕获且实例存活 / 死循环由指令预算中止（实测 ~13ms）/ 沙箱剥离 os.execute·io·require·loadfile·dofile | budget hook + ApplySandbox | S5a/S5b/S5c(7.96ms)/S5d |

**验收结果**：test_scripting **15/15 PASS**；physics 全量回归 16/16 PASS（EngineCore 变更零破坏）。

### 实施中发现并修复的问题（AI 辅助工程样本）

1. **文档契约 ≠ 代码语义（Phase 4 同类问题在脚本侧复现）**：S4 初版实现为"全局环境跨 Reload 延续"，与契约"瞬态干净重建"不符——通过测试断言暴露后修正为 ResetGlobalState + 先恢复持久表再执行新代码的正确顺序。
2. **C++ 求值顺序陷阱**：`now - ClockOrigin()` 中 ClockOrigin 的 static 初始化晚于 now 采样 → time_now 返回负值。修复为显式先取基点。
3. **lua_next 遍历中 luaL_ref 弹栈破坏迭代状态** → SEH 崩溃。修复为 pushvalue 复制 key 后 ref。
4. fmt v12 constexpr 格式串约束 / LuaError 符号歧义（struct vs C 函数）等编译期问题。

---

## Scripting 主线 — Gameplay Vertical Slice（2026-08-23 实施 · API Contract v2）

> 范围：验证一条完整链路 `Input → Script → Entity → Transform`，证明引擎可被真实游戏逻辑驱动。
> 决策遵守：手写 binding（不做反射/生成器）；分域命名空间在无债期建立；`_PERSIST` 语义冻结不扩展。

### API Contract v2（分域布局）

```lua
Engine.api_version            -- 2.0（破坏性变更须递增）
Engine.log.info/warn/error    -- 基础域（v1 平面命名已废弃）
Engine.time.now()             -- 秒（单调）
Engine.random(lo, hi)
Engine.input.is_down("W")     -- 后端经 IScriptInputProvider 注入（测试 mock / 沙盒 GLFW）
Engine.entity.spawn(name) -> handle ; destroy(handle)
Engine.transform.get_position(h) -> x,y,z ; set_position(h,x,y,z) ; translate(h,dx,dy,dz)
```

### 新增组件

| 组件 | 职责 |
|------|------|
| `GameplayAPI.h/.cpp` | 输入抽象接口 + 场景句柄桥接（handle→GameObject 映射，脚本零指针暴露）+ 分域注册 |
| `ScriptInstance::Execute(code)` | Console 能力原语：运行期任意语句执行，错误经 GetLastError 取回 |

### 验收结果（test_scripting 22/22 PASS）

| 门禁 | 结果 |
|------|------|
| G1 分域注册与 v2 契约 | ✅ |
| G2 输入注入反射（mock 状态 ↔ 脚本查询） | ✅ |
| G3 实体生命周期 + 句柄失效安全 | ✅ |
| G4 平移累积精度 | ✅ |
| **G5 全链路**：Lua Player 读输入驱动实体位移/停止 | ✅ |
| **G6 Reload × Gameplay**：_PERSIST 句柄跨热重载继续驱动 | ✅ |
| G7 Console：Execute 直驱实体 + 错误捕获实例存活 | ✅ |
| physics 全量回归 | ✅ 16/16 |

### 实施中发现并修复的问题

1. **Reload 后 gameplay 三域缺失**：ResetGlobalState 清空了 Engine 表而 Reload 仅重建基础域 → `Engine.input` 为 nil（由 G6 的 pcall 错误捕获定位）。修复：Reload 流程补齐 RegisterDomains。
2. **测试夹具误用**：Harness 写成普通 struct 导致 TearDown 从未执行、全局句柄计数器跨测试泄漏 —— 重构为标准 gtest fixture（TEST_F）。
3. **调试引用错 VM**：fixture 的独立 eng 与 ScriptInstance 内部 VM 是两个世界 —— 排查脚本问题必须引用 `inst.GetEngine()`。

### 下一步（Scripting 主线内）

1. **Scripted Sandbox Demo**：带窗口的最小场景（Player+Cube+Camera），GLFW 输入适配器实现 IScriptInputProvider，ImGui Console 面板挂接 Execute() —— 完成"人能不能舒服地使用它"的产品化门槛
2. Editor Console 正式集成
3. Scripting v1 API 冻结（按 v3.5 冻结原则执行）

---

## Scripting 主线 — Sandbox Demo + v1 FREEZE（2026-08-23 实施 · v3.7）

### Sandbox Demo（`ScriptSandbox.exe`，验收场而非新功能开发）

三条产品化链路全部成立：

| 链路 | 路径 | 验证 |
|------|------|------|
| 输入驱动渲染 | GLFW → `GLFWScriptInputProvider`(单向适配) → Engine.input.* → sandbox_player.lua → entity/transform → GameObject → SpriteBatch 渲染 | WASD 移动绿色 Player（蓝色 Cube 静态参照） |
| 热重载 | F5 → Reload() → _PERSIST.h 存活 → Player 继续响应 | G6 门禁 + 运行时 F5 |
| Console | ImGui InputText → Execute() → Engine.* ；错误/结果进滚动区 | G7 门禁 + 运行时面板 |

场景范围红线守住：仅 Player + Cube；无 Physics/Audio/Animation/序列化/ECS。

### G8 运行时冒烟测试（真实资产脚本串联验收）

`assets/scripts/sandbox_player.lua`（Sandbox 同一份文件）无窗口确定性回放：
OnCreate 初始位 → S 键位移 → **Reload** → 同句柄继续位移 → Console Execute 内断言终态。
**test_scripting 23/23 PASS。**

---

## 🔒 Scripting v1 API FREEZE 声明

### Supported（v1 契约面）

```text
Runtime      : Lua 5.4.6（vendored），每实例独立 VM
API Contract : Engine.api_version == 2.0
Domains      :
  Engine.log.{info,warn,error}
  Engine.time.now()
  Engine.random(lo,hi)
  Engine.input.is_down(key)
  Engine.entity.{spawn,destroy}
  Engine.transform.{get_position,set_position,translate}
Lifecycle    : OnCreate → OnUpdate(dt)* → OnFixedUpdate(fixedDt)* → OnDestroy
Reload       : _PERSIST 表保留；其余全局干净重建；回调存在性自动刷新
Isolation    : pcall 全捕获 / 指令预算死循环中止 / 沙箱剥离 os.execute·io·require·loadfile·dofile
Console      : ScriptInstance::Execute(code)，错误经 GetLastError
```

### Not Supported（明确拒绝，重启需面对本契约）

反射系统 / Binding Generator / Variant / 多语言运行时 / 序列化式 _PERSIST /
Console 自动补全与调试器 / ECS 直接暴露 / 自定义 C 模块加载

### Guarantees（稳定性承诺）

| 维度 | 保证 |
|------|------|
| 脚本错误 | 永不崩溃引擎；错误经 GetLastError/ErrorCallback 可观测 |
| 死循环 | 指令预算强制中止（实测 ≤70ms） |
| Reload | _PERSIST 数据存活；句柄有效性由桥接层（VM 外）保证 |
| 确定性 | 同输入序列 bit-exact 可复现（VM 无随机调度依赖） |

### API Versioning Rules

1. 新增域/函数 = 向后兼容 → api_version 小数位递增或维持 2.x
2. 改名/改语义/删除 = 破坏性 → api_version 主版本递增 + 迁移说明
3. 一切变更以 test_scripting 全绿为合入门禁

---

## Content Pipeline 垂直切片（2026-08-23 实施 · Ring8-Ring14-lite）

> 范围红线：仅 Texture+Script 两种资产；GUID 身份（场景不存路径）；无内容寻址/VFS/cooking/反射序列化。
> 加载失败契约（显式）：损坏文件=fail-clean；缺 name 实体=跳过+warning；**Texture/Script GUID 缺失=非致命**，实体照常加载、绑定降级 Null 并记 warning。

### 新增组件

| 组件 | 职责 |
|------|------|
| `Content::ContentRegistry`（复用 ResourceGUID UUIDv4） | Import 幂等 / RegisterExplicit 冲突拒绝（GUID≠路径双唯一）/ Resolve / 清单 JSON 持久化（跨会话身份稳定） |
| `Content::SceneSerializerV1` | 快照模型（Name+Position+SpriteGUID?+ScriptGUID?）↔ JSON；Scene 桥接：Capture（含内容绑定）/ Instantiate（GUID 解析→SpriteComponent 实挂，缺失降级） |

### 验收结果（test_content **11/11 PASS**）

| Ring | 门禁 | 结果 |
|------|------|------|
| R8 | 导入幂等 / 异路径异 GUID / 显式重复 GUID 拒绝 | ✅ |
| R9 | 清单跨会话身份稳定 + 损坏清单 fail-clean | ✅ |
| R10/R11 | 序列化→文件→加载 **语义等价门禁**（实体数/名字/位置/GUID 全比对 + JSON dump 一致） | ✅ |
| R12 | 对抗性：缺失 GUID 非致命降级 / 完整资产全绑定 / 损坏场景 fail-clean / 缺 name 实体跳过 | ✅ |
| R14-lite | 序列化实例化的 Player 由解析路径 ScriptInstance 驱动（OnUpdate 计数验证），序列化位置不被破坏 | ✅ |

**遗留到 Editor 阶段（Ring13）**：Hierarchy/Inspector/Save-Load UI 与 Restart-Play 黄金场景（数据层已就绪）。

---

## Editor Workflow v1 + Restart-Play GOLDEN GATE（2026-08-23 实施 · v3.10 · Ring13）

### 交付（ScriptSandbox 升级为 Editor Workflow v1 宿主）

| Gate | 能力 | 实现 |
|------|------|------|
| R13a | Hierarchy 创建/选择/删除 | Create/Delete 按钮 + Selectable 列表（句柄反查显示） |
| R13b | Inspector 修改 Transform | DragFloat3 实时回写 HandleSetPosition |
| R13c | Inspector 分配资产 GUID | 按路径 Import（幂等）→ 绑定 sprite/script GUID |
| R13d | Save 走 SceneSerializerV1 | CaptureScene(按对象序对齐绑定) → 快照+清单双落盘 |
| R13e | Load 语义等价 | LoadSnapshot → InstantiateScene → HandleAdopt 领养进句柄体系 |
| R13f | Console 复用 Execute | 无第二套 Lua Runtime |
| **Golden** | **Restart → Load → Play（真·跨进程）** | **PASS：z 0.00 → -1.00** |

启动时自动恢复已保存场景（`sandbox_scene.json` 存在即加载），Player 脚本 `_PERSIST.h` 由引擎重绑至新句柄 —— WASD 在重启后继续驱动同一实体。

### Golden Gate 测试形态

真·跨进程（非内存共享）：进程 A（`--gtest_filter=*ProcessA*`）保存后退出；进程 B（新 exe 进程，仅依赖磁盘文件）从零恢复注册表与场景、实例化脚本、驱动位移、Console 查询。test_content **13/13 PASS**。

---

## 🔒 Editor Workflow v1 FREEZE 声明

**Supported**：Hierarchy（创建/选择/删除）、Inspector（Transform 手写字段 / Sprite+Script GUID 按路径分配）、Scene Save/Load（SceneSerializerV1 + 清单双文件）、Console（Execute 复用）、启动自动恢复。

**Not Supported**（延期需面对本契约）：多选/拖拽层级/搜索、Undo/Redo、Prefab、Asset Browser、Project Manager、属性系统/反射、Serialization v2、RenderGraph 接线。

**产品化里程碑达成**：`Asset → Registry → Scene → Editor → Save → Restart → Load → Script → Input → Runtime → Render` 全链路成立——内容生产者首次可以不写 C++ 就创建并持久化一个可玩场景。

---

## 🔬 SG6 — RenderGraph 一次性质检报告（2026-08-23 · 判定：FAIL as-is）

> 质检对象：`Rendering::RenderGraph`（716 行）+ `TransientHeap`。
> 方法：真实 GL46 设备 + 真 SSBO + 三段计算内核链（Generate→Transform→Consume），
> 乱序注册组 / 顺序注册组对照 + 开销计时。
> 测试：`tests/test_renderer/RenderGraphSG6Test.cpp`（永久回归保留）。

### 五问答案

| # | 问题 | 实测答案 |
|---|------|---------|
| Q1 | API 降低复杂度？ | **中性偏正**。AddPass/setup/execute 三段式干净；但 pass 体仍需 raw GL 绑定内核与 SSBO（IRHICommandList 缺 compute 多绑定便捷层），与直连实现差距不大 |
| Q2 | 依赖推导正确？ | **❌ 正确性级缺陷**。`Execute` 按【注册序】迭代 passes，`TopologicalSort()` 计算出的 `topologicalOrder` **从未被执行路径使用**；且 `DeriveDependencies` 同样按注册序扫描——消费方先注册时连依赖都无法推导。实测：乱序注册下整条数据链**静默产出全零**（编译"成功"、无任何告警）|
| Q3 | 资源生命周期可靠？ | **部分**。firstUse/lastUse 计算正确；瞬态堆可登记请求且具备真实 device.CreateBuffer 能力；但 **Execute 从未消费堆分配产物**——登记簿未闭环到实际资源绑定 |
| Q4 | ExecuteParallel 有意义？ | **当前阶段不启用**（如实记录）。GL46 单上下文即时模式下按 layer 切分的多命令列表只能串行提交，无并行收益；ScenePass 分片录制路径未被任何生产者使用 |
| Q5 | 性能可接受？ | **❌ 不接受**。单 pass 微负载 500 帧：直连 6.5ms vs RenderGraph 309.5ms（**47.5×，0.606 ms/帧**）。构成：每帧 lambda/vector/unordered_map 构造 + Compile 全流程 + Execute 新建 CommandList + 热路径 spdlog Info |

### 附加发现

- **屏障描述符为伪指针**：`GenerateBarriers` 将资源名哈希 `reinterpret_cast` 为 IRHIBuffer*/IRHITexture* —— GL46 后端恰好只看 type 字段退化为全量 MemoryBarrier 而"碰巧工作"，状态迁移机制在该后端纯属装饰
- DumpGraph 功能正常（DOT 输出含全部 Pass 名）

### 判定：**FAIL as-is**（依据预设标准：正确性缺陷 + 不合理开销）

**处置（不是删除，是条件化重构清单）—— 重启触发即本清单完成并通过同套测试**：

| # | 修复项 | 说明 |
|---|--------|------|
| F1 | Execute/ExecuteParallel 按 topologicalOrder 迭代 | 正确性前提 |
| F2 | DeriveDependencies 支持乱序声明（两遍扫描：先收集写者再推导读依赖） | API 不再隐含"必须按序注册" |
| F3 | 图构建与提交分离：支持 Build-once + per-frame Execute 复用（消除每帧 lambda/map 重建） | 性能主因 |
| F4 | 热路径去日志 / CommandList 复用 | 性能次因 |
| F5 | 屏障机制二选一：GL46 下显式降级为 UAV-only 并删除状态机伪装；或接通真实资源句柄 | 诚实性 |
| F6 | 瞬态堆分配闭环：Execute 侧提供 GetAllocation→真实绑定的通路，或裁剪掉该子系统 | 诚实性 |

**当前架构决策**：RenderGraph 保持【隔离待重构】状态 —— 不接入 ScriptSandbox/SceneRenderer 生产路径，不删除代码；生产渲染继续走直连 SpriteBatch 路径（其正确性与性能已被 Sandbox/G 套件验证）。
