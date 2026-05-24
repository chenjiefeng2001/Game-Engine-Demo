#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/IGraphicsFactory.h"

#include <unordered_map>
#include <memory>
#include <string>
#include "Engine/Types.h"

namespace Engine {

    /**
     * @brief 纹理资源管理器 — 缓存已加载纹理，避免重复加载
     *
     * 使用方式（依赖注入，非单例）：
     *   TextureManager texMgr(factory);
     *   auto tex = texMgr.Load("assets/textures/player.png");
     */
    class TextureManager {
    public:
        explicit TextureManager(IGraphicsFactory& factory);

        // 禁止拷贝
        TextureManager(const TextureManager&) = delete;
        TextureManager& operator=(const TextureManager&) = delete;

        /**
         * @brief 加载纹理（缓存优先）
         * @param path 纹理文件路径（相对于可执行文件的工作目录）
         * @return 若已缓存则直接返回，否则通过 factory 加载并缓存
         */
        std::shared_ptr<Texture> Load(const std::string& path);

        /**
         * @brief 按路径查询纹理，不存在则返回 nullptr
         */
        std::shared_ptr<Texture> Get(const std::string& path) const;

        /**
         * @brief 从缓存中移除指定纹理
         */
        void Remove(const std::string& path);

        /**
         * @brief 清空所有缓存纹理
         */
        void Clear();

        /**
         * @brief 获取当前缓存的纹理数量
         */
SizeT Count() const { return m_Cache.size(); }

        /**
         * @brief 判断指定路径是否已缓存
         */
        bool Has(const std::string& path) const;

        /**
         * @brief 重新加载指定纹理（强制重新从磁盘加载）
         */
        std::shared_ptr<Texture> Reload(const std::string& path);

    private:
        IGraphicsFactory& m_Factory;
        std::unordered_map<std::string, std::shared_ptr<Texture>> m_Cache;
    };

} // namespace Engine