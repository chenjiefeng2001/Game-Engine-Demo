#pragma once

/**
 * @file SceneManager.h
 * @brief 场景管理器 — 工业级场景生命周期管理 Facade
 *
 * ======================== 架构设计 ========================
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │                    API 层 (Facade)                       │
 * │   SceneManager — 对外暴露的统一接口                      │
 * │   LoadScene / SwitchScene / UnloadScene / PreloadScene   │
 * ├─────────────────────────────────────────────────────────┤
 * │                逻辑控制层 (Controller)                    │
 * │   - 异步流水线: Task 队列 + 优先级调度                    │
 * │   - 生命周期钩子分发                                     │
 * │   - 进度管理与平滑插值                                   │
 * │   - 分帧激活 (allowSceneActivation = false)              │
 * ├─────────────────────────────────────────────────────────┤
 * │                资源驱动层 (Resource Driver)               │
 * │   - LevelManager (已有) — 关卡状态机管理                 │
 * │   - JsonSerializer — 场景序列化/反序列化                  │
 * │   - 依赖场景自动解析和预加载                              │
 * └─────────────────────────────────────────────────────────┘
 *
 * ======================== 核心能力 ========================
 *
 * 1. 异步加载流水线
 *    - 任务队列 + 优先级排序
 *    - 非阻塞加载（JobSystem 集成）
 *    - 标准 0→1 进度反馈 + 平滑插值
 *
 * 2. 场景叠加与组合
 *    - Additive 加载（主场景 + 多个子场景）
 *    - 场景组 (SceneGroup) 配置 — JSON 定义的关卡分组
 *
 * 3. 生命周期钩子
 *    - 精确的阶段回调：OnBeforeLoad → OnLoading → OnResourceLoaded
 *      → OnSceneInitialized → OnActive
 *    - 外部系统（UI、音效、分析）可通过回调注册同步
 *
 * 4. 数据持久化与传递
 *    - SceneContext 上下文对象（替代全局变量）
 *
 * 5. 预加载 (Preloading)
 *    - 根据玩家位置/逻辑预判，后台静默加载
 *    - PreloadHandle 令牌控制生命周期
 *
 * 6. 错误处理与回滚
 *    - 加载失败时自动回退到 Fallback 场景
 *    - 超时检测
 *
 * 7. 性能监控
 *    - SceneLoadProfile 记录各阶段耗时
 *    - 阈值超限告警
 *
 * ======================== 使用示例 ========================
 *
 * @code
 *   // 初始化
 *   SceneManager::Init();
 *
 *   // 注册场景
 *   SceneManager::RegisterLevel({"MainMenu", "assets/scenes/menu.json"});
 *   SceneManager::RegisterLevel({"Battle",   "assets/scenes/battle.json"});
 *
 *   // 设置回退场景（加载失败时自动跳转）
 *   SceneManager::SetFallbackScene("MainMenu");
 *
 *   // 加载场景组配置
 *   SceneManager::LoadGroupConfig("assets/scenes/groups.json");
 *
 *   // 简单场景加载（异步 + 自动显示加载界面）
 *   SceneManager::LoadScene("Battle");
 *
 *   // 带参数切换场景
 *   auto ctx = SceneContext::Create("Battle")
 *       .WithArg("difficulty", 3)
 *       .WithArg("playerHealth", 100.0f);
 *   SceneManager::SwitchScene("Battle", std::move(ctx));
 *
 *   // 注册生命周期钩子
 *   SceneManager::RegisterLifecycleCallback(
 *       [](const auto& name, auto phase, float progress, const auto& msg) {
 *           if (phase == LifecyclePhase::OnLoading)
 *               UpdateLoadingBar(progress);
 *       });
 *
 *   // 预加载（后台静默）
 *   auto handle = SceneManager::PreloadScene("Battle");
 *
 *   // 每帧更新
 *   SceneManager::Update(dt);
 *
 *   // 卸载
 *   SceneManager::UnloadScene("Battle");
 *
 *   // 获取性能数据
 *   auto profiles = SceneManager::GetLoadProfiles();
 *   for (auto& p : profiles)
 *       LogInfo("Scene '{}' loaded in {:.1f}ms", p.sceneName, p.totalTimeMs);
 *
 *   // 关闭
 *   SceneManager::Shutdown();
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>

namespace Engine {

    // ──────────────────────────────────────────────
    // 前向声明
    // ──────────────────────────────────────────────
    class SceneManagerImpl;

    // ═══════════════════════════════════════════════════════════════
    // SceneManager — 场景管理器 Facade
    // ═══════════════════════════════════════════════════════════════
    class SceneManager {
    public:
        // ═══════════════════════════════════════════════════════════
        // 初始化 & 关闭
        // ═══════════════════════════════════════════════════════════

        /** 初始化场景管理器（必须在首次使用前调用） */
        static void Init();

        /** 关闭场景管理器，卸载所有场景 */
        static void Shutdown();

        /** 检查是否已初始化 */
        static bool IsInitialized() noexcept;

        // ═══════════════════════════════════════════════════════════
        // 配置
        // ═══════════════════════════════════════════════════════════

        /** 设置加载超时时间（毫秒，默认 10000ms） */
        static void SetLoadTimeoutMs(uint32 timeoutMs);

        /** 设置回退场景（加载失败时自动跳转） */
        static void SetFallbackScene(const std::string& sceneName);

        /** 获取回退场景名称 */
        static const std::string& GetFallbackScene() noexcept;

        /** 设置是否启用预加载（默认 true） */
        static void SetPreloadEnabled(bool enabled);

        /** 设置进度平滑因子 [0,1]（默认 0.15） */
        static void SetProgressSmoothingFactor(float factor);

        /** 设置超时阈值（毫秒，超过此值记录告警日志） */
        static void SetPerformanceThresholdMs(double thresholdMs);

        // ═══════════════════════════════════════════════════════════
        // 场景注册
        // ═══════════════════════════════════════════════════════════

        /** 注册一个关卡信息 */
        static void RegisterLevel(const LevelInfo& info);

        /** 批量注册关卡 */
        static void RegisterLevels(const std::vector<LevelInfo>& infos);

        /** 注销关卡 */
        static void UnregisterLevel(const std::string& name);

        /** 查找关卡 */
        static Level* FindLevel(const std::string& name);

        // ═══════════════════════════════════════════════════════════
        // 场景组配置
        // ═══════════════════════════════════════════════════════════

        /** 从 JSON 文件加载场景组配置 */
        static bool LoadGroupConfig(const std::string& filePath);

        /** 从 JSON 字符串加载场景组配置 */
        static bool LoadGroupConfigFromString(const std::string& jsonContent);

        /** 获取所有场景组 */
        static const std::vector<SceneGroup>& GetSceneGroups() noexcept;

        // ═══════════════════════════════════════════════════════════
        // 核心 API — 场景加载/切换/卸载
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 同步加载场景（阻塞直到完成）
         * @param sceneName 场景名称
         * @param context   可选上下文参数
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError LoadScene(const std::string& sceneName,
                                            SceneContext context = SceneContext{});

        /**
         * @brief 异步加载场景（非阻塞，加载完成自动激活）
         * @param sceneName  场景名称
         * @param context    可选上下文参数
         * @param onComplete 完成回调 (SceneManagerError error)
         */
        static void LoadSceneAsync(const std::string& sceneName,
                                    SceneContext context = SceneContext{},
                                    std::function<void(SceneManagerError)> onComplete = nullptr);

        /**
         * @brief 切换场景（卸载当前活动场景 → 加载新场景）
         * @param sceneName 目标场景名称
         * @param context   可选上下文参数
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError SwitchScene(const std::string& sceneName,
                                              SceneContext context = SceneContext{});

        /**
         * @brief 异步切换场景
         * @param sceneName  目标场景名称
         * @param context    可选上下文参数
         * @param onComplete 完成回调
         */
        static void SwitchSceneAsync(const std::string& sceneName,
                                      SceneContext context = SceneContext{},
                                      std::function<void(SceneManagerError)> onComplete = nullptr);

        /**
         * @brief 卸载场景
         * @param sceneName 场景名称
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError UnloadScene(const std::string& sceneName);

        /**
         * @brief 叠加加载场景（不卸载现有场景）
         * @param sceneName 场景名称
         * @param context   可选上下文
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError LoadSceneAdditive(const std::string& sceneName,
                                                    SceneContext context = SceneContext{});

        /**
         * @brief 卸载叠加场景
         * @param sceneName 场景名称
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError UnloadAdditiveScene(const std::string& sceneName);

        /**
         * @brief 加载场景组（一组关联场景同时加载）
         * @param groupName 场景组名称
         * @param context   可选上下文
         * @return SceneManagerError::None 表示成功
         */
        static SceneManagerError LoadSceneGroup(const std::string& groupName,
                                                 SceneContext context = SceneContext{});

        /**
         * @brief 异步加载场景组
         * @param groupName  场景组名称
         * @param context    可选上下文
         * @param onComplete 完成回调
         */
        static void LoadSceneGroupAsync(const std::string& groupName,
                                         SceneContext context = SceneContext{},
                                         std::function<void(SceneManagerError)> onComplete = nullptr);

        // ═══════════════════════════════════════════════════════════
        // 预加载系统
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 预加载场景（后台静默加载资源）
         * @param sceneName 场景名称
         * @return 预加载令牌（可查询状态/取消）
         */
        static std::shared_ptr<PreloadHandle> PreloadScene(const std::string& sceneName);

        /**
         * @brief 取消预加载
         * @param handle 预加载令牌
         */
        static void CancelPreload(std::shared_ptr<PreloadHandle> handle);

        // ═══════════════════════════════════════════════════════════
        // 生命周期钩子注册
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 注册全局生命周期回调
         * @param callback 回调函数
         * @return 回调 ID（可用于取消注册）
         */
        static uint64 RegisterLifecycleCallback(SceneLifecycleCallback callback);

        /** 注销生命周期回调 */
        static void UnregisterLifecycleCallback(uint64 callbackId);

        // ═══════════════════════════════════════════════════════════
        // 场景状态查询
        // ═══════════════════════════════════════════════════════════

        /** 获取当前活动场景 */
        static Scene* GetActiveScene() noexcept;

        /** 获取当前活动场景名称 */
        static std::string GetActiveSceneName();

        /** 获取当前上下文（只读） */
        static const SceneContext* GetCurrentContext() noexcept;

        /** 获取场景状态 */
        static LevelState GetSceneState(const std::string& sceneName);

        /** 检查场景是否已加载 */
        static bool IsSceneLoaded(const std::string& sceneName);

        /** 检查场景是否正在加载 */
        static bool IsSceneLoading(const std::string& sceneName);

        /** 检查是否正在加载中 */
        static bool IsLoading() noexcept;

        /** 获取当前加载进度（平滑后的值，0~1） */
        static float GetLoadProgress() noexcept;

        /** 获取当前加载进度（原始值，0~1） */
        static float GetRawLoadProgress() noexcept;

        /** 获取当前加载场景名称 */
        static const std::string& GetCurrentLoadingScene() noexcept;

        /** 获取所有已加载的场景名称列表 */
        static std::vector<std::string> GetLoadedSceneNames();

        /** 获取所有叠加场景 */
        static std::vector<Level*> GetAdditiveLevels();

        // ═══════════════════════════════════════════════════════════
        // 性能监控
        // ═══════════════════════════════════════════════════════════

        /** 获取所有场景加载性能剖面 */
        static const std::vector<SceneLoadProfile>& GetLoadProfiles();

        /** 获取最后 N 次加载的平均耗时(ms) */
        static double GetAverageLoadTimeMs(uint32 lastN = 5);

        /** 清除性能数据 */
        static void ClearLoadProfiles();

        // ═══════════════════════════════════════════════════════════
        // 每帧更新（由 Application 调用）
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 每帧更新
         *
         * 执行以下操作：
         *   1. 检测是否有需要激活的预加载场景
         *   2. 处理异步加载任务的进度轮询
         *   3. 更新平滑进度
         *   4. 超时检测
         *   5. 分帧激活计数器
         *   6. 触发生命周期钩子
         *
         * @param dt 时间步长(秒)
         */
        static void Update(float32 dt);

        /** 获取底层 LevelManager 引用（高级操作） */
        static LevelManager& GetLevelManager();

    private:
        /** PIMPL — 隐藏实现细节 */
        static SceneManagerImpl* s_Impl;
    };

} // namespace Engine