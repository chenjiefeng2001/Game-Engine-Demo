#pragma once

/**
 * @file AudioClipManager.h
 * @brief 音频剪辑资源管理器 — 委托给统一 ResourceManager
 *
 * AudioClipManager 现在是 ResourceManager 的音频专用外观（Facade），
 * 所有缓存和生命周期管理由统一的 ResourceManager 处理。
 * 保留此类的目的是为已有代码提供向后兼容的 API。
 *
 * 推荐使用方式（新代码）：
 * @code
 *   auto clip = ResourceManager::Get()->Load<AudioClip>("sounds/explosion.wav");
 * @endcode
 *
 * 兼容使用方式（已有代码）：
 * @code
 *   AudioClipManager clipMgr;
 *   auto clip = clipMgr.Load("sounds/explosion.wav");
 * @endcode
 */

#include "Engine/Core/Audio/AudioClip.h"
#include <memory>
#include <string>

namespace Engine {

    class AudioClipManager {
    public:
        AudioClipManager() = default;

        // 禁止拷贝
        AudioClipManager(const AudioClipManager&) = delete;
        AudioClipManager& operator=(const AudioClipManager&) = delete;

        /**
         * @brief 加载音频剪辑（委托给 ResourceManager）
         * @param path 音频文件路径，支持 .wav / .ogg
         */
        std::shared_ptr<AudioClip> Load(const std::string& path);

        /** @brief 从 ResourceManager 查询已缓存的音频剪辑 */
        std::shared_ptr<AudioClip> Get(const std::string& path) const;

        /** @brief 从 ResourceManager 移除指定音频剪辑 */
        void Remove(const std::string& path);

        /** @brief 清空 ResourceManager 的所有缓存 */
        void Clear();

        /** @brief 获取 ResourceManager 中 AudioClip 类型的缓存数量 */
        size_t Count() const;

        /** @brief 判断 ResourceManager 中是否已缓存指定路径 */
        bool Has(const std::string& path) const;

        /** @brief 强制重新加载指定音频剪辑 */
        std::shared_ptr<AudioClip> Reload(const std::string& path);
    };

} // namespace Engine
