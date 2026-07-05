#pragma once

#include "Engine/Types.h"
#include "Engine/Editor/AssetTypes.h"
#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Editor/DependencyTracker.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <set>
#include <map>

#include <imgui.h>

namespace Engine {

    class EditorAssetDatabase;

    // ── VFS 挂载点 ──
    struct VFSMountPoint {
        std::string mountName;
        std::string physicalPath;
        bool        isReadOnly = false;
        int         priority   = 0;
        std::string icon;
    };

    // ── 搜索令牌 ──
    struct SearchToken {
        enum Type { TypeFilter, NameFilter, SizeFilter, TagFilter, FormatFilter, Text };
        Type type = Text;
        std::string key;
        std::string op;     // ":", ">", "<", ">=", "<="
        std::string value;
    };

    // ── 智能收藏集 ──
    struct SmartCollection {
        std::string name;
        std::string searchQuery;       // 保存的搜索条件
        bool        isDynamic = false; // true = 实时搜索
        std::vector<AssetType> typeFilter;
        std::vector<std::string> tags;
    };

    // ── 解析后的搜索条件 ──
    struct SearchQuery {
        std::vector<SearchToken> tokens;
        std::string rawText;
        bool Match(const AssetBrowserEntry& entry) const;
    };

    class AssetBrowserPanel {
    public:
        AssetBrowserPanel();
        ~AssetBrowserPanel() = default;

        AssetBrowserPanel(const AssetBrowserPanel&) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

        void Init(EditorAssetDatabase* db, DependencyTracker* depTracker);
        void OnImGui();
        void OnUpdate(float dt);

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        using AssetSelectCallback = std::function<void(const GUID& guid, const std::string& path)>;
        void SetAssetSelectCallback(AssetSelectCallback cb) { m_OnAssetSelect = std::move(cb); }
        using AssetDragCallback = std::function<void(const GUID& guid, const std::string& path)>;
        void SetAssetDragCallback(AssetDragCallback cb) { m_OnAssetDrag = std::move(cb); }

        void NavigateTo(const std::string& virtualPath);
        void SelectAsset(const GUID& guid);
        void Refresh();

        // ── VFS ──
        void Mount(const std::string& mountName, const std::string& physicalPath, bool readOnly = false, int priority = 0);
        void Unmount(const std::string& mountName);
        const std::vector<VFSMountPoint>& GetMountPoints() const { return m_MountPoints; }
        void SetActiveRoot(const std::string& rootName) { m_ActiveRoot = rootName; }
        std::string GetActiveRoot() const { return m_ActiveRoot; }

        // ── 标签系统 ──
        void AddTag(const GUID& guid, const std::string& tag);
        void RemoveTag(const GUID& guid, const std::string& tag);
        std::vector<std::string> GetTags(const GUID& guid) const;
        std::vector<std::string> GetAllTags() const;

        // ── 收藏夹 ──
        void ToggleFavorite(const GUID& guid);
        bool IsFavorite(const GUID& guid) const;

        // ── 智能收藏集 ──
        void AddSmartCollection(const std::string& name, const std::string& query, bool dynamic);
        void RemoveSmartCollection(const std::string& name);
        const std::vector<SmartCollection>& GetSmartCollections() const { return m_SmartCollections; }

    private:
        void DrawToolbar();
        void DrawSearchBar();
        void DrawFilterBar();
        void DrawDirectoryTree();
        void DrawAssetGrid();
        void DrawAssetTile(const AssetBrowserEntry& entry, int index);
        void DrawListView();
        void DrawContextMenuEmptySpace();
        void DrawContextMenuAsset(const AssetBrowserEntry& entry);
        void DrawImportSettingsPanel(const AssetBrowserEntry& entry);
        void DrawDependencyGraph(const AssetBrowserEntry& entry);
        void DrawCollectionPanel();
        void DrawStatusBar();
        void DrawTagEditor(const AssetBrowserEntry& entry);
        void DrawFavoritesPanel();

        void RefreshEntries();
        void RefreshDirectories();
        bool PassesFilter(const AssetBrowserEntry& entry) const;
        AssetBrowserEntry MakeEntry(const GUID& guid);
        std::string FormatFileSize(int64_t bytes) const;
        std::string FormatTime(int64_t timestamp) const;
        const char* GetFileTypeIcon(AssetType type, bool isDir) const;
        ImVec4 GetFileTypeColor(AssetType type, bool isDir) const;

        // ── 搜索解析 ──
        SearchQuery ParseSearchQuery(const std::string& raw) const;
        bool MatchQuery(const AssetBrowserEntry& entry, const SearchQuery& query) const;

        void OpenAssetExternally(const std::string& physicalPath);
        void RenameAsset(const AssetBrowserEntry& entry, const std::string& newName);

        // ── VFS ──
        std::string ResolveVFSPath(const std::string& virtualPath) const;
        std::string VFSToVirtual(const std::string& physicalPath) const;
        bool IsReadOnlyPath(const std::string& virtualPath) const;
        bool IsPathUnderMount(const std::string& path, const VFSMountPoint& mount) const;

        // ── 重命名 ──
        bool m_IsRenaming = false;
        GUID m_RenamingGUID;
        char m_RenameBuffer[256] = "";

        bool m_Visible = true;
        EditorAssetDatabase* m_Database = nullptr;
        DependencyTracker* m_DepTracker = nullptr;

        std::string m_CurrentDir = "Assets";
        std::string m_SearchText;

        std::vector<std::string> m_Directories;
        int m_SelectedDirIndex = -1;

        std::vector<AssetBrowserEntry> m_Entries;
        int m_SelectedEntryIndex = -1;
        GUID m_SelectedGUID;

        enum class ViewMode : uint8_t { Grid, List, LargeIcon };
        ViewMode m_ViewMode = ViewMode::Grid;
        float m_IconSize = 64.0f;

        bool m_ShowFilterBar = false;
        bool m_ShowImportSettings = false;
        AssetBrowserEntry m_SelectedForImport;
        bool m_ShowDependencyGraph = false;
        AssetBrowserEntry m_SelectedForDepGraph;
        bool m_ShowCollectionPanel = false;
        bool m_ShowFavoritesPanel = false;
        bool m_ShowTagEditor = false;

        std::atomic<bool> m_NeedRefresh{false};
        AssetSelectCallback m_OnAssetSelect;
        AssetDragCallback m_OnAssetDrag;

        // ── 资产验证 ──
        void ValidateAsset(const AssetBrowserEntry& entry);

        // ── 标签存储: GUID → tags ──
        std::map<GUID, std::set<std::string>> m_Tags;
        std::set<std::string> m_AllTags;         // 所有已知标签

        // ── 收藏夹 ──
        std::set<GUID> m_Favorites;

        // ── 验证结果 ──
        std::set<GUID> m_ValidationFailures;

        // ── 智能收藏集 ──
        std::vector<SmartCollection> m_SmartCollections;
        std::string m_ActiveCollection;           // 当前显示的收藏集名
        char m_NewCollectionName[128] = "";
        char m_NewCollectionQuery[256] = "";

        // ── VFS ──
        std::vector<VFSMountPoint> m_MountPoints;
        std::string m_ActiveRoot = "Game";
    };

} // namespace Engine