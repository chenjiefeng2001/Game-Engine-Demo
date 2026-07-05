#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cstring>
#include <cmath>

namespace Engine {

    // ── 辅助转换 ──
    namespace {
        inline glm::mat4 ToGlm(const Mat4& m) {
            glm::mat4 result;
            std::memcpy(&result, m.data, sizeof(float32) * 16);
            return result;
        }

        inline void StoreGlm(const glm::mat4& src, Mat4& dst) {
            std::memcpy(dst.data, &src, sizeof(float32) * 16);
        }
    } // anonymous namespace

    PerspectiveCamera::PerspectiveCamera(float fovDegrees, float aspectRatio,
                                         float nearPlane, float farPlane)
        : m_Fov(fovDegrees)
        , m_AspectRatio(aspectRatio)
        , m_Near(nearPlane)
        , m_Far(farPlane)
    {
        RecalculateProjection();
        RecalculateView();
    }

    // ============================================================
    // 投影参数
    // ============================================================

    void PerspectiveCamera::SetFov(float fovDegrees) {
        m_Fov = fovDegrees;
        m_ProjectionDirty = true;
    }

    void PerspectiveCamera::SetAspectRatio(float aspect) {
        m_AspectRatio = aspect;
        m_ProjectionDirty = true;
    }

    void PerspectiveCamera::SetNearFar(float nearPlane, float farPlane) {
        m_Near = nearPlane;
        m_Far = farPlane;
        m_ProjectionDirty = true;
    }

    // ============================================================
    // 朝向
    // ============================================================

    void PerspectiveCamera::LookAt(const Vec3& target, const Vec3& up) {
        glm::vec3 pos(m_Position.x, m_Position.y, m_Position.z);
        glm::vec3 tgt(target.x, target.y, target.z);
        glm::vec3 upVec(up.x, up.y, up.z);

        glm::mat4 view = glm::lookAt(pos, tgt, upVec);
        StoreGlm(view, m_ViewMatrix);

        // 从视图矩阵反算 pitch/yaw
        glm::vec3 direction = glm::normalize(pos - tgt);
        m_Yaw   = glm::degrees(std::atan2(direction.z, direction.x));
        m_Pitch = glm::degrees(std::asin(direction.y));

        // 计算 VP 矩阵
        glm::mat4 vp = ToGlm(m_ProjectionMatrix) * view;
        StoreGlm(vp, m_ViewProjectionMatrix);

        m_ViewDirty = false;
    }

    // ============================================================
    // 矩阵
    // ============================================================

    void PerspectiveCamera::RecalculateProjection() {
        glm::mat4 proj = glm::perspective(
            glm::radians(m_Fov), m_AspectRatio, m_Near, m_Far
        );
        // glm 默认是右手系，NDC z 范围 [-1, 1]。
        // 对于 OpenGL 4.6，可以直接使用。如果需要左手系可反转 z。
        StoreGlm(proj, m_ProjectionMatrix);
        m_ProjectionDirty = false;
    }

    void PerspectiveCamera::RecalculateView() {
        // 从 pitch/yaw 构建视图矩阵
        glm::vec3 direction;
        direction.x = std::cos(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch));
        direction.y = std::sin(glm::radians(m_Pitch));
        direction.z = std::sin(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch));

        glm::vec3 pos(m_Position.x, m_Position.y, m_Position.z);
        glm::vec3 center = pos + direction;
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        glm::mat4 view = glm::lookAt(pos, center, up);
        StoreGlm(view, m_ViewMatrix);
        m_ViewDirty = false;
    }

    const Mat4& PerspectiveCamera::GetProjectionMatrix() {
        if (m_ProjectionDirty) RecalculateProjection();
        return m_ProjectionMatrix;
    }

    const Mat4& PerspectiveCamera::GetViewMatrix() {
        if (m_ViewDirty) RecalculateView();
        return m_ViewMatrix;
    }

    const float* PerspectiveCamera::GetViewProjectionMatrixPtr() {
        if (m_ProjectionDirty) RecalculateProjection();
        if (m_ViewDirty) RecalculateView();

        glm::mat4 vp = ToGlm(m_ProjectionMatrix) * ToGlm(m_ViewMatrix);
        StoreGlm(vp, m_ViewProjectionMatrix);
        return m_ViewProjectionMatrix.Data();
    }

    // ════════════════════════════════════════════════
    // 坐标空间变换
    // ════════════════════════════════════════════════

    Vec3 PerspectiveCamera::WorldToView(const Vec3& worldPoint) const {
        glm::vec4 p(worldPoint.x, worldPoint.y, worldPoint.z, 1.0f);
        // 使用缓存的 m_ViewMatrix（需要确保是最新的——但我们标记为 const，
        // 因为外部调用应在帧内调用 GetViewMatrix() 先触发更新）
        glm::mat4 view = ToGlm(m_ViewMatrix);
        glm::vec4 result = view * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec3 PerspectiveCamera::ViewToWorld(const Vec3& viewPoint) const {
        glm::vec4 p(viewPoint.x, viewPoint.y, viewPoint.z, 1.0f);
        glm::mat4 view = ToGlm(m_ViewMatrix);
        glm::mat4 invView = glm::inverse(view);
        glm::vec4 result = invView * p;
        return Vec3(result.x, result.y, result.z);
    }

    Vec4 PerspectiveCamera::WorldToClip(const Vec3& worldPoint) const {
        glm::vec4 p(worldPoint.x, worldPoint.y, worldPoint.z, 1.0f);
        glm::mat4 vp = ToGlm(m_ProjectionMatrix) * ToGlm(m_ViewMatrix);
        glm::vec4 result = vp * p;
        return Vec4(result.x, result.y, result.z, result.w);
    }

    Vec3 PerspectiveCamera::ClipToWorld(const Vec3& clipPoint) const {
        glm::vec4 p(clipPoint.x, clipPoint.y, clipPoint.z, 1.0f);
        glm::mat4 vp = ToGlm(m_ProjectionMatrix) * ToGlm(m_ViewMatrix);
        glm::mat4 invVP = glm::inverse(vp);
        glm::vec4 result = invVP * p;
        if (result.w != 0.0f) {
            result.x /= result.w;
            result.y /= result.w;
            result.z /= result.w;
        }
        return Vec3(result.x, result.y, result.z);
    }

    // ============================================================
    // 朝向向量
    // ============================================================

    Vec3 PerspectiveCamera::GetForward() const {
        glm::vec3 front;
        front.x = std::cos(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch));
        front.y = std::sin(glm::radians(m_Pitch));
        front.z = std::sin(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch));
        front = glm::normalize(front);
        return Vec3(front.x, front.y, front.z);
    }

    Vec3 PerspectiveCamera::GetRight() const {
        glm::vec3 front = glm::normalize(glm::vec3(
            std::cos(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch)),
            std::sin(glm::radians(m_Pitch)),
            std::sin(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch))
        ));
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        return Vec3(right.x, right.y, right.z);
    }

    Vec3 PerspectiveCamera::GetUp() const {
        glm::vec3 front(GetForward().x, GetForward().y, GetForward().z);
        glm::vec3 right(GetRight().x, GetRight().y, GetRight().z);
        glm::vec3 up = glm::normalize(glm::cross(right, front));
        return Vec3(up.x, up.y, up.z);
    }

} // namespace Engine
