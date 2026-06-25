#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Core/Log.h"
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <cstdlib>

namespace Engine {

    AssetBrowserPanel::AssetBrowserPanel() = default;

    void AssetBrowserPanel::Init(EditorAssetDatabase* db, DependencyTracker* depTracker) {
        m_Database = db;
        m_DepTracker = depTracker;

        if (m_Database) {
            AssetDatabaseCallbacks cbs;
            cbs.onScanCompleted = [this]() { m_NeedRefresh = true; };
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
        if (m_NeedRefresh) Refresh();
    }

    void AssetBrowserPanel::RefreshDirectories() {
        if (!m_Database) return;
        m_Directories = m_Database->GetAllDirectories();
        if (m_Directories.empty()) m_Directories.push_back("Assets");
    }

    void AssetBrowserPanel::RefreshEntries() {
        if (!m_Database) return;
        m_Entries.clear();

        std::vector<GUID> assets;
        if (m_CurrentDir.empty() || m_CurrentDir == "Assets")
            assets = m_Database->GetAllAssets();
        else
            assets = m_Database->GetAssetsInDirectory(m_CurrentDir);

        for (const auto& guid : assets) {
            auto entry = MakeEntry(guid);
            if (PassesFilter(entry)) m_Entries.push_back(entry);
        }

        std::sort(m_Entries.begin(), m_Entries.end(),
                  [](const AssetBrowserEntry& a, const AssetBrowserEntry& b) {
                      if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
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
        if (!m_SearchText.empty()) {
            std::string search = m_SearchText;
            std::string name = entry.displayName;
            std::transform(search.begin(), search.end(), search.begin(), ::tolower);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(search) == std::string::npos) return false;
        }
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

    void AssetBrowserPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin("Asset Browser", &m_Visible);

        DrawToolbar();
        DrawSearchBar();

        if (m_ShowFilterBar) DrawFilterBar();

        ImGui::Separator();
        ImGui::Columns(2, "##AssetBrowserColumns", false);
        ImGui::SetColumnWidth(0, 200.0f);
        DrawDirectoryTree();
        ImGui::NextColumn();
        DrawAssetGrid();
        ImGui::Columns(1);
        DrawStatusBar();

        if (m_ShowImportSettings) DrawImportSettingsPanel(m_SelectedForImport);
        if (m_ShowDependencyGraph) DrawDependencyGraph(m_SelectedForDepGraph);
        if (m_ShowCollectionPanel) DrawCollectionPanel();

        ImGui::End();
    }

    void AssetBrowserPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        if (ImGui::Button("<-", ImVec2(28, 0))) {
            if (m_CurrentDir != "Assets") {
                auto pos = m_CurrentDir.find_last_of('/');
                m_CurrentDir = (pos != std::string::npos) ? m_CurrentDir.substr(0, pos) : "Assets";
                Refresh();
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("Refresh", ImVec2(60, 0))) {
            if (m_Database) m_Database->ScanSync();
            Refresh();
        }
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        if (ImGui::Button("Grid", ImVec2(40, 0))) m_ViewMode = ViewMode::Grid;
        ImGui::SameLine();
        if (ImGui::Button("List", ImVec2(40, 0))) m_ViewMode = ViewMode::List;
        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##size", &m_IconSize, 32, 128, "%.0f");
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        if (ImGui::Button("Filter", ImVec2(50, 0))) m_ShowFilterBar = !m_ShowFilterBar;
        ImGui::SameLine();
        if (ImGui::Button("Collections", ImVec2(80, 0))) m_ShowCollectionPanel = !m_ShowCollectionPanel;

        ImGui::PopStyleVar();
    }

    void AssetBrowserPanel::DrawSearchBar() {
        ImGui::PushItemWidth(-1);
        char searchBuf[256];
        std::strncpy(searchBuf, m_SearchText.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
        if (ImGui::InputText("##search", searchBuf, sizeof(searchBuf))) {
            m_SearchText = searchBuf;
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Search by name");
            ImGui::EndTooltip();
        }
    }

    void AssetBrowserPanel::DrawFilterBar() {
        ImGui::Text("Type:");
        ImGui::SameLine();
        ImGui::Checkbox("Texture", &m_FilterTextures); ImGui::SameLine();
        ImGui::Checkbox("Audio", &m_FilterAudio); ImGui::SameLine();
        ImGui::Checkbox("Shader", &m_FilterShaders); ImGui::SameLine();
        ImGui::Checkbox("Scene", &m_FilterScenes); ImGui::SameLine();
        ImGui::Checkbox("Font", &m_FilterFonts); ImGui::SameLine();
        ImGui::Checkbox("Other", &m_FilterOthers);
    }

    void AssetBrowserPanel::DrawDirectoryTree() {
        ImGui::BeginChild("##DirTree", ImVec2(0, 0), true);

        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (m_CurrentDir == "Assets") rootFlags |= ImGuiTreeNodeFlags_Selected;

        bool rootOpen = ImGui::TreeNodeEx("Assets", rootFlags);
        if (ImGui::IsItemClicked()) { m_CurrentDir = "Assets"; Refresh(); }

        if (rootOpen) {
            for (const auto& dir : m_Directories) {
                if (dir == "Assets") continue;
                if (!dir.starts_with(m_CurrentDir != "Assets" ? m_CurrentDir : "")) continue;

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf;
                if (dir == m_CurrentDir) nodeFlags |= ImGuiTreeNodeFlags_Selected;

                std::string displayName = std::filesystem::path(dir).filename().string();
                if (displayName.empty()) displayName = dir;

                bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), nodeFlags);
                if (ImGui::IsItemClicked()) { m_CurrentDir = dir; Refresh(); }
                if (nodeOpen) ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawAssetGrid() {
        ImGui::BeginChild("##AssetGrid", ImVec2(0, 0), true);

        if (m_ViewMode == ViewMode::List) {
            DrawListView();
        } else {
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columns = std::max(1, static_cast<int>(panelWidth / (m_IconSize + 20)));
            m_GridColumns = columns;
            ImGui::Columns(columns, "##GridColumns", false);

            for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i) {
                DrawAssetTile(m_Entries[i], i);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }
        ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawAssetTile(const AssetBrowserEntry& entry, int index) {
        ImGui::PushID(index);
        const char* icon = GetFileTypeIcon(entry.type);
        bool isSelected = (m_SelectedEntryIndex == index);

        if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 0.5f));

        ImVec2 iconSize = ImVec2(m_IconSize, m_IconSize);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

        if (ImGui::Button((icon + std::string("\n") + entry.displayName).c_str(), iconSize)) {
            m_SelectedEntryIndex = index;
            m_SelectedGUID = entry.guid;
            if (m_OnAssetSelect) m_OnAssetSelect(entry.guid, entry.physicalPath);
        }

        ImGui::PopStyleVar();
        if (isSelected) ImGui::PopStyleColor();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ASSET", entry.physicalPath.c_str(), entry.physicalPath.size() + 1);
            ImGui::Text("%s %s", icon, entry.displayName.c_str());
            if (m_OnAssetDrag) m_OnAssetDrag(entry.guid, entry.physicalPath);
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem()) { DrawContextMenu(entry); ImGui::EndPopup(); }

        if (entry.vcStatus != AssetVCStatus::None) {
            ImVec2 pos = ImGui::GetItemRectMin();
            ImU32 statusColor = IM_COL32_WHITE;
            const char* statusIcon = "";
            switch (entry.vcStatus) {
                case AssetVCStatus::CheckedOut: statusIcon = "V"; statusColor = IM_COL32(0, 150, 255, 255); break;
                case AssetVCStatus::LockedByOther: statusIcon = "X"; statusColor = IM_COL32(255, 50, 50, 255); break;
                case AssetVCStatus::Added: statusIcon = "+"; statusColor = IM_COL32(0, 200, 0, 255); break;
                case AssetVCStatus::Conflict: statusIcon = "!"; statusColor = IM_COL32(255, 200, 0, 255); break;
                default: break;
            }
            ImGui::GetWindowDrawList()->AddText(pos, statusColor, statusIcon);
        }
        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawListView() {
        ImGui::Columns(4, "##ListColumns");
        ImGui::Text("Name"); ImGui::NextColumn();
        ImGui::Text("Type"); ImGui::NextColumn();
        ImGui::Text("Size"); ImGui::NextColumn();
        ImGui::Text("Modified"); ImGui::NextColumn();
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i) {
            const auto& entry = m_Entries[i];
            bool isSelected = (m_SelectedEntryIndex == i);

            ImGui::PushID(i);
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));

            if (ImGui::Selectable(entry.displayName.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_SelectedEntryIndex = i;
                m_SelectedGUID = entry.guid;
                if (m_OnAssetSelect) m_OnAssetSelect(entry.guid, entry.physicalPath);
            }

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET", entry.physicalPath.c_str(), entry.physicalPath.size() + 1);
                ImGui::TextUnformatted(entry.displayName.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginPopupContextItem()) { DrawContextMenu(entry); ImGui::EndPopup(); }

            if (isSelected) ImGui::PopStyleColor();
            ImGui::NextColumn();
            ImGui::TextUnformatted(AssetTypeName(entry.type));
            ImGui::NextColumn();
            ImGui::TextUnformatted(FormatFileSize(entry.fileSize).c_str());
            ImGui::NextColumn();
            ImGui::TextUnformatted(FormatTime(entry.lastModified).c_str());
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    void AssetBrowserPanel::DrawContextMenu(const AssetBrowserEntry& entry) {
        if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
            std::string cmd = "explorer /select,\"" + entry.physicalPath + "\"";
            std::system(cmd.c_str());
#endif
        }
        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(entry.physicalPath.c_str());
        if (ImGui::MenuItem("Copy GUID")) ImGui::SetClipboardText(entry.guid.ToString().c_str());
        ImGui::Separator();

        if (ImGui::MenuItem("Import Settings...")) { m_SelectedForImport = entry; m_ShowImportSettings = true; }
        if (ImGui::MenuItem("Reimport")) { if (m_Database) m_Database->Reimport(entry.guid); }
        ImGui::Separator();

        if (ImGui::BeginMenu("Dependencies")) {
            if (ImGui::MenuItem("Show Dependency Graph")) {
                m_SelectedForDepGraph = entry;
                m_ShowDependencyGraph = true;
            }
            if (ImGui::MenuItem("Select Dependencies")) { /* TODO */ }
            if (ImGui::MenuItem("Select Users")) { /* TODO */ }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) {
            if (m_DepTracker) {
                auto check = m_DepTracker->CheckBeforeDelete(entry.guid);
                if (!check.canDelete)
                    Log::Info("[AssetBrowser] Cannot delete: still in use");
            }
        }
    }

    void AssetBrowserPanel::DrawImportSettingsPanel(const AssetBrowserEntry& entry) {
        if (!ImGui::Begin("Import Settings", &m_ShowImportSettings, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End(); return;
        }
        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::Separator();

        auto* meta = m_Database ? m_Database->GetMetaMutable(entry.guid) : nullptr;
        if (!meta) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No metadata");
            ImGui::End(); return;
        }

        if (meta->type == AssetType::Texture) {
            ImGui::Text("Texture Import Settings");
            ImGui::Separator();
            auto& ts = meta->textureSettings;
            int format = static_cast<int>(ts.format);
            const char* formats[] = { "RGBA32", "RGB24", "BC1", "BC3", "BC5", "BC7", "ASTC" };
            if (ImGui::Combo("Format", &format, formats, IM_ARRAYSIZE(formats)))
                ts.format = static_cast<TextureImportSettings::Format>(format);
            ImGui::Checkbox("sRGB", &ts.sRGB);
            ImGui::Checkbox("Generate MipMaps", &ts.generateMipMaps);
            ImGui::Checkbox("Is Normal Map", &ts.isNormalMap);
            ImGui::DragInt("Max Resolution", &ts.maxResolution, 1, 32, 16384);
        } else if (meta->type == AssetType::AudioClip) {
            ImGui::Text("Audio Import Settings");
            ImGui::Separator();
            auto& as = meta->audioSettings;
            int compression = static_cast<int>(as.compression);
            const char* compressions[] = { "PCM", "ADPCM", "Vorbis", "MP3" };
            if (ImGui::Combo("Compression", &compression, compressions, IM_ARRAYSIZE(compressions)))
                as.compression = static_cast<AudioImportSettings::Compression>(compression);
            ImGui::SliderFloat("Quality", &as.quality, 0.0f, 1.0f);
            ImGui::Checkbox("Stream from Disk", &as.streamFromDisk);
        }

        if (ImGui::Button("Apply")) {
            if (m_Database) m_Database->Reimport(entry.guid);
            m_ShowImportSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) m_ShowImportSettings = false;
        ImGui::End();
    }

    void AssetBrowserPanel::DrawDependencyGraph(const AssetBrowserEntry& entry) {
        if (!ImGui::Begin("Dependency Graph", &m_ShowDependencyGraph, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End(); return;
        }
        ImGui::TextUnformatted(entry.displayName.c_str());
        ImGui::Separator();

        if (!m_DepTracker) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No dependency tracker");
            ImGui::End(); return;
        }

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

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1), "Used By:");
        auto users = m_DepTracker->GetDirectUsers(entry.guid);
        if (users.empty()) {
            ImGui::TextDisabled("(none - unused)");
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
        if (!ImGui::Begin("Collections", &m_ShowCollectionPanel)) { ImGui::End(); return; }
        static char newName[128] = {};
        ImGui::InputText("##newcol", newName, sizeof(newName));
        ImGui::SameLine();
        if (ImGui::Button("Create")) {
            if (newName[0]) newName[0] = '\0';
        }
        ImGui::Separator();
        ImGui::TextDisabled("(Collections - coming soon)");
        ImGui::End();
    }

    void AssetBrowserPanel::DrawStatusBar() {
        ImGui::Separator();
        ImGui::TextDisabled("%zu assets | %s | %.0f", m_Entries.size(), m_CurrentDir.c_str(), m_IconSize);
    }

    std::string AssetBrowserPanel::FormatFileSize(int64_t bytes) const {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
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
            case AssetType::Texture:     return "Tex";
            case AssetType::Shader:      return "Shd";
            case AssetType::AudioClip:   return "Aud";
            case AssetType::Font:        return "Fnt";
            case AssetType::Scene:       return "Scn";
            case AssetType::Model:       return "Mdl";
            case AssetType::Material:    return "Mat";
            case AssetType::Prefab:      return "Prf";
            case AssetType::Script:      return "Scr";
            case AssetType::AnimationClip: return "Anm";
            default:                     return "File";
        }
    }

} // namespace Engine