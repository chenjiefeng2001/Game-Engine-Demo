/**
 * @file GPUPhysicsEngine.cpp
 * @brief GPU 物理引擎 MVP 实现 — 通过 RHI 抽象层驱动的 Compute Shader 粒子模拟
 *
 * 架构设计（v2.0 RHI 重构）：
 *   - 完全通过 Engine::RHI 抽象接口访问 GPU 资源
 *   - 无任何 OpenGL/Vulkan 直接调用
 *   - Buffer 创建：m_Device->CreateBuffer()
 *   - Compute 调度：cmdList->Dispatch() + cmdList->SetUnorderedAccess()
 *   - 屏障：cmdList->ResourceBarrier()
 *   - Shader 创建：m_Device->CreateComputePSO()
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/FileSystem.h"
#include <cstring>
#include <cmath>
#include <random>
#include <map>
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
        s_Log.Warn("Initialize: no RHI device - structural/validation mode only");
        m_Config = config;
        m_Initialized = true;
        return true;
    }

    // 1. 创建粒子 SSBO（使用 RHI Buffer）
    if (!CreateParticleBuffer()) {
        s_Log.Error("Failed to create particle buffer");
        return false;
    }

    // 2. 创建 Compute PSO（通过 RHI Shader）
    if (!CreateComputeShaders()) {
        s_Log.Error("Failed to create compute shaders");
        Shutdown();
        return false;
    }

    // 3. 创建渲染资源（VBO/IBO/Graphics PSO）
    if (!CreateRenderResources()) {
        s_Log.Error("Failed to create render resources");
        Shutdown();
        return false;
    }

    // 4. 分配 CPU 端粒子缓存
    m_CPUParticles.resize(config.particleCount);

    // 5. 初始化粒子数据
    ResetParticles();

    m_Initialized = true;
    s_Log.Info("GPU Physics Engine initialized via RHI successfully ({} particles)",
               config.particleCount);
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
    m_CPUParticles.clear();
    m_IndexCount = 0;
    m_Initialized = false;
    m_Stats = {};

    s_Log.Info("GPU Physics Engine shutdown");
}

void GPUPhysicsEngine::Update(float dt, RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList) return;

    m_Stats.frameCount++;

    // CPU 端物理模拟（在 GPU 后端完整实现前使用 CPU 更新）
    // 当 GL46Device 实现真正的 Compute Shader 调度后，下面的 CPU 代码
    // 将被 cmdList->Dispatch() 替换
    SimulateCPUParticles(dt);

    // RHI Compute 调度（占位，GL46 后端待完善）
    cmdList->SetPipelineState(m_IntegratePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());

    RHI::ResourceBarrierDesc preBarrier;
    preBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    preBarrier.buffer = m_ParticleBuffer.get();
    preBarrier.stateBefore = RHI::ResourceState::UnorderedAccess;
    preBarrier.stateAfter  = RHI::ResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(1, &preBarrier);

    uint32_t groupCount = (m_Config.particleCount + m_Config.workGroupSize - 1)
                          / m_Config.workGroupSize;
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &preBarrier);

    cmdList->SetPipelineState(m_CollidePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());
    cmdList->Dispatch(groupCount, 1, 1);
    cmdList->ResourceBarrier(1, &preBarrier);
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

void GPUPhysicsEngine::ReadbackParticles(uint32_t startIndex, uint32_t count,
                                          GPUParticleData* outData) {
    if (!m_Initialized || !outData || startIndex >= m_CPUParticles.size()) return;

    uint32_t copyCount = std::min(count, (uint32_t)(m_CPUParticles.size() - startIndex));
    std::memcpy(outData, m_CPUParticles.data() + startIndex,
                copyCount * sizeof(GPUParticleData));
}

void GPUPhysicsEngine::ResetParticles() {
    if (m_CPUParticles.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-m_Config.spawnRadius, m_Config.spawnRadius);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    for (uint32_t i = 0; i < m_Config.particleCount && i < (uint32_t)m_CPUParticles.size(); ++i) {
        auto& p = m_CPUParticles[i];
        p.position[0] = posDist(gen);
        p.position[1] = std::abs(posDist(gen)) + 10.0f;
        p.position[2] = posDist(gen);
        p.radius = radiusDist(gen);
        p.velocity[0] = m_Config.spawnVelocity[0] + posDist(gen) * 0.5f;
        p.velocity[1] = m_Config.spawnVelocity[1] + std::abs(posDist(gen) * 0.3f);
        p.velocity[2] = m_Config.spawnVelocity[2] + posDist(gen) * 0.5f;
        p.mass = massDist(gen);
        p.color[0] = colorDist(gen);
        p.color[1] = colorDist(gen);
        p.color[2] = colorDist(gen);
        p.color[3] = 1.0f;
    }

    m_Stats.totalParticles = m_Config.particleCount;
    s_Log.Info("Particles reset: {}", m_Config.particleCount);
}

// ── CPU 端物理模拟（用于测试验证，GPU 后端完善前使用） ──
void GPUPhysicsEngine::SimulateCPUParticles(float dt) {
    const float damping = m_Config.damping;
    const float restitution = m_Config.restitution;
    const float gx = m_Config.gravity[0];
    const float gy = m_Config.gravity[1];
    const float gz = m_Config.gravity[2];
    const float bminX = m_Config.boxMin[0], bmaxX = m_Config.boxMax[0];
    const float bminY = m_Config.boxMin[1], bmaxY = m_Config.boxMax[1];
    const float bminZ = m_Config.boxMin[2], bmaxZ = m_Config.boxMax[2];

    for (auto& p : m_CPUParticles) {
        // 半隐式欧拉积分
        p.velocity[0] += gx * dt;
        p.velocity[1] += gy * dt;
        p.velocity[2] += gz * dt;

        p.velocity[0] *= (1.0f - damping * dt);
        p.velocity[1] *= (1.0f - damping * dt);
        p.velocity[2] *= (1.0f - damping * dt);

        p.position[0] += p.velocity[0] * dt;
        p.position[1] += p.velocity[1] * dt;
        p.position[2] += p.velocity[2] * dt;

        // 边界碰撞（带弹性系数）
        float r = p.radius;
        if (p.position[0] - r < bminX) {
            p.position[0] = bminX + r;
            p.velocity[0] = -p.velocity[0] * restitution;
        }
        if (p.position[0] + r > bmaxX) {
            p.position[0] = bmaxX - r;
            p.velocity[0] = -p.velocity[0] * restitution;
        }
        if (p.position[1] - r < bminY) {
            p.position[1] = bminY + r;
            p.velocity[1] = -p.velocity[1] * restitution;
        }
        if (p.position[1] + r > bmaxY) {
            p.position[1] = bmaxY - r;
            p.velocity[1] = -p.velocity[1] * restitution;
        }
        if (p.position[2] - r < bminZ) {
            p.position[2] = bminZ + r;
            p.velocity[2] = -p.velocity[2] * restitution;
        }
        if (p.position[2] + r > bmaxZ) {
            p.position[2] = bmaxZ - r;
            p.velocity[2] = -p.velocity[2] * restitution;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 内部初始化辅助 — 通过 RHI 创建资源
// ═══════════════════════════════════════════════════════════

bool GPUPhysicsEngine::CreateParticleBuffer() {
    RHI::RHIBufferDesc desc;
    desc.size = m_Config.particleCount * sizeof(GPUParticleData);
    desc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    desc.initialData = nullptr;
    m_ParticleBuffer = m_Device->CreateBuffer(desc);
    return m_ParticleBuffer != nullptr;
}

bool GPUPhysicsEngine::CreateComputeShaders() {
    RHI::ComputePSODesc integrateDesc;
    integrateDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    m_IntegratePSO = m_Device->CreateComputePSO(integrateDesc);

    RHI::ComputePSODesc collideDesc;
    collideDesc.computeShader = StringID::Runtime("gpu_physics_collide");
    m_CollidePSO = m_Device->CreateComputePSO(collideDesc);

    return (m_IntegratePSO != nullptr && m_CollidePSO != nullptr);
}

bool GPUPhysicsEngine::CreateRenderResources() {
    RHI::RHIBufferDesc vbDesc;
    vbDesc.size = 1024 * 6 * sizeof(float);
    vbDesc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);

    RHI::RHIBufferDesc ibDesc;
    ibDesc.size = 1024 * sizeof(uint32_t);
    ibDesc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    m_IndexBuffer = m_Device->CreateBuffer(ibDesc);

    m_IndexCount = 1024;

    RHI::GraphicsPSODesc renderDesc = RHI::GraphicsPSODesc::DefaultOpaque();
    m_RenderPSO = m_Device->CreateGraphicsPSO(renderDesc);

    return (m_VertexBuffer != nullptr && m_IndexBuffer != nullptr);
}

} // namespace Engine