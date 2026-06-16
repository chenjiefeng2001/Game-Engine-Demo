#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <AL/al.h>
#include <AL/alc.h>

namespace {
    Engine::Logger s_Log("AudioClip");
}

// 安全宏：仅在 context 存在时执行 OpenAL 调用
static bool HasALContext()
{
    return alcGetCurrentContext() != nullptr;
}

namespace Engine {

AudioClip::~AudioClip() {
    Release();
}

AudioClip::AudioClip(AudioClip&& other) noexcept
    : Resource(std::move(other))
    , m_BufferID(other.m_BufferID)
    , m_Info(other.m_Info)
{
    other.m_BufferID = 0;
    other.m_Info = {};
}

AudioClip& AudioClip::operator=(AudioClip&& other) noexcept {
    if (this != &other) {
        Release();
        Resource::operator=(std::move(other));
        m_BufferID = other.m_BufferID;
        m_Info = other.m_Info;
        other.m_BufferID = 0;
        other.m_Info = {};
    }
    return *this;
}

bool AudioClip::LoadFromFile(const std::string& filePath) {
    if (!HasALContext()) {
        s_Log.Error("No OpenAL context active, cannot load '{}'", filePath);
        return false;
    }
    Release();
    SetState(ResourceState::Loading);
    if (GetPath().empty()) SetPath(filePath);

    AudioData data = AudioLoader::Load(filePath);
    if (!data.IsValid()) {
        s_Log.Error("Failed to load audio file: {}", filePath);
        SetState(ResourceState::Failed);
        return false;
    }
    if (!LoadPCM(data)) {
        SetState(ResourceState::Failed);
        return false;
    }
    SetState(ResourceState::Loaded);
    return true;
}

bool AudioClip::LoadFromMemory(const void* data, size_t dataSize,
                                const std::string& hint) {
    if (!HasALContext()) {
        s_Log.Error("No OpenAL context active, cannot load from memory");
        return false;
    }
    Release();
    std::string ext;
    size_t dot = hint.find_last_of('.');
    if (dot != std::string::npos) {
        ext = hint.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    }

    AudioData audioData;
    if (ext.empty() || ext == "wav") {
        audioData = AudioLoader::LoadWAVFromMemory(data, dataSize);
    }
    if (!audioData.IsValid() && (ext.empty() || ext == "ogg")) {
        audioData = AudioLoader::LoadOGGFromMemory(data, dataSize);
    }
    if (!audioData.IsValid()) {
        s_Log.Error("Failed to load audio from memory");
        SetState(ResourceState::Failed);
        return false;
    }
    if (!LoadPCM(audioData)) {
        SetState(ResourceState::Failed);
        return false;
    }
    SetState(ResourceState::Loaded);
    return true;
}

bool AudioClip::LoadPCM(const AudioData& audioData) {
    // 安全保护：无 context 时不做 OpenAL 调用
    if (!HasALContext()) {
        s_Log.Error("LoadPCM: No OpenAL context");
        return false;
    }

    alGenBuffers(1, &m_BufferID);
    if (alGetError() != AL_NO_ERROR) {
        s_Log.Error("Failed to generate OpenAL buffer");
        m_BufferID = 0;
        return false;
    }

    ALenum format = AL_NONE;
    switch (audioData.info.format) {
        case AudioFormat::Mono8:    format = AL_FORMAT_MONO8;   break;
        case AudioFormat::Mono16:   format = AL_FORMAT_MONO16;  break;
        case AudioFormat::Stereo8:  format = AL_FORMAT_STEREO8; break;
        case AudioFormat::Stereo16: format = AL_FORMAT_STEREO16;break;
    }
    if (format == AL_NONE) {
        s_Log.Error("Unsupported audio format");
        alDeleteBuffers(1, &m_BufferID);
        m_BufferID = 0;
        return false;
    }

    alBufferData(m_BufferID, format,
                 audioData.pcmData.data(),
                 static_cast<ALsizei>(audioData.pcmData.size()),
                 audioData.info.sampleRate);

    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        s_Log.Error("Failed to upload PCM data (error: {})", err);
        alDeleteBuffers(1, &m_BufferID);
        m_BufferID = 0;
        return false;
    }
    m_Info = audioData.info;
    return true;
}

void AudioClip::Release() {
    if (m_BufferID) {
        // 仅在 context 存在时释放 OpenAL 资源
        if (HasALContext()) {
            alDeleteBuffers(1, &m_BufferID);
        } else {
            s_Log.Trace("Skipping OpenAL buffer delete - no context");
        }
        m_BufferID = 0;
    }
    m_Info = {};
    SetState(ResourceState::Unloaded);
}

} // namespace Engine