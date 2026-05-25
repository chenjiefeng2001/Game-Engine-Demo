#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/FileWatcher.h"

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"

#include <iostream>
#include <algorithm>

namespace Engine {

    ResourceManager* ResourceManager::s_Instance = nullptr;
    std::unique_ptr<ResourceManager> ResourceManager::s_InstanceOwner = nullptr;

    // ============================================================
    // 单例生命周期
    // ============================================================

    void ResourceManager::Init(IGraphicsFactory& factory)
    {
        if (s_Instance)
        {
            std::cerr << "[ResourceManager] Already initialized" << std::endl;
            return;
        }
        s_InstanceOwner = std::unique_ptr<ResourceManager>(new ResourceManager(factory));
        s_Instance = s_InstanceOwner.get();
        std::cout << "[ResourceManager] Initialized" << std::endl;
    }

    void ResourceManager::Shutdown()
    {
        if (!s_Instance) return;
        std::cout << "[ResourceManager] Shutting down, unloading all resources..." << std::endl;
        s_Instance->UnloadAll();
        s_Instance = nullptr;
        s_InstanceOwner.reset();
        std::cout << "[ResourceManager] Shutdown complete" << std::endl;
    }

    ResourceManager::ResourceManager(IGraphicsFactory& factory)
        : m_Factory(factory)
    {
    }

    // ============================================================
    // 模板特化：Texture 加载（委托给抽象工厂）
    // ============================================================

    template<>
    std::shared_ptr<Texture> ResourceManager::LoadByType<Texture>(const std::string& path)
    {
        auto tex = m_Factory.CreateTexture(path);
        if (!tex || tex->GetWidth() == 0)
        {
            std::cerr << "[ResourceManager] Failed to load texture: " << path << std::endl;
            return nullptr;
        }

        std::cout << "[ResourceManager] Texture loaded: " << path
                  << " (" << tex->GetWidth() << "x" << tex->GetHeight() << ")" << std::endl;
        return tex;
    }

    // ============================================================
    // 模板特化：Shader 加载（委托给抽象工厂）
    // ============================================================

    template<>
    std::shared_ptr<Shader> ResourceManager::LoadByType<Shader>(
        const std::string& vertexPath, const std::string& fragmentPath)
    {
        auto shader = m_Factory.CreateShader(vertexPath, fragmentPath);
        if (!shader)
        {
            std::cerr << "[ResourceManager] Failed to create shader: "
                      << vertexPath << " | " << fragmentPath << std::endl;
            return nullptr;
        }

        std::cout << "[ResourceManager] Shader loaded: "
                  << vertexPath << " + " << fragmentPath << std::endl;
        return shader;
    }

    // ============================================================
    // 模板特化：AudioClip 加载（单路径）
    // ============================================================

    template<>
    std::shared_ptr<AudioClip> ResourceManager::LoadByType<AudioClip>(const std::string& path)
    {
        auto clip = std::make_shared<AudioClip>(path);
        if (!clip->LoadFromFile(path))
        {
            std::cerr << "[ResourceManager] Failed to load audio: " << path << std::endl;
            return nullptr;
        }

        std::cout << "[ResourceManager] AudioClip loaded: " << path
                  << " (" << clip->GetDuration() << "s, "
                  << clip->GetSampleRate() << "Hz)" << std::endl;
        return clip;
    }

    // ============================================================
    // 通用查询
    // ============================================================

    std::shared_ptr<Resource> ResourceManager::GetResource(const std::string& path) const
    {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end())
            return it->second.lock();
        return nullptr;
    }

    bool ResourceManager::Has(const std::string& path) const
    {
        auto it = m_Cache.find(path);
        if (it == m_Cache.end()) return false;
        return !it->second.expired();
    }

    std::vector<std::string> ResourceManager::GetPathsByType(ResourceType type) const
    {
        std::vector<std::string> result;
        for (const auto& [path, weak] : m_Cache)
        {
            if (auto res = weak.lock())
            {
                if (res->GetType() == type)
                    result.push_back(path);
            }
        }
        return result;
    }

    // ============================================================
    // 缓存管理
    // ============================================================

    void ResourceManager::Unload(const std::string& path)
    {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end())
        {
            std::cout << "[ResourceManager] Unloading: " << path << std::endl;
            m_Cache.erase(it);
        }
    }

    void ResourceManager::UnloadAll()
    {
        size_t count = m_Cache.size();
        m_Cache.clear();
        std::cout << "[ResourceManager] All " << count << " resources unloaded" << std::endl;
    }

    void ResourceManager::UnloadUnused()
    {
        size_t before = m_Cache.size();
        for (auto it = m_Cache.begin(); it != m_Cache.end();)
        {
            if (it->second.expired())
                it = m_Cache.erase(it);
            else
                ++it;
        }
        size_t after = m_Cache.size();
        if (before != after)
            std::cout << "[ResourceManager] Cleaned " << (before - after)
                      << " unused resources (" << after << " remaining)" << std::endl;
    }

    void ResourceManager::LogStats() const
    {
        std::cout << "=== ResourceManager Stats ===" << std::endl;
        size_t total = 0;
        for (int t = 1; t < static_cast<int>(ResourceType::COUNT); ++t)
        {
            ResourceType rt = static_cast<ResourceType>(t);
            auto paths = GetPathsByType(rt);
            if (!paths.empty())
            {
                std::cout << "  " << ResourceTypeName(rt) << ": " << paths.size() << std::endl;
                total += paths.size();
            }
        }
        std::cout << "  Total cached: " << total << std::endl;
        std::cout << "=============================" << std::endl;
    }

    // ============================================================
    // 热加载
    // ============================================================

    void ResourceManager::PollHotReload()
    {
        auto* fw = FileWatcher::Get();
        if (!fw) return;

        fw->PollPendingChanges([this](const std::string& path) {
            // 文件已变更——尝试热加载
            if (Reload(path))
            {
                std::cout << "[HotReload] Reloaded: " << path << std::endl;
            }
        });
    }

    bool ResourceManager::Reload(const std::string& path)
    {
        // 查找缓存中的资源
        auto it = m_Cache.find(path);
        if (it == m_Cache.end())
            return false;

        auto resource = it->second.lock();
        if (!resource)
        {
            // 资源已过期但缓存条目还在——清理
            m_Cache.erase(it);
            return false;
        }

        std::cout << "[HotReload] Reloading: " << path
                  << " (" << resource->GetTypeName() << ")" << std::endl;

        resource->SetState(ResourceState::Loading);

        // 调用资源自身的 Reload 方法
        if (!resource->Reload())
        {
            std::cerr << "[HotReload] Failed to reload: " << path << std::endl;
            resource->SetState(ResourceState::Failed);
            return false;
        }

        // 递增版本号
        resource->BumpReloadVersion();

        // ── 通知所有监听此路径的回调 ──
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        for (const auto& entry : m_ReloadCallbacks)
        {
            if (entry.path == path || entry.path == "*")
            {
                entry.callback(path);
            }
        }

        return true;
    }

    // ============================================================
    // 热加载回调管理
    // ============================================================

    uint32 ResourceManager::BindReloadCallback(const std::string& path,
                                                ReloadCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        uint32 id = m_NextCallbackId++;
        m_ReloadCallbacks.push_back({ id, path, std::move(callback) });
        return id;
    }

    void ResourceManager::UnbindReloadCallback(uint32 id)
    {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        auto it = std::remove_if(m_ReloadCallbacks.begin(), m_ReloadCallbacks.end(),
            [id](const ReloadCallbackEntry& entry) { return entry.id == id; });
        if (it != m_ReloadCallbacks.end())
            m_ReloadCallbacks.erase(it, m_ReloadCallbacks.end());
    }

} // namespace Engine
