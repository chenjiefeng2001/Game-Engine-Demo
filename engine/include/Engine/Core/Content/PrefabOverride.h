#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace Engine::Content {

    enum class PrefabSyncState : uint8_t {
        Clean = 0,
        Overrides,
        ApplyPending,
        RecoveryRequired,
        Conflict
    };

    enum class PrefabOverrideValueType : uint8_t {
        Bool = 0,
        Int,
        Float,
        String
    };

    struct PrefabOverrideRecord {
        uint64_t localId = 0;
        std::string component;
        std::string field;
        nlohmann::json value;
    };

    struct PrefabOverrideFieldSpec {
        PrefabOverrideValueType type = PrefabOverrideValueType::Float;
    };

    /// Resolve a frozen G6-A component/field path. Unsupported fields return
    /// false; this is the fail-closed allowlist boundary.
    bool ResolvePrefabOverrideField(const std::string& component,
                                    const std::string& field,
                                    PrefabOverrideFieldSpec* out);

    bool ValidatePrefabOverrideValue(const std::string& component,
                                     const std::string& field,
                                     const nlohmann::json& value,
                                     std::string* error);

    bool ValidatePrefabOverrideRecord(const PrefabOverrideRecord& record,
                                      std::string* error);

} // namespace Engine::Content