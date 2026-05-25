#pragma once

/**
 * @file Listener.h
 * @brief 音频听者（Listener）— 管理 3D 空间中的听者位置与朝向
 *
 * Listener 对应于 OpenAL 中的"听者"概念，决定用户如何听到 3D 世界的声音。
 * 所有 AudioSource 的空间属性（位置、衰减）都是相对于此听者计算的。
 *
 * 设计目标：
 *   - 独立于 IAudioEngine，可在没有音频引擎的情况下配置
 *   - 提供多种朝向同步方案（直接向量、Look-At、欧拉角、矩阵、Transform 组件）
 *   - 通过 Apply() 将状态批量同步到 IAudioEngine
 *   - 预留 3D 扩展点（速度、环境效果等）
 *
 * 使用示例：
 * @code
 *   Listener listener;
 *   listener.SetPosition(Vec3(0.0f, 0.0f, 5.0f));
 *   listener.SetOrientation(Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
 *   listener.Apply(engine);  // 同步到音频引擎
 *
 *   // 每帧从摄像机同步
 *   listener.SyncFromCamera(cameraViewMatrix);
 *   listener.Apply(engine);
 * @endcode
 */

#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/Audio/AudioDefs.h"
#include <cmath>

namespace Engine {

    // 前向声明（可选依赖）
    class IAudioEngine;
    class TransformComponent;

    /**
     * @brief 环境音效预设（3D 扩展点）
     *
     * 预留接口，未来可实现 OpenAL EFX 环境效果。
     */
    enum class EnvironmentPreset : uint8 {
        Default,       ///< 默认（无环境效果）
        Room,          ///< 小房间
        Hall,          ///< 大厅
        Cavern,        ///< 洞穴
        Outside,       ///< 户外开阔
        Underwater,    ///< 水下
        PaddedCell,    ///< 消声室
    };

    class Listener {
    public:
        Listener() = default;

        // ── 位置 ──

        /// 设置听者位置（世界坐标）
        void SetPosition(const Vec3& position) { m_Position = position; }
        /// 获取听者位置
        const Vec3& GetPosition() const { return m_Position; }

        // ── 朝向（方案 A：直接 Forward / Up 向量） ──

        /**
         * @brief 直接设置听者朝向（OpenAL 原生方式）
         * @param forward 前方向量（必须归一化）
         * @param up      上方向量（必须归一化，通常为 {0,1,0}）
         *
         * 这是最底层的方式，直接对应 OpenAL 的 alListenerfv(AL_ORIENTATION)。
         */
        void SetOrientation(const Vec3& forward, const Vec3& up);

        /// 获取前方向量
        const Vec3& GetForward() const { return m_Forward; }
        /// 获取上方向量
        const Vec3& GetUp() const { return m_Up; }

        // ── 朝向（方案 B：Look-At 目标点） ──

        /**
         * @brief 计算朝向，使听者"看向"目标点
         * @param target     目标世界坐标
         * @param worldUp    世界的上方向（默认 {0,1,0}）
         *
         * 适用于摄像机跟随、NPC 听者等场景。
         * 算法：forward = normalize(target - position)，然后叉积计算 right 和 up。
         */
        void LookAt(const Vec3& target, const Vec3& worldUp = Vec3(0.0f, 1.0f, 0.0f));

        // ── 朝向（方案 C：欧拉角） ──

        /**
         * @brief 从欧拉角设置朝向（First-Person 风格）
         * @param yaw   偏航角（弧度），绕 Y 轴，正值为右转
         * @param pitch 俯仰角（弧度），绕 X 轴，正值为抬头
         *
         * 适用于 FPS 摄像机、自由飞行视角。
         * 内部使用球坐标计算 forward 向量：
         *   forward.x = cos(pitch) * sin(yaw)
         *   forward.y = sin(pitch)
         *   forward.z = cos(pitch) * cos(yaw)
         */
        void SetOrientationFromEuler(float32 yaw, float32 pitch);

        /// 获取当前欧拉角（弧度）
        float32 GetYaw()   const { return m_Yaw; }
        float32 GetPitch() const { return m_Pitch; }

        // ── 朝向（方案 D：从 4×4 矩阵提取） ──

        /**
         * @brief 从视图/相机矩阵提取朝向
         * @param viewMatrix 4×4 列主序视图矩阵
         *
         * 适用于与相机系统集成。从矩阵的第三列提取 forward，
         * 第二列提取 up。假设矩阵为列主序（与 OpenGL/glm 一致）。
         */
        void SetOrientationFromMatrix(const Mat4& viewMatrix);

        // ── 朝向（方案 E：从 TransformComponent 同步） ──

        /**
         * @brief 从 TransformComponent 同步位置和朝向
         * @param transform 变换组件（需包含位置和旋转信息）
         *
         * 适用于将听者绑定到游戏对象（如玩家角色、摄像机对象）。
         * TransformComponent 需要提供 GetForward() / GetUp() 方法。
         */
        void SyncFromTransform(const class TransformComponent& transform);

        // ── 速度（用于多普勒效应） ──

        /// 设置听者速度（用于多普勒效应）
        void SetVelocity(const Vec3& velocity) { m_Velocity = velocity; }
        /// 获取听者速度
        const Vec3& GetVelocity() const { return m_Velocity; }

        // ── 音量 ──

        /// 设置听者增益（全局听者音量，默认 1.0）
        void SetVolume(float32 gain) { m_Volume = gain; }
        /// 获取听者增益
        float32 GetVolume() const { return m_Volume; }

        // ── 3D 扩展接口 ──

        /**
         * @brief 设置环境音效预设（3D 扩展）
         *
         * 预留接口。未来实现 OpenAL EFX 时，
         * EffectSlot 会根据此预设加载对应的 Reverb 效果。
         */
        void SetEnvironment(EnvironmentPreset preset) { m_Environment = preset; }
        EnvironmentPreset GetEnvironment() const { return m_Environment; }

        /// 设置房间环境参数（未来扩展）
        void SetRoomParameters(float32 roomSize, float32 reverb, float32 reflectivity) {
            m_RoomSize      = roomSize;
            m_ReverbLevel   = reverb;
            m_Reflectivity  = reflectivity;
        }

        // ── 同步到音频引擎 ──

        /**
         * @brief 将本 Listener 的所有状态同步到音频引擎
         * @param engine 音频引擎实例（需已 Init）
         *
         * 批量调用 IAudioEngine 的 SetListener* 方法。
         * 位置、朝向、音量、速度都会同步。
         */
        void Apply(class IAudioEngine& engine) const;

        /**
         * @brief 仅同步位置和朝向（忽略音量/速度）
         */
        void ApplyTransform(class IAudioEngine& engine) const;

    private:
        /// 内部：确保 forward/up 正交归一化
        void NormalizeOrientation();

        Vec3    m_Position  = {0.0f, 0.0f, 0.0f};
        Vec3    m_Forward   = {0.0f, 0.0f, -1.0f};  // OpenAL 默认：看向 -Z
        Vec3    m_Up        = {0.0f, 1.0f, 0.0f};
        Vec3    m_Velocity  = {0.0f, 0.0f, 0.0f};
        float32 m_Volume    = 1.0f;

        // 欧拉角缓存
        float32 m_Yaw   = 0.0f;
        float32 m_Pitch = 0.0f;

        // 3D 环境扩展
        EnvironmentPreset m_Environment = EnvironmentPreset::Default;
        float32 m_RoomSize      = 0.0f;
        float32 m_ReverbLevel   = 0.0f;
        float32 m_Reflectivity  = 0.0f;
    };

} // namespace Engine
