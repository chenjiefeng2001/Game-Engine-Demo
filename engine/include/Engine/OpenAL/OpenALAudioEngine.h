#pragma once

/**
 * @file OpenALAudioEngine.h
 * @brief OpenAL Soft 音频引擎实现
 *
 * 实现 IAudioEngine 接口，封装 alcOpenDevice / alcCreateContext 等 OpenAL 操作。
 * 公开头文件，sandbox 可直接 include 创建音频引擎。
 */

#include "Engine/Core/Audio/IAudioEngine.h"
#include <memory>
#include <vector>

struct ALCdevice;
struct ALCcontext;

namespace Engine {

    class OpenALAudioEngine : public IAudioEngine {
    public:
        OpenALAudioEngine();
        ~OpenALAudioEngine() override;

        // ── IAudioEngine ──
        bool Init() override;
        void Shutdown() override;
        bool IsInitialized() const override { return m_Initialized; }

        std::shared_ptr<IAudioBuffer> CreateBuffer(
            const void* pcmData, int32 dataSize,
            const AudioClipInfo& info) override;

        std::shared_ptr<IAudioSource> CreateSource() override;
        void DestroySource(IAudioSource* source) override;

        void SetListenerPosition(const Vec3& position) override;
        Vec3 GetListenerPosition() const override;
        void SetListenerOrientation(const Vec3& forward, const Vec3& up) override;
        void SetListenerVolume(float32 gain) override;

        void SetMasterVolume(float32 gain) override;
        float32 GetMasterVolume() const override;
        void SetDistanceModel(AttenuationMode mode) override;

        void Update() override;

        void* GetNativeDevice() const override { return m_Device; }
        void* GetNativeContext() const override { return m_Context; }

    private:
        ALCdevice*  m_Device  = nullptr;
        ALCcontext* m_Context = nullptr;
        bool m_Initialized = false;

        // 管理已创建的 source，用于清理
        std::vector<std::shared_ptr<class OpenALAudioSource>> m_Sources;
    };

} // namespace Engine
