#include "Engine/Core/RHI/ParticleSystem.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

namespace Engine {

ParticleEmitter::ParticleEmitter()
    : m_RNG(std::random_device{}())
{
    m_Particles.resize(m_Config.maxParticles);
}

void ParticleEmitter::Emit(int count) {
    std::uniform_real_distribution<float> dist01(0, 1);
    for (int i = 0; i < count && m_ActiveCount < m_Config.maxParticles; ++i) {
        // 找一个空闲槽位
        int idx = -1;
        for (int j = 0; j < (int)m_Particles.size(); ++j) {
            if (!m_Particles[j].active) { idx = j; break; }
        }
        if (idx < 0) break;

        auto& p = m_Particles[idx];
        const auto& c = m_Config;
        float rx = dist01(m_RNG) * 2 - 1;
        float ry = dist01(m_RNG) * 2 - 1;
        float rz = dist01(m_RNG) * 2 - 1;

        p.position = Vec3(
            c.position.x + c.positionVar.x * rx,
            c.position.y + c.positionVar.y * ry,
            c.position.z + c.positionVar.z * rz);
        p.velocity = Vec3(
            c.velocity.x + c.velocityVar.x * rx,
            c.velocity.y + c.velocityVar.y * ry,
            c.velocity.z + c.velocityVar.z * rz);
        p.startColor = c.startColor;
        p.endColor   = c.endColor;
        p.color      = c.startColor;
        p.startSize  = c.startSize;
        p.endSize    = c.endSize;
        p.size       = c.startSize;
        p.maxLife    = c.lifeTime + c.lifeVar * (dist01(m_RNG) * 2 - 1);
        if (p.maxLife < 0.1f) p.maxLife = 0.1f;
        p.life       = p.maxLife;
        p.active     = true;
        ++m_ActiveCount;
    }
}

void ParticleEmitter::Update(float dt) {
    const auto& c = m_Config;

    // 发射新粒子
    if (c.looping) {
        m_EmitAccum += dt * c.emitRate;
        int toEmit = (int)m_EmitAccum;
        if (toEmit > 0) { Emit(toEmit); m_EmitAccum -= toEmit; }
    }

    // 更新现有粒子
    for (auto& p : m_Particles) {
        if (!p.active) continue;
        p.life -= dt;
        if (p.life <= 0) { p.active = false; --m_ActiveCount; continue; }

        float t = 1.0f - p.life / p.maxLife; // 0→1
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
        // 简单重力
        p.velocity.y -= 4.0f * dt;

        // 插值颜色和大小
        p.color.x = p.startColor.x + (p.endColor.x - p.startColor.x) * t;
        p.color.y = p.startColor.y + (p.endColor.y - p.startColor.y) * t;
        p.color.z = p.startColor.z + (p.endColor.z - p.startColor.z) * t;
        p.color.w = p.startColor.w + (p.endColor.w - p.startColor.w) * t;
        p.size    = p.startSize  + (p.endSize - p.startSize) * t;
    }
}

void ParticleEmitter::Render(IPrimitiveBatch& batch, Shader* shader,
                              const Mat4& viewMatrix, const Mat4& projMatrix)
{
    if (m_ActiveCount <= 0 || !shader) return;

    shader->Bind();
    shader->SetMat4("u_View", viewMatrix.Data());
    shader->SetMat4("u_Projection", projMatrix.Data());

    if (m_Texture) {
        m_Texture->Bind(0);
        shader->SetInt("u_Texture", 0);
        shader->SetInt("u_HasTexture", 1);
    } else {
        shader->SetInt("u_HasTexture", 0);
    }

    // 从视图矩阵提取相机 right/up 向量（公告板）
    glm::mat4 viewGlm = glm::make_mat4(viewMatrix.Data());
    glm::vec3 camRight = glm::vec3(viewGlm[0][0], viewGlm[1][0], viewGlm[2][0]);
    glm::vec3 camUp    = glm::vec3(viewGlm[0][1], viewGlm[1][1], viewGlm[2][1]);

    batch.Begin(PrimitiveType::Triangles);

    for (auto& p : m_Particles) {
        if (!p.active) continue;
        float hs = p.size * 0.5f;

        glm::vec3 pos(p.position.x, p.position.y, p.position.z);
        glm::vec3 r = camRight * hs;
        glm::vec3 u = camUp * hs;

        // 公告板四边形（4 个角）
        Vec3 corners[4] = {
            Vec3((pos - r + u).x, (pos - r + u).y, (pos - r + u).z),  // 左上
            Vec3((pos - r - u).x, (pos - r - u).y, (pos - r - u).z),  // 左下
            Vec3((pos + r - u).x, (pos + r - u).y, (pos + r - u).z),  // 右下
            Vec3((pos + r + u).x, (pos + r + u).y, (pos + r + u).z),  // 右上
        };
        Vec2 uvs[4] = { {0,1}, {0,0}, {1,0}, {1,1} };

        uint32 base = batch.GetVertexCount();
        for (int j = 0; j < 4; ++j) {
            batch.Vertex(corners[j], Vec3(0,1,0), uvs[j], p.color);
        }
        batch.Index(base); batch.Index(base+1); batch.Index(base+2);
        batch.Index(base); batch.Index(base+2); batch.Index(base+3);
    }

    batch.Commit();
    batch.End();
}

} // namespace Engine
