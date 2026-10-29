/**
 * @file ContentAsset.cpp
 * @brief Content Registry 实现（Ring8/Ring9）
 */

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/AtomicFile.h"
#include "Engine/Core/Content/Utf8Path.h"
#include "Engine/Core/Log.h"

#include <fstream>
#include <random>

namespace Engine::Content {

namespace {
    Logger s_Log("ContentRegistry");
}

ResourceGUID ContentRegistry::Import(const std::string& path, AssetType type) {
    auto it = m_ByPath.find(path);
    if (it != m_ByPath.end()) return it->second;      // 幂等

    // 生成并确保唯一
    ResourceGUID guid = ResourceGUID::Create();
    while (m_ByGuid.count(guid.ToHex()))
        guid = ResourceGUID::Create();

    AssetEntry e;
    e.guid = guid; e.path = path; e.type = type;
    m_ByGuid[guid.ToHex()] = e;
    m_ByPath[path] = guid;
    return guid;
}

bool ContentRegistry::RegisterExplicit(const ResourceGUID& guid,
                                       const std::string& path, AssetType type) {
    if (guid.IsNull()) return false;

    // GUID 冲突检测：同 GUID 已被不同路径占用 → 拒绝
    auto gIt = m_ByGuid.find(guid.ToHex());
    if (gIt != m_ByGuid.end()) {
        if (gIt->second.path == path && gIt->second.type == type) return true; // 幂等
        s_Log.Error("RegisterExplicit: GUID {} already bound to '{}' (requested '{}')",
                    guid.ToHex(), gIt->second.path, path);
        return false;
    }

    // 路径冲突检测：同路径已有其他 GUID → 拒绝（身份必须唯一）
    auto pIt = m_ByPath.find(path);
    if (pIt != m_ByPath.end()) {
        s_Log.Error("RegisterExplicit: path '{}' already owned by GUID {}",
                    path, pIt->second.ToHex());
        return false;
    }

    AssetEntry e;
    e.guid = guid; e.path = path; e.type = type;
    m_ByGuid[guid.ToHex()] = e;
    m_ByPath[path] = guid;
    return true;
}

std::string ContentRegistry::ResolvePath(const ResourceGUID& guid) const {
    auto it = m_ByGuid.find(guid.ToHex());
    return it != m_ByGuid.end() ? it->second.path : std::string();
}

AssetType ContentRegistry::TypeOf(const ResourceGUID& guid) const {
    auto it = m_ByGuid.find(guid.ToHex());
    return it != m_ByGuid.end() ? it->second.type : AssetType::Texture;
}

bool ContentRegistry::ContainsGuid(const ResourceGUID& guid) const {
    return m_ByGuid.count(guid.ToHex()) > 0;
}

bool ContentRegistry::ContainsPath(const std::string& path) const {
    return m_ByPath.count(path) > 0;
}

void ContentRegistry::Unregister(const ResourceGUID& guid) {
    auto it = m_ByGuid.find(guid.ToHex());
    if (it != m_ByGuid.end()) {
        m_ByPath.erase(it->second.path);
        m_ByGuid.erase(it);
    }
}

std::string ContentRegistry::SerializeManifest() const {
    nlohmann::json j;
    j["version"] = 1;
    j["assets"] = nlohmann::json::array();
    for (const auto& [hex, e] : m_ByGuid) {
        j["assets"].push_back({
            { "guid", hex },
            { "path", e.path },
            { "type", ToString(e.type) },
        });
    }
    return j.dump(2);
}

bool ContentRegistry::SaveManifest(const std::string& filePath) const {
    const std::string body = SerializeManifest();
    const AtomicFileResult written = WriteFileAtomic(filePath, body);
    if (!written.ok) {
        s_Log.Error("SaveManifest: cannot write '{}': {} ({})", filePath,
                    ToString(written.error), written.diagnostic);
        return false;
    }
    return true;
}

bool ContentRegistry::LoadManifest(const std::string& filePath,
                                   const ManifestLoadOptions& options) {
    std::ifstream f(PathFromUtf8(filePath), std::ios::binary);
    if (!f.is_open()) {
        s_Log.Error("LoadManifest: cannot open '{}'", filePath);
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception&) {
        s_Log.Error("LoadManifest: corrupted manifest '{}'", filePath);
        return false;
    }

    constexpr int64_t kSupportedVersion = 1;
    if (!j.is_object() || !j.contains("version") ||
        !(j["version"].is_number_integer() ||
          j["version"].is_number_unsigned())) {
        s_Log.Error("LoadManifest: missing/invalid version in '{}'", filePath);
        return false;
    }
    int64_t version = 0;
    try {
        version = j["version"].get<int64_t>();
    } catch (const std::exception&) {
        s_Log.Error("LoadManifest: unreadable version in '{}'", filePath);
        return false;
    }
    if (version != kSupportedVersion) {
        s_Log.Error("LoadManifest: unsupported version {} in '{}'", version,
                    filePath);
        return false;
    }
    if (!j.contains("assets") || !j["assets"].is_array()) {
        s_Log.Error("LoadManifest: malformed manifest '{}'", filePath);
        return false;
    }

    // M1-B：先构造完整候选，任一条目失败即整体失败；成功后才交换。
    ContentRegistry candidate;
    std::unordered_map<std::string, std::string> guidOwners;
    std::unordered_map<std::string, std::string> pathOwners;

    size_t index = 0;
    for (const auto& a : j["assets"]) {
        const std::string where =
            "asset #" + std::to_string(index++) + " in '" + filePath + "'";
        if (!a.is_object() || !a.contains("guid") || !a.contains("path") ||
            !a.contains("type") || !a["guid"].is_string() ||
            !a["path"].is_string() || !a["type"].is_string()) {
            s_Log.Error("LoadManifest: {} missing/invalid required fields",
                        where);
            return false;
        }

        const std::string hex = a["guid"].get<std::string>();
        const ResourceGUID guid = ResourceGUID::FromHex(hex);
        if (guid.IsNull()) {
            s_Log.Error("LoadManifest: {} has invalid GUID '{}'", where, hex);
            return false;
        }
        const std::string canonicalHex = guid.ToHex();
        auto guidIt = guidOwners.find(canonicalHex);
        if (guidIt != guidOwners.end()) {
            s_Log.Error("LoadManifest: {} duplicates GUID {} (paths '{}' and "
                        "'{}')",
                        where, canonicalHex, guidIt->second,
                        a["path"].get<std::string>());
            return false;
        }

        const std::string typeName = a["type"].get<std::string>();
        AssetType type;
        if (typeName == "Texture") {
            type = AssetType::Texture;
        } else if (typeName == "Script") {
            type = AssetType::Script;
        } else {
            s_Log.Error("LoadManifest: {} has unknown type '{}'", where,
                        typeName);
            return false;
        }

        const std::string authoredPath = a["path"].get<std::string>();
        std::string canonicalPath;
        if (options.root) {
            const PathResolution resolved =
                options.root->ToCanonicalProjectPath(authoredPath);
            if (resolved.ok) {
                canonicalPath = resolved.path;
            } else if (options.allowLegacyAbsolute &&
                       resolved.error == PathError::EscapesRoot) {
                // K4.4 兼容：仅根外旧绝对路径保留为未解析；结构违规不降级。
                canonicalPath = authoredPath;
                if (options.warnings) {
                    options.warnings->push_back(
                        "asset " + canonicalHex + ": path '" + authoredPath +
                        "' kept unresolved (" + ToString(resolved.error) +
                        "); legacy absolute outside project root");
                }
            } else {
                s_Log.Error("LoadManifest: {} path '{}' rejected: {}", where,
                            authoredPath, ToString(resolved.error));
                return false;
            }
        } else {
            const PathResolution normalized =
                NormalizeProjectRelative(authoredPath);
            if (!normalized.ok) {
                s_Log.Error("LoadManifest: {} path '{}' rejected: {}", where,
                            authoredPath, ToString(normalized.error));
                return false;
            }
            canonicalPath = normalized.path;
        }

        const std::string pathKey =
            options.root ? options.root->ComparisonKey(canonicalPath)
                         : PathKey(canonicalPath, options.caseMode);
        auto pathIt = pathOwners.find(pathKey);
        if (pathIt != pathOwners.end()) {
            s_Log.Error("LoadManifest: {} path '{}' collides with '{}' under "
                        "PathKey",
                        where, canonicalPath, pathIt->second);
            return false;
        }

        if (!candidate.RegisterExplicit(guid, canonicalPath, type)) {
            s_Log.Error("LoadManifest: {} identity conflict for GUID {}", where,
                        canonicalHex);
            return false;
        }

        guidOwners.emplace(canonicalHex, canonicalPath);
        pathOwners.emplace(pathKey, canonicalPath);
    }

    *this = std::move(candidate);
    return true;
}

} // namespace Engine::Content
