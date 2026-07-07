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

        inline glm::quat ToGlmQuat(const Quat& q) {
            return glm::quat(q.w, q.x, q.y, q.z);
        }

        inline Quat FromGlmQuat(const glm::quat& q) {
            return Quat(q.x, q.y, q.z, q.w);
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
        , m_RotationQuat(Quat::Identity())
        , m_Scale(1.0f, 1.0f, 1.0f)
    {
    }

    TransformComponent::TransformComponent(const Vec3& position)
        : m_Position(position)
        , m_RotationQuat(Quat::Identity())
        , m_Scale(1.0f, 1.0f, 1.0f)
    {
    }

    TransformComponent::TransformComponent(const Vec3& position,
                                           const Vec3& rotation,
                                           const Vec3& scale)
        : m_Position(position)
        , m_Scale(scale)
    {
        // Convert Euler → Quat
        glm::quat q = glm::quat(glm::radians(glm::vec3(rotation.x, rotation.y, rotation.z)));
        m_RotationQuat = FromGlmQuat(q);
        m_CachedEuler = rotation;
        m_EulerDirty = false;
    }

    // ── 旋转访问器实现 ──

    Vec3 TransformComponent::GetRotationEuler() const {
        if (m_EulerDirty) {
            glm::quat q = ToGlmQuat(m_RotationQuat);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
            m_CachedEuler = Vec3(euler.x, euler.y, euler.z);
            m_EulerDirty = false;
        }
        return m_CachedEuler;
    }

    void TransformComponent::SetRotationEuler(const Vec3& euler) {
        glm::quat q = glm::quat(glm::radians(glm::vec3(euler.x, euler.y, euler.z)));
        m_RotationQuat = FromGlmQuat(q);
        m_CachedEuler = euler;
        m_EulerDirty = false;
        m_Dirty = true;
    }

    // ── 相对变换 ──
    void TransformComponent::Translate(const Vec3& delta) {
        m_Position.x += delta.x;
        m_Position.y += delta.y;
        m_Position.z += delta.z;
        m_Dirty = true;
    }

    void TransformComponent::Rotate(const Vec3& eulerDelta) {
        // Use quaternion multiplication instead of euler addition
        glm::quat delta = glm::quat(glm::radians(glm::vec3(eulerDelta.x, eulerDelta.y, eulerDelta.z)));
        m_RotationQuat = FromGlmQuat(delta * ToGlmQuat(m_RotationQuat));
        m_EulerDirty = true;
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

        // 使用四元数直接构造旋转矩阵，避免万向锁
        glm::quat q = ToGlmQuat(m_RotationQuat);
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

    // ════════════════════════════════════════════════
    // 坐标空间变换
    // ════════════════════════════════════════════════

    Vec3 TransformComponent::LocalToWorld(const Vec3& localPoint) {
        const Mat4& world = GetWorldMatrix();
        glm::vec4 p(localPoint.x, localPoint.y, localPoint.z, 1.0f);
        glm::mat4 w = ToGlm(world);
        glm::vec4 result = w * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 TransformComponent::LocalToWorldDir(const Vec3& localDir) {
        const Mat4& world = GetWorldMatrix();
        glm::vec4 p(localDir.x, localDir.y, localDir.z, 0.0f); // 方向向量 w=0
        glm::mat4 w = ToGlm(world);
        glm::vec4 result = w * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 TransformComponent::WorldToLocal(const Vec3& worldPoint) {
        const Mat4& world = GetWorldMatrix();
        glm::mat4 w = ToGlm(world);
        glm::mat4 inv = glm::inverse(w);
        glm::vec4 p(worldPoint.x, worldPoint.y, worldPoint.z, 1.0f);
        glm::vec4 result = inv * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 TransformComponent::WorldToLocalDir(const Vec3& worldDir) {
        const Mat4& world = GetWorldMatrix();
        glm::mat4 w = ToGlm(world);
        glm::mat4 inv = glm::inverse(w);
        glm::vec4 p(worldDir.x, worldDir.y, worldDir.z, 0.0f);
        glm::vec4 result = inv * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 TransformComponent::GetWorldForward() {
        const Mat4& w = GetWorldMatrix();
        // Z 轴（列 2）
        return Vec3(w(0, 2), w(1, 2), w(2, 2));
    }

    Vec3 TransformComponent::GetWorldRight() {
        const Mat4& w = GetWorldMatrix();
        // X 轴（列 0）
        return Vec3(w(0, 0), w(1, 0), w(2, 0));
    }

    Vec3 TransformComponent::GetWorldUp() {
        const Mat4& w = GetWorldMatrix();
        // Y 轴（列 1）
        return Vec3(w(0, 1), w(1, 1), w(2, 1));
    }

    // ── 朝向向量（直接委托 GetWorld*）──
    Vec3 TransformComponent::GetForward() const noexcept {
        return Vec3(m_WorldMatrix(0, 2), m_WorldMatrix(1, 2), m_WorldMatrix(2, 2));
    }

    Vec3 TransformComponent::GetRight() const noexcept {
        return Vec3(m_WorldMatrix(0, 0), m_WorldMatrix(1, 0), m_WorldMatrix(2, 0));
    }

    Vec3 TransformComponent::GetUp() const noexcept {
        return Vec3(m_WorldMatrix(0, 1), m_WorldMatrix(1, 1), m_WorldMatrix(2, 1));
    }

} // namespace Engine
