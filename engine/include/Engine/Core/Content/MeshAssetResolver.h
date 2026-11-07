#pragma once

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Engine::Content {

// P6-1B B5-G4: Native committed-asset resolver for runtime mesh loading.
//
// Identity = ResourceGUID (Scene-persisted). Path/type come from the *committed
// current revision* manifest, never from directory scans or name guessing.
// Asset bytes are read through ProjectRoot (traversal/link/UNC rejection).
enum class MeshResolveStatus : uint8_t {
    Ok = 0,
    NotCommitted,
    CommittedManifestInvalid,
    InvalidGuid,
    MissingAsset,
    UnsupportedAssetType,
    PathRejected,
    AssetBytesUnreadable,
};

const char* ToString(MeshResolveStatus status);

struct MeshAssetResolveResult {
    bool ok = false;
    MeshResolveStatus status = MeshResolveStatus::NotCommitted;
    ResourceGUID guid = ResourceGUID::Null;
    AssetType type = AssetType::Texture;
    int64_t revision = 0;
    std::string assetPath;    // committed project-relative path
    std::string absolutePath; // resolved OS path
    std::vector<uint8_t> bytes;
    std::string error;
};

class MeshAssetResolver {
public:
    MeshAssetResolver(const ProjectRoot& root, const RevisionStore& revisions);

    // Refreshes the committed manifest when the current revision changes.
    MeshAssetResolveResult Resolve(const ResourceGUID& guid);

    // Safe project-relative reads for Material/Texture side inputs.
    bool ReadProjectFile(const std::string& projectRelative,
                         std::vector<uint8_t>& out) const;
    std::string AbsoluteProjectPath(const std::string& projectRelative) const;

    int64_t CurrentCommittedRevision() const {
        return m_Revisions.IsVersioned() ? m_Revisions.Revision() : -1;
    }

    // Material slot resolver input: (path, typeToken) pairs from the same
    // committed snapshot used for the mesh. Empty until a successful snapshot.
    const std::vector<std::pair<std::string, std::string>>& CommittedEntries()
        const {
        return m_Entries;
    }

private:
    bool EnsureSnapshot(std::string* error, MeshResolveStatus* status);
    bool ReadAbsolute(const std::string& absolute,
                      std::vector<uint8_t>& out) const;

    const ProjectRoot& m_Root;
    const RevisionStore& m_Revisions;
    bool m_HasSnapshot = false;
    int64_t m_SnapshotRevision = 0;
    std::string m_SnapshotManifestPath;
    ContentRegistry m_Committed;
    std::vector<std::pair<std::string, std::string>> m_Entries;
};

} // namespace Engine::Content
