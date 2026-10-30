#pragma once

/**
 * @file IAudioEfx.h
 * @brief T-7-B minimal OpenAL EFX interface for Stack 2.
 *
 * This is an internal C++ interface; it is not a Bridge/Editor ABI. Handles are
 * non-owning logical tokens. Native OpenAL IDs stay inside the engine registry.
 */

#include "Engine/Core/Audio/AudioDefs.h"

namespace Engine {

    class IAudioSource;

    enum class AudioEfxType : uint8 {
        None = 0,
        Reverb,
        AuxiliarySlot,
        LowPassFilter,
    };

    struct AudioEfxHandle {
        uint32 engineId = 0;
        uint32 generation = 0;
        uint32 serial = 0;
        AudioEfxType type = AudioEfxType::None;

        bool IsValid() const {
            return engineId != 0 && generation != 0 && serial != 0 &&
                   type != AudioEfxType::None;
        }

        bool operator==(const AudioEfxHandle& other) const {
            return engineId == other.engineId &&
                   generation == other.generation &&
                   serial == other.serial &&
                   type == other.type;
        }

        bool operator!=(const AudioEfxHandle& other) const {
            return !(*this == other);
        }
    };

    struct AudioEfxCapabilities {
        bool supported = false;
        bool routingSupported = false;
        int32 maxAuxiliarySends = 0;
        AudioStatus status = AudioStatus::Unsupported;
    };

    struct AudioReverbParameters {
        float32 density = 1.0f;
        float32 diffusion = 1.0f;
        float32 gain = 0.32f;
        float32 gainHF = 0.89f;
        float32 decayTime = 1.49f;
        float32 decayHFRatio = 0.83f;
    };

    struct AudioLowPassParameters {
        float32 gain = 1.0f;
        float32 gainHF = 1.0f;
    };

    class IAudioEfx {
    public:
        virtual ~IAudioEfx() = default;

        virtual AudioEfxCapabilities GetEfxCapabilities() const = 0;

        virtual AudioEfxHandle CreateReverb() = 0;
        virtual AudioStatus SetReverbParameters(
            const AudioEfxHandle& effect,
            const AudioReverbParameters& params) = 0;
        virtual AudioStatus GetReverbParameters(
            const AudioEfxHandle& effect,
            AudioReverbParameters& out) const = 0;

        virtual AudioEfxHandle CreateAuxiliarySlot() = 0;
        virtual AudioStatus AttachEffect(
            const AudioEfxHandle& slot,
            const AudioEfxHandle& effect) = 0;
        virtual AudioStatus DetachEffect(const AudioEfxHandle& slot) = 0;

        virtual AudioStatus ConnectAuxSend(
            IAudioSource* source,
            const AudioEfxHandle& slot,
            float32 sendLevel) = 0;
        virtual AudioStatus DisconnectAuxSend(IAudioSource* source) = 0;

        virtual AudioEfxHandle CreateLowPassFilter() = 0;
        virtual AudioStatus SetLowPassParameters(
            const AudioEfxHandle& filter,
            const AudioLowPassParameters& params) = 0;
        virtual AudioStatus GetLowPassParameters(
            const AudioEfxHandle& filter,
            AudioLowPassParameters& out) const = 0;
        virtual AudioStatus BindDirectFilter(
            IAudioSource* source,
            const AudioEfxHandle& filter) = 0;
        virtual AudioStatus UnbindDirectFilter(IAudioSource* source) = 0;

        virtual AudioStatus DestroyEfxObject(const AudioEfxHandle& handle) = 0;
    };

} // namespace Engine
