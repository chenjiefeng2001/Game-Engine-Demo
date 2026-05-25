#pragma once

/**
 * @file AudioSourceComponent.h
 * @brief 音频源组件 — 可挂载到 GameObject 上，播放 AudioClip
 *
 * AudioSourceComponent 封装了一个 IAudioSource（OpenAL 音源），
 * 绑定到 AudioClip 音频资源，提供播放控制接口。
 *
 * 设计原则：
 *   - 与 GameObject 生命周期解耦
 *   - 不直接依赖 OpenAL 类型（通过 IAudioSource 和 AudioClip 间接使用）
 *   - 支持 3D 空间音频（位置、衰减）
 *
 * 使用方式：
 * @code
 *   auto engine = std::make_shared<OpenALAudioEngine>();
 *   engine->Init();
 *
 *   AudioClip clip;
 *   clip.LoadFromFile("sounds/effect.wav");
 *
 *   AudioSourceComponent audio(engine->CreateSource());
 *   audio.Play(clip);
 *   audio.SetVolume(0.5f);
 *   audio.SetLooping(true);
 * @endcode
 */

#include "Engine/Core/Audio/IAudioSource.h"
#include "Engine/Core/Audio/AudioClip.h"
#include <memory>

namespace Engine {

    class AudioSourceComponent {
    public:
        /**
         * @brief 构造音频源组件
         * @param source IAudioSource 实例（通过 IAudioEngine::CreateSource 创建）
         */
        explicit AudioSourceComponent(std::shared_ptr<IAudioSource> source);
        ~AudioSourceComponent() = default;

        // 禁止拷贝，允许移动
        AudioSourceComponent(const AudioSourceComponent&) = delete;
        AudioSourceComponent& operator=(const AudioSourceComponent&) = delete;
        AudioSourceComponent(AudioSourceComponent&&) noexcept = default;
        AudioSourceComponent& operator=(AudioSourceComponent&&) noexcept = default;

        // ── 播放控制 ──

        /**
         * @brief 播放一个音频剪辑
         * @param clip 音频资源（必须已通过 LoadFromFile/LoadFromMemory 加载）
         *
         * 每次调用 Play 会绑定 clip 的 OpenAL 缓冲区到音源并开始播放。
         * 若需要重复播放同一剪辑，调用 Play 前请确保前一次播放已停止，
         * 或使用 Stop() 停止后再 Play()。
         */
        void Play(AudioClip& clip);

        /// 停止播放
        void Stop();

        /// 暂停播放
        void Pause();

        /// 恢复暂停
        void Resume();

        // ── 状态查询 ──

        /// 获取当前播放状态
        PlayState GetState() const { return m_Source ? m_Source->GetState() : PlayState::Stopped; }

        /// 是否正在播放
        bool IsPlaying() const { return m_Source && m_Source->IsPlaying(); }

        /// 当前绑定的 AudioClip（可能为 nullptr）
        AudioClip* GetBoundClip() const { return m_BoundClip; }

        // ── 音频属性 ──

        /// 设置音量（0.0 ~ 1.0，默认 1.0）
        void SetVolume(float32 gain);

        /// 获取当前音量
        float32 GetVolume() const;

        /// 设置音调（0.5 ~ 2.0，默认 1.0）
        void SetPitch(float32 pitch);

        /// 获取当前音调
        float32 GetPitch() const;

        /// 设置是否循环播放
        void SetLooping(bool loop);

        /// 是否循环播放
        bool IsLooping() const;

        // ── 3D 空间属性 ──

        /// 设置音源位置（世界坐标）
        void SetPosition(const Vec3& position);

        /// 获取音源位置
        Vec3 GetPosition() const;

        /// 设置音源速度（用于多普勒效应）
        void SetVelocity(const Vec3& velocity);

        /// 获取音源速度
        Vec3 GetVelocity() const;

        // ── 衰减参数 ──

        /**
         * @brief 设置衰减模式
         * @param mode        衰减模式
         * @param minDistance 最小距离（音量开始衰减的起始点）
         * @param maxDistance 最大距离（超过此距离音量衰减到零）
         */
        void SetAttenuation(AttenuationMode mode,
                            float32 minDistance = 1.0f,
                            float32 maxDistance = FLT_MAX);

        // ── 播放进度 ──

        /// 获取当前播放位置（秒）
        float32 GetPlaybackPosition() const;

        /// 设置播放位置（秒）
        void SetPlaybackPosition(float32 seconds);

        // ── 内部访问 ──

        /// 获取底层 IAudioSource 指针
        IAudioSource* GetNativeSource() const { return m_Source.get(); }

    private:
        std::shared_ptr<IAudioSource> m_Source;
        AudioClip* m_BoundClip = nullptr;  ///< 当前绑定的剪辑（非拥有）
    };

} // namespace Engine
