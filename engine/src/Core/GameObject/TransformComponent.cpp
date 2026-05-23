#include "Engine/Core/GameObject/TransformComponent.h"

namespace Engine {

    TransformComponent::TransformComponent()
        : m_Position(0.0f), m_Rotation(0.0f), m_Scale(1.0f) {
    }

    TransformComponent::TransformComponent(const glm::vec3& position)
        : m_Position(position), m_Rotation(0.0f), m_Scale(1.0f) {
    }

    TransformComponent::TransformComponent(const glm::vec3& position,
                                           const glm::vec3& rotation,
                                           const glm::vec3& scale)
        : m_Position(position), m_Rotation(rotation), m_Scale(scale) {
    }


    void TransformComponent::RecalculateLocalMatrix() {
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

        // 使用四元数避免万向锁，按 Yaw → Pitch → Roll 顺序 (适合 3D 游戏)
        glm::quat q = glm::quat(glm::vec3(m_Rotation.x, m_Rotation.y, m_Rotation.z));
        glm::mat4 rotation = glm::mat4_cast(q);

        glm::mat4 scaling = glm::scale(glm::mat4(1.0f), m_Scale);

        m_LocalMatrix = translation * rotation * scaling;
    }


    void TransformComponent::RecalculateWorldMatrix() {
        if (m_Parent) {
            // 先保证父级是最新的
            m_Parent->GetWorldMatrix();
            m_WorldMatrix = m_Parent->m_WorldMatrix * m_LocalMatrix;
        } else {
            m_WorldMatrix = m_LocalMatrix;
        }
    }


    const glm::mat4& TransformComponent::GetLocalMatrix() {
        if (m_Dirty) {
            RecalculateLocalMatrix();
            m_Dirty = false;
        }
        return m_LocalMatrix;
    }

    const glm::mat4& TransformComponent::GetWorldMatrix() {
        if (m_Dirty) {
            RecalculateLocalMatrix();
            // 世界矩阵的计算也放在这里
            RecalculateWorldMatrix();
            m_Dirty = false;
        }

        return m_WorldMatrix;
    }

} // namespace Engine
