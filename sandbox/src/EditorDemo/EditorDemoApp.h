#pragma once

/**
 * @file EditorDemoApp.h
 * @brief 引擎编辑器演示 — 展示 EngineEditor UI 框架集成
 *
 * 演示内容：
 *   - EngineEditor 自动布局（SceneHierarchy / Inspector / Viewport / Console 等）
 *   - 主菜单栏 + 工具栏
 *   - 面板可见性切换
 */

#include <Engine/Application.h>
#include <Engine/Editor/EngineEditor.h>
#include <Engine/SceneHierarchyPanel.h>
#include <Engine/InspectorPanel.h>
#include <Engine/ConsolePanel.h>
#include <iostream>

namespace Engine {

    class EditorDemoApp : public Application {
    public:
        EditorDemoApp(IGraphicsFactory& factory)
            : Application(factory)
        {
            // 性能窗口交由 EngineEditor 管理，Application::Run() 不再自动绘制
            m_DrawPerformanceWindow = false;
        }

        ~EditorDemoApp() override {
            std::cout << "=== EditorDemo Shutdown ===" << std::endl;
        }

    protected:
        void OnStartup() override {
            // ── 初始化 Editor ──
            m_Editor.Init(this);

            // 注册已有的引擎面板
            m_Editor.RegisterSceneHierarchy(&m_HierarchyPanel);
            m_Editor.RegisterInspector(&m_InspectorPanel);
            m_Editor.RegisterConsole(&m_ConsolePanel);
            m_Editor.RegisterPerformance(&m_PerfWindow);

            std::cout << "=== EditorDemo Started ===" << std::endl;
            std::cout << "Press F1-F6 in View menu to toggle panels" << std::endl;
        }

        void OnUpdate(float32 dt) override {
            (void)dt;
        }

        void OnRender() override {
            // 默认的 Application::InternalRender() 已绘制基础场景
        }

        void OnImGui() override {
            // 只需一行——Editor 管理所有面板布局
            m_Editor.OnImGui();
        }

    private:
        EngineEditor          m_Editor;
        SceneHierarchyPanel   m_HierarchyPanel;
        InspectorPanel        m_InspectorPanel;
        ConsolePanel          m_ConsolePanel;
    };

} // namespace Engine
