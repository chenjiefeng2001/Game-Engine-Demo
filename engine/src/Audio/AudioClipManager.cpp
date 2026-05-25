#include "Engine/Core/Audio/AudioClipManager.h"
#include <iostream>

namespace Engine {

    // ============================================================
    // Load — 加载音频剪辑（缓存优先）
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Load(const std::string& path) {
        // 缓存命中直接返回
        auto it = m_Cache.find(path);
        if (it != m_Cache.end()) {
            return it->second;
        }

        // 创建新 AudioClip 并加载
        auto clip = std::make_shared<AudioClip>();
        if (!clip->LoadFromFile(path)) {
            std::cerr << "[AudioClipManager] Failed to load audio: " << path << std::endl;
            return nullptr;
        }

        // 加入缓存
        m_Cache[path] = clip;
        return clip;
    }

    // ============================================================
    // Get — 查询缓存
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Get(const std::string& path) const {
        auto it = m_Cache.find(path);
        return (it != m_Cache.end()) ? it->second : nullptr;
    }

    // ============================================================
    // Remove — 移除缓存
    // ============================================================

    void AudioClipManager::Remove(const std::string& path) {
        m_Cache.erase(path);
    }

    // ============================================================
    // Clear — 清空所有缓存
    // ============================================================

    void AudioClipManager::Clear() {
        m_Cache.clear();
    }

    // ============================================================
    // Has — 判断是否已缓存
    // ============================================================

    bool AudioClipManager::Has(const std::string& path) const {
        return m_Cache.find(path) != m_Cache.end();
    }

    // ============================================================
    // Reload — 强制重新加载
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Reload(const std::string& path) {
        // 先移除旧缓存
        m_Cache.erase(path);

        // 重新加载
        auto clip = std::make_shared<AudioClip>();
        if (!clip->LoadFromFile(path)) {
            std::cerr << "[AudioClipManager] Failed to reload audio: " << path << std::endl;
            return nullptr;
        }

        m_Cache[path] = clip;
        return clip;
    }

} // namespace Engine
