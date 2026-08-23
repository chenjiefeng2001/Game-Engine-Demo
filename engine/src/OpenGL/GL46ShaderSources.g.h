// ═════════════════════════════════════════════════════════
// GENERATED FILE — DO NOT EDIT
// Source of truth : assets/shaders/*.comp.glsl (7 files)
// Regenerator     : tools/embed_shaders.cmake (invoked by engine/CMakeLists.txt)
// Contract        : docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md · Phase 1
// ═════════════════════════════════════════════════════════

#pragma once
#include <string_view>

namespace Engine {
namespace RHI {
namespace ShaderSources {

struct Entry {
    std::string_view name;    ///< PSO 注册名
    std::string_view source;  ///< 完整 GLSL 源码（含 #version）
};

inline constexpr Entry kCompute[] = {
    // assets/shaders/gpu_physics_collide.comp.glsl  md5:6a2d2e390ad853577a22acdc980415f2
    { "gpu_physics_collide",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_collide.comp.glsl — GPU 物理 Collide Pass（单一真理源）
//
// 本文件是运行时着色器的唯一来源：
//   构建期由 tools/embed_shaders.cmake 嵌入至
//   engine/src/OpenGL/GL46ShaderSources.g.h（GENERATED, DO NOT EDIT）
// 修改本文件 → 重新构建 → 自动生效。禁止在 C++ 中手写 GLSL。
//
// ── 绑定契约（Phase 2 Ping-Pong）────────────────────────────
// PSO 注册名     : "gpu_physics_collide"     （Nsight 标签: CS_Collide）
// SSBO binding 0 : InBuf  — std430 readonly，积分后状态（稳定快照输入）
// SSBO binding 1 : OutBuf — std430，碰撞修正结果
// Workgroup      : local_size_x = 256 = GPUPhysicsConfig::workGroupSize
//
// ── Uniform 契约（与 GPUPhysicsEngine::RecordCollidePass 对应）──
//   u_Dt            float  （当前未消费；Phase 4 空间哈希将使用）
//   u_Restitution   float  碰撞弹性系数
//   u_ParticleCount uint   粒子总数（越界守卫 + 暴力遍历上界）
//
// ── 算法状态 ────────────────────────────────────────────────
// 当前为 O(N²) 全量暴力遍历 —— Phase 0 基线实测（RTX 3070 Laptop）:
//   16K=13.3ms / 64K=110.7ms / 256K≈1.27s
// Phase 4 将替换为空间哈希网格 26-Cell 邻接遍历，
// 本实现保留作为 Ring7 内核等价性 diff 的参考路径。
//
// ── 求解语义变更（Phase 2）─────────────────────────────────
// 旧版：单缓冲原位更新（Gauss-Seidel 式，读到的邻居可能已被本趟修改）
// 新版：src→dst 分离（Jacobi 式，全部线程读同一稳定输入快照）
//       ⇒ 确定性提升：同输入必产生同输出，与线程调度顺序无关
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) readonly buffer InBuf {
    Particle particles[];
} inBuf;

layout(std430, binding = 1) buffer OutBuf {
    Particle particles[];
} outBuf;

uniform float u_Dt;
uniform float u_Restitution;
uniform uint  u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = inBuf.particles[idx];
    vec3 originPos = p.position;   ///< 真 Jacobi：距离始终基于快照位置（顺序无关）
    uint probeEval = 0u;   // DEBUG PROBE (Phase 4)
    uint probeFix  = 0u;   // DEBUG PROBE (Phase 4)

    // 碰撞搜索：遍历所有粒子（暴力参考实现，读稳定快照）
    // Phase 4 将替换为空间哈希网格 26-Cell 邻接遍历
    for (uint j = 0; j < u_ParticleCount; ++j) {
        if (j == idx) continue;
        Particle other = inBuf.particles[j];
        vec3 diff = other.position - originPos;
        float dist = length(diff);
        float minDist = p.radius + other.radius;

        if (dist < minDist && dist > 0.0001) {
            vec3 normal = diff / dist;
            float overlap = minDist - dist;
            float totalMass = p.mass + other.mass;
            if (totalMass < 0.0001) continue;
            float pWeight = other.mass / totalMass;
            p.position -= normal * overlap * pWeight;

            vec3 relVel = p.velocity - other.velocity;
            float velAlongNormal = dot(relVel, normal);
            if (velAlongNormal < 0.0) {
                vec3 impulse = normal * velAlongNormal * (1.0 + u_Restitution) / totalMass;
                p.velocity -= impulse * other.mass;
            }
            ++probeFix;
        }
        ++probeEval;
    }

    // 无条件写回 —— 未碰撞粒子也必须落到目标槽位（Ping-Pong 完整性）
    p.padding[0] = uintBitsToFloat(probeEval);   // DEBUG PROBE (Phase 4)
    p.padding[1] = uintBitsToFloat(probeFix);    // DEBUG PROBE (Phase 4)
    outBuf.particles[idx] = p;
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_collide_spatial.comp.glsl  md5:418ae697cbb66d1b9782ffebdc2f20e2
    { "gpu_physics_collide_spatial",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_collide_spatial.comp.glsl — 27-Cell 邻域 Jacobi 碰撞（Phase 4-C/D）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_collide_spatial" （Nsight 标签: CS_CollideSpatial）
// SSBO binding 0 : InBuf     — readonly，稳定输入快照（与 brute-force 版一致）
// SSBO binding 1 : OutBuf    — 碰撞修正结果
// SSBO binding 4 : StartBuf  — uint cellStart[]（排他前缀和，长度 = cell 数）
// SSBO binding 5 : SortedBuf — uint sortedIdx[]（按 cell 分组的粒子索引）
//
// ── Uniform 契约（与 RecordCollideSpatialPass 对应）──────────
//   u_Dt            float  （未消费，保持与 brute-force 内核绑定兼容）
//   u_Restitution   float
//   u_ParticleCount uint
//   u_GridMin / u_CellSize / u_GridDim(vec3)  — 与 hash_build 相同
//
// ── 语义约束（Phase 4 核心架构红线）─────────────────────────
// 求解器语义与 brute-force 版【逐行一致】：
//   - Jacobi 快照：全部线程只读 InBuf，写 OutBuf，无原位修改
//   - 无条件写回所有槽位
//   - 冲量公式 / 质量加权位置修正 / 弹性系数完全相同
// 空间哈希仅改变"评估哪些候选对"：
//   候选集 = 27 个邻接 cell 的有序分段 [cellStart[c], cellStart[c+1])
//   完备性条件：cellSize ≥ 2×maxRadius ⇒ 所有可能接触对必然落在
//   27-cell 邻域内（Ring7 由 1-step Δv/Δpos 门禁验证）
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) readonly buffer InBuf {
    Particle particles[];
} inBuf;

layout(std430, binding = 1) writeonly buffer OutBuf {
    Particle particles[];
} outBuf;

layout(std430, binding = 4) readonly buffer StartBuf {
    uint cellStart[];
};

layout(std430, binding = 5) readonly buffer SortedBuf {
    uint sortedIdx[];
};

uniform float u_Dt;
uniform float u_Restitution;
uniform uint  u_ParticleCount;
uniform uint  u_CellCount;
uniform vec3  u_GridMin;
uniform float u_CellSize;
uniform vec3  u_GridDim;   // 整数维度以 float 传递（< 2^24 精确）

ivec3 cellCoords(vec3 pos) {
    ivec3 g = ivec3(floor((pos - u_GridMin) / u_CellSize));
    return clamp(g, ivec3(0), ivec3(u_GridDim) - ivec3(1));
}

uint encodeCell(ivec3 g) {
    return uint(g.x) + uint(g.y) * uint(u_GridDim.x)
         + uint(g.z) * uint(u_GridDim.x) * uint(u_GridDim.y);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = inBuf.particles[idx];
    vec3 originPos = p.position;   ///< 真 Jacobi：距离始终基于快照位置（与 brute 内核一致）
    uint probeEval = 0u;   // DEBUG PROBE (Phase 4)
    uint probeFix  = 0u;   // DEBUG PROBE (Phase 4)

    const ivec3 base = cellCoords(p.position);

    // ── 27-cell 邻域遍历（连续内存分段）──
    for (int oz = -1; oz <= 1; ++oz)
    for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
        ivec3 g = base + ivec3(ox, oy, oz);
        if (any(lessThan(g, ivec3(0))) || any(greaterThanEqual(g, ivec3(u_GridDim))))
            continue;                                   // 网格外

        const uint c  = encodeCell(g);
        const uint cs = cellStart[c];
        const uint ce = (c + 1u < u_CellCount) ? cellStart[c + 1u] : u_ParticleCount;

        for (uint k = cs; k < ce; ++k) {
            const uint j = sortedIdx[k];
            if (j == idx) continue;

            Particle other = inBuf.particles[j];
            vec3 diff = other.position - originPos;
            float dist = length(diff);
            float minDist = p.radius + other.radius;

            if (dist < minDist && dist > 0.0001) {
                vec3 normal = diff / dist;
                float overlap = minDist - dist;
                float totalMass = p.mass + other.mass;
                if (totalMass > 0.0001) {
                    float pWeight = other.mass / totalMass;
                    p.position -= normal * overlap * pWeight;

                    vec3 relVel = p.velocity - other.velocity;
                    float velAlongNormal = dot(relVel, normal);
                    if (velAlongNormal < 0.0) {
                        vec3 impulse = normal * velAlongNormal
                                     * (1.0 + u_Restitution) / totalMass;
                        p.velocity -= impulse * other.mass;
                    }
                    ++probeFix;
                }
            }
            ++probeEval;
        }
    }

    // 无条件写回 —— 与 brute-force 内核相同的 Ping-Pong 完整性契约
    p.padding[0] = uintBitsToFloat(probeEval);   // DEBUG PROBE (Phase 4)
    p.padding[1] = uintBitsToFloat(probeFix);    // DEBUG PROBE (Phase 4)
    outBuf.particles[idx] = p;
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_hash_build.comp.glsl  md5:e3e2b711082ac2b1507f7b1a67982019
    { "gpu_physics_hash_build",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_build.comp.glsl — 计数阶段（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_build"  （Nsight 标签: CS_HashBuild）
// SSBO binding 0 : InBuf    — readonly，积分后粒子快照
// SSBO binding 1 : CountBuf — uint cellCount[]，本 pass 前 clear 过
// SSBO binding 2 : CellOfBuf  — uint cellOf[]，每粒子所属 cell
// SSBO binding 3 : OffsetBuf  — uint offset[]，每粒子在 cell 内的序号
//
// ── Uniform 契约 ────────────────────────────────────────────
//   u_ParticleCount / u_GridMin / u_CellSize / u_GridDim(vec3)
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) readonly buffer InBuf {
    Particle particles[];
} inBuf;

layout(std430, binding = 1) buffer CountBuf {
    uint cellCount[];
};

layout(std430, binding = 2) writeonly buffer CellOfBuf {
    uint cellOf[];
};

layout(std430, binding = 3) writeonly buffer OffsetBuf {
    uint offset[];
};

uniform uint  u_ParticleCount;
uniform vec3  u_GridMin;
uniform float u_CellSize;
uniform vec3  u_GridDim;   // 整数维度以 float 传递（< 2^24 精确）

uint linearCell(vec3 pos) {
    // 越界钳制：边界碰撞保证粒子位于 [boxMin+r, boxMax-r]，此处仅防御浮点误差
    ivec3 g = ivec3(floor((pos - u_GridMin) / u_CellSize));
    g = clamp(g, ivec3(0), ivec3(u_GridDim) - ivec3(1));
    return uint(g.x)
         + uint(g.y) * uint(u_GridDim.x)
         + uint(g.z) * uint(u_GridDim.x) * uint(u_GridDim.y);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    uint c = linearCell(inBuf.particles[idx].position);
    cellOf[idx]  = c;
    offset[idx]  = atomicAdd(cellCount[c], 1u);   // cell 内序号（乱序但唯一）
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_hash_clear.comp.glsl  md5:b0d9ae70efe5d5bc801583ab25e366d4
    { "gpu_physics_hash_clear",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_clear.comp.glsl — 网格计数清空（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_clear"  （Nsight 标签: CS_HashClear）
// SSBO binding 0 : CountBuf — uint cellCount[]，每元素写 0
//
// ── 设计说明 ────────────────────────────────────────────────
// Broad-phase 采用"计数排序"而非链表桶：
//   - 仅依赖 atomicAdd（atomicExch 在部分编译器存在重载解析问题）
//   - 排序后邻域遍历为连续内存访问，缓存友好性优于链表随机跳转
// 约束：网格总 cell 数 ≤ 65536（CPU 侧自适应 cellSize 保证），
//       使 Scan 可在单工作组内完成。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) writeonly buffer CountBuf {
    uint cellCount[];
};

uniform uint u_CellCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_CellCount) return;
    cellCount[idx] = 0u;
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_hash_scan.comp.glsl  md5:20f177affa6737af52fa2e1d3bc245ba
    { "gpu_physics_hash_scan",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_scan.comp.glsl — 单工作组排他前缀和（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_scan"   （Nsight 标签: CS_HashScan）
// SSBO binding 1 : CountBuf  — uint cellCount[]（输入：每 cell 粒子数）
// SSBO binding 2 : StartBuf  — uint cellStart[]（输出：exclusive prefix sum）
//
// ── Uniform 契约 ────────────────────────────────────────────
//   u_CellCount uint — 网格总 cell 数（≤ 65536，CPU 侧保证）
//
// ── 算法 ────────────────────────────────────────────────────
// 单工作组分块串行扫描：
//   每线程负责一段连续 chunk（chunk 内串行累加）→ 共享内存汇总 →
//   tid0 对 chunk 总量做排他扫描 → 各线程回写自己 chunk 的绝对起点。
// 复杂度 O(C / 512 + C) @ 单工作组；C ≤ 65536 时 < 0.05ms。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 512, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 1) buffer CountBuf {
    uint cellCount[];
};

layout(std430, binding = 2) writeonly buffer StartBuf {
    uint cellStart[];
};

uniform uint u_CellCount;

shared uint chunkSum[512];
shared uint chunkBase[512];

void main() {
    const uint tid  = gl_LocalInvocationID.x;
    const uint nT   = 512u;
    const uint cCnt = u_CellCount;
    const uint chunkLen = (cCnt + nT - 1u) / nT;

    // ── Pass 1: chunk 内串行求和 ──
    uint sum = 0u;
    {
        const uint begin = tid * chunkLen;
        const uint end   = min(begin + chunkLen, cCnt);
        for (uint c = begin; c < end; ++c) sum += cellCount[c];
    }
    chunkSum[tid] = sum;
    barrier();

    // ── Pass 2: tid0 对 chunk 总量做排他扫描 ──
    if (tid == 0u) {
        uint run = 0u;
        for (uint t = 0; t < nT; ++t) {
            chunkBase[t] = run;
            run += chunkSum[t];
        }
    }
    barrier();

    // ── Pass 3: 回写本 chunk 每 cell 的绝对起点 ──
    uint run = chunkBase[tid];
    {
        const uint begin = tid * chunkLen;
        const uint end   = min(begin + chunkLen, cCnt);
        for (uint c = begin; c < end; ++c) {
            cellStart[c] = run;
            run += cellCount[c];
        }
    }
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_hash_scatter.comp.glsl  md5:56e58bcedcbcd0efadd6a1d3b7628493
    { "gpu_physics_hash_scatter",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_scatter.comp.glsl — 粒子散射进有序槽位（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_scatter" （Nsight 标签: CS_HashScatter）
// SSBO binding 2 : CellOfBuf — uint cellOf[]
// SSBO binding 3 : OffsetBuf — uint offset[]
// SSBO binding 4 : StartBuf  — uint cellStart[]
// SSBO binding 5 : SortedBuf — uint sortedIdx[]（输出：按 cell 分组的粒子索引）
//
// 无原子操作：slot = cellStart[cellOf[i]] + offset[i]，各槽位互斥。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 2) readonly buffer CellOfBuf {
    uint cellOf[];
};

layout(std430, binding = 3) readonly buffer OffsetBuf {
    uint offset[];
};

layout(std430, binding = 4) readonly buffer StartBuf {
    uint cellStart[];
};

layout(std430, binding = 5) writeonly buffer SortedBuf {
    uint sortedIdx[];
};

uniform uint u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;
    sortedIdx[cellStart[cellOf[idx]] + offset[idx]] = idx;
}
)GLSL_EMBED_7F3A" },
    // assets/shaders/gpu_physics_integrate.comp.glsl  md5:6f3cbd8b73ceb8f875b7561c6dfbf1f9
    { "gpu_physics_integrate",
      R"GLSL_EMBED_7F3A(#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_integrate.comp.glsl — GPU 物理 Integrate Pass（单一真理源）
//
// 本文件是运行时着色器的唯一来源：
//   构建期由 tools/embed_shaders.cmake 嵌入至
//   engine/src/OpenGL/GL46ShaderSources.g.h（GENERATED, DO NOT EDIT）
// 修改本文件 → 重新构建 → 自动生效。禁止在 C++ 中手写 GLSL。
//
// ── 绑定契约（Phase 2 Ping-Pong）────────────────────────────
// PSO 注册名     : "gpu_physics_integrate"   （Nsight 标签: CS_Integrate）
// SSBO binding 0 : InBuf  — std430 readonly，上一步状态（源）
// SSBO binding 1 : OutBuf — std430，本步结果（目标）
//   数据布局与 C++ GPUParticleData 对齐：
//   engine/include/Engine/Core/Physics/GPUParticle.h（64B/粒子，static_assert）
// Workgroup      : local_size_x = 256 = GPUPhysicsConfig::workGroupSize
//
// ── Uniform 契约（与 GPUPhysicsEngine::RecordIntegratePass 对应）──
//   u_Dt            float  时间步长
//   u_Gravity       vec3   重力加速度
//   u_Restitution   float  边界弹性系数
//   u_Damping       float  线性阻尼系数
//   u_BoxMin        vec3   仿真包围盒最小点
//   u_BoxMax        vec3   仿真包围盒最大点
//   u_ParticleCount uint   粒子总数（越界守卫）
//
// ── 语义 ────────────────────────────────────────────────────
// State(cur) → 半隐式欧拉积分 + 边界反弹 → State(next)
// 纯函数：读 src 写 dst，无跨线程依赖，无同 buffer 读 写竞争。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) readonly buffer InBuf {
    Particle particles[];
} inBuf;

layout(std430, binding = 1) writeonly buffer OutBuf {
    Particle particles[];
} outBuf;

uniform float u_Dt;
uniform vec3  u_Gravity;
uniform vec3  u_BoxMin;
uniform vec3  u_BoxMax;
uniform float u_Restitution;
uniform float u_Damping;
uniform uint  u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = inBuf.particles[idx];

    // ── 半隐式欧拉积分 ──
    // v(t+dt) = v(t) + g * dt
    p.velocity += u_Gravity * u_Dt;

    // 阻尼: v *= (1 - damping * dt)
    p.velocity *= (1.0 - u_Damping * u_Dt);

    // x(t+dt) = x(t) + v(t+dt) * dt
    p.position += p.velocity * u_Dt;

    // ── 边界碰撞 ──
    // X
    if (p.position.x - p.radius < u_BoxMin.x) {
        p.position.x = u_BoxMin.x + p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }
    if (p.position.x + p.radius > u_BoxMax.x) {
        p.position.x = u_BoxMax.x - p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }
    // Y
    if (p.position.y - p.radius < u_BoxMin.y) {
        p.position.y = u_BoxMin.y + p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }
    if (p.position.y + p.radius > u_BoxMax.y) {
        p.position.y = u_BoxMax.y - p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }
    // Z
    if (p.position.z - p.radius < u_BoxMin.z) {
        p.position.z = u_BoxMin.z + p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }
    if (p.position.z + p.radius > u_BoxMax.z) {
        p.position.z = u_BoxMax.z - p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }

    // 无条件写回（Ping-Pong 目标必须每槽位完整）
    outBuf.particles[idx] = p;
}
)GLSL_EMBED_7F3A" },
};

/** 按 PSO 注册名查找源码；未命中返回 nullptr */
inline const Entry* Find(std::string_view name) {
    for (const Entry& e : kCompute)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace ShaderSources
} // namespace RHI
} // namespace Engine
