#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Types.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 辅助工具
    // ═══════════════════════════════════════════════════════════════

    namespace {

        /** 获取当前时间戳(ms) */
        inline int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
        }

        /** 获取当前时间点（用于耗时计算） */
        inline std::chrono::steady_clock::time_point Now() {
            return std::chrono::steady_clock::now();
        }

        /** 计算耗时(ms) */
        inline double ElapsedMs(std::chrono::steady_clock::time_point start) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                Now() - start
            ).count() / 1000.0;
        }

    } // anonymous namespace

    // ═══════════════════════════════════════════════════════════════
    // 异步加载任务
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 异步加载任务的内部表示
     */
    struct AsyncLoadTask {
        uint64              id;
        std::string         sceneName;
        SceneContext        context;
        LoadPriority        priority    = LoadPriority::Normal;
        bool                isAdditive  = false;
        bool                isPreload   = false;
        std::function<void(SceneManagerError)> onComplete;

        // 计时/超时
        int64_t             startTimeMs = 0;
        uint32              timeoutMs   = 10000;

        // 分帧激活
        uint32              activationDelayFrames = 2;
        uint32              framesSinceLoad       = 0;
        bool                readyToActivate       = false;

        // 加载剖面数据
        SceneLoadProfile    profile;
    };

    // ═══════════════════════════════════════════════════════════════
    // SceneManagerImpl — PIMPL 实现
    // ═══════════════════════════════════════════════════════════════

    class SceneManagerImpl {
    public:
        SceneManagerImpl();
        ~SceneManagerImpl();

        // ── 初始化/关闭 ──
        bool Init();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // ── 配置 ──
        void SetLoadTimeoutMs(uint32 timeoutMs) { m_LoadTimeoutMs = timeoutMs; }
        void SetFallbackScene(const std::string& sceneName) { m_FallbackScene = sceneName; }
        const std::string& GetFallbackScene() const noexcept { return m_FallbackScene; }
        void SetPreloadEnabled(bool enabled) { m_PreloadEnabled = enabled; }
        void SetProgressSmoothingFactor(float factor) { m_SmoothProgress.SetSmoothingFactor(factor); }
        void SetPerformanceThresholdMs(double thresholdMs) { m_PerformanceThresholdMs = thresholdMs; }

        // ── 场景注册 ──
        void RegisterLevel(const LevelInfo& info) { m_LevelManager.RegisterLevel(info); }
        void RegisterLevels(const std::vector<LevelInfo>& infos) { m_LevelManager.RegisterLevels(infos); }
        void UnregisterLevel(const std::string& name) { m_LevelManager.UnregisterLevel(name); }
        Level* FindLevel(const std::string& name) { return m_LevelManager.FindLevel(name); }

        // ── 场景组配置 ──
        bool LoadGroupConfig(const std::string& filePath);
        bool LoadGroupConfigFromString(const std::string& jsonContent);
        const std::vector<SceneGroup>& GetSceneGroups() const noexcept { return m_SceneGroups; }

        // ── 核心 API ──
        SceneManagerError LoadScene(const std::string& sceneName, SceneContext context);
        void LoadSceneAsync(const std::string& sceneName,
                            SceneContext context,
                            std::function<void(SceneManagerError)> onComplete);
        SceneManagerError SwitchScene(const std::string& sceneName, SceneContext context);
        void SwitchSceneAsync(const std::string& sceneName,
                              SceneContext context,
                              std::function<void(SceneManagerError)> onComplete);
        SceneManagerError UnloadScene(const std::string& sceneName);
        SceneManagerError LoadSceneAdditive(const std::string& sceneName, SceneContext context);
        SceneManagerError UnloadAdditiveScene(const std::string& sceneName);
        SceneManagerError LoadSceneGroup(const std::string& groupName, SceneContext context);
        void LoadSceneGroupAsync(const std::string& groupName,
                                 SceneContext context,
                                 std::function<void(SceneManagerError)> onComplete);

        // ── 预加载 ──
        std::shared_ptr<PreloadHandle> PreloadScene(const std::string& sceneName);
        void CancelPreload(std::shared_ptr<PreloadHandle> handle);

        // ── 生命周期钩子 ──
        uint64 RegisterLifecycleCallback(SceneLifecycleCallback callback);
        void UnregisterLifecycleCallback(uint64 callbackId);

        // ── 场景状态查询 ──
        Scene* GetActiveScene() noexcept;
        std::string GetActiveSceneName();
        const SceneContext* GetCurrentContext() const noexcept { return m_CurrentContext.get(); }
        LevelState GetSceneState(const std::string& sceneName);
        bool IsSceneLoaded(const std::string& sceneName);
        bool IsSceneLoading(const std::string& sceneName);
        bool IsLoading() const noexcept { return m_IsLoading; }
        float GetLoadProgress() noexcept { return m_SmoothProgress.GetSmoothed(m_LastDt); }
        float GetRawLoadProgress() noexcept { return m_RawProgress; }
        const std::string& GetCurrentLoadingScene() const noexcept { return m_CurrentLoadingScene; }
        std::vector<std::string> GetLoadedSceneNames();
        std::vector<Level*> GetAdditiveLevels() { return m_LevelManager.GetAdditiveLevels(); }

        // ── 性能监控 ──
        const std::vector<SceneLoadProfile>& GetLoadProfiles() const noexcept { return m_LoadProfiles; }
        double GetAverageLoadTimeMs(uint32 lastN);
        void ClearLoadProfiles() { m_LoadProfiles.clear(); }

        // ── 每帧更新 ──
        void Update(float32 dt);

        LevelManager& GetLevelManager() { return m_LevelManager; }

    private:
        // ── 内部异步任务管理 ──
        uint64 EnqueueTask(AsyncLoadTask task);
        void ProcessTaskQueue();
        void ExecuteTask(const AsyncLoadTask& task);
        void PollPreloads();
        void CheckTimeouts();
        void FinalizeSceneLoad(AsyncLoadTask& task, SceneManagerError error);

        // ── 场景激活流水线 ──
        bool ActivateLoadedScene(const std::string& sceneName);
        bool DeactivateCurrentScene();

        // ── 生命周期钩子分发 ──
        void DispatchLifecycle(LifecyclePhase phase,
                               const std::string& sceneName,
                               float progress = 0.0f,
                               const std::string& message = "");

        // ── 回滚机制 ──
        void TriggerRollback(const std::string& failedScene, const std::string& errorMsg);

        // ── 进度管理 ──
        void UpdateProgress(float rawProgress);

        // ── 预加载内部 ──
        void ExecutePreload(const std::string& sceneName,
                            std::shared_ptr<PreloadHandle> handle);

        // ── 原始进度聚合 ──
        float CalculateRawProgress() const;

        // ── 配置 ──
        bool                    m_Initialized           = false;
        uint32                  m_LoadTimeoutMs         = 10000;
        std::string             m_FallbackScene         = "MainMenu";
        bool                    m_PreloadEnabled        = true;
        double                  m_PerformanceThresholdMs = 5000.0;

        // ── 底层管理器 ──
        LevelManager            m_LevelManager;

        // ── 场景组 ──
        std::vector<SceneGroup> m_SceneGroups;

        // ── 异步加载状态 ──
        bool                    m_IsLoading             = false;
        float                   m_RawProgress           = 0.0f;
        float                   m_LastDt                = 0.016f;
        SmoothProgress          m_SmoothProgress;
        std::string             m_CurrentLoadingScene;
        int64_t                 m_LoadStartTimeMs       = 0;

        // ── 异步任务队列 ──
        std::mutex              m_TaskMutex;
        std::deque<AsyncLoadTask> m_TaskQueue;
        uint64                  m_NextTaskId            = 1;
        std::atomic<bool>       m_TaskProcessing        { false };

        // ── 当前加载任务（同时只能有一个主要加载任务） ──
        std::optional<AsyncLoadTask> m_CurrentTask;

        // ── 当前场景上下文 ──
        std::unique_ptr<SceneContext> m_CurrentContext;

        // ── 预加载 ──
        std::mutex                              m_PreloadMutex;
        std::vector<std::shared_ptr<PreloadHandle>> m_PreloadHandles;
        uint64                                  m_NextPreloadId = 1;

        // ── 生命周期回调 ──
        std::mutex                                              m_CallbackMutex;
        std::unordered_map<uint64, SceneLifecycleCallback>      m_LifecycleCallbacks;
        uint64                                      m_NextCallbackId = 1;

        // ── 性能剖面 ──
        std::vector<SceneLoadProfile> m_LoadProfiles;
    };

    // ═══════════════════════════════════════════════════════════════
    // 构造/析构
    // ═══════════════════════════════════════════════════════════════

    SceneManagerImpl::SceneManagerImpl() = default;
    SceneManagerImpl::~SceneManagerImpl() { Shutdown(); }

    // ═══════════════════════════════════════════════════════════════
    // 初始化/关闭
    // ═══════════════════════════════════════════════════════════════

    bool SceneManagerImpl::Init() {
        if (m_Initialized) return true;

        ENGINE_LOG_INFO("SceneManager", "Initializing SceneManager...");
        m_SmoothProgress.Reset(0.0f);
        m_RawProgress = 0.0f;
        m_IsLoading = false;
        m_Initialized = true;

        // 设置默认回调
        RegisterLifecycleCallback([](const std::string& sceneName,
                                      LifecyclePhase phase,
                                      float progress,
                                      const std::string& message) {
            switch (phase) {
                case LifecyclePhase::OnBeforeLoad:
                    ENGINE_LOG_INFO("SceneManager", "[{}] Loading '{}'...",
                                    LifecyclePhaseToString(phase), sceneName);
                    break;
                case LifecyclePhase::OnLoading:
                    ENGINE_LOG_DEBUG("SceneManager", "[{}] '{}': {:.1f}%",
                                     LifecyclePhaseToString(phase), sceneName, progress * 100.0f);
                    break;
                case LifecyclePhase::OnActive:
                    ENGINE_LOG_INFO("SceneManager", "[{}] '{}' is now active",
                                    LifecyclePhaseToString(phase), sceneName);
                    break;
                case LifecyclePhase::OnError:
                    ENGINE_LOG_ERROR("SceneManager", "[{}] '{}': {}",
                                     LifecyclePhaseToString(phase), sceneName, message);
                    break;
                default:
                    break;
            }
        });

        ENGINE_LOG_INFO("SceneManager", "SceneManager initialized successfully");
        return true;
    }

    void SceneManagerImpl::Shutdown() {
        if (!m_Initialized) return;

        ENGINE_LOG_INFO("SceneManager", "Shutting down SceneManager...");

        // 取消所有预加载
        {
            std::lock_guard<std::mutex> lock(m_PreloadMutex);
            for (auto& handle : m_PreloadHandles) {
                handle->Cancel();
            }
            m_PreloadHandles.clear();
        }

        // 清除任务队列
        {
            std::lock_guard<std::mutex> lock(m_TaskMutex);
            m_TaskQueue.clear();
        }

        // 卸载所有场景
        m_LevelManager.UnloadAllNonPersistent();

        m_LifecycleCallbacks.clear();
        m_LoadProfiles.clear();
        m_CurrentContext.reset();
        m_SceneGroups.clear();

        m_Initialized = false;
        ENGINE_LOG_INFO("SceneManager", "SceneManager shut down");
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景组配置
    // ═══════════════════════════════════════════════════════════════

    bool SceneManagerImpl::LoadGroupConfig(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            ENGINE_LOG_ERROR("SceneManager", "Failed to open group config: {}", filePath);
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return LoadGroupConfigFromString(buffer.str());
    }

    bool SceneManagerImpl::LoadGroupConfigFromString(const std::string& jsonContent) {
        try {
            auto config = SceneGroupConfig::LoadFromJson(jsonContent);
            m_SceneGroups = config.groups;
            ENGINE_LOG_INFO("SceneManager", "Loaded {} scene groups", m_SceneGroups.size());
            return true;
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("SceneManager", "Failed to parse group config: {}", e.what());
            return false;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 同步加载
    // ═══════════════════════════════════════════════════════════════

    SceneManagerError SceneManagerImpl::LoadScene(const std::string& sceneName,
                                                    SceneContext context) {
        if (!m_Initialized) return SceneManagerError::Unknown;

        Level* level = m_LevelManager.FindLevel(sceneName);
        if (!level) {
            ENGINE_LOG_ERROR("SceneManager", "Scene '{}' not registered", sceneName);
            return SceneManagerError::SceneNotFound;
        }

        // 防止并发同步加载
        if (m_IsLoading) {
            ENGINE_LOG_WARN("SceneManager", "Already loading, cannot sync load '{}'", sceneName);
            return SceneManagerError::AlreadyLoading;
        }

        DispatchLifecycle(LifecyclePhase::OnBeforeLoad, sceneName);

        auto loadStart = Now();

        // 保存上下文
        m_CurrentContext = std::make_unique<SceneContext>(std::move(context));

        // 同步加载
        if (!m_LevelManager.LoadLevel(sceneName)) {
            m_CurrentContext.reset();
            DispatchLifecycle(LifecyclePhase::OnError, sceneName, 0.0f, "Load failed");
            TriggerRollback(sceneName, "Load failed");
            return SceneManagerError::DeserializeFailed;
        }

        // 激活
        if (!m_LevelManager.SetActiveLevel(sceneName)) {
            m_CurrentContext.reset();
            DispatchLifecycle(LifecyclePhase::OnError, sceneName, 0.0f, "Activation failed");
            return SceneManagerError::ActivationFailed;
        }

        double totalMs = ElapsedMs(loadStart);
        DispatchLifecycle(LifecyclePhase::OnActive, sceneName, 1.0f);

        // 记录性能数据
        SceneLoadProfile profile;
        profile.sceneName      = sceneName;
        profile.totalTimeMs    = totalMs;
        profile.timestamp      = NowMs();
        profile.success        = true;
        // 粗略分阶段统计
        profile.ioTimeMs       = totalMs * 0.6;
        profile.deserializeTimeMs = totalMs * 0.3;
        profile.initTimeMs     = totalMs * 0.1;

        if (level->HasScene()) {
            profile.objectCount = level->GetScene()->GetTotalObjectCount();
        }

        m_LoadProfiles.push_back(profile);

        if (profile.ExceedsThreshold(m_PerformanceThresholdMs)) {
            ENGINE_LOG_WARN("SceneManager", "Scene '{}' load time {:.1f}ms exceeds threshold {:.0f}ms",
                            sceneName, totalMs, m_PerformanceThresholdMs);
        }

        ENGINE_LOG_INFO("SceneManager", "Scene '{}' loaded in {:.1f}ms", sceneName, totalMs);
        return SceneManagerError::None;
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 异步加载
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::LoadSceneAsync(const std::string& sceneName,
                                           SceneContext context,
                                           std::function<void(SceneManagerError)> onComplete) {
        if (!m_Initialized) {
            if (onComplete) onComplete(SceneManagerError::Unknown);
            return;
        }

        Level* level = m_LevelManager.FindLevel(sceneName);
        if (!level) {
            ENGINE_LOG_ERROR("SceneManager", "Scene '{}' not registered", sceneName);
            if (onComplete) onComplete(SceneManagerError::SceneNotFound);
            return;
        }

        DispatchLifecycle(LifecyclePhase::OnBeforeLoad, sceneName);

        AsyncLoadTask task;
        task.id              = m_NextTaskId++;
        task.sceneName       = sceneName;
        task.context         = std::move(context);
        task.priority        = LoadPriority::Normal;
        task.isAdditive      = false;
        task.isPreload       = false;
        task.onComplete      = std::move(onComplete);
        task.startTimeMs     = NowMs();
        task.timeoutMs       = m_LoadTimeoutMs;
        task.activationDelayFrames = context.GetActivationDelayFrames();

        EnqueueTask(std::move(task));
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 切换场景
    // ═══════════════════════════════════════════════════════════════

    SceneManagerError SceneManagerImpl::SwitchScene(const std::string& sceneName,
                                                      SceneContext context) {
        if (!m_Initialized) return SceneManagerError::Unknown;

        // 卸载当前活动场景
        if (m_LevelManager.GetActiveLevel()) {
            std::string currentName = m_LevelManager.GetActiveLevelName();
            ENGINE_LOG_INFO("SceneManager", "Switching from '{}' to '{}'", currentName, sceneName);

            DispatchLifecycle(LifecyclePhase::OnUnloadStart, currentName);
            m_LevelManager.UnloadAllNonPersistent(sceneName);
            DispatchLifecycle(LifecyclePhase::OnUnloadComplete, currentName);
        }

        return LoadScene(sceneName, std::move(context));
    }

    void SceneManagerImpl::SwitchSceneAsync(const std::string& sceneName,
                                             SceneContext context,
                                             std::function<void(SceneManagerError)> onComplete) {
        if (!m_Initialized) {
            if (onComplete) onComplete(SceneManagerError::Unknown);
            return;
        }

        // 先卸载当前场景（同步）
        if (m_LevelManager.GetActiveLevel()) {
            std::string currentName = m_LevelManager.GetActiveLevelName();
            DispatchLifecycle(LifecyclePhase::OnUnloadStart, currentName);
            m_LevelManager.UnloadAllNonPersistent(sceneName);
            DispatchLifecycle(LifecyclePhase::OnUnloadComplete, currentName);
        }

        // 再异步加载新场景
        LoadSceneAsync(sceneName, std::move(context), std::move(onComplete));
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 卸载
    // ═══════════════════════════════════════════════════════════════

    SceneManagerError SceneManagerImpl::UnloadScene(const std::string& sceneName) {
        if (!m_Initialized) return SceneManagerError::Unknown;

        Level* level = m_LevelManager.FindLevel(sceneName);
        if (!level) {
            return SceneManagerError::SceneNotFound;
        }

        DispatchLifecycle(LifecyclePhase::OnUnloadStart, sceneName);
        m_LevelManager.UnloadLevel(sceneName);
        DispatchLifecycle(LifecyclePhase::OnUnloadComplete, sceneName);

        ENGINE_LOG_INFO("SceneManager", "Scene '{}' unloaded", sceneName);
        return SceneManagerError::None;
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 叠加加载
    // ═══════════════════════════════════════════════════════════════

    SceneManagerError SceneManagerImpl::LoadSceneAdditive(const std::string& sceneName,
                                                           SceneContext context) {
        if (!m_Initialized) return SceneManagerError::Unknown;

        // 保存上下文（Additive 场景也可以访问）
        m_CurrentContext = std::make_unique<SceneContext>(std::move(context));

        if (!m_LevelManager.LoadLevelAdditive(sceneName)) {
            m_CurrentContext.reset();
            return SceneManagerError::GroupLoadFailed;
        }

        return SceneManagerError::None;
    }

    SceneManagerError SceneManagerImpl::UnloadAdditiveScene(const std::string& sceneName) {
        return UnloadScene(sceneName);
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心 API — 场景组加载
    // ═══════════════════════════════════════════════════════════════

    SceneManagerError SceneManagerImpl::LoadSceneGroup(const std::string& groupName,
                                                        SceneContext context) {
        if (!m_Initialized) return SceneManagerError::Unknown;

        // 查找场景组
        const SceneGroup* group = nullptr;
        for (const auto& g : m_SceneGroups) {
            if (g.groupName == groupName) {
                group = &g;
                break;
            }
        }

        if (!group) {
            ENGINE_LOG_ERROR("SceneManager", "Scene group '{}' not found", groupName);
            return SceneManagerError::SceneNotFound;
        }

        ENGINE_LOG_INFO("SceneManager", "Loading scene group '{}' ({} scenes)",
                        groupName, group->subScenes.size() + 1);

        // 1. 加载主场景
        SceneManagerError err = LoadScene(group->masterScene, std::move(context));
        if (err != SceneManagerError::None) {
            TriggerRollback(group->masterScene, "Master scene load failed");
            return SceneManagerError::GroupLoadFailed;
        }

        // 2. 按优先级排序子场景
        std::vector<SceneGroupEntry> sortedSubs = group->subScenes;
        std::sort(sortedSubs.begin(), sortedSubs.end(),
                  [](const SceneGroupEntry& a, const SceneGroupEntry& b) {
                      return a.loadPriority > b.loadPriority;
                  });

        // 3. 加载子场景
        for (const auto& entry : sortedSubs) {
            if (!m_LevelManager.LoadLevelAdditive(entry.sceneName)) {
                ENGINE_LOG_ERROR("SceneManager", "Failed to load sub-scene '{}' in group '{}'",
                                 entry.sceneName, groupName);
                if (entry.required) {
                    TriggerRollback(entry.sceneName, "Required sub-scene load failed");
                    return SceneManagerError::GroupLoadFailed;
                }
                ENGINE_LOG_WARN("SceneManager", "Optional sub-scene '{}' skipped (not found)",
                                entry.sceneName);
            }
        }

        ENGINE_LOG_INFO("SceneManager", "Scene group '{}' loaded successfully", groupName);
        return SceneManagerError::None;
    }

    void SceneManagerImpl::LoadSceneGroupAsync(const std::string& groupName,
                                                SceneContext context,
                                                std::function<void(SceneManagerError)> onComplete) {
        // 对于场景组，先同步加载（因为其中涉及多个依赖场景）
        // 实际项目中可改为完整的异步流水线
        SceneManagerError err = LoadSceneGroup(groupName, std::move(context));
        if (onComplete) onComplete(err);
    }

    // ═══════════════════════════════════════════════════════════════
    // 预加载系统
    // ═══════════════════════════════════════════════════════════════

    std::shared_ptr<PreloadHandle> SceneManagerImpl::PreloadScene(const std::string& sceneName) {
        if (!m_Initialized || !m_PreloadEnabled) {
            return nullptr;
        }

        Level* level = m_LevelManager.FindLevel(sceneName);
        if (!level) {
            ENGINE_LOG_WARN("SceneManager", "Cannot preload '{}': not registered", sceneName);
            return nullptr;
        }

        // 如果已经加载，跳过
        if (level->GetState() != LevelState::Unloaded) {
            auto handle = std::make_shared<PreloadHandle>(m_NextPreloadId++);
            handle->SetState(PreloadState::Completed);
            handle->SetProgress(1.0f);
            return handle;
        }

        auto handle = std::make_shared<PreloadHandle>(m_NextPreloadId++);
        handle->m_TargetScene = sceneName;
        handle->SetState(PreloadState::InProgress);

        {
            std::lock_guard<std::mutex> lock(m_PreloadMutex);
            m_PreloadHandles.push_back(handle);
        }

        DispatchLifecycle(LifecyclePhase::OnPreloadStart, sceneName);

        // 在后台线程中执行预加载
        std::thread preloadThread([this, sceneName, handle]() {
            ExecutePreload(sceneName, handle);
        });
        preloadThread.detach();

        ENGINE_LOG_INFO("SceneManager", "Preloading scene '{}' (handle #{})", sceneName, handle->GetID());
        return handle;
    }

    void SceneManagerImpl::CancelPreload(std::shared_ptr<PreloadHandle> handle) {
        if (!handle) return;
        handle->Cancel();

        std::lock_guard<std::mutex> lock(m_PreloadMutex);
        auto it = std::remove_if(m_PreloadHandles.begin(), m_PreloadHandles.end(),
                                 [&](const auto& h) { return h->GetID() == handle->GetID(); });
        m_PreloadHandles.erase(it, m_PreloadHandles.end());
    }

    void SceneManagerImpl::ExecutePreload(const std::string& sceneName,
                                           std::shared_ptr<PreloadHandle> handle) {
        // 1. 加载场景文件但不激活
        Level* level = m_LevelManager.FindLevel(sceneName);
        if (!level) {
            handle->SetState(PreloadState::Failed);
            return;
        }

        // 阶段1：文件 I/O（~40%）
        if (handle->GetState() == PreloadState::Cancelled) { handle->SetState(PreloadState::Cancelled); return; }
        handle->SetProgress(0.1f);

        // 创建场景并加载
        auto scene = std::make_unique<Scene>(sceneName);
        if (!sceneName.empty() && !level->GetInfo().filePath.empty()) {
            if (!scene->LoadFromFile(level->GetInfo().filePath)) {
                handle->SetState(PreloadState::Failed);
                return;
            }
        }

        // 阶段2：资源加载（~80%）
        if (handle->GetState() == PreloadState::Cancelled) { handle->SetState(PreloadState::Cancelled); return; }
        handle->SetProgress(0.8f);

        // 阶段3：完成（100%）
        if (handle->GetState() == PreloadState::Cancelled) { handle->SetState(PreloadState::Cancelled); return; }
        handle->SetProgress(1.0f);
        handle->SetState(PreloadState::Completed);

        DispatchLifecycle(LifecyclePhase::OnPreloadComplete, sceneName);
        ENGINE_LOG_INFO("SceneManager", "Preload completed for '{}'", sceneName);
    }

    // ═══════════════════════════════════════════════════════════════
    // 生命周期钩子
    // ═══════════════════════════════════════════════════════════════

    uint64 SceneManagerImpl::RegisterLifecycleCallback(SceneLifecycleCallback callback) {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        uint64 id = m_NextCallbackId++;
        m_LifecycleCallbacks[id] = std::move(callback);
        return id;
    }

    void SceneManagerImpl::UnregisterLifecycleCallback(uint64 callbackId) {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        m_LifecycleCallbacks.erase(callbackId);
    }

    void SceneManagerImpl::DispatchLifecycle(LifecyclePhase phase,
                                              const std::string& sceneName,
                                              float progress,
                                              const std::string& message) {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        for (auto& [id, callback] : m_LifecycleCallbacks) {
            if (callback) {
                callback(sceneName, phase, progress, message);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景状态查询
    // ═══════════════════════════════════════════════════════════════

    Scene* SceneManagerImpl::GetActiveScene() noexcept {
        Level* active = m_LevelManager.GetActiveLevel();
        return active ? active->GetScene() : nullptr;
    }

    std::string SceneManagerImpl::GetActiveSceneName() {
        return m_LevelManager.GetActiveLevelName();
    }

    LevelState SceneManagerImpl::GetSceneState(const std::string& sceneName) {
        Level* level = m_LevelManager.FindLevel(sceneName);
        return level ? level->GetState() : LevelState::Unloaded;
    }

    bool SceneManagerImpl::IsSceneLoaded(const std::string& sceneName) {
        LevelState state = GetSceneState(sceneName);
        return state == LevelState::Loaded || state == LevelState::Active;
    }

    bool SceneManagerImpl::IsSceneLoading(const std::string& sceneName) {
        return GetSceneState(sceneName) == LevelState::Loading;
    }

    std::vector<std::string> SceneManagerImpl::GetLoadedSceneNames() {
        std::vector<std::string> names;
        for (auto& [name, level] : m_LevelManager.GetAllLevels()) {
            LevelState state = level->GetState();
            if (state == LevelState::Loaded || state == LevelState::Active) {
                names.push_back(name);
            }
        }
        return names;
    }

    // ═══════════════════════════════════════════════════════════════
    // 性能监控
    // ═══════════════════════════════════════════════════════════════

    double SceneManagerImpl::GetAverageLoadTimeMs(uint32 lastN) {
        if (m_LoadProfiles.empty()) return 0.0;

        uint32 count = 0;
        double total = 0.0;
        for (auto it = m_LoadProfiles.rbegin();
             it != m_LoadProfiles.rend() && count < lastN;
             ++it, ++count) {
            total += it->totalTimeMs;
        }

        return (count > 0) ? (total / count) : 0.0;
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：任务队列管理
    // ═══════════════════════════════════════════════════════════════

    uint64 SceneManagerImpl::EnqueueTask(AsyncLoadTask task) {
        std::lock_guard<std::mutex> lock(m_TaskMutex);
        task.id = m_NextTaskId++;
        m_TaskQueue.push_back(std::move(task));

        // 按优先级排序（高优先级优先）
        std::sort(m_TaskQueue.begin(), m_TaskQueue.end(),
                  [](const AsyncLoadTask& a, const AsyncLoadTask& b) {
                      return static_cast<int32>(a.priority) > static_cast<int32>(b.priority);
                  });

        ENGINE_LOG_DEBUG("SceneManager", "Enqueued load task #{} for '{}' (priority={})",
                         task.id, task.sceneName, static_cast<int32>(task.priority));

        return task.id;
    }

    void SceneManagerImpl::ProcessTaskQueue() {
        if (m_TaskProcessing.exchange(true)) return; // 防止并发处理

        // 取出最高优先级任务
        AsyncLoadTask task;
        {
            std::lock_guard<std::mutex> lock(m_TaskMutex);
            if (m_TaskQueue.empty()) {
                m_TaskProcessing = false;
                return;
            }
            task = std::move(m_TaskQueue.front());
            m_TaskQueue.pop_front();
        }

        m_CurrentTask = std::move(task);
        AsyncLoadTask& currentTask = *m_CurrentTask;

        m_IsLoading = true;
        m_CurrentLoadingScene = currentTask.sceneName;
        m_LoadStartTimeMs = NowMs();
        m_SmoothProgress.Reset(0.0f);
        m_RawProgress = 0.0f;

        // 保存上下文
        m_CurrentContext = std::make_unique<SceneContext>(std::move(currentTask.context));

        DispatchLifecycle(LifecyclePhase::OnBeforeLoad, currentTask.sceneName);

        // 开始异步加载
        Level* level = m_LevelManager.FindLevel(currentTask.sceneName);
        if (!level) {
            FinalizeSceneLoad(currentTask, SceneManagerError::SceneNotFound);
            m_TaskProcessing = false;
            return;
        }

        // 使用 LevelManager 的异步加载
        m_LevelManager.LoadLevelAsync(currentTask.sceneName,
            [this, &currentTask](bool success) {
                if (!success) {
                    FinalizeSceneLoad(currentTask, SceneManagerError::DeserializeFailed);
                    m_TaskProcessing = false;
                    return;
                }

                DispatchLifecycle(LifecyclePhase::OnResourceLoaded, currentTask.sceneName, 0.8f);
                m_RawProgress = 0.8f;

                // 分帧激活准备就绪
                currentTask.readyToActivate = true;

                // 激活（由 Update 在下一帧处理分帧延迟）
                ENGINE_LOG_INFO("SceneManager", "Async load complete for '{}', waiting for activation",
                                currentTask.sceneName);
            });

        m_TaskProcessing = false;
    }

    void SceneManagerImpl::FinalizeSceneLoad(AsyncLoadTask& task, SceneManagerError error) {
        m_IsLoading = false;

        if (error == SceneManagerError::None) {
            // 记录性能
            task.profile.totalTimeMs = ElapsedMs(
                std::chrono::steady_clock::time_point(std::chrono::milliseconds(task.startTimeMs)));
            task.profile.sceneName = task.sceneName;
            task.profile.timestamp = NowMs();
            task.profile.success = true;

            Level* level = m_LevelManager.FindLevel(task.sceneName);
            if (level && level->HasScene()) {
                task.profile.objectCount = level->GetScene()->GetTotalObjectCount();
            }

            m_LoadProfiles.push_back(task.profile);

            if (task.profile.ExceedsThreshold(m_PerformanceThresholdMs)) {
                ENGINE_LOG_WARN("SceneManager", "Scene '{}' load time {:.1f}ms exceeds threshold {:.0f}ms",
                                task.sceneName, task.profile.totalTimeMs, m_PerformanceThresholdMs);
            }

            ENGINE_LOG_INFO("SceneManager", "Scene '{}' loaded in {:.1f}ms",
                            task.sceneName, task.profile.totalTimeMs);
        } else {
            // 错误处理
            DispatchLifecycle(LifecyclePhase::OnError, task.sceneName, 0.0f,
                              SceneManagerErrorToString(error));
            TriggerRollback(task.sceneName, SceneManagerErrorToString(error));
        }

        if (task.onComplete) {
            task.onComplete(error);
        }

        DispatchLifecycle(LifecyclePhase::OnActive, task.sceneName, 1.0f);
        m_CurrentTask.reset();
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：场景激活
    // ═══════════════════════════════════════════════════════════════

    bool SceneManagerImpl::ActivateLoadedScene(const std::string& sceneName) {
        // 先设置活动关卡（会触发 Level.Activate() → Scene.OnCreate()）
        if (!m_LevelManager.SetActiveLevel(sceneName)) {
            ENGINE_LOG_ERROR("SceneManager", "Failed to activate scene '{}'", sceneName);
            return false;
        }

        DispatchLifecycle(LifecyclePhase::OnSceneInitialized, sceneName, 0.95f);
        ENGINE_LOG_INFO("SceneManager", "Scene '{}' activated", sceneName);
        return true;
    }

    bool SceneManagerImpl::DeactivateCurrentScene() {
        std::string currentName = m_LevelManager.GetActiveLevelName();
        if (currentName.empty()) return true;

        DispatchLifecycle(LifecyclePhase::OnUnloadStart, currentName);
        m_LevelManager.UnloadAllNonPersistent();
        DispatchLifecycle(LifecyclePhase::OnUnloadComplete, currentName);
        return true;
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：回滚机制
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::TriggerRollback(const std::string& failedScene,
                                            const std::string& errorMsg) {
        ENGINE_LOG_ERROR("SceneManager", "ROLLBACK: Scene '{}' failed: {}. Falling back to '{}'",
                         failedScene, errorMsg, m_FallbackScene);

        // 卸载失败场景（如果已部分加载）
        m_LevelManager.UnloadLevel(failedScene);

        // 卸载所有非 Persistent 场景
        m_LevelManager.UnloadAllNonPersistent();

        // 尝试加载回退场景
        if (!m_FallbackScene.empty() && m_FallbackScene != failedScene) {
            Level* fallback = m_LevelManager.FindLevel(m_FallbackScene);
            if (fallback) {
                ENGINE_LOG_INFO("SceneManager", "Loading fallback scene '{}'", m_FallbackScene);
                if (m_LevelManager.LoadLevel(m_FallbackScene)) {
                    m_LevelManager.SetActiveLevel(m_FallbackScene);
                    DispatchLifecycle(LifecyclePhase::OnActive, m_FallbackScene, 1.0f);
                } else {
                    ENGINE_LOG_ERROR("SceneManager", "Fallback scene '{}' also failed to load!",
                                     m_FallbackScene);
                }
            } else {
                ENGINE_LOG_ERROR("SceneManager", "Fallback scene '{}' not registered!",
                                 m_FallbackScene);
            }
        }

        m_IsLoading = false;
        m_CurrentLoadingScene.clear();
        m_SmoothProgress.Reset(0.0f);
        m_RawProgress = 0.0f;
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：进度管理
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::UpdateProgress(float rawProgress) {
        m_RawProgress = rawProgress;
        m_SmoothProgress.SetRawProgress(rawProgress);
    }

    float SceneManagerImpl::CalculateRawProgress() const {
        // 检查当前加载的关卡进度
        if (m_CurrentTask.has_value()) {
            const Level* level = m_LevelManager.FindLevel(m_CurrentTask->sceneName);
            if (level) {
                return level->GetLoadProgress();
            }
        }
        return m_RawProgress;
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：超时检测
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::CheckTimeouts() {
        if (!m_IsLoading || !m_CurrentTask.has_value()) return;

        int64_t elapsed = NowMs() - m_LoadStartTimeMs;
        if (elapsed > static_cast<int64_t>(m_CurrentTask->timeoutMs)) {
            ENGINE_LOG_ERROR("SceneManager", "TIMEOUT: Scene '{}' load exceeded {}ms (actual: {}ms)",
                             m_CurrentTask->sceneName, m_CurrentTask->timeoutMs, elapsed);

            AsyncLoadTask task = std::move(*m_CurrentTask);
            m_CurrentTask.reset();
            FinalizeSceneLoad(task, SceneManagerError::Timeout);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部：预加载轮询
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::PollPreloads() {
        std::lock_guard<std::mutex> lock(m_PreloadMutex);

        m_PreloadHandles.erase(
            std::remove_if(m_PreloadHandles.begin(), m_PreloadHandles.end(),
                           [](const auto& handle) {
                               return handle->GetState() == PreloadState::Completed ||
                                      handle->GetState() == PreloadState::Cancelled ||
                                      handle->GetState() == PreloadState::Failed;
                           }),
            m_PreloadHandles.end());
    }

    // ═══════════════════════════════════════════════════════════════
    // 每帧更新
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerImpl::Update(float32 dt) {
        m_LastDt = dt;

        // 1. 处理任务队列
        if (!m_IsLoading && !m_TaskQueue.empty()) {
            ProcessTaskQueue();
        }

        // 2. 更新进度
        float raw = CalculateRawProgress();
        UpdateProgress(raw);

        // 3. 分发加载中回调
        if (m_IsLoading && m_CurrentTask.has_value()) {
            DispatchLifecycle(LifecyclePhase::OnLoading,
                              m_CurrentTask->sceneName,
                              m_SmoothProgress.GetSmoothed(dt));
        }

        // 4. 分帧激活检测
        if (m_CurrentTask.has_value() && m_CurrentTask->readyToActivate) {
            m_CurrentTask->framesSinceLoad++;
            if (m_CurrentTask->framesSinceLoad >= m_CurrentTask->activationDelayFrames) {
                std::string sceneName = m_CurrentTask->sceneName;
                if (ActivateLoadedScene(sceneName)) {
                    AsyncLoadTask task = std::move(*m_CurrentTask);
                    m_CurrentTask.reset();
                    FinalizeSceneLoad(task, SceneManagerError::None);
                }
            }
        }

        // 5. 超时检测
        CheckTimeouts();

        // 6. 清理已完成预加载
        PollPreloads();

        // 7. 更新底层 LevelManager
        m_LevelManager.Update(dt);
    }

    // ═══════════════════════════════════════════════════════════════
    // Static 指针初始化
    // ═══════════════════════════════════════════════════════════════

    SceneManagerImpl* SceneManager::s_Impl = nullptr;

    // ═══════════════════════════════════════════════════════════════
    // SceneManager Facade 实现（委托到 Impl）
    // ═══════════════════════════════════════════════════════════════

    void SceneManager::Init() {
        if (!s_Impl) {
            s_Impl = new SceneManagerImpl();
        }
        s_Impl->Init();
    }

    void SceneManager::Shutdown() {
        if (s_Impl) {
            s_Impl->Shutdown();
            delete s_Impl;
            s_Impl = nullptr;
        }
    }

    bool SceneManager::IsInitialized() noexcept {
        return s_Impl && s_Impl->IsInitialized();
    }

    void SceneManager::SetLoadTimeoutMs(uint32 timeoutMs) {
        if (s_Impl) s_Impl->SetLoadTimeoutMs(timeoutMs);
    }

    void SceneManager::SetFallbackScene(const std::string& sceneName) {
        if (s_Impl) s_Impl->SetFallbackScene(sceneName);
    }

    const std::string& SceneManager::GetFallbackScene() noexcept {
        static const std::string empty;
        return s_Impl ? s_Impl->GetFallbackScene() : empty;
    }

    void SceneManager::SetPreloadEnabled(bool enabled) {
        if (s_Impl) s_Impl->SetPreloadEnabled(enabled);
    }

    void SceneManager::SetProgressSmoothingFactor(float factor) {
        if (s_Impl) s_Impl->SetProgressSmoothingFactor(factor);
    }

    void SceneManager::SetPerformanceThresholdMs(double thresholdMs) {
        if (s_Impl) s_Impl->SetPerformanceThresholdMs(thresholdMs);
    }

    void SceneManager::RegisterLevel(const LevelInfo& info) {
        if (s_Impl) s_Impl->RegisterLevel(info);
    }

    void SceneManager::RegisterLevels(const std::vector<LevelInfo>& infos) {
        if (s_Impl) s_Impl->RegisterLevels(infos);
    }

    void SceneManager::UnregisterLevel(const std::string& name) {
        if (s_Impl) s_Impl->UnregisterLevel(name);
    }

    Level* SceneManager::FindLevel(const std::string& name) {
        return s_Impl ? s_Impl->FindLevel(name) : nullptr;
    }

    bool SceneManager::LoadGroupConfig(const std::string& filePath) {
        return s_Impl && s_Impl->LoadGroupConfig(filePath);
    }

    bool SceneManager::LoadGroupConfigFromString(const std::string& jsonContent) {
        return s_Impl && s_Impl->LoadGroupConfigFromString(jsonContent);
    }

    const std::vector<SceneGroup>& SceneManager::GetSceneGroups() noexcept {
        static const std::vector<SceneGroup> empty;
        return s_Impl ? s_Impl->GetSceneGroups() : empty;
    }

    SceneManagerError SceneManager::LoadScene(const std::string& sceneName,
                                               SceneContext context) {
        return s_Impl ? s_Impl->LoadScene(sceneName, std::move(context))
                      : SceneManagerError::Unknown;
    }

    void SceneManager::LoadSceneAsync(const std::string& sceneName,
                                       SceneContext context,
                                       std::function<void(SceneManagerError)> onComplete) {
        if (s_Impl) s_Impl->LoadSceneAsync(sceneName, std::move(context), std::move(onComplete));
    }

    SceneManagerError SceneManager::SwitchScene(const std::string& sceneName,
                                                 SceneContext context) {
        return s_Impl ? s_Impl->SwitchScene(sceneName, std::move(context))
                      : SceneManagerError::Unknown;
    }

    void SceneManager::SwitchSceneAsync(const std::string& sceneName,
                                         SceneContext context,
                                         std::function<void(SceneManagerError)> onComplete) {
        if (s_Impl) s_Impl->SwitchSceneAsync(sceneName, std::move(context), std::move(onComplete));
    }

    SceneManagerError SceneManager::UnloadScene(const std::string& sceneName) {
        return s_Impl ? s_Impl->UnloadScene(sceneName) : SceneManagerError::Unknown;
    }

    SceneManagerError SceneManager::LoadSceneAdditive(const std::string& sceneName,
                                                       SceneContext context) {
        return s_Impl ? s_Impl->LoadSceneAdditive(sceneName, std::move(context))
                      : SceneManagerError::Unknown;
    }

    SceneManagerError SceneManager::UnloadAdditiveScene(const std::string& sceneName) {
        return s_Impl ? s_Impl->UnloadAdditiveScene(sceneName) : SceneManagerError::Unknown;
    }

    SceneManagerError SceneManager::LoadSceneGroup(const std::string& groupName,
                                                    SceneContext context) {
        return s_Impl ? s_Impl->LoadSceneGroup(groupName, std::move(context))
                      : SceneManagerError::Unknown;
    }

    void SceneManager::LoadSceneGroupAsync(const std::string& groupName,
                                            SceneContext context,
                                            std::function<void(SceneManagerError)> onComplete) {
        if (s_Impl) s_Impl->LoadSceneGroupAsync(groupName, std::move(context), std::move(onComplete));
    }

    std::shared_ptr<PreloadHandle> SceneManager::PreloadScene(const std::string& sceneName) {
        return s_Impl ? s_Impl->PreloadScene(sceneName) : nullptr;
    }

    void SceneManager::CancelPreload(std::shared_ptr<PreloadHandle> handle) {
        if (s_Impl) s_Impl->CancelPreload(std::move(handle));
    }

    uint64 SceneManager::RegisterLifecycleCallback(SceneLifecycleCallback callback) {
        return s_Impl ? s_Impl->RegisterLifecycleCallback(std::move(callback)) : 0;
    }

    void SceneManager::UnregisterLifecycleCallback(uint64 callbackId) {
        if (s_Impl) s_Impl->UnregisterLifecycleCallback(callbackId);
    }

    Scene* SceneManager::GetActiveScene() noexcept {
        return s_Impl ? s_Impl->GetActiveScene() : nullptr;
    }

    std::string SceneManager::GetActiveSceneName() {
        return s_Impl ? s_Impl->GetActiveSceneName() : "";
    }

    const SceneContext* SceneManager::GetCurrentContext() noexcept {
        return s_Impl ? s_Impl->GetCurrentContext() : nullptr;
    }

    LevelState SceneManager::GetSceneState(const std::string& sceneName) {
        return s_Impl ? s_Impl->GetSceneState(sceneName) : LevelState::Unloaded;
    }

    bool SceneManager::IsSceneLoaded(const std::string& sceneName) {
        return s_Impl && s_Impl->IsSceneLoaded(sceneName);
    }

    bool SceneManager::IsSceneLoading(const std::string& sceneName) {
        return s_Impl && s_Impl->IsSceneLoading(sceneName);
    }

    bool SceneManager::IsLoading() noexcept {
        return s_Impl && s_Impl->IsLoading();
    }

    float SceneManager::GetLoadProgress() noexcept {
        return s_Impl ? s_Impl->GetLoadProgress() : 0.0f;
    }

    float SceneManager::GetRawLoadProgress() noexcept {
        return s_Impl ? s_Impl->GetRawLoadProgress() : 0.0f;
    }

    const std::string& SceneManager::GetCurrentLoadingScene() noexcept {
        static const std::string empty;
        return s_Impl ? s_Impl->GetCurrentLoadingScene() : empty;
    }

    std::vector<std::string> SceneManager::GetLoadedSceneNames() {
        return s_Impl ? s_Impl->GetLoadedSceneNames() : std::vector<std::string>{};
    }

    std::vector<Level*> SceneManager::GetAdditiveLevels() {
        return s_Impl ? s_Impl->GetAdditiveLevels() : std::vector<Level*>{};
    }

    const std::vector<SceneLoadProfile>& SceneManager::GetLoadProfiles() {
        static const std::vector<SceneLoadProfile> empty;
        return s_Impl ? s_Impl->GetLoadProfiles() : empty;
    }

    double SceneManager::GetAverageLoadTimeMs(uint32 lastN) {
        return s_Impl ? s_Impl->GetAverageLoadTimeMs(lastN) : 0.0;
    }

    void SceneManager::ClearLoadProfiles() {
        if (s_Impl) s_Impl->ClearLoadProfiles();
    }

    void SceneManager::Update(float32 dt) {
        if (s_Impl) s_Impl->Update(dt);
    }

    LevelManager& SceneManager::GetLevelManager() {
        // 必须在 Init 后调用
        return s_Impl->GetLevelManager();
    }

} // namespace Engine