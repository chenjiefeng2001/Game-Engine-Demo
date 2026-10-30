#pragma once

/**
 * @file OpenALAudioEngine.h
 * @brief OpenAL Soft 音频引擎实现（Stack 2 canonical）
 *
 * T-7-A: single-owner-thread runtime, explicit engine state, engine-owned
 * resources, fail-closed device failure, and deterministic backend seam.
 */

#include "Engine/Core/Audio/IAudioEngine.h"
#include "Engine/Core/Audio/IAudioEfx.h"
#include <memory>

namespace Engine {

    class AudioEngineRuntime;
    class IOpenALBackend;
    struct OpenALEfxState;

    class OpenALAudioEngine : public IAudioEngine, public IAudioEfx {
    public:
        OpenALAudioEngine();
        explicit OpenALAudioEngine(std::shared_ptr<IOpenALBackend> backend);
        ~OpenALAudioEngine() override;

        // ── IAudioEngine ──
        bool Init() override;
        AudioStatus Shutdown() override;
        bool IsInitialized() const override;
        AudioEngineState GetState() const override;
        AudioStatus GetLastStatus() const override;

        std::shared_ptr<IAudioBuffer> CreateBuffer(
            const void* pcmData, int32 dataSize,
            const AudioClipInfo& info) override;
        AudioStatus DestroyBuffer(IAudioBuffer* buffer) override;

        std::shared_ptr<IAudioSource> CreateSource() override;
        AudioStatus DestroySource(IAudioSource* source) override;

        AudioStatus SetListenerPosition(const Vec3& position) override;
        Vec3 GetListenerPosition() const override;
        AudioStatus SetListenerOrientation(const Vec3& forward,
                                           const Vec3& up) override;
        AudioStatus SetListenerVolume(float32 gain) override;

        AudioStatus SetMasterVolume(float32 gain) override;
        float32 GetMasterVolume() const override;
        AudioStatus SetDistanceModel(AttenuationMode mode) override;

        AudioStatus Update() override;

        void* GetNativeDevice() const override;
        void* GetNativeContext() const override;

        // ── IAudioEfx ──
        AudioEfxCapabilities GetEfxCapabilities() const override;

        AudioEfxHandle CreateReverb() override;
        AudioStatus SetReverbParameters(
            const AudioEfxHandle& effect,
            const AudioReverbParameters& params) override;
        AudioStatus GetReverbParameters(
            const AudioEfxHandle& effect,
            AudioReverbParameters& out) const override;

        AudioEfxHandle CreateAuxiliarySlot() override;
        AudioStatus AttachEffect(
            const AudioEfxHandle& slot,
            const AudioEfxHandle& effect) override;
        AudioStatus DetachEffect(const AudioEfxHandle& slot) override;

        AudioStatus ConnectAuxSend(
            IAudioSource* source,
            const AudioEfxHandle& slot,
            float32 sendLevel) override;
        AudioStatus DisconnectAuxSend(IAudioSource* source) override;

        AudioEfxHandle CreateLowPassFilter() override;
        AudioStatus SetLowPassParameters(
            const AudioEfxHandle& filter,
            const AudioLowPassParameters& params) override;
        AudioStatus GetLowPassParameters(
            const AudioEfxHandle& filter,
            AudioLowPassParameters& out) const override;
        AudioStatus BindDirectFilter(
            IAudioSource* source,
            const AudioEfxHandle& filter) override;
        AudioStatus UnbindDirectFilter(IAudioSource* source) override;

        AudioStatus DestroyEfxObject(const AudioEfxHandle& handle) override;

    private:
        AudioStatus CheckEfxReady() const;
        void InitializeEfxCapabilities();
        void ShutdownEfx(bool backendAvailable);
        AudioStatus CloseSourceEfx(uint32 sourceId, bool backendAvailable);

        std::shared_ptr<AudioEngineRuntime> m_Runtime;
        std::unique_ptr<OpenALEfxState> m_Efx;
    };

} // namespace Engine
