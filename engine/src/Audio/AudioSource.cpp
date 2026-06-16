#include "Engine/Audio/AudioSource.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <cstring>

namespace {
    Engine::Logger s_Log("AudioSource");
}

// 安全检测：仅在当前有 context 时才执行 alGetError()
static bool SafeCheckALError()
{
    if (!alcGetCurrentContext()) return false;
    ALenum err = alGetError();
    return err == AL_NO_ERROR;
}

// 安全删除 OpenAL source：确保 context 存在且 source 有效
static void SafeDeleteSource(ALuint source, ALCcontext* ctx)
{
    if (source == AL_NONE || source == 0) return;
    if (!ctx) return;
    alcMakeContextCurrent(ctx);
    alDeleteSources(1, &source);
    SafeCheckALError(); // 吸收残留错误，不触发警告
}

namespace Engine {
namespace Audio {

AudioSource::AudioSource(uint32_t id, uint32_t nativeHandle, void* context)
    : m_id(id)
    , m_SourceID(nativeHandle)
    , m_Context(context)
{
}

void AudioSource::MakeContextCurrent() const
{
    if (m_Context)
        alcMakeContextCurrent(static_cast<ALCcontext*>(m_Context));
}

AudioSource::~AudioSource()
{
    SafeDeleteSource(m_SourceID, static_cast<ALCcontext*>(m_Context));
    m_SourceID = AL_NONE;
}

AudioSource::AudioSource(AudioSource&& other) noexcept
    : m_id(other.m_id)
    , m_Context(other.m_Context)
    , m_SourceID(other.m_SourceID)
    , m_State(other.m_State)
    , m_Volume(other.m_Volume)
    , m_Pitch(other.m_Pitch)
    , m_Looping(other.m_Looping)
    , m_Priority(other.m_Priority)
    , m_SpatializationEnabled(other.m_SpatializationEnabled)
    , m_Position(other.m_Position)
    , m_Velocity(other.m_Velocity)
    , m_Direction(other.m_Direction)
    , m_DistanceModel(other.m_DistanceModel)
    , m_MinDistance(other.m_MinDistance)
    , m_MaxDistance(other.m_MaxDistance)
    , m_RolloffFactor(other.m_RolloffFactor)
    , m_ReferenceDistance(other.m_ReferenceDistance)
    , m_ConeInnerAngle(other.m_ConeInnerAngle)
    , m_ConeOuterAngle(other.m_ConeOuterAngle)
    , m_ConeOuterGain(other.m_ConeOuterGain)
    , m_DopplerFactor(other.m_DopplerFactor)
    , m_Occlusion(other.m_Occlusion)
    , m_Obstruction(other.m_Obstruction)
    , m_Exclusion(other.m_Exclusion)
    , m_Diffraction(other.m_Diffraction)
    , m_DiffractionAngle(other.m_DiffractionAngle)
    , m_CurrentSpatialIR(std::move(other.m_CurrentSpatialIR))
    , m_AuxSends(std::move(other.m_AuxSends))
    , m_Clip(other.m_Clip)
{
    other.m_id = 0;
    other.m_Context = nullptr;
    other.m_SourceID = AL_NONE;
    other.m_State = AudioSourceState::Stopped;
    other.m_Clip = nullptr;
}

AudioSource& AudioSource::operator=(AudioSource&& other) noexcept
{
    if (this != &other)
    {
        SafeDeleteSource(m_SourceID, static_cast<ALCcontext*>(m_Context));

        m_id = other.m_id;
        m_Context = other.m_Context;
        m_SourceID = other.m_SourceID;
        m_State = other.m_State;
        m_Volume = other.m_Volume;
        m_Pitch = other.m_Pitch;
        m_Looping = other.m_Looping;
        m_Priority = other.m_Priority;
        m_SpatializationEnabled = other.m_SpatializationEnabled;
        m_Position = other.m_Position;
        m_Velocity = other.m_Velocity;
        m_Direction = other.m_Direction;
        m_DistanceModel = other.m_DistanceModel;
        m_MinDistance = other.m_MinDistance;
        m_MaxDistance = other.m_MaxDistance;
        m_RolloffFactor = other.m_RolloffFactor;
        m_ReferenceDistance = other.m_ReferenceDistance;
        m_ConeInnerAngle = other.m_ConeInnerAngle;
        m_ConeOuterAngle = other.m_ConeOuterAngle;
        m_ConeOuterGain = other.m_ConeOuterGain;
        m_DopplerFactor = other.m_DopplerFactor;
        m_Occlusion = other.m_Occlusion;
        m_Obstruction = other.m_Obstruction;
        m_Exclusion = other.m_Exclusion;
        m_Diffraction = other.m_Diffraction;
        m_DiffractionAngle = other.m_DiffractionAngle;
        m_CurrentSpatialIR = std::move(other.m_CurrentSpatialIR);
        m_AuxSends = std::move(other.m_AuxSends);
        m_Clip = other.m_Clip;

        other.m_id = 0;
        other.m_Context = nullptr;
        other.m_SourceID = AL_NONE;
        other.m_State = AudioSourceState::Stopped;
        other.m_Clip = nullptr;
    }
    return *this;
}

void AudioSource::Play()
{
    if (!m_Clip) { s_Log.Warn("AudioSource {}: Cannot play - no clip set", m_id); return; }
    if (m_SourceID == AL_NONE) { s_Log.Error("AudioSource {}: No valid source handle", m_id); return; }

    MakeContextCurrent();

    ALuint bufferHandle = m_Clip->GetBufferHandle();
    alSourcei(m_SourceID, AL_BUFFER, static_cast<ALint>(bufferHandle));
    if (!SafeCheckALError())
    {
        s_Log.Error("AudioSource {}: Failed to bind buffer", m_id);
        return;
    }

    UpdateSpatialState();
    ApplyOcclusionToSource();
    ConfigureAuxSends();
    alSourcePlay(m_SourceID);
    m_State = AudioSourceState::Playing;
}

void AudioSource::Pause()
{
    if (m_SourceID != AL_NONE && IsPlaying())
    {
        MakeContextCurrent();
        alSourcePause(m_SourceID);
        m_State = AudioSourceState::Paused;
    }
}

void AudioSource::Stop()
{
    if (m_SourceID != AL_NONE)
    {
        MakeContextCurrent();
        alSourceStop(m_SourceID);
        alSourcei(m_SourceID, AL_BUFFER, 0);
    }
    m_State = AudioSourceState::Stopped;
}

void AudioSource::Restart() { Stop(); Play(); }

bool AudioSource::IsPlaying() const
{
    if (m_SourceID == AL_NONE) return false;
    MakeContextCurrent();
    ALint state;
    alGetSourcei(m_SourceID, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

AudioSourceState AudioSource::GetState() const
{
    if (m_SourceID != AL_NONE)
    {
        MakeContextCurrent();
        ALint state;
        alGetSourcei(m_SourceID, AL_SOURCE_STATE, &state);
        switch (state)
        {
        case AL_PLAYING: return AudioSourceState::Playing;
        case AL_PAUSED:  return AudioSourceState::Paused;
        default:         return AudioSourceState::Stopped;
        }
    }
    return m_State;
}

float AudioSource::GetPlaybackTime() const
{
    if (m_SourceID == AL_NONE) return 0.0f;
    MakeContextCurrent();
    ALfloat offset;
    alGetSourcef(m_SourceID, AL_SEC_OFFSET, &offset);
    return static_cast<float>(offset);
}

void AudioSource::LowLevelPause() { if (m_SourceID != AL_NONE) { MakeContextCurrent(); alSourcePause(m_SourceID); } }
void AudioSource::LowLevelResume() { if (m_SourceID != AL_NONE) { MakeContextCurrent(); alSourcePlay(m_SourceID); } }

void AudioSource::SetClip(::Engine::AudioClip* clip) { m_Clip = clip; }
::Engine::AudioClip* AudioSource::GetClip() const { return m_Clip; }

void AudioSource::SetVolume(float volume)
{
    m_Volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_GAIN, m_Volume); }
}
float AudioSource::GetVolume() const { return m_Volume; }

void AudioSource::SetPitch(float pitch)
{
    m_Pitch = std::clamp(pitch, 0.5f, 2.0f);
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_PITCH, m_Pitch); }
}
float AudioSource::GetPitch() const { return m_Pitch; }

void AudioSource::SetLooping(bool loop)
{
    m_Looping = loop;
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcei(m_SourceID, AL_LOOPING, loop ? AL_TRUE : AL_FALSE); }
}
bool AudioSource::IsLooping() const { return m_Looping; }

void AudioSource::SetPriority(int priority) { m_Priority = priority; }
int AudioSource::GetPriority() const { return m_Priority; }

void AudioSource::SetSpatializationEnabled(bool enabled)
{
    m_SpatializationEnabled = enabled;
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcei(m_SourceID, AL_SOURCE_RELATIVE, enabled ? AL_FALSE : AL_TRUE); }
}
bool AudioSource::IsSpatializationEnabled() const { return m_SpatializationEnabled; }

void AudioSource::SetPosition(const glm::vec3& pos)
{
    m_Position = pos;
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSource3f(m_SourceID, AL_POSITION, pos.x, pos.y, pos.z); }
}
glm::vec3 AudioSource::GetPosition() const { return m_Position; }

void AudioSource::SetVelocity(const glm::vec3& vel)
{
    m_Velocity = vel;
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSource3f(m_SourceID, AL_VELOCITY, vel.x, vel.y, vel.z); }
}
glm::vec3 AudioSource::GetVelocity() const { return m_Velocity; }

void AudioSource::SetDirection(const glm::vec3& dir)
{
    m_Direction = dir;
    if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSource3f(m_SourceID, AL_DIRECTION, dir.x, dir.y, dir.z); }
}
glm::vec3 AudioSource::GetDirection() const { return m_Direction; }

void AudioSource::SetDistanceModel(DistanceModel model) { m_DistanceModel = model; }
void AudioSource::SetMinDistance(float dist) { m_MinDistance = dist; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_REFERENCE_DISTANCE, dist); } }
void AudioSource::SetMaxDistance(float dist) { m_MaxDistance = dist; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_MAX_DISTANCE, dist); } }
void AudioSource::SetRolloffFactor(float factor) { m_RolloffFactor = factor; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_ROLLOFF_FACTOR, factor); } }
void AudioSource::SetReferenceDistance(float dist) { m_ReferenceDistance = dist; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_REFERENCE_DISTANCE, dist); } }

void AudioSource::SetConeInnerAngle(float angle) { m_ConeInnerAngle = angle; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_CONE_INNER_ANGLE, angle); } }
void AudioSource::SetConeOuterAngle(float angle) { m_ConeOuterAngle = angle; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_CONE_OUTER_ANGLE, angle); } }
void AudioSource::SetConeOuterGain(float gain) { m_ConeOuterGain = gain; if (m_SourceID != AL_NONE && m_Context) { MakeContextCurrent(); alSourcef(m_SourceID, AL_CONE_OUTER_GAIN, gain); } }
void AudioSource::SetDopplerFactor(float factor) { m_DopplerFactor = factor; }

void AudioSource::SetOcclusion(float occlusion) { m_Occlusion = std::clamp(occlusion, 0.0f, 1.0f); ApplyOcclusionToSource(); }
void AudioSource::SetObstruction(float obstruction) { m_Obstruction = std::clamp(obstruction, 0.0f, 1.0f); ApplyOcclusionToSource(); }
void AudioSource::SetExclusion(float exclusion) { m_Exclusion = std::clamp(exclusion, 0.0f, 1.0f); ApplyOcclusionToSource(); }
void AudioSource::SetDiffraction(float diffraction) { m_Diffraction = std::clamp(diffraction, 0.0f, 1.0f); ApplyOcclusionToSource(); }
void AudioSource::SetDiffractionAngle(float angle) { m_DiffractionAngle = angle; }

void AudioSource::ApplyOcclusionToSource()
{
    if (m_SourceID == AL_NONE) return;
    MakeContextCurrent();
    if (m_Exclusion > 0.5f)
    {
        alSourcef(m_SourceID, AL_GAIN, m_Volume * (1.0f - m_Exclusion));
        return;
    }
    float combinedFactor = std::max({ m_Occlusion, m_Obstruction, m_Diffraction });
    if (combinedFactor > 0.01f)
        alSourcef(m_SourceID, AL_GAIN, m_Volume * (1.0f - combinedFactor * 0.6f));
}

void AudioSource::ApplySpatialIR(const SpatialIR& ir)
{
    m_CurrentSpatialIR = std::make_unique<SpatialIR>(ir);
    if (m_SourceID != AL_NONE)
    {
        MakeContextCurrent();
        alSourcef(m_SourceID, AL_GAIN, m_Volume * ir.gain);
        m_Occlusion = ir.occlusion.occlusionFactor;
        m_Obstruction = ir.occlusion.obstructionFactor;
        m_Exclusion = ir.occlusion.exclusionFactor;
        m_Diffraction = ir.diffractionAmount;
        m_DiffractionAngle = ir.diffractionAngle;
        ApplyOcclusionToSource();
    }
}

const SpatialIR* AudioSource::GetCurrentSpatialIR() const { return m_CurrentSpatialIR.get(); }

void AudioSource::UpdateSpatialState()
{
    if (m_SourceID == AL_NONE) return;
    MakeContextCurrent();
    alSource3f(m_SourceID, AL_POSITION, m_Position.x, m_Position.y, m_Position.z);
    alSource3f(m_SourceID, AL_VELOCITY, m_Velocity.x, m_Velocity.y, m_Velocity.z);
    alSourcei(m_SourceID, AL_SOURCE_RELATIVE, m_SpatializationEnabled ? AL_FALSE : AL_TRUE);
}

void AudioSource::AddAuxSend(const std::string& busName, float sendLevel) { m_AuxSends[busName] = std::clamp(sendLevel, 0.0f, 1.0f); ConfigureAuxSends(); }
void AudioSource::RemoveAuxSend(const std::string& busName) { m_AuxSends.erase(busName); ConfigureAuxSends(); }
void AudioSource::ClearAuxSends() { m_AuxSends.clear(); ConfigureAuxSends(); }
const std::unordered_map<std::string, float>& AudioSource::GetAuxSends() const { return m_AuxSends; }

void AudioSource::ConfigureAuxSends()
{
    if (m_SourceID == AL_NONE) return;
    for (const auto& [busName, level] : m_AuxSends)
        s_Log.Trace("AuxSend: source {} -> bus '{}' (level: {:.2f})", m_id, busName, level);
}

} // namespace Audio
} // namespace Engine