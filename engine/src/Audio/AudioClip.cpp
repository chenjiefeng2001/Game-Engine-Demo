#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <AL/al.h>
#include <AL/alc.h>

namespace {
    Engine::Logger s_Log("AudioClip");
}

namespace Engine {

// ============================================================
// 析构 — RAII 释放 OpenAL 缓冲区
// ============================================================

AudioClip::~AudioClip() {
    Release();
}

// ============================================================
// 移动语义（需要显式移动 Resource 基类）
// ============================================================

AudioClip::AudioClip(AudioClip&& other) noexcept
    : Resource(std::move(other))          // 移动 Resource 基类
    , m_BufferID(other.m_BufferID)
    , m_Info(other.m_Info)
{
    other.m_BufferID = 0;
    other.m_Info = {};
}

AudioClip& AudioClip::operator=(AudioClip&& other) noexcept {
    if (this != &other) {
        Release();
        Resource::operator=(std::move(other));  // 移动 Resource 基类
        m_BufferID = other.m_BufferID;
        m_Info = other.m_Info;
        other.m_BufferID = 0;
        other.m_Info = {};
    }
    return *this;
}

// ============================================================
// LoadFromFile — 从文件加载
// ============================================================

bool AudioClip::LoadFromFile(const std::string& filePath) {
    Release();
    SetState(ResourceState::Loading);

    // 同步路径到 Resource 基类（若构造函数未提供路径）
    if (GetPath().empty()) {
        SetPath(filePath);
    }

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

// ============================================================
// LoadFromMemory — 从内存加载
// ============================================================

bool AudioClip::LoadFromMemory(const void* data, size_t dataSize,
                                const std::string& hint) {
    Release();

    // 根据 hint 扩展名选择解码器
    std::string ext;
    size_t dot = hint.find_last_of('.');
    if (dot != std::string::npos) {
        ext = hint.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    }

    AudioData audioData;

    // 尝试 WAV
    if (ext.empty() || ext == "wav") {
        audioData = AudioLoader::LoadWAVFromMemory(data, dataSize);
    }

    // 如果 WAV 失败或 hint 是 ogg，尝试 OGG
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

// ============================================================
// LoadPCM — PCM → OpenAL 缓冲区
// ============================================================

bool AudioClip::LoadPCM(const AudioData& audioData) {
    // 生成 OpenAL 缓冲区
    alGenBuffers(1, &m_BufferID);
    if (alGetError() != AL_NO_ERROR) {
        s_Log.Error("Failed to generate OpenAL buffer");
        m_BufferID = 0;
        return false;
    }

    // 映射音频格式
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

    // 上传 PCM 数据
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

// ============================================================
// Release — 手动释放
// ============================================================

void AudioClip::Release() {
    if (m_BufferID) {
        alDeleteBuffers(1, &m_BufferID);
        m_BufferID = 0;
    }
    m_Info = {};
    SetState(ResourceState::Unloaded);
}

} // namespace Engine
