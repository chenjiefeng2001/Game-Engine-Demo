#pragma once

/**
 * @file GPUParticleSystem.h
 * @brief GPU 粒子系统 — Compute Shader 驱动的海量粒子
 *
 * 设计要点：
 *   - 所有粒子物理在 Compute Shader 中运行，GPU 零回读
 *   - 使用 Depth Buffer 进行碰撞检测（屏幕空间深度图碰撞）
 *   - 支持 Spawn/Update/Render 三阶段管线
 *   - 粒子数据存储在 SSBO (Shader Storage Buffer Object) 中
 *
 * 架构定位：
 *   CPU 负责：生成粒子、更新 spawn rate、管理生命周期
 *   GPU 负责：物理积分、碰撞检测、渲染
 *
 * 与 CPU ParticleSystem 的关系：
 *   CPU ParticleSystem (保留) — 用于小型/逻辑敏感的粒子（<1000）
 *   GPU ParticleSystem (新增) — 用于大规模视觉特效（>10000）
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <cstdint>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// GPU 粒子系统配置
// ═══════════════════════════════════════════════════════════
struct GPUParticleConfig {
    uint32_t maxParticles  = 65536;    // 最大粒子数
    float    spawnRate     = 1000.0f;  // 每秒生成数
    float    lifetime      = 3.0f;     // 粒子存活时间（秒）
    Vec3     gravity       = {0, -9.81f, 0};
    float    bounce        = 0.3f;     // 反弹系数
    Vec3     spawnArea     = {5, 5, 5};// 生成区域大小
    Vec3     initialVelocity = {0, 5, 0}; // 初始速度
    float    particleSize  = 0.1f;     // 粒子大小
};

// ═══════════════════════════════════════════════════════════
// GPU 粒子系统管理器
// ═══════════════════════════════════════════════════════════
class GPUParticleSystem {
public:
    GPUParticleSystem();
    ~GPUParticleSystem();

    GPUParticleSystem(const GPUParticleSystem&) = delete;
    GPUParticleSystem& operator=(const GPUParticleSystem&) = delete;

    // ── 初始化/关闭 ──
    bool Initialize(const GPUParticleConfig& config);
    void Shutdown();

    // ── 每帧更新（CPU 端逻辑） ──
    void Update(float32 dt);
    
    /** 生成一批新粒子 */
    void Spawn(uint32_t count);

    /** 获取当前活跃粒子数 */
    uint32_t GetActiveCount() const { return m_ActiveCount; }

    /** 获取最大粒子数 */
    uint32_t GetMaxParticles() const { return m_Config.maxParticles; }

    /** 渲染所有活跃粒子 */
    void Render(const Mat4& viewProj);

    /** 获取 SSBO 句柄（供 Compute Shader 和渲染使用） */
    uint32_t GetParticleSSBO() const { return m_SSBO; }

    /** 是否已初始化 */
    bool IsValid() const { return m_Initialized; }

    // ── 配置 ──
    const GPUParticleConfig& GetConfig() const { return m_Config; }
    void SetSpawnRate(float rate) { m_Config.spawnRate = rate; }

private:
    bool m_Initialized = false;
    GPUParticleConfig m_Config;

    // OpenGL SSBO 句柄
    uint32_t m_SSBO = 0;         // Shader Storage Buffer Object
    uint32_t m_VAO = 0;          // 粒子渲染用 VAO
    uint32_t m_ComputeProgram = 0; // Compute Shader 程序
    uint32_t m_RenderProgram = 0;  // 粒子渲染程序

    // CPU 端管理
    uint32_t m_ActiveCount = 0;
    float   m_SpawnAccumulator = 0.0f;

    // 时间
    float   m_ElapsedTime = 0.0f;
};

} // namespace Engine