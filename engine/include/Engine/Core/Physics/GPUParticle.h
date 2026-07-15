#pragma once

/**
 * @file GPUParticle.h
 * @brief GPU 物理引擎 MVP — 基于 Compute Shader 的粒子/球体离散元模拟
 *
 * 设计目标：
 *   1. 所有物理数据永久驻留 GPU VRAM，零回读
 *   2. Compute Shader 负责积分 + 碰撞
 *   3. Graphics Shader 直接读取 SSBO 做实例化渲染 (Zero-Copy)
 *   4. 验证引擎的 Barrier / Async Compute 基础架构
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

namespace Engine {

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
     * @param config 配置参数
     * @return 是否成功
     *
     * 初始化步骤：
     *   1. 创建粒子 SSBO（Compute Write + Vertex Read）
     *   2. 编译 Compute Shader（积分 Pass + 碰撞 Pass）
     *   3. 初始化粒子数据（随机位置/速度）
     *   4. 创建实例化渲染管线
     */
    bool Initialize(const GPUPhysicsConfig& config);

    /** 关闭并释放所有 GPU 资源 */
    void Shutdown();

    // ── 每帧更新 ──
    /**
     * @brief 执行 GPU 物理模拟
     * @param dt 时间步长（秒）
     *
     * 执行顺序：
     *   1. 绑定积分 Compute Shader + Dispatch
     *   2. 屏障: ComputeWrite → ComputeRead
     *   3. 绑定碰撞 Compute Shader + Dispatch
     *   4. 屏障: ComputeWrite → VertexRead
     */
    void Update(float dt);

    // ── 渲染 ──
    /**
     * @brief 渲染所有粒子（实例化绘制）
     *
     * 直接绑定粒子 SSBO 作为 Instance Buffer，
     * 无需任何 CPU 回读。
     */
    void Render();

    // ── 查询 ──
    bool IsValid() const { return m_Initialized; }
    const GPUPhysicsConfig& GetConfig() const { return m_Config; }
    const GPUPhysicsStats& GetStats() const { return m_Stats; }
    uint32_t GetParticleCount() const { return m_Config.particleCount; }

    /** 获取 SSBO 句柄（供外部绑定） */
    uint32_t GetParticleSSBO() const { return m_SSBO; }

    /** 重置所有粒子到初始状态 */
    void ResetParticles();

private:
    // ── 内部初始化辅助 ──
    bool CreateParticleBuffer();
    bool CompileComputeShaders();
    bool CompileRenderShader();
    bool CreateSphereMesh();

    // ── 状态 ──
    bool m_Initialized = false;
    GPUPhysicsConfig m_Config;
    GPUPhysicsStats  m_Stats;

    // ── GPU 资源（OpenGL SSBO + 管线） ──
    uint32_t m_SSBO = 0;               // 粒子数据 SSBO
    uint32_t m_VAO  = 0;               // 球体网格 VAO
    uint32_t m_VBO  = 0;               // 球体网格 VBO
    uint32_t m_IBO  = 0;               // 球体网格 IBO
    uint32_t m_IndexCount = 0;         // 球体网格索引数

    uint32_t m_IntegrateProgram = 0;   // 积分 Compute Shader
    uint32_t m_CollideProgram   = 0;   // 碰撞 Compute Shader
    uint32_t m_RenderProgram    = 0;   // 渲染 Shader (实例化)

    // ── 时间统计 ──
    float m_ElapsedTime = 0.0f;
};

} // namespace Engine