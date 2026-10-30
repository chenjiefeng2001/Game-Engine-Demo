#pragma once

/**
 * @file OpenALAudioBuffer.h
 * @brief OpenAL buffer wrapper for Stack 2 (T-7-A runtime semantics).
 *
 * The engine owns native buffers. The wrapper only holds a handle and engine
 * generation; destruction never calls AL directly.
 */

#include "Engine/Core/Audio/IAudioBuffer.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace Engine {

    class AudioEngineRuntime;

    class OpenALAudioBuffer : public IAudioBuffer {
    public:
        OpenALAudioBuffer(std::weak_ptr<AudioEngineRuntime> runtime,
                          uint32 bufferID,
                          uint64 generation);
        ~OpenALAudioBuffer() override;

        AudioStatus Load(const void* pcmData, int32 dataSize,
                         const AudioClipInfo& info) override;

        AudioClipInfo GetInfo() const override;
        int32 GetDataSize() const override;
        AudioStatus GetLastStatus() const override;

        void Invalidate() override;
        bool IsOwnedBy(const void* runtime) const override;
        uint32 GetNativeHandle() const override;

    private:
        AudioStatus CheckReady(const std::shared_ptr<AudioEngineRuntime>& runtime) const;
        AudioStatus Fail(AudioStatus status) const;

        std::weak_ptr<AudioEngineRuntime> m_Runtime;
        std::atomic<uint32> m_BufferID{0};
        uint64 m_Generation = 0;
        AudioClipInfo m_Info{};
        std::atomic<bool> m_Valid{true};
        std::atomic<bool> m_Loaded{false};
        mutable std::atomic<AudioStatus> m_LastStatus{AudioStatus::Ok};
    };

} // namespace Engine
