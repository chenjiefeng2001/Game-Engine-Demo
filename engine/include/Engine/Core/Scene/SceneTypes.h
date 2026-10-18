#pragma once

/**
 * @file SceneTypes.h
 * @brief 场景管理器扩展类型定义 — 场景组、生命周期钩子、预加载、性能埋点等
 *
 * 本文件是工业级场景管理器的核心类型定义，包括：
 *   - SceneGroup / SceneGroupConfig（场景组 & 配置文件）
 *   - LifecycleHook 阶段枚举
 *   - SceneChangeEvent（场景变更事件数据）
 *   - PreloadHandle（预加载令牌）
 *   - SceneLoadProfile（场景加载性能数据）
 *   - SceneManagerErrorCode（容错错误码）
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 1. 生命周期钩子阶段
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 精确的生命周期钩子阶段
     *
     * 每个阶段对应一个回调，供外部系统（音效、UI、分析）同步响应。
     * 回调签名：void(Scene& scene, const SceneContext* context)
     */
    enum class LifecyclePhase : uint8 {
        OnBeforeLoad,           ///< 开始加载前（旧场景尚未清理）
        OnPreloadStart,         ///< 预加载开始（后台静默加载）
        OnPreloadComplete,      ///< 预加载完成
        OnLoading,              ///< 加载中（携带标准化进度值 0.0~1.0）
        OnResourceLoaded,       ///< 资源加载完成（对象未实例化）
        OnSceneInitialized,     ///< 场景脚本 Initialize/Awake 已执行完毕
        OnActive,               ///< 场景完全可见，Loading 界面消失
        OnUnloadStart,          ///< 卸载开始
        OnUnloadComplete,       ///< 卸载完成
        OnError,                ///< 加载/卸载发生错误
        COUNT
    };

    inline const char* LifecyclePhaseToString(LifecyclePhase phase) {
        switch (phase) {
            case LifecyclePhase::OnBeforeLoad:       return "OnBeforeLoad";
            case LifecyclePhase::OnPreloadStart:     return "OnPreloadStart";
            case LifecyclePhase::OnPreloadComplete:  return "OnPreloadComplete";
            case LifecyclePhase::OnLoading:          return "OnLoading";
            case LifecyclePhase::OnResourceLoaded:   return "OnResourceLoaded";
            case LifecyclePhase::OnSceneInitialized: return "OnSceneInitialized";
            case LifecyclePhase::OnActive:           return "OnActive";
            case LifecyclePhase::OnUnloadStart:      return "OnUnloadStart";
            case LifecyclePhase::OnUnloadComplete:   return "OnUnloadComplete";
            case LifecyclePhase::OnError:            return "OnError";
            default:                                 return "Unknown";
        }
    }

    using SceneLifecycleCallback = std::function<void(const std::string& sceneName,
                                                       LifecyclePhase phase,
                                                       float progress,
                                                       const std::string& message)>;

    // ═══════════════════════════════════════════════════════════════
    // 2. 场景变更事件
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 场景变更事件类型
     */
    enum class SceneChangeType : uint8 {
        Load,               ///< 加载新场景
        Unload,             ///< 卸载场景
        Switch,             ///< 切换场景（卸载当前 + 加载新场景）
        LoadAdditive,       ///< 叠加加载
        Preload,            ///< 预加载（后台）
        Activate,           ///< 激活
        Deactivate,         ///< 取消激活
        Rollback,           ///< 回滚
    };

    inline const char* SceneChangeTypeToString(SceneChangeType type) {
        switch (type) {
            case SceneChangeType::Load:         return "Load";
            case SceneChangeType::Unload:       return "Unload";
            case SceneChangeType::Switch:       return "Switch";
            case SceneChangeType::LoadAdditive: return "LoadAdditive";
            case SceneChangeType::Preload:      return "Preload";
            case SceneChangeType::Activate:     return "Activate";
            case SceneChangeType::Deactivate:   return "Deactivate";
            case SceneChangeType::Rollback:     return "Rollback";
            default:                            return "Unknown";
        }
    }

    /**
     * @brief 场景变更事件数据结构
     *
     * 可用于事件总线广播、日志记录、UI 响应。
     */
    struct SceneChangeEvent {
        SceneChangeType type;               ///< 变更类型
        std::string     sourceScene;        ///< 源场景名称（切换/叠加时）
        std::string     targetScene;        ///< 目标场景名称
        bool            success     = true; ///< 是否成功
        float           durationMs  = 0.0f; ///< 操作耗时(ms)
        std::string     errorMessage;       ///< 错误信息（失败时）
        int64_t         timestamp;          ///< 时间戳(ms)
    };

    // ═══════════════════════════════════════════════════════════════
    // 3. 场景组（Scene Group）配置
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 场景组中的单个场景条目
     */
    struct SceneGroupEntry {
        std::string sceneName;          ///< 场景名称（对应注册的 Level 名称）
        bool        required     = true; ///< 是否为必需场景（false 表示可选）
        bool        loadAsync    = true; ///< 是否异步加载
        int32       loadPriority = 0;    ///< 加载优先级
    };

    /**
     * @brief 场景组（Scene Group）
     *
     * 描述进入"关卡 A"时需要同时加载哪几个场景。
     * 例如：{主关卡, 环境场景, NPC 场景, 静态光照场景}
     */
    struct SceneGroup {
        std::string     groupName;          ///< 组名（唯一标识）
        std::string     masterScene;        ///< 主场景名称
        std::vector<SceneGroupEntry> subScenes; ///< 子场景列表

        // 组元数据
        std::string description;            ///< 描述
        float       boundingRadius = 0.0f;  ///< 影响半径（流式加载用）
        bool        isStreaming    = false; ///< 是否为流式加载组
    };

    /**
     * @brief 场景组配置文件定义
     *
     * 对应 JSON 文件的 schema，用于定义关卡分组。
     */
    struct SceneGroupConfig {
        std::vector<SceneGroup> groups;

        /** 从 JSON 字符串加载配置 */
        static SceneGroupConfig LoadFromJson(const std::string& jsonContent);

        /** 从 JSON 文件加载配置 */
        static SceneGroupConfig LoadFromFile(const std::string& filePath);

        /** 序列化为 JSON 字符串 */
        std::string ToJson() const;

        /** 按名称查找场景组 */
        const SceneGroup* FindGroup(const std::string& groupName) const;

        /** 获取某个场景所属的所有组 */
        std::vector<const SceneGroup*> FindGroupsForScene(const std::string& sceneName) const;
    };

    // ═══════════════════════════════════════════════════════════════
    // 4. 预加载令牌（PreloadHandle）
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 预加载状态
     */
    enum class PreloadState : uint8 {
        Idle,           ///< 未开始
        InProgress,     ///< 进行中
        Completed,      ///< 已完成
        Cancelled,      ///< 已取消
        Failed          ///< 失败
    };

    /**
     * @brief 预加载令牌 — 控制预加载的生命周期
     *
     * 通过 PreloadHandle 可以查询预加载状态、取消预加载。
     * 由 SceneManager 创建并返回给调用者。
     */
    class PreloadHandle {
    public:
        explicit PreloadHandle(uint64 id) : m_ID(id) {}

        uint64 GetID() const noexcept { return m_ID; }

        PreloadState GetState() const noexcept { return m_State.load(); }
        float GetProgress() const noexcept { return m_Progress.load(); }

        bool IsCompleted() const noexcept {
            return m_State.load() == PreloadState::Completed;
        }

        bool IsFailed() const noexcept {
            return m_State.load() == PreloadState::Failed;
        }

        /** 取消预加载 */
        void Cancel() {
            PreloadState expected = PreloadState::InProgress;
            m_State.compare_exchange_strong(expected, PreloadState::Cancelled);
        }

        // ── 内部设置方法（SceneManagerImpl 需要访问） ──
        void SetState(PreloadState state) { m_State.store(state); }
        void SetProgress(float progress) { m_Progress.store(progress); }
        void SetTargetScene(const std::string& scene) { m_TargetScene = scene; }
        const std::string& GetTargetScene() const noexcept { return m_TargetScene; }

    private:
        friend class SceneManagerImpl;

        uint64                       m_ID;
        std::atomic<PreloadState>    m_State{PreloadState::Idle};
        std::atomic<float>           m_Progress{0.0f};
        std::string                  m_TargetScene;
    };

    // ═══════════════════════════════════════════════════════════════
    // 5. 性能监控 — 场景加载性能数据
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 场景加载性能剖面
     *
     * 每次场景加载后记录，用于性能分析、阈值告警。
     */
    struct SceneLoadProfile {
        std::string sceneName;

        // 各阶段耗时(ms)
        double ioTimeMs          = 0.0;  ///< I/O 时间（文件读取）
        double deserializeTimeMs = 0.0;  ///< 反序列化时间
        double resourceLoadMs    = 0.0;  ///< 资源加载时间（纹理、模型等）
        double instantiateTimeMs = 0.0;  ///< 对象实例化时间
        double initTimeMs        = 0.0;  ///< 初始化时间（Awake/Start）
        double totalTimeMs       = 0.0;  ///< 总耗时

        // 资源统计
        size_t objectCount          = 0;  ///< 场景对象数量
        size_t componentCount       = 0;  ///< 组件数量
        size_t textureCount         = 0;  ///< 纹理数量
        size_t meshCount            = 0;  ///< 网格数量
        size_t totalMemoryBytes     = 0;  ///< 总内存占用(bytes)

        // 元数据
        int64_t timestamp;                ///< 加载时间戳
        bool    success          = true;  ///< 是否成功
        bool    fromCache        = false; ///< 是否来自缓存

        /** 检查是否超过阈值 */
        bool ExceedsThreshold(double thresholdMs) const {
            return totalTimeMs > thresholdMs;
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // 6. 容错机制 — 错误码 & 回滚策略
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 场景管理器错误码
     */
    enum class SceneManagerError : uint8 {
        None,                   ///< 无错误
        SceneNotFound,          ///< 场景未注册
        FileNotFound,           ///< 场景文件未找到
        FileCorrupted,          ///< 场景文件损坏
        DeserializeFailed,      ///< 反序列化失败
        ResourceLoadFailed,     ///< 资源加载失败
        OutOfMemory,            ///< 内存不足
        AlreadyLoading,         ///< 正在加载中（防止并发加载冲突）
        AlreadyLoaded,          ///< 场景已加载
        DependencyFailed,       ///< 依赖场景加载失败
        GroupLoadFailed,        ///< 场景组加载失败
        ActivationFailed,       ///< 激活失败
        Timeout,                ///< 加载超时
        Unknown                 ///< 未知错误
    };

    inline const char* SceneManagerErrorToString(SceneManagerError error) {
        switch (error) {
            case SceneManagerError::None:               return "None";
            case SceneManagerError::SceneNotFound:      return "SceneNotFound";
            case SceneManagerError::FileNotFound:       return "FileNotFound";
            case SceneManagerError::FileCorrupted:      return "FileCorrupted";
            case SceneManagerError::DeserializeFailed:  return "DeserializeFailed";
            case SceneManagerError::ResourceLoadFailed: return "ResourceLoadFailed";
            case SceneManagerError::OutOfMemory:        return "OutOfMemory";
            case SceneManagerError::AlreadyLoading:     return "AlreadyLoading";
            case SceneManagerError::AlreadyLoaded:      return "AlreadyLoaded";
            case SceneManagerError::DependencyFailed:   return "DependencyFailed";
            case SceneManagerError::GroupLoadFailed:    return "GroupLoadFailed";
            case SceneManagerError::ActivationFailed:   return "ActivationFailed";
            case SceneManagerError::Timeout:            return "Timeout";
            case SceneManagerError::Unknown:            return "Unknown";
            default:                                    return "Unknown";
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 7. 加载优先级枚举（Task 排序用）
    // ═══════════════════════════════════════════════════════════════

    enum class LoadPriority : int32 {
        Critical    = 100,  ///< 关键场景（主场景切换）
        High        = 75,   ///< 高优先级（叠加 UI 场景）
        Normal      = 50,   ///< 正常
        Low         = 25,   ///< 低优先级（预加载）
        Background  = 0,    ///< 后台静默加载
    };

    // ═══════════════════════════════════════════════════════════════
    // 8. 平滑插值进度计算器
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 平滑进度插值器 — 防止进度条跳变
     *
     * 使用指数移动平均（EMA）平滑实际进度值，
     * 使得 UI 进度条变化流畅、不突兀。
     */
    class SmoothProgress {
    public:
        SmoothProgress() = default;

        /**
         * @param smoothingFactor 平滑因子 [0,1]，越大响应越快（默认 0.15）
         */
        explicit SmoothProgress(float smoothingFactor)
            : m_SmoothingFactor(smoothingFactor) {}

        /** 设置原始进度值（由加载系统传入） */
        void SetRawProgress(float raw) noexcept {
            m_RawProgress = std::clamp(raw, 0.0f, 1.0f);
        }

        /** 获取平滑后的进度值（每帧调用一次） */
        float GetSmoothed(float dt) noexcept {
            if (m_RawProgress > m_SmoothedProgress) {
                // 上升：使用平滑因子追赶
                float step = (m_RawProgress - m_SmoothedProgress) * m_SmoothingFactor;
                step = std::max(step, m_MinStep * dt);  // 保证最小速率
                m_SmoothedProgress = std::min(m_RawProgress, m_SmoothedProgress + step);
            } else if (m_RawProgress < m_SmoothedProgress) {
                // 下降：快速跟随（进度不会倒流）
                m_SmoothedProgress = m_RawProgress;
            }
            return m_SmoothedProgress;
        }

        /** 强制设置平滑值（重新开始时调用） */
        void Reset(float progress = 0.0f) noexcept {
            m_RawProgress       = progress;
            m_SmoothedProgress  = progress;
        }

        /** 检查是否完成 */
        bool IsComplete() const noexcept {
            return m_SmoothedProgress >= 1.0f;
        }

        // ── 配置 ──
        void SetSmoothingFactor(float factor) noexcept { m_SmoothingFactor = factor; }
        void SetMinStep(float step) noexcept { m_MinStep = step; }

    private:
        float m_RawProgress         = 0.0f;
        float m_SmoothedProgress    = 0.0f;
        float m_SmoothingFactor     = 0.15f;  // EMA 因子
        float m_MinStep             = 0.2f;   // 最小上升速率(每秒)
    };

} // namespace Engine