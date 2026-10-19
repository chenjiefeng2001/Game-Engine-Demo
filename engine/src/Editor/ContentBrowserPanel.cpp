#include "Engine/Editor/ContentBrowserPanel.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Log.h"
#include <imgui.h>
#include <filesystem>

namespace Engine {

    void ContentBrowserPanel::OnImGui() {
        if (!m_Visible) return;

        // 窗口名必须是 "Engine Resources" 而不是 "Content Browser"：
        // 本面板浏览的是**引擎级**纹理/着色器资源（ScanAssets 扫文件系统，
        // Textures/Shaders 两个页签），而 AssetBrowserPanel 浏览的是**工程**
        // 资产，两者都曾叫 "Content Browser"。ImGui 以标题标识窗口，同名
        // 窗口在同一帧被两次 Begin 会让 BeginCount=2、内容互相覆盖 ——
        // 而两者都能从 View 菜单打开，所以这不是理论风险。
        ImGui::Begin("Engine Resources", &m_Visible);

        // ── 筛选栏 ──
        ImGui::Checkbox("Textures", &m_ShowTextures); ImGui::SameLine();
        ImGui::Checkbox("Shaders",  &m_ShowShaders);  ImGui::SameLine();

        // 刷新按钮
        if (ImGui::SmallButton("Refresh")) {
            m_TextureFiles.clear();
            m_ShaderFiles.clear();
            ScanAssets();
        }

        // 首次扫描
        if (m_TextureFiles.empty() && m_ShaderFiles.empty()) {
            ScanAssets();
        }

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

        // 选中资源的详细信息
        if (!m_SelectedPath.empty()) {
            ImGui::Separator();
            ImGui::Text("Selected: %s", m_SelectedPath.c_str());
        }

        ImGui::End();
    }

    void ContentBrowserPanel::ScanAssets() {
        namespace fs = std::filesystem;

        // 扫描 textures 目录
        fs::path texDir("assets/textures");
        if (fs::exists(texDir)) {
            for (const auto& entry : fs::directory_iterator(texDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                        ext == ".bmp" || ext == ".tga" || ext == ".dds") {
                        m_TextureFiles.push_back(entry.path().string());
                    }
                }
            }
        }

        // 扫描 shaders 目录
        fs::path shaderDir("assets/shaders");
        if (fs::exists(shaderDir)) {
            for (const auto& entry : fs::directory_iterator(shaderDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".glsl" || ext == ".vert" || ext == ".frag" ||
                        ext == ".geom" || ext == ".comp" || ext == ".tesc" ||
                        ext == ".tese") {
                        m_ShaderFiles.push_back(entry.path().filename().string());
                    }
                }
            }
        }

        ENGINE_LOG_INFO("ContentBrowser", "Scanned %zu textures, %zu shaders",
                        m_TextureFiles.size(), m_ShaderFiles.size());
    }

    void ContentBrowserPanel::DrawTextureList() {
        if (m_TextureFiles.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "No texture assets found in assets/textures/");
            return;
        }

        // 网格视图：每行多个缩略图
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>(panelWidth / 120.0f));
        ImGui::Columns(columns, nullptr, false);

        for (const auto& texPath : m_TextureFiles) {
            namespace fs = std::filesystem;
            fs::path path(texPath);
            std::string filename = path.filename().string();

            ImGui::PushID(filename.c_str());
            ImGui::BeginGroup();

            // 文件图标 + 文件名
            if (ImGui::Button(("🖼\n" + filename).c_str(), ImVec2(100, 80))) {
                m_SelectedPath = texPath;
            }

            // 拖拽源
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM",
                                          texPath.c_str(), texPath.size() + 1);
                ImGui::Text("🖼 %s", filename.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TextWrapped("%s", filename.c_str());

            ImGui::EndGroup();
            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    void ContentBrowserPanel::DrawShaderList() {
        if (m_ShaderFiles.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "No shader assets found in assets/shaders/");
            return;
        }

        ImGui::Columns(3, "ShaderList", false);

        for (const auto& shader : m_ShaderFiles) {
            ImGui::Text("⚡ %s", shader.c_str());
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

} // namespace Engine