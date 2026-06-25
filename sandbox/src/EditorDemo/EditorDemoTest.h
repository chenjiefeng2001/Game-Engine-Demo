#pragma once

/**
 * @file EditorDemoTest.h
 * @brief 编辑器 Demo 测试 — 验证所有编辑器组件的集成
 *
 * 测试场景：
 *   1. 场景面板中介者 (ScenePanelMediator)
 *   2. 属性检视器 (InspectorPanel) 
 *   3. 撤销系统 (UndoSystem)
 *   4. 资产浏览器 (AssetBrowserPanel) + 资产数据库 (EditorAssetDatabase)
 *   5. 控制台 (ConsolePanel) — 频道/正则/折叠
 *   6. 性能分析器 (ProfilerPanel) + ProfilerCore
 *   7. 状态栏 (StatusBar)
 *   8. Lua 脚本引擎 (LuaEngine) — 桩模式
 *   9. 依赖追踪 (DependencyTracker)
 */

#include "Engine/InspectorPanel.h"
#include "Engine/Editor/ScenePanelMediator.h"
#include "Engine/Editor/UndoSystem.h"
#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Editor/DependencyTracker.h"
#include "Engine/Editor/StatusBar.h"
#include "Engine/Editor/ProfilerPanel.h"
#include "Engine/Debug/ProfilerCore.h"
#include "Engine/Scripting/PluginSystem.h"
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/ConsolePanel.h"

#include <imgui.h>

namespace Engine {

    class EditorDemoApp;

    class EditorDemoTest {
    public:
        EditorDemoTest(EditorDemoApp* app);
        ~EditorDemoTest() = default;

        void Init();
        void OnImGui();
        void OnUpdate(float dt);

    private:
        void DrawTestMenuBar();
        void DrawTestResults();

        EditorDemoApp* m_App = nullptr;

        // 测试对象
        InspectorPanel     m_Inspector;
        ScenePanelMediator m_Mediator;
        UndoManager&       m_UndoMgr = UndoManager::Get();
        EditorAssetDatabase* m_AssetDB = nullptr;
        DependencyTracker  m_DepTracker;
        AssetBrowserPanel  m_AssetBrowser;
        StatusBar          m_StatusBar;
        ProfilerPanel      m_ProfilerPanel;
        Debug::ProfilerCore m_ProfilerCore;
        ConsolePanel       m_ConsolePanel;
        Plugin::PluginManager& m_PluginMgr = Plugin::PluginManager::Get();

        // 测试状态
        int m_TestFrameCount = 0;
        bool m_ShowTestUI = true;

        // 测试快捷开关
        bool m_WireframeMode = false;
        bool m_MuteAudio = false;
    };

} // namespace Engine