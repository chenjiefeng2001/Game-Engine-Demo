#include "Engine/OpenAL/OpenALAudioSource.h"

#include "OpenALAudioState.h"
#include "Engine/Core/Audio/IAudioBuffer.h"

namespace Engine {

    OpenALAudioSource::OpenALAudioSource(
        std::weak_ptr<AudioEngineRuntime> runtime,
        uint32 sourceID,
        uint64 generation)
        : m_Runtime(std::move(runtime))
        , m_SourceID(sourceID)
        , m_Generation(generation) {
    }

    OpenALAudioSource::~OpenALAudioSource() {
        const bool wasValid = m_Valid.exchange(false);
        if (!wasValid) return;

        if (auto runtime = m_Runtime.lock()) {
            const uint32 bound = m_BoundBufferID.exchange(0);
            if (bound) runtime->RemoveBufferBinding(bound);
            if (runtime->phase.load(std::memory_order_acquire) ==
                    AudioEngineState::Ready &&
                m_Generation ==
                    runtime->generation.load(std::memory_order_acquire)) {
                runtime->EnqueueSourceRelease(
                    m_SourceID.load(std::memory_order_acquire), m_Generation);
            }
        }
        m_SourceID.store(0, std::memory_order_release);
    }

    AudioStatus OpenALAudioSource::Fail(AudioStatus status) const {
        m_LastStatus.store(status, std::memory_order_release);
        return status;
    }

    AudioStatus OpenALAudioSource::CheckReady(
        const std::shared_ptr<AudioEngineRuntime>& runtime) const {
        if (!runtime) return Fail(AudioStatus::NotInitialized);
        if (m_Generation != runtime->generation.load(std::memory_order_acquire)) {
            return Fail(AudioStatus::NotInitialized);
        }
        if (!m_Valid.load(std::memory_order_acquire)) {
            return Fail(AudioStatus::NotFound);
        }
        const auto status = runtime->CheckOwnerReady();
        return status == AudioStatus::Ok ? status : Fail(status);
    }

    AudioStatus OpenALAudioSource::Play(std::shared_ptr<IAudioBuffer> buffer) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;
        if (!buffer) return Fail(AudioStatus::InvalidArgument);
        if (!buffer->IsOwnedBy(runtime.get())) {
            return Fail(AudioStatus::InvalidArgument);
        }

        const uint32 bufferID = buffer->GetNativeHandle();
        if (!bufferID) return Fail(AudioStatus::InvalidArgument);

        const uint32 sourceID = m_SourceID.load(std::memory_order_acquire);
        if (!runtime->backend->SetSourceInt(
                sourceID, AudioBackendSourceParam::Buffer,
                static_cast<int32>(bufferID))) {
            return Fail(runtime->ReportBackendFailure());
        }
        if (!runtime->backend->SourcePlay(sourceID)) {
            return Fail(runtime->ReportBackendFailure());
        }

        const uint32 previousBuffer = m_BoundBufferID.load(std::memory_order_acquire);
        if (previousBuffer != bufferID) {
            if (previousBuffer != 0) runtime->RemoveBufferBinding(previousBuffer);
            runtime->AddBufferBinding(bufferID);
            m_BoundBufferID.store(bufferID, std::memory_order_release);
        }
        m_Buffer = std::move(buffer);
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioSource::Stop() {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        const uint32 sourceID = m_SourceID.load(std::memory_order_acquire);
        if (!runtime->backend->SourceStop(sourceID)) {
            return Fail(runtime->ReportBackendFailure());
        }
        runtime->backend->SetSourceInt(
            sourceID, AudioBackendSourceParam::Buffer, 0);
        const uint32 bound = m_BoundBufferID.exchange(0, std::memory_order_acq_rel);
        if (bound) runtime->RemoveBufferBinding(bound);
        m_Buffer.reset();
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioSource::Pause() {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SourcePause(
                m_SourceID.load(std::memory_order_acquire))) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    AudioStatus OpenALAudioSource::Resume() {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SourcePlay(
                m_SourceID.load(std::memory_order_acquire))) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    PlayState OpenALAudioSource::GetState() const {
        auto runtime = m_Runtime.lock();
        const auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return PlayState::Stopped;

        switch (runtime->backend->GetSourceState(
            m_SourceID.load(std::memory_order_acquire))) {
            case AudioBackendSourceState::Playing: return PlayState::Playing;
            case AudioBackendSourceState::Paused:  return PlayState::Paused;
            case AudioBackendSourceState::Initial: return PlayState::Initial;
            case AudioBackendSourceState::Stopped: return PlayState::Stopped;
        }
        return PlayState::Stopped;
    }

    bool OpenALAudioSource::IsPlaying() const {
        return GetState() == PlayState::Playing;
    }

    AudioStatus OpenALAudioSource::GetLastStatus() const {
        return m_LastStatus.load(std::memory_order_acquire);
    }

    AudioStatus OpenALAudioSource::SetPosition(const Vec3& position) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceVec3(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Position, position)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    Vec3 OpenALAudioSource::GetPosition() const {
        auto runtime = m_Runtime.lock();
        const auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return Vec3(0.0f, 0.0f, 0.0f);

        Vec3 position(0.0f, 0.0f, 0.0f);
        if (!runtime->backend->GetSourceVec3(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Position, position)) {
            Fail(runtime->ReportBackendFailure());
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return position;
    }

    AudioStatus OpenALAudioSource::SetVelocity(const Vec3& velocity) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceVec3(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Velocity, velocity)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    Vec3 OpenALAudioSource::GetVelocity() const {
        auto runtime = m_Runtime.lock();
        const auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return Vec3(0.0f, 0.0f, 0.0f);

        Vec3 velocity(0.0f, 0.0f, 0.0f);
        if (!runtime->backend->GetSourceVec3(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Velocity, velocity)) {
            Fail(runtime->ReportBackendFailure());
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return velocity;
    }

    AudioStatus OpenALAudioSource::SetVolume(float32 gain) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceFloat(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Gain, gain)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_Volume = gain;
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    float32 OpenALAudioSource::GetVolume() const {
        return m_Volume;
    }

    AudioStatus OpenALAudioSource::SetPitch(float32 pitch) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceFloat(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Pitch, pitch)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_Pitch = pitch;
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    float32 OpenALAudioSource::GetPitch() const {
        return m_Pitch;
    }

    AudioStatus OpenALAudioSource::SetLooping(bool loop) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceInt(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::Looping, loop ? 1 : 0)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_Looping = loop;
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    bool OpenALAudioSource::IsLooping() const {
        return m_Looping;
    }

    AudioStatus OpenALAudioSource::SetAttenuation(
        AttenuationMode mode, float32 minDistance, float32 maxDistance) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        const uint32 sourceID = m_SourceID.load(std::memory_order_acquire);
        if (!runtime->backend->SetSourceInt(
                sourceID, AudioBackendSourceParam::DistanceModel,
                static_cast<int32>(mode)) ||
            !runtime->backend->SetSourceFloat(
                sourceID, AudioBackendSourceParam::ReferenceDistance, minDistance) ||
            !runtime->backend->SetSourceFloat(
                sourceID, AudioBackendSourceParam::MaxDistance, maxDistance)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    float32 OpenALAudioSource::GetPlaybackPosition() const {
        auto runtime = m_Runtime.lock();
        const auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return 0.0f;

        float32 seconds = 0.0f;
        if (!runtime->backend->GetSourceFloat(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::SecOffset, seconds)) {
            Fail(runtime->ReportBackendFailure());
            return 0.0f;
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return seconds;
    }

    AudioStatus OpenALAudioSource::SetPlaybackPosition(float32 seconds) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;

        if (!runtime->backend->SetSourceFloat(
                m_SourceID.load(std::memory_order_acquire),
                AudioBackendSourceParam::SecOffset, seconds)) {
            return Fail(runtime->ReportBackendFailure());
        }
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    void OpenALAudioSource::Invalidate() {
        m_Valid.store(false, std::memory_order_release);
        m_SourceID.store(0, std::memory_order_release);
        const uint32 bound = m_BoundBufferID.exchange(0, std::memory_order_acq_rel);
        if (bound) {
            if (auto runtime = m_Runtime.lock()) {
                runtime->RemoveBufferBinding(bound);
            }
        }
        m_Buffer.reset();
        m_LastStatus.store(AudioStatus::NotFound, std::memory_order_release);
    }

    bool OpenALAudioSource::IsOwnedBy(const void* runtime) const {
        auto owner = m_Runtime.lock();
        return owner && runtime == owner.get();
    }

    uint32 OpenALAudioSource::GetNativeHandle() const {
        if (!m_Valid.load(std::memory_order_acquire)) return 0;
        auto runtime = m_Runtime.lock();
        if (!runtime) return 0;
        if (runtime->phase.load(std::memory_order_acquire) !=
                AudioEngineState::Ready ||
            m_Generation != runtime->generation.load(std::memory_order_acquire)) {
            return 0;
        }
        return m_SourceID.load(std::memory_order_acquire);
    }

} // namespace Engine
