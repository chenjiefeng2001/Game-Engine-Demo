#pragma once

/**
 * @file EngineEditor.h
 * @brief 引擎编辑器 — 统一管理所有 UI 面板和菜单的编辑层框架
 *
 * 职责：
 *   1. 管理 ImGui Docking 布局
 *   2. 拥有并协调所有编辑器面板的生命周期
 *   3. 提供主菜单栏 + 工具栏
 *   4. 与 Application::OnImGui() 集成
 *   5. 支持面板分离为独立 GLFW 窗口
 *
 * 使用方式（在 Application 子类中）：
 * @code
 *   class MyApp : public Application {
 *       EngineEditor m_Editor;
 *       void OnStartup() override { m_Editor.Init(this); }
 *       void OnImGui()  override { m_Editor.OnImGui(); }
 *   };
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Editor/MainMenuBar.h"
#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Editor/EditorSceneManager.h"
#include "Engine/Editor/ContentBrowserPanel.h"
#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Editor/DependencyGraphPanel.h"
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

        // ── 生命周期 ──

        /**
         * @brief 初始化编辑器
         * @param app 关联的 Application 实例（用于获取窗口/上下文等）
         */
        void Init(Application* app);

        /** 在 Application::OnImGui() 中调用 */
        void OnImGui();

        /** 在 Application::OnRender() 前调用，更新视口相关数据 */
        void OnUpdate(float32 dt);

        // ── 面板访问 ──
        MainMenuBar&          GetMenuBar()        { return m_MenuBar; }
        Toolbar&              GetToolbar()        { return m_Toolbar; }
        ViewportPanel&        GetViewport()       { return m_Viewport; }
        EditorSceneManager&   GetSceneManager()   { return m_SceneManager; }
        ContentBrowserPanel&  GetContentBrowser()   { return m_ContentBrowser; }
        AssetBrowserPanel&      GetAssetBrowser()     { return m_AssetBrowser; }
        DependencyGraphPanel& GetDependencyGraph()  { return m_DepGraph; }

        // ── 编辑器状态 ──
        EditorState GetEditorState() const { return m_SceneManager.GetState(); }
        bool IsPlaying() const { return m_SceneManager.IsPlaying(); }
        bool IsPaused()  const { return m_SceneManager.IsPaused(); }
        bool IsEditing() const { return m_SceneManager.IsEditing(); }

        // ── 面板注册（供外部访问已有的 Engine 面板） ──
        void RegisterSceneHierarchy(SceneHierarchyPanel* panel) { m_SceneHierarchy = panel; }
        void RegisterInspector(InspectorPanel* panel)           { m_Inspector = panel; }
        void RegisterConsole(ConsolePanel* panel)               { m_Console = panel; }
        void RegisterPerformance(PerformanceWindow* window)     { m_Performance = window; }

        // ── Gizmo 操作物体选择 ──
        void SetSelectedObject(std::shared_ptr<class GameObject> obj) { m_SelectedObject = obj; }
        std::shared_ptr<class GameObject> GetSelectedObject() const { return m_SelectedObject.lock(); }

    private:
        // ── Gizmo 渲染 ──
        /** 在 Viewport 中绘制 ImGuizmo 操作手柄 */
        void DrawGizmo(const glm::mat4& viewMatrix, const glm::mat4& projMatrix);

        // ── 初始化 Docking 布局 ──
        void InitDockingLayout();

        // ── 绘制各个面板的包裹窗口（带 Docking 支持） ──
        void DrawSceneHierarchyWindow();
        void DrawInspectorWindow();
        void DrawConsoleWindow();
        void DrawPerformanceWindow();
        void DrawContentBrowserWindow();
        void DrawAssetBrowserWindow();
        void DrawDepGraphWindow();
        void DrawViewportPanel();

        // ── 拥有的面板 ──
        MainMenuBar           m_MenuBar;
        Toolbar               m_Toolbar;
        ViewportPanel         m_Viewport;
        EditorSceneManager    m_SceneManager;
        ContentBrowserPanel   m_ContentBrowser;
        AssetBrowserPanel     m_AssetBrowser;
        DependencyGraphPanel  m_DepGraph;

        // ── 面板可见性（EngineEditor 拥有，MainMenuBar 持有指针） ──
        PanelVisibility m_Visibility;

        // ── 外部注册的引擎面板（非拥有，仅引用） ──
        SceneHierarchyPanel* m_SceneHierarchy = nullptr;
        InspectorPanel*      m_Inspector      = nullptr;
        ConsolePanel*        m_Console        = nullptr;
        PerformanceWindow*   m_Performance    = nullptr;

        // ── 布局状态 ──
        bool m_DockingInitialized = false;
        bool m_ShowDockingDemo    = false;  // ImGui 自带的 Docking 演示

        // 关联的 Application
        Application* m_App = nullptr;

        // ── Gizmo 操作物体（通过 weak_ptr 避免循环引用） ──
        std::weak_ptr<class GameObject> m_SelectedObject;
    };

} // namespace Engine
