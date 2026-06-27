#include "Engine/Core/Resources/AssetMetaDb.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Engine {

    // ── 单例 ──
    AssetMetaDb* AssetMetaDb::s_Instance = nullptr;
    std::unique_ptr<AssetMetaDb> AssetMetaDb::s_InstanceOwner;

    // ── 可扫描的资产文件扩展名 ──
    static const std::unordered_set<std::string> s_AssetExtensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tif", ".tiff",
        ".wav", ".mp3", ".ogg", ".flac",
        ".glsl", ".vert", ".frag", ".comp",
        ".obj", ".fbx", ".gltf", ".glb",
        ".ttf", ".otf",
        ".scene", ".json",
    };

    // ═══════════════════════════════════════════════════════════════
    // 构造 & 初始化
    // ═══════════════════════════════════════════════════════════════

    AssetMetaDb::AssetMetaDb(const std::string& assetRoot)
        : m_AssetRoot(assetRoot) {
        if (!m_AssetRoot.empty() && m_AssetRoot.back() != '/' && m_AssetRoot.back() != '\\') {
            m_AssetRoot += '/';
        }
    }

    void AssetMetaDb::Init(const std::string& assetRoot) {
        if (s_Instance) {
            Log::Warn("[AssetMetaDb] Already initialized");
            Shutdown();
        }
        s_InstanceOwner = std::unique_ptr<AssetMetaDb>(new AssetMetaDb(assetRoot));
        s_Instance = s_InstanceOwner.get();
        s_Instance->ScanAll();
    }

    void AssetMetaDb::Shutdown() {
        s_Instance = nullptr;
        s_InstanceOwner.reset();
    }

    // ═══════════════════════════════════════════════════════════════
    // GUID ↔ Path 查询
    // ═══════════════════════════════════════════════════════════════

    const std::string* AssetMetaDb::GetPath(const ResourceGUID& guid) const {
        auto it = m_GuidToMeta.find(guid);
        if (it != m_GuidToMeta.end()) return &it->second.path;
        return nullptr;
    }

    ResourceGUID AssetMetaDb::GetGUID(const std::string& path) const {
        std::string n = path;
        std::replace(n.begin(), n.end(), '\\', '/');
        auto it = m_PathToGuid.find(n);
        return it != m_PathToGuid.end() ? it->second : ResourceGUID::Null;
    }

    bool AssetMetaDb::HasGUID(const ResourceGUID& guid) const {
        return m_GuidToMeta.find(guid) != m_GuidToMeta.end();
    }

    bool AssetMetaDb::HasPath(const std::string& path) const {
        return m_PathToGuid.find(path) != m_PathToGuid.end();
    }

    // ═══════════════════════════════════════════════════════════════
    // 资产扫描
    // ═══════════════════════════════════════════════════════════════

    void AssetMetaDb::ScanAll() {
        m_GuidToMeta.clear();
        m_PathToGuid.clear();
        if (!fs::exists(m_AssetRoot)) return;

        for (const auto& entry : fs::recursive_directory_iterator(m_AssetRoot, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;

            auto pathStr = entry.path().lexically_normal().string();
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            if (pathStr.size() > 5 && pathStr.substr(pathStr.size() - 5) == ".meta") continue;

            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (s_AssetExtensions.find(ext) == s_AssetExtensions.end()) continue;

            EnsureMeta(pathStr);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 元数据管理
    // ═══════════════════════════════════════════════════════════════

    ResourceGUID AssetMetaDb::EnsureMeta(const std::string& filePath) {
        auto it = m_PathToGuid.find(filePath);
        if (it != m_PathToGuid.end()) return it->second;

        std::string metaPath = filePath + ".meta";
        AssetMetaEntry meta;
        meta.path = filePath;
        meta.type = GuessResourceType(filePath);

        if (!ReadMetaFile(metaPath, meta)) {
            meta.guid = ResourceGUID::Create();
            WriteMetaFile(metaPath, meta);
        }

        m_GuidToMeta[meta.guid] = meta;
        m_PathToGuid[filePath] = meta.guid;
        return meta.guid;
    }

    const AssetMetaEntry* AssetMetaDb::GetMeta(const ResourceGUID& guid) const {
        auto it = m_GuidToMeta.find(guid);
        return it != m_GuidToMeta.end() ? &it->second : nullptr;
    }

    // ═══════════════════════════════════════════════════════════════
    // 查询
    // ═══════════════════════════════════════════════════════════════

    std::vector<std::string> AssetMetaDb::GetAllPaths() const {
        std::vector<std::string> result;
        for (const auto& [p, g] : m_PathToGuid) { (void)g; result.push_back(p); }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<ResourceGUID> AssetMetaDb::GetGuidsByType(ResourceType type) const {
        std::vector<ResourceGUID> result;
        for (const auto& [g, m] : m_GuidToMeta) {
            if (m.type == type) result.push_back(g);
        }
        return result;
    }

    std::vector<std::string> AssetMetaDb::FindPaths(const std::string& pattern) const {
        std::vector<std::string> result;
        bool hp = pattern.front() == '*', hs = pattern.back() == '*';
        auto s = pattern;
        if (hp) s = s.substr(1);
        if (hs) s = s.substr(0, s.size() - (hs ? 1 : 0));
        for (const auto& [p, g] : m_PathToGuid) {
            (void)g;
            bool m = true;
            if (hp && hs) m = p.find(s) != std::string::npos;
            else if (hs) m = p.size() >= s.size() && p.compare(0, s.size(), s) == 0;
            else if (hp) m = p.size() >= s.size() && p.compare(p.size()-s.size(), s.size(), s) == 0;
            else m = p == pattern;
            if (m) result.push_back(p);
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════════
    // .meta 文件 I/O
    // ═══════════════════════════════════════════════════════════════

    bool AssetMetaDb::ReadMetaFile(const std::string& metaPath, AssetMetaEntry& outMeta) {
        std::ifstream file(metaPath);
        if (!file.is_open()) return false;
        try {
            auto json = nlohmann::json::parse(file);
            if (json.contains("guid") && json["guid"].is_string())
                outMeta.guid = ResourceGUID::FromString(json["guid"]);
            else return false;
            if (json.contains("type") && json["type"].is_string())
                outMeta.type = ResourceType::Texture; // simplified
            return true;
        } catch (...) { return false; }
    }

    bool AssetMetaDb::WriteMetaFile(const std::string& metaPath, const AssetMetaEntry& meta) {
        nlohmann::json j;
        j["guid"] = meta.guid.ToString();
        j["type"] = ResourceTypeName(meta.type);
        std::ofstream f(metaPath);
        if (!f.is_open()) return false;
        f << j.dump(4);
        return true;
    }

    ResourceType AssetMetaDb::GuessResourceType(const std::string& path) const {
        auto dot = path.rfind('.');
        if (dot == std::string::npos) return ResourceType::Unknown;
        auto ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tga") return ResourceType::Texture;
        if (ext==".wav"||ext==".mp3"||ext==".ogg"||ext==".flac") return ResourceType::AudioClip;
        if (ext==".glsl"||ext==".vert"||ext==".frag"||ext==".comp") return ResourceType::Shader;
        if (ext==".ttf"||ext==".otf") return ResourceType::Font;
        return ResourceType::Unknown;
    }

    // ═══════════════════════════════════════════════════════════════
    // 事件
    // ═══════════════════════════════════════════════════════════════

    uint32 AssetMetaDb::BindEventCallback(AssetEventCallback callback) {
        auto id = m_NextCallbackId++;
        m_Callbacks.push_back({id, std::move(callback)});
        return id;
    }

    void AssetMetaDb::UnbindEventCallback(uint32 id) {
        std::erase_if(m_Callbacks, [id](const auto& e) { return e.id == id; });
    }

    void AssetMetaDb::FireEvent(AssetEvent event, const AssetMetaEntry& meta) {
        for (const auto& e : m_Callbacks)
            if (e.callback) e.callback(event, meta);
    }

} // namespace Engine