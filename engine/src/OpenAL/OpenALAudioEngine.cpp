#include "Engine/OpenAL/OpenALAudioEngine.h"
#include "Engine/OpenAL/OpenALAudioBuffer.h"
#include "Engine/OpenAL/OpenALAudioSource.h"
#include <iostream>
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine {

    // ============================================================
    // 工具：AttenuationMode → OpenAL 距离模型
    // ============================================================
    static ALenum ToALDistanceModel(AttenuationMode mode) {
        switch (mode) {
            case AttenuationMode::None:           return AL_NONE;
            case AttenuationMode::Inverse:        return AL_INVERSE_DISTANCE;
            case AttenuationMode::InverseClamped: return AL_INVERSE_DISTANCE_CLAMPED;
            case AttenuationMode::Linear:         return AL_LINEAR_DISTANCE;
            case AttenuationMode::LinearClamped:  return AL_LINEAR_DISTANCE_CLAMPED;
        }
        return AL_INVERSE_DISTANCE_CLAMPED;
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    OpenALAudioEngine::OpenALAudioEngine() = default;

    OpenALAudioEngine::~OpenALAudioEngine() {
        Shutdown();
    }

    // ============================================================
    // 初始化 / 关闭
    // ============================================================

    bool OpenALAudioEngine::Init() {
        if (m_Initialized) return true;

        // 打开默认音频设备
        m_Device = alcOpenDevice(nullptr);
        if (!m_Device) {
            std::cerr << "[OpenAL] Failed to open default audio device" << std::endl;
            return false;
        }

        // 创建音频上下文
        m_Context = alcCreateContext(m_Device, nullptr);
        if (!m_Context) {
            std::cerr << "[OpenAL] Failed to create audio context" << std::endl;
            alcCloseDevice(m_Device);
            m_Device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(m_Context)) {
            std::cerr << "[OpenAL] Failed to make context current" << std::endl;
            alcDestroyContext(m_Context);
            m_Context = nullptr;
            alcCloseDevice(m_Device);
            m_Device = nullptr;
            return false;
        }

        // 设置默认距离模型
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

        std::cout << "[OpenAL] Initialized successfully" << std::endl;
        std::cout << "  Vendor:   " << alGetString(AL_VENDOR) << std::endl;
        std::cout << "  Version:  " << alGetString(AL_VERSION) << std::endl;
        std::cout << "  Renderer: " << alGetString(AL_RENDERER) << std::endl;

        m_Initialized = true;
        return true;
    }

    void OpenALAudioEngine::Shutdown() {
        m_Sources.clear();

        if (m_Context) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(m_Context);
            m_Context = nullptr;
        }

        if (m_Device) {
            alcCloseDevice(m_Device);
            m_Device = nullptr;
        }

        m_Initialized = false;
    }

    // ============================================================
    // 资源管理
    // ============================================================

    std::shared_ptr<IAudioBuffer> OpenALAudioEngine::CreateBuffer(
        const void* pcmData, int32 dataSize, const AudioClipInfo& info) {
        auto buffer = std::make_shared<OpenALAudioBuffer>();
        buffer->Load(pcmData, dataSize, info);
        return buffer;
    }

    std::shared_ptr<IAudioSource> OpenALAudioEngine::CreateSource() {
        auto source = std::make_shared<OpenALAudioSource>();
        m_Sources.push_back(source);
        return source;
    }

    void OpenALAudioEngine::DestroySource(IAudioSource* source) {
        if (!source) return;
        for (auto it = m_Sources.begin(); it != m_Sources.end(); ++it) {
            if (it->get() == source) {
                m_Sources.erase(it);
                return;
            }
        }
    }

    // ============================================================
    // 听者管理
    // ============================================================

    void OpenALAudioEngine::SetListenerPosition(const Vec3& position) {
        alListener3f(AL_POSITION, position.x, position.y, position.z);
    }

    Vec3 OpenALAudioEngine::GetListenerPosition() const {
        ALfloat x, y, z;
        alGetListener3f(AL_POSITION, &x, &y, &z);
        return Vec3(x, y, z);
    }

    void OpenALAudioEngine::SetListenerOrientation(const Vec3& forward,
                                                    const Vec3& up) {
        ALfloat orient[6] = { forward.x, forward.y, forward.z,
                              up.x, up.y, up.z };
        alListenerfv(AL_ORIENTATION, orient);
    }

    void OpenALAudioEngine::SetListenerVolume(float32 gain) {
        alListenerf(AL_GAIN, gain);
    }

    // ============================================================
    // 全局控制
    // ============================================================

    void OpenALAudioEngine::SetMasterVolume(float32 gain) {
        alListenerf(AL_GAIN, gain);
    }

    float32 OpenALAudioEngine::GetMasterVolume() const {
        ALfloat gain;
        alGetListenerf(AL_GAIN, &gain);
        return static_cast<float32>(gain);
    }

    void OpenALAudioEngine::SetDistanceModel(AttenuationMode mode) {
        alDistanceModel(ToALDistanceModel(mode));
    }

    // ============================================================
    // 每帧更新
    // ============================================================

    void OpenALAudioEngine::Update() {
        // 清理已停止的 source 引用（可选）
        // OpenAL 的源状态由用户管理
    }

} // namespace Engine
