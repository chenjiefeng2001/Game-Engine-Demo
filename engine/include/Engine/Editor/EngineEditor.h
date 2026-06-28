#pragma once

/**
 * @file EngineEditor.h
 * @brief 引擎编辑器 — 统一管理所有 UI 面板和菜单的编辑层框架
 */

#include "Engine/Types.h"
#include "Engine/Editor/MainMenuBar.h"
#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Editor/EditorSceneManager.h"
#include "Engine/Editor/ContentBrowserPanel.h"
#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Editor/DependencyGraphPanel.h"
#include "Engine/Editor/ScenePanel.h"
#include "Engine/Editor/SceneManagerPanel.h"
#include "Engine/Editor/SceneViewerPanel.h"
#include "Engine/Editor/StatusBar.h"
#include "Engine/Editor/ViewModePanel.h"
#include "Engine/Editor/EditorTools.h"
#include "Engine/Editor/EditorDefs.h"
#include "Engine/Editor/ShaderGraph/ShaderGraphPanel.h"
#include "Engine/Editor/VFXGraph/VFXGraphPanel.h"
    #include "Engine/Editor/VFXGraph/VFXGraphCore.h"
#include "Engine/Editor/VFXGraph/VFXNodeFactory.h"
#include "Engine/Core/EventBus.h"
    #include <glm/glm.hpp>
    #include <glm/gtc/type_ptr.hpp>

namespace Engine {

    class Application;
    class SceneHierarchyPanel;
    class InspectorPanel;
    class ConsolePanel;
    class PerformanceWindow;
    class IRenderContext;

    class EngineEditor {
    public:
        EngineEditor();
        ~EngineEditor();

        EngineEditor(const EngineEditor&) = delete;
        EngineEditor& operator=(const EngineEditor&) = delete;

        void Init(Application* app);
        void OnImGui();
        void OnUpdate(float32 dt);

        // ── 面板访问 ──
        MainMenuBar&          GetMenuBar()        { return m_MenuBar; }
        Toolbar&              GetToolbar()        { return m_Toolbar; }
        ViewportPanel&        GetViewport()       { return m_Viewport; }
        EditorSceneManager&   GetSceneManager()   { return m_SceneManager; }
        ContentBrowserPanel&  GetContentBrowser()   { return m_ContentBrowser; }
        AssetBrowserPanel&      GetAssetBrowser()     { return m_AssetBrowser; }
        DependencyGraphPanel& GetDependencyGraph()  { return m_DepGraph; }
        SceneViewerPanel&     GetSceneViewerPanel()   { return m_SceneViewerPanel; }

        EditorState GetEditorState() const { return m_SceneManager.GetState(); }
        bool IsPlaying() const { return m_SceneManager.IsPlaying(); }
        bool IsPaused()  const { return m_SceneManager.IsPaused(); }
        bool IsEditing() const { return m_SceneManager.IsEditing(); }

        void RegisterSceneHierarchy(SceneHierarchyPanel* panel);
        void RegisterInspector(InspectorPanel* panel)           { m_Inspector = panel; }
        void RegisterConsole(ConsolePanel* panel)               { m_Console = panel; }
        void RegisterPerformance(PerformanceWindow* window)     { m_Performance = window; }

        void SetSelectedObject(std::shared_ptr<class GameObject> obj) { m_SelectedObject = obj; }
        std::shared_ptr<class GameObject> GetSelectedObject() const { return m_SelectedObject.lock(); }

        // ── 统一选择回调（Hierarchy ↔ Inspector ↔ Viewport Gizmo 联动） ──
        void OnSelectionChanged(GameObject* obj);

        // ── 场景渲染注入器（由 Application 子类设置，将场景绘制到视口 FBO） ──
        // MRT 单次 Pass：Fragment Shader 同时输出颜色(location=0)和ID(location=1)
        using SceneRenderInjector = std::function<void(const float* viewProj16, const float* camPos3)>;
        void SetSceneRenderInjector(SceneRenderInjector injector) { m_SceneRenderInjector = std::move(injector); }

    private:
        void DrawGizmo(const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
        void InitDockingLayout();

        // ── 面板绘制 ──
        void DrawSceneHierarchyWindow();
        void DrawInspectorWindow();
        void DrawConsoleWindow();
        void DrawPerformanceWindow();
        void DrawContentBrowserWindow();
        void DrawAssetBrowserWindow();
        void DrawDepGraphWindow();
        void DrawViewportPanel();
        void DrawSceneManagerWindow();
        void DrawSceneViewerWindow();
        void DrawViewModeWindow();
        void DrawEditorSettingsWindow();

        // ── 拥有的面板 ──
        MainMenuBar           m_MenuBar;
        Toolbar               m_Toolbar;
        StatusBar             m_StatusBar;
        ViewportPanel         m_Viewport{"Viewport"};
        EditorSceneManager    m_SceneManager;
        ContentBrowserPanel   m_ContentBrowser;
        AssetBrowserPanel     m_AssetBrowser;
        DependencyGraphPanel  m_DepGraph;
        ScenePanel            m_ScenePanel;           // 统一场景面板（Tab 合并视图）
        SceneManagerPanel     m_SceneManagerPanel;    // 场景管理器（独立窗口）
        SceneViewerPanel      m_SceneViewerPanel;     // 场景查看器（独立窗口）
        ViewModePanel         m_ViewModePanel;

        // ── 工具面板 ──
        ShaderGraph::ShaderGraphPanel m_ShaderGraphPanel;
        ShaderGraph::ShaderGraph      m_ShaderGraph;

        // ── VFX 编辑器 ──
        VFX::VFXGraphPanel m_VFXGraphPanel;
        VFX::VFXGraph      m_VFXGraph;

        // ── 编辑器工具状态 ──
        GizmoOperation  m_GizmoOp     = GizmoOperation::Translate;
        GizmoSpace      m_GizmoSpace  = GizmoSpace::World;
        PivotMode       m_PivotMode   = PivotMode::Center;
        SnapConfig      m_SnapCfg;
        SelectionMask   m_SelMask;
        IsolationState  m_Isolation;
        StatsOverlayConfig m_StatsCfg;
        uint32          m_VisMask     = 0xFFFFFFFF;

        // ── 面板可见性 ──
        PanelVisibility m_Visibility;

        // ── 外部注册的引擎面板 ──
        SceneHierarchyPanel* m_SceneHierarchy = nullptr;
        InspectorPanel*      m_Inspector      = nullptr;
        ConsolePanel*        m_Console        = nullptr;
        PerformanceWindow*   m_Performance    = nullptr;

        // ── 布局状态 ──
        bool m_DockingInitialized = false;
        bool m_ResetLayoutRequested = false;
        bool m_ShowDockingDemo      = false;
        Application* m_App = nullptr;

        std::weak_ptr<class GameObject> m_SelectedObject;

        // ── EventBus 订阅句柄（RAII 自动取消订阅） ──
        SubscriptionHandle m_PickSubscription;
        SubscriptionHandle m_SceneSwitchSubscription;

        // ── 场景渲染注入器（由 Application 子类设置，用于绘制场景到主视口 FBO） ──
        SceneRenderInjector m_SceneRenderInjector;
    };

} // namespace Engine