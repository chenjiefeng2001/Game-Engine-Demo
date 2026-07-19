# GPU 物理引擎 v2.5 架构升级方案

> **日期**: 2026-07-18
> **版本**: v1.0
> **依据**: 基于 v1.x MVP 的实际代码审阅（GPUPhysicsEngine / GL46CommandList / GPUParticle / RenderGraph）和 Ring2 测试验证结果
> **前置文档**: docs/Next-Phase-Implementation-Plan.md (阶段 0.3)

---

## 目录

1. [当前架构全景分析](#一当前架构全景分析)
2. [v2.5 架构升级方案](#二v25-架构升级方案)
3. [分阶段实施路线图](#三分阶段实施路线图)
4. [项目日程甘特图](#四项目日程甘特图)
5. [与现有代码的衔接策略](#五与现有代码的衔接策略)
6. [附录：核心代码伪代码清单](#六附录核心代码伪代码清单)

---

## 一、当前架构全景分析

### 1.1 当前架构图

`
                CPU Side                          GPU Side
        ┌─────────────────────┐    ┌──────────────────────────┐
        │ GPUPhysicsEngine    │    │  GL46CommandList          │
        │   ::Update(float dt)│───▶│   UseProgram + glUniform* │
        │   ::Render(cmdList) │    │   DispatchCompute         │
        └─────────────────────┘    └──────────┬───────────────┘
                 │                             │
                 ▼                             ▼
        ┌─────────────────────┐    ┌──────────────────────────┐
        │ 1x SSBO (Particle)  │    │ Integrate CS (Pass 1)    │
        │ 64B per particle    │    │ Collide CS (Pass 2)      │
        │ GPU_Only memory     │    │ (暴力 O(N²/k) 共享内存)   │
        └─────────────────────┘    └──────────────────────────┘
`

### 1.2 现存架构瓶颈

| # | 瓶颈 | 当前状态 | 影响 |
|---|------|---------|------|
| **C1** | **无固定步长** | 帧率波动，物理抖动 | 帧率波动→物理抖动 |
| **C2** | **单缓冲读写冲突** | 1 个 SSBO as input/output | 工作组间数据竞争 |
| **C3** | **暴力碰撞 O(N/k)** | 工作组共享内存+全局遍历 | 65536 粒子 > 8ms |
| **C4** | **CPU 回读 = glFinish** | ReadbackParticles 含 glFinish | 管线 Stall |
| **C5** | **64B 全量双写** | 动静数据一起搬运 | 带宽浪费 50% |
| **C6** | **无 RenderGraph 集成** | 独立 Update() 调用 | 屏障/同步手动管理 |

### 1.3 已验证项（Ring2 测试通过状态）

| 项 | 状态 | 说明 |
|----|------|------|
| glUniform* 路径正确 | PASS | Ring2-A vy=-1.6303 m/s 偏差 0.18% |
| Uniform1ui 用于 uint | PASS | 类型匹配 GL_UNSIGNED_INT |
| SSBO 持久映射 | PASS | Ring1 验证 CPU/GPU 一致性 |
| 5 个 GPUPhysicsRingTest | ALL PASS | Ring0-2 + 诊断 |


## 二、v2.5 架构升级方案

### 总体架构设计

```
                    ┌──────────────── Frame N ─────────────────┐
                    │                                          │
     ┌───────────┐  │  ┌──────────────┐   ┌──────────────┐   │
     │ RenderGraph│──┼─▶│ Physics Pass │──▶│ Collide Pass │───│──▶ Draw
     │   Compile  │  │  │ (Integrate)  │   │ (SpatialHash)│   │
     └───────────┘  │  └──────┬───────┘   └──────┬───────┘   │
                    │         │                   │            │
                    │  ┌──────▼───────────────────▼───────┐   │
                    │  │  Double-Buffered SSBOs            │   │
                    │  │  Ring[0] Write, Ring[1] Read      │   │
                    │  └──────────────────────────────────┘   │
                    │                                          │
                    │  ┌──────────────┐   ┌──────────────┐   │
                    │  │ ReductionPass│   │ EventBuffer  │   │
                    │  │ (Fence async)│   │ (Atomic min) │   │
                    │  └──────┬───────┘   └──────┬───────┘   │
                    │         │                   │            │
                    │         ▼                   ▼            │
                    │  CPU (N+2帧)              CPU (N帧)     │
                    └──────────────────────────────────────────┘
```

### 核心一：GPU 端固定步长累加器

#### 为什么不能放在 CPU？

CPU 循环 Dispatch 多次会导致帧率波动、CPU 堵塞、Dispatch 开销。

#### 解决方案

```cpp
struct GPUPhysicsGlobalState {
    float frameDeltaTime;
    float fixedDt;          // 1/60
    float accumulator;
    uint  maxSubSteps;      // 防止螺旋
    uint  frameIndex;
};
static_assert(sizeof(GPUPhysicsGlobalState) == 32);
```

#### 着色器关键逻辑

```glsl
layout(std430, binding = 2) buffer GlobalStateBuf {
    float u_FrameDeltaTime;
    float u_FixedDt;
    float u_Accumulator;
    uint  u_MaxSubSteps;
    uint  u_FrameIndex;
    float _pad0, _pad1;
} global;

if (gl_GlobalInvocationID.x == 0) {
    global.u_Accumulator += global.u_FrameDeltaTime;
}
barrier();

uint steps = 0;
while (global.u_Accumulator >= global.u_FixedDt && steps < global.u_MaxSubSteps) {
    IntegrateSubstep(particlesIn, particlesOut, global.u_FixedDt);
    global.u_Accumulator -= global.u_FixedDt;
    steps++;
}
```

#### 对比

| 维度 | CPU 循环 | GPU 内部子步 |
|------|---------|-------------|
| 调用次数 | N 次 Dispatch | **1 次** |
| 带宽 | 每 Dispatch 读写 SSBO | 共享内存驻留 |
| 帧率自适应 | 需 CPU 预测 | 累加器自动适配 |

---

### 核心二：AoS-oA 动静分离 + 双缓冲乒乓

#### 数据布局

```cpp
struct ParticleDynamic {
    float position[3];     // 12B
    float radius;          // 4B
    float velocity[3];     // 12B
    uint  _flags;          // 4B
};

struct ParticleStatic {
    float mass;            // 4B
    float color[4];        // 16B
    float _pad[3];         // 12B
};
```

#### 双缓冲翻转

```cpp
struct DoubleBuffer {
    std::shared_ptr<RHI::IRHIBuffer> dynamic[2];
    std::shared_ptr<RHI::IRHIBuffer> staticBuffer;
    uint32_t readIndex = 0, writeIndex = 1;
    void Flip() { std::swap(readIndex, writeIndex); }
};
```


### 核心三：Spatial Hashing 空间哈希碰撞

#### 算法流程

```
Pass B1: CellHash — CellID = Hash(position / cellSize)
Pass B2: 基数排序按 CellID
Pass B3: 构建 cellStart[] / cellEnd[]
Pass B4: 26-Cell 邻域碰撞检测
```

#### 着色器片段

```glsl
layout(std430, binding = 0) buffer HashGrid {
    uint cellStart[];
    uint cellEnd[];
};

// 26-Cell 邻接遍历
uint myCell = sorted[myParticleIndex].cellID;
uint3 cellCoords = DecodeCell(myCell);

for (int oz = -1; oz <= 1; oz++)
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++) {
        uint nCell = EncodeCell(cellCoords + int3(ox, oy, oz));
        for (uint j = cellStart[nCell]; j < cellEnd[nCell]; j++) {
            if (j == myParticleIndex) continue;
            // 窄相碰撞
        }
    }
```

#### 加速比 (N=65536)

| 算法 | 检查次数/线程 |
|------|-------------|
| 暴力 | 16.7M |
| 空间哈希 | 55K |
| **加速比** | **~300x** |

---

### 核心四：PVP 渲染 + Alpha 插值

#### PVP 顶点着色器

```glsl
layout(std430, binding = 0) readonly buffer DynamicBuf {
    ParticleDynamic particles[];
} buf;

uniform float u_Alpha;

void main() {
    ParticleDynamic p = mix(bufOld.particles[gl_InstanceID],
                            bufNew.particles[gl_InstanceID], u_Alpha);
    vec3 worldPos = a_Position * p.radius + p.position;
    gl_Position = u_ViewProj * vec4(worldPos, 1.0);
}
```

#### 插值时序

渲染帧率 != 物理帧率时: alpha = (render_time - last_fixed_time) / fixedDt


### 核心五：Triple-Buffered Readback + Reduction Event

#### 3 帧延迟回读

Frame N: GPU → Buffer_A | Frame N+1: Fence_A, GPU → Buffer_B | Frame N+2: 检查 Fence_A → 读 Buffer_A

#### Reduction Pass

```glsl
if (collisionForce > threshold) {
    uint slot = atomicAdd(eventCount, 1);
    if (slot < capacity) events[slot] = Event(id, force);
}
```

#### CPU Fence 查询

```cpp
void GPUPhysicsEngine::PollCollisionEvents() {
    GLenum result = glClientWaitSync(m_Fence[m_ReadSlot], 0, 0);
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
        ReadbackEventBuffer(&m_PendingEvents);
        glDeleteSync(m_Fence[m_ReadSlot]);
    }
}
```

---

### 核心六：RenderGraph 集成

```cpp
RenderGraph graph;

graph.AddPass("Integrate", [&](auto& b) {
    b.ReadBuffer("ParticleDynamic_In");
    b.WriteBuffer("ParticleDynamic_Out");
}, [&](IRHICommandList& cmd) {
    cmd.SetPipelineState(m_IntegratePSO);
    cmd.SetUnorderedAccess(0, m_Buffer.dynamic[m_Buffer.readIndex].get());
    cmd.SetUnorderedAccess(1, m_Buffer.dynamic[m_Buffer.writeIndex].get());
    cmd.Dispatch(groupCount, 1, 1);
});

graph.AddPass("HashBuild", [&](auto& b) {
    b.ReadBuffer("ParticleDynamic_Out");
    b.WriteBuffer("SpatialHashGrid");
    b.WriteBuffer("SortedPairs");
}, [&](IRHICommandList&) {});

graph.AddPass("Collide", [&](auto& b) {
    b.ReadBuffer("ParticleDynamic_Out");
    b.ReadBuffer("SpatialHashGrid");
    b.ReadBuffer("SortedPairs");
    b.WriteBuffer("ParticleDynamic_Out");
    b.WriteBuffer("CollisionEventBuf");
}, [&](IRHICommandList&) {});

graph.AddPass("Reduction", [&](auto& b) {
    b.ReadBuffer("CollisionEventBuf");
}, [&](IRHICommandList& cmd) {
    if (auto* gl = dynamic_cast<GL46CommandList*>(&cmd))
        gl->InsertFence(m_Fence[m_ReadSlot]);
});

graph.Compile(m_TransientHeap);
graph.ExecuteParallel(m_Device, m_Queue, m_JobSystem);
```

#### 自动屏障推导

| 资源过渡 | 屏障 |
|---------|------|
| Write(DynamicOut)->Read(DynamicOut) | UAV Barrier |
| Write(SpatialHashGrid)->Read(SpatialHashGrid) | UAV Barrier |
| Write(CollisionEventBuf)->CPU | CLIENT_MAPPED_BUFFER_BARRIER_BIT |


---

## 三、分阶段实施路线图

### Phase A: 基础架构重构 (7天)

| Step | 任务 | 工时 | 验收 |
|------|------|------|------|
| A1 | GPU 累加器 | 2d | Ring4: 60Hz + 可变帧率, 抖动<0.01% |
| A2 | 动静分离 | 1.5d | Ring1 内存完整 |
| A3 | 双缓冲 | 1.5d | Ring4: readIndex 交替 |
| A4 | Fence 回读 | 1d | 零 glFinish |
| A5 | Ring4-6 测试 | 1d | 全部 PASS |

### Phase B: 空间哈希 (4天)

| Step | 任务 | 工时 | 验收 |
|------|------|------|------|
| B1 | CellHash | 1.5d | 65536 粒子 < 1ms |
| B2 | 26-Cell 碰撞 | 1.5d | 碰撞 < 3ms |
| B3 | Cell 调优 | 0.5d | 无遗漏 |
| B4 | Ring7 | 0.5d | 冲量差异 < 1% |

### Phase C: PVP 渲染 (4天)

| Step | 任务 | 工时 | 验收 |
|------|------|------|------|
| C1 | PVP 着色器 | 1.5d | 65536 > 60fps |
| C2 | Alpha 插值 | 1d | Ring5 验证 |
| C3 | 验证 | 1d | 无抖动 |
| C4 | 清理 | 0.5d | 零警告 |

### Phase D: Reduction (2天)

| Step | 任务 | 工时 | 验收 |
|------|------|------|------|
| D1 | EventBuffer | 1d | 事件数 == CPU |
| D2 | Fence 延迟 | 0.5d | ≤ 1 帧延迟 |
| D3 | Tracy | 0.5d | 完整 Profiling |

## 四、项目日程甘特图

Week1: A1-A5 | Week2: B1-B4 | Week3: C1-C4 | Week4: D1-D3

Total: ~17 人天

## 五、与现有代码的衔接策略

| 组件 | 改造 | 风险 |
|------|------|------|
| GPUPhysicsEngine | 增加 DoubleBuffer/GlobalState | 低 |
| GL46CommandList | InsertFence 接口 | 低 |
| RenderGraph | Buffer 资源支持 | 低 |
| RingTest | 新增 Ring4-7 | 低 |
| integrate.glsl | GPU 子步循环 | 中 |
| collide.glsl | 重写空间哈希 | 中 |
| render.vert | PVP 模式 | 中 |

### 版本兼容性

```
v1.x              v2.5-physics
Ring0-2 PASS      Ring0-7 ALL PASS
单缓冲            双缓冲乒乓
CPU 循环 dt       GPU 累加器
暴力碰撞          空间哈希 26-Cell
glFinish 回读     Fence 异步回读
VBO 渲染          PVP 零拷贝
Feature flag: GPU_PHYSICS_V2 (#ifdef 双路径共存)
```


---

## 六、附录：核心代码伪代码清单

### A. GPUPhysicsGlobalState

```cpp
struct GPUPhysicsGlobalState {
    float frameDeltaTime;
    float fixedDt;          // 1/60
    float accumulator;
    uint  maxSubSteps;
    uint  frameIndex;
    float _pad0, _pad1;
};
static_assert(sizeof(GPUPhysicsGlobalState) == 32);
```

### B. ParticleDynamic / ParticleStatic

```cpp
struct ParticleDynamic {
    float position[3]; uint _pad0;   // 16B
    float velocity[3]; uint _flags;  // 16B
};
struct ParticleStatic {
    float mass; float color[4];     // 20B
    float _pad[3];                  // 12B
};
static_assert(sizeof(ParticleDynamic) == 32);
static_assert(sizeof(ParticleStatic) == 32);
```

### C. InsertFence

```cpp
class GL46CommandList : public IRHICommandList {
public:
    void InsertFence(GLsync& outFence) {
        outFence = m_GL->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
};
```

### D. 双缓冲翻转

```cpp
void GPUPhysicsEngine::BeginFrame(float dt, IRHICommandList* cmd) {
    m_GlobalState.frameDeltaTime = dt;
    UploadGlobalState(cmd);
    cmd->SetPipelineState(m_IntegratePSO);
    cmd->SetUnorderedAccess(0, m_Buffer.dynamic[m_Buffer.readIndex].get());
    cmd->SetUnorderedAccess(1, m_Buffer.dynamic[m_Buffer.writeIndex].get());
    cmd->ResourceBarrier(/* UAV barrier */);
    cmd->Dispatch(groupCount, 1, 1);
}

void GPUPhysicsEngine::EndFrame() { m_Buffer.Flip(); }
```

### E. 累加器着色器整合

```glsl
#version 460 core
layout(local_size_x = 256) in;

struct ParticleDynamic {
    vec3 position; float radius;
    vec3 velocity; uint  flags;
};

layout(std430, binding = 0) readonly  buffer InBuf  { ParticleDynamic particles[]; } inBuf;
layout(std430, binding = 1) writeonly buffer OutBuf { ParticleDynamic particles[]; } outBuf;
layout(std430, binding = 2) buffer GlobalBuf {
    float frameDeltaTime, fixedDt, accumulator;
    uint  maxSubSteps, frameIndex;
    float _pad0, _pad1;
} global;

uniform vec3  u_Gravity, u_BoxMin, u_BoxMax;
uniform float u_Restitution, u_Damping;

void IntegrateSubstep(inout ParticleDynamic p, float dt) {
    p.velocity += u_Gravity * dt;
    p.velocity *= (1.0 - u_Damping * dt);
    p.position += p.velocity * dt;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= inBuf.particles.length()) return;

    if (gl_GlobalInvocationID.x == 0) {
        global.accumulator += global.frameDeltaTime;
    }
    barrier();

    ParticleDynamic p = inBuf.particles[idx];

    uint steps = 0;
    while (global.accumulator >= global.fixedDt && steps < global.maxSubSteps) {
        IntegrateSubstep(p, global.fixedDt);
        global.accumulator -= global.fixedDt;
        steps++;
    }

    outBuf.particles[idx] = p;
}
```

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-07-18 | 基于 v1.x MVP 代码审阅和 Ring2 测试结果 |

---
