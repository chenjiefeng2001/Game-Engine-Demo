#pragma once

#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine::Content {

    enum class PrefabApplyState : uint8_t {
        Prepared = 0,
        PrefabPublished,
        ScenePublished,
        RecoveryRequired
    };

    const char* ToString(PrefabApplyState state);
    bool ParsePrefabApplyState(const std::string& text, PrefabApplyState* out);

    struct PrefabApplyRecord {
        std::string transactionId;
        int32_t recordVersion = 1;
        PrefabApplyState state = PrefabApplyState::Prepared;
        ResourceGUID prefabGuid;
        int64_t oldSourceRevision = 0;
        std::string oldSourceDigest;
        std::string oldSourcePath;
        std::string newDigest;
        std::string newPath;
        int64_t newSourceRevision = -1;
        uint64_t instanceRootEntityId = 0;
        nlohmann::json oldInstanceMetadata = nlohmann::json::object();
        nlohmann::json newInstanceMetadata = nlohmann::json::object();
        int64_t oldRevisionSnapshot = 0;
        int64_t phase1Revision = -1;
        int64_t phase2Revision = -1;
        std::string integrityHash;
        bool integrityValid = false;
    };

    bool ValidatePrefabApplyRecord(const PrefabApplyRecord& record,
                                   std::string* error);

    /// Canonical JSON of record without integrityHash is hashed by
    /// RevisionStore::HashBytes (full 16-hex FNV-1a).
    bool ComputePrefabApplyRecordIntegrity(
        const PrefabApplyRecord& record, std::string* hash, std::string* error);

    /// Serialize canonical record JSON including a newly computed integrityHash.
    bool SerializePrefabApplyRecord(const PrefabApplyRecord& record,
                                    std::string* bytes, std::string* error);

    /// Strict parser: exact fields, version 1, state/identity/path/digest and
    /// integrity validation. Unknown fields/versions/corruption reject.
    bool ParsePrefabApplyRecord(std::string_view bytes,
                                PrefabApplyRecord* out, std::string* error);

    std::string PrefabApplyRecordPath(const ProjectRoot& root,
                                      const ResourceGUID& prefabGuid,
                                      uint64_t instanceRootEntityId);

    bool WritePrefabApplyRecord(const ProjectRoot& root,
                                const PrefabApplyRecord& record,
                                std::string* error);
    bool ReadPrefabApplyRecord(const ProjectRoot& root,
                               const ResourceGUID& prefabGuid,
                               uint64_t instanceRootEntityId,
                               PrefabApplyRecord* out, std::string* error);
    bool RemovePrefabApplyRecord(const ProjectRoot& root,
                                 const ResourceGUID& prefabGuid,
                                 uint64_t instanceRootEntityId,
                                 std::string* error);

} // namespace Engine::Content