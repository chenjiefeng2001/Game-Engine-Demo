#include "Engine/Core/Content/PrefabApplyCandidate.h"

#include "Engine/Core/Content/PrefabOverrideModel.h"
#include "Engine/Core/Content/PrefabSerialization.h"

#include <unordered_set>

namespace Engine::Content {

namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

PrefabEntity* FindEntity(PrefabModel* model, uint64_t localId) {
    if (!model) return nullptr;
    for (auto& entity : model->entities)
        if (entity.localId == localId) return &entity;
    return nullptr;
}

PrefabComponentData* FindComponent(PrefabEntity* entity,
                                   const std::string& type) {
    if (!entity) return nullptr;
    for (auto& component : entity->components)
        if (component.type == type) return &component;
    return nullptr;
}

bool SetTransformField(PrefabLocalTransform* transform, const std::string& field,
                       const nlohmann::json& value) {
    if (!transform || !value.is_number()) return false;
    const float scalar = value.get<float>();
    if (field == "position.x") transform->position.x = scalar;
    else if (field == "position.y") transform->position.y = scalar;
    else if (field == "position.z") transform->position.z = scalar;
    else if (field == "rotation.x") transform->rotation.x = scalar;
    else if (field == "rotation.y") transform->rotation.y = scalar;
    else if (field == "rotation.z") transform->rotation.z = scalar;
    else if (field == "rotation.w") transform->rotation.w = scalar;
    else if (field == "scale.x") transform->scale.x = scalar;
    else if (field == "scale.y") transform->scale.y = scalar;
    else if (field == "scale.z") transform->scale.z = scalar;
    else return false;
    return true;
}

} // namespace

PrefabApplyCandidateResult BuildPrefabApplyCandidate(
    const PrefabModel& source,
    const std::vector<PrefabOverrideRecord>& overrides,
    const std::unordered_map<uint64_t, uint64_t>& localToScene,
    const ResourceGUID& expectedGuid,
    int64_t sourceRevision) {
    PrefabApplyCandidateResult result;
    auto fail = [&](const std::string& message) {
        result.ok = false;
        result.model = {};
        result.bytes.clear();
        result.digest.clear();
        result.error = message;
        return result;
    };
    if (source.prefabGuid.IsNull())
        return fail("Apply source Prefab GUID is zero");
    if (source.prefabGuid != expectedGuid)
        return fail("Apply source GUID does not match the instance");
    if (sourceRevision < 0)
        return fail("Apply source revision is negative");
    const auto validation = ValidatePrefabModel(source);
    if (!validation.ok)
        return fail("Apply source is invalid: " + validation.error);

    if (localToScene.size() != source.entities.size())
        return fail("Apply Local ID mapping count mismatch");
    std::unordered_set<uint64_t> sourceIds;
    std::unordered_set<uint64_t> sceneIds;
    for (const auto& entity : source.entities) {
        if (localToScene.find(entity.localId) == localToScene.end())
            return fail("Apply source Local ID is unmapped");
        if (!sourceIds.insert(entity.localId).second)
            return fail("Apply source contains duplicate Local ID");
    }
    bool rootMapped = false;
    for (const auto& [localId, sceneId] : localToScene) {
        if (sourceIds.find(localId) == sourceIds.end())
            return fail("Apply mapping contains an unknown Local ID");
        if (sceneId == 0 || !sceneIds.insert(sceneId).second)
            return fail("Apply mapping contains an invalid Scene ID");
        if (localId == source.rootLocalId) rootMapped = true;
    }
    if (!rootMapped)
        return fail("Apply mapping does not contain the source root");

    std::unordered_set<std::string> identities;
    for (const auto& record : overrides) {
        std::string error;
        if (!ValidatePrefabOverrideRecord(record, &error))
            return fail("Apply override invalid: " + error);
        if (localToScene.find(record.localId) == localToScene.end())
            return fail("Apply override Local ID is not mapped");
        nlohmann::json base;
        if (!ReadPrefabOverrideBaseValue(source, record.localId,
                                         record.component, record.field, &base,
                                         &error))
            return fail("Apply override base value unavailable: " + error);
        if (base == record.value)
            return fail("Apply override equals its source base value");
        const std::string key = std::to_string(record.localId) + "|" +
                                record.component + "|" + record.field;
        if (!identities.insert(key).second)
            return fail("Apply overrides contain duplicate identity");
    }

    PrefabModel candidate = source;
    for (const auto& record : overrides) {
        PrefabEntity* entity = FindEntity(&candidate, record.localId);
        if (!entity)
            return fail("Apply override entity not found");
        if (record.component == "Transform") {
            if (!SetTransformField(&entity->localTransform, record.field,
                                   record.value))
                return fail("Apply Transform override path is invalid");
            continue;
        }
        PrefabComponentData* component =
            FindComponent(entity, record.component);
        if (!component)
            return fail("Apply override component not found");
        component->data[record.field] = record.value;
    }

    const auto candidateValidation = ValidatePrefabModel(candidate);
    if (!candidateValidation.ok)
        return fail("Apply candidate is invalid: " + candidateValidation.error);
    const auto written = WritePrefab(candidate);
    if (!written.ok)
        return fail("Apply candidate Writer failed: " + written.error);
    result.ok = true;
    result.model = std::move(candidate);
    result.bytes = written.bytes;
    result.digest = written.digest;
    return result;
}

} // namespace Engine::Content