#include "Engine/Core/Resources/AssetDatabase.h"
#include "Engine/Core/FileSystem.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Engine {

    // ============================================================
    // 文件扩展名 → ResourceType 映射
    // ============================================================

    ResourceType AssetDatabase::DeduceTypeFromExtension(const std::string& ext) {
        if (ext == "png" || ext == "jpg" || ext == "jpeg" ||
            ext == "bmp" || ext == "tga" || ext == "dds" ||
            ext == "hdr" || ext == "ktx")      return ResourceType::Texture;

        if (ext == "glsl" || ext == "vert" ||
            ext == "frag" || ext == "comp" ||
            ext == "spv")                       return ResourceType::Shader;

        if (ext == "wav" || ext == "ogg" ||
            ext == "mp3" || ext == "flac" ||
            ext == "aac")                       return ResourceType::AudioClip;

        if (ext == "ttf" || ext == "ttc" ||
            ext == "otf")                       return ResourceType::Font;

        return ResourceType::Unknown;
    }

    // ============================================================
    // GUID 生成
    // ============================================================

    std::string AssetDatabase::GenerateGUID() {
        static std::mt19937_64 rng(std::random_device{}());

        auto r = [&]() -> uint64 { return rng(); };

        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8)  << static_cast<uint32>(r() & 0xFFFFFFFF) << "-"
           << std::setw(4)  << static_cast<uint32>(r() & 0xFFFF)     << "-4"
           << std::setw(3)  << static_cast<uint32>(r() & 0x0FFF)     << "-"
           << std::setw(1)  << (8 | (r() & 3))
           << std::setw(3)  << static_cast<uint32>(r() & 0x0FFF)     << "-"
           << std::setw(12) << (r() & 0xFFFFFFFFFFFFULL);
        return ss.str();
    }

    // ============================================================
    // 目录扫描
    // ============================================================

    uint32 AssetDatabase::ScanDirectory(const std::string& rootDir, bool recursive) {
        m_Entries.clear();
        uint32 count = 0;

        auto files = FileSystem::ScanFiles(rootDir, "*", recursive);
        for (const auto& file : files) {
            AssetEntry entry;
            entry.path       = file.path;
            entry.fileName   = file.name;
            entry.extension  = file.extension;
            entry.type       = DeduceTypeFromExtension(file.extension);
            entry.fileSize   = file.size;
            entry.lastModified = FileSystem::GetLastWriteTime(file.path);
            entry.guid       = GenerateGUID();

            // 相对路径标准化
            // 对于 Windows，将反斜杠统一为正斜杠
            std::replace(entry.path.begin(), entry.path.end(), '\\', '/');

            m_Entries.push_back(std::move(entry));
            count++;
        }

        RebuildAllIndexes();

        std::cout << "[AssetDatabase] Scanned " << rootDir
                  << ": " << count << " assets found" << std::endl;

        // 记录审计日志
        for (const auto& entry : m_Entries)
            LogAudit(AuditAction::Created, entry.guid, entry.path, "scanned from disk");

        return count;
    }

    // ============================================================
    // JSON 持久化
    // ============================================================

    bool AssetDatabase::Load(const std::string& jsonPath) {
        std::string resolved = FileSystem::ResolvePath(jsonPath);
        std::string content = FileSystem::ReadTextFile(resolved);
        if (content.empty()) {
            std::cerr << "[AssetDatabase] Failed to load: " << resolved << std::endl;
            return false;
        }

        try {
            nlohmann::json json = nlohmann::json::parse(content);
            m_Entries.clear();

            for (const auto& item : json["assets"]) {
                AssetEntry entry;
                entry.guid       = item.value("guid", "");
                entry.path       = item.value("path", "");
                entry.fileName   = item.value("fileName", "");
                entry.extension  = item.value("extension", "");
                entry.type       = static_cast<ResourceType>(item.value("type", 0));
                entry.fileSize   = item.value("fileSize", 0ULL);
                entry.lastModified = item.value("lastModified", 0LL);
                entry.isValid    = item.value("isValid", true);

                if (item.contains("tags"))
                    entry.tags = item["tags"].get<std::vector<std::string>>();

                if (item.contains("dependencies"))
                    entry.dependencies = item["dependencies"].get<std::vector<std::string>>();

                m_Entries.push_back(std::move(entry));
            }

            RebuildAllIndexes();
            std::cout << "[AssetDatabase] Loaded " << m_Entries.size()
                      << " assets from " << resolved << std::endl;

            LogAudit(AuditAction::Updated, "", "database loaded from " + resolved);
            return true;

        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[AssetDatabase] JSON parse error: " << e.what() << std::endl;
            return false;
        }
    }

    bool AssetDatabase::Save(const std::string& jsonPath) const {
        nlohmann::json json;
        json["version"] = 1;
        json["created"] = std::time(nullptr);

        auto& assets = json["assets"] = nlohmann::json::array();
        for (const auto& entry : m_Entries) {
            nlohmann::json item;
            item["guid"]         = entry.guid;
            item["path"]         = entry.path;
            item["fileName"]     = entry.fileName;
            item["extension"]    = entry.extension;
            item["type"]         = static_cast<int>(entry.type);
            item["fileSize"]     = entry.fileSize;
            item["lastModified"] = entry.lastModified;
            item["isValid"]      = entry.isValid;

            if (!entry.tags.empty())
                item["tags"] = entry.tags;

            if (!entry.dependencies.empty())
                item["dependencies"] = entry.dependencies;

            assets.push_back(std::move(item));
        }

        std::string resolved = FileSystem::ResolvePath(jsonPath);
        bool ok = FileSystem::WriteTextFile(resolved, json.dump(2));
        if (ok) {
            std::cout << "[AssetDatabase] Saved " << m_Entries.size()
                      << " assets to " << resolved << std::endl;
        }
        return ok;
    }

    // ============================================================
    // 索引重建
    // ============================================================

    void AssetDatabase::RebuildIndex(size_t index) {
        if (index >= m_Entries.size()) return;
        const auto& e = m_Entries[index];

        m_GuidIndex[e.guid] = index;
        m_PathIndex[e.path] = index;
        m_TypeIndex.emplace(e.type, index);
        for (const auto& tag : e.tags)
            m_TagIndex.emplace(tag, index);
    }

    void AssetDatabase::RebuildAllIndexes() {
        m_GuidIndex.clear();
        m_PathIndex.clear();
        m_TypeIndex.clear();
        m_TagIndex.clear();

        for (size_t i = 0; i < m_Entries.size(); ++i)
            RebuildIndex(i);
    }

    // ============================================================
    // 查询
    // ============================================================

    const AssetEntry* AssetDatabase::FindByGuid(const std::string& guid) const {
        auto it = m_GuidIndex.find(guid);
        if (it != m_GuidIndex.end() && it->second < m_Entries.size())
            return &m_Entries[it->second];
        return nullptr;
    }

    const AssetEntry* AssetDatabase::FindByPath(const std::string& path) const {
        // 标准化路径
        std::string norm = path;
        std::replace(norm.begin(), norm.end(), '\\', '/');
        auto it = m_PathIndex.find(norm);
        if (it != m_PathIndex.end() && it->second < m_Entries.size())
            return &m_Entries[it->second];
        return nullptr;
    }

    std::vector<const AssetEntry*> AssetDatabase::QueryByType(ResourceType type) const {
        std::vector<const AssetEntry*> results;
        auto range = m_TypeIndex.equal_range(type);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second < m_Entries.size())
                results.push_back(&m_Entries[it->second]);
        }
        return results;
    }

    std::vector<const AssetEntry*> AssetDatabase::QueryByTag(const std::string& tag) const {
        std::vector<const AssetEntry*> results;
        auto range = m_TagIndex.equal_range(tag);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second < m_Entries.size())
                results.push_back(&m_Entries[it->second]);
        }
        return results;
    }

    std::vector<const AssetEntry*> AssetDatabase::Query(
        ResourceType type, const std::string& tag) const
    {
        if (tag.empty())
            return QueryByType(type);

        std::vector<const AssetEntry*> results;
        auto range = m_TypeIndex.equal_range(type);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second >= m_Entries.size()) continue;
            const auto& e = m_Entries[it->second];
            // 检查是否包含标签
            if (std::find(e.tags.begin(), e.tags.end(), tag) != e.tags.end())
                results.push_back(&e);
        }
        return results;
    }

    // ============================================================
    // 依赖管理
    // ============================================================

    std::vector<const AssetEntry*> AssetDatabase::ResolveDependencies(
        const std::string& guid) const
    {
        std::vector<const AssetEntry*> result;
        std::unordered_set<std::string> visited;

        // 递归解析
        std::function<void(const std::string&)> resolve =
            [&](const std::string& currentGuid) {
                if (!visited.insert(currentGuid).second) return; // 已访问
                auto* entry = FindByGuid(currentGuid);
                if (!entry) return;

                for (const auto& depGuid : entry->dependencies) {
                    auto* dep = FindByGuid(depGuid);
                    if (dep) {
                        result.push_back(dep);
                        resolve(depGuid);
                    }
                }
            };

        resolve(guid);
        return result;
    }

    std::vector<const AssetEntry*> AssetDatabase::GetReferencers(
        const std::string& guid) const
    {
        std::vector<const AssetEntry*> result;
        for (const auto& entry : m_Entries) {
            for (const auto& dep : entry.dependencies) {
                if (dep == guid) {
                    result.push_back(&entry);
                    break;
                }
            }
        }
        return result;
    }

    // ============================================================
    // 统计
    // ============================================================

    size_t AssetDatabase::GetCountByType(ResourceType type) const {
        return m_TypeIndex.count(type);
    }

    bool AssetDatabase::HasEntry(const std::string& guid) const {
        return m_GuidIndex.find(guid) != m_GuidIndex.end();
    }

    // ============================================================
    // 审计日志
    // ============================================================

    void AssetDatabase::LogAudit(AuditAction action, const std::string& guid,
                                  const std::string& path,
                                  const std::string& overrideReason) const {
        AuditEntry entry;
        entry.timestamp = std::time(nullptr);
        entry.action    = action;
        entry.assetGuid = guid;
        entry.assetPath = path;
        entry.reason    = overrideReason.empty() ? m_PendingReason : overrideReason;
        m_AuditLog.push_back(std::move(entry));
        m_PendingReason.clear();
    }

    bool AssetDatabase::SaveAuditLog(const std::string& jsonPath) const {
        nlohmann::json json = nlohmann::json::array();
        for (const auto& e : m_AuditLog) {
            json.push_back({
                {"timestamp",   e.timestamp},
                {"action",      static_cast<int>(e.action)},
                {"assetGuid",   e.assetGuid},
                {"assetPath",   e.assetPath},
                {"reason",      e.reason}
            });
        }
        std::string resolved = FileSystem::ResolvePath(jsonPath);
        return FileSystem::WriteTextFile(resolved, json.dump(2));
    }

    // ============================================================
    // 引用完整性
    // ============================================================

    IntegrityCheckResult AssetDatabase::ValidateIntegrity() const {
        IntegrityCheckResult result;

        std::unordered_set<std::string> allGuids;
        allGuids.reserve(m_Entries.size());
        for (const auto& e : m_Entries)
            allGuids.insert(e.guid);

        for (const auto& e : m_Entries) {
            for (const auto& dep : e.dependencies) {
                result.totalReferences++;

                if (dep == e.guid) {
                    result.brokenReferences++;
                    result.issues.push_back({e.guid, e.path, dep, "self_reference"});
                    continue;
                }

                if (allGuids.find(dep) == allGuids.end()) {
                    result.brokenReferences++;
                    result.issues.push_back({e.guid, e.path, dep, "missing_dependency"});
                    continue;
                }

                result.validReferences++;
            }
        }
        return result;
    }

    uint32 AssetDatabase::RepairIntegrity(const std::string& reason) {
        SetChangeReason(reason);
        uint32 fixed = 0;

        for (auto& e : m_Entries) {
            auto it = e.dependencies.begin();
            while (it != e.dependencies.end()) {
                bool found = false;
                for (const auto& existing : m_Entries) {
                    if (existing.guid == *it) {
                        found = true;
                        break;
                    }
                }
                if (!found || *it == e.guid) {
                    LogAudit(AuditAction::IntegrityFix, e.guid, e.path,
                             "removed broken reference to " + *it);
                    it = e.dependencies.erase(it);
                    fixed++;
                } else {
                    ++it;
                }
            }
        }

        if (fixed > 0) RebuildAllIndexes();
        std::cout << "[AssetDatabase] Repaired " << fixed
                  << " broken references (" << reason << ")" << std::endl;
        return fixed;
    }

} // namespace Engine
