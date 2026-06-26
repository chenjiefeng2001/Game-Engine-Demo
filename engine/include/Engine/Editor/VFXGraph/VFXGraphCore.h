#pragma once

/**
 * @file VFXGraphCore.h
 * @brief VFX 图核心数据结构 — GPU 粒子系统节点图
 *
 * 类似 Unity VFX Graph / Unreal Niagara 的节点式视觉特效编辑系统。
 * 支持 GPU 粒子发射、更新、输出到屏幕。
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Engine {
namespace VFX {

    // ============================================================
    // 粒子容量
    // ============================================================
    constexpr uint32 kDefaultParticleCapacity = 65536;

    // ============================================================
    // 发射器类型
    // ============================================================
    enum class EmitterType : uint8 {
        Burst,      ///< 瞬间爆发
        Continuous, ///< 连续发射
        Distance,   ///< 距离触发
        Count
    };

    // ============================================================
    // 粒子数据布局（映射到 GPU 缓冲区）
    // ============================================================
    struct ParticleData {
        float position[3];   // float3
        float velocity[3];   // float3
        float color[4];      // float4
        float size;          // float
        float lifetime;      // float
        float age;           // float
        uint32 alive;        // bool (1 = alive)
        float pad[2];        // padding for alignment
    };

    // ============================================================
    // 发射器配置
    // ============================================================
    struct EmitterConfig {
        EmitterType type = EmitterType::Continuous;
        uint32 maxParticles = kDefaultParticleCapacity;
        float  emitRate = 100.0f;       // 每秒发射粒子数
        float  lifetime = 2.0f;          // 粒子寿命
        float  startSpeed = 5.0f;
        float  startSize = 1.0f;
        float  startColor[4] = {1,1,1,1};
        float  gravityModifier = 1.0f;
        bool   loop = true;
        bool   prewarm = false;

        // 随机范围
        float  lifetimeRandom = 0.0f;
        float  speedRandom = 0.5f;
        float  sizeRandom = 0.2f;
    };

    // ============================================================
    // 更新模块（力场）
    // ============================================================
    struct ForceField {
        float gravity = -9.8f;
        float drag = 0.0f;
        float turbulence = 0.0f;
        float noiseStrength = 0.0f;
        float wind[3] = {0,0,0};

        // 向量场（Texture3D 查找）
        std::string vectorFieldPath;
        float vectorFieldStength = 0.0f;
    };

    // ============================================================
    // 输出配置
    // ============================================================
    struct RenderConfig {
        enum class RenderMode : uint8 {
            Billboard,      ///< 面向摄像机的面片
            Mesh,           ///< 模型粒子
            Trail,          ///< 拖尾
            Stripe,         ///< 条带
            Count
        };

        RenderMode mode = RenderMode::Billboard;
        std::string texturePath;     ///< 粒子纹理
        bool depthWrite = false;
        bool depthTest = true;
        bool softParticles = true;   ///< 软粒子（避免近景硬边）
        bool castShadows = false;
        bool receiveShadows = false;

        // 排序
        enum class SortMode : uint8 {
            None,       ///< 无排序（快）
            Distance,   ///< 按距离排序（半透明正确）
            Age         ///< 按年龄排序
        };
        SortMode sortMode = SortMode::Distance;
    };

    // ============================================================
    // VFX 实例（运行时）
    // ============================================================
    class VFXInstance {
    public:
        VFXInstance();
        ~VFXInstance();

        void SetConfig(const EmitterConfig& cfg) { m_Config = cfg; }
        void SetForceField(const ForceField& ff) { m_Force = ff; }
        void SetRenderConfig(const RenderConfig& rc) { m_RenderCfg = rc; }

        const EmitterConfig& GetConfig() const { return m_Config; }
        const ForceField& GetForceField() const { return m_Force; }
        const RenderConfig& GetRenderConfig() const { return m_RenderCfg; }

        void Play()  { m_Playing = true; }
        void Stop()  { m_Playing = false; }
        void Pause() { m_Paused = !m_Paused; }

        bool IsPlaying() const { return m_Playing; }
        bool IsPaused() const { return m_Paused; }

        /// 每帧更新
        void OnUpdate(float dt);

        /// 获取 GPU buffer 数据指针
        const ParticleData* GetParticleBuffer() const { return m_Particles.data(); }
        uint32 GetParticleCount() const { return m_AliveCount; }

        /// 重置
        void Clear();

    private:
        EmitterConfig m_Config;
        ForceField m_Force;
        RenderConfig m_RenderCfg;

        std::vector<ParticleData> m_Particles;
        uint32 m_AliveCount = 0;
        float m_EmitAccum = 0.0f;
        bool m_Playing = false;
        bool m_Paused = false;
    };

    // ============================================================
    // VFX 管理器
    // ============================================================
    class VFXManager {
    public:
        static VFXManager& Get() {
            static VFXManager instance;
            return instance;
        }

        uint32 Spawn(std::unique_ptr<VFXInstance> vfx);
        void   Despawn(uint32 id);
        VFXInstance* Get(uint32 id);

        void OnUpdate(float dt);
        void Clear();

        const std::unordered_map<uint32, std::unique_ptr<VFXInstance>>& GetAll() const { return m_Instances; }

    private:
        VFXManager() = default;
        std::unordered_map<uint32, std::unique_ptr<VFXInstance>> m_Instances;
        uint32 m_NextId = 1;
    };

}} // namespace Engine::VFX