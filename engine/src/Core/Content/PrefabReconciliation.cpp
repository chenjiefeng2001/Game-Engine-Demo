#include "Engine/Core/Content/PrefabReconciliation.h"

#include "Engine/Core/Content/PrefabOverrideModel.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Engine::Content {

namespace {

const PrefabEntity* FindEntity(const PrefabModel& model, uint64_t localId) {
    for (const auto& entity : model.entities)
        if (entity.localId == localId) return &entity;
    return nullptr;
}

const PrefabLocalEntitySnapshot* FindLocal(
    const PrefabLocalInstanceSnapshot& local, uint64_t localId) {
    for (const auto& entity : local.entities)
        if (entity.localId == localId) return &entity;
    return nullptr;
}

const PrefabComponentData* FindComponent(const PrefabEntity* entity,
                                         const std::string& type) {
    if (!entity) return nullptr;
    for (const auto& component : entity->components)
        if (component.type == type) return &component;
    return nullptr;
}

const PrefabLocalComponentSnapshot* FindLocalComponent(
    const PrefabLocalEntitySnapshot* entity, const std::string& type) {
    if (!entity) return nullptr;
    for (const auto& component : entity->components)
        if (component.type == type) return &component;
    return nullptr;
}

bool IsSupportedComponentType(const std::string& type) {
    return type == "Mesh" || type == "Camera" || type == "Collider" ||
           type == "RigidBody";
}

std::vector<std::string> AllowedFields(const std::string& component) {
    if (component == "Transform") {
        return {"position.x", "position.y", "position.z",
                "rotation.x", "rotation.y", "rotation.z", "rotation.w",
                "scale.x", "scale.y", "scale.z"};
    }
    if (component == "Mesh") return {"meshGuid"};
    if (component == "Camera") return {"enabled", "zoom", "viewport", "bounds"};
    if (component == "Collider") {
        return {"enabled", "shape", "radius", "halfX", "halfY", "isSensor",
                "category", "mask"};
    }
    if (component == "RigidBody") {
        return {"motionType", "linearDamping", "angularDamping"};
    }
    return {};
}

nlohmann::json ComponentField(const PrefabComponentData* component,
                              const std::string& field) {
    if (!component) return nullptr;
    if (component->data.contains(field)) return component->data[field];
    if (component->type == "Mesh" && field == "meshGuid") return std::string();
    return nullptr;
}

nlohmann::json TransformField(const PrefabLocalTransform& transform,
                              const std::string& field) {
    if (field == "position.x") return transform.position.x;
    if (field == "position.y") return transform.position.y;
    if (field == "position.z") return transform.position.z;
    if (field == "rotation.x") return transform.rotation.x;
    if (field == "rotation.y") return transform.rotation.y;
    if (field == "rotation.z") return transform.rotation.z;
    if (field == "rotation.w") return transform.rotation.w;
    if (field == "scale.x") return transform.scale.x;
    if (field == "scale.y") return transform.scale.y;
    if (field == "scale.z") return transform.scale.z;
    return nullptr;
}

nlohmann::json LocalComponentField(const PrefabLocalComponentSnapshot* component,
                                   const std::string& field) {
    if (!component) return nullptr;
    if (component->data.contains(field)) return component->data[field];
    if (component->type == "Mesh" && field == "meshGuid") return std::string();
    return nullptr;
}

void AddConflict(PrefabReconciliationPlan* plan, const std::string& id,
                 PrefabConflictType type, uint64_t localId,
                 const std::string& component, const std::string& field,
                 const nlohmann::json& base, const nlohmann::json& latest,
                 const nlohmann::json& local, const std::string& detail) {
    plan->conflicts.push_back(
        PrefabReconciliationConflict{id, type, localId, component, field,
                                     base, latest, local, detail});
    plan->hasConflicts = true;
}

void AddField(PrefabReconciliationPlan* plan,
              PrefabReconciliationDecision decision,
              uint64_t localId, const std::string& component,
              const std::string& field, const nlohmann::json& base,
              const nlohmann::json& latest, const nlohmann::json& local) {
    PrefabReconciliationField change;
    change.decision = decision;
    change.localId = localId;
    change.component = component;
    change.field = field;
    change.base = base;
    change.latest = latest;
    change.local = local;
    plan->fields.push_back(std::move(change));
}

void AddStructural(PrefabReconciliationPlan* plan,
                   PrefabStructuralChangeKind kind, uint64_t localId,
                   uint64_t parentLocalId, const std::string& name,
                   const std::string& component,
                   const nlohmann::json& data,
                   const std::vector<PrefabComponentData>& components,
                   const PrefabLocalTransform& transform) {
    PrefabReconciliationStructural change;
    change.kind = kind;
    change.localId = localId;
    change.parentLocalId = parentLocalId;
    change.name = name;
    change.component = component;
    change.data = data;
    change.components = components;
    change.localTransform = transform;
    change.automatic = true;
    plan->structural.push_back(std::move(change));
}

void CompareFields(PrefabReconciliationPlan* plan, uint64_t localId,
                   const PrefabEntity* base, const PrefabEntity* latest,
                   const PrefabLocalEntitySnapshot* local) {
    for (const std::string& component : {"Transform", "Mesh", "Camera",
                                         "Collider", "RigidBody"}) {
        if (component == "Transform") {
            const auto fields = AllowedFields(component);
            for (const auto& field : fields) {
                const auto baseValue = TransformField(base->localTransform, field);
                const auto latestValue = TransformField(latest->localTransform, field);
                const auto localValue = TransformField(local->localTransform, field);
                if (baseValue == latestValue && baseValue == localValue) continue;
                const bool baseChanged = baseValue != latestValue;
                const bool localChanged = baseValue != localValue;
                if (!baseChanged) {
                    AddField(plan, PrefabReconciliationDecision::KeepLocal,
                             localId, component, field, baseValue, latestValue,
                             localValue);
                } else if (!localChanged || localValue == latestValue) {
                    AddField(plan, PrefabReconciliationDecision::TakeLatest,
                             localId, component, field, baseValue, latestValue,
                             localValue);
                } else {
                    const std::string id = "field:" + std::to_string(localId) +
                                           ":" + component + ":" + field;
                    AddField(plan, PrefabReconciliationDecision::Conflict,
                             localId, component, field, baseValue, latestValue,
                             localValue);
                    plan->fields.back().conflictId = id;
                    AddConflict(plan, id, PrefabConflictType::Field, localId,
                                component, field, baseValue, latestValue,
                                localValue, "both source and local changed");
                }
            }
            continue;
        }
        const PrefabComponentData* baseComponent = FindComponent(base, component);
        const PrefabComponentData* latestComponent = FindComponent(latest, component);
        const PrefabLocalComponentSnapshot* localComponent =
            FindLocalComponent(local, component);
        if (!baseComponent && !latestComponent && !localComponent) continue;
        if (!baseComponent || !latestComponent || !localComponent) continue;
        const auto fields = AllowedFields(component);
        for (const auto& field : fields) {
            const auto baseValue = ComponentField(baseComponent, field);
            const auto latestValue = ComponentField(latestComponent, field);
            const auto localValue = LocalComponentField(localComponent, field);
            if (baseValue == latestValue && baseValue == localValue) continue;
            const bool baseChanged = baseValue != latestValue;
            const bool localChanged = baseValue != localValue;
            if (!baseChanged) {
                AddField(plan, PrefabReconciliationDecision::KeepLocal, localId,
                         component, field, baseValue, latestValue, localValue);
            } else if (!localChanged || localValue == latestValue) {
                AddField(plan, PrefabReconciliationDecision::TakeLatest, localId,
                         component, field, baseValue, latestValue, localValue);
            } else {
                const std::string id = "field:" + std::to_string(localId) +
                                       ":" + component + ":" + field;
                AddField(plan, PrefabReconciliationDecision::Conflict, localId,
                         component, field, baseValue, latestValue, localValue);
                plan->fields.back().conflictId = id;
                AddConflict(plan, id, PrefabConflictType::Field, localId,
                            component, field, baseValue, latestValue,
                            localValue, "both source and local changed");
            }
        }
        std::set<std::string> unknownFields;
        for (const auto& item : baseComponent->data.items())
            if (std::find(fields.begin(), fields.end(), item.key()) == fields.end())
                unknownFields.insert(item.key());
        for (const auto& item : latestComponent->data.items())
            if (std::find(fields.begin(), fields.end(), item.key()) == fields.end())
                unknownFields.insert(item.key());
        for (const auto& item : localComponent->data.items())
            if (std::find(fields.begin(), fields.end(), item.key()) == fields.end())
                unknownFields.insert(item.key());
        auto unknownValue = [](const nlohmann::json& data, const std::string& field) {
            if (data.is_object() && data.contains(field)) return data[field];
            return nlohmann::json(nullptr);
        };
        for (const auto& field : unknownFields) {
            const auto baseValue = unknownValue(baseComponent->data, field);
            const auto latestValue = unknownValue(latestComponent->data, field);
            const auto localValue = unknownValue(localComponent->data, field);
            if (baseValue == latestValue && baseValue == localValue) continue;
            const bool localChanged = baseValue != localValue;
            const std::string id = "unsupported:" + std::to_string(localId) +
                                   ":" + component + ":" + field;
            AddConflict(plan, id,
                        localChanged
                            ? PrefabConflictType::UnsupportedLocalEdit
                            : PrefabConflictType::UnsupportedComponentChange,
                        localId, component, field, baseValue, latestValue,
                        localValue,
                        localChanged ? "local component has unsupported fields"
                                     : "source changed unsupported component field");
        }
    }
}

void CompareComponents(PrefabReconciliationPlan* plan, uint64_t localId,
                       const PrefabEntity* base, const PrefabEntity* latest,
                       const PrefabLocalEntitySnapshot* local) {
    std::set<std::string> types;
    if (base)
        for (const auto& component : base->components) types.insert(component.type);
    if (latest)
        for (const auto& component : latest->components) types.insert(component.type);
    if (local)
        for (const auto& component : local->components) types.insert(component.type);

    for (const auto& type : types) {
        const PrefabComponentData* b = FindComponent(base, type);
        const PrefabComponentData* l = FindComponent(latest, type);
        const PrefabLocalComponentSnapshot* loc = FindLocalComponent(local, type);
        if (!IsSupportedComponentType(type)) {
            const bool changed =
                (b ? b->data : nlohmann::json{}) !=
                (l ? l->data : nlohmann::json{}) ||
                (b ? b->data : nlohmann::json{}) !=
                (loc ? loc->data : nlohmann::json{});
            if (changed) {
                const std::string id = "component:" + std::to_string(localId) +
                                       ":" + type;
                AddConflict(plan, id, PrefabConflictType::UnsupportedComponentChange,
                            localId, type, "", b ? b->data : nullptr,
                            l ? l->data : nullptr, loc ? loc->data : nullptr,
                            "unsupported component changed");
            }
            continue;
        }
        if (b && !l) {
            if (loc && loc->data != b->data) {
                const std::string id = "component:" + std::to_string(localId) +
                                       ":" + type;
                AddConflict(plan, id, PrefabConflictType::ComponentSet, localId,
                            type, "", b->data, nullptr, loc->data,
                            "source removed component with local changes");
            } else {
                AddStructural(plan, PrefabStructuralChangeKind::RemoveComponent,
                              localId, 0, "", type, nullptr, {}, {});
            }
            continue;
        }
        if (!b && l) {
            if (loc && loc->data != l->data) {
                const std::string id = "component:" + std::to_string(localId) +
                                       ":" + type;
                AddConflict(plan, id, PrefabConflictType::ComponentSet, localId,
                            type, "", nullptr, l->data, loc->data,
                            "source added component with local changes");
            } else if (!loc) {
                AddStructural(plan, PrefabStructuralChangeKind::AddComponent,
                              localId, 0, "", type, l->data, {}, {});
            }
            continue;
        }
        if (b && l && !loc) {
            const std::string id = "component:" + std::to_string(localId) +
                                   ":" + type;
            AddConflict(plan, id, PrefabConflictType::ComponentSet, localId, type,
                        "", b->data, l->data, nullptr,
                        "local component was removed");
            continue;
        }
        if (!b && !l && loc) {
            const std::string id = "component:" + std::to_string(localId) +
                                   ":" + type;
            AddConflict(plan, id, PrefabConflictType::UnsupportedLocalEdit,
                        localId, type, "", nullptr, nullptr, loc->data,
                        "local-only component");
        }
    }
    CompareFields(plan, localId, base, latest, local);
}

void RebaseOverrides(PrefabReconciliationPlan* plan,
                     const PrefabSourceSnapshot& base,
                     const PrefabSourceSnapshot& latest,
                     const PrefabLocalInstanceSnapshot& local) {
    for (const auto& record : local.overrides) {
        PrefabOverrideFieldSpec spec;
        const bool supported =
            ResolvePrefabOverrideField(record.component, record.field, &spec);
        const PrefabEntity* baseEntity = FindEntity(base.model, record.localId);
        const PrefabEntity* latestEntity = FindEntity(latest.model, record.localId);
        const PrefabLocalEntitySnapshot* localEntity =
            FindLocal(local, record.localId);
        if (!supported || !baseEntity || !latestEntity || !localEntity) {
            const std::string id = "unsupported:" +
                                   std::to_string(record.localId) + ":" +
                                   record.component + ":" + record.field;
            AddConflict(plan, id, PrefabConflictType::UnsupportedLocalEdit,
                        record.localId, record.component, record.field,
                        record.value, record.value, record.value,
                        "override field or source entity is unsupported");
            PrefabReconciliationOverride override;
            override.decision = PrefabReconciliationDecision::Conflict;
            override.localId = record.localId;
            override.component = record.component;
            override.field = record.field;
            override.base = record.value;
            override.latestBase = record.value;
            override.value = record.value;
            override.conflictId = id;
            plan->overrides.push_back(std::move(override));
            continue;
        }

        nlohmann::json baseValue = nullptr;
        nlohmann::json latestValue = nullptr;
        if (record.component == "Transform") {
            baseValue = TransformField(baseEntity->localTransform, record.field);
            latestValue = TransformField(latestEntity->localTransform, record.field);
        } else {
            const PrefabComponentData* baseComponent =
                FindComponent(baseEntity, record.component);
            const PrefabComponentData* latestComponent =
                FindComponent(latestEntity, record.component);
            if (!baseComponent || !latestComponent) {
                const std::string id = "override:" +
                                       std::to_string(record.localId) + ":" +
                                       record.component + ":" + record.field;
                AddConflict(plan, id, PrefabConflictType::ComponentSet,
                            record.localId, record.component, record.field,
                            record.value, record.value, record.value,
                            "override component missing from source");
                PrefabReconciliationOverride override;
                override.decision = PrefabReconciliationDecision::Conflict;
                override.localId = record.localId;
                override.component = record.component;
                override.field = record.field;
                override.base = record.value;
                override.latestBase = record.value;
                override.value = record.value;
                override.conflictId = id;
                plan->overrides.push_back(std::move(override));
                continue;
            }
            baseValue = ComponentField(baseComponent, record.field);
            latestValue = ComponentField(latestComponent, record.field);
        }

        PrefabReconciliationOverride override;
        override.localId = record.localId;
        override.component = record.component;
        override.field = record.field;
        override.base = baseValue;
        override.latestBase = latestValue;
        override.value = record.value;
        if (baseValue == latestValue) {
            override.decision = PrefabReconciliationDecision::KeepLocal;
        } else if (record.value == latestValue || record.value == baseValue) {
            override.decision = PrefabReconciliationDecision::TakeLatest;
        } else {
            override.decision = PrefabReconciliationDecision::Conflict;
            override.conflictId = "field:" + std::to_string(record.localId) +
                                  ":" + record.component + ":" + record.field;
            if (plan->conflicts.end() ==
                std::find_if(plan->conflicts.begin(), plan->conflicts.end(),
                             [&](const PrefabReconciliationConflict& conflict) {
                                 return conflict.id == override.conflictId;
                             })) {
                AddConflict(plan, override.conflictId, PrefabConflictType::Field,
                            record.localId, record.component, record.field,
                            baseValue, latestValue, record.value,
                            "override conflicts with source change");
            }
        }
        plan->overrides.push_back(std::move(override));
    }
}

void CompareEntityStructure(PrefabReconciliationPlan* plan,
                            uint64_t localId,
                            const PrefabEntity* base,
                            const PrefabEntity* latest,
                            const PrefabLocalEntitySnapshot* local) {
    const std::optional<uint64_t> baseParent = base->parentLocalId;
    const std::optional<uint64_t> latestParent = latest->parentLocalId;
    const std::optional<uint64_t> localParent = local->parentLocalId;
    if (baseParent != latestParent) {
        if (localParent != baseParent && localParent != latestParent) {
            const std::string id = "parent:" + std::to_string(localId);
            AddConflict(plan, id, PrefabConflictType::EntityParent, localId, "",
                        "", baseParent ? nlohmann::json(*baseParent) : nullptr,
                        latestParent ? nlohmann::json(*latestParent) : nullptr,
                        localParent ? nlohmann::json(*localParent) : nullptr,
                        "source and local parent changed");
        } else if (localParent != latestParent) {
            AddStructural(plan, PrefabStructuralChangeKind::SetParent, localId,
                          latestParent ? *latestParent : 0, "", "", nullptr, {},
                          {});
        }
    }
    if (base->name != latest->name) {
        if (local->name != base->name && local->name != latest->name) {
            const std::string id = "name:" + std::to_string(localId);
            AddConflict(plan, id, PrefabConflictType::EntityName, localId, "",
                        "", base->name, latest->name, local->name,
                        "source and local name changed");
        } else if (local->name != latest->name) {
            AddStructural(plan, PrefabStructuralChangeKind::SetName, localId, 0,
                          latest->name, "", nullptr, {}, {});
        }
    }
    CompareComponents(plan, localId, base, latest, local);
}

} // namespace

PrefabReconciliationPlan BuildPrefabReconciliation(
    const PrefabSourceSnapshot& base,
    const PrefabSourceSnapshot& latest,
    const PrefabLocalInstanceSnapshot& local) {
    PrefabReconciliationPlan plan;
    plan.outOfDate = base.contentDigest != latest.contentDigest ||
                     base.contentPath != latest.contentPath;
    if (base.prefabGuid.IsNull() || latest.prefabGuid.IsNull() ||
        base.prefabGuid != latest.prefabGuid) {
        plan.validationErrors.push_back("Base/Latest GUID mismatch");
        plan.hasConflicts = true;
        return plan;
    }
    if (local.rootEntityId == 0 || local.localToScene.empty()) {
        plan.validationErrors.push_back("Local instance mapping empty");
        plan.hasConflicts = true;
        return plan;
    }
    std::unordered_map<uint64_t, const PrefabEntity*> baseById;
    std::unordered_map<uint64_t, const PrefabEntity*> latestById;
    std::unordered_map<uint64_t, const PrefabLocalEntitySnapshot*> localById;
    for (const auto& entity : base.model.entities) {
        if (!baseById.emplace(entity.localId, &entity).second) {
            plan.validationErrors.push_back("duplicate Base Local ID");
            plan.hasConflicts = true;
            return plan;
        }
    }
    for (const auto& entity : latest.model.entities) {
        if (!latestById.emplace(entity.localId, &entity).second) {
            plan.validationErrors.push_back("duplicate Latest Local ID");
            plan.hasConflicts = true;
            return plan;
        }
    }
    for (const auto& entity : local.entities) {
        if (!localById.emplace(entity.localId, &entity).second) {
            plan.validationErrors.push_back("duplicate Local Local ID");
            plan.hasConflicts = true;
            return plan;
        }
    }

    std::set<uint64_t> ids;
    for (const auto& item : baseById) ids.insert(item.first);
    for (const auto& item : latestById) ids.insert(item.first);
    for (const auto& item : localById) ids.insert(item.first);
    for (const uint64_t id : ids) {
        const PrefabEntity* b = baseById.count(id) ? baseById[id] : nullptr;
        const PrefabEntity* l = latestById.count(id) ? latestById[id] : nullptr;
        const PrefabLocalEntitySnapshot* loc =
            localById.count(id) ? localById[id] : nullptr;
        if (!b && l && !loc) {
            AddStructural(&plan, PrefabStructuralChangeKind::AddEntity, id,
                          l->parentLocalId ? *l->parentLocalId : 0, l->name, "",
                          nullptr, l->components, l->localTransform);
            continue;
        }
        if (!b && !l && loc) {
            const std::string conflictId = "entity:extra:" + std::to_string(id);
            AddConflict(&plan, conflictId, PrefabConflictType::UnsupportedLocalEdit,
                        id, "", "", nullptr, nullptr, loc->name,
                        "local-only entity");
            continue;
        }
        if (b && !l && !loc) continue;
        if (b && l && !loc) {
            const std::string conflictId = "entity:missing:" + std::to_string(id);
            AddConflict(&plan, conflictId, PrefabConflictType::EntityMissing, id,
                        "", "", b->name, l->name, nullptr,
                        "mapped entity is missing in Scene");
            continue;
        }
        if (b && !l && loc) {
            bool localChanged = loc->name != b->name ||
                                loc->parentLocalId != b->parentLocalId;
            for (const auto& record : local.overrides) {
                if (record.localId == id) {
                    localChanged = true;
                    break;
                }
            }
            if (!localChanged) {
                for (const auto& component : b->components) {
                    const auto* localComponent = FindLocalComponent(loc, component.type);
                    if (!localComponent || localComponent->data != component.data) {
                        localChanged = true;
                        break;
                    }
                }
            }
            if (!localChanged) {
                for (const auto& component : loc->components) {
                    if (!FindComponent(b, component.type)) {
                        localChanged = true;
                        break;
                    }
                }
            }
            if (!localChanged) {
                for (const auto& candidate : local.entities) {
                    if (candidate.parentLocalId == id &&
                        latestById.count(candidate.localId)) {
                        localChanged = true;
                        break;
                    }
                }
            }
            if (localChanged) {
                const std::string conflictId = "entity:delete:" + std::to_string(id);
                AddConflict(&plan, conflictId, PrefabConflictType::EntityDelete, id,
                            "", "", b->name, nullptr, loc->name,
                            "source removed entity with local changes");
            } else {
                AddStructural(&plan, PrefabStructuralChangeKind::DeleteEntity, id, 0,
                              "", "", nullptr, {}, {});
            }
            continue;
        }
        if (b && l && loc) {
            CompareEntityStructure(&plan, id, b, l, loc);
            continue;
        }
        const std::string conflictId = "entity:identity:" + std::to_string(id);
        AddConflict(&plan, conflictId, PrefabConflictType::UnsupportedLocalEdit, id,
                    "", "", b ? b->name : nullptr, l ? l->name : nullptr,
                    loc ? loc->name : nullptr, "unclassifiable entity identity");
    }

    RebaseOverrides(&plan, base, latest, local);

    std::sort(plan.overrides.begin(), plan.overrides.end(),
              [](const PrefabReconciliationOverride& a,
                 const PrefabReconciliationOverride& b) {
                  if (a.localId != b.localId) return a.localId < b.localId;
                  if (a.component != b.component) return a.component < b.component;
                  return a.field < b.field;
              });
    std::sort(plan.fields.begin(), plan.fields.end(),
              [](const PrefabReconciliationField& a, const PrefabReconciliationField& b) {
                  if (a.localId != b.localId) return a.localId < b.localId;
                  if (a.component != b.component) return a.component < b.component;
                  return a.field < b.field;
              });
    std::sort(plan.structural.begin(), plan.structural.end(),
              [](const PrefabReconciliationStructural& a, const PrefabReconciliationStructural& b) {
                  if (a.localId != b.localId) return a.localId < b.localId;
                  return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              });
    std::sort(plan.conflicts.begin(), plan.conflicts.end(),
              [](const PrefabReconciliationConflict& a, const PrefabReconciliationConflict& b) {
                  return a.id < b.id;
              });
    plan.hasConflicts = plan.hasConflicts || !plan.conflicts.empty() ||
                        !plan.validationErrors.empty();
    return plan;
}

const char* PrefabConflictTypeText(PrefabConflictType type) {
    switch (type) {
    case PrefabConflictType::Field: return "field";
    case PrefabConflictType::EntityMissing: return "entity_missing";
    case PrefabConflictType::EntityDelete: return "entity_delete";
    case PrefabConflictType::EntityName: return "entity_name";
    case PrefabConflictType::EntityParent: return "entity_parent";
    case PrefabConflictType::ComponentSet: return "component_set";
    case PrefabConflictType::UnsupportedLocalEdit:
        return "unsupported_local_edit";
    case PrefabConflictType::UnsupportedComponentChange:
        return "unsupported_component_change";
    }
    return "";
}

bool ParsePrefabConflictType(const std::string& text, PrefabConflictType* out) {
    if (!out) return false;
    for (int i = 0; i <= static_cast<int>(PrefabConflictType::UnsupportedComponentChange); ++i) {
        const auto type = static_cast<PrefabConflictType>(i);
        if (text == PrefabConflictTypeText(type)) {
            *out = type;
            return true;
        }
    }
    return false;
}

namespace {

bool ValidateSourcePath(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
    if (path.find("..") != std::string::npos) return false;
    return true;
}

bool ValidateDigest(const std::string& digest) {
    if (digest.size() != 16) return false;
    for (const char c : digest)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

} // namespace

bool ValidatePrefabReconciliationState(
    const PrefabReconciliationState& state, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!ValidateDigest(state.latestSourceDigest))
        return fail("reconciliation latest digest is invalid");
    if (!ValidateSourcePath(state.latestSourcePath))
        return fail("reconciliation latest path is invalid");
    if (state.conflicts.empty())
        return fail("reconciliation state has no conflicts");
    std::unordered_set<std::string> ids;
    for (const auto& conflict : state.conflicts) {
        if (conflict.id.empty())
            return fail("reconciliation conflict ID is empty");
        if (!ids.insert(conflict.id).second)
            return fail("reconciliation conflict ID is duplicated");
        if (PrefabConflictTypeText(conflict.type)[0] == '\0')
            return fail("reconciliation conflict type is invalid");
        if (conflict.localId == 0)
            return fail("reconciliation conflict Local ID is zero");
        if (conflict.type == PrefabConflictType::Field) {
            if (conflict.component.empty() || conflict.field.empty()) {
                return fail("reconciliation field conflict path is empty");
            }
            PrefabOverrideFieldSpec spec;
            if (!ResolvePrefabOverrideField(conflict.component, conflict.field,
                                            &spec)) {
                return fail("reconciliation field conflict path is unsupported");
            }
        }
    }
    return true;
}

nlohmann::json PrefabReconciliationStateToJson(
    const PrefabReconciliationState& state) {
    nlohmann::json json = nlohmann::json::object();
    json["latestSourceDigest"] = state.latestSourceDigest;
    json["latestSourcePath"] = state.latestSourcePath;
    json["conflicts"] = nlohmann::json::array();
    for (const auto& conflict : state.conflicts) {
        nlohmann::json item = {
            {"id", conflict.id},
            {"type", PrefabConflictTypeText(conflict.type)},
            {"localId", conflict.localId},
            {"detail", conflict.detail},
            {"base", conflict.base},
            {"latest", conflict.latest},
            {"local", conflict.local}
        };
        if (!conflict.component.empty()) item["component"] = conflict.component;
        if (!conflict.field.empty()) item["field"] = conflict.field;
        json["conflicts"].push_back(std::move(item));
    }
    return json;
}

bool PrefabReconciliationStateFromJson(
    const nlohmann::json& json, PrefabReconciliationState* out,
    std::string* error) {
    if (!out) return false;
    *out = {};
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (!json.is_object())
        return fail("reconciliation state must be an object");
    if (!json.contains("latestSourceDigest") ||
        !json["latestSourceDigest"].is_string() ||
        !json.contains("latestSourcePath") ||
        !json["latestSourcePath"].is_string() ||
        !json.contains("conflicts") || !json["conflicts"].is_array()) {
        return fail("reconciliation state is incomplete");
    }
    out->latestSourceDigest = json["latestSourceDigest"].get<std::string>();
    out->latestSourcePath = json["latestSourcePath"].get<std::string>();
    std::unordered_set<std::string> ids;
    for (const auto& item : json["conflicts"]) {
        if (!item.is_object() || !item.contains("id") ||
            !item["id"].is_string() || !item.contains("type") ||
            !item["type"].is_string() || !item.contains("localId") ||
            !item["localId"].is_number_unsigned() ||
            !item.contains("detail") || !item["detail"].is_string() ||
            !item.contains("base") || !item.contains("latest") ||
            !item.contains("local")) {
            return fail("reconciliation conflict is malformed");
        }
        PrefabReconciliationConflict conflict;
        conflict.id = item["id"].get<std::string>();
        if (!ParsePrefabConflictType(item["type"].get<std::string>(),
                                     &conflict.type)) {
            return fail("reconciliation conflict type is unknown");
        }
        conflict.localId = item["localId"].get<uint64_t>();
        conflict.detail = item["detail"].get<std::string>();
        conflict.base = item["base"];
        conflict.latest = item["latest"];
        conflict.local = item["local"];
        if (item.contains("component")) {
            if (!item["component"].is_string())
                return fail("reconciliation conflict component is invalid");
            conflict.component = item["component"].get<std::string>();
        }
        if (item.contains("field")) {
            if (!item["field"].is_string())
                return fail("reconciliation conflict field is invalid");
            conflict.field = item["field"].get<std::string>();
        }
        if (!ids.insert(conflict.id).second)
            return fail("reconciliation conflict ID is duplicated");
        out->conflicts.push_back(std::move(conflict));
    }
    return ValidatePrefabReconciliationState(*out, error);
}
} // namespace Engine::Content
