#pragma once

/**
 * @file LevelManager.h
 * @brief 关卡管理器 — 统一管理所有 Level 的生命周期
 *
 * 核心职责：
 *   1. 关卡注册表管理（注册/注销/查找 Level）
 *   2. 关卡加载/切换/卸载（同步 + 异步）
 *   3. Additive 关卡叠加渲染
 *   4. 编辑器 Play/Stop 快照集成
 *
 * 使用方式：
 * @code
 *   LevelManager mgr;
 *   mgr.RegisterLevel({"Main", "assets/scenes/main.scene"});
 *   mgr.LoadLevel("Main");           // 同步加载
 *   mgr.SetActiveLevel("Main");      // 激活
 *   // ...
 *   mgr.UnloadLevel("Main");
 * @endcode
 *
 * 架构关系：
 *   EngineEditor -> LevelManager (关卡管理)
 *                -> EditorSceneManager (Play/Stop 快照控制)
 *                -> Scene (当前活动场景数据)
 */

#include "Engine/Types.h"
#include "Engine/Core/Level/Level.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace Engine {

    class LevelManager {
    public:
        LevelManager() = default;
        ~LevelManager();

        LevelManager(const LevelManager&) = delete;
        LevelManager& operator=(const LevelManager&) = delete;

        // ═══════════════════════════════════════════════════════════
        // 关卡注册表管理
        // ═══════════════════════════════════════════════════════════

        /** 注册一个关卡信息 */
        void RegisterLevel(const LevelInfo& info);

        /** 批量注册关卡 */
        void RegisterLevels(const std::vector<LevelInfo>& infos);

        /** 注销关卡 */
        void UnregisterLevel(const std::string& name);

        /** 查找关卡 */
        Level* FindLevel(const std::string& name);
        const Level* FindLevel(const std::string& name) const;

        /** 获取所有已注册的关卡信息 */
        const std::unordered_map<std::string, std::unique_ptr<Level>>& GetAllLevels() const {
            return m_Levels;
        }

        /** 获取所有关卡信息列表（非拥有） */
        std::vector<Level*> GetLevelList();

        /** 获取关卡数量 */
        size_t GetLevelCount() const { return m_Levels.size(); }

        // ═══════════════════════════════════════════════════════════
        // 关卡加载/卸载（同步）
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 同步加载一个关卡
         * @param name 关卡名称（必须在 RegisterLevel 中注册过）
         * @return true 加载成功
         */
        bool LoadLevel(const std::string& name);

        /**
         * @brief 同步卸载一个关卡
         * @param name 关卡名称
         */
        void UnloadLevel(const std::string& name);

        /**
         * @brief 卸载所有非 Persist 关卡
         * @param exceptName 例外关卡（可选，保留该关卡不卸载）
         */
        void UnloadAllNonPersistent(const std::string& exceptName = "");

        // ═══════════════════════════════════════════════════════════
        // 关卡激活/切换
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 设置活动关卡（自动取消激活当前活动关卡）
         * @param name 关卡名称
         * @return true 激活成功
         */
        bool SetActiveLevel(const std::string& name);

        /** 获取当前活动关卡 */
        Level* GetActiveLevel() { return m_ActiveLevel; }
        const Level* GetActiveLevel() const { return m_ActiveLevel; }

        /** 当前活动关卡名称（空字符串若无） */
        std::string GetActiveLevelName() const;

        // ═══════════════════════════════════════════════════════════
        // 关卡叠加（Additive）
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 在现有关卡之上叠加加载一个关卡
         * @param name 关卡名称
         * @return true 加载成功
         */
        bool LoadLevelAdditive(const std::string& name);

        /** 获取所有已加载的叠加关卡列表 */
        std::vector<Level*> GetAdditiveLevels() const;

        // ═══════════════════════════════════════════════════════════
        // 异步加载
        // ═══════════════════════════════════════════════════════════

        /**
         * @brief 异步加载关卡
         * @param name 关卡名称
         * @param onComplete 完成回调 (bool success)
         */
        void LoadLevelAsync(const std::string& name,
                            std::function<void(bool)> onComplete = nullptr);

        /** 获取当前异步加载的进度（0.0 ~ 1.0） */
        float GetLoadProgress() const;

        /** 检查是否正在异步加载中 */
        bool IsLoading() const { return m_IsLoading; }

        // ═══════════════════════════════════════════════════════════
        // 每帧更新（由 Application/EngineEditor 调用）
        // ═══════════════════════════════════════════════════════════

        /** 更新所有 Active 状态的关卡 */
        void Update(float32 dt);

        /** 装饰所有 Active 状态的关卡 */
        void Render();

        /** 物理同步（所有 Active 关卡） */
        void PostPhysicsUpdate();

        // ═══════════════════════════════════════════════════════════
        // 序列化
        // ═══════════════════════════════════════════════════════════

        /** 保存关卡注册表到 JSON 文件 */
        bool SaveRegistry(const std::string& filePath) const;

        /** 从 JSON 文件加载关卡注册表 */
        bool LoadRegistry(const std::string& filePath);

        // ═══════════════════════════════════════════════════════════
        // 编辑器集成（保留 EditorSceneManager 风格的 Play/Stop）
        // ═══════════════════════════════════════════════════════════

        using StateChangeCallback = std::function<void(const std::string& levelName)>;
        void SetLevelLoadedCallback(StateChangeCallback cb)   { m_OnLevelLoaded = std::move(cb); }
        void SetLevelUnloadedCallback(StateChangeCallback cb) { m_OnLevelUnloaded = std::move(cb); }
        void SetLevelActivatedCallback(StateChangeCallback cb){ m_OnLevelActivated = std::move(cb); }

    private:
        /** 内部：获取或创建 Level 实例 */
        Level* GetOrCreateLevel(const std::string& name);

        std::unordered_map<std::string, std::unique_ptr<Level>> m_Levels;

        // 活动关卡
        Level* m_ActiveLevel = nullptr;

        // 叠加关卡
        std::vector<Level*> m_AdditiveLevels;

        // 异步加载状态
        bool m_IsLoading = false;
        float m_LoadProgress = 0.0f;

        // 回调
        StateChangeCallback m_OnLevelLoaded;
        StateChangeCallback m_OnLevelUnloaded;
        StateChangeCallback m_OnLevelActivated;
    };

} // namespace Engine