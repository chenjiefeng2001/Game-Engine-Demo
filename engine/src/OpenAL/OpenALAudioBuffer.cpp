#include "Engine/OpenAL/OpenALAudioBuffer.h"

#include "OpenALAudioState.h"

#include <cmath>

namespace Engine {

    OpenALAudioBuffer::OpenALAudioBuffer(
        std::weak_ptr<AudioEngineRuntime> runtime,
        uint32 bufferID,
        uint64 generation)
        : m_Runtime(std::move(runtime))
        , m_BufferID(bufferID)
        , m_Generation(generation) {
    }

    OpenALAudioBuffer::~OpenALAudioBuffer() {
        const bool wasValid = m_Valid.exchange(false);
        if (!wasValid) return;

        if (auto runtime = m_Runtime.lock()) {
            if (runtime->phase.load(std::memory_order_acquire) ==
                    AudioEngineState::Ready &&
                m_Generation ==
                    runtime->generation.load(std::memory_order_acquire)) {
                runtime->EnqueueBufferRelease(
                    m_BufferID.load(std::memory_order_acquire), m_Generation);
            }
        }
        m_BufferID.store(0, std::memory_order_release);
    }

    AudioStatus OpenALAudioBuffer::Fail(AudioStatus status) const {
        m_LastStatus.store(status, std::memory_order_release);
        return status;
    }

    AudioStatus OpenALAudioBuffer::CheckReady(
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

    AudioStatus OpenALAudioBuffer::Load(const void* pcmData, int32 dataSize,
                                        const AudioClipInfo& info) {
        auto runtime = m_Runtime.lock();
        auto status = CheckReady(runtime);
        if (status != AudioStatus::Ok) return status;
        if (m_Loaded.load(std::memory_order_acquire)) {
            return Fail(AudioStatus::InvalidState);
        }
        if (!pcmData || dataSize <= 0 || info.sampleRate <= 0) {
            return Fail(AudioStatus::InvalidArgument);
        }

        if (!runtime->backend->BufferData(
                m_BufferID.load(std::memory_order_acquire),
                info.format, pcmData, dataSize, info.sampleRate)) {
            return Fail(runtime->ReportBackendFailure());
        }

        const int32 bytesPerSample =
            (info.format == AudioFormat::Mono16 ||
             info.format == AudioFormat::Stereo16) ? 2 : 1;
        const int32 channels =
            (info.format == AudioFormat::Mono8 ||
             info.format == AudioFormat::Mono16) ? 1 : 2;
        const int32 totalSamples = dataSize / bytesPerSample;

        m_Info = info;
        m_Info.dataSize = dataSize;
        m_Info.duration = static_cast<float32>(totalSamples) /
                          (static_cast<float32>(info.sampleRate) * channels);
        if (!std::isfinite(m_Info.duration)) {
            m_Info.duration = 0.0f;
        }
        m_Loaded.store(true, std::memory_order_release);
        m_LastStatus.store(AudioStatus::Ok, std::memory_order_release);
        return AudioStatus::Ok;
    }

    AudioClipInfo OpenALAudioBuffer::GetInfo() const {
        return m_Info;
    }

    int32 OpenALAudioBuffer::GetDataSize() const {
        return m_Info.dataSize;
    }

    AudioStatus OpenALAudioBuffer::GetLastStatus() const {
        return m_LastStatus.load(std::memory_order_acquire);
    }

    void OpenALAudioBuffer::Invalidate() {
        m_Valid.store(false, std::memory_order_release);
        m_Loaded.store(false, std::memory_order_release);
        m_BufferID.store(0, std::memory_order_release);
        m_LastStatus.store(AudioStatus::NotFound, std::memory_order_release);
    }

    bool OpenALAudioBuffer::IsOwnedBy(const void* runtime) const {
        auto owner = m_Runtime.lock();
        return owner && runtime == owner.get();
    }

    uint32 OpenALAudioBuffer::GetNativeHandle() const {
        if (!m_Valid.load(std::memory_order_acquire) ||
            !m_Loaded.load(std::memory_order_acquire)) {
            return 0;
        }
        auto runtime = m_Runtime.lock();
        if (!runtime) return 0;
        if (runtime->phase.load(std::memory_order_acquire) !=
                AudioEngineState::Ready ||
            m_Generation != runtime->generation.load(std::memory_order_acquire)) {
            return 0;
        }
        return m_BufferID.load(std::memory_order_acquire);
    }

} // namespace Engine
