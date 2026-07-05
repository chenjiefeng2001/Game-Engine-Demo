#pragma once

#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/AudioSource.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <array>
#include <memory>
#include <functional>

// 注意：遵循 RHI 抽象工厂原则
// - 不暴露任何 OpenAL 类型到公共接口
// - 所有硬件资源通过内部工厂创建
// - 底层 API (OpenAL/Vulkan/D3D12) 对高层完全透明

namespace Engine {
namespace Audio {

// 前向声明 — 隐藏底层 API 的具体类型
class AudioAssetManager;
class AudioBusManager;
class SpatialAudioManager;

/**
 * @brief 音频引擎 — Subsystem 级高层封装
 *
 * 设计原则（与 IGraphicsFactory / IRHIFactory 一致）：
 * - Initialize() 内部通过 IAudioEngine 工厂创建底层实现
 * - 所有 OpenAL/Vulkan/D3D12 类型完全隐藏在 .cpp 中
 * - 未来可切换后端实现（当前为 OpenAL Soft）
 */
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool Initialize();
    void Shutdown();
    void Update(float deltaTime);
    const char* GetName() const { return "AudioEngine"; }
    bool IsInitialized() const { return m_Initialized; }

    // ── 全局控制 ──
    void  SetMasterVolume(float volume);
    float GetMasterVolume() const;
    void  SetPaused(bool paused);
    void  SetSpeedOfSound(float speed);
    void  SetDopplerFactor(float factor);

    // ── 声源管理（通过句柄，不暴露原生类型） ──
    AudioSourceHandle CreateSource(const std::string& name = "");
    void              DestroySource(AudioSourceHandle handle);
    AudioSource*      GetSource(AudioSourceHandle handle);
    [[nodiscard]] float GetSourcePlaybackTime(AudioSourceHandle handle) const;

    // ── 听者管理 ──
    void SetListenerPosition(const glm::vec3& pos);
    void SetListenerOrientation(const glm::vec3& forward, const glm::vec3& up);
    void SetListenerVelocity(const glm::vec3& vel);
    glm::vec3 GetListenerPosition() const;

    // ── 声源池化 ──
    AudioSourceHandle PlayOneShot(const std::shared_ptr<::Engine::AudioClip>& clip,
                                  float volume = 1.0f, float pitch = 1.0f);
    void SetPoolSize(size_t size);
    [[nodiscard]] size_t GetActiveVoiceCount() const;

private:
    void ApplyListenerToOpenAL() const;
    void ComputeIndirectPaths();

    bool m_Initialized = false;

    // 底层 API 句柄（实现细节，在 .cpp 中完整定义）
    // 使用 void* 而非具体类型，完全隐藏底层 API
    void* m_Device  = nullptr;   // ALCdevice* (hidden)
    void* m_Context = nullptr;   // ALCcontext* (hidden)

    float m_MasterVolume = 1.0f;
    bool  m_Paused       = false;

    glm::vec3 m_ListenerPos      { 0.0f };
    glm::vec3 m_ListenerVelocity { 0.0f };
    glm::vec3 m_ListenerForward  { 0.0f, 0.0f, -1.0f };
    glm::vec3 m_ListenerUp       { 0.0f, 1.0f, 0.0f };

    AudioSourceHandle m_NextHandle = 1;
    std::unordered_map<AudioSourceHandle, AudioSource> m_Sources;
    std::vector<AudioSourceHandle> m_SourcePool;
    size_t m_VoiceCountEstimate = 0;
};

// ============================================================================
// AudioAssetManager — 音频资产管理
// ============================================================================
class AudioAssetManager
{
public:
    AudioAssetManager() = default;

    struct CachedEntry
    {
        std::shared_ptr<AudioClip> clip;
        uint64_t lastAccessTime = 0;
        size_t refCount = 0;
        bool isStreaming = false;
    };

    bool ImportAsset(const std::string& path);
    void BuildCache();
    void StreamAsset(const std::string& path);
    void ReleaseUnused();
    size_t GetMemoryUsage() const;
    void LoadAsync(const std::string& path,
                   std::function<void(std::shared_ptr<AudioClip>)> callback);
    std::shared_ptr<AudioClip> GetClip(const std::string& path);
    void UnloadClip(const std::string& path);
    void UnloadAll();
    void EnableHotReload(bool enabled);
    bool IsHotReloadEnabled() const;

private:
    uint64_t GetCurrentTimeMs() const;
    void CheckHotReload();
    std::unordered_map<std::string, CachedEntry> m_Cache;
    bool m_HotReload = false;
};

// ============================================================================
// AudioBusManager — 音频总线
// ============================================================================
class AudioBusManager
{
public:
    AudioBusManager() = default;

    AudioBusConfig* CreateBus(const std::string& name);
    void RemoveBus(const std::string& name);
    AudioBusConfig* GetBus(const std::string& name);
    void SetBusVolume(const std::string& name, float volume);
    void AddSend(const std::string& sourceBus, const std::string& destBus, float level);
    void RemoveSend(const std::string& sourceBus, const std::string& destBus);
    void MuteBus(const std::string& name, bool mute);
    void ApplyBusProcessing(float* buffer, size_t numFrames, uint32_t sampleRate);
    void AddEffect(const std::string& busName, std::unique_ptr<AudioEffect> effect);
    void RemoveEffect(const std::string& busName, const std::string& effectName);
    void ClearEffects(const std::string& busName);

private:
    std::unordered_map<std::string, AudioBusConfig> m_Buses;
    std::unordered_map<std::string, std::vector<std::unique_ptr<AudioEffect>>> m_Effects;
};

// ============================================================================
// SpatialAudioManager — 空间音频（纯数学运算，无 API 依赖）
// ============================================================================
class SpatialAudioManager
{
public:
    SpatialAudioManager();

    void RegisterSource(AudioSourceHandle handle, const glm::vec3& position);
    void UnregisterSource(AudioSourceHandle handle);
    void UpdateSourcePosition(AudioSourceHandle handle, const glm::vec3& position);

    std::vector<SpatialIR> ComputeSpatialIRs(
        const glm::vec3& sourcePos,
        const glm::vec3& listenerPos,
        const glm::vec3& listenerForward,
        const glm::vec3& listenerUp) const;

    using RaycastCallback = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;
    void SetRaycastCallback(RaycastCallback callback);

    struct GeometryTriangle
    {
        std::array<glm::vec3, 3> vertices;
        std::array<glm::vec3, 3> normals;
        float absorption  = 0.3f;
        float scattering  = 0.1f;
    };
    void SetSceneGeometry(const std::vector<GeometryTriangle>& triangles);
    void ClearSceneGeometry();

    struct DiffractionParams
    {
        float maxDiffractionAngle = glm::radians(90.0f);
        float diffractionStrength = 0.5f;
        float lowpassCutoff       = 2000.0f;
        bool  enableUTDDiffraction = true;
    };
    void SetDiffractionParams(const DiffractionParams& params);
    struct ReflectionParams
    {
        uint32_t maxReflectionOrder = 3;
        float    reflectionGainScale = 0.8f;
        float    distanceThreshold   = 50.0f;
        float    rirLength           = 0.5f;
    };
    void SetReflectionParams(const ReflectionParams& params);
    struct ObstructionParams
    {
        float occlusionLowpassCutoff   = 1000.0f;
        float obstructionLowpassCutoff = 500.0f;
        float exclusionThreshold       = 0.9f;
        bool  enableDiffractionOcclusion = true;
    };
    void SetOcclusionParams(const ObstructionParams& params);
    using IRCallback = std::function<void(const SpatialIR&)>;
    void RegisterIRCallback(IRCallback callback);

private:
    struct SpatialSourceData
    {
        glm::vec3 position;
        AudioSourceHandle handle;
    };

    std::vector<SpatialSourceData> m_Sources;
    RaycastCallback m_Raycast;
    std::vector<GeometryTriangle> m_Geometry;
    DiffractionParams m_DiffractionParams;
    ReflectionParams m_ReflectionParams;
    ObstructionParams m_OcclusionParams;
    IRCallback m_IRCallback;

    SpatialIR ComputeDirectPath(const glm::vec3& source, const glm::vec3& listener) const;
    std::vector<SpatialIR> ComputeReflections(
        const glm::vec3& source, const glm::vec3& listener,
        const glm::vec3& forward, const glm::vec3& up) const;
    SpatialIR ComputeDiffraction(
        const glm::vec3& source, const glm::vec3& listener,
        const glm::vec3& forward, const glm::vec3& up) const;
    OcclusionData ComputeOcclusion(const glm::vec3& source, const glm::vec3& listener) const;
    void ApplyLTIPropertyToIR(SpatialIR& ir, const glm::vec3& source, const glm::vec3& listener) const;
    void ApplyOcclusionToIR(SpatialIR& ir, const OcclusionData& occlusion) const;
    float ComputeUTDDiffraction(
        const glm::vec3& source, const glm::vec3& listener,
        const glm::vec3& edgeStart, const glm::vec3& edgeEnd) const;
};

} // namespace Audio
} // namespace Engine