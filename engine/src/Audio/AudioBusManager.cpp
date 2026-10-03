#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <cstring>

namespace {
    Engine::Logger s_Log("AudioBus");
}

namespace Engine {
namespace Audio {

// ============================================================================
// 构造函数
// ============================================================================

AudioBusConfig* AudioBusManager::CreateBus(const std::string& name)
{
    // 检查是否已存在同名总线
    auto it = m_Buses.find(name);
    if (it != m_Buses.end())
    {
        s_Log.Warn("Bus '{}' already exists", name);
        return &it->second;
    }

    AudioBusConfig config;
    config.name = name;
    config.volume = 1.0f;
    config.pitch = 1.0f;
    config.muted = false;
    config.isBypassed = false;

    auto [iter, inserted] = m_Buses.emplace(name, std::move(config));
    if (inserted)
    {
        s_Log.Info("Created audio bus '{}'", name);
        return &iter->second;
    }
    return nullptr;
}

void AudioBusManager::RemoveBus(const std::string& name)
{
    // 先移除所有指向该总线的发送
    for (auto& [busName, bus] : m_Buses)
    {
        auto& sends = bus.sends;
        for (auto it = sends.begin(); it != sends.end();)
        {
            if ((*it)->name == name)
            {
                it = sends.erase(it);
                // 同步移除 sendLevels
                size_t idx = std::distance(sends.begin(), it);
                if (idx < bus.sendLevels.size())
                {
                    bus.sendLevels.erase(bus.sendLevels.begin() + static_cast<ptrdiff_t>(idx));
                }
            }
            else
            {
                ++it;
            }
        }
    }

    // 移除效果器
    m_Effects.erase(name);

    // 移除总线本身
    m_Buses.erase(name);
    s_Log.Info("Removed audio bus '{}'", name);
}

AudioBusConfig* AudioBusManager::GetBus(const std::string& name)
{
    auto it = m_Buses.find(name);
    return (it != m_Buses.end()) ? &it->second : nullptr;
}

void AudioBusManager::SetBusVolume(const std::string& name, float volume)
{
    auto it = m_Buses.find(name);
    if (it != m_Buses.end())
    {
        it->second.volume = std::clamp(volume, 0.0f, 1.0f);
    }
}

void AudioBusManager::AddSend(const std::string& sourceBus, const std::string& destBus, float level)
{
    auto srcIt = m_Buses.find(sourceBus);
    auto dstIt = m_Buses.find(destBus);
    if (srcIt == m_Buses.end() || dstIt == m_Buses.end())
    {
        s_Log.Error("AddSend: Bus '{}' or '{}' not found", sourceBus, destBus);
        return;
    }

    // 避免自身发送
    if (sourceBus == destBus)
    {
        s_Log.Error("AddSend: Cannot send bus to itself");
        return;
    }

    auto& sends = srcIt->second.sends;
    auto& levels = srcIt->second.sendLevels;

    // 检查是否已存在同名发送
    for (size_t i = 0; i < sends.size(); ++i)
    {
        if (sends[i]->name == destBus)
        {
            // 更新发送电平
            if (i < levels.size())
            {
                levels[i] = std::clamp(level, 0.0f, 1.0f);
            }
            return;
        }
    }

    sends.push_back(&dstIt->second);
    levels.push_back(std::clamp(level, 0.0f, 1.0f));
    s_Log.Info("Added send '{}' -> '{}' (level: {:.2f})", sourceBus, destBus, level);
}

void AudioBusManager::RemoveSend(const std::string& sourceBus, const std::string& destBus)
{
    auto it = m_Buses.find(sourceBus);
    if (it == m_Buses.end()) return;

    auto& sends = it->second.sends;
    auto& levels = it->second.sendLevels;

    for (size_t i = 0; i < sends.size(); ++i)
    {
        if (sends[i]->name == destBus)
        {
            sends.erase(sends.begin() + static_cast<ptrdiff_t>(i));
            if (i < levels.size())
            {
                levels.erase(levels.begin() + static_cast<ptrdiff_t>(i));
            }
            return;
        }
    }
}

void AudioBusManager::MuteBus(const std::string& name, bool mute)
{
    auto it = m_Buses.find(name);
    if (it != m_Buses.end())
    {
        it->second.muted = mute;
    }
}

void AudioBusManager::ApplyBusProcessing(float* buffer, size_t numFrames, uint32_t sampleRate)
{
    if (buffer == nullptr || numFrames == 0) return;

    // 步骤 1: 创建辅助发送缓冲区
    // 对每个有辅助发送的总线，计算发送量并混入目标总线
    for (auto& [name, bus] : m_Buses)
    {
        if (bus.muted || bus.isBypassed) continue;

        // 应用总线音量
        if (bus.volume < 0.999f)
        {
            for (size_t i = 0; i < numFrames * 2; ++i)
            {
                buffer[i] *= bus.volume;
            }
        }
    }

    // 步骤 2: 应用总线效果器
    for (auto& [busName, effects] : m_Effects)
    {
        auto busIt = m_Buses.find(busName);
        if (busIt == m_Buses.end()) continue;
        if (busIt->second.muted) continue;

        // 创建处理缓冲区
        std::vector<float> processed(numFrames * 2);
        std::memcpy(processed.data(), buffer, numFrames * 2 * sizeof(float));

        for (auto& effect : effects)
        {
            if (effect)
            {
                effect->Process(processed.data(), processed.data(), numFrames, sampleRate);
            }
        }

        // 混回主缓冲区
        for (size_t i = 0; i < numFrames * 2; ++i)
        {
            buffer[i] += processed[i] * 0.3f; // 效果器湿/干混合
        }
    }

    // 步骤 3: 处理辅助发送
    //
    // K4 修复：此处原本执行
    //     destBus->volume += bus.volume * sendLevel * 0.1f;
    // 即把 **routing state** 当作累加目标。后果：
    //   - 每调用一次处理，目标总线音量被永久抬高（实测 1.0 → 1.1 → 1.3）
    //   - 该路径绕过 SetBusVolume 的 [0,1] 钳制，音量可无界增长
    //   - 处理函数产生了非幂等的副作用
    //
    // 现在把发送贡献累加到**局部**增益，不再写回 destBus->volume。
    // 当前简化实现只有一条共享缓冲区、没有 per-bus 目标缓冲可供混音，
    // 因此该局部贡献不写入任何持久状态；保留遍历与累加以便后续
    // 实现真实的 per-bus 混音。
    float sendContribution = 0.0f;
    for (const auto& [name, bus] : m_Buses)
    {
        if (bus.sends.empty()) continue;

        for (size_t i = 0; i < bus.sends.size(); ++i)
        {
            const auto* destBus = bus.sends[i];
            if (!destBus || destBus->muted) continue;

            const float sendLevel = (i < bus.sendLevels.size()) ? bus.sendLevels[i] : 1.0f;

            // 注意：这是一个简化的实现，实际中需要每个总线独立处理
            if (sendLevel > 0.01f)
            {
                sendContribution += bus.volume * sendLevel * 0.1f;
            }
        }
    }

    // 无 per-bus 目标缓冲可混音，故局部发送增益不落地。
    (void)sendContribution;
}

// ============================================================================
// 效果器管理
// ============================================================================

void AudioBusManager::AddEffect(const std::string& busName, std::unique_ptr<AudioEffect> effect)
{
    if (!effect)
    {
        s_Log.Error("AddEffect: null effect for bus '{}'", busName);
        return;
    }

    auto busIt = m_Buses.find(busName);
    if (busIt == m_Buses.end())
    {
        s_Log.Error("AddEffect: Bus '{}' not found", busName);
        return;
    }

    m_Effects[busName].push_back(std::move(effect));
    s_Log.Info("Added effect to bus '{}'", busName);
}

void AudioBusManager::RemoveEffect(const std::string& busName, const std::string& effectName)
{
    auto it = m_Effects.find(busName);
    if (it == m_Effects.end()) return;

    auto& effects = it->second;
    std::erase_if(effects, [&effectName](const std::unique_ptr<AudioEffect>& effect)
    {
        return effect && effect->GetEffectName() == effectName;
    });

    if (effects.empty())
    {
        m_Effects.erase(it);
    }
}

void AudioBusManager::ClearEffects(const std::string& busName)
{
    m_Effects.erase(busName);
}

} // namespace Audio
} // namespace Engine