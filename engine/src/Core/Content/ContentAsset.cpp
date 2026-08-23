/**
 * @file ContentAsset.cpp
 * @brief Content Registry 实现（Ring8/Ring9）
 */

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Log.h"

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

bool ContentRegistry::SaveManifest(const std::string& filePath) const {
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
    std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return f.good();
}

bool ContentRegistry::LoadManifest(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) return false;

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception&) {
        s_Log.Error("LoadManifest: corrupted manifest '{}'", filePath);
        return false;                                   // 保持空表（fail-clean）
    }

    if (!j.contains("assets") || !j["assets"].is_array()) {
        s_Log.Error("LoadManifest: malformed manifest '{}'", filePath);
        return false;
    }

    Clear();
    for (const auto& a : j["assets"]) {
        if (!a.contains("guid") || !a.contains("path") || !a.contains("type")) continue;
        const std::string hex = a["guid"].get<std::string>();
        ResourceGUID guid = ResourceGUID::FromHex(hex);
        if (guid.IsNull()) continue;
        AssetEntry e;
        e.guid = guid;
        e.path = a["path"].get<std::string>();
        e.type = (a["type"].get<std::string>() == "Script")
                     ? AssetType::Script : AssetType::Texture;
        m_ByGuid[hex] = e;
        m_ByPath[e.path] = guid;
    }
    return true;
}

} // namespace Engine::Content
