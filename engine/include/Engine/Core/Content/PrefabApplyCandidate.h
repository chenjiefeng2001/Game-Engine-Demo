#pragma once

#include "Engine/Core/Content/PrefabAsset.h"
#include "Engine/Core/Content/PrefabOverride.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::Content {

    struct PrefabApplyCandidateResult {
        bool ok = false;
        PrefabModel model;
        std::string bytes;
        std::string digest;
        std::string error;
    };

    /// Build and validate an Apply candidate from an already parsed exact source.
    /// The source identity (digest/path) is verified by the caller before this
    /// call; this function enforces GUID, revision, local mapping, override
    /// identity/field/value/base mismatch and canonical Writer rules.
    PrefabApplyCandidateResult BuildPrefabApplyCandidate(
        const PrefabModel& source,
        const std::vector<PrefabOverrideRecord>& overrides,
        const std::unordered_map<uint64_t, uint64_t>& localToScene,
        const ResourceGUID& expectedGuid,
        int64_t sourceRevision);

} // namespace Engine::Content