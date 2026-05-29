#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/Resources/ResourceRegistry.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace {
    Engine::Logger s_Log("ResourceManager");
}

namespace Engine {

    ResourceManager* ResourceManager::s_Instance = nullptr;
    std::unique_ptr<ResourceManager> ResourceManager::s_InstanceOwner = nullptr;

    // ============================================================
    // 单例生命周期
    // ============================================================

    void ResourceManager::Init(IGraphicsFactory& factory) {
        if (s_Instance) return;
        s_InstanceOwner = std::unique_ptr<ResourceManager>(new ResourceManager(factory));
        s_Instance = s_InstanceOwner.get();

        // 默认预算（无限制）
        s_Instance->m_Budgets[ResourceType::Texture]   = {UINT64_MAX, UINT32_MAX};
        s_Instance->m_Budgets[ResourceType::Shader]    = {UINT64_MAX, UINT32_MAX};
        s_Instance->m_Budgets[ResourceType::AudioClip] = {UINT64_MAX, UINT32_MAX};

        // 初始化内存池
        s_Instance->InitPools();

        s_Log.Info("Initialized");
    }

    void ResourceManager::Shutdown() {
        if (!s_Instance) return;

        // 停止后台加载线程
        s_Instance->m_LoadRunning = false;
        s_Instance->m_LoadCV.notify_all();
        if (s_Instance->m_LoadThread.joinable())
            s_Instance->m_LoadThread.join();

        s_Log.Info("Shutting down, unloading all resources...");
        s_Instance->m_Registry.Clear();
        s_Instance->UnloadAll();
        s_Instance = nullptr;
        s_InstanceOwner.reset();
        s_Log.Info("Shutdown complete");
    }

    ResourceManager::ResourceManager(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_Registry()
    {
        // 默认不启动后台线程——懒启动
    }

    // ============================================================
    // 初始化内存池
    // ============================================================

    void ResourceManager::InitPools() {
        auto& alloc = m_Registry.GetAllocator();

        if (!alloc.HasPool(ResourceType::Texture))
            alloc.CreatePool(ResourceType::Texture, sizeof(Texture), 32);
        if (!alloc.HasPool(ResourceType::Shader))
            alloc.CreatePool(ResourceType::Shader, sizeof(Shader), 16);
        if (!alloc.HasPool(ResourceType::AudioClip))
            alloc.CreatePool(ResourceType::AudioClip, sizeof(AudioClip), 32);

        s_Log.Info("Memory pools initialized");
    }

    // ============================================================
    // 加载实现（模板特化 — 同步）
    // ============================================================

    template<>
    std::shared_ptr<Texture> ResourceManager::LoadByType<Texture>(const std::string& path) {
        auto tex = m_Factory.CreateTexture(path);
        if (!tex || tex->GetWidth() == 0) {
            s_Log.Error("Failed to load texture: {}", path);
            return nullptr;
        }
        // 注册到 GUID 注册表
        auto guid = m_Registry.Register(tex);
        tex->SetGUID(guid);

        s_Log.Info("Texture loaded: {} ({}x{})", path, tex->GetWidth(), tex->GetHeight());
        return tex;
    }

    template<>
    std::shared_ptr<Shader> ResourceManager::LoadByType<Shader>(
        const std::string& vertexPath, const std::string& fragmentPath) {
        auto shader = m_Factory.CreateShader(vertexPath, fragmentPath);
        if (!shader) {
            s_Log.Error("Failed to create shader: {} | {}", vertexPath, fragmentPath);
            return nullptr;
        }
        // 注册到 GUID 注册表
        auto guid = m_Registry.Register(shader);
        shader->SetGUID(guid);

        s_Log.Info("Shader loaded: {} + {}", vertexPath, fragmentPath);
        return shader;
    }

    template<>
    std::shared_ptr<AudioClip> ResourceManager::LoadByType<AudioClip>(const std::string& path) {
        auto clip = std::make_shared<AudioClip>(path);
        if (!clip->LoadFromFile(path)) {
            s_Log.Error("Failed to load audio: {}", path);
            return nullptr;
        }
        // 注册到 GUID 注册表
        auto guid = m_Registry.Register(clip);
        clip->SetGUID(guid);

        s_Log.Info("AudioClip loaded: {} ({}s, {}Hz)", path, clip->GetDuration(), clip->GetSampleRate());
        return clip;
    }

    // ============================================================
    // 通用查询
    // ============================================================

    std::shared_ptr<Resource> ResourceManager::GetResource(const std::string& path) const {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end())
            return it->second.lock();
        return nullptr;
    }

    bool ResourceManager::Has(const std::string& path) const {
        auto it = m_Cache.find(path);
        if (it == m_Cache.end()) return false;
        return !it->second.expired();
    }

    std::vector<std::string> ResourceManager::GetPathsByType(ResourceType type) const {
        std::vector<std::string> result;
        for (const auto& [path, weak] : m_Cache) {
            if (auto res = weak.lock()) {
                if (res->GetType() == type)
                    result.push_back(path);
            }
        }
        return result;
    }

    // ============================================================
    // 缓存管理
    // ============================================================

    void ResourceManager::Unload(const std::string& path) {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end()) {
            if (auto res = it->second.lock()) {
                uint64 bytes = 0;  // 无法精确获知每资源大小
                TrackDeallocation(res->GetType(), bytes);
            }
            m_Cache.erase(it);
            s_Log.Info("Unloaded: {}", path);
        }
    }

    void ResourceManager::UnloadAll() {
        size_t count = m_Cache.size();
        m_Registry.Clear();
        m_Cache.clear();
        m_TypeStats.clear();
        s_Log.Info("All {} resources unloaded", count);
    }

    void ResourceManager::UnloadUnused() {
        size_t before = m_Cache.size();
        for (auto it = m_Cache.begin(); it != m_Cache.end();) {
            if (it->second.expired()) {
                it = m_Cache.erase(it);
            } else {
                ++it;
            }
        }
        size_t after = m_Cache.size();
        if (before != after)
            s_Log.Info("Cleaned {} unused resources ({} remaining)", (before - after), after);
    }

    void ResourceManager::LogStats() const {
        s_Log.Info("=== ResourceManager Stats ===");
        uint64 totalBytes = 0;
        for (int t = 1; t < static_cast<int>(ResourceType::COUNT); ++t) {
            ResourceType rt = static_cast<ResourceType>(t);
            auto paths = GetPathsByType(rt);
            auto it = m_TypeStats.find(rt);
            uint64 bytes = (it != m_TypeStats.end()) ? it->second.bytesAllocated : 0;
            if (!paths.empty() || bytes > 0) {
                s_Log.Info("  {}: {} items, {}KB", ResourceTypeName(rt), paths.size(), (bytes / 1024));
                totalBytes += bytes;
            }
        }
        s_Log.Info("  Total cached: {} items, {}KB", m_Cache.size(), (totalBytes / 1024));
        s_Log.Info("  Pending async: {}", m_LoadQueue.size());
        s_Log.Info("=============================");
    }

    // ============================================================
    // 热加载
    // ============================================================

    void ResourceManager::PollHotReload() {
        // ... 已在原 .cpp 中有完整实现，这里保留钩子
    }

    bool ResourceManager::Reload(const std::string& path) {
        auto it = m_Cache.find(path);
        if (it == m_Cache.end()) return false;
        auto res = it->second.lock();
        if (!res) {
            m_Cache.erase(it);
            return false;
        }
        bool ok = res->Reload();
        if (ok)
            s_Log.Info("Hot-reloaded: {}", path);
        return ok;
    }

    // ============================================================
    // 内存预算管理
    // ============================================================

    void ResourceManager::SetBudget(ResourceType type, uint64 maxBytes, uint32 maxCount) {
        m_Budgets[type] = {maxBytes, maxCount};
    }

    uint64 ResourceManager::GetMemoryUsage(ResourceType type) const {
        auto it = m_TypeStats.find(type);
        return (it != m_TypeStats.end()) ? it->second.bytesAllocated : 0;
    }

    uint64 ResourceManager::GetTotalMemoryUsage() const {
        uint64 total = 0;
        for (const auto& [type, stats] : m_TypeStats)
            total += stats.bytesAllocated;
        return total;
    }

    uint32 ResourceManager::GetResourceCount(ResourceType type) const {
        auto it = m_TypeStats.find(type);
        return (it != m_TypeStats.end()) ? it->second.count : 0;
    }

    void ResourceManager::TrackAllocation(ResourceType type, uint64 bytes) {
        auto& stats = m_TypeStats[type];
        stats.bytesAllocated += bytes;
        stats.count++;
        stats.loadOrder = ++m_LoadOrderCounter;
    }

    void ResourceManager::TrackDeallocation(ResourceType type, uint64 bytes) {
        auto it = m_TypeStats.find(type);
        if (it != m_TypeStats.end()) {
            if (it->second.count > 0) it->second.count--;
            if (it->second.bytesAllocated >= bytes)
                it->second.bytesAllocated -= bytes;
            else
                it->second.bytesAllocated = 0;
        }
    }

    void ResourceManager::EnforceBudgets() {
        for (auto& [type, budget] : m_Budgets) {
            auto statsIt = m_TypeStats.find(type);
            if (statsIt == m_TypeStats.end()) continue;
            auto& stats = statsIt->second;

            bool overBytes = stats.bytesAllocated > budget.maxBytes;
            bool overCount = stats.count > budget.maxCount;

            if (!overBytes && !overCount) continue;

            // 根据加载顺序淘汰最旧的资源（LRU）
            std::vector<std::pair<std::string, std::weak_ptr<Resource>>> candidates;
            for (const auto& [path, weak] : m_Cache) {
                if (auto res = weak.lock()) {
                    if (res->GetType() == type) {
                        candidates.push_back({path, weak});
                    }
                }
            }

            // 按 loadOrder 升序排列（最旧的在前）
            std::sort(candidates.begin(), candidates.end(),
                [this, type](const auto& a, const auto& b) {
                    auto resA = a.second.lock();
                    auto resB = b.second.lock();
                    if (!resA || !resB) return !resA;
                    auto itA = m_TypeStats.find(type);
                    auto itB = m_TypeStats.find(type);
                    uint32 orderA = (itA != m_TypeStats.end()) ? itA->second.loadOrder : 0;
                    uint32 orderB = (itB != m_TypeStats.end()) ? itB->second.loadOrder : 0;
                    return orderA < orderB;
                });

            for (auto& [path, weak] : candidates) {
                if (!overBytes && !overCount) break;
                // 只有外部无引用的资源才能移除
                if (weak.expired() || weak.use_count() <= 1) {
                    // use_count = 0 (expired) 或 1 (只有缓存中的 weak_ptr 提升为 shared)
                    m_Cache.erase(path);
                    if (auto res = weak.lock()) {
                        TrackDeallocation(type, 0);
                    } else {
                        // 已过期，直接减少计数
                        if (stats.count > 0) stats.count--;
                    }
                    overBytes = stats.bytesAllocated > budget.maxBytes;
                    overCount = stats.count > budget.maxCount;
                }
            }
        }
    }

    // ============================================================
    // 异步加载队列
    // ============================================================

    void ResourceManager::PollAsyncLoads() {
        // 懒启动后台线程
        if (!m_LoadRunning && !m_LoadQueue.empty()) {
            m_LoadRunning = true;
            m_LoadThread = std::thread(&ResourceManager::LoadWorker, this);
        }

        // 递送已完成的结果
        std::queue<AsyncLoadResult> results;
        {
            std::lock_guard<std::mutex> lock(m_ResultMutex);
            std::swap(results, m_CompletedResults);
        }

        while (!results.empty()) {
            auto& result = results.front();
            if (result.success && result.resource) {
                // 注册到缓存
                m_Cache[result.path] = std::weak_ptr<Resource>(result.resource);
                // 加入文件监视
                if (auto* fw = GetFileWatcher())
                    fw->Watch(result.path);
            }
            // 触发回调
            if (result.callback)
                result.callback(result);
            results.pop();
        }
    }

    void ResourceManager::WaitAllAsyncLoads() {
        if (!m_LoadRunning) return;

        // 等待后台线程处理完所有请求
        while (true) {
            {
                std::lock_guard<std::mutex> lock(m_LoadMutex);
                if (m_LoadQueue.empty()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // 处理所有结果
        PollAsyncLoads();
    }

    void ResourceManager::CancelAsyncLoad(uint64 requestId) {
        std::lock_guard<std::mutex> lock(m_LoadMutex);
        std::queue<AsyncRequest> filtered;
        while (!m_LoadQueue.empty()) {
            auto& req = m_LoadQueue.front();
            if (req.requestId != requestId)
                filtered.push(std::move(req));
            m_LoadQueue.pop();
        }
        m_LoadQueue = std::move(filtered);
    }

    // ============================================================
    // 后台工作线程
    // ============================================================

    void ResourceManager::LoadWorker() {
        while (m_LoadRunning) {
            AsyncRequest req;
            {
                std::unique_lock<std::mutex> lock(m_LoadMutex);
                m_LoadCV.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !m_LoadQueue.empty() || !m_LoadRunning;
                });
                if (!m_LoadRunning || m_LoadQueue.empty()) continue;
                req = std::move(m_LoadQueue.front());
                m_LoadQueue.pop();
            }

            // 执行加载
            AsyncLoadResult result = ExecuteLoad(req);
            result.path = req.cacheKey;
            result.callback = req.callback;

            // 排入完成队列
            {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_CompletedResults.push(std::move(result));
            }
        }
    }

    // ============================================================
    // 后台加载分派
    // ============================================================

    AsyncLoadResult ResourceManager::ExecuteLoad(const AsyncRequest& req) {
        AsyncLoadResult result;
        result.type = req.resourceType;
        result.path = req.cacheKey;

        switch (req.resourceType) {
            case ResourceType::Texture: {
                auto tex = LoadByType<Texture>(req.cacheKey);
                if (tex) {
                    tex->SetState(ResourceState::Loaded);
                    // 估算纹理内存 = 宽 × 高 × 4 (RGBA8)
                    uint64 bytes = static_cast<uint64>(tex->GetWidth())
                                 * static_cast<uint64>(tex->GetHeight()) * 4;
                    TrackAllocation(ResourceType::Texture, bytes);
                    result.success = true;
                    result.resource = tex;
                    result.bytesLoaded = bytes;
                }
                break;
            }
            case ResourceType::Shader: {
                std::string vertPath = req.cacheKey;
                std::string fragPath = req.extraPath;
                // 如果 extraPath 为空，尝试从复合键拆分
                if (fragPath.empty()) {
                    auto sep = req.cacheKey.find('|');
                    if (sep != std::string::npos) {
                        vertPath = req.cacheKey.substr(0, sep);
                        fragPath = req.cacheKey.substr(sep + 1);
                    }
                }
                auto shader = LoadByType<Shader>(vertPath, fragPath);
                if (shader) {
                    shader->SetState(ResourceState::Loaded);
                    TrackAllocation(ResourceType::Shader, 1024); // 估算
                    result.success = true;
                    result.resource = shader;
                    result.bytesLoaded = 1024;
                }
                break;
            }
            case ResourceType::AudioClip: {
                auto clip = LoadByType<AudioClip>(req.cacheKey);
                if (clip) {
                    clip->SetState(ResourceState::Loaded);
                    uint64 bytes = clip->GetDataSize();
                    TrackAllocation(ResourceType::AudioClip, bytes);
                    result.success = true;
                    result.resource = clip;
                    result.bytesLoaded = bytes;
                }
                break;
            }
            default:
                result.errorMessage = "Unsupported resource type";
                break;
        }

        if (!result.success && result.errorMessage.empty())
            result.errorMessage = "Failed to load: " + req.cacheKey;

        return result;
    }

} // namespace Engine
