#pragma once

/**
 * @file PrefabSerialization.h
 * @brief Strict canonical Prefab JSON Writer/Reader (G4-2).
 */

#include "Engine/Core/Content/PrefabAsset.h"

#include <string>
#include <string_view>

namespace Engine::Content {

    struct PrefabWriteResult {
        bool ok = false;
        std::string bytes;
        std::string digest;
        std::string error;
    };

    struct PrefabReadResult {
        bool ok = false;
        PrefabModel model;
        std::string error;
    };

    /// Canonical bytes are deterministic and covered completely by the Digest.
    PrefabWriteResult WritePrefab(const PrefabModel& model);

    /// Strict Reader. Duplicate JSON object keys, unknown fields, malformed
    /// uint64 IDs, invalid hierarchy, and invalid component payloads reject.
    /// The optional expected GUID is used by the G4-3 asset resolver.
    PrefabReadResult ReadPrefab(std::string_view bytes,
                                const ResourceGUID* expectedGuid = nullptr);

    /// Frozen G4-A Digest: RevisionStore::HashBytes(complete canonical bytes).
    std::string ComputePrefabDigest(std::string_view bytes);

    /// Compare the persisted Prefab fields only. Runtime state is not modeled.
    bool PrefabModelsEqual(const PrefabModel& a, const PrefabModel& b);

} // namespace Engine::Content