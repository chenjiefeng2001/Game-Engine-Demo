#pragma once

/**
 * @file Level.h
 * @brief 关卡定义 — Scene 的封装 + 元数据 + 生命周期状态机
 *
 * Level 是 Scene 的上层容器，在 Scene 的基础上添加了：
 *   - 加载/卸载状态机（Unloaded → Loading → Loaded → Active → Unloading）
 *   - 关卡元数据（缩略图、依赖、分类）
 *   - 异步加载支持
 *   - Additive 叠加渲染支持
 *
 * 与 Scene 的关系：
 *   Level 持有 Scene，一个 Level 对应一个 Scene 文件。
 *   LevelManager 管理多个 Level 的生命周期。
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace Engine {

    // ──────────────────────────────────────────────
    // 关卡状态枚举
    // ──────────────────────────────────────────────
    enum class LevelState : uint8 {
        Unloaded,       ///< 未加载（初始状态）
        Loading,        ///< 正在加载（异步加载中）
        Loaded,         ///< 已加载但未激活
        Activating,     ///< 正在激活（调用 OnActivate 中）
        Active,         ///< 激活（每帧 Update/Render）
        Deactivating,   ///< 正在卸载（调用 OnDeactivate 中）
        Unloading,      ///< 正在释放资源
        Failed          ///< 加载失败
    };

    inline const char* LevelStateToString(LevelState state) {
        switch (state) {
            case LevelState::Unloaded:     return "Unloaded";
            case LevelState::Loading:      return "Loading";
            case LevelState::Loaded:       return "Loaded";
            case LevelState::Activating:   return "Activating";
            case LevelState::Active:       return "Active";
            case LevelState::Deactivating: return "Deactivating";
            case LevelState::Unloading:    return "Unloading";
            case LevelState::Failed:       return "Failed";
            default:                       return "Unknown";
        }
    }

    // ──────────────────────────────────────────────
    // 关卡分类枚举
    // ──────────────────────────────────────────────
    enum class LevelCategory : uint8 {
        Persistent,    ///< 常驻关卡（UI / 全局光照 / 管理器）
        Main,          ///< 主关卡（逻辑场景）
        SubLevel,      ///< 子关卡（叠加场景 / 室内区域）
        Streaming,     ///< 流式关卡（按距离动态加载）
        Cinematic,     ///< 过场动画关卡
        Debug          ///< 调试用关卡
    };

    // ──────────────────────────────────────────────
    // 关卡信息结构（纯数据，可序列化）
    // ──────────────────────────────────────────────
    struct LevelInfo {
        std::string name;                     ///< 关卡显示名称（唯一标识）
        std::string filePath;                 ///< 场景文件路径 (.scene 或 .json)
        LevelCategory category = LevelCategory::Main;
        bool isPersistent = false;            ///< 是否常驻（卸载其他关卡时保留）
        int32 loadPriority = 0;               ///< 加载优先级（越大越优先）
        float boundingRadius = 0.0f;          ///< 影响半径（Streaming 模式下用）
        std::vector<std::string> dependencies; ///< 依赖关卡名称列表

        // 元数据（编辑器显示用）
        std::string thumbnailPath;            ///< 缩略图路径
        std::string description;              ///< 关卡描述
    };

    // ──────────────────────────────────────────────
    // 关卡（Level）
    // ──────────────────────────────────────────────
    class Level {
    public:
        explicit Level(LevelInfo info);
        ~Level();

        Level(const Level&) = delete;
        Level& operator=(const Level&) = delete;

        // ── 访问器 ──
        const std::string& GetName() const { return m_Info.name; }
        const LevelInfo& GetInfo() const { return m_Info; }
        LevelState GetState() const { return m_State.load(); }
        const char* GetStateString() const { return LevelStateToString(m_State); }

        // ── 场景访问 ──
        Scene* GetScene() { return m_Scene.get(); }
        const Scene* GetScene() const { return m_Scene.get(); }
        bool HasScene() const { return m_Scene != nullptr; }

        // ── 生命周期（由 LevelManager 调用） ──

        /** 同步加载关卡（阻塞直到完成） */
        bool Load();

        /** 开始异步加载（返回后后台线程继续） */
        void LoadAsync(std::function<void(bool)> onComplete = nullptr);

        /** 激活关卡（调用 Scene::OnCreate） */
        bool Activate();

        /** 取消激活（调用 Scene::OnDestroy + 释放资源） */
        void Deactivate();

        /** 卸载关卡（释放 GPU 资源） */
        void Unload();

        /** 获取异步加载进度（0.0 ~ 1.0） */
        float GetLoadProgress() const { return m_LoadProgress.load(); }

        /** 检查是否加载完成并可以激活 */
        bool IsReadyToActivate() const {
            return m_State.load() == LevelState::Loaded;
        }

        // ── 每帧更新（仅在 Active 状态时由 LevelManager 调用） ──
        void Update(float32 dt);
        void Render();
        void PostPhysicsUpdate();

    private:
        /** 状态转换（线程安全） */
        void SetState(LevelState newState);

        LevelInfo                m_Info;
        std::atomic<LevelState>  m_State{LevelState::Unloaded};
        std::atomic<float>       m_LoadProgress{0.0f};

        std::unique_ptr<Scene>   m_Scene;

        // 异步加载支持
        std::function<void(bool)> m_OnLoadComplete;
    };

} // namespace Engine