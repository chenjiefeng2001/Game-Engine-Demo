/**
 * @file SceneSerializerV1.cpp
 * @brief 场景序列化 v1 实现（Ring10-12）
 */

#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Content/Utf8Path.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/Content/PrefabOverrideModel.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Log.h"

#include <fstream>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Engine::Content {

namespace {
    bool ValidateV2Graph(const SceneSnapshot& snap, std::string& err);
    Logger s_Log("SceneSerializerV1");
    constexpr int kFormatVersion = 2;
}

// ── 快照 ↔ JSON ─────────────────────────────────────────

nlohmann::json SerializeSnapshot(const SceneSnapshot& snap) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["version"] = snap.version;
    j["entities"] = nlohmann::json::array();
    for (const auto& e : snap.entities) {
        nlohmann::json je = {
            { "name", e.name },
            { "position", { e.px, e.py, e.pz } },
        };
        if (snap.version >= 2) {
            je["id"] = e.entityId;
            if (e.parentId != 0) je["parent"] = e.parentId;
        if (e.prefabInstance.has_value()) {
            nlohmann::json pi;
            pi["guid"] = e.prefabInstance->prefabGuid.ToHex();
            pi["sourceRevision"] = e.prefabInstance->sourceRevision;
            if (e.prefabInstance->legacySourceIdentity) {
                pi["legacySourceIdentity"] = true;
            } else {
                pi["sourceDigest"] = e.prefabInstance->sourceDigest;
                pi["sourcePath"] = e.prefabInstance->sourcePath;
            }
            nlohmann::json map = nlohmann::json::object();
            for (const auto& [localId, sceneId] : e.prefabInstance->localToScene)
                map[std::to_string(localId)] = sceneId;
            pi["localToScene"] = std::move(map);
            pi["syncState"] =
                e.prefabInstance->syncState == PrefabSyncState::Overrides
                    ? "overrides"
                    : e.prefabInstance->syncState == PrefabSyncState::ApplyPending
                          ? "apply_pending"
                          : e.prefabInstance->syncState == PrefabSyncState::RecoveryRequired
                                ? "recovery_required"
                                : e.prefabInstance->syncState == PrefabSyncState::Conflict
                                      ? "conflict" : "clean";
            nlohmann::json overrides = nlohmann::json::array();
            for (const auto& record : e.prefabInstance->overrides)
                overrides.push_back({{"localId", record.localId},
                                     {"component", record.component},
                                     {"field", record.field},
                                     {"value", record.value}});
            pi["overrides"] = std::move(overrides);
            if (e.prefabInstance->reconciliation.has_value())
                pi["reconciliation"] = PrefabReconciliationStateToJson(
                    *e.prefabInstance->reconciliation);
            je["prefabInstance"] = std::move(pi);
        }
        }
        if (!(e.rx == 0.f && e.ry == 0.f && e.rz == 0.f && e.rw == 1.f))
            je["rotation"] = { e.rx, e.ry, e.rz, e.rw };
        if (!(e.sx == 1.f && e.sy == 1.f && e.sz == 1.f))
            je["scale"] = { e.sx, e.sy, e.sz };
        if (!e.spriteGuid.IsNull()) je["sprite"] = e.spriteGuid.ToHex();
        if (!e.scriptGuid.IsNull()) je["script"] = e.scriptGuid.ToHex();
        // 契约组件（F1-D）：type = 稳定类型名，data = 组件 Serialize 产出
        if (!e.components.empty()) {
            auto cj = nlohmann::json::array();
            for (const auto& c : e.components)
                cj.push_back({ { "type", c.type }, { "data", c.data } });
            je["components"] = std::move(cj);
        }
        j["entities"].push_back(je);
    }
    return j;
}

bool DeserializeSnapshot(const nlohmann::json& j, SceneSnapshot& out,
                         std::string& err) {
    out = {};
    if (!j.is_object()) { err = "root is not an object"; return false; }
    if (!j.contains("format") || j["format"] != "engine.scene") {
        err = "not an engine.scene file"; return false;
    }
    int version = 1;
    if (j.contains("version")) {
        if (!j["version"].is_number_integer() && !j["version"].is_number_unsigned()) {
            err = "version must be an integer";
            return false;
        }
        version = j["version"].get<int>();
        if (version < 1 || version > kFormatVersion) {
            err = "unsupported scene version " + std::to_string(version);
            return false;
        }
    }
    out.version = version;
    if (!j.contains("entities") || !j["entities"].is_array()) {
        err = "missing entities array"; return false;
    }

    for (const auto& e : j["entities"]) {
        SerializedEntity se;
        if (!e.contains("name") || !e["name"].is_string()) {
            continue;   // 契约：缺 name 的实体跳过（调用方经 warnings 得知需自行统计）
        }
        if (version >= 2) {
            if (!e.contains("id") || !e["id"].is_number_unsigned()) {
                err = "v2 entity missing id";
                return false;
            }
            se.entityId = e["id"].get<uint64>();
            if (e.contains("parent")) {
                if (!e["parent"].is_number_unsigned()) {
                    err = "v2 parent must be an unsigned integer";
                    return false;
                }
                se.parentId = e["parent"].get<uint64>();
            }
            if (e.contains("prefabInstance")) {
                if (!e["prefabInstance"].is_object()) {
                    err = "v2 prefabInstance must be an object";
                    return false;
                }
                const auto& pi = e["prefabInstance"];
                if (!pi.contains("guid") || !pi["guid"].is_string() ||
                    !pi.contains("sourceRevision") ||
                    !pi["sourceRevision"].is_number_integer() ||
                    !pi.contains("localToScene") ||
                    !pi["localToScene"].is_object()) {
                    err = "v2 prefabInstance missing/invalid fields";
                    return false;
                }
                SerializedPrefabInstance metadata;
                metadata.prefabGuid =
                    ResourceGUID::FromHex(pi["guid"].get<std::string>());
                if (metadata.prefabGuid.IsNull()) {
                    err = "v2 prefabInstance GUID is invalid";
                    return false;
                }
                metadata.sourceRevision =
                    pi["sourceRevision"].get<int64_t>();
                if (metadata.sourceRevision < 0) {
                    err = "v2 prefabInstance sourceRevision is negative";
                    return false;
                }
                for (auto it = pi["localToScene"].begin();
                     it != pi["localToScene"].end(); ++it) {
                    if (!it.value().is_number_unsigned()) {
                        err = "v2 prefabInstance scene id must be unsigned";
                        return false;
                    }
                    size_t consumed = 0;
                    uint64_t localId = 0;
                    try {
                        localId = std::stoull(it.key(), &consumed);
                    } catch (...) {
                        consumed = 0;
                    }
                    if (consumed == 0 || consumed != it.key().size() ||
                        localId == 0) {
                        err = "v2 prefabInstance local id is invalid";
                        return false;
                    }
                    const uint64_t sceneId = it.value().get<uint64_t>();
                    if (sceneId == 0 ||
                        !metadata.localToScene.emplace(localId, sceneId).second) {
                        err = "v2 prefabInstance map is invalid";
                        return false;
                    }
                }
                if (metadata.localToScene.empty()) {
                    err = "v2 prefabInstance map is empty";
                    return false;
                }
                const bool hasDigest = pi.contains("sourceDigest");
                const bool hasPath = pi.contains("sourcePath");
                const bool declaredLegacy =
                    pi.value("legacySourceIdentity", false);
                if (declaredLegacy) {
                    if (hasDigest || hasPath) {
                        err = "legacy prefabInstance has source fields";
                        return false;
                    }
                    metadata.legacySourceIdentity = true;
                } else if (hasDigest || hasPath) {
                    if (!hasDigest || !hasPath ||
                        !pi["sourceDigest"].is_string() ||
                        !pi["sourcePath"].is_string()) {
                        err = "prefabInstance source identity is incomplete";
                        return false;
                    }
                    metadata.sourceDigest = pi["sourceDigest"].get<std::string>();
                    metadata.sourcePath = pi["sourcePath"].get<std::string>();
                    if (metadata.sourceDigest.size() != 16 ||
                        metadata.sourcePath.empty()) {
                        err = "prefabInstance source identity is invalid";
                        return false;
                    }
                    for (const char c : metadata.sourceDigest)
                        if (!((c >= 0x30 && c <= 0x39) ||
                              (c >= 0x61 && c <= 0x66))) {
                            err = "prefabInstance sourceDigest is invalid";
                            return false;
                        }
                } else {
                    metadata.legacySourceIdentity = true;
                }

                if (pi.contains("syncState")) {
                    if (!pi["syncState"].is_string()) {
                        err = "prefabInstance syncState must be a string";
                        return false;
                    }
                    const std::string state = pi["syncState"].get<std::string>();
                    if (state == "clean") metadata.syncState = PrefabSyncState::Clean;
                    else if (state == "overrides") metadata.syncState = PrefabSyncState::Overrides;
                    else if (state == "apply_pending") metadata.syncState = PrefabSyncState::ApplyPending;
                    else if (state == "recovery_required") metadata.syncState = PrefabSyncState::RecoveryRequired;
                    else if (state == "conflict") metadata.syncState = PrefabSyncState::Conflict;
                    else {
                        err = "prefabInstance syncState is invalid";
                        return false;
                    }
                }
                if (pi.contains("overrides")) {
                    if (!pi["overrides"].is_array()) {
                        err = "prefabInstance overrides must be an array";
                        return false;
                    }
                    std::unordered_set<std::string> overrideIds;
                    for (const auto& item : pi["overrides"]) {
                        if (!item.is_object() || !item.contains("localId") ||
                            !item["localId"].is_number_unsigned() ||
                            !item.contains("component") ||
                            !item["component"].is_string() ||
                            !item.contains("field") ||
                            !item["field"].is_string() ||
                            !item.contains("value")) {
                            err = "prefabInstance override is malformed";
                            return false;
                        }
                        PrefabOverrideRecord record;
                        record.localId = item["localId"].get<uint64_t>();
                        record.component = item["component"].get<std::string>();
                        record.field = item["field"].get<std::string>();
                        record.value = item["value"];
                        std::string overrideError;
                        if (!ValidatePrefabOverrideRecord(record, &overrideError)) {
                            err = "prefabInstance override invalid: " + overrideError;
                            return false;
                        }
                        const std::string key = std::to_string(record.localId) +
                                                "|" + record.component + "|" +
                                                record.field;
                        if (!overrideIds.insert(key).second) {
                            err = "prefabInstance duplicate override";
                            return false;
                        }
                        metadata.overrides.push_back(std::move(record));
                    }
                }
                if (pi.contains("reconciliation")) {
                    std::string reconciliationError;
                    PrefabReconciliationState reconciliation;
                    if (!PrefabReconciliationStateFromJson(
                            pi["reconciliation"], &reconciliation,
                            &reconciliationError)) {
                        err = "prefabInstance reconciliation invalid: " +
                              reconciliationError;
                        return false;
                    }
                    metadata.reconciliation = std::move(reconciliation);
                }
                if (metadata.reconciliation.has_value() &&
                    metadata.syncState != PrefabSyncState::Conflict) {
                    err = "prefabInstance reconciliation requires conflict state";
                    return false;
                }
                if (!metadata.reconciliation.has_value() &&
                    metadata.syncState == PrefabSyncState::Conflict) {
                    err = "prefabInstance conflict state has no records";
                    return false;
                }
                if (metadata.legacySourceIdentity &&
                    (metadata.reconciliation.has_value() ||
                     !metadata.overrides.empty())) {
                    err = "legacy prefabInstance cannot have overrides";
                    return false;
                }
                if (metadata.overrides.empty() &&
                    (metadata.syncState == PrefabSyncState::Overrides ||
                     metadata.syncState == PrefabSyncState::ApplyPending)) {
                    err = "prefabInstance syncState expects overrides";
                    return false;
                }
                if (!metadata.overrides.empty() &&
                    metadata.syncState == PrefabSyncState::Clean) {
                    err = "prefabInstance syncState does not reflect overrides";
                    return false;
                }
                NormalizePrefabOverrides(&metadata.overrides);
                se.prefabInstance = std::move(metadata);
            }
        }
        se.name = e["name"].get<std::string>();
        if (e.contains("position") && e["position"].is_array()
                && e["position"].size() >= 3) {
            // 元素类型必须都是数值。position 是**可选**的实体级数据，
            // 不可用时退回默认坐标 —— 与既有"position 缺失/长度不足"的
            // 宽容契约一致，而不是让整个快照加载失败。
            const auto& p = e["position"];
            if (p[0].is_number() && p[1].is_number() && p[2].is_number()) {
                se.px = p[0].get<float>();
                se.py = p[1].get<float>();
                se.pz = p[2].get<float>();
            }
        }
        if (e.contains("rotation") && e["rotation"].is_array() &&
            e["rotation"].size() >= 4 &&
            e["rotation"][0].is_number() && e["rotation"][1].is_number() &&
            e["rotation"][2].is_number() && e["rotation"][3].is_number()) {
            se.rx = e["rotation"][0].get<float>();
            se.ry = e["rotation"][1].get<float>();
            se.rz = e["rotation"][2].get<float>();
            se.rw = e["rotation"][3].get<float>();
        }
        if (e.contains("scale") && e["scale"].is_array() &&
            e["scale"].size() >= 3 && e["scale"][0].is_number() &&
            e["scale"][1].is_number() && e["scale"][2].is_number()) {
            se.sx = e["scale"][0].get<float>();
            se.sy = e["scale"][1].get<float>();
            se.sz = e["scale"][2].get<float>();
        }
        if (e.contains("sprite") && e["sprite"].is_string())
            se.spriteGuid = ResourceGUID::FromHex(e["sprite"].get<std::string>());
        if (e.contains("script") && e["script"].is_string())
            se.scriptGuid = ResourceGUID::FromHex(e["script"].get<std::string>());
        // 契约组件（F1-D）：未知类型保留数据但不在加载层扩展（负路径在
        // InstantiateScene 处理），这里原样还原到快照避免信息丢失。
        if (e.contains("components") && e["components"].is_array()) {
            for (const auto& c : e["components"]) {
                if (!c.is_object() || !c.contains("type") || !c["type"].is_string())
                    continue;   // 结构损坏的子项跳过（整体仍然可加载）
                SerializedComponent sc;
                sc.type = c["type"].get<std::string>();
                if (c.contains("data") && c["data"].is_object())
                    sc.data = c["data"];
                se.components.push_back(std::move(sc));
            }
        }
        out.entities.push_back(std::move(se));
    }
    if (version >= 2 && !ValidateV2Graph(out, err)) {
        out = {};
        return false;
    }
    return true;
}

// ── Scene 桥接 ──────────────────────────────────────────
namespace {
    bool ValidateV2Graph(const SceneSnapshot& snap, std::string& err);
    bool ValidateV2Graph(const SceneSnapshot& snap, std::string& err) {
        std::unordered_map<uint64, const SerializedEntity*> byId;
        for (const auto& e : snap.entities) {
            if (e.entityId == 0) { err = "v2 entity id must be nonzero"; return false; }
            if (!byId.emplace(e.entityId, &e).second) {
                err = "v2 duplicate entity id";
                return false;
            }
        }
        for (const auto& e : snap.entities) {
            if (e.parentId == 0) continue;
            if (e.parentId == e.entityId) {
                err = "v2 self parent";
                return false;
            }
            if (byId.find(e.parentId) == byId.end()) {
                err = "v2 parent id does not exist";
                return false;
        }
            }
        for (const auto& e : snap.entities) {
            std::unordered_set<uint64> seen;
            uint64 cursor = e.entityId;
            while (cursor != 0) {
                if (!seen.insert(cursor).second) {
                    err = "v2 parent cycle";
                    return false;
                }
                cursor = byId.at(cursor)->parentId;
            }
        }
        std::unordered_map<uint64, std::vector<uint64>> children;
        for (const auto& e : snap.entities)
            if (e.parentId != 0) children[e.parentId].push_back(e.entityId);
        std::unordered_set<uint64> instanceAnchors;
        for (const auto& e : snap.entities) {
            if (!e.prefabInstance.has_value()) continue;
            const auto& metadata = *e.prefabInstance;
            if (metadata.prefabGuid.IsNull() || metadata.sourceRevision < 0 ||
                metadata.localToScene.empty()) {
                err = "v2 prefabInstance metadata invalid";
                return false;
            }
            instanceAnchors.insert(e.entityId);
        }
        for (const auto& e : snap.entities) {
            if (!e.prefabInstance.has_value()) continue;
            uint64 cursor = e.parentId;
            while (cursor != 0) {
                if (instanceAnchors.count(cursor)) {
                    err = "v2 nested prefab instances are not supported";
                    return false;
                }
                cursor = byId.at(cursor)->parentId;
            }
            std::unordered_set<uint64> subtree;
            std::function<void(uint64)> visit = [&](uint64 id) {
                subtree.insert(id);
                const auto it = children.find(id);
                if (it != children.end())
                    for (const uint64 child : it->second) visit(child);
            };
            visit(e.entityId);
            std::unordered_set<uint64> mapped;
            bool rootMapped = false;
            for (const auto& [localId, sceneId] : e.prefabInstance->localToScene) {
                if (localId == 0 || sceneId == 0 ||
                    byId.find(sceneId) == byId.end() ||
                    !mapped.insert(sceneId).second) {
                    err = "v2 prefabInstance map invalid";
                    return false;
                }
                if (sceneId == e.entityId) rootMapped = true;
            }
            std::unordered_set<std::string> overrideIds;
            for (const auto& record : e.prefabInstance->overrides) {
                std::string overrideError;
                if (!ValidatePrefabOverrideRecord(record, &overrideError)) {
                    err = "v2 prefabInstance override invalid: " + overrideError;
                    return false;
                }
                if (e.prefabInstance->localToScene.find(record.localId) ==
                    e.prefabInstance->localToScene.end()) {
                    err = "v2 prefabInstance override localId is not mapped";
                    return false;
                }
                const std::string key = std::to_string(record.localId) + "|" +
                                        record.component + "|" + record.field;
                if (!overrideIds.insert(key).second) {
                    err = "v2 duplicate prefabInstance override";
                    return false;
                }
            }
            if (e.prefabInstance->legacySourceIdentity &&
                !e.prefabInstance->overrides.empty()) {
                err = "v2 legacy prefabInstance cannot have overrides";
                return false;
            }
            if (!e.prefabInstance->legacySourceIdentity) {
                if (e.prefabInstance->sourceDigest.size() != 16 ||
                    e.prefabInstance->sourcePath.empty()) {
                    err = "v2 prefabInstance source identity invalid";
                    return false;
                }
                for (const char c : e.prefabInstance->sourceDigest)
                    if (!((c >= 0x30 && c <= 0x39) ||
                          (c >= 0x61 && c <= 0x66))) {
                        err = "v2 prefabInstance sourceDigest invalid";
                        return false;
                    }
            }
            if (!rootMapped || mapped != subtree) {
                err = "v2 prefabInstance map does not match its subtree";
                return false;
            }
        }
        return true;
    }
}
SceneSnapshot CaptureSceneById(
    const Scene& scene,
    const std::unordered_map<uint64, EntityContentBinding>& bindings) {
    SceneSnapshot snap;
    snap.version = 2;
    const auto all = scene.GetAllEntities();
    snap.entities.reserve(all.size());
    for (GameObject* raw : all) {
        if (!raw) continue;
        const auto& obj = *raw;
        SerializedEntity e;
        e.entityId = obj.GetSceneEntityId();
        e.parentId = obj.GetParent() ? obj.GetParent()->GetSceneEntityId() : 0;
        e.name = obj.GetName();
        const Vec3 p = obj.GetTransform().GetPosition();
        e.px = p.x; e.py = p.y; e.pz = p.z;
        const Quat q = obj.GetTransform().GetRotationQuat();
        e.rx = q.x; e.ry = q.y; e.rz = q.z; e.rw = q.w;
        const Vec3 s = obj.GetTransform().GetScale();
        e.sx = s.x; e.sy = s.y; e.sz = s.z;
        PrefabInstanceMetadata metadata;
        if (scene.GetPrefabInstanceMetadata(e.entityId, &metadata)) {
            SerializedPrefabInstance serialized;
            serialized.prefabGuid = metadata.prefabGuid;
            serialized.sourceRevision = metadata.sourceRevision;
            serialized.sourceDigest = metadata.sourceDigest;
            serialized.sourcePath = metadata.sourcePath;
            serialized.legacySourceIdentity = metadata.legacySourceIdentity;
            serialized.overrides = metadata.overrides;
            serialized.syncState = metadata.syncState;
            serialized.reconciliation = metadata.reconciliation;
            serialized.localToScene = metadata.localToScene;
            e.prefabInstance = std::move(serialized);
        }
        const auto binding = bindings.find(e.entityId);
        if (binding != bindings.end()) {
            e.spriteGuid = binding->second.spriteGuid;
            e.scriptGuid = binding->second.scriptGuid;
        }
        obj.ForEachComponent([&e](const Component& c) {
            const char* tn = c.GetComponentTypeName();
            if (!tn) return;
            SerializedComponent sc;
            sc.type = tn;
            c.Serialize(sc.data);
            e.components.push_back(std::move(sc));
        });
        snap.entities.push_back(std::move(e));
    }
    return snap;
}

SceneSnapshot CaptureScene(
    const Scene& scene,
    const std::vector<EntityContentBinding>& bindings) {
    std::unordered_map<uint64, EntityContentBinding> byId;
    const auto& roots = scene.GetObjects();
    for (size_t i = 0; i < roots.size() && i < bindings.size(); ++i) {
        if (roots[i]) byId[roots[i]->GetSceneEntityId()] = bindings[i];
    }
    return CaptureSceneById(scene, byId);
}

LoadResult InstantiateScene(const SceneSnapshot& snap, Scene& outScene,
                            TextureManager& texMgr,
                            const ContentRegistry& registry,
                            AssetPathResolver resolveAssetPath) {
    LoadResult r;
    r.ok = true;

    const auto resolveStored = [&resolveAssetPath](const std::string& stored)
        -> std::pair<std::string, bool> {
        if (stored.empty()) return {"", false};
        if (!resolveAssetPath) return {stored, true};
        const std::string resolved = resolveAssetPath(stored);
        return {resolved, !resolved.empty()};
    };

    for (const auto& e : snap.entities) {
        if (snap.version >= 2 && e.entityId == 0) {
            r.ok = false;
            r.warnings.push_back("v2 entity id is zero");
            outScene.Clear();
            return r;
        }
        if (snap.version >= 2 && outScene.FindByEntityId(e.entityId)) {
            r.ok = false;
            r.warnings.push_back("duplicate v2 entity id");
            outScene.Clear();
            return r;
        }
        auto obj = std::make_shared<::Engine::GameObject>(e.name);
        if (e.entityId != 0) obj->SetSceneEntityId(e.entityId);
        obj->GetTransform().SetPosition(e.px, e.py, e.pz);
        obj->GetTransform().SetRotationQuat(Quat(e.rx, e.ry, e.rz, e.rw));
        obj->GetTransform().SetScale(Vec3(e.sx, e.sy, e.sz));

        EntityContentBinding binding;

        if (!e.spriteGuid.IsNull()) {
            const std::string stored = registry.ResolvePath(e.spriteGuid);
            if (stored.empty()) {
                r.warnings.push_back("entity '" + e.name
                    + "': sprite GUID missing in registry (loaded without sprite)");
            } else {
                auto [path, usable] = resolveStored(stored);
                if (!usable) {
                    r.warnings.push_back("entity '" + e.name
                        + "': sprite path '" + stored
                        + "' rejected by project root (loaded without sprite)");
                } else {
                    auto* spr = obj->AddComponent<SpriteComponent>();
                    spr->SetTexture(texMgr, path);
                    binding.spriteGuid = e.spriteGuid;
                }
            }
        }

        if (!e.scriptGuid.IsNull()) {
            const std::string stored = registry.ResolvePath(e.scriptGuid);
            if (stored.empty()) {
                r.warnings.push_back("entity '" + e.name
                    + "': script GUID missing in registry (loaded without script)");
            } else {
                auto [path, usable] = resolveStored(stored);
                if (!usable) {
                    r.warnings.push_back("entity '" + e.name
                        + "': script path '" + stored
                        + "' rejected by project root (loaded without script)");
                } else {
                    binding.scriptGuid = e.scriptGuid;
                }
            }
        }

        for (const auto& sc : e.components) {
            if (!ComponentRegistryGo::IsRegistered(sc.type)) {
                r.warnings.push_back("entity '" + e.name
                    + "': unknown contract component type '" + sc.type
                    + "' (skipped)");
                continue;
            }
            auto comp = ComponentRegistryGo::Create(sc.type);
            if (!comp) continue;
            if (!comp->Deserialize(sc.data)) {
                r.warnings.push_back("entity '" + e.name
                    + "': component '" + sc.type + "' DataBad (kept defaults)");
            }
            obj->Attach(comp);
        }

        outScene.AddObject(obj);
        const uint64 id = obj->GetSceneEntityId();
        r.bindingsById[id] = binding;
        if (e.parentId == 0) r.bindings.push_back(binding);
    }

    if (snap.version >= 2) {
        for (const auto& e : snap.entities) {
            if (e.parentId == 0) continue;
            std::string err;
            if (!outScene.SetEntityParent(e.entityId, e.parentId,
                                          ReparentMode::KeepLocal, &err)) {
                r.ok = false;
                r.warnings.push_back("v2 hierarchy apply failed: " + err);
                outScene.Clear();
                r.bindings.clear();
                r.bindingsById.clear();
                return r;
            }
        }
    }
    for (const auto& e : snap.entities) {
        if (!e.prefabInstance.has_value()) continue;
        PrefabInstanceMetadata metadata;
        metadata.prefabGuid = e.prefabInstance->prefabGuid;
        metadata.sourceRevision = e.prefabInstance->sourceRevision;
        metadata.sourceDigest = e.prefabInstance->sourceDigest;
        metadata.sourcePath = e.prefabInstance->sourcePath;
        metadata.legacySourceIdentity =
            e.prefabInstance->legacySourceIdentity;
        metadata.overrides = e.prefabInstance->overrides;
        metadata.syncState = e.prefabInstance->syncState;
        metadata.reconciliation = e.prefabInstance->reconciliation;
        metadata.localToScene = e.prefabInstance->localToScene;
        std::string err;
        if (!outScene.SetPrefabInstanceMetadata(e.entityId, metadata, &err)) {
            r.ok = false;
            r.warnings.push_back("v2 prefab metadata apply failed: " + err);
            outScene.Clear();
            r.bindings.clear();
            r.bindingsById.clear();
            return r;
        }
    }

    if (!outScene.ValidateComposition()) {
        r.ok = false;
        r.warnings.push_back("scene composition validation failed");
        outScene.Clear();
        r.bindings.clear();
        r.bindingsById.clear();
    }
    return r;
}

// ── 文件 IO ─────────────────────────────────────────────

bool SaveSnapshotToFile(const SceneSnapshot& snap, const std::string& path) {
    std::ofstream f(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f << SerializeSnapshot(snap).dump(2);
    return f.good();
}

bool LoadSnapshotFromFile(const std::string& path, SceneSnapshot& out,
                          std::string& err) {
    std::ifstream f(PathFromUtf8(path), std::ios::binary);
    if (!f.is_open()) { err = "cannot open file: " + path; return false; }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& ex) {
        err = std::string("corrupted scene file: ") + ex.what();
        return false;                       // fail-clean：out 保持空
    }
    // 提取也放进失败边界：显式校验已让正常路径不再抛，但保留一个
    // nlohmann 层面的兜底，保证任何 json 异常都不会穿出返回 bool 的加载器。
    // 只捕获 nlohmann::json::exception，不做 std::exception 一网打尽。
    try {
        return DeserializeSnapshot(j, out, err);
    } catch (const nlohmann::json::exception& ex) {
        err = std::string("malformed scene schema: ") + ex.what();
        out = {};
        return false;
    }
}

} // namespace Engine::Content
