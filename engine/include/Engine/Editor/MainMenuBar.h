#pragma once

#include "Engine/Types.h"
#include <functional>
#include <string>

namespace Engine {

    struct PanelVisibility {
        bool sceneHierarchy = true;
        bool inspector      = true;
        bool console        = true;
        bool performance    = true;
        bool contentBrowser = true;
        bool assetBrowser   = true;
        bool depGraph       = false;
        bool viewport       = true;
    };

    class MainMenuBar {
    public:
        MainMenuBar() = default;
        ~MainMenuBar() = default;

        MainMenuBar(const MainMenuBar&) = delete;
        MainMenuBar& operator=(const MainMenuBar&) = delete;

        /**
         * @brief 作为独立顶层菜单栏渲染（使用 ImGui::BeginMainMenuBar）
         * 适用于未使用 DockSpace 容器的场景
         */
        void OnImGui();

        /**
         * @brief 在窗口菜单栏内部渲染菜单（在 BeginMenuBar/EndMenuBar 之间调用）
         * 适用于全屏 DockSpace 窗口内嵌菜单栏的架构
         */
        void OnMenuBar();

        void SetPanelVisibility(PanelVisibility* vis) { m_Visibility = vis; }

        using ExitCallback = std::function<void()>;
        void SetExitCallback(ExitCallback cb) { m_ExitCallback = std::move(cb); }

        using NewSceneCallback = std::function<void()>;
        void SetNewSceneCallback(NewSceneCallback cb) { m_NewSceneCallback = std::move(cb); }

        using OpenSceneCallback = std::function<void()>;
        void SetOpenSceneCallback(OpenSceneCallback cb) { m_OpenSceneCallback = std::move(cb); }

        using SaveSceneCallback = std::function<void()>;
        void SetSaveSceneCallback(SaveSceneCallback cb) { m_SaveSceneCallback = std::move(cb); }

        using SaveAsCallback = std::function<void()>;
        void SetSaveAsCallback(SaveAsCallback cb) { m_SaveAsCallback = std::move(cb); }

        using PlayCallback   = std::function<void()>;
        using StopCallback   = std::function<void()>;
        using PauseCallback  = std::function<void()>;
        using StepCallback   = std::function<void()>;

        void SetPlayCallback(PlayCallback cb)     { m_PlayCallback = std::move(cb); }
        void SetStopCallback(StopCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(PauseCallback cb)   { m_PauseCallback = std::move(cb); }
        void SetStepCallback(StepCallback cb)     { m_StepCallback = std::move(cb); }

        using ToggleDemoCallback = std::function<void()>;
        void SetToggleDemoCallback(ToggleDemoCallback cb) { m_ToggleDemoCallback = std::move(cb); }

        bool IsDemoVisible() const { return m_ShowDemo; }
        void SetDemoVisible(bool v) { m_ShowDemo = v; }

    private:
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawSceneMenu();
        void DrawViewMenu();
        void DrawToolsMenu();
        void DrawHelpMenu();

        PanelVisibility* m_Visibility = nullptr;
        bool m_ShowDemo = false;

        ExitCallback       m_ExitCallback;
        NewSceneCallback   m_NewSceneCallback;
        OpenSceneCallback  m_OpenSceneCallback;
        SaveSceneCallback  m_SaveSceneCallback;
        SaveAsCallback     m_SaveAsCallback;
        PlayCallback       m_PlayCallback;
        StopCallback       m_StopCallback;
        PauseCallback      m_PauseCallback;
        StepCallback       m_StepCallback;
        ToggleDemoCallback m_ToggleDemoCallback;
    };

} // namespace Engine