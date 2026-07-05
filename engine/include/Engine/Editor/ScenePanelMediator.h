#pragma once

/**
 * @file ScenePanelMediator.h
 * @brief 场景面板中介者 — 协调 SceneHierarchyPanel / SceneViewerPanel / SceneManagerPanel 之间的通信
 *
 * 参考架构：Unity Editor 中 Hierarchy + SceneView + Inspector 的联动模式
 *
 * 职责：
 *   1. 选中事件转发：当用户在任意面板选中某个场景/物体时，同步更新其他面板的高亮状态
 *   2. Solo/Focus 协调：SceneViewer 的 Solo 模式应影响 SceneHierarchy 的显示过滤
 *   3. 加载状态同步：SceneManager 加载/卸载场景组时，通知 Viewer 刷新数据源
 *   4. 双程绑定：面板之间不直接持有对方引用，全部通过 Mediator 间接通信
 */

#include "Engine/Types.h"
#include <string>
#include <functional>
#include <unordered_set>

namespace Engine {

    // 前向声明
    class SceneHierarchyPanel;
    class SceneViewerPanel;
    class SceneManagerPanel;

    // ============================================================
    // 选中事件类型
    // ============================================================
    enum class SceneSelectionSource : uint8 {
        Hierarchy,  ///< 来自实体层级树（选中 GameObject）
        Viewer,     ///< 来自场景查看器（选中 Scene）
        Manager,    ///< 来自场景管理器（选中 SceneGroup）
        External,   ///< 来自外部（如 Gizmo 点击）
    };

    struct SceneSelectionEvent {
        std::string sceneName;           ///< 关联场景名
        std::string objectName;          ///< 选中对象名（若为 GameObject）
        uint64      objectId = 0;        ///< 选中对象 ID
        SceneSelectionSource source = SceneSelectionSource::External;
    };

    // ============================================================
    // 场景面板中介者
    // ============================================================
    class ScenePanelMediator {
    public:
        ScenePanelMediator() = default;
        ~ScenePanelMediator() = default;

        ScenePanelMediator(const ScenePanelMediator&) = delete;
        ScenePanelMediator& operator=(const ScenePanelMediator&) = delete;

        // ── 注册面板（不拥有所有权） ──
        void RegisterHierarchy(SceneHierarchyPanel* panel)  { m_Hierarchy = panel; }
        void RegisterViewer(SceneViewerPanel* panel)        { m_Viewer = panel; }
        void RegisterManager(SceneManagerPanel* panel)      { m_Manager = panel; }

        // ── 选中事件 ──
        void OnSceneSelected(const SceneSelectionEvent& event);

        // ── 场景加载/卸载通知 ──
        void OnSceneLoaded(const std::string& sceneName);
        void OnSceneUnloaded(const std::string& sceneName);

        // ── SceneViewer Solo 模式变更 ──
        void OnSoloModeChanged(const std::string& soloSceneName, bool active);

        // ── 查询 ──
        bool IsSoloModeActive() const { return m_SoloActive; }
        const std::string& GetSoloSceneName() const { return m_SoloSceneName; }

        // ── 回调（供 EngineEditor 监听） ──
        using GlobalSelectionCallback = std::function<void(const SceneSelectionEvent&)>;
        void SetGlobalSelectionCallback(GlobalSelectionCallback cb) {
            m_GlobalSelectionCallback = std::move(cb);
        }

    private:
        SceneHierarchyPanel* m_Hierarchy = nullptr;
        SceneViewerPanel*    m_Viewer    = nullptr;
        SceneManagerPanel*   m_Manager   = nullptr;

        // Solo 模式状态
        bool        m_SoloActive = false;
        std::string m_SoloSceneName;

        // 全局回调
        GlobalSelectionCallback m_GlobalSelectionCallback;
    };

} // namespace Engine