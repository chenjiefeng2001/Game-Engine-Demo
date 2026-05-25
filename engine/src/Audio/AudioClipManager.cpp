#include "Engine/Core/Audio/AudioClipManager.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include <iostream>

namespace Engine {

    // ============================================================
    // Load — 委托给统一 ResourceManager（缓存优先）
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Load(const std::string& path) {
        if (auto* rm = ResourceManager::Get()) {
            return rm->LoadAudio(path);
        }

        // 回退：ResourceManager 未初始化时的直接加载
        auto clip = std::make_shared<AudioClip>(path);
        if (!clip->LoadFromFile(path)) {
            std::cerr << "[AudioClipManager] Failed to load audio: " << path << std::endl;
            return nullptr;
        }
        return clip;
    }

    // ============================================================
    // Get — 从统一 ResourceManager 查询
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Get(const std::string& path) const {
        if (auto* rm = ResourceManager::Get()) {
            return rm->Get<AudioClip>(path);
        }
        return nullptr;
    }

    // ============================================================
    // Remove — 从统一 ResourceManager 移除
    // ============================================================

    void AudioClipManager::Remove(const std::string& path) {
        if (auto* rm = ResourceManager::Get()) {
            rm->Unload(path);
        }
    }

    // ============================================================
    // Clear — 清空所有缓存
    // ============================================================

    void AudioClipManager::Clear() {
        if (auto* rm = ResourceManager::Get()) {
            rm->UnloadAll();
        }
    }

    // ============================================================
    // Count — 从 ResourceManager 查询 AudioClip 缓存数量
    // ============================================================

    size_t AudioClipManager::Count() const {
        if (auto* rm = ResourceManager::Get()) {
            return rm->GetPathsByType(ResourceType::AudioClip).size();
        }
        return 0;
    }

    // ============================================================
    // Has — 判断是否已缓存
    // ============================================================

    bool AudioClipManager::Has(const std::string& path) const {
        if (auto* rm = ResourceManager::Get()) {
            return rm->Has(path);
        }
        return false;
    }

    // ============================================================
    // Reload — 强制重新加载
    // ============================================================

    std::shared_ptr<AudioClip> AudioClipManager::Reload(const std::string& path) {
        // 先移除旧缓存
        if (auto* rm = ResourceManager::Get()) {
            rm->Unload(path);
            return rm->LoadAudio(path);
        }

        // 回退
        auto clip = std::make_shared<AudioClip>(path);
        if (!clip->LoadFromFile(path)) {
            std::cerr << "[AudioClipManager] Failed to reload audio: " << path << std::endl;
            return nullptr;
        }
        return clip;
    }

} // namespace Engine
