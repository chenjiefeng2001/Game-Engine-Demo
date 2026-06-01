#pragma once
/**
 * @file ParticleSystem.h
 * @brief 简单粒子系统 — 发射器 + 批渲染
 *
 * 粒子使用 IPrimitiveBatch 批量渲染为公告板四边形。
 * 支持：重力、随机速度/颜色/大小、生命周期、纹理
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/IGraphicsFactory.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <random>

namespace Engine {

    class Texture;
    class IPrimitiveBatch;
    class Shader;

    struct Particle {
        Vec3  position;
        Vec3  velocity;
        Vec4  color;
        Vec4  startColor;
        Vec4  endColor;
        float size;
        float startSize;
        float endSize;
        float life;
        float maxLife;
        bool  active = false;
    };

    struct ParticleEmitterConfig {
        Vec3  position    = {0, 0, 0};
        Vec3  positionVar = {1, 0.5f, 1};    // 位置随机范围
        Vec3  velocity    = {0, 2, 0};
        Vec3  velocityVar = {1, 1, 1};        // 速度随机范围
        Vec4  startColor  = {1, 1, 1, 1};
        Vec4  endColor    = {0.5f, 0.5f, 1, 0};
        float startSize  = 0.5f;
        float endSize    = 0.1f;
        float lifeTime   = 2.0f;
        float lifeVar    = 1.0f;
        float emitRate   = 30.0f;             // 每秒发射数
        int   maxParticles = 500;
        bool  looping    = true;
        bool  localSpace = false;             // 是否使用局部空间
    };

    class ParticleEmitter {
    public:
        ParticleEmitter();
        ~ParticleEmitter() = default;

        void SetConfig(const ParticleEmitterConfig& cfg) { m_Config = cfg; }
        const ParticleEmitterConfig& GetConfig() const { return m_Config; }

        void Update(float dt);
        void Render(IPrimitiveBatch& batch, Shader* shader,
                    const Mat4& viewMatrix, const Mat4& projMatrix);

        void SetTexture(std::shared_ptr<Texture> tex) { m_Texture = tex; }
        std::shared_ptr<Texture> GetTexture() const { return m_Texture; }

        void SetPosition(const Vec3& pos) { m_Config.position = pos; }
        Vec3 GetPosition() const { return m_Config.position; }

        int GetActiveCount() const { return m_ActiveCount; }

    private:
        void Emit(int count);

        ParticleEmitterConfig m_Config;
        std::vector<Particle> m_Particles;
        int  m_ActiveCount = 0;
        float m_EmitAccum = 0;
        std::mt19937 m_RNG;
        std::shared_ptr<Texture> m_Texture;
    };

} // namespace Engine
