#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {
namespace Audio {

// ── 基本类型别名 ──
using AudioSourceHandle = uint32_t;
using AudioBusHandle    = uint32_t;

constexpr AudioSourceHandle InvalidAudioSource = 0;
constexpr AudioBusHandle    InvalidAudioBus    = 0;

// ── 声源状态 ──
enum class AudioSourceState : uint8_t
{
    Stopped,
    Playing,
    Paused,
};

// ── 距离衰减模型 ──
enum class DistanceModel : uint8_t
{
    None,
    Inverse,
    InverseClamped,
    Linear,
    LinearClamped,
    Exponent,
    ExponentClamped,
};

// ── 空间音频路径类型 ──
enum class SpatialPathType : uint8_t
{
    Direct,
    Reflection,
    Diffraction,
};

// ── 遮挡/阻断/排他数据 ──
struct OcclusionData
{
    float occlusionFactor   = 0.0f; // 遮挡量 [0, 1]
    float obstructionFactor = 0.0f; // 阻断量 [0, 1]
    float exclusionFactor   = 0.0f; // 排他量 [0, 1]
};

// ── LTI 空间音频脉冲响应 ──
struct SpatialIR
{
    std::vector<float> coefficients;  // 脉冲响应系数
    float delay = 0.0f;              // 传播延迟（秒）
    float gain = 1.0f;               // 增益
    SpatialPathType type = SpatialPathType::Direct;
    OcclusionData occlusion;
    float diffractionAmount = 0.0f;  // 衍射量 [0, 1]
    float diffractionAngle  = 0.0f;  // 衍射角度（弧度）
};

// ── 音频总线配置 ──
struct AudioBusConfig
{
    std::string name;
    float volume = 1.0f;
    float pitch  = 1.0f;
    std::vector<AudioBusConfig*> sends;
    std::vector<float> sendLevels;
    bool muted = false;
    std::vector<std::string> effects;
    bool isBypassed = false;
};

// ── DSP 效果器基类 ──
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;
    virtual void Process(float* input, float* output, size_t numFrames, uint32_t sampleRate) = 0;
    virtual void Reset() = 0;
    virtual const char* GetEffectName() const = 0;
};

// ── 效果器工厂函数 ──
std::unique_ptr<AudioEffect> CreateAudioEffect(const std::string& type,
                                                float param1 = 0.0f,
                                                uint32_t sampleRate = 44100);

} // namespace Audio
} // namespace Engine
