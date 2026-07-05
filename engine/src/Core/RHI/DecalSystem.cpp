#include "Engine/Core/RHI/DecalSystem.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

namespace Engine {

void DecalSystem::AddDecal(const Vec3& pos, const Vec3& normal,
                            std::shared_ptr<Texture> tex, const Vec3& scale)
{
    Decal d;
    d.position = pos;
    d.normal = normal;
    d.scale = scale;
    d.texture = tex;
    d.life = d.maxLife = -1;
    d.active = true;
    ComputeBasis(d);
    m_Decals.push_back(d);
}

void DecalSystem::AddDecalTimed(const Vec3& pos, const Vec3& normal,
                                 std::shared_ptr<Texture> tex,
                                 float lifeTime, const Vec3& scale)
{
    Decal d;
    d.position = pos;
    d.normal = normal;
    d.scale = scale;
    d.texture = tex;
    d.maxLife = lifeTime;
    d.life = lifeTime;
    d.active = true;
    ComputeBasis(d);
    m_Decals.push_back(d);
}

void DecalSystem::ComputeBasis(Decal& d) {
    // 从法线计算 right/up 基向量
    glm::vec3 n(d.normal.x, d.normal.y, d.normal.z);
    n = glm::normalize(n);
    glm::vec3 worldUp(0, 1, 0);
    if (std::abs(glm::dot(n, worldUp)) > 0.99f) worldUp = glm::vec3(0, 0, 1);
    glm::vec3 r = glm::normalize(glm::cross(n, worldUp));
    glm::vec3 u = glm::normalize(glm::cross(r, n));

    d.right = Vec3(r.x, r.y, r.z);
    d.up    = Vec3(u.x, u.y, u.z);
}

void DecalSystem::Update(float dt) {
    for (auto it = m_Decals.begin(); it != m_Decals.end(); ) {
        if (it->maxLife > 0) {
            it->life -= dt;
            if (it->life <= 0) { it->active = false; it = m_Decals.erase(it); continue; }
        }
        ++it;
    }
}

void DecalSystem::Render(IPrimitiveBatch& batch, Shader* shader,
                          PerspectiveCamera* camera)
{
    if (m_Decals.empty() || !shader || !camera) return;

    shader->Bind();
    const Mat4& viewMat = camera->GetViewMatrix();
    const Mat4& projMat = camera->GetProjectionMatrix();
    shader->SetMat4("u_View", viewMat.Data());
    shader->SetMat4("u_Projection", projMat.Data());

    batch.Begin(PrimitiveType::Triangles);

    for (auto& d : m_Decals) {
        if (!d.active || !d.texture) continue;

        d.texture->Bind(0);
        shader->SetInt("u_Texture", 0);
        shader->SetInt("u_HasTexture", 1);

        glm::vec3 pos(d.position.x, d.position.y, d.position.z);
        glm::vec3 r(d.right.x, d.right.y, d.right.z);
        glm::vec3 u(d.up.x, d.up.y, d.up.z);
        float sx = d.scale.x * 0.5f;
        float sy = d.scale.y * 0.5f;

        Vec3 corners[4] = {
            Vec3((pos - r*sx + u*sy).x, (pos - r*sx + u*sy).y, (pos - r*sx + u*sy).z),
            Vec3((pos - r*sx - u*sy).x, (pos - r*sx - u*sy).y, (pos - r*sx - u*sy).z),
            Vec3((pos + r*sx - u*sy).x, (pos + r*sx - u*sy).y, (pos + r*sx - u*sy).z),
            Vec3((pos + r*sx + u*sy).x, (pos + r*sx + u*sy).y, (pos + r*sx + u*sy).z),
        };
        Vec2 uvs[4] = { {0,1}, {0,0}, {1,0}, {1,1} };

        // 贴花需要深度写入，使用略微偏移防止 Z-fighting
        uint32 base = batch.GetVertexCount();
        float alpha = d.maxLife > 0 ? std::min(d.life / d.maxLife, 1.0f) : 1.0f;
        Vec4 col(1,1,1,alpha * 0.7f);

        for (int j = 0; j < 4; ++j) {
            batch.Vertex(corners[j], Vec3(0,1,0), uvs[j], col);
        }
        batch.Index(base); batch.Index(base+1); batch.Index(base+2);
        batch.Index(base); batch.Index(base+2); batch.Index(base+3);
    }

    batch.Commit();
    batch.End();
}

} // namespace Engine
