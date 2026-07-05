#pragma once

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Core/Audio/AudioClip.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
namespace Audio {

/**
 * @brief 高层音源封装 — 遵循 RHI 抽象工厂原则
 */
class AudioSource
{
public:
    AudioSource(uint32_t id, uint32_t nativeHandle, void* context = nullptr);
    ~AudioSource();

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&& other) noexcept;
    AudioSource& operator=(AudioSource&& other) noexcept;

    void Play();
    void Pause();
    void Stop();
    void Restart();
    [[nodiscard]] bool IsPlaying() const;
    [[nodiscard]] AudioSourceState GetState() const;
    [[nodiscard]] float GetPlaybackTime() const;

    void LowLevelPause();
    void LowLevelResume();

    void SetClip(::Engine::AudioClip* clip);
    [[nodiscard]] ::Engine::AudioClip* GetClip() const;

    void  SetVolume(float volume);
    float GetVolume() const;
    void  SetPitch(float pitch);
    float GetPitch() const;
    void  SetLooping(bool loop);
    bool  IsLooping() const;
    void  SetPriority(int priority);
    int   GetPriority() const;

    void SetSpatializationEnabled(bool enabled);
    [[nodiscard]] bool IsSpatializationEnabled() const;

    void SetPosition(const glm::vec3& pos);
    [[nodiscard]] glm::vec3 GetPosition() const;
    void SetVelocity(const glm::vec3& vel);
    [[nodiscard]] glm::vec3 GetVelocity() const;
    void SetDirection(const glm::vec3& dir);
    [[nodiscard]] glm::vec3 GetDirection() const;

    void SetDistanceModel(DistanceModel model);
    void SetMinDistance(float dist);
    void SetMaxDistance(float dist);
    void SetRolloffFactor(float factor);
    void SetReferenceDistance(float dist);

    void SetConeInnerAngle(float angle);
    void SetConeOuterAngle(float angle);
    void SetConeOuterGain(float gain);
    void SetDopplerFactor(float factor);

    void SetOcclusion(float occlusion);
    void SetObstruction(float obstruction);
    void SetExclusion(float exclusion);
    void SetDiffraction(float diffraction);
    void SetDiffractionAngle(float angle);
    void ApplySpatialIR(const SpatialIR& ir);
    [[nodiscard]] const SpatialIR* GetCurrentSpatialIR() const;

    void AddAuxSend(const std::string& busName, float sendLevel);
    void RemoveAuxSend(const std::string& busName);
    void ClearAuxSends();
    [[nodiscard]] const std::unordered_map<std::string, float>& GetAuxSends() const;

    [[nodiscard]] uint32_t GetNativeHandle() const { return m_SourceID; }
    [[nodiscard]] uint32_t GetID() const { return m_id; }

private:
    void MakeContextCurrent() const;
    void UpdateSpatialState();
    void ApplyOcclusionToSource();
    void ApplySpatialIRToSource(const SpatialIR& ir);
    void ApplyFrequencyResponse(const std::vector<float>& coefficients);
    void ConfigureAuxSends();

    uint32_t m_id = 0;
    void*    m_Context = nullptr;  // ALCcontext* (隐藏底层类型)
    uint32_t m_SourceID = 0;
    AudioSourceState m_State = AudioSourceState::Stopped;

    ::Engine::AudioClip* m_Clip = nullptr;

    float m_Volume  = 1.0f;
    float m_Pitch   = 1.0f;
    bool  m_Looping = false;
    int   m_Priority = 0;
    bool  m_SpatializationEnabled = true;

    glm::vec3 m_Position  { 0.0f };
    glm::vec3 m_Velocity  { 0.0f };
    glm::vec3 m_Direction { 0.0f };

    DistanceModel m_DistanceModel = DistanceModel::InverseClamped;
    float m_MinDistance      = 1.0f;
    float m_MaxDistance      = 100.0f;
    float m_RolloffFactor    = 1.0f;
    float m_ReferenceDistance = 1.0f;
    float m_ConeInnerAngle = 360.0f;
    float m_ConeOuterAngle = 360.0f;
    float m_ConeOuterGain  = 0.0f;
    float m_DopplerFactor = 1.0f;

    float m_Occlusion   = 0.0f;
    float m_Obstruction = 0.0f;
    float m_Exclusion   = 0.0f;
    float m_Diffraction = 0.0f;
    float m_DiffractionAngle = 0.0f;

    std::unique_ptr<SpatialIR> m_CurrentSpatialIR;
    std::unordered_map<std::string, float> m_AuxSends;
};

} // namespace Audio
} // namespace Engine