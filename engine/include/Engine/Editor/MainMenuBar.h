#pragma once

#include "Engine/Types.h"
#include "Engine/Editor/EditorDefs.h"
#include <functional>
#include <string>

namespace Engine {

    /**
     * @brief 撤消/重做状态查询（供 Edit 菜单启用/禁用菜单项使用）
     *
     * 由引擎的 CommandStack 或 UndoSystem 通过 SetCanUndo/SetCanRedo 注入，
     * 菜单栏在每帧绘制时查询这些值。
     */
    struct UndoState {
        bool canUndo = false;
        bool canRedo = false;
    };

    class MainMenuBar {
    public:
        MainMenuBar() = default;
        ~MainMenuBar() = default;

        MainMenuBar(const MainMenuBar&) = delete;
        MainMenuBar& operator=(const MainMenuBar&) = delete;

        // ── 渲染 ──

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

        // ── 面板可见性 ──
        void SetPanelVisibility(PanelVisibility* vis) { m_Visibility = vis; }

        // ── 撤消/重做状态 ──
        UndoState& GetUndoState() { return m_UndoState; }
        void SetCanUndo(bool v) { m_UndoState.canUndo = v; }
        void SetCanRedo(bool v) { m_UndoState.canRedo = v; }

        // ── 文件操作回调 ──
        using ExitCallback           = std::function<void()>;
        using NewSceneCallback       = std::function<void()>;
        using OpenScenePathCallback  = std::function<void(const std::string&)>;
        using SaveSceneCallback      = std::function<void()>;
        using SaveAsCallback         = std::function<void()>;

        void SetExitCallback(ExitCallback cb)                    { m_ExitCallback = std::move(cb); }
        void SetNewSceneCallback(NewSceneCallback cb)            { m_NewSceneCallback = std::move(cb); }
        void SetOpenSceneCallback(OpenScenePathCallback cb)      { m_OpenSceneCallback = std::move(cb); }
        void SetSaveSceneCallback(SaveSceneCallback cb)          { m_SaveSceneCallback = std::move(cb); }
        void SetSaveAsCallback(SaveAsCallback cb)                { m_SaveAsCallback = std::move(cb); }

        // ── 编辑操作回调 ──
        using UndoCallback   = std::function<void()>;
        using RedoCallback   = std::function<void()>;

        void SetUndoCallback(UndoCallback cb)  { m_UndoCallback = std::move(cb); }
        void SetRedoCallback(RedoCallback cb)  { m_RedoCallback = std::move(cb); }

        // ── 场景控制回调（应与 Toolbar 共享同一套逻辑） ──
        using PlayCallback   = std::function<void()>;
        using StopCallback   = std::function<void()>;
        using PauseCallback  = std::function<void()>;
        using StepCallback   = std::function<void()>;

        void SetPlayCallback(PlayCallback cb)     { m_PlayCallback = std::move(cb); }
        void SetStopCallback(StopCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(PauseCallback cb)   { m_PauseCallback = std::move(cb); }
        void SetStepCallback(StepCallback cb)     { m_StepCallback = std::move(cb); }

        // ── ImGui Demo 窗口 ──
        using ToggleDemoCallback = std::function<void()>;
        void SetToggleDemoCallback(ToggleDemoCallback cb) { m_ToggleDemoCallback = std::move(cb); }
        bool IsDemoVisible() const { return m_ShowDemo; }
        void SetDemoVisible(bool v) { m_ShowDemo = v; }

        // ── 布局重置信号（由外部 EngineEditor 消费，在 View 菜单触发） ──
        bool ConsumeResetLayoutSignal() { bool v = m_ResetLayoutRequested; m_ResetLayoutRequested = false; return v; }
        void RequestResetLayout() { m_ResetLayoutRequested = true; }

    private:
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawSceneMenu();
        void DrawViewMenu();
        void DrawToolsMenu();
        void DrawHelpMenu();

        // ── Preferences 偏好设置窗口 ──
        void DrawPreferencesWindow();

        // ── 外部指针（不拥有） ──
        PanelVisibility* m_Visibility = nullptr;

        // ── 内部对话框状态 ──
        bool m_ShowDemo        = false;   // ImGui Demo 窗口
        bool m_ShowPreferences = false;   // 偏好设置窗口
        bool m_ShowMetrics     = false;   // ImGui Metrics/Debugger
        bool m_ShowStackTool   = false;   // ImGui Stack Tool

        // ── 布局重置信号 ──
        bool m_ResetLayoutRequested = false;

        // ── 撤消/重做状态 ──
        UndoState m_UndoState;

        // ── 回调函数组 ──
        // 文件操作
        ExitCallback           m_ExitCallback;
        NewSceneCallback       m_NewSceneCallback;
        OpenScenePathCallback  m_OpenSceneCallback;
        SaveSceneCallback      m_SaveSceneCallback;
        SaveAsCallback         m_SaveAsCallback;

        // 编辑操作
        UndoCallback           m_UndoCallback;
        RedoCallback           m_RedoCallback;

        // 场景控制
        PlayCallback           m_PlayCallback;
        StopCallback           m_StopCallback;
        PauseCallback          m_PauseCallback;
        StepCallback           m_StepCallback;

        // 工具
        ToggleDemoCallback     m_ToggleDemoCallback;
    };

} // namespace Engine
