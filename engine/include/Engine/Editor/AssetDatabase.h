#pragma once

/**
 * @file AssetDatabase.h
 * @brief 工业级资产数据库 — GUID 映射、.meta 文件管理、虚拟文件系统
 *
 * 核心职责：
 *   1. GUID ↔ 文件路径双向映射（路径移动不影响引用）
 *   2. .meta 文件的读取/写入/同步
 *   3. 虚拟路径系统（VFS）
 *   4. 异步资产扫描
 *   5. 缓存管理（Library 目录）
 *
 * 设计参考 Unity 的 AssetDatabase 系统。
 *
 * 注意：本类命名为 EditorAssetDatabase 以避免与
 *       engine/include/Engine/Core/Resources/AssetDatabase.h 冲突。
 */

#include "Engine/Types.h"
#include "Engine/Editor/AssetTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <filesystem>

namespace Engine {

    // ============================================================
    // 资产数据库事件回调
    // ============================================================
    struct AssetDatabaseCallbacks {
        std::function<void(const GUID& guid, const std::string& path)> onAssetImported;
        std::function<void(const GUID& guid, const std::string& path)> onAssetDeleted;
        std::function<void(const GUID& guid, const std::string& oldPath, const std::string& newPath)> onAssetMoved;
        std::function<void()> onScanStarted;
        std::function<void(int progress, int total)> onScanProgress;
        std::function<void()> onScanCompleted;
    };

    // ============================================================
    // 资产数据库（单例）
    // ============================================================
    class EditorAssetDatabase {
    public:
        static EditorAssetDatabase& Get();

        EditorAssetDatabase(const EditorAssetDatabase&) = delete;
        EditorAssetDatabase& operator=(const EditorAssetDatabase&) = delete;

        // ── 初始化与扫描 ──
        void Initialize(const std::string& assetRoot, const std::string& libraryPath);
        bool IsInitialized() const { return m_Initialized; }

        void ScanAsync();
        void ScanSync();
        void ScanIncremental();
        void Update();

        // ── GUID 查询 ──
        GUID GetGUID(const std::string& physicalPath) const;
        GUID GetGUIDByVirtual(const std::string& virtualPath) const;
        std::string GetPhysicalPath(const GUID& guid) const;
        std::string GetVirtualPath(const GUID& guid) const;
        bool HasGUID(const GUID& guid) const;
        bool HasPath(const std::string& physicalPath) const;

        // ── 元数据访问 ──
        const AssetMeta* GetMeta(const GUID& guid) const;
        AssetMeta* GetMetaMutable(const GUID& guid);
        AssetType GetAssetType(const GUID& guid) const;
        AssetType GetAssetType(const std::string& path) const;

        // ── 资产枚举 ──
        std::vector<GUID> GetAllAssets(AssetType typeFilter = AssetType::Unknown) const;
        std::vector<GUID> GetAssetsInDirectory(const std::string& virtualDir) const;
        std::vector<std::string> GetAllDirectories() const;
        std::vector<GUID> GetUnusedAssets() const;

        // ── .meta 文件管理 ──
        bool CreateMetaFile(const std::string& physicalPath);
        bool ReadMetaFile(const std::string& metaPath, AssetMeta& outMeta) const;
        bool WriteMetaFile(const std::string& metaPath, const AssetMeta& meta) const;
        static std::string GetMetaPath(const std::string& physicalPath);

        // ── 导入管理 ──
        bool Reimport(const GUID& guid);

        // ── 虚拟路径管理 ──
        bool SetVirtualPath(const GUID& guid, const std::string& newVirtualPath);
        std::string ToVirtualPath(const std::string& physicalPath) const;
        std::string ToPhysicalPath(const std::string& virtualPath) const;

        // ── 回调 ──
        void SetCallbacks(const AssetDatabaseCallbacks& callbacks) { m_Callbacks = callbacks; }
        AssetDatabaseCallbacks& GetCallbacks() { return m_Callbacks; }

        // ── 文件监视 ──
        void EnableFileWatching(bool enable);
        bool IsFileWatchingEnabled() const { return m_FileWatching; }

    private:
        EditorAssetDatabase() = default;
        ~EditorAssetDatabase() = default;

        void ScanDirectory(const std::filesystem::path& dir, bool recursive);
        void ProcessFile(const std::filesystem::path& filePath);
        void LoadMetaFile(const std::filesystem::path& metaPath);
        void SaveAllMetaFiles();

        void LoadCache();
        void SaveCache();
        std::string GetCachePath() const;

        bool m_Initialized = false;
        bool m_FileWatching = false;
        std::string m_AssetRoot;
        std::string m_LibraryPath;

        std::unordered_map<GUID, AssetMeta> m_GuidToMeta;
        std::unordered_map<std::string, GUID> m_PathToGuid;
        std::unordered_map<std::string, GUID> m_VirtualPathToGuid;
        std::unordered_map<GUID, AssetDependencyInfo> m_Dependencies;
        std::vector<AssetCollection> m_Collections;

        std::atomic<bool> m_Scanning{false};
        std::atomic<int> m_ScanProgress{0};
        std::atomic<int> m_ScanTotal{0};
        std::vector<AssetMeta> m_PendingResults;
        mutable std::mutex m_Mutex;

        AssetDatabaseCallbacks m_Callbacks;
    };

} // namespace Engine