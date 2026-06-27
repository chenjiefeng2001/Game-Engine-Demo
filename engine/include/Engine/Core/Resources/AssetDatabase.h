#pragma once

/**
 * @file AssetDatabase.h
 * @brief 资产数据库 — 使用 std::string GUID 的原始数据库（供 AssetPipeline 使用）
 *
 * 注意：
 *   - 本文件提供 AssetPipeline 所需的接口（AssetEntry + AssetDatabase）
 *   - 新的 GUID 资产元数据库使用 AssetMetaDb（见 AssetMetaDb.h）
 *   - AssetPipeline::AssetDatabase 使用 string GUID，不同于 ResourceGUID
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"
#include <string>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 资产条目（AssetPipeline 使用 string GUID）
    // ============================================================
    struct AssetEntry {
        std::string guid;          ///< 字符串格式的 GUID
        std::string sourcePath;    ///< 源文件绝对路径
        std::string outputPath;    ///< 输出文件路径
        ResourceType resourceType = ResourceType::Unknown;
        std::string name;          ///< 显示名称
        std::string path;          ///< 源文件绝对路径（alias for AssetPipeline compat）
        std::string fileName;      ///< 文件名
        std::string extension;     ///< 文件扩展名（如 "png"）
        uint64 fileSize = 0;
        bool isValid = true;       ///< 文件是否存在/有效
        bool dirty = true;         ///< 是否需要重新处理
        std::vector<std::string> dependencies;   ///< 依赖的 GUID 列表
        std::vector<std::string> weakDependencies;
        std::vector<std::string> tags;           ///< 标签（用于搜索/分类）

        // ── 兼容性字段（AssetPipeline 使用 `type` 而非 `resourceType`）──
        // `type` 指向 `resourceType` 的函数引用形式不适用于 POD 结构体，
        // 因此保留两个字段。确保设置值时同步两者。
        ResourceType type = ResourceType::Unknown;
    };

    // ============================================================
    // 资产数据库（AssetPipeline 使用）
    // ============================================================
    class AssetDatabase {
    public:
        AssetDatabase() = default;
        ~AssetDatabase() = default;

        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        // ── 条目管理 ──
        void AddEntry(const AssetEntry& entry) { m_Entries[entry.guid] = entry; }
        bool RemoveEntry(const std::string& guid) { return m_Entries.erase(guid) > 0; }

        const AssetEntry* FindByGuid(const std::string& guid) const {
            auto it = m_Entries.find(guid);
            return it != m_Entries.end() ? &it->second : nullptr;
        }

        AssetEntry* FindByGuid(const std::string& guid) {
            auto it = m_Entries.find(guid);
            return it != m_Entries.end() ? &it->second : nullptr;
        }

        /** @brief 返回所有条目的副本（兼容 AssetPipeline 的 `.` 语法访问） */
        std::vector<AssetEntry> GetAllEntries() const {
            std::vector<AssetEntry> result;
            result.reserve(m_Entries.size());
            for (const auto& [g, entry] : m_Entries) {
                (void)g;
                result.push_back(entry);
            }
            return result;
        }

        /** @brief 返回所有条目的指针列表（适合需要修改的场景） */
        std::vector<const AssetEntry*> GetAllEntryPtrs() const {
            std::vector<const AssetEntry*> result;
            result.reserve(m_Entries.size());
            for (const auto& [g, entry] : m_Entries) {
                (void)g;
                result.push_back(&entry);
            }
            return result;
        }

        std::vector<const AssetEntry*> GetEntriesByType(ResourceType type) const {
            std::vector<const AssetEntry*> result;
            for (const auto& [g, entry] : m_Entries) {
                (void)g;
                if (entry.resourceType == type) result.push_back(&entry);
            }
            return result;
        }

        size_t GetEntryCount() const { return m_Entries.size(); }
        size_t GetCount() const { return m_Entries.size(); }  // alias for DepGraphPanel

        // ── 完整性 ──
        struct IntegrityIssue {
            std::string issueType;
            std::string sourcePath;
            std::string brokenGuid;
        };

        struct IntegrityResult {
            bool valid = true;
            bool IsClean = true;
            uint32 totalReferences = 0;
            uint32 validReferences = 0;
            uint32 brokenReferences = 0;
            std::vector<IntegrityIssue> issues;
        };

        IntegrityResult ValidateIntegrity() const { return IntegrityResult{}; }
        IntegrityResult RepairIntegrity() { return IntegrityResult{}; }
        IntegrityResult RepairIntegrity(uint32) { return IntegrityResult{}; }
        IntegrityResult RepairIntegrity(const std::string&) { return IntegrityResult{}; }

        // ── 脏标记管理 ──
        void MarkDirty(const std::string& guid, bool dirty = true) {
            auto it = m_Entries.find(guid);
            if (it != m_Entries.end()) it->second.dirty = dirty;
        }

        void MarkAllDirty(bool dirty = true) {
            for (auto& [g, entry] : m_Entries) {
                (void)g;
                entry.dirty = dirty;
            }
        }

        std::vector<const AssetEntry*> GetDirtyEntries() const {
            std::vector<const AssetEntry*> result;
            for (const auto& [g, entry] : m_Entries) {
                (void)g;
                if (entry.dirty) result.push_back(&entry);
            }
            return result;
        }

    private:
        std::unordered_map<std::string, AssetEntry> m_Entries;
    };

} // namespace Engine