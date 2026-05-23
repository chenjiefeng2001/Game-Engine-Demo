#include "Engine/Core/GameObject/SpriteComponent.h"
#include <glm/gtc/matrix_transform.hpp>

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
                                     const glm::vec4& color)
        : m_Texture(std::move(texture))
        , m_Color(color) {
    }

    SpriteData SpriteComponent::ToSpriteData(const glm::mat4& worldMatrix) const {
        SpriteData data;

        // ── 从矩阵提取位置 ──
        data.transform.x = worldMatrix[3][0];
        data.transform.y = worldMatrix[3][1];

        float sx = glm::length(glm::vec3(worldMatrix[0]));
        float sy = glm::length(glm::vec3(worldMatrix[1]));
        // 归一化后取 atan2(up.x, right.x) 得到 Z 轴旋转
        glm::vec3 right(
            worldMatrix[0][0] / sx,
            worldMatrix[0][1] / sx,
            worldMatrix[0][2] / sx
        );
        glm::vec3 up(
            worldMatrix[1][0] / sy,
            worldMatrix[1][1] / sy,
            worldMatrix[1][2] / sy
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
        data.colorR = m_Color.r;
        data.colorG = m_Color.g;
        data.colorB = m_Color.b;
        data.colorA = m_Color.a;

        return data;
    }

} // namespace Engine
