#include "Engine/OpenAL/OpenALAudioSource.h"
#include "Engine/OpenAL/OpenALAudioBuffer.h"
#include "Engine/Core/Log.h"
#include <cfloat>

namespace {
    Engine::Logger s_Log("OpenAL");
}
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine {

    OpenALAudioSource::OpenALAudioSource() {
        alGenSources(1, &m_SourceID);
        if (alGetError() != AL_NO_ERROR) {
            s_Log.Error("Failed to generate source");
            m_SourceID = 0;
        }
    }

    OpenALAudioSource::~OpenALAudioSource() {
        if (m_SourceID) {
            alDeleteSources(1, &m_SourceID);
            m_SourceID = 0;
        }
    }

    void OpenALAudioSource::Play(std::shared_ptr<IAudioBuffer> buffer) {
        if (!m_SourceID || !buffer) return;

        m_Buffer = buffer;
        alSourcei(m_SourceID, AL_BUFFER, static_cast<ALint>(buffer->GetNativeHandle()));
        alSourcePlay(m_SourceID);

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            s_Log.Error("Failed to play source (error: {})", err);
        }
    }

    void OpenALAudioSource::Stop() {
        if (m_SourceID) {
            alSourceStop(m_SourceID);
            alSourcei(m_SourceID, AL_BUFFER, 0);
            m_Buffer.reset();
        }
    }

    void OpenALAudioSource::Pause() {
        if (m_SourceID) alSourcePause(m_SourceID);
    }

    void OpenALAudioSource::Resume() {
        if (m_SourceID) alSourcePlay(m_SourceID);
    }

    PlayState OpenALAudioSource::GetState() const {
        if (!m_SourceID) return PlayState::Stopped;
        ALint state;
        alGetSourcei(m_SourceID, AL_SOURCE_STATE, &state);
        switch (state) {
            case AL_PLAYING: return PlayState::Playing;
            case AL_PAUSED:  return PlayState::Paused;
            case AL_STOPPED:
            default:         return PlayState::Stopped;
        }
    }

    bool OpenALAudioSource::IsPlaying() const {
        return GetState() == PlayState::Playing;
    }

    void OpenALAudioSource::SetPosition(const Vec3& position) {
        if (m_SourceID) alSource3f(m_SourceID, AL_POSITION,
                                    position.x, position.y, position.z);
    }

    Vec3 OpenALAudioSource::GetPosition() const {
        if (!m_SourceID) return Vec3(0.0f, 0.0f, 0.0f);
        ALfloat x, y, z;
        alGetSource3f(m_SourceID, AL_POSITION, &x, &y, &z);
        return Vec3(x, y, z);
    }

    void OpenALAudioSource::SetVelocity(const Vec3& velocity) {
        if (m_SourceID) alSource3f(m_SourceID, AL_VELOCITY,
                                    velocity.x, velocity.y, velocity.z);
    }

    Vec3 OpenALAudioSource::GetVelocity() const {
        if (!m_SourceID) return Vec3(0.0f, 0.0f, 0.0f);
        ALfloat x, y, z;
        alGetSource3f(m_SourceID, AL_VELOCITY, &x, &y, &z);
        return Vec3(x, y, z);
    }

    void OpenALAudioSource::SetVolume(float32 gain) {
        m_Volume = gain;
        if (m_SourceID) alSourcef(m_SourceID, AL_GAIN, gain);
    }

    float32 OpenALAudioSource::GetVolume() const { return m_Volume; }

    void OpenALAudioSource::SetPitch(float32 pitch) {
        m_Pitch = pitch;
        if (m_SourceID) alSourcef(m_SourceID, AL_PITCH, pitch);
    }

    float32 OpenALAudioSource::GetPitch() const { return m_Pitch; }

    void OpenALAudioSource::SetLooping(bool loop) {
        m_Looping = loop;
        if (m_SourceID) alSourcei(m_SourceID, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    }

    bool OpenALAudioSource::IsLooping() const { return m_Looping; }

    void OpenALAudioSource::SetAttenuation(AttenuationMode mode,
                                            float32 minDistance,
                                            float32 maxDistance) {
        if (!m_SourceID) return;

        ALint alModel;
        switch (mode) {
            case AttenuationMode::None:           alModel = AL_NONE; break;
            case AttenuationMode::Inverse:        alModel = AL_INVERSE_DISTANCE; break;
            case AttenuationMode::InverseClamped: alModel = AL_INVERSE_DISTANCE_CLAMPED; break;
            case AttenuationMode::Linear:         alModel = AL_LINEAR_DISTANCE; break;
            case AttenuationMode::LinearClamped:  alModel = AL_LINEAR_DISTANCE_CLAMPED; break;
            default: alModel = AL_INVERSE_DISTANCE_CLAMPED; break;
        }

        alSourcei(m_SourceID, AL_DISTANCE_MODEL, alModel);
        alSourcef(m_SourceID, AL_REFERENCE_DISTANCE, minDistance);
        alSourcef(m_SourceID, AL_MAX_DISTANCE, maxDistance);
    }

    float32 OpenALAudioSource::GetPlaybackPosition() const {
        if (!m_SourceID) return 0.0f;
        ALfloat sec;
        alGetSourcef(m_SourceID, AL_SEC_OFFSET, &sec);
        return static_cast<float32>(sec);
    }

    void OpenALAudioSource::SetPlaybackPosition(float32 seconds) {
        if (m_SourceID) alSourcef(m_SourceID, AL_SEC_OFFSET, seconds);
    }

} // namespace Engine
