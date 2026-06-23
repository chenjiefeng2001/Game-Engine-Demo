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
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            namespace fs = std::filesystem;
            fs::path p = m_CurrentDir;
            if (p.has_parent_path()) {
                m_CurrentDir = p.parent_path().string();
            }
        }
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
        namespace fs = std::filesystem;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (m_CurrentDir == dirPath)
            flags |= ImGuiTreeNodeFlags_Selected;

        // 检查是否有子目录 → 如果没有子目录则标记为 leaf
        bool hasSubdirs = false;
        if (fs::exists(dirPath)) {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                if (entry.is_directory()) {
                    hasSubdirs = true;
                    break;
                }
            }
        }
        if (!hasSubdirs)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool opened = ImGui::TreeNodeEx(displayName.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_CurrentDir = dirPath;
        }

        if (opened) {
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

    const char* AssetBrowserPanel::GetFileIcon(const std::string& ext) {
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
            return "🖼";  // 图片
        if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".hlsl")
            return "⚡";  // 着色器
        if (ext == ".json" || ext == ".scene")
            return "📄";  // 场景/数据
        if (ext == ".ttf" || ext == ".otf")
            return "🔤";  // 字体
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
            return "📦";  // 模型
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
            return "🎵";  // 音频
        if (ext == ".mp4" || ext == ".avi" || ext == ".mov")
            return "🎬";  // 视频
        return "📄";  // 默认文件
    }

    void AssetBrowserPanel::DrawFileGrid(const std::string& dirPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(dirPath)) return;

        // 收集文件
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (!entry.is_directory())
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        const float thumbnailSize = 80.0f;
        const float padding = 8.0f;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>(panelWidth / (thumbnailSize + padding)));

        ImGui::Columns(columns, nullptr, false);

        for (const auto& file : files) {
            std::string filename = file.filename().string();
            std::string ext = file.extension().string();
            std::string fullPath = file.string();

            ImGui::PushID(filename.c_str());
            ImGui::BeginGroup();

            // 文件图标按钮
            ImVec2 btnSize(thumbnailSize - padding, thumbnailSize - padding);
            const char* icon = GetFileIcon(ext);
            std::string btnLabel = std::string(icon) + "\n" + filename;
            if (ImGui::Button(btnLabel.c_str(), btnSize)) {
                m_SelectedFile = fullPath;
            }

            // 拖拽源
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("ASSET_BROWSER_ITEM", fullPath.c_str(),
                                          fullPath.size() + 1);
                ImGui::Text("%s %s", icon, filename.c_str());
                ImGui::EndDragDropSource();
            }

            // 双击打开
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_SelectedFile = fullPath;
                if (m_FileOpenCallback)
                    m_FileOpenCallback(fullPath);
            }

            // 文件名（限制宽度）
            ImGui::TextWrapped("%s", filename.c_str());

            ImGui::EndGroup();
            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

} // namespace Engine