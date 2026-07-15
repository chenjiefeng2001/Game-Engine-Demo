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

    // 4. 初始化粒子数据
    ResetParticles();

    m_Initialized = true;
    s_Log.Info("GPU Physics Engine initialized via RHI successfully");
    return true;
}

void GPUPhysicsEngine::Shutdown() {
    if (!m_Initialized) return;

    // RHI 资源由 shared_ptr 自动释放，PSO 需要返回给设备
    // 当前实现中 GL46PipelineState 使用裸指针 new，需要 delete
    delete m_IntegratePSO; m_IntegratePSO = nullptr;
    delete m_CollidePSO;   m_CollidePSO = nullptr;
    delete m_RenderPSO;    m_RenderPSO = nullptr;

    m_ParticleBuffer.reset();
    m_VertexBuffer.reset();
    m_IndexBuffer.reset();
    m_IndexCount = 0;
    m_Initialized = false;
    m_Stats = {};

    s_Log.Info("GPU Physics Engine shutdown");
}

void GPUPhysicsEngine::Update(float dt, RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList) return;

    m_Stats.frameCount++;

    // Pass 1: 积分（半隐式欧拉 + 边界碰撞）
    cmdList->SetPipelineState(m_IntegratePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());

    // 通过 RHI Barrier 确保数据就绪
    RHI::ResourceBarrierDesc preBarrier;
    preBarrier.type = RHI::ResourceBarrierDesc::Type::UAV;
    preBarrier.buffer = m_ParticleBuffer.get();
    preBarrier.stateBefore = RHI::ResourceState::UnorderedAccess;
    preBarrier.stateAfter  = RHI::ResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(1, &preBarrier);

    // Dispatch 积分 Pass
    uint32_t groupCount = (m_Config.particleCount + m_Config.workGroupSize - 1)
                          / m_Config.workGroupSize;
    cmdList->Dispatch(groupCount, 1, 1);

    // 屏障: ComputeWrite → ComputeRead（确保积分完成后碰撞才能读）
    cmdList->ResourceBarrier(1, &preBarrier);

    // Pass 2: 碰撞检测 + 惩罚力响应
    cmdList->SetPipelineState(m_CollidePSO);
    cmdList->SetUnorderedAccess(0, m_ParticleBuffer.get());
    cmdList->Dispatch(groupCount, 1, 1);

    // 屏障: ComputeWrite → VertexRead（确保碰撞完成后渲染才能读取）
    cmdList->ResourceBarrier(1, &preBarrier);
}

void GPUPhysicsEngine::Render(RHI::IRHICommandList* cmdList) {
    if (!m_Initialized || !cmdList || m_Stats.frameCount == 0) return;

    cmdList->SetPipelineState(m_RenderPSO);
    cmdList->SetVertexBuffer(0, m_VertexBuffer.get(), 6 * sizeof(float), 0);
    cmdList->SetIndexBuffer(m_IndexBuffer.get(), 0);
    cmdList->SetPrimitiveTopology(RHI::PrimitiveTopology::TriangleList);

    // 实例化绘制（每个粒子一个实例）
    // 注意：实例数据从 m_ParticleBuffer SSBO 读取，需要绑定到 slot
    RHI::Viewport vp = { 0, 0, 1280, 720, 0, 1 };
    cmdList->SetViewport(vp);
    cmdList->DrawIndexed(m_IndexCount, 0, 0);
}

void GPUPhysicsEngine::ResetParticles() {
    if (!m_ParticleBuffer) return;

    // 在 CPU 上生成初始数据
    std::vector<GPUParticleData> initialData(m_Config.particleCount);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-m_Config.spawnRadius, m_Config.spawnRadius);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    for (uint32_t i = 0; i < m_Config.particleCount; ++i) {
        auto& p = initialData[i];
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

    // 通过 UpdateData 上传初始数据（需要 RHI Buffer 支持映射）
    // 当前 GL46Buffer 实现为存根，数据上传在 GL46Device 完善后生效
    s_Log.Info("Particle data generated: {} particles", m_Config.particleCount);
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
    // 通过 RHI 创建 Compute PSO
    // 实际着色器编译由 ShaderCompiler + PSOCache 完成
    // 这里使用 StringID 空值作为占位符，真实实现需接入 ShaderCompiler
    RHI::ComputePSODesc integrateDesc;
    integrateDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    m_IntegratePSO = m_Device->CreateComputePSO(integrateDesc);

    RHI::ComputePSODesc collideDesc;
    collideDesc.computeShader = StringID::Runtime("gpu_physics_collide");
    m_CollidePSO = m_Device->CreateComputePSO(collideDesc);

    return (m_IntegratePSO != nullptr && m_CollidePSO != nullptr);
}

bool GPUPhysicsEngine::CreateRenderResources() {
    // 创建球体网格缓冲区（VBO/IBO）
    // 简化实现：创建空 Buffer 占位
    RHI::RHIBufferDesc vbDesc;
    vbDesc.size = 1024 * 6 * sizeof(float);  // 球体顶点近似大小
    vbDesc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    m_VertexBuffer = m_Device->CreateBuffer(vbDesc);

    RHI::RHIBufferDesc ibDesc;
    ibDesc.size = 1024 * sizeof(uint32_t);
    ibDesc.memoryUsage = RHI::MemoryUsage::GPU_Only;
    m_IndexBuffer = m_Device->CreateBuffer(ibDesc);

    m_IndexCount = 1024;

    // 创建渲染 PSO（占位）
    RHI::GraphicsPSODesc renderDesc = RHI::GraphicsPSODesc::DefaultOpaque();
    m_RenderPSO = m_Device->CreateGraphicsPSO(renderDesc);

    return (m_VertexBuffer != nullptr && m_IndexBuffer != nullptr);
}

} // namespace Engine