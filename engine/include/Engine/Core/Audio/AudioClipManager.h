#pragma once

/**
 * @file AudioClipManager.h
 * @brief 音频剪辑资源管理器 — 缓存已加载的 AudioClip，避免重复加载
 *
 * 设计原则（与 TextureManager 一致）：
 *   - 非单例，通过依赖注入使用
 *   - 内部维护路径 → AudioClip 的缓存映射
 *   - 线程安全：调用方需保证外部同步（与 TextureManager 一致）
 *
 * 使用方式：
 * @code
 *   AudioClipManager clipMgr;
 *   auto clip = clipMgr.Load("sounds/explosion.wav");
 *
 *   AudioSourceComponent audio(engine->CreateSource());
 *   audio.Play(*clip);
 * @endcode
 */

#include "Engine/Core/Audio/AudioClip.h"
#include <unordered_map>
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
         * @brief 加载音频剪辑（缓存优先）
         * @param path 音频文件路径，支持 .wav / .ogg
         * @return 若已缓存则直接返回，否则加载并缓存
         *
         * 内部调用 AudioLoader::Load() 解码文件，
         * 然后将 PCM 数据上传到 OpenAL 缓冲区。
         */
        std::shared_ptr<AudioClip> Load(const std::string& path);

        /**
         * @brief 按路径查询已缓存的音频剪辑
         * @param path 文件路径
         * @return 若已缓存返回共享指针，否则返回 nullptr
         */
        std::shared_ptr<AudioClip> Get(const std::string& path) const;

        /**
         * @brief 从缓存中移除指定音频剪辑
         * @param path 文件路径
         *
         * 当最后一个 shared_ptr 销毁时，AudioClip 自动释放 OpenAL 缓冲区。
         */
        void Remove(const std::string& path);

        /**
         * @brief 清空所有缓存音频剪辑
         *
         * 所有已加载的剪辑将被释放（前提是外部没有额外的 shared_ptr 持有）。
         */
        void Clear();

        /**
         * @brief 获取当前缓存的音频剪辑数量
         */
        size_t Count() const { return m_Cache.size(); }

        /**
         * @brief 判断指定路径是否已缓存
         */
        bool Has(const std::string& path) const;

        /**
         * @brief 重新加载指定音频剪辑（强制重新从磁盘加载）
         * @param path 文件路径
         * @return 重新加载后的 AudioClip
         */
        std::shared_ptr<AudioClip> Reload(const std::string& path);

    private:
        std::unordered_map<std::string, std::shared_ptr<AudioClip>> m_Cache;
    };

} // namespace Engine
