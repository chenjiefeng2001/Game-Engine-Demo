#include "Engine/Core/Audio/Listener.h"
#include "Engine/Core/Audio/IAudioEngine.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include <cmath>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 工具：向量运算（避免依赖 glm）
    // ============================================================

    namespace {

        Vec3 Normalize(const Vec3& v) {
            float32 len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len < 1e-8f) return v;
            float32 inv = 1.0f / len;
            return Vec3(v.x * inv, v.y * inv, v.z * inv);
        }

        Vec3 Cross(const Vec3& a, const Vec3& b) {
            return Vec3(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            );
        }

        float32 Dot(const Vec3& a, const Vec3& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

    } // anonymous namespace

    // ============================================================
    // SetOrientation — 直接设置 Forward / Up
    // ============================================================

    void Listener::SetOrientation(const Vec3& forward, const Vec3& up) {
        m_Forward = Normalize(forward);
        m_Up      = Normalize(up);
        NormalizeOrientation();

        // 更新欧拉角缓存
        m_Pitch = std::asin(m_Forward.y);
        m_Yaw   = std::atan2(m_Forward.x, m_Forward.z);
    }

    // ============================================================
    // LookAt — 看向目标点
    // ============================================================

    void Listener::LookAt(const Vec3& target, const Vec3& worldUp) {
        // forward = normalize(target - position)
        Vec3 dir = {
            target.x - m_Position.x,
            target.y - m_Position.y,
            target.z - m_Position.z
        };
        m_Forward = Normalize(dir);

        // right = cross(forward, worldUp)
        // up    = cross(right, forward)
        Vec3 right = Cross(m_Forward, Normalize(worldUp));
        m_Up = Normalize(Cross(right, m_Forward));

        NormalizeOrientation();

        m_Pitch = std::asin(m_Forward.y);
        m_Yaw   = std::atan2(m_Forward.x, m_Forward.z);
    }

    // ============================================================
    // SetOrientationFromEuler — 从偏航/俯仰角设置
    // ============================================================

    void Listener::SetOrientationFromEuler(float32 yaw, float32 pitch) {
        m_Yaw   = yaw;
        m_Pitch = pitch;

        // 球坐标 → 笛卡尔坐标（右手系，Y 向上）
        float32 cp = std::cos(pitch);
        m_Forward.x = cp * std::sin(yaw);
        m_Forward.y = std::sin(pitch);
        m_Forward.z = cp * std::cos(yaw);

        m_Forward = Normalize(m_Forward);

        // 计算 up 向量
        Vec3 worldUp(0.0f, 1.0f, 0.0f);
        Vec3 right = Cross(m_Forward, worldUp);
        if (std::abs(Dot(right, right)) < 1e-8f) {
            // 朝上或朝下时，右向量退化为零向量
            // 用世界 Z 轴作为参考计算右向量
            right = Cross(m_Forward, Vec3(0.0f, 0.0f, 1.0f));
        }
        m_Up = Normalize(Cross(right, m_Forward));

        NormalizeOrientation();
    }

    // ============================================================
    // SetOrientationFromMatrix — 从 4×4 矩阵提取
    // ============================================================

    void Listener::SetOrientationFromMatrix(const Mat4& viewMatrix) {
        // 假设列主序矩阵，列索引: data[col*4 + row]
        // 第三列 (col 2) = forward 向量
        // 第二列 (col 1) = up 向量
        // 注意：视图矩阵的 forward 是 -Z，需要取反
        m_Forward.x = -viewMatrix.data[8];
        m_Forward.y = -viewMatrix.data[9];
        m_Forward.z = -viewMatrix.data[10];

        m_Up.x = viewMatrix.data[4];
        m_Up.y = viewMatrix.data[5];
        m_Up.z = viewMatrix.data[6];

        m_Forward = Normalize(m_Forward);
        m_Up      = Normalize(m_Up);
        NormalizeOrientation();

        m_Pitch = std::asin(m_Forward.y);
        m_Yaw   = std::atan2(m_Forward.x, m_Forward.z);
    }

    // ============================================================
    // SyncFromTransform — 从 TransformComponent 同步
    // ============================================================

    void Listener::SyncFromTransform(const TransformComponent& transform) {
        // 位置
        m_Position = transform.GetPosition();

        // 朝向 — 假设 TransformComponent 实现了 GetForward/GetUp
        // 此处使用标准约定：Transform 的 -Z 轴为 forward，Y 轴为 up
        Vec3 worldForward = transform.GetForward();  // 通常为 -Z
        Vec3 worldUp      = transform.GetUp();        // 通常为 +Y

        SetOrientation(worldForward, worldUp);
    }

    // ============================================================
    // NormalizeOrientation — 确保 Forward / Up 正交归一化
    // ============================================================

    void Listener::NormalizeOrientation() {
        m_Forward = Normalize(m_Forward);

        // 格拉姆-施密特正交化：up = up - dot(up, forward) * forward
        float32 d = Dot(m_Up, m_Forward);
        m_Up.x -= d * m_Forward.x;
        m_Up.y -= d * m_Forward.y;
        m_Up.z -= d * m_Forward.z;
        m_Up = Normalize(m_Up);
    }

    // ============================================================
    // Apply — 同步到音频引擎
    // ============================================================

    void Listener::Apply(IAudioEngine& engine) const {
        engine.SetListenerPosition(m_Position);
        engine.SetListenerOrientation(m_Forward, m_Up);
        engine.SetListenerVolume(m_Volume);

        // 速度（多普勒）— 通过 IAudioEngine 扩展接口
        // 目前 IAudioEngine 没有 SetListenerVelocity，
        // 预留：engine.SetListenerVelocity(m_Velocity);
    }

    // ============================================================
    // ApplyTransform — 仅同步变换
    // ============================================================

    void Listener::ApplyTransform(IAudioEngine& engine) const {
        engine.SetListenerPosition(m_Position);
        engine.SetListenerOrientation(m_Forward, m_Up);
    }

} // namespace Engine
