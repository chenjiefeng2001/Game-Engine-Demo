#pragma once

#include "Engine/Core/Content/PrefabAsset.h"
#include "Engine/Core/Content/PrefabOverride.h"

#include <vector>

namespace Engine::Content {

    bool ReadPrefabOverrideBaseValue(const PrefabModel& source,
                                     uint64_t localId,
                                     const std::string& component,
                                     const std::string& field,
                                     nlohmann::json* out,
                                     std::string* error);

    bool PrefabOverrideMatchesBase(const PrefabModel& source,
                                   const PrefabOverrideRecord& record,
                                   bool* matches,
                                   std::string* error);

    /// Validate and normalize one record against the source. If the value equals
    /// the base value, the matching override is removed.
    bool AddOrUpdatePrefabOverride(const PrefabModel& source,
                                   std::vector<PrefabOverrideRecord>* overrides,
                                   const PrefabOverrideRecord& record,
                                   std::string* error);

    bool RemovePrefabOverride(std::vector<PrefabOverrideRecord>* overrides,
                              uint64_t localId,
                              const std::string& component,
                              const std::string& field);

    void NormalizePrefabOverrides(std::vector<PrefabOverrideRecord>* overrides);

} // namespace Engine::Content