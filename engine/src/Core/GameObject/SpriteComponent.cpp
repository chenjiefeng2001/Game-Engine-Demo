#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cmath>

namespace Engine {

    // ────────────────────────────────────────────────────────────
    // 构造
    // ────────────────────────────────────────────────────────────
    SpriteComponent::SpriteComponent()
        : m_Texture(nullptr) {
    }

    SpriteComponent::SpriteComponent(std::shared_ptr<Texture> texture)
        : m_Texture(std::move(texture)) {
    }

    SpriteComponent::SpriteComponent(std::shared_ptr<Texture> texture,
                                     const Vec4& color)
        : m_Texture(std::move(texture))
        , m_Color(color) {
    }

    SpriteComponent::SpriteComponent(TextureManager& texMgr, const std::string& path)
        : m_Texture(texMgr.Load(path)) {
    }

    SpriteComponent::SpriteComponent(TextureManager& texMgr, const std::string& path,
                                     const Vec4& color)
        : m_Texture(texMgr.Load(path))
        , m_Color(color) {
    }

    void SpriteComponent::SetTexture(TextureManager& texMgr, const std::string& path) {
        m_Texture = texMgr.Load(path);
    }

    SpriteData SpriteComponent::ToSpriteData(const float32* worldMatrix) const {
        SpriteData data;

        // 将 float[16] 转为 glm::mat4 进行计算
        glm::mat4 m;
        std::memcpy(&m, worldMatrix, sizeof(float32) * 16);

        // ── 从矩阵提取位置 ──
        data.transform.x = m[3][0];
        data.transform.y = m[3][1];

        float32 sx = glm::length(glm::vec3(m[0]));
        float32 sy = glm::length(glm::vec3(m[1]));

        // 归一化后取 atan2(up.x, right.x) 得到 Z 轴旋转
        glm::vec3 right(
            m[0][0] / sx,
            m[0][1] / sx,
            m[0][2] / sx
        );
        glm::vec3 up(
            m[1][0] / sy,
            m[1][1] / sy,
            m[1][2] / sy
        );
        data.transform.angle = std::atan2(up.x, right.x);

        // ── 缩放 ──
        data.transform.scaleX = sx;
        data.transform.scaleY = sy;

        // ── UV ──
        data.uvX = m_UVX;
        data.uvY = m_UVY;
        data.uvW = m_UVW;
        data.uvH = m_UVH;

        // ── 颜色 ──
        data.colorR = m_Color.x;
        data.colorG = m_Color.y;
        data.colorB = m_Color.z;
        data.colorA = m_Color.w;

        return data;
    }

    // ============================================================
    // CollectRenderCommands — RHI 渲染接口
    // ============================================================

    void SpriteComponent::CollectRenderCommands(IRenderQueue& queue) {
        if (!m_Visible || !IsEnabled())
            return;

        auto* owner = GetOwner();
        if (!owner)
            return;

        RenderCommand cmd;

        // 世界矩阵
        const Mat4& world = owner->GetTransform().GetWorldMatrix();
        std::memcpy(cmd.worldMatrix, world.Data(), sizeof(cmd.worldMatrix));

        // UV
        cmd.uv[0] = m_UVX;
        cmd.uv[1] = m_UVY;
        cmd.uv[2] = m_UVW;
        cmd.uv[3] = m_UVH;

        // 颜色
        cmd.color[0] = m_Color.x;
        cmd.color[1] = m_Color.y;
        cmd.color[2] = m_Color.z;
        cmd.color[3] = m_Color.w;

        // 纹理
        cmd.texture = m_Texture;

        // 排序
        cmd.sortingLayer = m_SortingLayer;
        cmd.orderInLayer = m_OrderInLayer;

        queue.Push(cmd);
    }

} // namespace Engine
