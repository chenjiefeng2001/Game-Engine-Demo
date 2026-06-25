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
    class AssetDatabase {
    public:
        static AssetDatabase& Get();

        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        // ── 初始化与扫描 ──
        /** 初始化数据库，指定资产根目录和 Library 缓存目录 */
        void Initialize(const std::string& assetRoot, const std::string& libraryPath);
        bool IsInitialized() const { return m_Initialized; }

        /** 异步扫描所有资产（在后台线程执行） */
        void ScanAsync();
        /** 同步扫描 */
        void ScanSync();
        /** 增量扫描（只检查修改过的文件） */
        void ScanIncremental();

        /** 每帧更新（处理异步扫描结果） */
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
        /** 获取所有资产（可选按类型过滤） */
        std::vector<GUID> GetAllAssets(AssetType typeFilter = AssetType::Unknown) const;
        /** 获取指定目录下的资产 */
        std::vector<GUID> GetAssetsInDirectory(const std::string& virtualDir) const;
        /** 获取所有目录 */
        std::vector<std::string> GetAllDirectories() const;
        /** 获取未使用的资产 */
        std::vector<GUID> GetUnusedAssets() const;

        // ── .meta 文件管理 ──
        /** 为指定文件创建/更新 .meta 文件 */
        bool CreateMetaFile(const std::string& physicalPath);
        /** 读取 .meta 文件 */
        bool ReadMetaFile(const std::string& metaPath, AssetMeta& outMeta) const;
        /** 写入 .meta 文件 */
        bool WriteMetaFile(const std::string& metaPath, const AssetMeta& meta) const;
        /** 获取 .meta 文件路径 */
        static std::string GetMetaPath(const std::string& physicalPath);

        // ── 导入管理 ──
        /** 重新导入指定资产 */
        bool Reimport(const GUID& guid);
        /** 获取/设置导入设置 */
        TextureImportSettings& GetTextureImportSettings(const GUID& guid);
        ModelImportSettings& GetModelImportSettings(const GUID& guid);
        AudioImportSettings& GetAudioImportSettings(const GUID& guid);

        // ── 虚拟路径管理 ──
        /** 设置资产的虚拟路径（移动/重命名时更新） */
        bool SetVirtualPath(const GUID& guid, const std::string& newVirtualPath);
        /** 物理路径 → 虚拟路径 */
        std::string ToVirtualPath(const std::string& physicalPath) const;
        /** 虚拟路径 → 物理路径 */
        std::string ToPhysicalPath(const std::string& virtualPath) const;

        // ── 回调 ──
        void SetCallbacks(const AssetDatabaseCallbacks& callbacks) { m_Callbacks = callbacks; }
        AssetDatabaseCallbacks& GetCallbacks() { return m_Callbacks; }

        // ── 文件监视 ──
        /** 启用文件系统监视（自动检测文件变化） */
        void EnableFileWatching(bool enable);
        bool IsFileWatchingEnabled() const { return m_FileWatching; }

    private:
        AssetDatabase() = default;
        ~AssetDatabase() = default;

        // ── 内部扫描 ──
        void ScanDirectory(const std::filesystem::path& dir, bool recursive);
        void ProcessFile(const std::filesystem::path& filePath);
        void LoadMetaFile(const std::filesystem::path& metaPath);
        void SaveAllMetaFiles();

        // ── 缓存管理 ──
        void LoadCache();
        void SaveCache();
        std::string GetCachePath() const;

        // ── 内部数据 ──
        bool m_Initialized = false;
        bool m_FileWatching = false;
        std::string m_AssetRoot;
        std::string m_LibraryPath;

        // GUID → Meta 映射
        std::unordered_map<GUID, AssetMeta> m_GuidToMeta;

        // 路径 → GUID 映射（用于快速反向查询）
        std::unordered_map<std::string, GUID> m_PathToGuid;
        std::unordered_map<std::string, GUID> m_VirtualPathToGuid;

        // 依赖信息
        std::unordered_map<GUID, AssetDependencyInfo> m_Dependencies;

        // 收藏集
        std::vector<AssetCollection> m_Collections;

        // 异步扫描状态
        std::atomic<bool> m_Scanning{false};
        std::atomic<int> m_ScanProgress{0};
        std::atomic<int> m_ScanTotal{0};
        std::vector<AssetMeta> m_PendingResults;
        mutable std::mutex m_Mutex;

        // 回调
        AssetDatabaseCallbacks m_Callbacks;
    };

} // namespace Engine