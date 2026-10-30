#pragma once

/**
 * @file OpenALBackend.h
 * @brief T-7-A internal OpenAL backend seam.
 *
 * This is an internal C++ boundary for deterministic runtime tests; it is not a
 * Bridge/Editor ABI. Production uses the real OpenAL adapter in
 * engine/src/OpenAL/OpenALBackend.cpp.
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include <cstdint>

namespace Engine {

    enum class AudioBackendSourceState : uint8 {
        Initial = 0,
        Playing,
        Paused,
        Stopped,
    };

    enum class AudioBackendSourceParam : uint8 {
        Buffer = 0,
        Looping,
        DistanceModel,
        Relative,
        Position,
        Velocity,
        Gain,
        Pitch,
        ReferenceDistance,
        MaxDistance,
        ConeInnerAngle,
        ConeOuterAngle,
        ConeOuterGain,
        SecOffset,
    };

    enum class AudioBackendEffectParam : uint8 {
        Type = 0,
        Density,
        Diffusion,
        Gain,
        GainHF,
        DecayTime,
        DecayHFRatio,
    };

    enum class AudioBackendEffectType : int32 {
        Reverb = 1, // AL_EFFECT_REVERB
    };

    enum class AudioBackendFilterParam : uint8 {
        Type = 0,
        Gain,
        GainHF,
    };

    enum class AudioBackendFilterType : int32 {
        LowPass = 1, // AL_FILTER_LOWPASS
    };

    enum class AudioBackendSlotParam : uint8 {
        Effect = 0,
    };

    class IOpenALBackend {
    public:
        virtual ~IOpenALBackend() = default;

        // ── Device / context ──
        virtual bool OpenDevice() = 0;
        virtual void CloseDevice() = 0;
        virtual bool CreateContext() = 0;
        virtual void DestroyContext() = 0;
        virtual bool MakeContextCurrent() = 0;
        virtual void ClearContextCurrent() = 0;
        virtual void* GetDeviceHandle() const = 0;
        virtual void* GetContextHandle() const = 0;
        virtual bool IsExtensionPresent(const char* name) const = 0;
        virtual bool IsDeviceConnected() const = 0;

        // ── Listener / global ──
        virtual bool SetDistanceModel(AttenuationMode mode) = 0;
        virtual bool SetListenerPosition(const Vec3& position) = 0;
        virtual bool GetListenerPosition(Vec3& position) const = 0;
        virtual bool SetListenerOrientation(const Vec3& forward,
                                            const Vec3& up) = 0;
        virtual bool SetListenerGain(float32 gain) = 0;
        virtual bool GetListenerGain(float32& gain) const = 0;

        // ── Source ──
        virtual uint32 GenSource() = 0;
        virtual bool DeleteSource(uint32 source) = 0;
        virtual bool SourcePlay(uint32 source) = 0;
        virtual bool SourceStop(uint32 source) = 0;
        virtual bool SourcePause(uint32 source) = 0;
        virtual AudioBackendSourceState GetSourceState(uint32 source) const = 0;
        virtual bool SetSourceInt(uint32 source,
                                  AudioBackendSourceParam param,
                                  int32 value) = 0;
        virtual bool SetSourceFloat(uint32 source,
                                    AudioBackendSourceParam param,
                                    float32 value) = 0;
        virtual bool SetSourceVec3(uint32 source,
                                   AudioBackendSourceParam param,
                                   const Vec3& value) = 0;
        virtual bool GetSourceFloat(uint32 source,
                                    AudioBackendSourceParam param,
                                    float32& value) const = 0;
        virtual bool GetSourceVec3(uint32 source,
                                   AudioBackendSourceParam param,
                                   Vec3& value) const = 0;

        // ── Buffer ──
        virtual uint32 GenBuffer() = 0;
        virtual bool DeleteBuffer(uint32 buffer) = 0;
        virtual bool BufferData(uint32 buffer,
                                AudioFormat format,
                                const void* pcmData,
                                int32 dataSize,
                                int32 sampleRate) = 0;

        // ── EFX (T-7-B) ──
        virtual bool AreEfxEntryPointsAvailable() const = 0;
        virtual int32 GetMaxAuxiliarySends() const = 0;

        virtual uint32 GenEffect() = 0;
        virtual bool DeleteEffect(uint32 effect) = 0;
        virtual bool SetEffectInt(uint32 effect,
                                  AudioBackendEffectParam param,
                                  int32 value) = 0;
        virtual bool SetEffectFloat(uint32 effect,
                                    AudioBackendEffectParam param,
                                    float32 value) = 0;
        virtual bool GetEffectFloat(uint32 effect,
                                    AudioBackendEffectParam param,
                                    float32& value) const = 0;

        virtual uint32 GenAuxiliarySlot() = 0;
        virtual bool DeleteAuxiliarySlot(uint32 slot) = 0;
        virtual bool SetAuxiliarySlotInt(uint32 slot,
                                         AudioBackendSlotParam param,
                                         int32 value) = 0;

        virtual uint32 GenFilter() = 0;
        virtual bool DeleteFilter(uint32 filter) = 0;
        virtual bool SetFilterInt(uint32 filter,
                                  AudioBackendFilterParam param,
                                  int32 value) = 0;
        virtual bool SetFilterFloat(uint32 filter,
                                    AudioBackendFilterParam param,
                                    float32 value) = 0;
        virtual bool GetFilterFloat(uint32 filter,
                                    AudioBackendFilterParam param,
                                    float32& value) const = 0;

        virtual bool SetSourceAuxSend(uint32 source,
                                      int32 send,
                                      uint32 slot,
                                      uint32 filter) = 0;
        virtual bool SetSourceDirectFilter(uint32 source, uint32 filter) = 0;
    };

} // namespace Engine
