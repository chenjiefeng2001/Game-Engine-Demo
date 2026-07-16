#pragma once

/**
 * @file GPUParticle.h
 * @brief GPU 物理引擎 MVP — 基于 Compute Shader 的粒子/球体离散元模拟
 *
 * 设计原则：
 *   - 纯 RHI 抽象层：GPUPhysicsEngine 不包含任何 CPU 物理模拟代码
 *   - 影子内存：Stub 模式下使用 std::vector<uint8_t> 后备存储，
 *     使 Upload→Readback 管线在无 GPU 环境下也可验证
 *   - CPU 端粒子物理验证由外部测试独立实现（见 sandbox CPUSimulator）
 */

#include "Engine/Types.h"
#include <cstdint>
#include <vector>
#include <string>
#include <cstddef> // offsetof
#include <memory>

namespace Engine {

namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHIBuffer;
    class IRHIPipelineState;
}

// ═══════════════════════════════════════════════════════════
// 粒子数据结构（CPU 与 GPU 共享，std430 对齐）
// ═══════════════════════════════════════════════════════════
#pragma pack(push, 4)
struct GPUParticleData {
    float position[3];   // offset 0,  12 bytes
    float radius;        // offset 12, 4 bytes
    float velocity[3];   // offset 16, 12 bytes
    float mass;          // offset 28, 4 bytes
    float color[4];      // offset 32, 16 bytes
    float padding[4];    // offset 48, 16 bytes  → total 64 bytes
};
#pragma pack(pop)

// 编译期校验 std430 布局（与 GLSL std430 对齐规则一致）
static_assert(offsetof(GPUParticleData, position) == 0,  "pos must be at offset 0");
static_assert(offsetof(GPUParticleData, radius)   == 12, "rad must be at offset 12");
static_assert(offsetof(GPUParticleData, velocity) == 16, "vel must be at offset 16");
static_assert(offsetof(GPUParticleData, mass)     == 28, "mass must be at offset 28");
static_assert(offsetof(GPUParticleData, color)    == 32, "color must be at offset 32");
static_assert(sizeof(GPUParticleData) == 64,             "GPUParticleData must be exactly 64 bytes");

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎配置
// ═══════════════════════════════════════════════════════════
struct GPUPhysicsConfig {
    uint32_t particleCount  = 65536;
    float    gravity[3]     = {0.0f, -9.8f, 0.0f};
    float    restitution    = 0.8f;
    float    stiffness      = 1000.0f;
    float    damping        = 0.02f;
    float    boxMin[3]      = {-50.0f, 0.0f, -50.0f};
    float    boxMax[3]      = {50.0f, 100.0f, 50.0f};
    float    spawnVelocity[3] = {0.0f, 0.0f, 0.0f};
    float    spawnRadius    = 30.0f;
    uint32_t workGroupSize = 256;
};

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎统计
// ═══════════════════════════════════════════════════════════
struct GPUPhysicsStats {
    uint32_t totalParticles  = 0;
    uint32_t activeParticles = 0;
    float    avgFPS          = 0.0f;
    float    computeTimeMs   = 0.0f;
    uint64_t frameCount      = 0;
};

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎（主类）
// ═══════════════════════════════════════════════════════════
class GPUPhysicsEngine {
public:
    GPUPhysicsEngine();
    ~GPUPhysicsEngine();

    GPUPhysicsEngine(const GPUPhysicsEngine&) = delete;
    GPUPhysicsEngine& operator=(const GPUPhysicsEngine&) = delete;

    bool Initialize(RHI::IRHIDevice* device, const GPUPhysicsConfig& config);
    void Shutdown();

    void Update(float dt, RHI::IRHICommandList* cmdList);
    void Render(RHI::IRHICommandList* renderCmdList);

    bool IsValid() const { return m_Initialized; }
    const GPUPhysicsConfig& GetConfig() const { return m_Config; }
    const GPUPhysicsStats& GetStats() const { return m_Stats; }
    uint32_t GetParticleCount() const { return m_Config.particleCount; }
    RHI::IRHIBuffer* GetParticleBuffer() const { return m_ParticleBuffer.get(); }

    void UploadInitialData(const GPUParticleData* data, uint32_t count);
    void ReadbackParticles(uint32_t startIndex, uint32_t count, GPUParticleData* outData);
    void ResetParticles();

private:
    bool CreateParticleBuffer();
    bool CreateComputeShaders();
    bool CreateRenderResources();

    // ── RHI Buffer（GL46Buffer 持久映射在 stub 模式下也分配内存） ──
    std::shared_ptr<RHI::IRHIBuffer> m_ParticleBuffer;

    // ── 其余成员变量 ──
    bool m_Initialized = false;
    GPUPhysicsConfig m_Config;
    GPUPhysicsStats  m_Stats;
    RHI::IRHIDevice* m_Device = nullptr;

    std::shared_ptr<RHI::IRHIBuffer> m_VertexBuffer;
    std::shared_ptr<RHI::IRHIBuffer> m_IndexBuffer;
    RHI::IRHIPipelineState* m_IntegratePSO = nullptr;
    RHI::IRHIPipelineState* m_CollidePSO   = nullptr;
    RHI::IRHIPipelineState* m_RenderPSO    = nullptr;

    uint32_t m_IndexCount = 0;
    float m_ElapsedTime = 0.0f;
};

} // namespace Engine