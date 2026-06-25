#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Core/Log.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>

namespace Engine {

    AssetBrowserPanel::AssetBrowserPanel() = default;

    void AssetBrowserPanel::Init(AssetDatabase* db, DependencyTracker* depTracker) {
        m_Database = db;
        m_DepTracker = depTracker;

        // 注册回调：数据库扫描完成后刷新
        if (m_Database) {
            AssetDatabaseCallbacks cbs;
            cbs.onScanCompleted = [this]() {
                m_NeedRefresh = true;
            };
            m_Database->SetCallbacks(cbs);
        }

        Refresh();
    }

    void AssetBrowserPanel::Refresh() {
        if (!m_Database) return;
        RefreshDirectories();
        RefreshEntries();
        m_NeedRefresh = false;
    }

    void AssetBrowserPanel::OnUpdate(float dt) {
        if (m_NeedRefresh) {
            Refresh();
        }
    }

    void AssetBrowserPanel::RefreshDirectories() {
        if (!m_Database) return;
        m_Directories = m_Database->GetAllDirectories();
        // 确保 Assets 根目录存在
        if (m_Directories.empty()) {
            m_Directories.push_back("Assets");
        }
    }

    void AssetBrowserPanel::RefreshEntries() {
        if (!m_Database) return;
        m_Entries.clear();

        std::vector<GUID> assets;
        if (m_CurrentDir.empty() || m_CurrentDir == "Assets") {
            assets = m_Database->GetAllAssets();
        } else {
            assets = m_Database->GetAssetsInDirectory(m_CurrentDir);
        }

        for (const auto& guid : assets) {
            auto entry = MakeEntry(guid);
            if (PassesFilter(entry)) {
                m_Entries.push_back(entry);
            }
        }

        // 排序：目录优先，然后按文件名
        std::sort(m_Entries.begin(), m_Entries.end(),
                  [](const AssetBrowserEntry& a, const AssetBrowserEntry& b) {
                      if (a.isDirectory != b.isDirectory)
                          return a.isDirectory > b.isDirectory;
                      return a.displayName < b.displayName;
                  });
    }

    AssetBrowserEntry AssetBrowserPanel::MakeEntry(const GUID& guid) {
        AssetBrowserEntry entry;
        entry.guid = guid;

        const auto* meta = m_Database ? m_Database->GetMeta(guid) : nullptr;
        if (meta) {
            entry.displayName = std::filesystem::path(meta->originalPath).filename().string();
            entry.virtualPath = meta->virtualPath;
            entry.physicalPath = meta->originalPath;
            entry.type = meta->type;
            entry.fileSize = meta->fileSize;
            entry.lastModified = meta->lastModifiedTime;
            entry.isDirectory = meta->isDirectory;
            entry.hasMetaFile = true;
        }

        return entry;
    }

    bool AssetBrowserPanel::PassesFilter(const AssetBrowserEntry& entry) const {
        // 搜索文本过滤
        if (!m_SearchText.empty()) {
            std::string search = m_SearchText;
            std::string name = entry.displayName;
            std::transform(search.begin(), search.end(), search.begin(), ::tolower);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(search) == std::string::npos) return false;
        }

        // 类型过滤
        if (m_SearchFilter.typeFilter != AssetType::Unknown) {
            if (entry.type != m_SearchFilter.typeFilter) return false;
        }

        // 收藏夹过滤
        if (m_SearchFilter.favoritesOnly && !entry.isFavorite) return false;

        // 文件大小过滤
        if (entry.fileSize < m_SearchFilter.minFileSize) return false;
        if (m_SearchFilter.maxFileSize > 0 && entry.fileSize > m_SearchFilter.maxFileSize) return false;

        // 时间过滤
        if (entry.lastModified < m_SearchFilter.modifiedAfter) return false;
        if (m_SearchFilter.modifiedBefore < INT64_MAX && entry.lastModified > m_SearchFilter.modifiedBefore) return false;

        return true;
    }

    void AssetBrowserPanel::NavigateTo(const std::string& virtualPath) {
        m_CurrentDir = virtualPath;
        Refresh();
    }

    void AssetBrowserPanel::SelectAsset(const GUID& guid) {
        m_SelectedGUID = guid;
        if (m_OnAssetSelect) {
            std::string path = m_Database ? m_Database->GetPhysicalPath(guid) : "";
            m_OnAssetSelect(guid, path);
        }
    }

    // ============================================================
    // UI 绘制
    // ============================================================

    void AssetBrowserPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin("Asset Browser", &m_Visible);

        // ── 工具栏 ──
        DrawToolbar();

        // ── 搜索栏 ──
        DrawSearchBar();

        // ── 高级过滤栏 ──
        if (m_ShowFilterBar) {
            DrawFilterBar();
        }

        ImGui::Separator();

        // ── 主内容区（分割为左右两栏） ──
        ImGui::Columns(2, "##AssetBrowserColumns", false);
        ImGui::SetColumnWidth(0, 200.0f);

        // 左栏：目录树
        DrawDirectoryTree();

        ImGui::NextColumn();

        // 右栏：资产网格/列表
        DrawAssetGrid();

        ImGui::Columns(1);

        // ── 状态栏 ──
        DrawStatusBar();

        // ── 弹出面板 ──
        if (m_ShowImportSettings) {
            DrawImportSettingsPanel(m_SelectedForImport);
        }
        if (m_ShowDependencyGraph) {
            DrawDependencyGraph(m_SelectedForDepGraph);
        }
        if (m_ShowCollectionPanel) {
            DrawCollectionPanel();
        }

        ImGui::End();
    }

    void AssetBrowserPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        // 返回上级目录
        if (ImGui::Button("<-", ImVec2(28, 0))) {
            if (m_CurrentDir != "Assets") {
                auto pos = m_CurrentDir.find_last_of('/');
                if (pos != std::string::npos) {
                    m_CurrentDir = m_CurrentDir.substr(0, pos);
                } else {
                    m_CurrentDir = "Assets";
                }
                Refresh();
            }
        }
        ImGui::SameLine();

        // 刷新按钮
        if (ImGui::Button("Refresh", ImVec2(60, 0))) {
            if (m_Database) m_Database->ScanSync();
            Refresh();
        }
        ImGui::SameLine();

        // 视图切换
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button("Grid", m_ViewMode == ViewMode::Grid ? ImVec2(40, 0) : ImVec2(40, 0))) {
            m_ViewMode = ViewMode::Grid;
        }
        ImGui::SameLine();
        if (ImGui::Button("List", m_ViewMode == ViewMode::List ? ImVec2(40, 0) : ImVec2(40, 0))) {
            m_ViewMode = ViewMode::List;
        }
        ImGui::SameLine();

        // 图标大小滑块
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##iconsize", &m_IconSize, 32, 128, "%.0f");
        ImGui::SameLine();

        // 过滤按钮
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        if (ImGui::Button("Filter", m_ShowFilterBar ? ImVec2(50, 0) : ImVec2(50, 0))) {
            m_ShowFilterBar = !m_ShowFilterBar;
        }
        ImGui::SameLine();

        // 收藏集面板
        if (ImGui::Button("Collections", ImVec2(80, 0))) {
            m_ShowCollectionPanel = !m_ShowCollectionPanel;
        }

        ImGui::PopStyleVar();
    }

    void AssetBrowserPanel::DrawSearchBar() {
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Search assets (name:Rock t:Texture s>1024)...",
                                 &m_SearchText);
        ImGui::PopItemWidth();

        // 搜索语法提示
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Search syntax:");
            ImGui::BulletText("name:xxx  - Filter by name");
            ImGui::BulletText("t:Texture - Filter by type");
            ImGui::BulletText("s>1024    - File size > 1024 bytes");
            ImGui::EndTooltip();
        }
    }

    void AssetBrowserPanel::DrawFilterBar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        ImGui::Text("Type:");
        ImGui::SameLine();
        ImGui::Checkbox("Texture", &m_FilterTextures); ImGui::SameLine();
        ImGui::Checkbox("Audio",   &m_FilterAudio);    ImGui::SameLine();
        ImGui::Checkbox("Shader",  &m_FilterShaders);  ImGui::SameLine();
        ImGui::Checkbox("Scene",   &m_FilterScenes);   ImGui::SameLine();
        ImGui::Checkbox("Font",    &m_FilterFonts);    ImGui::SameLine();
        ImGui::Checkbox("Other",   &m_FilterOthers);

        ImGui::PopStyleVar();
    }

    void AssetBrowserPanel::DrawDirectoryTree() {
        ImGui::BeginChild("##DirTree", ImVec2(0, 0), ImGuiChildFlags_Borders);

        // 根目录
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_DefaultOpen;
        if (m_CurrentDir == "Assets") rootFlags |= ImGuiTreeNodeFlags_Selected;

        bool rootOpen = ImGui::TreeNodeEx("Assets", rootFlags);
        if (ImGui::IsItemClicked()) {
            m_CurrentDir = "Assets";
            Refresh();
        }

        // 子目录
        if (rootOpen) {
            for (const auto& dir : m_Directories) {
                if (dir == "Assets") continue;

                // 只显示当前目录的直接子目录
                std::string parentDir = m_CurrentDir != "Assets" ? m_CurrentDir : "";
                if (!dir.starts_with(parentDir)) continue;

                // 计算缩进级别
                std::string relative = dir.substr(7); // 去掉 "Assets/"
                size_t slashPos = relative.find('/');

                // 只显示第一级子目录（递归目录通过展开显示）
                bool isChild = (slashPos == std::string::npos);
                if (!isChild && !dir.starts_with(m_CurrentDir)) continue;

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf;
                if (dir == m_CurrentDir) nodeFlags |= ImGuiTreeNodeFlags_Selected;

                // 用节点路径的显示名
                std::string displayName = std::filesystem::path(dir).filename().string();
                if (displayName.empty()) displayName = dir;

                bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), nodeFlags);
                if (ImGui::IsItemClicked()) {
                    m_CurrentDir = dir;
                    Refresh();
                }
                if (nodeOpen) ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawAssetGrid() {
        ImGui::BeginChild("##AssetGrid", ImVec2(0, 0), ImGuiChildFlags_Borders);

        if (m_ViewMode == ViewMode::List) {
            DrawListView();
        } else {
            // Grid / LargeIcon 模式
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columns = std::max(1, static_cast<int>(panelWidth / (m_IconSize + 20)));
            m_GridColumns = columns;

            ImGui::Columns(columns, "##AssetGridColumns", false);

            for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i) {
                DrawAssetTile(m_Entries[i], i);
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
        }

        ImGui::EndChild();

        // 处理拖拽
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
                // 处理拖入
            }
            ImGui::EndDragDropTarget();
        }
    }

    void AssetBrowserPanel::DrawAssetTile(const AssetBrowserEntry& entry, int index) {
        ImGui::PushID(index);

        // 文件类型图标（用 emoji 简化）
        const char* icon = GetFileTypeIcon(entry.type);
        bool isSelected = (m_SelectedEntryIndex == index);

        // 背景色（选中状态）
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 0.5f));
        }

        // 图标按钮
        ImVec2 iconSize = ImVec2(m_IconSize, m_IconSize);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

        // 模拟缩略图（用文件类型图标填充）
        std::string btnLabel = icon + std::string("\n") + entry.displayName;
        if (ImGui::Button(btnLabel.c_str(), iconSize)) {
            m_SelectedEntryIndex = index;
            m_SelectedGUID = entry.guid;
            if (m_OnAssetSelect) {
                m_OnAssetSelect(entry.guid, entry.physicalPath);
            }
        }

        ImGui::PopStyleVar();

        if (isSelected) {
            ImGui::PopStyleColor();
        }

        // 拖拽源
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ASSET", entry.physicalPath.c_str(),
                                       entry.physicalPath.size() + 1);
            ImGui::Text("%s %s", icon, entry.displayName.c_str());
            if (m_OnAssetDrag) {
                m_OnAssetDrag(entry.guid, entry.physicalPath);
            }
            ImGui::EndDragDropSource();
        }

        // 右键菜单
        if (ImGui::BeginPopupContextItem()) {
            DrawContextMenu(entry);
            ImGui::EndPopup();
        }

        // 源控制状态图标
        if (entry.vcStatus != AssetVCStatus::None) {
            ImVec2 pos = ImGui::GetItemRectMin();
            const char* statusIcon = "";
            ImU32 statusColor = IM_COL32_WHITE;
            switch (entry.vcStatus) {
                case AssetVCStatus::CheckedOut:   statusIcon = "\xE2\x9C\x93"; statusColor = IM_COL32(0, 150, 255, 255); break;  // ✓ blue
                case AssetVCStatus::LockedByOther: statusIcon = "\xE2\x9C\x97"; statusColor = IM_COL32(255, 50, 50, 255); break; // ✗ red
                case AssetVCStatus::Added:         statusIcon = "+";             statusColor = IM_COL32(0, 200, 0, 255); break;
                case AssetVCStatus::Conflict:      statusIcon = "!";             statusColor = IM_COL32(255, 200, 0, 255); break;
                default: break;
            }
            ImGui::GetWindowDrawList()->AddText(pos, statusColor, statusIcon);
        }

        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawListView() {
        // 表头
        ImGui::Columns(4, "##ListColumns");
        ImGui::Text("Name"); ImGui::NextColumn();
        ImGui::Text("Type"); ImGui::NextColumn();
        ImGui::Text("Size"); ImGui::NextColumn();
        ImGui::Text("Modified"); ImGui::NextColumn();
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i) {
            const auto& entry = m_Entries[i];
            bool isSelected = (m_SelectedEntryIndex == i);

            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            }

            ImGui::PushID(i);
            ImGui::Selectable(entry.displayName.c_str(), isSelected,
                              ImGuiSelectableFlags_SpanAllColumns);

            if (ImGui::IsItemClicked()) {
                m_SelectedEntryIndex = i;
                m_SelectedGUID = entry.guid;
                if (m_OnAssetSelect) {
                    m_OnAssetSelect(entry.guid, entry.physicalPath);
                }
            }

            // 拖拽
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET", entry.physicalPath.c_str(),
                                           entry.physicalPath.size() + 1);
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            // 右键菜单
            if (ImGui::BeginPopupContextItem()) {
                DrawContextMenu(entry);
                ImGui::EndPopup();
            }

            ImGui::NextColumn();
            ImGui::TextUnformatted(AssetTypeName(entry.type));
            ImGui::NextColumn();
            ImGui::TextUnformatted(FormatFileSize(entry.fileSize).c_str());
            ImGui::NextColumn();
            ImGui::TextUnformatted(FormatTime(entry.lastModified).c_str());
            ImGui::NextColumn();

            if (isSelected) ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    void AssetBrowserPanel::DrawContextMenu(const AssetBrowserEntry& entry) {
        if (ImGui::MenuItem("Show in Explorer")) {
            std::string cmd = "explorer /select,\"" + entry.physicalPath + "\"";
            std::system(cmd.c_str());
        }
        if (ImGui::MenuItem("Copy Path")) {
            ImGui::SetClipboardText(entry.physicalPath.c_str());
        }
        if (ImGui::MenuItem("Copy GUID")) {
            ImGui::SetClipboardText(entry.guid.ToString().c_str());
        }
        ImGui::Separator();

        // 导入设置
        if (ImGui::MenuItem("Import Settings...")) {
            m_SelectedForImport = entry;
            m_ShowImportSettings = true;
        }
        if (ImGui::MenuItem("Reimport")) {
            if (m_Database) m_Database->Reimport(entry.guid);
        }

        ImGui::Separator();

        // 依赖关系
        if (ImGui::BeginMenu("Dependencies")) {
            if (ImGui::MenuItem("Show Dependency Graph")) {
                m_SelectedForDepGraph = entry;
                m_ShowDependencyGraph = true;
            }
            if (ImGui::MenuItem("Select Dependencies")) {
                if (m_DepTracker) {
                    auto deps = m_DepTracker->GetDirectDependencies(entry.guid);
                    // TODO: highlight in browser
                }
            }
            if (ImGui::MenuItem("Select Users")) {
                if (m_DepTracker) {
                    auto users = m_DepTracker->GetDirectUsers(entry.guid);
                    // TODO: highlight in browser
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // 收藏
        if (ImGui::MenuItem("Add to Collection")) {
            // TODO: show collection picker
        }

        // 删除
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            if (m_DepTracker) {
                auto check = m_DepTracker->CheckBeforeDelete(entry.guid);
                if (!check.canDelete) {
                    Log::Warning("[AssetBrowser] Cannot delete asset: Still in use");
                } else {
                    // TODO: move to trash
                }
            }
        }
    }

    void AssetBrowserPanel::DrawImportSettingsPanel(const AssetBrowserEntry& entry) {
        if (!ImGui::Begin("Import Settings", &m_ShowImportSettings,
                          ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::Separator();

        auto* meta = m_Database ? m_Database->GetMetaMutable(entry.guid) : nullptr;
        if (!meta) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No metadata available");
            ImGui::End();
            return;
        }

        if (meta->type == AssetType::Texture) {
            ImGui::Text("Texture Import Settings");
            ImGui::Separator();

            auto& ts = meta->textureSettings;
            int format = static_cast<int>(ts.format);
            const char* formats[] = { "RGBA32", "RGB24", "BC1", "BC3", "BC5", "BC7", "ASTC" };
            if (ImGui::Combo("Format", &format, formats, IM_ARRAYSIZE(formats))) {
                ts.format = static_cast<TextureImportSettings::Format>(format);
            }

            ImGui::Checkbox("sRGB", &ts.sRGB);
            ImGui::Checkbox("Generate MipMaps", &ts.generateMipMaps);
            ImGui::Checkbox("Is Normal Map", &ts.isNormalMap);
            ImGui::Checkbox("Is Sprite", &ts.isSprite);
            ImGui::DragInt("Max Resolution", &ts.maxResolution, 1, 32, 16384);

        } else if (meta->type == AssetType::AudioClip) {
            ImGui::Text("Audio Import Settings");
            ImGui::Separator();

            auto& as = meta->audioSettings;
            int compression = static_cast<int>(as.compression);
            const char* compressions[] = { "PCM", "ADPCM", "Vorbis", "MP3" };
            if (ImGui::Combo("Compression", &compression, compressions, IM_ARRAYSIZE(compressions))) {
                as.compression = static_cast<AudioImportSettings::Compression>(compression);
            }
            ImGui::SliderFloat("Quality", &as.quality, 0.0f, 1.0f);
            ImGui::Checkbox("Stream from Disk", &as.streamFromDisk);
            ImGui::Checkbox("Normalize", &as.normalize);
        }

        if (ImGui::Button("Apply")) {
            if (m_Database) {
                m_Database->Reimport(entry.guid);
            }
            m_ShowImportSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ShowImportSettings = false;
        }

        ImGui::End();
    }

    void AssetBrowserPanel::DrawDependencyGraph(const AssetBrowserEntry& entry) {
        if (!ImGui::Begin("Dependency Graph", &m_ShowDependencyGraph,
                          ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::Separator();

        if (!m_DepTracker) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Dependency tracker not available");
            ImGui::End();
            return;
        }

        // Depends On
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "Depends On:");
        auto deps = m_DepTracker->GetDirectDependencies(entry.guid);
        if (deps.empty()) {
            ImGui::TextDisabled("(none)");
        } else {
            ImGui::Indent();
            for (const auto& dep : deps) {
                std::string depPath = m_Database ? m_Database->GetPhysicalPath(dep) : dep.ToString();
                ImGui::TextUnformatted(depPath.c_str());
            }
            ImGui::Unindent();
        }

        ImGui::Separator();

        // Used By
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1), "Used By:");
        auto users = m_DepTracker->GetDirectUsers(entry.guid);
        if (users.empty()) {
            ImGui::TextDisabled("(none - unused asset)");
        } else {
            ImGui::Indent();
            for (const auto& user : users) {
                std::string userPath = m_Database ? m_Database->GetPhysicalPath(user) : user.ToString();
                ImGui::TextUnformatted(userPath.c_str());
            }
            ImGui::Unindent();
        }

        ImGui::End();
    }

    void AssetBrowserPanel::DrawCollectionPanel() {
        if (!ImGui::Begin("Collections", &m_ShowCollectionPanel)) {
            ImGui::End();
            return;
        }

        // 新建收藏集
        static char newCollectionName[128] = {};
        ImGui::InputTextWithHint("##newcol", "New collection name...",
                                 newCollectionName, sizeof(newCollectionName));
        ImGui::SameLine();
        if (ImGui::Button("Create")) {
            if (newCollectionName[0] && m_Database) {
                AssetCollection col;
                col.name = newCollectionName;
                // m_Database->AddCollection(col);
                newCollectionName[0] = '\0';
            }
        }

        ImGui::Separator();

        // 列出收藏集
        ImGui::TextDisabled("(Collections - coming soon)");

        ImGui::End();
    }

    void AssetBrowserPanel::DrawStatusBar() {
        ImGui::Separator();
        ImGui::TextDisabled("%zu assets | %s | Icon: %.0f",
                            m_Entries.size(),
                            m_CurrentDir.c_str(),
                            m_IconSize);
    }

    // ============================================================
    // 辅助函数
    // ============================================================

    std::string AssetBrowserPanel::FormatFileSize(int64_t bytes) const {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        if (bytes < 1024LL * 1024 * 1024) {
            return std::to_string(bytes / (1024 * 1024)) + " MB";
        }
        return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
    }

    std::string AssetBrowserPanel::FormatTime(int64_t timestamp) const {
        if (timestamp == 0) return "Unknown";
        time_t t = static_cast<time_t>(timestamp);
        struct tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return buf;
    }

    const char* AssetBrowserPanel::GetFileTypeIcon(AssetType type) const {
        switch (type) {
            case AssetType::Texture:     return "\xF0\x9F\x96\xBC"; // 🖼
            case AssetType::Shader:      return "\xF0\x9F\x93\x9D"; // 📝
            case AssetType::AudioClip:   return "\xF0\x9F\x8E\xB5"; // 🎵
            case AssetType::Font:        return "\xF0\x9F\x94\xA4"; // 🔤
            case AssetType::Scene:       return "\xF0\x9F\x8E\xAE"; // 🎮
            case AssetType::Model:       return "\xF0\x9F\x97\xBF"; // 🗿
            case AssetType::Material:    return "\xF0\x9F\x8E\xA8"; // 🎨
            case AssetType::Prefab:      return "\xF0\x9F\xA7\x8A"; // 🧊
            case AssetType::Script:      return "\xF0\x9F\x92\xBB"; // 💻
            case AssetType::AnimationClip: return "\xF0\x9F\x8E\xAC"; // 🎬
            default:                     return "\xF0\x9F\x93\x84"; // 📄
        }
    }

} // namespace Engine