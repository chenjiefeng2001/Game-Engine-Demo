#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <algorithm>
#include <sstream>

namespace Engine {

    AssetDatabase& AssetDatabase::Get() {
        static AssetDatabase instance;
        return instance;
    }

    void AssetDatabase::Initialize(const std::string& assetRoot, const std::string& libraryPath) {
        m_AssetRoot = assetRoot;
        m_LibraryPath = libraryPath;
        m_Initialized = true;

        // 确保 Library 目录存在
        std::filesystem::create_directories(m_LibraryPath);

        // 加载缓存
        LoadCache();

        Log::Info("[AssetDB] Initialized. Root: {}, Library: {}", assetRoot, libraryPath);
    }

    void AssetDatabase::ScanSync() {
        if (!m_Initialized) return;

        m_Scanning = true;
        Log::Info("[AssetDB] Starting full scan...");

        if (m_Callbacks.onScanStarted) m_Callbacks.onScanStarted();

        m_GuidToMeta.clear();
        m_PathToGuid.clear();
        m_VirtualPathToGuid.clear();

        ScanDirectory(m_AssetRoot, true);

        // 保存缓存
        SaveCache();

        m_Scanning = false;
        if (m_Callbacks.onScanCompleted) m_Callbacks.onScanCompleted();

        Log::Info("[AssetDB] Scan complete. {} assets found.", m_GuidToMeta.size());
    }

    void AssetDatabase::ScanAsync() {
        if (!m_Initialized || m_Scanning) return;

        m_Scanning = true;

        if (m_Callbacks.onScanStarted) m_Callbacks.onScanStarted();

        std::thread([this]() {
            // 清除旧数据并重新扫描
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_GuidToMeta.clear();
            m_PathToGuid.clear();
            m_VirtualPathToGuid.clear();
            ScanDirectory(m_AssetRoot, true);
            SaveCache();
            m_Scanning = false;

            if (m_Callbacks.onScanCompleted) m_Callbacks.onScanCompleted();
        }).detach();
    }

    void AssetDatabase::ScanIncremental() {
        if (!m_Initialized || m_Scanning) return;

        m_Scanning = true;

        std::thread([this]() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            // 遍历所有已知资产，检查是否有文件修改
            for (auto& [guid, meta] : m_GuidToMeta) {
                std::error_code ec;
                auto ftime = std::filesystem::last_write_time(meta.originalPath, ec);
                if (ec) continue;

                auto newTime = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::time_point_cast<std::chrono::seconds>(
                        std::chrono::file_clock::to_utc(ftime)).time_since_epoch()).count();

                if (newTime != meta.sourceFileTime) {
                    // 文件已修改，重新导入
                    Log::Info("[AssetDB] File modified: {}", meta.originalPath);
                    meta.sourceFileTime = newTime;
                    if (m_Callbacks.onAssetImported)
                        m_Callbacks.onAssetImported(guid, meta.originalPath);
                }
            }
            m_Scanning = false;
        }).detach();
    }

    void AssetDatabase::ScanDirectory(const std::filesystem::path& dir, bool recursive) {
        try {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) continue;

                const auto& path = entry.path();
                if (path.filename().string().starts_with('.')) continue;

                if (entry.is_directory(ec)) {
                    if (recursive) ScanDirectory(path, true);
                } else if (entry.is_regular_file(ec)) {
                    // 跳过 .meta 文件
                    if (path.extension() == ".meta") continue;
                    ProcessFile(path);
                }
            }
        } catch (const std::exception& e) {
            Log::Error("[AssetDB] Scan error: {}", e.what());
        }
    }

    void AssetDatabase::ProcessFile(const std::filesystem::path& filePath) {
        std::string pathStr = filePath.string();
        std::string ext = filePath.extension().string();
        std::string filename = filePath.filename().string();

        // 检查是否有 .meta 文件
        std::string metaPath = GetMetaPath(pathStr);
        AssetMeta meta;

        if (std::filesystem::exists(metaPath)) {
            // 读取已有 .meta
            if (!ReadMetaFile(metaPath, meta)) {
                Log::Warning("[AssetDB] Failed to read meta: {}", metaPath);
                return;
            }
        } else {
            // 创建新 .meta
            meta.guid = GUID::Generate();
            meta.type = AssetTypeFromExtension(ext);
            meta.originalPath = pathStr;
            meta.lastModifiedTime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::time_point_cast<std::chrono::seconds>(
                    std::chrono::file_clock::to_utc(
                        std::filesystem::last_write_time(filePath)))
                    .time_since_epoch()).count();
            meta.fileSize = std::filesystem::file_size(filePath);
            meta.isDirectory = false;

            WriteMetaFile(metaPath, meta);
        }

        // 更新虚拟路径
        meta.virtualPath = ToVirtualPath(pathStr);

        // 存入数据库
        m_GuidToMeta[meta.guid] = meta;
        m_PathToGuid[pathStr] = meta.guid;
        m_VirtualPathToGuid[meta.virtualPath] = meta.guid;

        // 触发导入回调
        if (m_Callbacks.onAssetImported) {
            m_Callbacks.onAssetImported(meta.guid, pathStr);
        }
    }

    void AssetDatabase::Update() {
        // 处理异步扫描结果（当前实现中异步线程直接写入了数据）
        // 如果使用队列模式，这里消费 m_PendingResults
    }

    // ── GUID 查询 ──

    GUID AssetDatabase::GetGUID(const std::string& physicalPath) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_PathToGuid.find(physicalPath);
        if (it != m_PathToGuid.end()) return it->second;
        return GUID{};
    }

    GUID AssetDatabase::GetGUIDByVirtual(const std::string& virtualPath) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_VirtualPathToGuid.find(virtualPath);
        if (it != m_VirtualPathToGuid.end()) return it->second;
        return GUID{};
    }

    std::string AssetDatabase::GetPhysicalPath(const GUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_GuidToMeta.find(guid);
        if (it != m_GuidToMeta.end()) return it->second.originalPath;
        return "";
    }

    std::string AssetDatabase::GetVirtualPath(const GUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_GuidToMeta.find(guid);
        if (it != m_GuidToMeta.end()) return it->second.virtualPath;
        return "";
    }

    bool AssetDatabase::HasGUID(const GUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_GuidToMeta.find(guid) != m_GuidToMeta.end();
    }

    bool AssetDatabase::HasPath(const std::string& physicalPath) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_PathToGuid.find(physicalPath) != m_PathToGuid.end();
    }

    // ── 元数据访问 ──

    const AssetMeta* AssetDatabase::GetMeta(const GUID& guid) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_GuidToMeta.find(guid);
        if (it != m_GuidToMeta.end()) return &it->second;
        return nullptr;
    }

    AssetMeta* AssetDatabase::GetMetaMutable(const GUID& guid) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_GuidToMeta.find(guid);
        if (it != m_GuidToMeta.end()) return &it->second;
        return nullptr;
    }

    AssetType AssetDatabase::GetAssetType(const GUID& guid) const {
        auto* meta = GetMeta(guid);
        return meta ? meta->type : AssetType::Unknown;
    }

    AssetType AssetDatabase::GetAssetType(const std::string& path) const {
        return AssetTypeFromExtension(std::filesystem::path(path).extension().string());
    }

    // ── 资产枚举 ──

    std::vector<GUID> AssetDatabase::GetAllAssets(AssetType typeFilter) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<GUID> result;
        for (const auto& [guid, meta] : m_GuidToMeta) {
            if (typeFilter == AssetType::Unknown || meta.type == typeFilter) {
                result.push_back(guid);
            }
        }
        return result;
    }

    std::vector<GUID> AssetDatabase::GetAssetsInDirectory(const std::string& virtualDir) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<GUID> result;
        std::string dir = virtualDir;
        if (!dir.empty() && dir.back() != '/') dir += '/';

        for (const auto& [guid, meta] : m_GuidToMeta) {
            if (meta.virtualPath.starts_with(dir)) {
                result.push_back(guid);
            }
        }
        return result;
    }

    std::vector<std::string> AssetDatabase::GetAllDirectories() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::set<std::string> dirs;
        for (const auto& [guid, meta] : m_GuidToMeta) {
            auto pos = meta.virtualPath.find_last_of('/');
            if (pos != std::string::npos) {
                dirs.insert(meta.virtualPath.substr(0, pos));
            }
        }
        return {dirs.begin(), dirs.end()};
    }

    std::vector<GUID> AssetDatabase::GetUnusedAssets() const {
        // 简化实现：没有依赖项或依赖项全部为空的资产视为"未使用"
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<GUID> result;
        for (const auto& [guid, meta] : m_GuidToMeta) {
            auto depIt = m_Dependencies.find(guid);
            if (depIt == m_Dependencies.end() || depIt->second.usedBy.empty()) {
                result.push_back(guid);
            }
        }
        return result;
    }

    // ── .meta 文件管理 ──

    std::string AssetDatabase::GetMetaPath(const std::string& physicalPath) {
        return physicalPath + ".meta";
    }

    bool AssetDatabase::CreateMetaFile(const std::string& physicalPath) {
        if (HasPath(physicalPath)) return true;

        AssetMeta meta;
        meta.guid = GUID::Generate();
        meta.type = AssetTypeFromExtension(std::filesystem::path(physicalPath).extension().string());
        meta.originalPath = physicalPath;
        meta.virtualPath = ToVirtualPath(physicalPath);

        std::string metaPath = GetMetaPath(physicalPath);
        return WriteMetaFile(metaPath, meta);
    }

    bool AssetDatabase::ReadMetaFile(const std::string& metaPath, AssetMeta& outMeta) const {
        try {
            std::ifstream file(metaPath);
            if (!file.is_open()) return false;

            nlohmann::json j;
            file >> j;

            outMeta.guid = GUID::FromString(j.value("guid", ""));
            outMeta.type = static_cast<AssetType>(j.value("type", 0));
            outMeta.originalPath = j.value("originalPath", "");
            outMeta.virtualPath = j.value("virtualPath", "");
            outMeta.fileSize = j.value("fileSize", 0LL);
            outMeta.lastModifiedTime = j.value("lastModified", 0LL);
            outMeta.importedTime = j.value("importedTime", 0LL);
            outMeta.sourceFileTime = j.value("sourceFileTime", 0LL);
            outMeta.isDirectory = j.value("isDirectory", false);

            // 导入设置
            if (j.contains("textureSettings")) {
                const auto& ts = j["textureSettings"];
                outMeta.textureSettings.format = static_cast<TextureImportSettings::Format>(
                    ts.value("format", 0));
                outMeta.textureSettings.sRGB = ts.value("sRGB", true);
                outMeta.textureSettings.generateMipMaps = ts.value("generateMipMaps", true);
                outMeta.textureSettings.maxResolution = ts.value("maxResolution", 4096);
                outMeta.textureSettings.isNormalMap = ts.value("isNormalMap", false);
                outMeta.textureSettings.isSprite = ts.value("isSprite", false);
            }

            return true;
        } catch (const std::exception& e) {
            Log::Error("[AssetDB] Failed to read meta file: {}", e.what());
            return false;
        }
    }

    bool AssetDatabase::WriteMetaFile(const std::string& metaPath, const AssetMeta& meta) const {
        try {
            nlohmann::json j;
            j["guid"] = meta.guid.ToString();
            j["type"] = static_cast<int>(meta.type);
            j["originalPath"] = meta.originalPath;
            j["virtualPath"] = meta.virtualPath;
            j["fileSize"] = meta.fileSize;
            j["lastModified"] = meta.lastModifiedTime;
            j["importedTime"] = meta.importedTime;
            j["sourceFileTime"] = meta.sourceFileTime;
            j["isDirectory"] = meta.isDirectory;

            // 纹理导入设置
            nlohmann::json ts;
            ts["format"] = static_cast<int>(meta.textureSettings.format);
            ts["sRGB"] = meta.textureSettings.sRGB;
            ts["generateMipMaps"] = meta.textureSettings.generateMipMaps;
            ts["maxResolution"] = meta.textureSettings.maxResolution;
            ts["isNormalMap"] = meta.textureSettings.isNormalMap;
            ts["isSprite"] = meta.textureSettings.isSprite;
            j["textureSettings"] = ts;

            std::ofstream file(metaPath);
            if (!file.is_open()) return false;
            file << j.dump(4);
            return true;
        } catch (const std::exception& e) {
            Log::Error("[AssetDB] Failed to write meta file: {}", e.what());
            return false;
        }
    }

    // ── 缓存管理 ──

    void AssetDatabase::LoadCache() {
        std::string cachePath = GetCachePath();
        if (!std::filesystem::exists(cachePath)) return;

        try {
            std::ifstream file(cachePath);
            if (!file.is_open()) return;

            nlohmann::json j;
            file >> j;

            m_Collections.clear();
            if (j.contains("collections")) {
                for (const auto& cj : j["collections"]) {
                    AssetCollection col;
                    col.name = cj.value("name", "");
                    col.description = cj.value("description", "");
                    col.isBuiltin = cj.value("isBuiltin", false);
                    for (const auto& g : cj["assets"]) {
                        col.assetGUIDs.push_back(GUID::FromString(g));
                    }
                    m_Collections.push_back(col);
                }
            }
        } catch (const std::exception& e) {
            Log::Warning("[AssetDB] Failed to load cache: {}", e.what());
        }
    }

    void AssetDatabase::SaveCache() {
        std::string cachePath = GetCachePath();
        try {
            nlohmann::json j;
            j["assetRoot"] = m_AssetRoot;
            j["scanTime"] = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 收藏集
            nlohmann::json cols = nlohmann::json::array();
            for (const auto& col : m_Collections) {
                nlohmann::json cj;
                cj["name"] = col.name;
                cj["description"] = col.description;
                cj["isBuiltin"] = col.isBuiltin;
                nlohmann::json assets = nlohmann::json::array();
                for (const auto& g : col.assetGUIDs) {
                    assets.push_back(g.ToString());
                }
                cj["assets"] = assets;
                cols.push_back(cj);
            }
            j["collections"] = cols;
            j["assetCount"] = m_GuidToMeta.size();

            std::ofstream file(cachePath);
            if (file.is_open()) {
                file << j.dump(4);
            }
        } catch (const std::exception& e) {
            Log::Error("[AssetDB] Failed to save cache: {}", e.what());
        }
    }

    std::string AssetDatabase::GetCachePath() const {
        return (std::filesystem::path(m_LibraryPath) / "AssetDatabaseCache.json").string();
    }

    // ── 虚拟路径 ──

    std::string AssetDatabase::ToVirtualPath(const std::string& physicalPath) const {
        // 将物理路径中资产根目录部分替换为 "Assets/"
        auto pos = physicalPath.find(m_AssetRoot);
        if (pos == 0) {
            std::string relative = physicalPath.substr(m_AssetRoot.length());
            // 将反斜杠统一为正斜杠
            std::replace(relative.begin(), relative.end(), '\\', '/');
            if (relative.starts_with('/')) relative = relative.substr(1);
            return "Assets/" + relative;
        }
        return physicalPath;
    }

    std::string AssetDatabase::ToPhysicalPath(const std::string& virtualPath) const {
        // 将 "Assets/..." 路径映射回物理路径
        if (virtualPath.starts_with("Assets/")) {
            std::string relative = virtualPath.substr(7);
            std::filesystem::path result = std::filesystem::path(m_AssetRoot) / relative;
            return result.string();
        }
        return virtualPath;
    }

    bool AssetDatabase::SetVirtualPath(const GUID& guid, const std::string& newVirtualPath) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_GuidToMeta.find(guid);
        if (it == m_GuidToMeta.end()) return false;

        // 更新映射
        m_VirtualPathToGuid.erase(it->second.virtualPath);
        it->second.virtualPath = newVirtualPath;
        m_VirtualPathToGuid[newVirtualPath] = guid;

        // 更新 .meta 文件
        return WriteMetaFile(GetMetaPath(it->second.originalPath), it->second);
    }

    bool AssetDatabase::Reimport(const GUID& guid) {
        auto* meta = GetMetaMutable(guid);
        if (!meta) return false;

        std::string metaPath = GetMetaPath(meta->originalPath);
        WriteMetaFile(metaPath, *meta);

        if (m_Callbacks.onAssetImported)
            m_Callbacks.onAssetImported(guid, meta->originalPath);

        return true;
    }

    void AssetDatabase::EnableFileWatching(bool enable) {
        m_FileWatching = enable;
        if (enable) {
            Log::Info("[AssetDB] File watching enabled");
        }
    }

} // namespace Engine