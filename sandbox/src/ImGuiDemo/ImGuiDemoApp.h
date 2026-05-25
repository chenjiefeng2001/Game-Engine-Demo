#pragma once

/**
 * @file ImGuiDemoApp.h
 * @brief 展示 Dear ImGui 集成效果的演示应用
 *
 * 继承 Application 并重写 OnImGui() 来显示 ImGui 示例窗口。
 */

#include <Engine/Application.h>
#include <imgui.h>

namespace Engine {

    class ImGuiDemoApp : public Application {
    public:
        ImGuiDemoApp(IGraphicsFactory& factory)
            : Application(factory)
        {
        }

    protected:
        void OnImGui() override
        {
            // 显示 ImGui 内置的示例窗口
            ImGui::ShowDemoWindow(nullptr);

            // 也可以添加自定义窗口
            ImGui::Begin("Engine Info");
            ImGui::Text("Dear ImGui 集成演示");
            ImGui::Separator();
            ImGui::Text("帧率: %.1f FPS", ImGui::GetIO().Framerate);
            ImGui::Text("窗口尺寸: %.0f x %.0f",
                ImGui::GetIO().DisplaySize.x,
                ImGui::GetIO().DisplaySize.y);
            ImGui::End();
        }
    };

} // namespace Engine
