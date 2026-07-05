#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <algorithm>
#include <cmath>

namespace {
    Engine::Logger s_Log("AudioEngine");
}

using ALDevice  = ALCdevice;
using ALContext = ALCcontext;

namespace Engine {
namespace Audio {

AudioEngine::AudioEngine()
{
}

AudioEngine::~AudioEngine()
{
    if (IsInitialized()) Shutdown();
}

bool AudioEngine::Initialize()
{
    if (IsInitialized()) return true;

    ALDevice* device = alcOpenDevice(nullptr);
    if (!device) { s_Log.Error("Failed to open audio device"); return false; }

    ALContext* context = alcCreateContext(device, nullptr);
    if (!context) { s_Log.Error("Failed to create context"); alcCloseDevice(device); return false; }

    if (!alcMakeContextCurrent(context))
    {
        s_Log.Error("Failed to make context current");
        alcDestroyContext(context);
        alcCloseDevice(device);
        return false;
    }

    m_Device  = static_cast<void*>(device);
    m_Context = static_cast<void*>(context);

    // 清除 OpenAL 错误队列中的任何残留错误
    (void)alGetError();  // 吸收设备初始化期间产生的任何虚警

    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat orientation[] = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
    alListenerfv(AL_ORIENTATION, orientation);
    alListenerf(AL_GAIN, m_MasterVolume);
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alSpeedOfSound(343.3f);
    alDopplerFactor(1.0f);

    m_Initialized = true;
    s_Log.Info("AudioEngine initialized");
    return true;
}

void AudioEngine::Shutdown()
{
    if (!m_Initialized) return;

    // 步骤 1: 销毁所有 AudioSource（需要 context 仍有效）
    m_Sources.clear();
    m_SourcePool.clear();

    // 步骤 2: 设置无 context → 此后不再调用任何 al* 函数
    ALContext* ctx = static_cast<ALContext*>(m_Context);
    ALDevice*  dev = static_cast<ALDevice*>(m_Device);

    if (ctx)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(ctx);
    }
    m_Context = nullptr;

    if (dev) alcCloseDevice(dev);
    m_Device = nullptr;

    m_Initialized = false;
    s_Log.Info("AudioEngine shut down");
}

void AudioEngine::Update(float deltaTime)
{
    if (!IsInitialized()) return;

    for (auto it = m_Sources.begin(); it != m_Sources.end();)
    {
        if (it->second.GetState() == AudioSourceState::Stopped)
            it = m_Sources.erase(it);
        else
            ++it;
    }

    if (!m_Sources.empty()) ComputeIndirectPaths();

    for (auto pit = m_SourcePool.begin(); pit != m_SourcePool.end();)
    {
        auto* src = GetSource(*pit);
        if (!src || src->GetState() == AudioSourceState::Stopped)
            pit = m_SourcePool.erase(pit);
        else
            ++pit;
    }
}

void AudioEngine::SetMasterVolume(float volume)
{
    m_MasterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (IsInitialized()) alListenerf(AL_GAIN, m_MasterVolume);
}
float AudioEngine::GetMasterVolume() const { return m_MasterVolume; }

void AudioEngine::SetPaused(bool paused)
{
    m_Paused = paused;
    if (!IsInitialized()) return;
    for (auto& [id, source] : m_Sources)
    {
        if (paused && source.IsPlaying()) source.Pause();
        else if (!paused) source.Play();
    }
}

void AudioEngine::SetSpeedOfSound(float speed)
{
    if (IsInitialized()) alSpeedOfSound(std::max(speed, 1.0f));
}
void AudioEngine::SetDopplerFactor(float factor)
{
    if (IsInitialized()) alDopplerFactor(std::clamp(factor, 0.0f, 10.0f));
}

AudioSourceHandle AudioEngine::CreateSource(const std::string& name)
{
    if (!IsInitialized()) return InvalidAudioSource;
    if (!m_Context) return InvalidAudioSource;

    AudioSourceHandle handle = m_NextHandle++;

    ALContext* ctx = static_cast<ALContext*>(m_Context);
    alcMakeContextCurrent(ctx);

    ALuint alSource = 0;
    alGenSources(1, &alSource);
    if (alGetError() != AL_NO_ERROR)
    {
        s_Log.Error("CreateSource: Failed to generate source");
        return InvalidAudioSource;
    }

    // 将原生句柄和 context 传递给 AudioSource
    auto [it, inserted] = m_Sources.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(handle),
        std::forward_as_tuple(handle, static_cast<uint32_t>(alSource), m_Context));

    if (inserted && !name.empty())
        s_Log.Trace("Created source '{}' (handle: {})", name, handle);

    return inserted ? handle : InvalidAudioSource;
}

void AudioEngine::DestroySource(AudioSourceHandle handle)
{
    auto it = m_Sources.find(handle);
    if (it != m_Sources.end())
    {
        // Stop() 内部会调用 MakeContextCurrent → 确保 context 有效
        it->second.Stop();
        m_Sources.erase(it);

        // 从池中移除
        std::erase(m_SourcePool, handle);
    }
}

AudioSource* AudioEngine::GetSource(AudioSourceHandle handle)
{
    auto it = m_Sources.find(handle);
    return (it != m_Sources.end()) ? &it->second : nullptr;
}

float AudioEngine::GetSourcePlaybackTime(AudioSourceHandle handle) const
{
    auto it = m_Sources.find(handle);
    return (it != m_Sources.end()) ? it->second.GetPlaybackTime() : 0.0f;
}

void AudioEngine::SetListenerPosition(const glm::vec3& pos)
{
    m_ListenerPos = pos;
    ApplyListenerToOpenAL();
}
void AudioEngine::SetListenerOrientation(const glm::vec3& forward, const glm::vec3& up)
{
    m_ListenerForward = forward;
    m_ListenerUp = up;
    ApplyListenerToOpenAL();
}
void AudioEngine::SetListenerVelocity(const glm::vec3& vel)
{
    m_ListenerVelocity = vel;
    ApplyListenerToOpenAL();
}
glm::vec3 AudioEngine::GetListenerPosition() const { return m_ListenerPos; }

void AudioEngine::ApplyListenerToOpenAL() const
{
    ALContext* ctx = static_cast<ALContext*>(m_Context);
    if (!ctx) return;

    alcMakeContextCurrent(ctx);
    alListener3f(AL_POSITION, m_ListenerPos.x, m_ListenerPos.y, m_ListenerPos.z);
    alListener3f(AL_VELOCITY, m_ListenerVelocity.x, m_ListenerVelocity.y, m_ListenerVelocity.z);
    ALfloat orientation[6] = {
        m_ListenerForward.x, m_ListenerForward.y, m_ListenerForward.z,
        m_ListenerUp.x,      m_ListenerUp.y,      m_ListenerUp.z
    };
    alListenerfv(AL_ORIENTATION, orientation);
}

AudioSourceHandle AudioEngine::PlayOneShot(
    const std::shared_ptr<::Engine::AudioClip>& clip, float volume, float pitch)
{
    if (!IsInitialized() || !clip || !clip->IsLoaded()) return InvalidAudioSource;

    AudioSourceHandle handle = InvalidAudioSource;
    for (auto poolId : m_SourcePool)
    {
        auto* src = GetSource(poolId);
        if (src && src->GetState() == AudioSourceState::Stopped) { handle = poolId; break; }
    }

    if (handle == InvalidAudioSource)
    {
        handle = CreateSource("oneshot");
        if (handle == InvalidAudioSource) return InvalidAudioSource;
        m_SourcePool.push_back(handle);
    }

    auto* src = GetSource(handle);
    if (!src) return InvalidAudioSource;

    src->SetClip(clip.get());
    src->SetVolume(volume);
    src->SetPitch(pitch);
    src->SetLooping(false);
    src->Play();
    return handle;
}

void AudioEngine::SetPoolSize(size_t size)
{
    while (m_SourcePool.size() < size)
    {
        auto h = CreateSource("pool");
        if (h == InvalidAudioSource) break;
        m_SourcePool.push_back(h);
    }
    while (m_SourcePool.size() > size)
    {
        DestroySource(m_SourcePool.back());
    }
}

size_t AudioEngine::GetActiveVoiceCount() const
{
    size_t count = 0;
    for (const auto& [id, src] : m_Sources)
        if (src.GetState() == AudioSourceState::Playing) count++;
    return count;
}

void AudioEngine::ComputeIndirectPaths()
{
    static float counter = 0.0f;
    counter += 0.016f;
    if (counter > 1.0f)
    {
        counter = 0.0f;
        s_Log.Trace("LTI path: {} active sources", GetActiveVoiceCount());
    }
}

} // namespace Audio
} // namespace Engine