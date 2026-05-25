#pragma once

/**
 * @file ImGuiDemoApp.h
 * @brief 展示 Dear ImGui 集成效果的演示应用
 *
 * 包含场景层级面板、性能监控和 ImGui 示例窗口。
 */

#include <Engine/Application.h>
#include <Engine/SceneHierarchyPanel.h>
#include <Engine/InspectorPanel.h>
#include <Engine/ConsoleLog.h>
#include <Engine/ConsolePanel.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <imgui.h>
#include <iostream>

namespace Engine {

    class ImGuiDemoApp : public Application {
    public:
        ImGuiDemoApp(IGraphicsFactory& factory)
            : Application(factory)
        {
            // 创建演示场景
            m_DemoScene = std::make_unique<Scene>();

            // 创建根对象
            auto root = std::make_shared<GameObject>("Root Object");
            auto player = std::make_shared<GameObject>("Player");
            auto camera = std::make_shared<GameObject>("Main Camera");
            auto light = std::make_shared<GameObject>("Directional Light");

            // 添加子对象
            auto weapon = std::make_shared<GameObject>("Weapon");
            player->AddChild(weapon);

            auto muzzle = std::make_shared<GameObject>("Muzzle Flash");
            weapon->AddChild(muzzle);

            auto uiCamera = std::make_shared<GameObject>("UI Camera");
            camera->AddChild(uiCamera);

            // 将对象加入场景
            m_DemoScene->AddObject(root);
            m_DemoScene->AddObject(player);
            m_DemoScene->AddObject(camera);
            m_DemoScene->AddObject(light);

            // 额外一些扁平对象
            m_DemoScene->AddObject(std::make_shared<GameObject>("Terrain"));
            m_DemoScene->AddObject(std::make_shared<GameObject>("Skybox"));
            m_DemoScene->AddObject(std::make_shared<GameObject>("Particle System"));

            // 初始化层级面板
            m_HierarchyPanel.SetScene(m_DemoScene.get());
            m_HierarchyPanel.SetSelectionCallback([this](GameObject* selected) {
                m_InspectorPanel.SetTarget(selected);
                if (selected)
                    LOG_INFO(std::string("Selected: ") + selected->GetName());
                else
                    LOG_INFO("Selection cleared");
            });

            // ── 写入演示日志 ──
            LOG_INFO("Demo scene created with 8 objects");
            LOG_INFO("Initializing UI panels...");
            LOG_WARN("This is a warning message");
            LOG_INFO("Application ready");
            LOG_ERROR("This is a sample error (not a real problem)");
        }

    protected:
        void OnImGui() override
        {
            // ── 场景层级面板 ──
            m_HierarchyPanel.OnImGui();

            // ── 属性检视器面板 ──
            m_InspectorPanel.OnImGui();

            // ── 控制台日志窗口（默认隐藏，按 F1 切换） ──
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
                m_ConsolePanel.ToggleVisibility();
            m_ConsolePanel.OnImGui();

            // ── ImGui 内置示例窗口 ──
            ImGui::ShowDemoWindow(nullptr);

            // ── 引擎信息 ──
            ImGui::Begin("Engine Info");
            ImGui::Text("Dear ImGui 集成演示");
            ImGui::Separator();
            ImGui::Text("帧率: %.1f FPS", ImGui::GetIO().Framerate);
            ImGui::Text("窗口尺寸: %.0f x %.0f",
                ImGui::GetIO().DisplaySize.x,
                ImGui::GetIO().DisplaySize.y);
            ImGui::Text("F1: 打开/关闭控制台");
            ImGui::End();
        }

    private:
        std::unique_ptr<Scene> m_DemoScene;
        SceneHierarchyPanel m_HierarchyPanel;
        InspectorPanel m_InspectorPanel;
        ConsolePanel m_ConsolePanel;
    };

} // namespace Engine
