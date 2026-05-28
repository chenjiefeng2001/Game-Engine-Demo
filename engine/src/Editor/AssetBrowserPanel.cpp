#include "Engine/Editor/AssetBrowserPanel.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

namespace Engine {

    void AssetBrowserPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::Begin("Asset Browser", &m_Visible);

        // ── 路径导航 ──
        ImGui::Text("Path: %s", m_CurrentDir.c_str());
        ImGui::Separator();

        // ── 左右分栏：目录树 | 文件列表 ──
        ImGui::BeginChild("DirTree", ImVec2(200, 0), true);
        DrawDirectoryTree(m_RootPath, m_RootPath);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("FileGrid", ImVec2(0, 0), true);
        DrawFileGrid(m_CurrentDir);
        ImGui::EndChild();

        ImGui::End();
    }

    void AssetBrowserPanel::DrawDirectoryTree(const std::string& dirPath,
                                               const std::string& displayName) {
        // 简化实现：仅显示根目录
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (m_CurrentDir == dirPath)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool opened = ImGui::TreeNodeEx(displayName.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_CurrentDir = dirPath;
        }

        if (opened) {
            // 遍历子目录（使用 std::filesystem）
            namespace fs = std::filesystem;
            if (fs::exists(dirPath)) {
                std::vector<fs::path> dirs;
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.is_directory())
                        dirs.push_back(entry.path());
                }
                std::sort(dirs.begin(), dirs.end());
                for (const auto& dir : dirs) {
                    DrawDirectoryTree(dir.string(), dir.filename().string());
                }
            }
            ImGui::TreePop();
        }
    }

    void AssetBrowserPanel::DrawFileGrid(const std::string& dirPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(dirPath)) return;

        // 文件列表
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (!entry.is_directory())
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        ImVec2 cellSize(80, 80);
        int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize.x));

        ImGui::Columns(columns, nullptr, false);

        for (const auto& file : files) {
            std::string filename = file.filename().string();
            std::string ext = file.extension().string();

            // 按钮表示文件
            ImGui::BeginGroup();
            ImGui::PushID(filename.c_str());
            ImGui::Button("File", ImVec2(64, 64));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_SelectedFile = file.string();
                if (m_FileOpenCallback)
                    m_FileOpenCallback(file.string());
            }
            ImGui::TextWrapped("%s", filename.c_str());
            ImGui::PopID();
            ImGui::EndGroup();
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

} // namespace Engine
