#pragma once

/**
 * @file OpenALAudioBuffer.h
 * @brief OpenAL 音频缓冲区实现
 *
 * 实现 IAudioBuffer 接口，封装 alGenBuffers / alBufferData 等操作。
 */

#include "Engine/Core/Audio/IAudioBuffer.h"
#include <memory>

namespace Engine {

    class OpenALAudioBuffer : public IAudioBuffer {
    public:
        OpenALAudioBuffer();
        ~OpenALAudioBuffer() override;

        void Load(const void* pcmData, int32 dataSize,
                  const AudioClipInfo& info) override;

        AudioClipInfo GetInfo() const override { return m_Info; }
        int32 GetDataSize() const override { return m_Info.dataSize; }
        uint32 GetNativeHandle() const override { return m_BufferID; }

    private:
        uint32    m_BufferID = 0;
        AudioClipInfo m_Info = {};
    };

} // namespace Engine
