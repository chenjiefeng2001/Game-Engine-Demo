#pragma once

/**
 * @file OpenALAudioSource.h
 * @brief OpenAL 音源实现
 *
 * 实现 IAudioSource 接口，封装 alGenSources / alSourcei 等操作。
 */

#include "Engine/Core/Audio/IAudioSource.h"
#include <memory>

namespace Engine {

    class OpenALAudioSource : public IAudioSource {
    public:
        OpenALAudioSource();
        ~OpenALAudioSource() override;

        // ── IAudioSource ──
        void Play(std::shared_ptr<IAudioBuffer> buffer) override;
        void Stop() override;
        void Pause() override;
        void Resume() override;

        PlayState GetState() const override;
        bool IsPlaying() const override;

        void SetPosition(const Vec3& position) override;
        Vec3 GetPosition() const override;
        void SetVelocity(const Vec3& velocity) override;
        Vec3 GetVelocity() const override;

        void SetVolume(float32 gain) override;
        float32 GetVolume() const override;
        void SetPitch(float32 pitch) override;
        float32 GetPitch() const override;
        void SetLooping(bool loop) override;
        bool IsLooping() const override;

        void SetAttenuation(AttenuationMode mode,
                            float32 minDistance = 1.0f,
                            float32 maxDistance = FLT_MAX) override;

        float32 GetPlaybackPosition() const override;
        void   SetPlaybackPosition(float32 seconds) override;

        uint32 GetNativeHandle() const override { return m_SourceID; }

    private:
        uint32 m_SourceID = 0;
        std::shared_ptr<IAudioBuffer> m_Buffer;
        float32 m_Volume = 1.0f;
        float32 m_Pitch  = 1.0f;
        bool    m_Looping = false;
    };

} // namespace Engine
