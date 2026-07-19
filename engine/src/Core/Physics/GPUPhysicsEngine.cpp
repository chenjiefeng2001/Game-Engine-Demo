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

    if (!device) {
        s_Log.Warn("Initialize: no RHI device - structural mode only");
        m_Initialized = true;
        return true;
    }
    if (!CreateParticleBuffer()) return false;
    if (!CreateComputeShaders()) { Shutdown(); return false; }
    if (!CreateRenderResources()) { Shutdown(); return false; }

    m_Initialized = true;
    s_Log.Info("GPU Physics Engine initialized ({} particles)", config.particleCount);
    return true;
}

void GPUPhysicsEngine::Shutdown() {
    if (!m_Initialized) return;
    delete m_IntegratePSO; m_IntegratePSO = nullptr;
    delete m_CollidePSO;   m_CollidePSO = nullptr;
    delete m_RenderPSO;    m_RenderPSO = nullptr;
    m_ParticleBuffer.reset();
    m_VertexBuffer.reset();
    m_IndexBuffer.reset();
    m_IndexCount = 0;
    m_Initialized = false;
    m_Stats = {};
}

void GPUPhysicsEngine::Update(float dt, RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList) return;

    // 验证 PSO 有效性 — 如果 PSO 为 null（通常是 hash 不匹配），
    // SetPipelineState 会静默失败，导致 dispatch 无效果
    if (!m_IntegratePSO || !m_CollidePSO) {
        s_Log.Warn("Update skipped: PSO(s) not valid (integrate=0x{:x}, collide=0x{:x})",
                   reinterpret_cast<uintptr_t>(m_IntegratePSO),
                   reinterpret_cast<uintptr_t>(m_CollidePSO));
        return;
    }

    m_Stats.frameCount++;

    uint32_t groupCount = (m_Config.particleCount + m_Config.workGroupSize - 1)
                          / m_Config.workGroupSize;

    // ── Integrate Pass ──
    cmdList->SetPipelineState(m_IntegratePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());

    // 设置计算着色器参数
    cmdList->SetComputeFloat("u_Dt", dt);
    cmdList->SetComputeVec3("u_Gravity", m_Config.gravity[0], m_Config.gravity[1], m_Config.gravity[2]);
    cmdList->SetComputeFloat("u_Restitution", m_Config.restitution);
    cmdList->SetComputeFloat("u_Damping", m_Config.damping);
    cmdList->SetComputeVec3("u_BoxMin", m_Config.boxMin[0], m_Config.boxMin[1], m_Config.boxMin[2]);
    cmdList->SetComputeVec3("u_BoxMax", m_Config.boxMax[0], m_Config.boxMax[1], m_Config.boxMax[2]);
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);

    // UAV 屏障 → Dispatch → UAV 屏障
    RHI::ResourceBarrierDesc uavBarrier;
    uavBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    uavBarrier.buffer = m_ParticleBuffer.get();
    cmdList->ResourceBarrier(1, &uavBarrier);

    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &uavBarrier);

    // ── Collide Pass ──
    cmdList->SetPipelineState(m_CollidePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());
    cmdList->SetComputeFloat("u_Dt", dt);
    cmdList->SetComputeFloat("u_Restitution", m_Config.restitution);
    cmdList->SetComputeInt("u_ParticleCount", (int32_t)m_Config.particleCount);

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
    if (!m_Device || !m_ParticleBuffer) return;
    size_t dataSize = count * sizeof(GPUParticleData);
    size_t bufSize  = (size_t)m_Config.particleCount * sizeof(GPUParticleData);
    size_t uploadSize = (std::min)(dataSize, bufSize);

    // 通过 RHI Buffer 持久映射写入（GL46Device::CreateBuffer 在 stub 模式下
    // 也会分配 CPU 内存并设置 m_MappedPtr，因此此路径始终可用）
    auto* gl46Buf = static_cast<RHI::GL46Buffer*>(m_ParticleBuffer.get());
    void* mapped = gl46Buf->GetPersistentPtr();
    if (mapped) {
        std::memcpy(mapped, data, uploadSize);
        s_Log.Info("Uploaded {} bytes via persistent mapping", uploadSize);
    } else {
        s_Log.Error("Buffer has no persistent mapping - {} bytes not written", uploadSize);
    }
}

void GPUPhysicsEngine::ReadbackParticles(uint32_t startIndex, uint32_t count,
                                          GPUParticleData* outData) {
    if (!m_Device || !m_ParticleBuffer || !outData) return;

    auto* gl46Buf = static_cast<RHI::GL46Buffer*>(m_ParticleBuffer.get());

    if (gl46Buf->GetGLHandle() != 0) {
        auto* gl46Dev = static_cast<RHI::GL46Device*>(m_Device);
        auto& gl = gl46Dev->GetGL();
        
        // 确保 GPU 完成所有操作
        gl.Finish();
        
        // 先用持久映射读
        void* mapped = gl46Buf->GetPersistentPtr();
        if (mapped) {
            size_t offset = (size_t)startIndex * sizeof(GPUParticleData);
            size_t reqSize = (size_t)count * sizeof(GPUParticleData);
            size_t bufSize = (size_t)m_Config.particleCount * sizeof(GPUParticleData);
            size_t copyBytes = (std::min)(reqSize, bufSize - offset);
            std::memcpy(outData, static_cast<const char*>(mapped) + offset, copyBytes);
        }
        
        // 用 glGetNamedBufferSubData 验证（仅第一个粒子）
        GPUParticleData verify;
        gl.GetNamedBufferSubData(gl46Buf->GetGLHandle(), 0, sizeof(GPUParticleData), &verify);
        std::fprintf(stdout, "    [Readback] persistent: y=%.4f vy=%.4f  glGetSubData: y=%.4f vy=%.4f\n",
                     outData[0].position[1], outData[0].velocity[1],
                     verify.position[1], verify.velocity[1]);
        std::fflush(stdout);
        return;
    }

    // 持久映射回退（stub 模式）
    void* mapped = gl46Buf->GetPersistentPtr();
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
    if (!m_ParticleBuffer) return;

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
    m_Stats.totalParticles = m_Config.particleCount;
}

bool GPUPhysicsEngine::CreateParticleBuffer() {
    RHI::RHIBufferDesc desc;
    desc.size = m_Config.particleCount * sizeof(GPUParticleData);
    desc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    m_ParticleBuffer = m_Device->CreateBuffer(desc);
    return m_ParticleBuffer != nullptr;
}

bool GPUPhysicsEngine::CreateComputeShaders() {
    RHI::ComputePSODesc integrateDesc, collideDesc;
    integrateDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    m_IntegratePSO = m_Device->CreateComputePSO(integrateDesc);
    collideDesc.computeShader = StringID::Runtime("gpu_physics_collide");
    m_CollidePSO = m_Device->CreateComputePSO(collideDesc);
    return (m_IntegratePSO != nullptr && m_CollidePSO != nullptr);
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