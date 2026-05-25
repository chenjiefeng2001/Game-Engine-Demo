#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/FileWatcher.h"

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "stb_image.h"

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

        // 停止异步线程
        if (s_Instance->m_AsyncThreadRunning.load())
        {
            s_Instance->m_AsyncStopRequested = true;
            s_Instance->m_AsyncCV.notify_all();
            if (s_Instance->m_AsyncThread.joinable())
                s_Instance->m_AsyncThread.join();
        }

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

    // ============================================================
    // 异步加载
    // ============================================================

    // ── 模板特化：EnqueueAsyncIO<Texture> ──
    // 后台：stbi_load 解码图片
    // 主线程：OpenGL 纹理上传
    template<>
    void ResourceManager::EnqueueAsyncIO<Texture>(
        const std::string& path, std::shared_ptr<Texture> resource)
    {
        AsyncJob job;
        job.path = path;
        job.type = ResourceType::Texture;
        job.resource = resource;

        job.backgroundIO = [path]() -> std::shared_ptr<void> {
            // 后台线程——文件读取 + 解码
            auto data = std::make_shared<TextureLoadData>();
            data->path = path;

            stbi_set_flip_vertically_on_load(true);
            data->pixels = stbi_load(path.c_str(),
                &data->width, &data->height, &data->channels, 0);

            if (!data->IsValid())
            {
                std::cerr << "[AsyncIO] Failed to load texture: " << path << std::endl;
            }
            return data;
        };

        job.finalizeOnMain = [this, resource](std::shared_ptr<void> decoded) -> bool {
            auto* texData = static_cast<TextureLoadData*>(decoded.get());
            if (!texData || !texData->IsValid())
            {
                resource->SetState(ResourceState::Failed);
                return false;
            }

            // 主线程通过虚拟 Reload() 接口触发纹理重新上传
            // 注：此处的 decoded pixel data 尚未被消费，后续可扩展
            // Texture 基类以支持直接接收 raw pixel data 上传，
            // 从而避免 Reload() 重复读取磁盘，进一步完善异步流水线。
            return resource->Reload();
        };

        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_AsyncJobs.push_back(std::move(job));
            if (!m_AsyncThreadRunning.load())
            {
                m_AsyncStopRequested = false;
                m_AsyncThreadRunning = true;
                m_AsyncThread = std::thread(&ResourceManager::AsyncWorkerLoop, this);
            }
        }
        m_AsyncCV.notify_one();
    }

    // ── 模板特化：EnqueueAsyncIO<AudioClip> ──
    template<>
    void ResourceManager::EnqueueAsyncIO<AudioClip>(
        const std::string& path, std::shared_ptr<AudioClip> resource)
    {
        AsyncJob job;
        job.path = path;
        job.type = ResourceType::AudioClip;
        job.resource = resource;

        job.backgroundIO = [path]() -> std::shared_ptr<void> {
            // 后台线程——文件读取 + PCM 解码
            auto audioData = std::make_shared<AudioData>(AudioLoader::Load(path));
            if (!audioData->IsValid())
            {
                std::cerr << "[AsyncIO] Failed to decode audio: " << path << std::endl;
            }
            return audioData;
        };

        job.finalizeOnMain = [resource](std::shared_ptr<void> decoded) -> bool {
            // 主线程执行 Reload（释放旧 OpenAL 缓冲，重新加载）
            return resource->Reload();
        };

        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_AsyncJobs.push_back(std::move(job));
            if (!m_AsyncThreadRunning.load())
            {
                m_AsyncStopRequested = false;
                m_AsyncThreadRunning = true;
                m_AsyncThread = std::thread(&ResourceManager::AsyncWorkerLoop, this);
            }
        }
        m_AsyncCV.notify_one();
    }

    // ── 模板特化：EnqueueAsyncIO<Shader> ──
    template<>
    void ResourceManager::EnqueueAsyncIO<Shader>(
        const std::string& pathA, const std::string& pathB, std::shared_ptr<Shader> resource)
    {
        // 着色器加载：后台读取源码，主线程编译链接
        AsyncJob job;
        job.path = pathA + "|" + pathB;
        job.type = ResourceType::Shader;
        job.resource = resource;

        job.backgroundIO = [pathA, pathB]() -> std::shared_ptr<void> {
            auto shaderData = std::make_shared<ShaderLoadData>();
            shaderData->vertexPath = pathA;
            shaderData->fragmentPath = pathB;

            // 后台读取文件
            std::ifstream vf(pathA);
            std::ifstream ff(pathB);
            if (vf.is_open()) {
                std::stringstream ss; ss << vf.rdbuf();
                shaderData->vertexSource = ss.str();
            }
            if (ff.is_open()) {
                std::stringstream ss; ss << ff.rdbuf();
                shaderData->fragmentSource = ss.str();
            }

            return shaderData;
        };

        job.finalizeOnMain = [resource](std::shared_ptr<void> decoded) -> bool {
            return resource->Reload();
        };

        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            m_AsyncJobs.push_back(std::move(job));
            if (!m_AsyncThreadRunning.load())
            {
                m_AsyncStopRequested = false;
                m_AsyncThreadRunning = true;
                m_AsyncThread = std::thread(&ResourceManager::AsyncWorkerLoop, this);
            }
        }
        m_AsyncCV.notify_one();
    }

    // ── 处理已完成异步加载（主线程每帧调用） ──

    void ResourceManager::ProcessAsyncLoads()
    {
        std::vector<size_t> completed;
        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            completed.swap(m_CompletedJobs);
        }

        for (size_t idx : completed)
        {
            if (idx >= m_AsyncJobs.size()) continue;

            auto& job = m_AsyncJobs[idx];
            if (!job.ioCompleted) continue;

            // 检查资源是否仍存活
            auto resource = job.resource.lock();
            if (!resource)
            {
                std::cerr << "[AsyncIO] Resource expired before finalize: "
                          << job.path << std::endl;
                continue;
            }

            // 主线程执行 GPU/API 上传
            bool success = false;
            if (job.finalizeOnMain)
            {
                success = job.finalizeOnMain(job.decodedData);
            }

            if (success)
            {
                resource->SetState(ResourceState::Loaded);
                resource->BumpReloadVersion();

                // 通知热加载回调
                std::lock_guard<std::mutex> cbLock(m_CallbackMutex);
                for (const auto& entry : m_ReloadCallbacks)
                {
                    if (entry.path == job.path || entry.path == "*")
                        entry.callback(job.path);
                }

                std::cout << "[AsyncIO] Async load complete: " << job.path << std::endl;
            }
            else
            {
                resource->SetState(ResourceState::Failed);
                std::cerr << "[AsyncIO] Async load failed: " << job.path << std::endl;
            }
        }

        // 清理已完成的任务
        if (!completed.empty())
        {
            std::lock_guard<std::mutex> lock(m_AsyncMutex);
            // 标记不再使用之后，统一清除
            // 注意：这里不能直接 erase 因为可能还有其他线程引用索引
            for (auto it = m_AsyncJobs.begin(); it != m_AsyncJobs.end(); )
            {
                if (it->ioCompleted)
                    it = m_AsyncJobs.erase(it);
                else
                    ++it;
            }
        }
    }

    // ── 查询是否正在加载 ──

    bool ResourceManager::IsLoading(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(m_AsyncMutex);
        for (const auto& job : m_AsyncJobs)
        {
            if (job.path == path && !job.ioCompleted)
                return true;
        }
        return false;
    }

    // ── 检查是否已有排期的异步任务 ──

    bool ResourceManager::HasAsyncJob(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(m_AsyncMutex);
        for (const auto& job : m_AsyncJobs)
        {
            if (job.path == path)
                return true;
        }
        return false;
    }

    // ── 后台线程主循环 ──

    void ResourceManager::AsyncWorkerLoop()
    {
        std::cout << "[AsyncIO] Worker thread started" << std::endl;

        while (!m_AsyncStopRequested.load())
        {
            // 等待任务
            std::unique_lock<std::mutex> lock(m_AsyncMutex);
            m_AsyncCV.wait(lock, [this]() {
                if (m_AsyncStopRequested.load()) return true;
                for (const auto& job : m_AsyncJobs)
                    if (!job.ioCompleted && job.backgroundIO)
                        return true;
                return false;
            });

            if (m_AsyncStopRequested.load()) break;

            // 找第一个未完成的 job
            size_t idx = SIZE_MAX;
            for (size_t i = 0; i < m_AsyncJobs.size(); ++i)
            {
                if (!m_AsyncJobs[i].ioCompleted && m_AsyncJobs[i].backgroundIO)
                {
                    idx = i;
                    break;
                }
            }

            if (idx == SIZE_MAX)
            {
                lock.unlock();
                continue;
            }

            auto& job = m_AsyncJobs[idx];
            lock.unlock();

            // 执行后台 I/O
            std::shared_ptr<void> result;
            try
            {
                result = job.backgroundIO();
            }
            catch (const std::exception& e)
            {
                std::cerr << "[AsyncIO] Background IO error: " << e.what() << std::endl;
            }

            // 标记完成
            {
                std::lock_guard<std::mutex> lock2(m_AsyncMutex);
                m_AsyncJobs[idx].decodedData = result;
                m_AsyncJobs[idx].ioCompleted = true;
                m_CompletedJobs.push_back(idx);
            }
        }

        std::cout << "[AsyncIO] Worker thread stopped" << std::endl;
    }

} // namespace Engine
