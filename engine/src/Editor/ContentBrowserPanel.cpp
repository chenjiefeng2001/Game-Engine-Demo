#include "Engine/Editor/ContentBrowserPanel.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include <imgui.h>

namespace Engine {

    void ContentBrowserPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::Begin("Content Browser", &m_Visible);

        // ── 筛选栏 ──
        ImGui::Checkbox("Textures", &m_ShowTextures); ImGui::SameLine();
        ImGui::Checkbox("Shaders",  &m_ShowShaders);

        ImGui::Separator();

        // ── 资源列表 ──
        if (ImGui::BeginTabBar("ContentTabs")) {
            if (m_ShowTextures && ImGui::BeginTabItem("Textures")) {
                DrawTextureList();
                ImGui::EndTabItem();
            }
            if (m_ShowShaders && ImGui::BeginTabItem("Shaders")) {
                DrawShaderList();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void ContentBrowserPanel::DrawTextureList() {
        auto* rm = ResourceManager::Get();
        if (!rm) {
            ImGui::TextColored(ImVec4(1,0,0,1), "ResourceManager not available");
            return;
        }

        // 这里简化实现 — 实际应遍历 ResourceManager 中的纹理
        ImGui::Text("Loaded textures will appear here.");
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Use ResourceManager::GetResources() to list.");
    }

    void ContentBrowserPanel::DrawShaderList() {
        auto* rm = ResourceManager::Get();
        if (!rm) {
            ImGui::TextColored(ImVec4(1,0,0,1), "ResourceManager not available");
            return;
        }

        ImGui::Text("Loaded shaders will appear here.");
    }

} // namespace Engine
