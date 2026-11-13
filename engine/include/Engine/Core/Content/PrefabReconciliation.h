#pragma once

#include "Engine/Core/Content/PrefabReconciliationSources.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Engine::Content {

    enum class PrefabReconciliationDecision : uint8_t {
        TakeLatest = 0,
        KeepLocal,
        Conflict
    };

    enum class PrefabStructuralChangeKind : uint8_t {
        AddEntity = 0,
        DeleteEntity,
        SetName,
        SetParent,
        AddComponent,
        RemoveComponent
    };

    enum class PrefabConflictType : uint8_t {
        Field = 0,
        EntityMissing,
        EntityDelete,
        EntityName,
        EntityParent,
        ComponentSet,
        UnsupportedLocalEdit,
        UnsupportedComponentChange
    };

    struct PrefabReconciliationField {
        PrefabReconciliationDecision decision =
            PrefabReconciliationDecision::Conflict;
        uint64_t localId = 0;
        std::string component;
        std::string field;
        nlohmann::json base = nullptr;
        nlohmann::json latest = nullptr;
        nlohmann::json local = nullptr;
        std::string conflictId;
    };

    struct PrefabReconciliationStructural {
        PrefabStructuralChangeKind kind =
            PrefabStructuralChangeKind::AddEntity;
        uint64_t localId = 0;
        uint64_t parentLocalId = 0;
        std::string name;
        std::string component;
        nlohmann::json data = nullptr;
        std::vector<PrefabComponentData> components;
        PrefabLocalTransform localTransform;
        bool automatic = false;
        std::string conflictId;
    };

    struct PrefabReconciliationConflict {
        std::string id;
        PrefabConflictType type = PrefabConflictType::Field;
        uint64_t localId = 0;
        std::string component;
        std::string field;
        nlohmann::json base = nullptr;
        nlohmann::json latest = nullptr;
        nlohmann::json local = nullptr;
        std::string detail;
    };

    struct PrefabReconciliationOverride {
        PrefabReconciliationDecision decision =
            PrefabReconciliationDecision::Conflict;
        uint64_t localId = 0;
        std::string component;
        std::string field;
        nlohmann::json base = nullptr;
        nlohmann::json latestBase = nullptr;
        nlohmann::json value = nullptr;
        std::string conflictId;
    };

    struct PrefabReconciliationState {
        std::string latestSourceDigest;
        std::string latestSourcePath;
        std::vector<PrefabReconciliationConflict> conflicts;
    };

    struct PrefabReconciliationPlan {
        bool outOfDate = false;
        bool hasConflicts = false;
        std::vector<PrefabReconciliationField> fields;
        std::vector<PrefabReconciliationStructural> structural;
        std::vector<PrefabReconciliationOverride> overrides;
        std::vector<PrefabReconciliationConflict> conflicts;
        std::vector<std::string> validationErrors;
    };

    PrefabReconciliationPlan BuildPrefabReconciliation(
        const PrefabSourceSnapshot& base,
        const PrefabSourceSnapshot& latest,
        const PrefabLocalInstanceSnapshot& local);

    const char* PrefabConflictTypeText(PrefabConflictType type);
    bool ParsePrefabConflictType(const std::string& text, PrefabConflictType* out);
    bool ValidatePrefabReconciliationState(
        const PrefabReconciliationState& state, std::string* error);
    nlohmann::json PrefabReconciliationStateToJson(
        const PrefabReconciliationState& state);
    bool PrefabReconciliationStateFromJson(
        const nlohmann::json& json, PrefabReconciliationState* out,
        std::string* error);

} // namespace Engine::Content