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

namespace Engine {

    class EditorAssetDatabase;

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

    private:
        void DrawToolbar();
        void DrawSearchBar();
        void DrawFilterBar();
        void DrawDirectoryTree();
        void DrawAssetGrid();
        void DrawAssetTile(const AssetBrowserEntry& entry, int index);
        void DrawListView();
        void DrawContextMenu(const AssetBrowserEntry& entry);
        void DrawImportSettingsPanel(const AssetBrowserEntry& entry);
        void DrawDependencyGraph(const AssetBrowserEntry& entry);
        void DrawCollectionPanel();
        void DrawStatusBar();

        void RefreshEntries();
        void RefreshDirectories();
        bool PassesFilter(const AssetBrowserEntry& entry) const;
        AssetBrowserEntry MakeEntry(const GUID& guid);
        std::string FormatFileSize(int64_t bytes) const;
        std::string FormatTime(int64_t timestamp) const;
        const char* GetFileTypeIcon(AssetType type) const;

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
        int m_GridColumns = 4;

        bool m_ShowFilterBar = false;
        bool m_ShowImportSettings = false;
        AssetBrowserEntry m_SelectedForImport;
        bool m_ShowDependencyGraph = false;
        AssetBrowserEntry m_SelectedForDepGraph;
        bool m_ShowCollectionPanel = false;

        std::atomic<bool> m_NeedRefresh{false};
        AssetSelectCallback m_OnAssetSelect;
        AssetDragCallback m_OnAssetDrag;

        bool m_FilterTextures = true, m_FilterAudio = true, m_FilterShaders = true;
        bool m_FilterScenes = true, m_FilterFonts = true, m_FilterOthers = true;
        float m_ScrollY = 0.0f;
    };

} // namespace Engine