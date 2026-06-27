#pragma once

/**
 * @file AssetDatabase.h
 * @brief GUID 资产元数据库 — GUID ↔ 文件路径双向映射、.meta 文件管理
 *
 * 工业标准资产管理系统：
 *   - 场景中永远不保存文件路径，只保存 GUID
 *   - 每个物理资产在同级目录下生成对应的 .meta 文件（记录 GUID + 导入设置）
 *   - 提供 GUID↔Path 双向查询映射表
 *
 * 与 AssetPipeline 的关系：
 *   - AssetPipeline 使用 AssetEntry（string guid）处理导入/构建流程
 *   - 本数据库（AssetMetaDb）使用 ResourceGUID（128-bit）提供运行时 GUID 寻址
 *
 * 工作流：
 *   1. 引擎启动时扫描资产目录树（assets/）
 *   2. 对每个已知资产文件检查 .meta 是否存在：
 *      - 存在 → 读取 GUID，建立映射
 *      - 不存在 → 生成新的 UUID v4，写入 .meta 文件
 *   3. 运行时通过 GUID 查找文件路径，加载资源
 *
 * .meta 文件格式 (JSON)：
 * @code
 * {
 *     "guid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
 *     "type": "Texture",
 *     "importSettings": {
 *         "compression": "BC7",
 *         "generateMips": true,
 *         "sRGB": true
 *     }
 * }
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/Resources/Resource.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // .meta 导入设置
    // ============================================================
    struct MetaImportSettings {
        std::string compression = "";
        bool generateMips = false;
        bool sRGB = false;
        float quality = 1.0f;
        float modelScale = 1.0f;
    };

    // ============================================================
    // 资产元数据条目（对应一个 .meta 文件）
    // ============================================================
    struct AssetMetaEntry {
        ResourceGUID     guid;
        std::string      path;             ///< 相对于资产根目录的路径
        ResourceType     type    = ResourceType::Unknown;
        MetaImportSettings importSettings;
        uint64           fileSize = 0;
        uint64           lastModified = 0;
    };

    // ============================================================
    // 资产事件
    // ============================================================
    enum class AssetEvent : uint8 {
        Added,
        Removed,
        Modified,
        MetaChanged
    };

    using AssetEventCallback = std::function<void(AssetEvent event, const AssetMetaEntry& meta)>;

    // ============================================================
    // 资产元数据库（单例）
    // ============================================================
    class AssetMetaDb {
    public:
        static void Init(const std::string& assetRoot);
        static void Shutdown();
        static AssetMetaDb* Get() { return s_Instance; }

        // ── GUID ↔ Path ──
        const std::string* GetPath(const ResourceGUID& guid) const;
        ResourceGUID GetGUID(const std::string& path) const;
        bool HasGUID(const ResourceGUID& guid) const;
        bool HasPath(const std::string& path) const;

        // ── 扫描 ──
        void ScanAll();
        const AssetMetaEntry* GetMeta(const ResourceGUID& guid) const;

        // ── .meta 管理 ──
        ResourceGUID EnsureMeta(const std::string& filePath);

        // ── 查询 ──
        std::vector<std::string> GetAllPaths() const;
        std::vector<ResourceGUID> GetGuidsByType(ResourceType type) const;
        std::vector<std::string> FindPaths(const std::string& pattern) const;

        // ── 事件 ──
        uint32 BindEventCallback(AssetEventCallback callback);
        void UnbindEventCallback(uint32 id);

        size_t GetAssetCount() const { return m_GuidToMeta.size(); }
        const std::string& GetAssetRoot() const { return m_AssetRoot; }

    public:
        ~AssetMetaDb() = default;
    private:
        explicit AssetMetaDb(const std::string& assetRoot);
        AssetMetaDb(const AssetMetaDb&) = delete;
        AssetMetaDb& operator=(const AssetMetaDb&) = delete;

        bool ReadMetaFile(const std::string& metaPath, AssetMetaEntry& outMeta);
        bool WriteMetaFile(const std::string& metaPath, const AssetMetaEntry& meta);
        ResourceType GuessResourceType(const std::string& path) const;
        void FireEvent(AssetEvent event, const AssetMetaEntry& meta);

        static AssetMetaDb* s_Instance;
        static std::unique_ptr<AssetMetaDb> s_InstanceOwner;

        std::string m_AssetRoot;
        std::unordered_map<ResourceGUID, AssetMetaEntry> m_GuidToMeta;
        std::unordered_map<std::string, ResourceGUID> m_PathToGuid;

        struct CallbackEntry {
            uint32 id;
            AssetEventCallback callback;
        };
        std::vector<CallbackEntry> m_Callbacks;
        uint32 m_NextCallbackId = 1;
    };

} // namespace Engine
