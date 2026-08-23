/**
 * @file GPUPhysicsEngine.cpp
 * @brief GPU 物理引擎 — 纯 RHI Compute 调度层
 *
 * 设计原则：
 *   - 不包含任何 CPU 物理模拟代码
 *   - 所有粒子数据通过 RHI Buffer 上传/回读
 *   - CPU 验证通过独立的 CPUSimulator 实现
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>

namespace Engine {

static Logger s_Log("GPUPhysics");

GPUPhysicsEngine::GPUPhysicsEngine() = default;
GPUPhysicsEngine::~GPUPhysicsEngine() { Shutdown(); }

bool GPUPhysicsEngine::Initialize(RHI::IRHIDevice* device, const GPUPhysicsConfig& config) {
    if (m_Initialized) Shutdown();
    m_Device = device;
    m_Config = config;
    m_Accumulator = FixedTimestepAccumulator(config.fixedDt, config.maxSubSteps);
    m_CurSlot = 0;
    m_LastFrameSubsteps = 0;

    if (!device) {
        s_Log.Warn("Initialize: no RHI device - structural mode only");
        m_Initialized = true;
        return true;
    }
    if (!CreateParticleBuffers()) return false;
    if (!CreateComputeShaders()) { Shutdown(); return false; }
    if (!CreateRenderResources()) { Shutdown(); return false; }
    if (m_Config.collisionBackend == GPUCollisionBackend::SpatialHash) {
        if (!SetupSpatialGrid()) { Shutdown(); return false; }
    }

    m_Initialized = true;
    const char* backendName = (m_Config.collisionBackend == GPUCollisionBackend::SpatialHash)
                              ? "spatial-hash" : "brute-force";
    s_Log.Info("GPU Physics Engine initialized ({} particles, fixedDt={:.4f}, maxSubSteps={}, ping-pong, {})",
               config.particleCount, config.fixedDt, config.maxSubSteps, backendName);
    return true;
}

void GPUPhysicsEngine::Shutdown() {
    if (!m_Initialized) return;
    delete m_IntegratePSO; m_IntegratePSO = nullptr;
    delete m_CollidePSO;   m_CollidePSO = nullptr;
    delete m_RenderPSO;    m_RenderPSO = nullptr;
    delete m_HashClearPSO; m_HashClearPSO = nullptr;
    delete m_HashBuildPSO; m_HashBuildPSO = nullptr;
    delete m_HashScanPSO;  m_HashScanPSO = nullptr;
    delete m_HashScatterPSO; m_HashScatterPSO = nullptr;
    delete m_CollideSpatialPSO; m_CollideSpatialPSO = nullptr;
    m_StateBuffer[0].reset();
    m_StateBuffer[1].reset();
    m_CellCountBuf.reset();
    m_CellStartBuf.reset();
    m_CellOfBuf.reset();
    m_OffsetBuf.reset();
    m_SortedIdxBuf.reset();
    m_VertexBuffer.reset();
    m_IndexBuffer.reset();
    m_IndexCount = 0;
    m_CurSlot = 0;
    m_LastFrameSubsteps = 0;
    m_GridDim[0] = m_GridDim[1] = m_GridDim[2] = 0;
    m_GridCells = 0;
    m_Stats = {};
    m_Initialized = false;
}

RHI::IRHIBuffer* GPUPhysicsEngine::GetStateBuffer() const {
    return m_StateBuffer[m_CurSlot & 1u].get();
}

RHI::IRHIBuffer* GPUPhysicsEngine::GetScratchBuffer() const {
    return m_StateBuffer[(m_CurSlot ^ 1u) & 1u].get();
}

RHI::IRHIBuffer* GPUPhysicsEngine::GetParticleBuffer() const {
    return GetStateBuffer();
}

RHI::IRHIBuffer* GPUPhysicsEngine::GetDebugAuxBuffer(uint32_t index) const {
    switch (index) {
        case 0: return m_CellCountBuf.get();
        case 1: return m_CellStartBuf.get();
        case 2: return m_CellOfBuf.get();
        case 3: return m_OffsetBuf.get();
        case 4: return m_SortedIdxBuf.get();
        default: return nullptr;
    }
}

void GPUPhysicsEngine::Update(float frameDt, RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList) return;
    if (!m_IntegratePSO || !m_CollidePSO || !m_StateBuffer[0]) {
        s_Log.Warn("Update skipped: PSO(s)/buffer not valid");
        return;
    }

    m_Stats.frameCount++;
    m_LastFrameSubsteps = 0;

    // ── Phase 2A：累加器决定本帧子步数（帧率解耦 + 螺旋保护）──
    const int32_t steps = m_Accumulator.Advance(frameDt);
    for (int32_t s = 0; s < steps; ++s) {
        RecordStep(cmdList);
    }
    m_LastFrameSubsteps = static_cast<uint32_t>(steps);
    m_Stats.stepCount  += static_cast<uint64_t>(steps);
    m_Stats.simulationTime += static_cast<double>(steps) * m_Config.fixedDt;
}

uint32_t GPUPhysicsEngine::GetDispatchGroupCount() const {
    return (m_Config.particleCount + m_Config.workGroupSize - 1)
           / m_Config.workGroupSize;
}

void GPUPhysicsEngine::RecordStep(RHI::IRHICommandList* cmdList) {
    RHI::IRHIBuffer* cur = GetStateBuffer();     // 稳定输入快照
    RHI::IRHIBuffer* nxt = GetScratchBuffer();   // 本步写入目标

    RecordIntegratePass(m_Config.fixedDt, cur, nxt, cmdList);
    FlipSlots();                                  // 积分结果成为新 cur

    // Phase 4：broad-phase 只改变"评估哪些候选对"，不改求解器语义
    if (m_Config.collisionBackend == GPUCollisionBackend::SpatialHash
            && m_GridCells > 0) {
        RecordHashClearPass(cmdList);
        RecordHashBuildPass(GetStateBuffer(), cmdList);
        RecordHashScanPass(cmdList);
        RecordHashScatterPass(cmdList);
    }

    RecordCollidePass(GetStateBuffer(), GetScratchBuffer(), cmdList);
    FlipSlots();                                  // 碰撞结果写回原槽位
                                                   // ⇒ 每步后稳定状态槽位不变
}

void GPUPhysicsEngine::RecordIntegratePass(float dt,
                                           RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                           RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList || !m_IntegratePSO || !src || !dst) return;

    const uint32_t groupCount = GetDispatchGroupCount();

    cmdList->SetPipelineState(m_IntegratePSO);
    cmdList->SetUnorderedAccess(0, src);   // InBuf  (readonly in shader)
    cmdList->SetUnorderedAccess(1, dst);   // OutBuf

    cmdList->SetComputeFloat("u_Dt", dt);
    cmdList->SetComputeVec3("u_Gravity", m_Config.gravity[0], m_Config.gravity[1], m_Config.gravity[2]);
    cmdList->SetComputeFloat("u_Restitution", m_Config.restitution);
    cmdList->SetComputeFloat("u_Damping", m_Config.damping);
    cmdList->SetComputeVec3("u_BoxMin", m_Config.boxMin[0], m_Config.boxMin[1], m_Config.boxMin[2]);
    cmdList->SetComputeVec3("u_BoxMax", m_Config.boxMax[0], m_Config.boxMax[1], m_Config.boxMax[2]);
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = dst;
    cmdList->ResourceBarrier(1, &uavBarrier);

    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::RecordCollidePass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                         RHI::IRHICommandList* cmdList) {
    if (m_Config.collisionBackend == GPUCollisionBackend::SpatialHash
            && m_CollideSpatialPSO && m_GridCells > 0) {
        RecordCollideSpatialPass(src, dst, cmdList);
    } else {
        RecordCollideBrutePass(src, dst, cmdList);
    }
}

void GPUPhysicsEngine::RecordCollideBrutePass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                              RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList || !m_CollidePSO || !src || !dst) return;

    const uint32_t groupCount = GetDispatchGroupCount();

    cmdList->SetPipelineState(m_CollidePSO);
    cmdList->SetUnorderedAccess(0, src);   // InBuf  (readonly 快照)
    cmdList->SetUnorderedAccess(1, dst);   // OutBuf
    cmdList->SetComputeFloat("u_Dt", 0.0f);   // collide 着色器当前未消费 dt
    cmdList->SetComputeFloat("u_Restitution", m_Config.restitution);
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = dst;
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

bool GPUPhysicsEngine::SetupSpatialGrid() {
    if (!m_Device || !m_StateBuffer[0]) return false;

    // cellSize 自适应：总量超上限时按 1.5x 放大（保序：cell ≥ 2×maxRadius 完备性优先于分辨率）
    float cell = std::max(m_Config.spatialCellSize, 1e-3f);
    float ext[3];
    for (int i = 0; i < 3; ++i)
        ext[i] = std::max(m_Config.boxMax[i] - m_Config.boxMin[i], cell);

    auto totalCellsFor = [&](float c) -> uint64_t {
        uint64_t d0 = (uint64_t)std::ceil(ext[0] / c);
        uint64_t d1 = (uint64_t)std::ceil(ext[1] / c);
        uint64_t d2 = (uint64_t)std::ceil(ext[2] / c);
        return d0 * d1 * d2;
    };
    int guard = 0;
    while (totalCellsFor(cell) > m_Config.spatialMaxCells && guard++ < 32)
        cell *= 1.5f;

    m_CellSize = cell;
    for (int i = 0; i < 3; ++i)
        m_GridDim[i] = (uint32_t)std::ceil(ext[i] / cell);
    m_GridCells = m_GridDim[0] * m_GridDim[1] * m_GridDim[2];

    RHI::RHIBufferDesc desc;
    desc.memoryUsage = RHI::MemoryUsage::GPU_Only;

    desc.size = (size_t)m_GridCells * sizeof(uint32_t);
    m_CellCountBuf = m_Device->CreateBuffer(desc);
    m_CellStartBuf = m_Device->CreateBuffer(desc);

    desc.size = (size_t)m_Config.particleCount * sizeof(uint32_t);
    m_CellOfBuf    = m_Device->CreateBuffer(desc);
    m_OffsetBuf    = m_Device->CreateBuffer(desc);
    m_SortedIdxBuf = m_Device->CreateBuffer(desc);

    const bool ok = m_CellCountBuf && m_CellStartBuf && m_CellOfBuf
                 && m_OffsetBuf && m_SortedIdxBuf;
    if (ok)
        s_Log.Info("Spatial grid: {}x{}x{} = {} cells, cellSize={:.3f}",
                   m_GridDim[0], m_GridDim[1], m_GridDim[2], m_GridCells, cell);
    return ok;
}

void GPUPhysicsEngine::RecordHashClearPass(RHI::IRHICommandList* cmdList) {
    if (!m_HashClearPSO || !cmdList || !m_CellCountBuf) return;
    const uint32_t groups = (m_GridCells + m_Config.workGroupSize - 1) / m_Config.workGroupSize;

    cmdList->SetPipelineState(m_HashClearPSO);
    cmdList->SetUnorderedAccess(0, m_CellCountBuf.get());
    cmdList->SetComputeInt("u_CellCount", (int32_t)m_GridCells);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = m_CellCountBuf.get();
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(groups, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::RecordHashBuildPass(RHI::IRHIBuffer* src, RHI::IRHICommandList* cmdList) {
    if (!m_HashBuildPSO || !cmdList || !src || !m_CellCountBuf || !m_CellOfBuf || !m_OffsetBuf) return;
    const uint32_t groupCount = GetDispatchGroupCount();

    cmdList->SetPipelineState(m_HashBuildPSO);
    cmdList->SetUnorderedAccess(0, src);                 // InBuf
    cmdList->SetUnorderedAccess(1, m_CellCountBuf.get());// CountBuf
    cmdList->SetUnorderedAccess(2, m_CellOfBuf.get());   // CellOfBuf
    cmdList->SetUnorderedAccess(3, m_OffsetBuf.get());   // OffsetBuf
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);
    cmdList->SetComputeVec3("u_GridMin", m_Config.boxMin[0], m_Config.boxMin[1], m_Config.boxMin[2]);
    cmdList->SetComputeFloat("u_CellSize", m_CellSize);
    cmdList->SetComputeVec3("u_GridDim",
                            (float)m_GridDim[0], (float)m_GridDim[1], (float)m_GridDim[2]);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = m_CellCountBuf.get();
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::RecordHashScanPass(RHI::IRHICommandList* cmdList) {
    if (!m_HashScanPSO || !cmdList || !m_CellCountBuf || !m_CellStartBuf) return;

    // 单工作组（local_size_x=512，着色器内固定）
    cmdList->SetPipelineState(m_HashScanPSO);
    cmdList->SetUnorderedAccess(1, m_CellCountBuf.get());
    cmdList->SetUnorderedAccess(2, m_CellStartBuf.get());
    cmdList->SetComputeInt("u_CellCount", (int32_t)m_GridCells);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = m_CellStartBuf.get();
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(1, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::RecordHashScatterPass(RHI::IRHICommandList* cmdList) {
    if (!m_HashScatterPSO || !cmdList || !m_CellOfBuf || !m_OffsetBuf
            || !m_CellStartBuf || !m_SortedIdxBuf) return;
    const uint32_t groupCount = GetDispatchGroupCount();

    cmdList->SetPipelineState(m_HashScatterPSO);
    cmdList->SetUnorderedAccess(2, m_CellOfBuf.get());
    cmdList->SetUnorderedAccess(3, m_OffsetBuf.get());
    cmdList->SetUnorderedAccess(4, m_CellStartBuf.get());
    cmdList->SetUnorderedAccess(5, m_SortedIdxBuf.get());
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = m_SortedIdxBuf.get();
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::RecordCollideSpatialPass(RHI::IRHIBuffer* src, RHI::IRHIBuffer* dst,
                                                RHI::IRHICommandList* cmdList) {
    if (!m_CollideSpatialPSO || !cmdList || !src || !dst
            || !m_CellStartBuf || !m_SortedIdxBuf) return;
    const uint32_t groupCount = GetDispatchGroupCount();

    cmdList->SetPipelineState(m_CollideSpatialPSO);
    cmdList->SetUnorderedAccess(0, src);                 // InBuf
    cmdList->SetUnorderedAccess(1, dst);                 // OutBuf
    cmdList->SetUnorderedAccess(4, m_CellStartBuf.get());// StartBuf
    cmdList->SetUnorderedAccess(5, m_SortedIdxBuf.get());// SortedBuf
    cmdList->SetComputeFloat("u_Dt", 0.0f);
    cmdList->SetComputeFloat("u_Restitution", m_Config.restitution);
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);
    cmdList->SetComputeInt("u_CellCount", (int32_t)m_GridCells);
    cmdList->SetComputeVec3("u_GridMin", m_Config.boxMin[0], m_Config.boxMin[1], m_Config.boxMin[2]);
    cmdList->SetComputeFloat("u_CellSize", m_CellSize);
    cmdList->SetComputeVec3("u_GridDim",
                            (float)m_GridDim[0], (float)m_GridDim[1], (float)m_GridDim[2]);

    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = dst;
    cmdList->ResourceBarrier(1, &uavBarrier);
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);
}

void GPUPhysicsEngine::Render(RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList || m_Stats.frameCount == 0) return;
    cmdList->SetPipelineState(m_RenderPSO);
    cmdList->SetVertexBuffer(0, m_VertexBuffer.get(), 6 * sizeof(float), 0);
    cmdList->SetIndexBuffer(m_IndexBuffer.get(), 0);
    cmdList->SetPrimitiveTopology(RHI::PrimitiveTopology::TriangleList);
    RHI::Viewport vp = { 0, 0, 1280, 720, 0, 1 };
    cmdList->SetViewport(vp);
    cmdList->DrawIndexed(m_IndexCount, 0, 0);
}

void GPUPhysicsEngine::UploadInitialData(const GPUParticleData* data, uint32_t count) {
    if (!m_Device || !m_StateBuffer[0]) return;
    size_t dataSize = count * sizeof(GPUParticleData);
    size_t bufSize  = (size_t)m_Config.particleCount * sizeof(GPUParticleData);
    size_t uploadSize = (std::min)(dataSize, bufSize);

    // 通过 RHI Buffer 持久映射写入（GL46Device::CreateBuffer 在 stub 模式下
    // 也会分配 CPU 内存并设置 m_MappedPtr，因此此路径始终可用）
    // Phase 2：双槽位同步写入，保证 Ping-Pong 两槽内容一致（确定性基线）
    for (int i = 0; i < 2; ++i) {
        auto* gl46Buf = static_cast<RHI::GL46Buffer*>(m_StateBuffer[i].get());
        void* mapped = gl46Buf ? gl46Buf->GetPersistentPtr() : nullptr;
        if (mapped) {
            std::memcpy(mapped, data, uploadSize);
            if (i == 0)
                s_Log.Info("Uploaded {} bytes via persistent mapping (both slots)", uploadSize);
        } else if (i == 0) {
            s_Log.Error("Buffer has no persistent mapping - {} bytes not written", uploadSize);
        }
    }
}

void GPUPhysicsEngine::ReadbackParticles(uint32_t startIndex, uint32_t count,
                                          GPUParticleData* outData) {
    if (!m_Device || !outData) return;
    auto* stateBuf = static_cast<RHI::GL46Buffer*>(GetStateBuffer());
    if (!stateBuf) return;

    if (stateBuf->GetGLHandle() != 0) {
        auto* gl46Dev = static_cast<RHI::GL46Device*>(m_Device);
        auto& gl = gl46Dev->GetGL();

        // 确保 GPU 完成所有操作
        gl.Finish();

        // 先用持久映射读
        void* mapped = stateBuf->GetPersistentPtr();
        if (mapped) {
            size_t offset = (size_t)startIndex * sizeof(GPUParticleData);
            size_t reqSize = (size_t)count * sizeof(GPUParticleData);
            size_t bufSize = (size_t)m_Config.particleCount * sizeof(GPUParticleData);
            size_t copyBytes = (std::min)(reqSize, bufSize - offset);
            std::memcpy(outData, static_cast<const char*>(mapped) + offset, copyBytes);
        }

        // 用 glGetNamedBufferSubData 验证（仅第一个粒子）
        GPUParticleData verify;
        gl.GetNamedBufferSubData(stateBuf->GetGLHandle(), 0, sizeof(GPUParticleData), &verify);
        std::fprintf(stdout, "    [Readback] persistent: y=%.4f vy=%.4f  glGetSubData: y=%.4f vy=%.4f\n",
                     outData[0].position[1], outData[0].velocity[1],
                     verify.position[1], verify.velocity[1]);
        std::fflush(stdout);
        return;
    }

    // 持久映射回退（stub 模式）
    void* mapped = stateBuf->GetPersistentPtr();
    if (mapped) {
        size_t offset = (size_t)startIndex * sizeof(GPUParticleData);
        size_t reqSize = (size_t)count * sizeof(GPUParticleData);
        size_t bufSize = (size_t)m_Config.particleCount * sizeof(GPUParticleData);
        size_t copyBytes = (std::min)(reqSize, bufSize - offset);
        std::memcpy(outData, static_cast<const char*>(mapped) + offset, copyBytes);
    } else {
        std::memset(outData, 0, count * sizeof(GPUParticleData));
    }
}

void GPUPhysicsEngine::ResetParticles() {
    if (!m_StateBuffer[0]) return;

    std::vector<GPUParticleData> particles(m_Config.particleCount);
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> posDist(-m_Config.spawnRadius, m_Config.spawnRadius);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    for (auto& p : particles) {
        p.position[0] = posDist(gen);
        p.position[1] = std::abs(posDist(gen)) + 10.0f;
        p.position[2] = posDist(gen);
        p.radius = radiusDist(gen);
        p.velocity[0] = m_Config.spawnVelocity[0];
        p.velocity[1] = m_Config.spawnVelocity[1];
        p.velocity[2] = m_Config.spawnVelocity[2];
        p.mass = massDist(gen);
        p.color[0] = colorDist(gen);
        p.color[1] = colorDist(gen);
        p.color[2] = colorDist(gen);
        p.color[3] = 1.0f;
    }
    UploadInitialData(particles.data(), m_Config.particleCount);
    m_CurSlot = 0;
    m_Accumulator.Reset();
    m_Stats.totalParticles = m_Config.particleCount;
}

bool GPUPhysicsEngine::CreateParticleBuffers() {
    RHI::RHIBufferDesc desc;
    desc.size = m_Config.particleCount * sizeof(GPUParticleData);
    desc.memoryUsage = RHI::MemoryUsage::GPU_Only;

    // Phase 2B：Ping-Pong 双缓冲
    // VRAM 代价：64K × 64B × 2 = 8 MB（v3.0 文档风险 R3：可接受）
    m_StateBuffer[0] = m_Device->CreateBuffer(desc);
    m_StateBuffer[1] = m_Device->CreateBuffer(desc);
    return (m_StateBuffer[0] != nullptr && m_StateBuffer[1] != nullptr);
}

bool GPUPhysicsEngine::CreateComputeShaders() {
    RHI::ComputePSODesc integrateDesc, collideDesc;
    integrateDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    m_IntegratePSO = m_Device->CreateComputePSO(integrateDesc);
    collideDesc.computeShader = StringID::Runtime("gpu_physics_collide");
    m_CollidePSO = m_Device->CreateComputePSO(collideDesc);
    const bool coreOk = (m_IntegratePSO != nullptr && m_CollidePSO != nullptr);

    // Phase 4：Spatial Hash 内核（注册表已含源码；编译失败仅在启用后端时致命）
    RHI::ComputePSODesc clearDesc, buildDesc, scanDesc, scatterDesc, spatialDesc;
    clearDesc.computeShader   = StringID::Runtime("gpu_physics_hash_clear");
    buildDesc.computeShader   = StringID::Runtime("gpu_physics_hash_build");
    scanDesc.computeShader    = StringID::Runtime("gpu_physics_hash_scan");
    scatterDesc.computeShader = StringID::Runtime("gpu_physics_hash_scatter");
    spatialDesc.computeShader = StringID::Runtime("gpu_physics_collide_spatial");
    m_HashClearPSO       = m_Device->CreateComputePSO(clearDesc);
    m_HashBuildPSO       = m_Device->CreateComputePSO(buildDesc);
    m_HashScanPSO        = m_Device->CreateComputePSO(scanDesc);
    m_HashScatterPSO     = m_Device->CreateComputePSO(scatterDesc);
    m_CollideSpatialPSO  = m_Device->CreateComputePSO(spatialDesc);

    if (m_Config.collisionBackend == GPUCollisionBackend::SpatialHash
            && (!m_HashClearPSO || !m_HashBuildPSO || !m_HashScanPSO
                || !m_HashScatterPSO || !m_CollideSpatialPSO)) {
        s_Log.Error("SpatialHash backend requested but kernels unavailable");
        return false;
    }
    return coreOk;
}

bool GPUPhysicsEngine::CreateRenderResources() {
    RHI::RHIBufferDesc vbDesc, ibDesc;
    vbDesc.size = 1024 * 6 * sizeof(float);
    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);
    ibDesc.size = 1024 * sizeof(uint32_t);
    m_IndexBuffer = m_Device->CreateBuffer(ibDesc);
    m_IndexCount = 1024;
    RHI::GraphicsPSODesc renderDesc = RHI::GraphicsPSODesc::DefaultOpaque();
    m_RenderPSO = m_Device->CreateGraphicsPSO(renderDesc);
    return (m_VertexBuffer != nullptr && m_IndexBuffer != nullptr);
}

} // namespace Engine