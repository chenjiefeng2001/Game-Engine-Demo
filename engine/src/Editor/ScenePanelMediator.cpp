#include "Engine/Editor/ScenePanelMediator.h"
#include "Engine/SceneHierarchyPanel.h"
#include "Engine/Editor/SceneViewerPanel.h"
#include "Engine/Editor/SceneManagerPanel.h"
#include "Engine/Core/Log.h"

namespace Engine {

    void ScenePanelMediator::OnSceneSelected(const SceneSelectionEvent& event) {
        // 1. 同步 SceneHierarchyPanel
        if (m_Hierarchy) {
            // 如果选中来自 Viewer 或 Manager，通知 Hierarchy 定位到对应场景
            if (event.source == SceneSelectionSource::Viewer ||
                event.source == SceneSelectionSource::Manager) {
                // 选中场景名对应的第一个根节点
                // SceneHierarchyPanel 内部通过 SetScene 切换场景
                Scene* scene = m_Hierarchy->GetScene();
                if (scene && scene->GetName() != event.sceneName) {
                    // 不强制切换场景，只做高亮提示
                    // 具体实现由 SceneHierarchyPanel 的 SelectScene 方法处理
                }
            }
        }

        // 2. 同步 SceneViewerPanel
        if (m_Viewer) {
            if (!event.sceneName.empty()) {
                m_Viewer->FocusOnScene(event.sceneName);
                m_Viewer->HighlightGroup(event.sceneName);
            } else {
                m_Viewer->ClearHighlights();
            }
        }

        // 3. 全局回调（EngineEditor 用它更新 Inspector）
        if (m_GlobalSelectionCallback) {
            m_GlobalSelectionCallback(event);
        }
    }

    void ScenePanelMediator::OnSceneLoaded(const std::string& sceneName) {
        Log::Info("[Mediator] Scene loaded: {}", sceneName);
        // Viewer 会在下一帧 RefreshSceneData 时自动检测新场景
        // 如有需要可强制刷新
        if (m_Viewer) {
            // 触发 Viewer 刷新
        }
    }

    void ScenePanelMediator::OnSceneUnloaded(const std::string& sceneName) {
        Log::Info("[Mediator] Scene unloaded: {}", sceneName);
        if (m_Viewer) {
            m_Viewer->ClearHighlights();
        }
    }

    void ScenePanelMediator::OnSoloModeChanged(const std::string& soloSceneName, bool active) {
        m_SoloActive = active;
        m_SoloSceneName = active ? soloSceneName : "";

        // Solo 模式下，SceneHierarchy 应只显示 solo 场景中的物体
        if (m_Hierarchy) {
            Scene* scene = m_Hierarchy->GetScene();
            if (scene && active) {
                // 如果 Hierarchy 当前显示的场景不是 solo 场景，自动切换
                // SceneHierarchyPanel 内部做场景切换
            }
        }

        Log::Info("[Mediator] Solo mode {} for scene '{}'",
                  active ? "activated" : "deactivated",
                  soloSceneName);
    }

} // namespace Engine