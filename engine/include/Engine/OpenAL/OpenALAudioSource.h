#pragma once

/**
 * @file OpenALAudioSource.h
 * @brief OpenAL source wrapper for Stack 2 (T-7-A runtime semantics).
 *
 * All AL calls are checked against the owner thread, engine state and the
 * wrapper's engine generation before reaching the backend.
 */

#include "Engine/Core/Audio/IAudioSource.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace Engine {

    class AudioEngineRuntime;

    class OpenALAudioSource : public IAudioSource {
    public:
        OpenALAudioSource(std::weak_ptr<AudioEngineRuntime> runtime,
                          uint32 sourceID,
                          uint64 generation);
        ~OpenALAudioSource() override;

        // ── IAudioSource ──
        AudioStatus Play(std::shared_ptr<IAudioBuffer> buffer) override;
        AudioStatus Stop() override;
        AudioStatus Pause() override;
        AudioStatus Resume() override;

        PlayState GetState() const override;
        bool IsPlaying() const override;
        AudioStatus GetLastStatus() const override;

        AudioStatus SetPosition(const Vec3& position) override;
        Vec3 GetPosition() const override;
        AudioStatus SetVelocity(const Vec3& velocity) override;
        Vec3 GetVelocity() const override;

        AudioStatus SetVolume(float32 gain) override;
        float32 GetVolume() const override;
        AudioStatus SetPitch(float32 pitch) override;
        float32 GetPitch() const override;
        AudioStatus SetLooping(bool loop) override;
        bool IsLooping() const override;

        AudioStatus SetAttenuation(AttenuationMode mode,
                                   float32 minDistance = 1.0f,
                                   float32 maxDistance = FLT_MAX) override;

        float32 GetPlaybackPosition() const override;
        AudioStatus SetPlaybackPosition(float32 seconds) override;

        void Invalidate() override;
        bool IsOwnedBy(const void* runtime) const override;
        uint32 GetNativeHandle() const override;

    private:
        AudioStatus CheckReady(const std::shared_ptr<AudioEngineRuntime>& runtime) const;
        AudioStatus Fail(AudioStatus status) const;

        std::weak_ptr<AudioEngineRuntime> m_Runtime;
        std::atomic<uint32> m_SourceID{0};
        std::atomic<uint32> m_BoundBufferID{0};
        uint64 m_Generation = 0;
        std::shared_ptr<IAudioBuffer> m_Buffer;
        std::atomic<bool> m_Valid{true};
        mutable std::atomic<AudioStatus> m_LastStatus{AudioStatus::Ok};

        float32 m_Volume = 1.0f;
        float32 m_Pitch = 1.0f;
        bool m_Looping = false;
    };

} // namespace Engine
