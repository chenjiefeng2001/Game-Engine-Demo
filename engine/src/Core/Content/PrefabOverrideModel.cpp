#include "Engine/Core/Content/PrefabOverrideModel.h"

#include <algorithm>
#include <cmath>

namespace Engine::Content {

namespace {

const PrefabEntity* FindEntity(const PrefabModel& source, uint64_t localId) {
    for (const auto& entity : source.entities)
        if (entity.localId == localId) return &entity;
    return nullptr;
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool SameIdentity(const PrefabOverrideRecord& a,
                  const PrefabOverrideRecord& b) {
    return a.localId == b.localId && a.component == b.component &&
           a.field == b.field;
}

} // namespace

bool ReadPrefabOverrideBaseValue(const PrefabModel& source,
                                 uint64_t localId,
                                 const std::string& component,
                                 const std::string& field,
                                 nlohmann::json* out,
                                 std::string* error) {
    if (!out) return Fail(error, "Prefab override output is null");
    PrefabOverrideFieldSpec spec;
    if (!ResolvePrefabOverrideField(component, field, &spec))
        return Fail(error, "unsupported Prefab override field");
    const PrefabEntity* entity = FindEntity(source, localId);
    if (!entity)
        return Fail(error, "Prefab override localId not found");
    if (component == "Transform") {
        const auto& transform = entity->localTransform;
        if (field == "position.x") *out = transform.position.x;
        else if (field == "position.y") *out = transform.position.y;
        else if (field == "position.z") *out = transform.position.z;
        else if (field == "rotation.x") *out = transform.rotation.x;
        else if (field == "rotation.y") *out = transform.rotation.y;
        else if (field == "rotation.z") *out = transform.rotation.z;
        else if (field == "rotation.w") *out = transform.rotation.w;
        else if (field == "scale.x") *out = transform.scale.x;
        else if (field == "scale.y") *out = transform.scale.y;
        else if (field == "scale.z") *out = transform.scale.z;
        else return Fail(error, "unsupported Transform override field");
        return true;
    }
    const PrefabComponentData* found = nullptr;
    for (const auto& item : entity->components)
        if (item.type == component) { found = &item; break; }
    if (!found)
        return Fail(error, "Prefab override component not found");
    if (component == "Mesh" && field == "meshGuid") {
        if (found->data.contains("meshGuid"))
            *out = found->data["meshGuid"];
        else
            *out = std::string();
        return true;
    }
    if (!found->data.is_object() || !found->data.contains(field))
        return Fail(error, "Prefab override base field not found");
    *out = found->data[field];
    return true;
}

bool PrefabOverrideMatchesBase(const PrefabModel& source,
                               const PrefabOverrideRecord& record,
                               bool* matches,
                               std::string* error) {
    if (!matches) return Fail(error, "Prefab override match output is null");
    nlohmann::json base;
    if (!ReadPrefabOverrideBaseValue(source, record.localId, record.component,
                                     record.field, &base, error))
        return false;
    *matches = base == record.value;
    return true;
}

bool AddOrUpdatePrefabOverride(const PrefabModel& source,
                               std::vector<PrefabOverrideRecord>* overrides,
                               const PrefabOverrideRecord& record,
                               std::string* error) {
    if (!overrides) return Fail(error, "Prefab override list is null");
    if (!ValidatePrefabOverrideRecord(record, error)) return false;
    nlohmann::json base;
    if (!ReadPrefabOverrideBaseValue(source, record.localId, record.component,
                                     record.field, &base, error))
        return false;

    overrides->erase(
        std::remove_if(overrides->begin(), overrides->end(),
                       [&](const PrefabOverrideRecord& item) {
                           return SameIdentity(item, record);
                       }),
        overrides->end());
    if (base == record.value) {
        NormalizePrefabOverrides(overrides);
        return true;
    }
    overrides->push_back(record);
    NormalizePrefabOverrides(overrides);
    return true;
}

bool RemovePrefabOverride(std::vector<PrefabOverrideRecord>* overrides,
                          uint64_t localId,
                          const std::string& component,
                          const std::string& field) {
    if (!overrides) return false;
    const auto before = overrides->size();
    overrides->erase(
        std::remove_if(overrides->begin(), overrides->end(),
                       [&](const PrefabOverrideRecord& item) {
                           return item.localId == localId &&
                                  item.component == component &&
                                  item.field == field;
                       }),
        overrides->end());
    return overrides->size() != before;
}

void NormalizePrefabOverrides(std::vector<PrefabOverrideRecord>* overrides) {
    if (!overrides) return;
    std::sort(overrides->begin(), overrides->end(),
              [](const PrefabOverrideRecord& a,
                 const PrefabOverrideRecord& b) {
                  if (a.localId != b.localId) return a.localId < b.localId;
                  if (a.component != b.component) return a.component < b.component;
                  return a.field < b.field;
              });
}

} // namespace Engine::Content
