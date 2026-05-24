#include "Engine/Core/GameObject/TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

    // ── 辅助转换函数 ──
    namespace {

        inline glm::vec3 ToGlm(const Vec3& v) {
            return glm::vec3(v.x, v.y, v.z);
        }

        inline Vec3 FromGlm(const glm::vec3& v) {
            return Vec3(v.x, v.y, v.z);
        }

        inline glm::mat4 ToGlm(const Mat4& m) {
            glm::mat4 result;
            std::memcpy(&result, m.data, sizeof(float32) * 16);
            return result;
        }

        inline void FromGlm(const glm::mat4& src, Mat4& dst) {
            std::memcpy(dst.data, &src, sizeof(float32) * 16);
        }

        inline Mat4 FromGlm(const glm::mat4& src) {
            Mat4 dst;
            std::memcpy(dst.data, &src, sizeof(float32) * 16);
            return dst;
        }

    } // anonymous namespace

    // ── 构造 ──
    TransformComponent::TransformComponent()
        : m_Position(0.0f, 0.0f, 0.0f)
        , m_Rotation(0.0f, 0.0f, 0.0f)
        , m_Scale(1.0f, 1.0f, 1.0f)
    {
    }

    TransformComponent::TransformComponent(const Vec3& position)
        : m_Position(position)
        , m_Rotation(0.0f, 0.0f, 0.0f)
        , m_Scale(1.0f, 1.0f, 1.0f)
    {
    }

    TransformComponent::TransformComponent(const Vec3& position,
                                           const Vec3& rotation,
                                           const Vec3& scale)
        : m_Position(position)
        , m_Rotation(rotation)
        , m_Scale(scale)
    {
    }

    // ── 相对变换 ──
    void TransformComponent::Translate(const Vec3& delta) {
        m_Position.x += delta.x;
        m_Position.y += delta.y;
        m_Position.z += delta.z;
        m_Dirty = true;
    }

    void TransformComponent::Rotate(const Vec3& eulerDelta) {
        m_Rotation.x += eulerDelta.x;
        m_Rotation.y += eulerDelta.y;
        m_Rotation.z += eulerDelta.z;
        m_Dirty = true;
    }

    void TransformComponent::ScaleBy(const Vec3& factor) {
        m_Scale.x *= factor.x;
        m_Scale.y *= factor.y;
        m_Scale.z *= factor.z;
        m_Dirty = true;
    }

    // ── 矩阵计算（使用 glm）──
    void TransformComponent::RecalculateMatrices() {
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), ToGlm(m_Position));

        // 使用四元数避免万向锁，按 Yaw → Pitch → Roll 顺序
        glm::quat q = glm::quat(glm::radians(ToGlm(m_Rotation)));
        glm::mat4 rot = glm::mat4_cast(q);

        glm::mat4 scaling = glm::scale(glm::mat4(1.0f), ToGlm(m_Scale));

        glm::mat4 local = translation * rot * scaling;

        if (m_Parent) {
            // 先保证父级是最新的
            m_Parent->RecalculateMatrices();
            glm::mat4 parentWorld = ToGlm(m_Parent->m_WorldMatrix);
            FromGlm(parentWorld * local, m_WorldMatrix);
        } else {
            FromGlm(local, m_WorldMatrix);
        }

        FromGlm(local, m_LocalMatrix);
    }

    const Mat4& TransformComponent::GetLocalMatrix() {
        if (m_Dirty) {
            RecalculateMatrices();
            m_Dirty = false;
        }
        return m_LocalMatrix;
    }

    const Mat4& TransformComponent::GetWorldMatrix() {
        if (m_Dirty) {
            RecalculateMatrices();
            m_Dirty = false;
        }
        return m_WorldMatrix;
    }

    // ── 朝向向量 ──
    Vec3 TransformComponent::GetForward() const noexcept {
        // 从世界矩阵 col 2 (Z 轴)
        return Vec3(m_WorldMatrix(0, 2), m_WorldMatrix(1, 2), m_WorldMatrix(2, 2));
    }

    Vec3 TransformComponent::GetRight() const noexcept {
        // 从世界矩阵 col 0 (X 轴)
        return Vec3(m_WorldMatrix(0, 0), m_WorldMatrix(1, 0), m_WorldMatrix(2, 0));
    }

    Vec3 TransformComponent::GetUp() const noexcept {
        // 从世界矩阵 col 1 (Y 轴)
        return Vec3(m_WorldMatrix(0, 1), m_WorldMatrix(1, 1), m_WorldMatrix(2, 1));
    }

} // namespace Engine
