#include "Engine/Core/Audio/AudioSourceComponent.h"
#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("AudioSourceComponent");
}

// OpenAL 头文件（用于将 AudioClip 的缓冲区绑定到音源）
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    AudioSourceComponent::AudioSourceComponent(std::shared_ptr<IAudioSource> source)
        : m_Source(std::move(source))
    {
    }

    // ============================================================
    // 播放控制
    // ============================================================

    void AudioSourceComponent::Play(AudioClip& clip) {
        if (!m_Source) {
            s_Log.Error("No audio source available");
            return;
        }
        if (!clip.IsValid()) {
            s_Log.Error("AudioClip is not valid");
            return;
        }

        // 绑定 AudioClip 的 OpenAL 缓冲区到音源
        ALuint bufferHandle = clip.GetBufferHandle();
        ALuint sourceHandle = m_Source->GetNativeHandle();
        alSourcei(sourceHandle, AL_BUFFER, static_cast<ALint>(bufferHandle));

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            s_Log.Error("Failed to bind buffer (error: {})", err);
            return;
        }

        // 同步当前组件的音量/音调/循环设置到音源
        m_Source->SetVolume(m_Source->GetVolume());
        m_Source->SetPitch(m_Source->GetPitch());
        m_Source->SetLooping(m_Source->IsLooping());

        // 开始播放
        m_Source->Play(nullptr);  // buffer 已通过 alSourcei 绑定，此处传 nullptr

        m_BoundClip = &clip;
    }

    void AudioSourceComponent::Stop() {
        if (m_Source) {
            m_Source->Stop();
        }
    }

    void AudioSourceComponent::Pause() {
        if (m_Source) {
            m_Source->Pause();
        }
    }

    void AudioSourceComponent::Resume() {
        if (m_Source) {
            m_Source->Resume();
        }
    }

    // ============================================================
    // 音频属性
    // ============================================================

    void AudioSourceComponent::SetVolume(float32 gain) {
        if (m_Source) {
            m_Source->SetVolume(gain);
        }
    }

    float32 AudioSourceComponent::GetVolume() const {
        return m_Source ? m_Source->GetVolume() : 0.0f;
    }

    void AudioSourceComponent::SetPitch(float32 pitch) {
        if (m_Source) {
            m_Source->SetPitch(pitch);
        }
    }

    float32 AudioSourceComponent::GetPitch() const {
        return m_Source ? m_Source->GetPitch() : 1.0f;
    }

    void AudioSourceComponent::SetLooping(bool loop) {
        if (m_Source) {
            m_Source->SetLooping(loop);
        }
    }

    bool AudioSourceComponent::IsLooping() const {
        return m_Source && m_Source->IsLooping();
    }

    // ============================================================
    // 3D 空间属性
    // ============================================================

    void AudioSourceComponent::SetPosition(const Vec3& position) {
        if (m_Source) {
            m_Source->SetPosition(position);
        }
    }

    Vec3 AudioSourceComponent::GetPosition() const {
        return m_Source ? m_Source->GetPosition() : Vec3(0.0f, 0.0f, 0.0f);
    }

    void AudioSourceComponent::SetVelocity(const Vec3& velocity) {
        if (m_Source) {
            m_Source->SetVelocity(velocity);
        }
    }

    Vec3 AudioSourceComponent::GetVelocity() const {
        return m_Source ? m_Source->GetVelocity() : Vec3(0.0f, 0.0f, 0.0f);
    }

    // ============================================================
    // 衰减参数
    // ============================================================

    void AudioSourceComponent::SetAttenuation(AttenuationMode mode,
                                               float32 minDistance,
                                               float32 maxDistance) {
        if (m_Source) {
            m_Source->SetAttenuation(mode, minDistance, maxDistance);
        }
    }

    // ============================================================
    // 播放进度
    // ============================================================

    float32 AudioSourceComponent::GetPlaybackPosition() const {
        return m_Source ? m_Source->GetPlaybackPosition() : 0.0f;
    }

    void AudioSourceComponent::SetPlaybackPosition(float32 seconds) {
        if (m_Source) {
            m_Source->SetPlaybackPosition(seconds);
        }
    }

} // namespace Engine
