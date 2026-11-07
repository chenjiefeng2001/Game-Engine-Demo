#include "Engine/Core/Content/MeshAssetResolver.h"

#include "AssetImport/ImportLimits.h"

#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/Content/Utf8Path.h"

#include <filesystem>
#include <fstream>

namespace Engine::Content {

const char* ToString(MeshResolveStatus status) {
    switch (status) {
    case MeshResolveStatus::Ok: return "Ok";
    case MeshResolveStatus::NotCommitted: return "NotCommitted";
    case MeshResolveStatus::CommittedManifestInvalid:
        return "CommittedManifestInvalid";
    case MeshResolveStatus::InvalidGuid: return "InvalidGuid";
    case MeshResolveStatus::MissingAsset: return "MissingAsset";
    case MeshResolveStatus::UnsupportedAssetType: return "UnsupportedAssetType";
    case MeshResolveStatus::PathRejected: return "PathRejected";
    case MeshResolveStatus::AssetBytesUnreadable: return "AssetBytesUnreadable";
    }
    return "Unknown";
}

MeshAssetResolver::MeshAssetResolver(const ProjectRoot& root,
                                     const RevisionStore& revisions)
    : m_Root(root)
    , m_Revisions(revisions) {}

bool MeshAssetResolver::EnsureSnapshot(std::string* error,
                                       MeshResolveStatus* status) {
    if (!m_Revisions.IsVersioned()) {
        *status = MeshResolveStatus::NotCommitted;
        *error = "NOT_COMMITTED: project has no committed current revision";
        return false;
    }
    const std::string manifestPath = m_Revisions.CommittedManifestPath();
    if (manifestPath.empty()) {
        *status = MeshResolveStatus::NotCommitted;
        *error = "NOT_COMMITTED: committed manifest path is empty";
        return false;
    }
    if (m_HasSnapshot && m_SnapshotRevision == m_Revisions.Revision() &&
        m_SnapshotManifestPath == manifestPath) {
        return true;
    }
    ContentRegistry next;
    ManifestLoadOptions options;
    options.root = &m_Root;
    options.caseMode = m_Root.CaseMode();
    if (!next.LoadManifest(manifestPath, options)) {
        *status = MeshResolveStatus::CommittedManifestInvalid;
        *error = "COMMITTED_MANIFEST_INVALID: " + manifestPath;
        return false;
    }
    m_Committed = std::move(next);
    m_Entries.clear();
    for (const AssetEntry& entry : m_Committed.GetAllEntries())
        m_Entries.emplace_back(entry.path, ToString(entry.type));
    m_SnapshotRevision = m_Revisions.Revision();
    m_SnapshotManifestPath = manifestPath;
    m_HasSnapshot = true;
    return true;
}

bool MeshAssetResolver::ReadAbsolute(const std::string& absolute,
                                     std::vector<uint8_t>& out) const {
    std::error_code ec;
    const std::filesystem::path p = PathFromUtf8(absolute);
    const uint64_t size = std::filesystem::file_size(p, ec);
    if (ec || size == 0 || size > AssetImport::kMaxSourceFileBytes) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()),
           static_cast<std::streamsize>(size));
    return f.good() || (f.eof() && static_cast<uint64_t>(f.gcount()) == size);
}

std::string MeshAssetResolver::AbsoluteProjectPath(
    const std::string& projectRelative) const {
    const PathResolution resolved = m_Root.Resolve(projectRelative);
    return resolved.ok ? resolved.path : std::string();
}

bool MeshAssetResolver::ReadProjectFile(const std::string& projectRelative,
                                        std::vector<uint8_t>& out) const {
    const std::string absolute = AbsoluteProjectPath(projectRelative);
    if (absolute.empty()) return false;
    return ReadAbsolute(absolute, out);
}

MeshAssetResolveResult MeshAssetResolver::Resolve(const ResourceGUID& guid) {
    MeshAssetResolveResult result;
    result.guid = guid;
    if (guid.IsNull()) {
        result.status = MeshResolveStatus::InvalidGuid;
        result.error = "INVALID_GUID: null asset GUID";
        return result;
    }
    std::string error;
    if (!EnsureSnapshot(&error, &result.status)) {
        result.error = error;
        return result;
    }

    if (!m_Committed.ContainsGuid(guid)) {
        result.status = MeshResolveStatus::MissingAsset;
        result.error = "MISSING_ASSET: GUID not in committed manifest";
        return result;
    }
    result.type = m_Committed.TypeOf(guid);
    if (result.type != AssetType::Mesh) {
        result.status = MeshResolveStatus::UnsupportedAssetType;
        result.error = "UNSUPPORTED_ASSET_TYPE: committed type is " +
                       std::string(ToString(result.type));
        return result;
    }
    result.assetPath = m_Committed.ResolvePath(guid);
    if (result.assetPath.empty()) {
        result.status = MeshResolveStatus::MissingAsset;
        result.error = "MISSING_ASSET: committed path is empty";
        return result;
    }
    const PathResolution resolved = m_Root.Resolve(result.assetPath);
    if (!resolved.ok) {
        result.status = MeshResolveStatus::PathRejected;
        result.error = "PATH_REJECTED: committed path outside project root";
        return result;
    }
    result.absolutePath = resolved.path;
    if (!ReadAbsolute(result.absolutePath, result.bytes)) {
        result.status = MeshResolveStatus::AssetBytesUnreadable;
        result.error = "ASSET_BYTES_UNREADABLE: " + result.assetPath;
        return result;
    }
    result.revision = m_SnapshotRevision;
    result.ok = true;
    result.status = MeshResolveStatus::Ok;
    return result;
}

} // namespace Engine::Content
