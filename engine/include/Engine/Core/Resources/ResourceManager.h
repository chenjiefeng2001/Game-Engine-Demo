#pragma once

/**
 * @file ResourceManager.h
 * @brief 统一资源管理器 — 缓存所有资源类型，统一生命周期
 *
 * 设计原则：
 *   - 单例模式（全局唯一实例）
 *   - 路径即标识：任何资源以文件路径或唯一 ID 字符串索引
 *   - 引用计数自动管理：资源存活期由 shared_ptr/weak_ptr 控制
 *   - 资源卸载：当所有外部引用释放后，资源自动从缓存移除
 *   - 类型安全：模板方法 Load<T>() 自动匹配资源类型
 *
 * 使用方式：
 * @code
 *   // 初始化（传入工厂）
 *   ResourceManager::Init(graphicsFactory);
 *
 *   // 模板加载（推荐）
 *   auto tex = ResourceManager::Get()->Load<Texture>("assets/textures/test.png");
 *   auto shader = ResourceManager::Get()->Load<Shader>(
 *       "assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl");
 *   auto audio = ResourceManager::Get()->Load<AudioClip>("assets/audio/explosion.wav");
 *
 *   // 便捷方法（与模板等价）
 *   auto tex2 = ResourceManager::Get()->LoadTexture("assets/textures/test.png");
 *
 *   // 弱引用观察
 *   std::weak_ptr<Texture> weakTex = tex;
 *   tex.reset();  // 若其他引用也已释放，缓存自动清理
 *   if (weakTex.expired()) { }  // 已卸载
 *
 *   // 统计
 *   ResourceManager::Get()->LogStats();
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/Resources/ResourceRegistry.h"
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/IGraphicsFactory.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <queue>

namespace Engine {

    // 前向声明
    class IGraphicsFactory;
    struct AsyncLoadResult;
    using AsyncLoadCallback = std::function<void(const AsyncLoadResult& result)>;

    // 异步加载结果
    struct AsyncLoadResult {
        bool      success = false;
        std::string path;
        ResourceType type = ResourceType::Unknown;
        std::shared_ptr<Resource> resource;
        uint64    bytesLoaded = 0;
        std::string errorMessage;
        AsyncLoadCallback callback;
    };

    // 内存预算配置（每资源类型）
    struct MemoryBudget {
        uint64 maxBytes = UINT64_MAX;   // 最大字节数（默认无限制）
        uint32 maxCount  = UINT32_MAX;  // 最大实例数（默认无限制）
    };

    class ResourceManager {
    public:
        // ── 单例生命周期 ──
        static void Init(IGraphicsFactory& factory);
        static void Shutdown();
        static ResourceManager* Get() { return s_Instance; }

        // ... existing code ...

        // ============================================================
        // 模板加载接口（推荐使用）
        // ============================================================

        /**
         * @brief 通用资源加载模板（单路径版本）
         * @tparam T 资源类型，必须继承自 Resource
         * @param path 资源文件路径
         * @return 缓存的或新加载的资源句柄
         *
         * 适用类型：Texture, AudioClip
         */
        template<typename T>
        std::shared_ptr<T> Load(const std::string& path) {
            static_assert(std::is_base_of_v<Resource, T>,
                          "T must derive from Resource");

            // 检查缓存
            auto it = m_Cache.find(path);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    return existing;
                }
            }

            // 委托给具体加载函数
            auto resource = LoadByType<T>(path);
            if (resource) {
                // 调用初始化钩子（GPU 上传、音频预处理等）
                if (!resource->PostLoad(&m_Factory)) {
                    std::cerr << "[ResourceManager] PostLoad failed: " << path << std::endl;
                    resource->SetState(ResourceState::Failed);
                    return nullptr;
                }
                resource->SetState(ResourceState::Loaded);
                m_Cache[path] = std::weak_ptr<Resource>(resource);
                // 自动加入文件监视（用于热加载）
                if (auto* fw = GetFileWatcher())
                    fw->Watch(path);
            }
            return resource;
        }

        /**
         * @brief 通用资源加载模板（双路径版本，用于着色器等需要多个文件的资源）
         * @tparam T 资源类型
         * @param pathA 第一路径（如 vertex shader）
         * @param pathB 第二路径（如 fragment shader）
         * @return 缓存的或新加载的资源句柄
         */
        template<typename T>
        std::shared_ptr<T> Load(const std::string& pathA,
                                const std::string& pathB) {
            static_assert(std::is_base_of_v<Resource, T>,
                          "T must derive from Resource");

            std::string id = pathA + "|" + pathB;

            auto it = m_Cache.find(id);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    return existing;
                }
            }

            auto resource = LoadByType<T>(pathA, pathB);
            if (resource) {
                // 调用初始化钩子
                if (!resource->PostLoad(&m_Factory)) {
                    std::cerr << "[ResourceManager] PostLoad failed: "
                              << pathA << " | " << pathB << std::endl;
                    resource->SetState(ResourceState::Failed);
                    return nullptr;
                }
                resource->SetState(ResourceState::Loaded);
                m_Cache[id] = std::weak_ptr<Resource>(resource);
                // 自动加入文件监视两个着色器文件
                if (auto* fw = GetFileWatcher()) {
                    fw->Watch(pathA);
                    fw->Watch(pathB);
                }
            }
            return resource;
        }

        // ============================================================
        // 便捷加载方法（显式命名）
        // ============================================================

        TextureHandle LoadTexture(const std::string& path) {
            return Load<Texture>(path);
        }

        ShaderHandle LoadShader(const std::string& vertexPath,
                                const std::string& fragmentPath) {
            return Load<Shader>(vertexPath, fragmentPath);
        }

        AudioClipHandle LoadAudio(const std::string& path) {
            return Load<AudioClip>(path);
        }

        // ============================================================
        // 外部注册接口（供 UIManager 等系统将已创建的资源注入缓存）
        // ============================================================

        /**
         * @brief 将已创建的资源注册到缓存中
         * @tparam T 资源类型
         * @param resource 已构造完成的资源（必须已设置 Loaded 状态）
         *
         * 用于无法通过文件路径直接加载的资源类型（如 Font 需要 ImGui 上下文）。
         * 若同路径已有缓存且仍存活，则返回已有资源而非替换。
         */
        template<typename T>
        std::shared_ptr<T> Register(std::shared_ptr<T> resource) {
            if (!resource) return nullptr;

            const auto& path = resource->GetPath();
            auto it = m_Cache.find(path);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    return existing;  // 已有缓存，返回已有
                }
            }

            m_Cache[path] = std::weak_ptr<Resource>(resource);
            return resource;
        }

        // ============================================================
        // 通用查询
        // ============================================================

        /** 按路径查询任意资源，返回基类指针 */
        std::shared_ptr<Resource> GetResource(const std::string& path) const;

        /** 按类型和路径查询资源（模板版本） */
        template<typename T>
        std::shared_ptr<T> Get(const std::string& path) const {
            auto res = GetResource(path);
            if (res && res->GetState() == ResourceState::Loaded) {
                return std::dynamic_pointer_cast<T>(res);
            }
            return nullptr;
        }

        /** 查询指定类型的资源路径列表 */
        std::vector<std::string> GetPathsByType(ResourceType type) const;

        /** 检查某路径资源是否已缓存 */
        bool Has(const std::string& path) const;

        // ============================================================
        // 缓存管理
        // ============================================================

        /** 移除指定路径的缓存条目 */
        void Unload(const std::string& path);
        /** 清空所有缓存（注意：外部 shared_ptr 持有者仍能保持资源存活） */
        void UnloadAll();
        /** 清理所有已无外部引用的缓存条目 */
        void UnloadUnused();

        // ============================================================
        // 热加载
        // ============================================================

        /**
         * @brief 检测文件变更并触发资源热加载（每帧在主线程调用）
         *
         * 内部调用 FileWatcher::PollPendingChanges，对每个变更文件
         * 执行 Reload(path)，并通知已注册的回调。
         */
        void PollHotReload();

        /**
         * @brief 强制重新加载指定资源
         * @param path 资源路径
         * @return 是否成功重新加载
         *
         * 此操作会：
         *   1. 查找缓存中的资源
         *   2. 调用 resource->Reload()（释放旧资源、重新从磁盘加载）
         *   3. 递增资源版本号
         *   4. 调用此路径绑定的所有 reload 回调
         */
        bool Reload(const std::string& path);

        // ============================================================
        // 热加载回调观察者模式
        // ============================================================

        /** 资源变更事件回调签名 (path) */
        using ReloadCallback = std::function<void(const std::string& path)>;

        /**
         * @brief 绑定热加载回调
         * @param path    资源路径（也可用 "*" 监听所有资源）
         * @param callback 当资源被重新加载时调用的回调
         * @return 回调 ID（用于解绑）
         *
         * 使用示例：
         * @code
         *   auto id = ResourceManager::Get()->BindReloadCallback(
         *       "assets/textures/player.png",
         *       [](const std::string& path) {
         *           // 纹理已重载，刷新使用该纹理的 SpriteComponent
         *       }
         *   );
         * @endcode
         */
        uint32 BindReloadCallback(const std::string& path, ReloadCallback callback);

        /**
         * @brief 解绑热加载回调
         * @param id 回调 ID（BindReloadCallback 的返回值）
         */
        void UnbindReloadCallback(uint32 id);

        /** 打印当前缓存统计到控制台 */
        void LogStats() const;

        /** 获取缓存条目总数 */
        size_t GetCacheCount() const { return m_Cache.size(); }

        // ============================================================
        // 注册表（GUID + 生命周期 + 交叉引用）
        // ============================================================

        /** 获取资源注册表引用 */
        ResourceRegistry& GetRegistry() { return m_Registry; }
        const ResourceRegistry& GetRegistry() const { return m_Registry; }

        /**
         * @brief 使用 GUID 查找资源
         * @param guid 资源 GUID
         * @return 资源 shared_ptr，未找到返回 nullptr
         */
        template<typename T = Resource>
        std::shared_ptr<T> GetByGUID(const ResourceGUID& guid) {
            return m_Registry.Get<T>(guid);
        }

        /**
         * @brief 确保注册表的池已为所有资源类型初始化
         */
        void InitPools();

        // ============================================================
        // 异步加载队列
        // ============================================================

        /**
         * @brief 异步加载资源
         * @tparam T 资源类型
         * @param path     资源路径
         * @param callback 完成回调（在主线程 PollAsyncLoads 中触发）
         * @param priority 优先级（越大越优先，默认 0）
         * @return 请求 ID（可用于取消）
         *
         * 加载在主线程通过 PollAsyncLoads() 递送结果。
         * 若该资源已在缓存中，回调会立即在主线程下一次 PollAsyncLoads 时调用。
         */
        template<typename T>
        uint64 LoadAsync(const std::string& path,
                         AsyncLoadCallback callback,
                         int32 priority = 0) {
            static_assert(std::is_base_of_v<Resource, T>,
                          "T must derive from Resource");

            uint64 requestId = ++m_NextRequestId;

            // 检查缓存
            auto it = m_Cache.find(path);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    // 缓存命中——排入完成队列立即返回
                    std::lock_guard<std::mutex> lock(m_ResultMutex);
                    m_CompletedResults.push({
                        true, path, existing->GetType(),
                        existing, 0, ""
                    });
                    return requestId;
                }
            }

            // 排入异步加载队列
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            m_LoadQueue.push({
                requestId, path, ResourceTypeFor<T>(),
                priority, callback
            });
            m_LoadCV.notify_one();
            return requestId;
        }

        /** 双路径版本（用于 Shader） */
        template<typename T>
        uint64 LoadAsync(const std::string& pathA,
                         const std::string& pathB,
                         AsyncLoadCallback callback,
                         int32 priority = 0) {
            static_assert(std::is_base_of_v<Resource, T>,
                          "T must derive from Resource");

            std::string id = pathA + "|" + pathB;
            uint64 requestId = ++m_NextRequestId;

            auto it = m_Cache.find(id);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    std::lock_guard<std::mutex> lock(m_ResultMutex);
                    m_CompletedResults.push({
                        true, id, existing->GetType(),
                        existing, 0, ""
                    });
                    return requestId;
                }
            }

            // 对于 Shader 等双资源，使用复合路径作为加载 ID
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            m_LoadQueue.push({
                requestId, id,
                ResourceTypeFor<T>(),
                priority, callback,
                pathA, pathB  // 存储双路径供后台加载
            });
            m_LoadCV.notify_one();
            return requestId;
        }

        /**
         * @brief 在主线程每帧调用，递送异步加载结果
         *
         * 后台线程加载完成后将结果排入 m_CompletedResults 队列，
         * 此函数在 Run() 的主循环中调用，在主线程触发回调。
         */
        void PollAsyncLoads();

        /** 等待所有异步加载完成（关闭前调用） */
        void WaitAllAsyncLoads();

        /** 取消指定请求 */
        void CancelAsyncLoad(uint64 requestId);

        // ============================================================
        // 内存预算管理
        // ============================================================

        /**
         * @brief 设置某类型资源的内存预算
         * @param type     资源类型
         * @param maxBytes 最大字节数（0 = 无限制）
         * @param maxCount 最大实例数（0 = 无限制）
         */
        void SetBudget(ResourceType type, uint64 maxBytes, uint32 maxCount);

        /** 获取当前内存使用统计 */
        uint64 GetMemoryUsage(ResourceType type) const;
        uint64 GetTotalMemoryUsage() const;

        /** 获取当前各类型资源的实例数 */
        uint32 GetResourceCount(ResourceType type) const;

        /** 尝试释放内存直到低于预算（根据 LRU） */
        void EnforceBudgets();

        ~ResourceManager() = default;

    private:
        ResourceManager(IGraphicsFactory& factory);

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        // ── 具体加载实现（按类型派发） ──
        template<typename T>
        std::shared_ptr<T> LoadByType(const std::string& path);

        template<typename T>
        std::shared_ptr<T> LoadByType(const std::string& pathA,
                                      const std::string& pathB);

        /** 获取 FileWatcher 单例（内联，方便模板方法调用） */
        static FileWatcher* GetFileWatcher() { return FileWatcher::Get(); }

        static ResourceManager* s_Instance;
        static std::unique_ptr<ResourceManager> s_InstanceOwner;

        IGraphicsFactory& m_Factory;

        // GUID 注册表 + 交叉引用系统
        ResourceRegistry m_Registry;

        // 按路径索引的弱引用缓存（资源存活由外部 shared_ptr 决定）
        std::unordered_map<std::string, std::weak_ptr<Resource>> m_Cache;

        // ── 异步加载队列 ──

        struct AsyncRequest {
            uint64          requestId;
            std::string     cacheKey;     // 缓存键
            ResourceType    resourceType;
            int32           priority      = 0;
            AsyncLoadCallback callback;
            // 双路径资源（Shader）用
            std::string     extraPath;
        };

        // 后台加载线程
        std::thread                     m_LoadThread;
        std::mutex                      m_LoadMutex;
        std::condition_variable         m_LoadCV;
        std::queue<AsyncRequest>        m_LoadQueue;
        std::atomic<bool>               m_LoadRunning{false};
        std::atomic<uint64>             m_NextRequestId{0};

        // 完成队列（后台→主线程）
        std::mutex                      m_ResultMutex;
        std::queue<AsyncLoadResult>     m_CompletedResults;

        /** 后台线程函数 */
        void LoadWorker();

        /** 在后台线程执行实际加载 */
        AsyncLoadResult ExecuteLoad(const AsyncRequest& req);

        // ── 内存预算 ──

        struct TypeStats {
            uint64 bytesAllocated = 0;
            uint32 count         = 0;
            uint32 loadOrder     = 0;  // 越大越近期加载（用于 LRU 淘汰）
        };

        std::unordered_map<ResourceType, TypeStats> m_TypeStats;
        std::unordered_map<ResourceType, MemoryBudget> m_Budgets;
        std::atomic<uint32> m_LoadOrderCounter{0};

        /** 跟踪资源分配（每次加载成功后调用） */
        void TrackAllocation(ResourceType type, uint64 bytes);
        /** 跟踪资源释放（每次卸载时调用） */
        void TrackDeallocation(ResourceType type, uint64 bytes);

        // ── 热加载回调系统 ──
        struct ReloadCallbackEntry {
            uint32         id;
            std::string    path;       // 监听的资源路径，"" 表示全部
            ReloadCallback callback;
        };
        std::vector<ReloadCallbackEntry> m_ReloadCallbacks;
        mutable std::mutex               m_CallbackMutex;
        uint32                           m_NextCallbackId = 1;

        // ── 类型 → ResourceType 映射 ──
        template<typename T>
        static constexpr ResourceType ResourceTypeFor() {
            if constexpr (std::is_same_v<T, Texture>)     return ResourceType::Texture;
            if constexpr (std::is_same_v<T, Shader>)      return ResourceType::Shader;
            if constexpr (std::is_same_v<T, AudioClip>)   return ResourceType::AudioClip;
            return ResourceType::Unknown;
        }
    };

} // namespace Engine
