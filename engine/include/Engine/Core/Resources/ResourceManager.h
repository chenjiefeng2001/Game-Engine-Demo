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
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/Resources/AsyncLoadData.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Audio/AudioLoader.h"
#include "Engine/Core/IGraphicsFactory.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <future>
#include <condition_variable>

namespace Engine {

    class ResourceManager {
    public:
        // ── 单例生命周期 ──
        static void Init(IGraphicsFactory& factory);
        static void Shutdown();
        static ResourceManager* Get() { return s_Instance; }

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

        // ============================================================
        // 异步加载
        // ============================================================

        /**
         * @brief 通用资源异步加载模板（单路径版本）
         * @tparam T 资源类型，必须继承自 Resource
         * @param path 资源文件路径
         * @return 资源共享指针（若已缓存则直接返回已加载的资源，
         *         否则返回 Loading 状态的资源，后台进行 I/O）
         *
         * 适用类型：Texture, AudioClip
         *
         * 使用示例：
         * @code
         *   auto tex = ResourceManager::Get()->LoadAsync<Texture>("tex.png");
         *   // tex 可能尚未加载完成
         *   // ... 若干帧后 ...
         *   if (tex->IsLoaded()) { OnTextureReady(tex); }
         * @endcode
         */
        template<typename T>
        std::shared_ptr<T> LoadAsync(const std::string& path) {
            static_assert(std::is_base_of_v<Resource, T>,
                          "T must derive from Resource");

            // 检查缓存
            auto it = m_Cache.find(path);
            if (it != m_Cache.end()) {
                if (auto existing = std::dynamic_pointer_cast<T>(it->second.lock())) {
                    return existing;  // 已缓存（无论是否加载完成）
                }
            }

            // 检查是否已有异步任务排期
            if (HasAsyncJob(path))
                return nullptr;  // 已在队列中

            // 创建资源对象，状态设为 Loading
            auto resource = std::make_shared<T>(path);
            resource->SetState(ResourceState::Loading);
            m_Cache[path] = std::weak_ptr<Resource>(resource);

            // 注册文件监视（加载完成后生效）
            if (auto* fw = GetFileWatcher())
                fw->Watch(path);

            // 创建并排入异步任务
            EnqueueAsyncIO<T>(path, resource);
            return resource;
        }

        template<typename T>
        std::shared_ptr<T> LoadAsync(const std::string& pathA,
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

            if (HasAsyncJob(id))
                return nullptr;

            auto resource = std::make_shared<T>(id);
            resource->SetState(ResourceState::Loading);
            m_Cache[id] = std::weak_ptr<Resource>(resource);

            if (auto* fw = GetFileWatcher()) {
                fw->Watch(pathA);
                fw->Watch(pathB);
            }

            EnqueueAsyncIO<T>(pathA, pathB, resource);
            return resource;
        }

        /**
         * @brief 异步加载完成处理（每帧在主线程调用）
         *
         * 检查后台线程是否完成了文件 I/O，
         * 若完成则在主线程执行 GPU/API 上传。
         */
        void ProcessAsyncLoads();

        /**
         * @brief 查询指定路径是否正在异步加载中
         */
        bool IsLoading(const std::string& path) const;

        /** 打印当前缓存统计到控制台 */
        void LogStats() const;

        /** 获取缓存条目总数 */
        size_t GetCacheCount() const { return m_Cache.size(); }

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

        // ── 异步加载内部方法 ──
        /** 创建并排入异步 I/O 任务（单路径） */
        template<typename T>
        void EnqueueAsyncIO(const std::string& path, std::shared_ptr<T> resource);

        /** 创建并排入异步 I/O 任务（双路径，如 Shader） */
        template<typename T>
        void EnqueueAsyncIO(const std::string& pathA, const std::string& pathB,
                            std::shared_ptr<T> resource);

        /** 检查路径是否已有排期的异步任务 */
        bool HasAsyncJob(const std::string& path) const;

        /** 后台线程主循环 */
        void AsyncWorkerLoop();

        static ResourceManager* s_Instance;
        static std::unique_ptr<ResourceManager> s_InstanceOwner;

        IGraphicsFactory& m_Factory;

        // 按路径索引的弱引用缓存（资源存活由外部 shared_ptr 决定）
        // key = 资源路径/ID, value = weak_ptr（过期后自动失效）
        std::unordered_map<std::string, std::weak_ptr<Resource>> m_Cache;

        // ── 热加载回调系统 ──
        struct ReloadCallbackEntry {
            uint32         id;
            std::string    path;      
            ReloadCallback callback;
        };
        std::vector<ReloadCallbackEntry> m_ReloadCallbacks;
        mutable std::mutex               m_CallbackMutex;
        uint32                           m_NextCallbackId = 1;

        // ── 异步加载系统 ──
        struct AsyncJob {
            std::string               path;
            ResourceType              type;
            std::weak_ptr<Resource>   resource;
            // 后台线程执行：文件 I/O + 解码，返回解码数据
            std::function<std::shared_ptr<void>()>  backgroundIO;
            // 主线程执行：GPU/API 上传，参数为 backgroundIO 的返回值
            std::function<bool(std::shared_ptr<void>)> finalizeOnMain;
            std::shared_ptr<void>     decodedData;  
            bool                      ioCompleted = false;
        };

        mutable std::mutex           m_AsyncMutex;
        std::vector<AsyncJob>        m_AsyncJobs;      
        std::vector<size_t>          m_CompletedJobs; 
        std::thread                  m_AsyncThread;
        std::atomic<bool>            m_AsyncThreadRunning{ false };
        std::atomic<bool>            m_AsyncStopRequested{ false };
        std::condition_variable      m_AsyncCV;
    };

} // namespace Engine
