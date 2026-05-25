#include "Engine/OpenAL/OpenALAudioBuffer.h"
#include <iostream>

// OpenAL 头文件
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine {

    OpenALAudioBuffer::OpenALAudioBuffer() {
        alGenBuffers(1, &m_BufferID);
        if (alGetError() != AL_NO_ERROR) {
            std::cerr << "[OpenAL] Failed to generate buffer" << std::endl;
            m_BufferID = 0;
        }
    }

    OpenALAudioBuffer::~OpenALAudioBuffer() {
        if (m_BufferID) {
            alDeleteBuffers(1, &m_BufferID);
            m_BufferID = 0;
        }
    }

    void OpenALAudioBuffer::Load(const void* pcmData, int32 dataSize,
                                  const AudioClipInfo& info) {
        if (!m_BufferID || !pcmData || dataSize <= 0) return;

        m_Info = info;

        // 确定 OpenAL 格式
        ALenum format = AL_NONE;
        switch (info.format) {
            case AudioFormat::Mono8:    format = AL_FORMAT_MONO8;   break;
            case AudioFormat::Mono16:   format = AL_FORMAT_MONO16;  break;
            case AudioFormat::Stereo8:  format = AL_FORMAT_STEREO8; break;
            case AudioFormat::Stereo16: format = AL_FORMAT_STEREO16;break;
        }

        if (format == AL_NONE) {
            std::cerr << "[OpenAL] Unsupported audio format" << std::endl;
            return;
        }

        alBufferData(m_BufferID, format, pcmData, dataSize, info.sampleRate);

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::cerr << "[OpenAL] Failed to buffer data (error: " << err << ")" << std::endl;
            return;
        }

        // 计算时长
        int32 bytesPerSample = (info.format == AudioFormat::Mono16 || 
                                info.format == AudioFormat::Stereo16) ? 2 : 1;
        int32 channels = (info.format == AudioFormat::Mono8 || 
                          info.format == AudioFormat::Mono16) ? 1 : 2;
        int32 totalSamples = dataSize / bytesPerSample;
        m_Info.duration = static_cast<float32>(totalSamples) / 
                          (info.sampleRate * channels);
    }

} // namespace Engine
