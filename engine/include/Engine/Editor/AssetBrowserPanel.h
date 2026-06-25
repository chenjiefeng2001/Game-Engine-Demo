#pragma once

/**
 * @file AssetBrowserPanel.h
 * @brief 工业级资产浏览器面板 — GUID 驱动的完整资产管理系统
 *
 * 七大核心功能：
 *   1. 树形目录 + 文件网格（双栏布局）
 *   2. 多维搜索过滤器（类型/状态/大小/日期/标签）
 *   3. 收藏集（虚拟文件夹）
 *   4. 资产缩略图预览（悬浮大图/3D 预览）
 *   5. 源控制状态图标（签出/锁定/冲突）
 *   6. 依赖关系可视化（右键 → 引用查看器）
 *   7. 批量操作（批量修改导入设置）
 *
 * 底层通过 AssetDatabase (GUID 系统) 驱动，文件路径移动不影响引用。
 */

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

    class AssetBrowserPanel {
    public:
        AssetBrowserPanel();
        ~AssetBrowserPanel() = default;

        AssetBrowserPanel(const AssetBrowserPanel&) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

        // ── 初始化 ──
        void Init(AssetDatabase* db, DependencyTracker* depTracker);
        void OnImGui();
        void OnUpdate(float dt);

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 资产选中回调 ──
        using AssetSelectCallback = std::function<void(const GUID& guid, const std::string& path)>;
        void SetAssetSelectCallback(AssetSelectCallback cb) { m_OnAssetSelect = std::move(cb); }

        // ── 拖拽回调 ──
        using AssetDragCallback = std::function<void(const GUID& guid, const std::string& path)>;
        void SetAssetDragCallback(AssetDragCallback cb) { m_OnAssetDrag = std::move(cb); }

        // ── 外部控制 ──
        void NavigateTo(const std::string& virtualPath);
        void SelectAsset(const GUID& guid);
        void Refresh();

        // ── 搜索 ──
        void SetSearchFilter(const AssetSearchFilter& filter) { m_SearchFilter = filter; }
        const AssetSearchFilter& GetSearchFilter() const { return m_SearchFilter; }

    private:
        // ── 内部绘制 ──
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

        // ── 辅助函数 ──
        void RefreshEntries();
        void RefreshDirectories();
        bool PassesFilter(const AssetBrowserEntry& entry) const;
        AssetBrowserEntry MakeEntry(const GUID& guid);
        std::string FormatFileSize(int64_t bytes) const;
        std::string FormatTime(int64_t timestamp) const;
        const char* GetFileTypeIcon(AssetType type) const;

        // ── 数据 ──
        bool m_Visible = true;
        AssetDatabase* m_Database = nullptr;
        DependencyTracker* m_DepTracker = nullptr;

        // 导航
        std::string m_CurrentDir = "Assets";
        std::string m_SearchText;

        // 目录树
        std::vector<std::string> m_Directories;
        int m_SelectedDirIndex = -1;

        // 资产列表
        std::vector<AssetBrowserEntry> m_Entries;
        int m_SelectedEntryIndex = -1;
        GUID m_SelectedGUID;

        // 视图模式
        enum class ViewMode : uint8_t { Grid, List, LargeIcon };
        ViewMode m_ViewMode = ViewMode::Grid;
        float m_IconSize = 64.0f;
        int m_GridColumns = 4;

        // 搜索过滤
        AssetSearchFilter m_SearchFilter;
        bool m_ShowFilterBar = false;

        // 导入设置面板
        bool m_ShowImportSettings = false;
        AssetBrowserEntry m_SelectedForImport;

        // 依赖图面板
        bool m_ShowDependencyGraph = false;
        AssetBrowserEntry m_SelectedForDepGraph;

        // 收藏集
        bool m_ShowCollectionPanel = false;
        std::string m_SelectedCollection;

        // 异步刷新
        std::atomic<bool> m_NeedRefresh{false};

        // 回调
        AssetSelectCallback m_OnAssetSelect;
        AssetDragCallback m_OnAssetDrag;

        // 缓存：类型过滤复选框
        bool m_FilterTextures = true;
        bool m_FilterAudio = true;
        bool m_FilterShaders = true;
        bool m_FilterScenes = true;
        bool m_FilterFonts = true;
        bool m_FilterOthers = true;

        // 滚动位置
        float m_ScrollY = 0.0f;
    };

} // namespace Engine