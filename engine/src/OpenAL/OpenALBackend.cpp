#define AL_ALEXT_PROTOTYPES
#include "OpenALBackend.h"

#include "Engine/OpenAL/OpenALBackend.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

namespace {

    ALenum ToALDistanceModel(Engine::AttenuationMode mode) {
        switch (mode) {
            case Engine::AttenuationMode::None:           return AL_NONE;
            case Engine::AttenuationMode::Inverse:        return AL_INVERSE_DISTANCE;
            case Engine::AttenuationMode::InverseClamped: return AL_INVERSE_DISTANCE_CLAMPED;
            case Engine::AttenuationMode::Linear:         return AL_LINEAR_DISTANCE;
            case Engine::AttenuationMode::LinearClamped:  return AL_LINEAR_DISTANCE_CLAMPED;
        }
        return AL_INVERSE_DISTANCE_CLAMPED;
    }

    ALenum ToALSourceParam(Engine::AudioBackendSourceParam param) {
        using Param = Engine::AudioBackendSourceParam;
        switch (param) {
            case Param::Buffer:            return AL_BUFFER;
            case Param::Looping:           return AL_LOOPING;
            case Param::DistanceModel:     return AL_DISTANCE_MODEL;
            case Param::Relative:          return AL_SOURCE_RELATIVE;
            case Param::Position:          return AL_POSITION;
            case Param::Velocity:          return AL_VELOCITY;
            case Param::Gain:              return AL_GAIN;
            case Param::Pitch:             return AL_PITCH;
            case Param::ReferenceDistance: return AL_REFERENCE_DISTANCE;
            case Param::MaxDistance:       return AL_MAX_DISTANCE;
            case Param::ConeInnerAngle:    return AL_CONE_INNER_ANGLE;
            case Param::ConeOuterAngle:    return AL_CONE_OUTER_ANGLE;
            case Param::ConeOuterGain:     return AL_CONE_OUTER_GAIN;
            case Param::SecOffset:         return AL_SEC_OFFSET;
        }
        return AL_NONE;
    }

    ALenum ToALFormat(Engine::AudioFormat format) {
        switch (format) {
            case Engine::AudioFormat::Mono8:    return AL_FORMAT_MONO8;
            case Engine::AudioFormat::Mono16:   return AL_FORMAT_MONO16;
            case Engine::AudioFormat::Stereo8:  return AL_FORMAT_STEREO8;
            case Engine::AudioFormat::Stereo16: return AL_FORMAT_STEREO16;
        }
        return AL_NONE;
    }

    ALenum ToALEffectParam(Engine::AudioBackendEffectParam param) {
        using Param = Engine::AudioBackendEffectParam;
        switch (param) {
            case Param::Type:          return AL_EFFECT_TYPE;
            case Param::Density:       return AL_REVERB_DENSITY;
            case Param::Diffusion:     return AL_REVERB_DIFFUSION;
            case Param::Gain:          return AL_REVERB_GAIN;
            case Param::GainHF:        return AL_REVERB_GAINHF;
            case Param::DecayTime:     return AL_REVERB_DECAY_TIME;
            case Param::DecayHFRatio:  return AL_REVERB_DECAY_HFRATIO;
        }
        return AL_NONE;
    }

    ALenum ToALFilterParam(Engine::AudioBackendFilterParam param) {
        using Param = Engine::AudioBackendFilterParam;
        switch (param) {
            case Param::Type:   return AL_FILTER_TYPE;
            case Param::Gain:   return AL_LOWPASS_GAIN;
            case Param::GainHF: return AL_LOWPASS_GAINHF;
        }
        return AL_NONE;
    }

    ALenum ToALSlotParam(Engine::AudioBackendSlotParam param) {
        switch (param) {
            case Engine::AudioBackendSlotParam::Effect: return AL_EFFECTSLOT_EFFECT;
        }
        return AL_NONE;
    }

    bool NoALError() {
        return alGetError() == AL_NO_ERROR;
    }

} // namespace

namespace Engine {

    class OpenALBackend final : public IOpenALBackend {
    public:
        OpenALBackend() = default;
        ~OpenALBackend() override = default;

        bool OpenDevice() override {
            if (m_Device) return true;
            m_Device = alcOpenDevice(nullptr);
            return m_Device != nullptr;
        }

        void CloseDevice() override {
            if (m_Device) {
                alcCloseDevice(m_Device);
                m_Device = nullptr;
            }
        }

        bool CreateContext() override {
            if (!m_Device) return false;
            if (m_Context) return true;
            m_Context = alcCreateContext(m_Device, nullptr);
            return m_Context != nullptr;
        }

        void DestroyContext() override {
            if (m_Context) {
                alcDestroyContext(m_Context);
                m_Context = nullptr;
            }
        }

        bool MakeContextCurrent() override {
            if (!m_Context) return false;
            return alcMakeContextCurrent(m_Context) == ALC_TRUE;
        }

        void ClearContextCurrent() override {
            alcMakeContextCurrent(nullptr);
        }

        void* GetDeviceHandle() const override {
            return m_Device;
        }

        void* GetContextHandle() const override {
            return m_Context;
        }

        bool IsExtensionPresent(const char* name) const override {
            if (!m_Device || !name) return false;
            return alcIsExtensionPresent(m_Device, name) == ALC_TRUE;
        }

        bool IsDeviceConnected() const override {
            if (!m_Device) return false;
            if (alcIsExtensionPresent(m_Device, "ALC_EXT_disconnect") != ALC_TRUE) {
                return true;
            }
            ALCint connected = ALC_TRUE;
            alcGetIntegerv(m_Device, ALC_CONNECTED, 1, &connected);
            if (alcGetError(m_Device) != ALC_NO_ERROR) return false;
            return connected == ALC_TRUE;
        }

        bool SetDistanceModel(AttenuationMode mode) override {
            alGetError();
            alDistanceModel(ToALDistanceModel(mode));
            return NoALError();
        }

        bool SetListenerPosition(const Vec3& position) override {
            alGetError();
            alListener3f(AL_POSITION, position.x, position.y, position.z);
            return NoALError();
        }

        bool GetListenerPosition(Vec3& position) const override {
            alGetError();
            ALfloat x = 0.0f, y = 0.0f, z = 0.0f;
            alGetListener3f(AL_POSITION, &x, &y, &z);
            if (!NoALError()) return false;
            position = Vec3(x, y, z);
            return true;
        }

        bool SetListenerOrientation(const Vec3& forward, const Vec3& up) override {
            alGetError();
            ALfloat orient[6] = {
                forward.x, forward.y, forward.z,
                up.x, up.y, up.z
            };
            alListenerfv(AL_ORIENTATION, orient);
            return NoALError();
        }

        bool SetListenerGain(float32 gain) override {
            alGetError();
            alListenerf(AL_GAIN, gain);
            return NoALError();
        }

        bool GetListenerGain(float32& gain) const override {
            alGetError();
            ALfloat value = 0.0f;
            alGetListenerf(AL_GAIN, &value);
            if (!NoALError()) return false;
            gain = static_cast<float32>(value);
            return true;
        }

        uint32 GenSource() override {
            alGetError();
            ALuint source = 0;
            alGenSources(1, &source);
            if (!NoALError()) return 0;
            return static_cast<uint32>(source);
        }

        bool DeleteSource(uint32 source) override {
            ALuint value = static_cast<ALuint>(source);
            alGetError();
            alDeleteSources(1, &value);
            return NoALError();
        }

        bool SourcePlay(uint32 source) override {
            alGetError();
            alSourcePlay(static_cast<ALuint>(source));
            return NoALError();
        }

        bool SourceStop(uint32 source) override {
            alGetError();
            alSourceStop(static_cast<ALuint>(source));
            return NoALError();
        }

        bool SourcePause(uint32 source) override {
            alGetError();
            alSourcePause(static_cast<ALuint>(source));
            return NoALError();
        }

        AudioBackendSourceState GetSourceState(uint32 source) const override {
            alGetError();
            ALint state = AL_STOPPED;
            alGetSourcei(static_cast<ALuint>(source), AL_SOURCE_STATE, &state);
            if (!NoALError()) return AudioBackendSourceState::Stopped;
            switch (state) {
                case AL_INITIAL: return AudioBackendSourceState::Initial;
                case AL_PLAYING: return AudioBackendSourceState::Playing;
                case AL_PAUSED:  return AudioBackendSourceState::Paused;
                default:         return AudioBackendSourceState::Stopped;
            }
        }

        bool SetSourceInt(uint32 source, AudioBackendSourceParam param,
                          int32 value) override {
            alGetError();
            if (param == AudioBackendSourceParam::DistanceModel) {
                const auto mode = static_cast<AttenuationMode>(value);
                alSourcei(static_cast<ALuint>(source), AL_DISTANCE_MODEL,
                          static_cast<ALint>(ToALDistanceModel(mode)));
            } else {
                alSourcei(static_cast<ALuint>(source),
                          ToALSourceParam(param),
                          static_cast<ALint>(value));
            }
            return NoALError();
        }

        bool SetSourceFloat(uint32 source, AudioBackendSourceParam param,
                            float32 value) override {
            alGetError();
            alSourcef(static_cast<ALuint>(source),
                      ToALSourceParam(param),
                      value);
            return NoALError();
        }

        bool SetSourceVec3(uint32 source, AudioBackendSourceParam param,
                           const Vec3& value) override {
            alGetError();
            alSource3f(static_cast<ALuint>(source),
                       ToALSourceParam(param),
                       value.x, value.y, value.z);
            return NoALError();
        }

        bool GetSourceFloat(uint32 source, AudioBackendSourceParam param,
                            float32& value) const override {
            alGetError();
            ALfloat result = 0.0f;
            alGetSourcef(static_cast<ALuint>(source),
                         ToALSourceParam(param), &result);
            if (!NoALError()) return false;
            value = static_cast<float32>(result);
            return true;
        }

        bool GetSourceVec3(uint32 source, AudioBackendSourceParam param,
                           Vec3& value) const override {
            alGetError();
            ALfloat x = 0.0f, y = 0.0f, z = 0.0f;
            alGetSource3f(static_cast<ALuint>(source),
                          ToALSourceParam(param), &x, &y, &z);
            if (!NoALError()) return false;
            value = Vec3(x, y, z);
            return true;
        }

        uint32 GenBuffer() override {
            alGetError();
            ALuint buffer = 0;
            alGenBuffers(1, &buffer);
            if (!NoALError()) return 0;
            return static_cast<uint32>(buffer);
        }

        bool DeleteBuffer(uint32 buffer) override {
            ALuint value = static_cast<ALuint>(buffer);
            alGetError();
            alDeleteBuffers(1, &value);
            return NoALError();
        }

        bool BufferData(uint32 buffer, AudioFormat format,
                        const void* pcmData, int32 dataSize,
                        int32 sampleRate) override {
            const ALenum alFormat = ToALFormat(format);
            if (alFormat == AL_NONE || !pcmData || dataSize <= 0 || sampleRate <= 0) {
                return false;
            }
            alGetError();
            alBufferData(static_cast<ALuint>(buffer), alFormat,
                         pcmData, static_cast<ALsizei>(dataSize), sampleRate);
            return NoALError();
        }

        // ── EFX (T-7-B) ──
        bool AreEfxEntryPointsAvailable() const override {
            // The pinned OpenAL Soft build exports EFX entry points; the
            // extension query is the runtime gate. A fake backend can report
            // entry-point failure separately.
            return IsExtensionPresent("ALC_EXT_EFX");
        }

        int32 GetMaxAuxiliarySends() const override {
            if (!m_Device) return 0;
            ALCint sends = 0;
            alcGetIntegerv(m_Device, ALC_MAX_AUXILIARY_SENDS, 1, &sends);
            if (alcGetError(m_Device) != ALC_NO_ERROR) return 0;
            return static_cast<int32>(sends);
        }

        uint32 GenEffect() override {
            alGetError();
            ALuint effect = 0;
            alGenEffects(1, &effect);
            if (!NoALError()) return 0;
            return static_cast<uint32>(effect);
        }

        bool DeleteEffect(uint32 effect) override {
            ALuint value = static_cast<ALuint>(effect);
            alGetError();
            alDeleteEffects(1, &value);
            return NoALError();
        }

        bool SetEffectInt(uint32 effect, AudioBackendEffectParam param,
                          int32 value) override {
            alGetError();
            alEffecti(static_cast<ALuint>(effect), ToALEffectParam(param), value);
            return NoALError();
        }

        bool SetEffectFloat(uint32 effect, AudioBackendEffectParam param,
                            float32 value) override {
            alGetError();
            alEffectf(static_cast<ALuint>(effect), ToALEffectParam(param), value);
            return NoALError();
        }

        bool GetEffectFloat(uint32 effect, AudioBackendEffectParam param,
                            float32& value) const override {
            alGetError();
            ALfloat result = 0.0f;
            alGetEffectf(static_cast<ALuint>(effect), ToALEffectParam(param), &result);
            if (!NoALError()) return false;
            value = static_cast<float32>(result);
            return true;
        }

        uint32 GenAuxiliarySlot() override {
            alGetError();
            ALuint slot = 0;
            alGenAuxiliaryEffectSlots(1, &slot);
            if (!NoALError()) return 0;
            return static_cast<uint32>(slot);
        }

        bool DeleteAuxiliarySlot(uint32 slot) override {
            ALuint value = static_cast<ALuint>(slot);
            alGetError();
            alDeleteAuxiliaryEffectSlots(1, &value);
            return NoALError();
        }

        bool SetAuxiliarySlotInt(uint32 slot, AudioBackendSlotParam param,
                                 int32 value) override {
            alGetError();
            alAuxiliaryEffectSloti(static_cast<ALuint>(slot),
                                   ToALSlotParam(param), value);
            return NoALError();
        }

        uint32 GenFilter() override {
            alGetError();
            ALuint filter = 0;
            alGenFilters(1, &filter);
            if (!NoALError()) return 0;
            return static_cast<uint32>(filter);
        }

        bool DeleteFilter(uint32 filter) override {
            ALuint value = static_cast<ALuint>(filter);
            alGetError();
            alDeleteFilters(1, &value);
            return NoALError();
        }

        bool SetFilterInt(uint32 filter, AudioBackendFilterParam param,
                          int32 value) override {
            alGetError();
            alFilteri(static_cast<ALuint>(filter), ToALFilterParam(param), value);
            return NoALError();
        }

        bool SetFilterFloat(uint32 filter, AudioBackendFilterParam param,
                            float32 value) override {
            alGetError();
            alFilterf(static_cast<ALuint>(filter), ToALFilterParam(param), value);
            return NoALError();
        }

        bool GetFilterFloat(uint32 filter, AudioBackendFilterParam param,
                            float32& value) const override {
            alGetError();
            ALfloat result = 0.0f;
            alGetFilterf(static_cast<ALuint>(filter), ToALFilterParam(param), &result);
            if (!NoALError()) return false;
            value = static_cast<float32>(result);
            return true;
        }

        bool SetSourceAuxSend(uint32 source, int32 send,
                              uint32 slot, uint32 filter) override {
            alGetError();
            alSource3i(static_cast<ALuint>(source), AL_AUXILIARY_SEND_FILTER,
                       static_cast<ALint>(slot), send,
                       static_cast<ALint>(filter));
            return NoALError();
        }

        bool SetSourceDirectFilter(uint32 source, uint32 filter) override {
            alGetError();
            alSourcei(static_cast<ALuint>(source), AL_DIRECT_FILTER,
                      static_cast<ALint>(filter));
            return NoALError();
        }

    private:
        ALCdevice* m_Device = nullptr;
        ALCcontext* m_Context = nullptr;
    };

    std::shared_ptr<IOpenALBackend> CreateOpenALBackend() {
        return std::make_shared<OpenALBackend>();
    }

} // namespace Engine
