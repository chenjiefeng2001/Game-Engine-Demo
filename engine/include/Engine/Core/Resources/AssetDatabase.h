#pragma once

/**
 * @file AssetDatabase.h
 * @brief 资源数据库 — JSON 清单 + 内存索引的资产管理系统
 *
 * 设计要点：
 *   - 每个资产有唯一 GUID（保证与路径解耦）
 *   - 支持按类型、标签、路径查询
 *   - 依赖图自动解析（材质→纹理等）
 *   - 清单文件为 JSON 格式，可读、可版本管理
 *   - 与 FileSystem::ResolvePath 配合使用
 *
 * 使用示例：
 * @code
 *   AssetDatabase db;
 *
 *   // 首次：扫描目录生成清单
 *   db.ScanDirectory("assets/");
 *   db.Save("config/asset_db.json");
 *
 *   // 后续：直接从清单加载
 *   db.Load("config/asset_db.json");
 *
 *   // 查询
 *   auto tex  = db.FindByPath("assets/textures/player.png");
 *   auto mats = db.QueryByType(ResourceType::Texture);
 *   auto charTex = db.QueryByTag("character");
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <nlohmann/json.hpp>

namespace Engine {

    // ============================================================
    // 资产条目
    // ============================================================
    struct AssetEntry {
        std::string guid;                   // 全局唯一标识符
        std::string path;                   // 相对于根目录的路径
        std::string fileName;               // 文件名（含扩展名）
        std::string extension;              // 扩展名（小写，不含点）
        ResourceType type = ResourceType::Unknown;
        std::vector<std::string> tags;      // 用户标签
        std::vector<std::string> dependencies;  // 依赖的 GUID 列表
        uint64      fileSize   = 0;
        int64       lastModified = 0;       // 时间戳
        bool        isValid    = true;
    };

    // ============================================================
    // 审计日志条目
    // ============================================================
    enum class AuditAction : uint8 {
        Created          = 0,
        Updated          = 1,
        Deleted          = 2,
        TagAdded         = 3,
        TagRemoved       = 4,
        DependencyAdded  = 5,
        DependencyRemoved= 6,
        IntegrityFix     = 7,
    };

    struct AuditEntry {
        int64       timestamp;
        AuditAction action;
        std::string assetGuid;
        std::string assetPath;
        std::string reason;
    };

    // ============================================================
    // 完整性检查结果
    // ============================================================
    struct IntegrityIssue {
        std::string sourceGuid;
        std::string sourcePath;
        std::string brokenGuid;
        std::string issueType;  // "missing_dependency" / "self_reference"
    };

    struct IntegrityCheckResult {
        std::vector<IntegrityIssue> issues;
        size_t totalReferences  = 0;
        size_t validReferences  = 0;
        size_t brokenReferences = 0;
        bool   IsClean() const { return issues.empty(); }
    };

    // ============================================================
    // 资源数据库
    // ============================================================
    class AssetDatabase {
    public:
        AssetDatabase() = default;
        ~AssetDatabase() = default;

        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        // ── 构建与持久化 ──

        /**
         * @brief 扫描目录生成资产清单
         * @param rootDir 资源根目录（如 "assets/"）
         * @param recursive 是否递归扫描子目录
         * @return 扫描到的资产数量
         *
         * 会根据文件扩展名自动识别资源类型，
         * 并为每个资产生成 GUID。
         */
        uint32 ScanDirectory(const std::string& rootDir, bool recursive = true);

        /**
         * @brief 从 JSON 清单文件加载
         * @param jsonPath 清单文件路径
         * @return 是否成功
         */
        bool Load(const std::string& jsonPath);

        /**
         * @brief 保存清单到 JSON 文件
         * @param jsonPath 输出路径
         * @return 是否成功
         */
        bool Save(const std::string& jsonPath) const;

        // ── 查询 ──

        /** 通过 GUID 查找资产 */
        const AssetEntry* FindByGuid(const std::string& guid) const;

        /** 通过路径查找资产 */
        const AssetEntry* FindByPath(const std::string& path) const;

        /** 查询指定类型的所有资产 */
        std::vector<const AssetEntry*> QueryByType(ResourceType type) const;

        /** 查询包含指定标签的所有资产 */
        std::vector<const AssetEntry*> QueryByTag(const std::string& tag) const;

        /** 组合查询：按类型 + 标签 */
        std::vector<const AssetEntry*> Query(ResourceType type,
                                             const std::string& tag = "") const;

        // ── 依赖管理 ──

        /**
         * @brief 获取指定资产的所有依赖（递归解析）
         * @param guid 资产 GUID
         * @return 依赖资产列表（包含间接依赖）
         */
        std::vector<const AssetEntry*> ResolveDependencies(const std::string& guid) const;

        /** 检查指定资产是否被其他资产引用 */
        std::vector<const AssetEntry*> GetReferencers(const std::string& guid) const;

        // ── 统计 ──

        size_t GetCount() const { return m_Entries.size(); }
        size_t GetCountByType(ResourceType type) const;
        bool   HasEntry(const std::string& guid) const;

        /** 获取所有资产的只读引用 */
        const std::vector<AssetEntry>& GetAllEntries() const { return m_Entries; }

        // ── 引用完整性 ──

        /** 检查所有依赖引用的 GUID 是否存在 */
        IntegrityCheckResult ValidateIntegrity() const;

        /** 自动修复损坏的引用（删除指向不存在 GUID 的依赖） */
        uint32 RepairIntegrity(const std::string& reason = "auto-repair");

        // ── 审计日志 ──

        /** 设置变更事由（在下次写入操作前调用） */
        void SetChangeReason(const std::string& reason) { m_PendingReason = reason; }

        /** 获取审计日志 */
        const std::vector<AuditEntry>& GetAuditLog() const { return m_AuditLog; }

        /** 清空审计日志 */
        void ClearAuditLog() { m_AuditLog.clear(); }

        /** 保存审计日志到 JSON 文件 */
        bool SaveAuditLog(const std::string& jsonPath) const;

    private:
        // ── 内部存储 ──
        std::vector<AssetEntry> m_Entries;

        // ── 索引 ──
        std::unordered_map<std::string, size_t>        m_GuidIndex;   // GUID → index
        std::unordered_map<std::string, size_t>        m_PathIndex;   // path → index
        std::unordered_multimap<ResourceType, size_t>  m_TypeIndex;   // type → index[]
        std::unordered_multimap<std::string, size_t>   m_TagIndex;    // tag → index[]

        // ── 审计日志 ──
        mutable std::vector<AuditEntry> m_AuditLog;
        mutable std::string            m_PendingReason;

        /** 记录一条审计日志 */
        void LogAudit(AuditAction action, const std::string& guid,
                      const std::string& path,
                      const std::string& overrideReason = "") const;

        /** 为指定索引位置重建所有索引 */
        void RebuildIndex(size_t index);

        /** 重建全部索引 */
        void RebuildAllIndexes();

        /** 从文件扩展名推断资源类型 */
        static ResourceType DeduceTypeFromExtension(const std::string& ext);

        /** 生成随机 GUID */
        static std::string GenerateGUID();
    };

} // namespace Engine
