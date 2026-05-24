#include "Engine/Core/RenderResources/TextureManager.h"

#include <cassert>

namespace Engine {

    TextureManager::TextureManager(IGraphicsFactory& factory)
        : m_Factory(factory) {
    }

    std::shared_ptr<Texture> TextureManager::Load(const std::string& path) {
        // 1. 缓存命中
        auto it = m_Cache.find(path);
        if (it != m_Cache.end()) {
            return it->second;
        }

        // 2. 通过工厂加载
        auto texture = m_Factory.CreateTexture(path);
        if (texture) {
            m_Cache[path] = texture;
        }

        return texture;
    }

    std::shared_ptr<Texture> TextureManager::Get(const std::string& path) const {
        auto it = m_Cache.find(path);
        return (it != m_Cache.end()) ? it->second : nullptr;
    }

    void TextureManager::Remove(const std::string& path) {
        m_Cache.erase(path);
    }

    void TextureManager::Clear() {
        m_Cache.clear();
    }

    bool TextureManager::Has(const std::string& path) const {
        return m_Cache.find(path) != m_Cache.end();
    }

    std::shared_ptr<Texture> TextureManager::Reload(const std::string& path) {
        // 从缓存中移除旧纹理
        m_Cache.erase(path);

        // 强制重新加载
        auto texture = m_Factory.CreateTexture(path);
        if (texture) {
            m_Cache[path] = texture;
        }
        return texture;
    }

} // namespace Engine
