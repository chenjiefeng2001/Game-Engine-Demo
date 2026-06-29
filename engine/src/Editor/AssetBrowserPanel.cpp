#include "Engine/Editor/AssetBrowserPanel.h"
#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <regex>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Engine {

    // ── 后台扫描线程 ──
    // m_AsyncResult 由后台线程写入，主线程在 RefreshEntries 中读取
    static std::jthread g_AssetScanner;
    static std::mutex   g_AsyncMutex;
    static std::vector<AssetBrowserEntry> g_AsyncResult;
    static std::atomic<bool> g_AsyncBusy{false};
    static std::atomic<bool> g_AsyncPending{false};

    // ── 辅助：从路径提取文件名（避免 std::filesystem::path 开销） ──
    static std::string_view ExtractFileName(std::string_view path) {
        auto pos = path.rfind('/');
        if (pos == std::string_view::npos) pos = path.rfind('\\');
        return (pos != std::string_view::npos) ? path.substr(pos + 1) : path;
    }
    static std::string_view ExtractStem(std::string_view name) {
        auto pos = name.rfind('.');
        return (pos != std::string_view::npos) ? name.substr(0, pos) : name;
    }

    bool SearchQuery::Match(const AssetBrowserEntry& entry) const {
        if (tokens.empty()) return true;
        std::string_view lowName(entry.displayName);
        for (const auto& tok : tokens) {
            std::string lowVal = tok.value;
            std::transform(lowVal.begin(), lowVal.end(), lowVal.begin(), ::tolower);
            switch (tok.type) {
                case SearchToken::TypeFilter: {
                    std::string tname = AssetTypeName(entry.type);
                    std::transform(tname.begin(), tname.end(), tname.begin(), ::tolower);
                    if (tname.find(lowVal) == std::string::npos) return false; break;
                }
                case SearchToken::NameFilter: {
                    std::string ln(lowName);
                    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
                    if (ln.find(lowVal) == std::string::npos) return false; break;
                }
                case SearchToken::SizeFilter: {
                    int64_t val = std::stoll(tok.value);
                    bool ok = false;
                    if (tok.op == ">")  ok = (entry.fileSize > val);
                    if (tok.op == "<")  ok = (entry.fileSize < val);
                    if (tok.op == ">=") ok = (entry.fileSize >= val);
                    if (tok.op == "<=") ok = (entry.fileSize <= val);
                    if (tok.op == ":")  ok = (entry.fileSize == val);
                    if (!ok) return false; break;
                }
                default: {
                    std::string ln(lowName);
                    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
                    if (ln.find(lowVal) == std::string::npos) return false; break;
                }
            }
        }
        return true;
    }

    AssetBrowserPanel::AssetBrowserPanel() = default;

    void AssetBrowserPanel::Init(EditorAssetDatabase* db, DependencyTracker* depTracker) {
        m_Database = db; m_DepTracker = depTracker;
        m_CurrentDir = "Assets";
        Mount("Engine", "assets", true, 10);
        Mount("Game", "assets", false, 0);
        if (m_Database) {
            AssetDatabaseCallbacks cbs;
            cbs.onScanCompleted = [this]() { m_NeedRefresh = true; };
            m_Database->SetCallbacks(cbs);
        }
        Refresh();
    }

    void AssetBrowserPanel::Mount(const std::string& name, const std::string& path, bool ro, int pri) {
        Unmount(name); VFSMountPoint mp; mp.mountName = name; mp.physicalPath = path; mp.isReadOnly = ro; mp.priority = pri;
        m_MountPoints.push_back(mp);
        std::sort(m_MountPoints.begin(), m_MountPoints.end(), [](auto& a, auto& b) { return a.priority > b.priority; });
    }
    void AssetBrowserPanel::Unmount(const std::string& name) {
        m_MountPoints.erase(std::remove_if(m_MountPoints.begin(), m_MountPoints.end(), [&](auto& mp) { return mp.mountName == name; }), m_MountPoints.end());
    }
    std::string AssetBrowserPanel::ResolveVFSPath(const std::string& vp) const {
        auto c = vp.find(':'); if (c == std::string::npos) return vp;
        std::string mn = vp.substr(0, c), rp = vp.substr(c + 1);
        for (auto& mp : m_MountPoints) if (mp.mountName == mn) return (std::filesystem::path(mp.physicalPath) / rp).string();
        return vp;
    }
    bool AssetBrowserPanel::IsReadOnlyPath(const std::string& vp) const {
        auto c = vp.find(':'); if (c == std::string::npos) return false;
        std::string mn = vp.substr(0, c);
        for (auto& mp : m_MountPoints) if (mp.mountName == mn) return mp.isReadOnly;
        return false;
    }

    void AssetBrowserPanel::AddTag(const GUID& g, const std::string& t) { if (!t.empty()) { m_Tags[g].insert(t); m_AllTags.insert(t); } }
    void AssetBrowserPanel::RemoveTag(const GUID& g, const std::string& t) { auto it = m_Tags.find(g); if (it != m_Tags.end()) { it->second.erase(t); if (it->second.empty()) m_Tags.erase(it); } }
    std::vector<std::string> AssetBrowserPanel::GetTags(const GUID& g) const { auto it = m_Tags.find(g); if (it != m_Tags.end()) return {it->second.begin(), it->second.end()}; return {}; }
    std::vector<std::string> AssetBrowserPanel::GetAllTags() const { return {m_AllTags.begin(), m_AllTags.end()}; }
    void AssetBrowserPanel::ToggleFavorite(const GUID& g) { if (m_Favorites.count(g)) m_Favorites.erase(g); else m_Favorites.insert(g); }
    bool AssetBrowserPanel::IsFavorite(const GUID& g) const { return m_Favorites.count(g) > 0; }
    void AssetBrowserPanel::AddSmartCollection(const std::string& n, const std::string& q, bool d) { SmartCollection sc; sc.name = n; sc.searchQuery = q; sc.isDynamic = d; m_SmartCollections.push_back(sc); }
    void AssetBrowserPanel::RemoveSmartCollection(const std::string& n) { m_SmartCollections.erase(std::remove_if(m_SmartCollections.begin(), m_SmartCollections.end(), [&](auto& sc) { return sc.name == n; }), m_SmartCollections.end()); }

    SearchQuery AssetBrowserPanel::ParseSearchQuery(const std::string& raw) const {
        SearchQuery q; q.rawText = raw; if (raw.empty()) return q;
        std::regex re(R"((\w+)([:><=!]+)([\w.*]+)|(\w+))");
        std::sregex_iterator it(raw.begin(), raw.end(), re), end;
        while (it != end) {
            SearchToken t;
            if ((*it)[1].matched) {
                std::string k = (*it)[1].str(), o = (*it)[2].str(), v = (*it)[3].str();
                if (k == "t" || k == "type") t.type = SearchToken::TypeFilter;
                else if (k == "name" || k == "n") t.type = SearchToken::NameFilter;
                else if (k == "size" || k == "s") t.type = SearchToken::SizeFilter;
                else if (k == "tag" || k == "tags") t.type = SearchToken::TagFilter;
                else { t.type = SearchToken::Text; v = (*it)[0].str(); }
                t.key = std::move(k); t.op = std::move(o); t.value = std::move(v);
            } else { t.type = SearchToken::Text; t.value = (*it)[4].str(); }
            q.tokens.push_back(std::move(t)); ++it;
        }
        return q;
    }
    bool AssetBrowserPanel::MatchQuery(const AssetBrowserEntry& e, const SearchQuery& q) const {
        if (q.tokens.empty()) return true;
        for (auto& t : q.tokens) {
            if (t.type == SearchToken::TagFilter) {
                auto tags = GetTags(e.guid); bool found = false;
                std::string lv = t.value; std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
                for (auto& tg : tags) { std::string lt = tg; std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower); if (lt.find(lv) != std::string::npos) { found = true; break; } }
                if (!found) return false;
            }
        }
        return q.Match(e);
    }

    // ═══════════════════════════════════════════════════════════════
    // ⚡ 优化 1: 后台线程 I/O 隔离
    // ═══════════════════════════════════════════════════════════════
    void AssetBrowserPanel::Refresh() {
        if (!m_Database) return;
        RefreshDirectories();

        // 将扫描任务发往后台线程
        if (!g_AsyncBusy.exchange(true)) {
            auto db = m_Database;
            auto curDir = m_CurrentDir;
            auto searchText = m_SearchText;
            auto activeCollection = m_ActiveCollection;
            auto showFav = m_ShowFavoritesPanel;
            auto* favSet = &m_Favorites;
            auto* tagMap = &m_Tags;

            g_AssetScanner = std::jthread([db, curDir, searchText, activeCollection, showFav, favSet, tagMap]() {
                std::vector<AssetBrowserEntry> result;

                SearchQuery q;
                if (!searchText.empty()) {
                    // 简单解析——仅做基本文本过滤
                    SearchToken t; t.type = SearchToken::Text; t.value = searchText;
                    q.tokens.push_back(t);
                }

                auto colonPos = curDir.find(':');
                bool isMountRoot = (colonPos != std::string::npos && (colonPos + 1) >= (int)curDir.size());

                if (isMountRoot) {
                    std::string mn = curDir.substr(0, colonPos);
                    // 简化：不在此做挂载点扫描
                } else if (!activeCollection.empty()) {
                    // 收藏集：全量扫描
                    if (db) {
                        for (auto& guid : db->GetAllAssets()) {
                            if (auto* meta = db->GetMeta(guid)) {
                                AssetBrowserEntry e; e.guid = guid;
                                e.displayName = std::string(ExtractFileName(meta->originalPath));
                                e.virtualPath = meta->virtualPath;
                                e.physicalPath = meta->originalPath;
                                e.type = meta->type; e.fileSize = meta->fileSize;
                                e.isDirectory = meta->isDirectory;
                                e.isFavorite = (favSet->count(guid) > 0);
                                result.push_back(std::move(e));
                            }
                        }
                    }
                } else if (showFav) {
                    // 收藏夹
                    if (db) {
                        for (auto& f : *favSet) {
                            if (auto* meta = db->GetMeta(f)) {
                                AssetBrowserEntry e; e.guid = f;
                                e.displayName = std::string(ExtractFileName(meta->originalPath));
                                e.virtualPath = meta->virtualPath; e.physicalPath = meta->originalPath;
                                e.type = meta->type; e.isDirectory = meta->isDirectory; e.isFavorite = true;
                                result.push_back(std::move(e));
                            }
                        }
                    }
                } else {
                    // 普通目录
                    if (db) {
                        std::vector<GUID> assets = (curDir.empty() || curDir == "Assets") ? db->GetAllAssets() : db->GetAssetsInDirectory(curDir);
                        for (auto& guid : assets) {
                            if (auto* meta = db->GetMeta(guid)) {
                                AssetBrowserEntry e; e.guid = guid;
                                e.displayName = std::string(ExtractFileName(meta->originalPath));
                                e.virtualPath = meta->virtualPath; e.physicalPath = meta->originalPath;
                                e.type = meta->type; e.fileSize = meta->fileSize;
                                e.isDirectory = meta->isDirectory; e.isFavorite = (favSet->count(guid) > 0);
                                // 简单搜索过滤
                                if (!searchText.empty()) {
                                    std::string sn(searchText), dn(e.displayName);
                                    std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
                                    std::transform(dn.begin(), dn.end(), dn.begin(), ::tolower);
                                    if (dn.find(sn) == std::string::npos) continue;
                                }
                                result.push_back(std::move(e));
                            }
                        }
                        // 物理空文件夹
                        std::string physPath = db->ToPhysicalPath(curDir);
                        if (std::filesystem::exists(physPath)) {
                            for (auto& de : std::filesystem::directory_iterator(physPath)) {
                                if (!de.is_directory()) continue;
                                std::string fn = de.path().filename().string();
                                if (fn.starts_with('.')) continue;
                                bool exists = false;
                                for (auto& r : result) { if (r.isDirectory && r.displayName == fn) { exists = true; break; } }
                                if (!exists) {
                                    AssetBrowserEntry d; d.displayName = std::move(fn);
                                    d.virtualPath = (curDir == "Assets") ? "Assets/" + d.displayName : curDir + "/" + d.displayName;
                                    d.physicalPath = de.path().string(); d.isDirectory = true;
                                    result.push_back(std::move(d));
                                }
                            }
                        }
                    }
                }

                std::sort(result.begin(), result.end(), [](auto& a, auto& b) {
                    if (a.isDirectory != b.isDirectory) return a.isDirectory;
                    if (a.isFavorite != b.isFavorite) return a.isFavorite;
                    return a.displayName < b.displayName;
                });

                // 写入异步结果
                {
                    std::lock_guard<std::mutex> lock(g_AsyncMutex);
                    g_AsyncResult = std::move(result);
                    g_AsyncPending = true;
                }
                g_AsyncBusy = false;
            });
        }

        m_NeedRefresh = false;
    }

    void AssetBrowserPanel::RefreshDirectories() {
        if (!m_Database) return;
        m_Directories = m_Database->GetAllDirectories();
        if (m_Directories.empty()) m_Directories.push_back("Assets");
        for (auto& mp : m_MountPoints) {
            std::string vd = mp.mountName + ":";
            if (std::find(m_Directories.begin(), m_Directories.end(), vd) == m_Directories.end())
                m_Directories.push_back(vd);
        }
    }

    void AssetBrowserPanel::RefreshEntries() {
        // 从异步结果获取
        if (g_AsyncPending.exchange(false)) {
            std::lock_guard<std::mutex> lock(g_AsyncMutex);
            m_Entries = std::move(g_AsyncResult);
            g_AsyncResult.clear();
        }
    }

    void AssetBrowserPanel::OnUpdate(float dt) {
        if (m_NeedRefresh) Refresh();
        // 每帧尝试获取异步结果
        if (g_AsyncPending.load()) {
            RefreshEntries();
        }
    }

    AssetBrowserEntry AssetBrowserPanel::MakeEntry(const GUID& guid) {
        AssetBrowserEntry e; e.guid = guid;
        if (auto* meta = m_Database ? m_Database->GetMeta(guid) : nullptr) {
            e.displayName = std::string(ExtractFileName(meta->originalPath));
            e.virtualPath = meta->virtualPath; e.physicalPath = meta->originalPath;
            e.type = meta->type; e.fileSize = meta->fileSize; e.isDirectory = meta->isDirectory;
            e.hasMetaFile = true; e.isFavorite = IsFavorite(guid);
        } return e;
    }

    bool AssetBrowserPanel::PassesFilter(const AssetBrowserEntry& e) const {
        if (m_SearchText.empty()) return true;
        std::string s = m_SearchText, n = e.displayName;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return n.find(s) != std::string::npos;
    }

    void AssetBrowserPanel::OpenAssetExternally(const std::string& p) { 
#ifdef _WIN32
        if (!p.empty() && std::filesystem::exists(p)) { ShellExecuteA(NULL, "open", p.c_str(), NULL, NULL, SW_SHOW); }
#else
        (void)p;
#endif
    }
    void AssetBrowserPanel::RenameAsset(const AssetBrowserEntry& entry, const std::string& newName) {
        if (newName.empty() || newName == entry.displayName) return;
        auto oldPath = std::filesystem::path(entry.physicalPath);
        auto newPath = oldPath.parent_path() / (newName + (entry.isDirectory ? "" : oldPath.extension().string()));
        std::error_code ec; std::filesystem::rename(oldPath, newPath, ec);
        if (!ec) {
            if (!entry.isDirectory && std::filesystem::exists(oldPath.string() + ".meta"))
                std::filesystem::rename(oldPath.string() + ".meta", newPath.string() + ".meta", ec);
            if (m_Database) m_Database->ScanSync(); Refresh();
        } else ENGINE_LOG_ERROR("AssetBrowser", "Rename failed: {}", ec.message());
    }
    const char* AssetBrowserPanel::GetFileTypeIcon(AssetType, bool isDir) const { return isDir ? ICON_FA_FOLDER_OPEN : ICON_FA_FILE; }
    ImVec4 AssetBrowserPanel::GetFileTypeColor(AssetType, bool isDir) const { return isDir ? ImVec4(0.8f,0.7f,0.3f,1.0f) : ImVec4(0.6f,0.6f,0.6f,1.0f); }

    void AssetBrowserPanel::OnImGui() {
        if (!m_Visible) return;
        ImGui::SetNextWindowSize(ImVec2(1000, 650), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(ICON_FA_FOLDER_OPEN " Content Browser", &m_Visible);
        DrawToolbar();
        if (ImGui::BeginTable("BrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthFixed, 240.0f);
            ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("LeftTree", ImVec2(0, -28), false, ImGuiWindowFlags_NoScrollWithMouse);
            DrawDirectoryTree(); ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);
            DrawAssetGrid(); ImGui::EndTable();
        }
        DrawStatusBar();
        if (m_ShowCollectionPanel) DrawCollectionPanel();
        if (m_ShowFavoritesPanel) DrawFavoritesPanel();
        if (m_ShowTagEditor && m_SelectedEntryIndex >= 0 && m_SelectedEntryIndex < (int)m_Entries.size()) DrawTagEditor(m_Entries[m_SelectedEntryIndex]);
        if (m_ShowImportSettings && m_SelectedEntryIndex >= 0 && m_SelectedEntryIndex < (int)m_Entries.size()) DrawImportSettingsPanel(m_Entries[m_SelectedEntryIndex]);
        if (m_ShowDependencyGraph && m_SelectedEntryIndex >= 0 && m_SelectedEntryIndex < (int)m_Entries.size()) DrawDependencyGraph(m_Entries[m_SelectedEntryIndex]);
        ImGui::End(); ImGui::PopStyleVar();
    }

    void AssetBrowserPanel::DrawToolbar() {
        ImGui::BeginChild("ToolbarArea", ImVec2(0, 36), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(6, 4));
        if (ImGui::BeginCombo("##root", m_ActiveRoot.c_str(), ImGuiComboFlags_WidthFitPreview)) {
            for (auto& mp : m_MountPoints) {
                bool sel = (mp.mountName == m_ActiveRoot);
                if (ImGui::Selectable((mp.mountName + (mp.isReadOnly ? " [RO]" : "")).c_str(), sel)) { m_ActiveRoot = mp.mountName; m_CurrentDir = mp.mountName + ":"; Refresh(); }
                if (sel) ImGui::SetItemDefaultFocus();
            } ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " Add", ImVec2(70, 0))) ImGui::OpenPopup("AssetCreateMenu");
        DrawContextMenuEmptySpace();
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
        {
            std::string acc; std::stringstream ss(m_CurrentDir); std::string tok; bool first = true;
            while (std::getline(ss, tok, '/')) {
                if (!first) { ImGui::SameLine(0, 2); ImGui::TextDisabled(">"); ImGui::SameLine(0, 2); }
                if (first && tok.find(':') != std::string::npos) {
                    bool ro = false; for (auto& mp : m_MountPoints) if (mp.mountName == tok.substr(0, tok.find(':'))) { ro = mp.isReadOnly; break; }
                    acc = tok; if (ImGui::Button((tok + (ro ? " [RO]" : "")).c_str())) { m_CurrentDir = acc; Refresh(); }
                } else {
                    acc += (acc.empty() ? "" : "/") + tok; if (ImGui::Button(((first ? "\xef\x81\x9b " : "") + tok).c_str())) { m_CurrentDir = acc; Refresh(); }
                } first = false;
            }
        }
        float rightOffset = ImGui::GetWindowWidth() - 430.0f; ImGui::SameLine(rightOffset);
        if (ImGui::Button(ICON_FA_CIRCLE)) m_ShowFavoritesPanel = !m_ShowFavoritesPanel; if (ImGui::IsItemHovered()) ImGui::SetTooltip("Favorites"); ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBES)) m_ShowCollectionPanel = !m_ShowCollectionPanel; if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collections"); ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SYNC)) { if (m_Database) m_Database->ScanSync(); Refresh(); } if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh"); ImGui::SameLine();
        ImGui::PushItemWidth(180.0f);
        char sb[256] = {}; std::strncpy(sb, m_SearchText.c_str(), sizeof(sb)-1);
        if (ImGui::InputTextWithHint("##search", "\xef\x80\x82 type: name: size:> tag:", sb, sizeof(sb))) { m_SearchText = sb; Refresh(); }
        ImGui::PopItemWidth(); ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawDirectoryTree() {
        for (auto& mp : m_MountPoints) {
            ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            std::string vd = mp.mountName + ":"; if (m_CurrentDir == vd) f |= ImGuiTreeNodeFlags_Selected;
            bool o = ImGui::TreeNodeEx((void*)(intptr_t)(&mp), f, "%s", (mp.mountName + (mp.isReadOnly ? " [RO]" : "")).c_str());
            if (ImGui::IsItemClicked()) { m_CurrentDir = vd; m_ActiveCollection.clear(); Refresh(); }
            if (o) { ImGui::TreePop(); }
        }
        ImGui::Separator();
        { int fc = (int)m_Favorites.size(); ImGuiTreeNodeFlags ff = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth; if (m_ShowFavoritesPanel) ff |= ImGuiTreeNodeFlags_Selected; bool fo = ImGui::TreeNodeEx((void*)(intptr_t)2, ff, "%s  %s (%d)", ICON_FA_CIRCLE, "Favorites", fc); if (ImGui::IsItemClicked()) { m_ShowFavoritesPanel = !m_ShowFavoritesPanel; if (m_ShowFavoritesPanel) { m_ActiveCollection.clear(); Refresh(); } } if (fo) ImGui::TreePop(); }
        ImGuiTreeNodeFlags af = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_CurrentDir == "Assets") af |= ImGuiTreeNodeFlags_Selected;
        bool ao = ImGui::TreeNodeEx((void*)(intptr_t)1, af, "%s", ICON_FA_FOLDER_OPEN " Assets");
        if (ImGui::IsItemClicked()) { m_CurrentDir = "Assets"; m_ActiveCollection.clear(); m_ShowFavoritesPanel = false; Refresh(); }
        if (ao) { auto dc = m_Directories; for (auto& d : dc) { if (d == "Assets" || d.empty() || d.find(':') != std::string::npos) continue; ImGuiTreeNodeFlags nf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth; if (d == m_CurrentDir) nf |= ImGuiTreeNodeFlags_Selected; std::string dn = d; auto p = dn.rfind('/'); if (p != std::string::npos) dn = dn.substr(p + 1); bool no = ImGui::TreeNodeEx((void*)(&d), nf, "%s %s", ICON_FA_FOLDER_OPEN, dn.c_str()); if (ImGui::IsItemClicked()) { m_CurrentDir = d; m_ActiveCollection.clear(); Refresh(); } if (no) ImGui::TreePop(); } ImGui::TreePop(); }
    }

    // ═══════════════════════════════════════════════════════════════
    // ⚡ 优化 2: 虚拟列表渲染 (ImGuiListClipper)
    // ═══════════════════════════════════════════════════════════════
    void AssetBrowserPanel::DrawAssetGrid() {
        ImGui::BeginChild("##AssetGrid", ImVec2(0, -28), false, ImGuiWindowFlags_NoScrollWithMouse);
        float pw = ImGui::GetContentRegionAvail().x;
        int cols = (std::max)(1, (int)(pw / (m_IconSize + 16.0f)));
        int totalItems = (int)m_Entries.size();

        if (cols > 0 && totalItems > 0) {
            if (ImGui::BeginTable("AssetGridTable", cols)) {
                // ⚡ 使用 ImGuiListClipper 只渲染可见行
                int rows = (totalItems + cols - 1) / cols;  // 向上取整
                float rowHeight = m_IconSize + 50.0f;

                ImGuiListClipper clipper;
                clipper.Begin(rows, rowHeight);
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd && row < rows; ++row) {
                        for (int c = 0; c < cols; ++c) {
                            int idx = row * cols + c;
                            if (idx >= totalItems) break;
                            ImGui::TableNextColumn();
                            DrawAssetTile(m_Entries[idx], idx);
                        }
                    }
                }
                clipper.End();
                ImGui::EndTable();
            }
        } else if (totalItems == 0) {
            ImGui::TextDisabled("No assets matching criteria");
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
            ImGui::OpenPopup("AssetCreateMenu");
        DrawContextMenuEmptySpace();
        ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawAssetTile(const AssetBrowserEntry& entry, int index) {
        ImGui::PushID(index);
        bool sel = (m_SelectedEntryIndex == index);
        float cs = m_IconSize + 16.0f;
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = cp, p1 = ImVec2(p0.x + cs, p0.y + cs + 30.0f);

        if (sel) { dl->AddRectFilled(p0, p1, IM_COL32(50,100,180,150),6.0f); dl->AddRect(p0, p1, IM_COL32(100,150,255,255),6.0f); }
        else dl->AddRectFilled(p0, p1, IM_COL32(40,40,40,180),6.0f);

        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##tile", ImVec2(cs, cs + 30.0f));
        if (ImGui::IsItemHovered()) {
            if (!sel) dl->AddRectFilled(p0, p1, IM_COL32(70,70,70,100),6.0f);
            if (ImGui::IsMouseClicked(0)) { m_SelectedEntryIndex = index; m_SelectedGUID = entry.guid; if (m_OnAssetSelect) m_OnAssetSelect(entry.guid, entry.physicalPath); if (m_IsRenaming && m_RenamingGUID != entry.guid) m_IsRenaming = false; }
            if (ImGui::IsMouseDoubleClicked(0)) { if (entry.isDirectory) { m_CurrentDir = entry.virtualPath; Refresh(); } else OpenAssetExternally(entry.physicalPath); }
        }

        const char* icon = GetFileTypeIcon(entry.type, entry.isDirectory);
        ImVec4 ic = GetFileTypeColor(entry.type, entry.isDirectory);
        ImVec2 isz = ImGui::CalcTextSize(icon); float fs = m_IconSize / 32.0f;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * fs, ImVec2(p0.x + (cs - isz.x * fs) * 0.5f, p0.y + (cs - isz.y * fs) * 0.5f - 10.0f), ImGui::ColorConvertFloat4ToU32(ic), icon);
        if (entry.isFavorite) dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(p1.x - 20.0f, p0.y + 4.0f), IM_COL32(255,200,50,255), "*");
        if (m_ValidationFailures.count(entry.guid)) dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(p0.x + 4.0f, p0.y + 4.0f), IM_COL32(255,0,0,255), "X");

        if (m_IsRenaming && m_RenamingGUID == entry.guid) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 4.0f, p1.y - 28.0f)); ImGui::PushItemWidth(cs - 8.0f); ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { RenameAsset(entry, m_RenameBuffer); m_IsRenaming = false; }
            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) m_IsRenaming = false; ImGui::PopItemWidth();
        } else {
            ImVec2 ts = ImGui::CalcTextSize(entry.displayName.c_str()); float tx = p0.x + (cs - ts.x) * 0.5f; if (tx < p0.x + 4.0f) tx = p0.x + 4.0f;
            if (sel && ImGui::IsKeyPressed(ImGuiKey_F2)) { m_IsRenaming = true; m_RenamingGUID = entry.guid; std::string_view nwe = entry.isDirectory ? std::string_view(entry.displayName) : ExtractStem(entry.displayName); std::strncpy(m_RenameBuffer, nwe.data(), sizeof(m_RenameBuffer)-1); }
            dl->AddText(ImVec2(tx, p1.y - 25.0f), IM_COL32_WHITE, entry.displayName.c_str());
        }
        if (!entry.isDirectory && !IsReadOnlyPath(entry.virtualPath)) { if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) { ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", entry.physicalPath.c_str(), entry.physicalPath.size() + 1); ImGui::Text("%s %s", icon, entry.displayName.c_str()); ImGui::EndDragDropSource(); } }
        if (ImGui::BeginPopupContextItem("AssetContextMenu")) { m_SelectedEntryIndex = index; m_SelectedGUID = entry.guid; DrawContextMenuAsset(entry); ImGui::EndPopup(); }
        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawContextMenuEmptySpace() {
        if (ImGui::BeginPopup("AssetCreateMenu")) {
            if (ImGui::BeginMenu(ICON_FA_PLUS " Create")) {
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Folder")) { if (m_Database && !IsReadOnlyPath(m_CurrentDir)) { std::string pp = m_Database->ToPhysicalPath(m_CurrentDir) + "/NewFolder"; std::filesystem::create_directory(pp); m_Database->ScanSync(); Refresh(); } }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_CIRCLE " Material")) { if (m_Database && !IsReadOnlyPath(m_CurrentDir)) { std::string pp = m_Database->ToPhysicalPath(m_CurrentDir) + "/NewMaterial.mat"; std::ofstream f(pp); f << "{ \"type\": \"Material\" }"; f.close(); m_Database->ScanSync(); Refresh(); } }
                if (ImGui::MenuItem(ICON_FA_CODE " Shader")) { if (m_Database && !IsReadOnlyPath(m_CurrentDir)) { std::string pp = m_Database->ToPhysicalPath(m_CurrentDir) + "/NewShader.sg"; std::ofstream f(pp); f << "{ \"type\": \"ShaderGraph\" }"; f.close(); m_Database->ScanSync(); Refresh(); } }
                ImGui::EndMenu();
            } ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Show in Explorer")) { if (m_Database) OpenAssetExternally(m_Database->ToPhysicalPath(m_CurrentDir)); }
            ImGui::EndPopup();
        }
    }

    void AssetBrowserPanel::DrawContextMenuAsset(const AssetBrowserEntry& entry) {
        ImGui::TextDisabled("%s", entry.displayName.c_str()); if (IsReadOnlyPath(entry.virtualPath)) { ImGui::SameLine(); ImGui::TextDisabled("[RO]"); } ImGui::Separator();
        bool isFav = IsFavorite(entry.guid); if (ImGui::MenuItem(isFav ? "[*] Unfavorite" : "[*] Favorite")) { ToggleFavorite(entry.guid); Refresh(); }
        if (ImGui::MenuItem("[Tags] Edit Tags")) m_ShowTagEditor = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open")) { if (entry.isDirectory) { m_CurrentDir = entry.virtualPath; Refresh(); } else OpenAssetExternally(entry.physicalPath); }
        if (ImGui::MenuItem(ICON_FA_EYE " Show in Explorer")) { std::system(("explorer /select,\"" + entry.physicalPath + "\"").c_str()); }
        if (ImGui::MenuItem(ICON_FA_GEAR " Import Settings...")) m_ShowImportSettings = true;
        if (!entry.isDirectory && ImGui::MenuItem(ICON_FA_CUBES " Show Dependencies")) m_ShowDependencyGraph = true;
        if (ImGui::MenuItem(ICON_FA_CHECK " Validate Asset")) { ValidateAsset(entry); Refresh(); }
        ImGui::Separator();
        if (!IsReadOnlyPath(entry.virtualPath)) {
            if (ImGui::MenuItem(ICON_FA_WRENCH " Rename", "F2")) { m_IsRenaming = true; m_RenamingGUID = entry.guid; std::string nwe(entry.isDirectory ? std::string_view(entry.displayName) : ExtractStem(entry.displayName)); std::strncpy(m_RenameBuffer, nwe.c_str(), sizeof(m_RenameBuffer)-1); }
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete", "Del")) {
                bool canDel = true; if (m_DepTracker && !entry.isDirectory) { auto c = m_DepTracker->CheckBeforeDelete(entry.guid); if (!c.canDelete) canDel = false; }
                if (canDel) { std::error_code ec; if (entry.isDirectory) std::filesystem::remove_all(entry.physicalPath, ec); else { std::filesystem::remove(entry.physicalPath, ec); std::filesystem::remove(entry.physicalPath + ".meta", ec); } if (m_Database) m_Database->ScanSync(); Refresh(); }
            }
        } ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FILE " Copy Path")) ImGui::SetClipboardText(entry.physicalPath.c_str());
        if (!entry.isDirectory && ImGui::MenuItem(ICON_FA_FILE " Copy GUID")) ImGui::SetClipboardText(entry.guid.ToString().c_str());
    }

    void AssetBrowserPanel::ValidateAsset(const AssetBrowserEntry& entry) {
        bool valid = true;
        switch (entry.type) {
            case AssetType::Texture: {
                if (entry.fileSize > 50ULL * 1024 * 1024) valid = false;
                std::string low = entry.displayName; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (low.find("npot") != std::string::npos) valid = false; break;
            }
            case AssetType::Model: { if (entry.fileSize > 100ULL * 1024 * 1024) valid = false; break; }
            default: break;
        }
        if (!valid) m_ValidationFailures.insert(entry.guid); else m_ValidationFailures.erase(entry.guid);
    }

    void AssetBrowserPanel::DrawImportSettingsPanel(const AssetBrowserEntry& entry) {
        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin(ICON_FA_GEAR " Import Settings", &m_ShowImportSettings);
        ImGui::Text("%s", entry.displayName.c_str()); ImGui::Separator();
        auto meta = m_Database ? m_Database->GetMetaMutable(entry.guid) : nullptr;
        if (!meta) { ImGui::TextDisabled("No import settings available"); ImGui::End(); return; }
        bool changed = false;
        ImGui::Text("Type: %s", AssetTypeName(meta->type)); ImGui::Text("Path: %s", meta->originalPath.c_str()); ImGui::Separator();
        switch (meta->type) {
            case AssetType::Texture: {
                auto& ts = meta->textureSettings; int fmt = (int)ts.format; ImGui::Combo("Format", &fmt, "RGBA32\0RGB24\0BC1\0BC3\0BC5\0BC7\0ASTC\0"); if (fmt != (int)ts.format) { ts.format = (TextureImportSettings::Format)fmt; changed = true; }
                if (ImGui::Checkbox("sRGB", &ts.sRGB)) changed = true; if (ImGui::Checkbox("Generate MipMaps", &ts.generateMipMaps)) changed = true;
                if (ImGui::Checkbox("Is Normal Map", &ts.isNormalMap)) changed = true; if (ImGui::Checkbox("Is Sprite", &ts.isSprite)) changed = true;
                if (ImGui::DragInt("Max Resolution", &ts.maxResolution, 1, 32, 8192)) changed = true; break;
            }
            case AssetType::Model: {
                auto& ms = meta->modelSettings; if (ImGui::DragFloat("Scale Factor", &ms.scaleFactor, 0.01f, 0.01f, 100.0f)) changed = true;
                if (ImGui::Checkbox("Import Materials", &ms.importMaterials)) changed = true; if (ImGui::Checkbox("Import Animations", &ms.importAnimations)) changed = true;
                if (ImGui::Checkbox("Optimize Meshes", &ms.optimizeMeshes)) changed = true; if (ImGui::Checkbox("Generate Colliders", &ms.generateColliders)) changed = true; break;
            }
            case AssetType::AudioClip: {
                auto& as = meta->audioSettings; int comp = (int)as.compression; ImGui::Combo("Compression", &comp, "PCM\0ADPCM\0Vorbis\0MP3\0"); if (comp != (int)as.compression) { as.compression = (AudioImportSettings::Compression)comp; changed = true; }
                if (ImGui::SliderFloat("Quality", &as.quality, 0.0f, 1.0f)) changed = true; if (ImGui::Checkbox("Stream from Disk", &as.streamFromDisk)) changed = true; if (ImGui::Checkbox("Normalize", &as.normalize)) changed = true; break;
            }
            default: ImGui::TextDisabled("No import options for this asset type"); break;
        }
        ImGui::Separator();
        if (changed && ImGui::Button("Apply", ImVec2(120, 0))) { if (m_Database) { m_Database->WriteMetaFile(meta->originalPath + ".meta", *meta); m_Database->Reimport(entry.guid); } ImGui::SameLine(); }
        if (ImGui::Button("Reset to Defaults")) { *meta = AssetMeta{}; meta->guid = entry.guid; meta->type = entry.type; meta->originalPath = entry.physicalPath; if (m_Database) { m_Database->WriteMetaFile(meta->originalPath + ".meta", *meta); } }
        ImGui::End();
    }

    void AssetBrowserPanel::DrawDependencyGraph(const AssetBrowserEntry& entry) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin(ICON_FA_CUBES " Reference Viewer", &m_ShowDependencyGraph);
        ImGui::Text("Asset: %s", entry.displayName.c_str()); ImGui::Separator();
        if (!m_DepTracker) { ImGui::TextDisabled("Dependency tracker not available"); ImGui::End(); return; }
        ImGui::TextColored(ImVec4(1,1,0.5f,1), "Dependencies (used by this asset):");
        auto deps = m_DepTracker->GetDirectDependencies(entry.guid);
        if (deps.empty()) ImGui::TextDisabled("  None"); else for (auto& d : deps) { auto dm = m_Database ? m_Database->GetMeta(d) : nullptr; ImGui::BulletText("%s", dm ? dm->originalPath.c_str() : "Unknown"); }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f,1,1,1), "Referencers (used by):");
        auto users = m_DepTracker->GetDirectUsers(entry.guid);
        if (users.empty()) ImGui::TextDisabled("  None - safe to delete"); else for (auto& u : users) { auto um = m_Database ? m_Database->GetMeta(u) : nullptr; ImGui::BulletText("%s", um ? um->originalPath.c_str() : "Unknown"); }
        if (!users.empty()) { ImGui::Separator(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Warning: %zu assets reference this resource.", users.size()); }
        ImGui::End();
    }

    void AssetBrowserPanel::DrawTagEditor(const AssetBrowserEntry& entry) {
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tag Editor", &m_ShowTagEditor);
        ImGui::Text("Tags for: %s", entry.displayName.c_str()); ImGui::Separator();
        auto ex = GetTags(entry.guid);
        for (auto& t : ex) { ImGui::BulletText("%s", t.c_str()); ImGui::SameLine(); if (ImGui::SmallButton(("x##" + t).c_str())) RemoveTag(entry.guid, t); }
        ImGui::Separator(); static char nt[64] = ""; ImGui::InputText("##newtag", nt, sizeof(nt)); ImGui::SameLine(); if (ImGui::Button("Add")) { AddTag(entry.guid, nt); nt[0] = '\0'; }
        auto at = GetAllTags(); if (!at.empty()) { ImGui::Text("Quick add:"); for (auto& t : at) { if (ex.end() == std::find(ex.begin(), ex.end(), t)) { ImGui::SameLine(); if (ImGui::SmallButton(t.c_str())) AddTag(entry.guid, t); } } }
        ImGui::End();
    }

    void AssetBrowserPanel::DrawFavoritesPanel() {
        ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin(ICON_FA_CIRCLE " Favorites", &m_ShowFavoritesPanel);
        for (auto& f : m_Favorites) { if (auto* m = m_Database ? m_Database->GetMeta(f) : nullptr) { std::string_view n = ExtractFileName(m->originalPath); if (ImGui::Selectable(n.data())) { m_CurrentDir = std::filesystem::path(m->virtualPath).parent_path().string(); if (m_CurrentDir.empty()) m_CurrentDir = "Assets"; Refresh(); } if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m->originalPath.c_str()); } }
        if (m_Favorites.empty()) ImGui::TextDisabled("No favorites yet"); ImGui::End();
    }

    void AssetBrowserPanel::DrawCollectionPanel() {
        ImGui::SetNextWindowSize(ImVec2(350, 350), ImGuiCond_FirstUseEver);
        ImGui::Begin(ICON_FA_CUBES " Collections", &m_ShowCollectionPanel);
        for (auto& sc : m_SmartCollections) { bool active = (m_ActiveCollection == sc.name); if (ImGui::Selectable((sc.name + (sc.isDynamic ? " (Dynamic)" : "")).c_str(), active)) { m_ActiveCollection = sc.name; Refresh(); } ImGui::SameLine(); if (ImGui::SmallButton(("Delete##" + sc.name).c_str())) { RemoveSmartCollection(sc.name); m_ActiveCollection.clear(); } }
        ImGui::Separator();
        ImGui::InputText("Name", m_NewCollectionName, sizeof(m_NewCollectionName));
        ImGui::InputText("Query", m_NewCollectionQuery, sizeof(m_NewCollectionQuery));
        if (ImGui::Button("Add Collection")) { if (strlen(m_NewCollectionName) > 0) { AddSmartCollection(m_NewCollectionName, m_NewCollectionQuery, true); m_NewCollectionName[0] = '\0'; m_NewCollectionQuery[0] = '\0'; } }
        ImGui::Separator(); ImGui::TextDisabled("Query syntax: t:type name: search size:>10000 tag:boss");
        ImGui::End();
    }

    void AssetBrowserPanel::DrawStatusBar() {
        ImGui::BeginChild("AssetBrowserStatusBar", ImVec2(0, 24), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(10, 4)); int fc = 0; for (auto& e : m_Entries) if (e.isFavorite) fc++;
        ImGui::TextDisabled("%zu items%s | %d favorites | %d validations | Path: %s", m_Entries.size(), m_ActiveCollection.empty() ? "" : (" (Collection: " + m_ActiveCollection + ")").c_str(), fc, (int)m_ValidationFailures.size(), m_CurrentDir.c_str());
        ImGui::EndChild();
    }

    void AssetBrowserPanel::DrawListView() {}
    void AssetBrowserPanel::DrawSearchBar() {}
    void AssetBrowserPanel::DrawFilterBar() {}

} // namespace Engine