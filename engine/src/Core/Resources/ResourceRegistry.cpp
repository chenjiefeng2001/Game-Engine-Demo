#include "Engine/Core/Resources/ResourceRegistry.h"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace Engine {

    // ============================================================
    // Register
    // ============================================================

    ResourceGUID ResourceRegistry::Register(std::shared_ptr<Resource> resource) {
        if (!resource) return ResourceGUID::Null;

        std::lock_guard<std::mutex> lock(m_Mutex);

        ResourceGUID guid = ResourceGUID::Create();
        const std::string& path = resource->GetPath();

        // 检查路径是否已注册
        auto pathIt = m_PathIndex.find(path);
        if (pathIt != m_PathIndex.end()) {
            // 路径已存在——复用旧 GUID
            guid = pathIt->second;
            auto entryIt = m_Entries.find(guid);
            if (entryIt != m_Entries.end()) {
                entryIt->second.resource = resource.get();
                entryIt->second.state = resource->GetState();
                m_Holders[guid] = std::move(resource);
                FireLifecycleEvent(LifecycleEvent::AfterLoad, guid);
                return guid;
            }
        }

        // 创建新条目
        RegistryEntry entry;
        entry.guid     = guid;
        entry.path     = path;
        entry.type     = resource->GetType();
        entry.resource = resource.get();
        entry.state    = resource->GetState();
        entry.lastUsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        m_Entries[guid] = std::move(entry);
        m_PathIndex[path] = guid;
        m_Holders[guid] = std::move(resource);

        FireLifecycleEvent(LifecycleEvent::AfterLoad, guid);
        return guid;
    }

    bool ResourceRegistry::RegisterWithGUID(const ResourceGUID& guid,
                                             std::shared_ptr<Resource> resource) {
        if (!resource || guid.IsNull()) return false;

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_Entries.count(guid)) return false;  // GUID 已存在

        RegistryEntry entry;
        entry.guid     = guid;
        entry.path     = resource->GetPath();
        entry.type     = resource->GetType();
        entry.resource = resource.get();
        entry.state    = resource->GetState();

        m_Entries[guid] = std::move(entry);
        m_PathIndex[resource->GetPath()] = guid;
        m_Holders[guid] = std::move(resource);

        FireLifecycleEvent(LifecycleEvent::AfterLoad, guid);
        return true;
    }

    // ============================================================
    // Unregister
    // ============================================================

    void ResourceRegistry::Unregister(const ResourceGUID& guid,
                                       const std::string& reason) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Entries.find(guid);
        if (it == m_Entries.end()) return;

        // 检查引用计数
        if (it->second.refCount > 0) {
            std::cerr << "[Registry] Cannot unregister " << guid.ToString()
                      << ": still has " << it->second.refCount
                      << " reference(s)" << std::endl;
            return;
        }

        FireLifecycleEvent(LifecycleEvent::BeforeUnload, guid);

        m_PathIndex.erase(it->second.path);
        m_Holders.erase(guid);
        m_Entries.erase(it);

        FireLifecycleEvent(LifecycleEvent::AfterUnload, guid);

        if (!reason.empty()) {
            std::cout << "[Registry] Unregistered " << guid.ToString()
                      << " (" << reason << ")" << std::endl;
        }
    }

    void ResourceRegistry::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        for (auto& [guid, entry] : m_Entries) {
            FireLifecycleEvent(LifecycleEvent::BeforeUnload, guid);
        }

        m_Entries.clear();
        m_PathIndex.clear();
        m_Holders.clear();
    }

    // ============================================================
    // 查询
    // ============================================================

    ResourceGUID ResourceRegistry::FindByPath(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_PathIndex.find(path);
        if (it != m_PathIndex.end())
            return it->second;
        return ResourceGUID::Null;
    }

    const RegistryEntry* ResourceRegistry::FindEntry(const ResourceGUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end())
            return &it->second;
        return nullptr;
    }

    bool ResourceRegistry::Has(const ResourceGUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Entries.count(guid) > 0;
    }

    bool ResourceRegistry::HasPath(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PathIndex.count(path) > 0;
    }

    std::vector<ResourceGUID> ResourceRegistry::GetAllGuids() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<ResourceGUID> result;
        result.reserve(m_Entries.size());
        for (const auto& [guid, entry] : m_Entries)
            result.push_back(guid);
        return result;
    }

    std::vector<ResourceGUID> ResourceRegistry::GetGuidsByType(ResourceType type) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<ResourceGUID> result;
        for (const auto& [guid, entry] : m_Entries) {
            if (entry.type == type)
                result.push_back(guid);
        }
        return result;
    }

    // ============================================================
    // 状态管理
    // ============================================================

    void ResourceRegistry::SetState(const ResourceGUID& guid, ResourceState state) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end()) {
            it->second.state = state;
            if (it->second.resource)
                it->second.resource->SetState(state);
        }
    }

    ResourceState ResourceRegistry::GetState(const ResourceGUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end())
            return it->second.state;
        return ResourceState::Unloaded;
    }

    void ResourceRegistry::Touch(const ResourceGUID& guid) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end()) {
            it->second.lastUsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

    // ============================================================
    // 生命周期钩子
    // ============================================================

    uint32 ResourceRegistry::BindLifecycleCallback(LifecycleCallback callback) {
        uint32 id = m_NextCallbackId++;
        m_LifecycleCallbacks.push_back({id, std::move(callback)});
        return id;
    }

    void ResourceRegistry::UnbindLifecycleCallback(uint32 id) {
        auto it = std::remove_if(m_LifecycleCallbacks.begin(),
                                  m_LifecycleCallbacks.end(),
                                  [id](const LifecycleEntry& e) { return e.id == id; });
        m_LifecycleCallbacks.erase(it, m_LifecycleCallbacks.end());
    }

    void ResourceRegistry::FireLifecycleEvent(LifecycleEvent event,
                                               const ResourceGUID& guid) {
        for (const auto& entry : m_LifecycleCallbacks) {
            if (entry.callback)
                entry.callback(event, guid);
        }
    }

    // ============================================================
    // 交叉引用管理
    // ============================================================

    void ResourceRegistry::AddRef(const ResourceGUID& guid,
                                   const ResourceGUID& referer) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end()) {
            it->second.refCount++;
            if (!referer.IsNull())
                it->second.referencedBy.insert(referer);
        }
    }

    void ResourceRegistry::ReleaseRef(const ResourceGUID& guid,
                                       const ResourceGUID& referer) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end()) {
            if (it->second.refCount > 0)
                it->second.refCount--;
            if (!referer.IsNull())
                it->second.referencedBy.erase(referer);
        }
    }

    uint32 ResourceRegistry::GetRefCount(const ResourceGUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Entries.find(guid);
        if (it != m_Entries.end())
            return it->second.refCount;
        return 0;
    }

    // ============================================================
    // 内部辅助
    // ============================================================

    std::shared_ptr<Resource> ResourceRegistry::GetHolderLock(const ResourceGUID& guid) const {
        auto it = m_Holders.find(guid);
        if (it != m_Holders.end())
            return it->second;
        return nullptr;
    }

    // ============================================================
    // 统计
    // ============================================================

    size_t ResourceRegistry::GetRegisteredCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Entries.size();
    }

    size_t ResourceRegistry::GetLoadedCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        size_t count = 0;
        for (const auto& [guid, entry] : m_Entries) {
            if (entry.state == ResourceState::Ready)
                count++;
        }
        return count;
    }

    void ResourceRegistry::LogStats() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::cout << "=== ResourceRegistry Stats ===" << std::endl;
        std::cout << "Registered: " << m_Entries.size() << std::endl;

        size_t loaded = 0, ready = 0, failed = 0, loading = 0, unloaded = 0;
        for (const auto& [guid, entry] : m_Entries) {
            switch (entry.state) {
                case ResourceState::Loaded:    loaded++;   break;
                case ResourceState::Ready:     ready++;    break;
                case ResourceState::Failed:    failed++;   break;
                case ResourceState::Loading:   loading++;  break;
                case ResourceState::Resolving: loading++;  break;
                default:                       unloaded++; break;
            }
        }
        std::cout << "  Loaded:  " << loaded << std::endl;
        std::cout << "  Ready:   " << ready << std::endl;
        std::cout << "  Failed:  " << failed << std::endl;
        std::cout << "  Loading: " << loading << std::endl;
        std::cout << "  Unloaded:" << unloaded << std::endl;
        std::cout << "Holders:   " << m_Holders.size() << std::endl;

        // 池统计
        auto ps = m_Allocator.GetGlobalStats();
        std::cout << "Pool memory: " << (ps.totalBytes / 1024) << "KB"
                  << " (" << ps.poolCount << " pools)" << std::endl;
        std::cout << "=============================" << std::endl;
    }

} // namespace Engine
