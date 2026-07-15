#pragma once

/**
 * @file GPUParticle.h
 * @brief GPU 物理引擎 MVP — 基于 Compute Shader 的粒子/球体离散元模拟
 *
 * 设计目标：
 *   1. 所有物理数据永久驻留 GPU VRAM，零回读
 *   2. Compute Shader 负责积分 + 碰撞
 *   3. Graphics Shader 直接读取 SSBO 做实例化渲染 (Zero-Copy)
 *   4. 通过 RHI 抽象层与具体 API 解耦
 *
 * 设计变更（v2.0）：
 *   - 移除所有 OpenGL 直接调用，改用 Engine::RHI 抽象接口
 *   - Buffer 创建：IRHIDevice::CreateBuffer()
 *   - Compute 调度：IRHICommandList::Dispatch() + SetUnorderedAccess()
 *   - 屏障：IRHICommandList::ResourceBarrier()
 *   - Shader 创建：IRHIDevice::CreateComputePSO()
 *
 * 管线流程：
 *   CPU 端：提交 Dispatch + 设置时间步长
 *   Pass 1 (积分): 半隐式欧拉积分、重力、边界反弹
 *   Pass 2 (碰撞): 粒子间碰撞检测 + 惩罚力响应
 *   屏障: ComputeWrite → ComputeRead | GraphicsRead
 *   渲染: 实例化绘制 (DrawInstanced)
 */

#include "Engine/Types.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace Engine {

// 前向声明 RHI 类型
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
    float position[3];   // 位置 (xyz)
    float radius;        // 半径
    float velocity[3];   // 速度 (xyz)
    float mass;          // 质量
    float color[4];      // 颜色 (rgba)
    float padding[4];    // 填充至 64 bytes（确保 16 字节对齐）
};
#pragma pack(pop)

static_assert(sizeof(GPUParticleData) == 64,
    "GPUParticleData must be 64 bytes for std430 alignment");

// ═══════════════════════════════════════════════════════════
// GPU 物理引擎配置
// ═══════════════════════════════════════════════════════════
struct GPUPhysicsConfig {
    uint32_t particleCount  = 65536;    // 粒子总数
    float    gravity[3]     = {0.0f, -9.8f, 0.0f};  // 重力
    float    restitution    = 0.8f;     // 边界弹性系数
    float    stiffness      = 1000.0f;  // 粒子间碰撞弹簧刚度
    float    damping        = 0.02f;    // 速度阻尼
    float    boxMin[3]      = {-50.0f, 0.0f, -50.0f}; // 边界最小值
    float    boxMax[3]      = {50.0f, 100.0f, 50.0f}; // 边界最大值
    float    spawnVelocity[3] = {0.0f, 5.0f, 0.0f};   // 初始速度
    float    spawnRadius    = 30.0f;     // 生成区域半径
    uint32_t workGroupSize = 256;       // Compute Shader 工作组大小
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

    // ── 生命周期 ──
    /**
     * @brief 初始化 GPU 物理引擎
     * @param device RHI 设备（用于创建 Buffer/PSO）
     * @param config 配置参数
     * @return 是否成功
     */
    bool Initialize(RHI::IRHIDevice* device, const GPUPhysicsConfig& config);

    /** 关闭并释放所有 GPU 资源 */
    void Shutdown();

    // ── 每帧更新 ──
    /**
     * @brief 执行 GPU 物理模拟
     * @param dt 时间步长（秒）
     * @param cmdList RHI 命令列表（用于录制 Dispatch/Barrier）
     */
    void Update(float dt, RHI::IRHICommandList* cmdList);

    // ── 渲染 ──
    /**
     * @brief 渲染所有粒子（实例化绘制）
     * @param renderCmdList RHI 命令列表（用于录制 Draw 命令）
     */
    void Render(RHI::IRHICommandList* renderCmdList);

    // ── 查询 ──
    bool IsValid() const { return m_Initialized; }
    const GPUPhysicsConfig& GetConfig() const { return m_Config; }
    const GPUPhysicsStats& GetStats() const { return m_Stats; }
    uint32_t GetParticleCount() const { return m_Config.particleCount; }

    /** 获取 SSBO 句柄（供外部绑定） */
    RHI::IRHIBuffer* GetParticleBuffer() const { return m_ParticleBuffer.get(); }

    /** 获取存储的 RHI 设备指针 */
    RHI::IRHIDevice* GetDevice() const { return m_Device; }

    /**
     * @brief 从 GPU 回读粒子数据（用于调试/验证）
     * @param startIndex 起始粒子索引
     * @param count 回读粒子数量
     * @param outData 输出缓冲区（需预分配至少 count 个元素）
     */
    void ReadbackParticles(uint32_t startIndex, uint32_t count, GPUParticleData* outData);

    /** 重置所有粒子到初始状态 */
    void ResetParticles();

private:
    // ── 内部初始化辅助 ──
    bool CreateParticleBuffer();
    bool CreateComputeShaders();
    bool CreateRenderResources();

    // ── 状态 ──
    bool m_Initialized = false;
    GPUPhysicsConfig m_Config;
    GPUPhysicsStats  m_Stats;

    // ── RHI 设备（不拥有） ──
    RHI::IRHIDevice* m_Device = nullptr;

    // ── CPU 端粒子缓存（用于回读，GPU Compute 完善前代替验证） ──
    std::vector<GPUParticleData> m_CPUParticles;
    void SimulateCPUParticles(float dt);

    // ── RHI 资源 ──
    std::shared_ptr<RHI::IRHIBuffer> m_ParticleBuffer;       // 粒子数据 SSBO
    std::shared_ptr<RHI::IRHIBuffer> m_VertexBuffer;         // 球体网格 VBO
    std::shared_ptr<RHI::IRHIBuffer> m_IndexBuffer;          // 球体网格 IBO
    RHI::IRHIPipelineState* m_IntegratePSO = nullptr;        // 积分 Compute PSO
    RHI::IRHIPipelineState* m_CollidePSO   = nullptr;        // 碰撞 Compute PSO
    RHI::IRHIPipelineState* m_RenderPSO    = nullptr;        // 渲染 Graphics PSO

    uint32_t m_IndexCount = 0;         // 球体网格索引数
    float m_ElapsedTime = 0.0f;
};

} // namespace Engine