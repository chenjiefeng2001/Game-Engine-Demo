#include "Engine/Core/Content/PrefabAsset.h"

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace Engine::Content {

namespace {

using Json = nlohmann::json;

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool IsAllowedComponentType(const std::string& type) {
    return type == "Mesh" || type == "Camera" || type == "Collider" ||
           type == "RigidBody" || type == "Skinning";
}

bool CheckKeys(const Json& value,
               std::initializer_list<std::string_view> allowed,
               std::initializer_list<std::string_view> required,
               const char* context, std::string* error) {
    if (!value.is_object())
        return Fail(error, std::string(context) + " must be an object");
    std::unordered_set<std::string_view> accepted;
    for (const auto key : allowed) accepted.insert(key);
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (accepted.find(it.key()) == accepted.end())
            return Fail(error, std::string(context) + " has unknown field '" +
                                  it.key() + "'");
    }
    for (const auto key : required) {
        if (!value.contains(std::string(key)))
            return Fail(error, std::string(context) + " is missing field '" +
                                  std::string(key) + "'");
    }
    return true;
}

bool ReadFiniteNumber(const Json& value, float* out) {
    if (!out || !value.is_number()) return false;
    double raw = 0.0;
    try {
        raw = value.get<double>();
    } catch (const std::exception&) {
        return false;
    }
    if (!std::isfinite(raw) ||
        std::abs(raw) > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    *out = static_cast<float>(raw);
    return true;
}

bool ReadFiniteArray(const Json& value, size_t count, std::vector<float>* out) {
    if (!out || !value.is_array() || value.size() != count) return false;
    out->resize(count);
    for (size_t i = 0; i < count; ++i) {
        if (!ReadFiniteNumber(value[i], &(*out)[i])) return false;
    }
    return true;
}

bool ReadInteger(const Json& value, int64_t* out) {
    if (!out || !value.is_number_integer()) return false;
    try {
        *out = value.get<int64_t>();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool ReadUInt32(const Json& value, uint32_t* out) {
    int64_t parsed = 0;
    if (!ReadInteger(value, &parsed) || parsed < 0 ||
        parsed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
        return false;
    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool IsCanonicalGuid(const Json& value, ResourceGUID* out) {
    if (!value.is_string()) return false;
    const std::string hex = value.get<std::string>();
    if (hex.size() != 32) return false;
    for (const char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    const ResourceGUID guid = ResourceGUID::FromHex(hex);
    if (guid.IsNull()) return false;
    if (out) *out = guid;
    return true;
}

bool ValidateMesh(const Json& data, const char* context, std::string* error) {
    if (!CheckKeys(data, {"meshGuid"}, {}, context, error)) return false;
    if (!data.contains("meshGuid")) return true;
    if (!IsCanonicalGuid(data["meshGuid"], nullptr))
        return Fail(error, std::string(context) + ".meshGuid must be a nonzero "
                              "canonical 32-hex GUID");
    return true;
}

bool ValidateCamera(const Json& data, const char* context, std::string* error) {
    if (!CheckKeys(data, {"enabled", "zoom", "viewport", "bounds"},
                   {"enabled", "zoom", "viewport", "bounds"}, context, error))
        return false;
    if (!data["enabled"].is_boolean())
        return Fail(error, std::string(context) + ".enabled must be boolean");
    float zoom = 0.0f;
    if (!ReadFiniteNumber(data["zoom"], &zoom) || zoom <= 0.0f)
        return Fail(error, std::string(context) +
                              ".zoom must be finite and greater than zero");
    if (!data["viewport"].is_string() || !data["bounds"].is_string())
        return Fail(error, std::string(context) +
                              ".viewport/.bounds must be strings");
    return true;
}

bool ValidateCollider(const Json& data, const char* context, std::string* error) {
    if (!CheckKeys(data,
                   {"enabled", "shape", "radius", "halfX", "halfY", "isSensor",
                    "category", "mask"},
                   {"enabled", "shape", "radius", "halfX", "halfY", "isSensor",
                    "category", "mask"},
                   context, error))
        return false;
    if (!data["enabled"].is_boolean() || !data["isSensor"].is_boolean())
        return Fail(error, std::string(context) +
                              ".enabled/.isSensor must be boolean");
    if (!data["shape"].is_string())
        return Fail(error, std::string(context) + ".shape must be a string");
    const std::string shape = data["shape"].get<std::string>();
    if (shape != "Circle" && shape != "Box")
        return Fail(error, std::string(context) + ".shape must be Circle or Box");
    for (const char* key : {"radius", "halfX", "halfY"}) {
        float value = 0.0f;
        if (!ReadFiniteNumber(data[key], &value) || value < 0.0f)
            return Fail(error, std::string(context) + "." + key +
                                  " must be finite and non-negative");
    }
    for (const char* key : {"category", "mask"}) {
        int64_t value = 0;
        if (!ReadInteger(data[key], &value) || value < 0 || value > 65535)
            return Fail(error, std::string(context) + "." + key +
                                  " must be an integer in [0, 65535]");
    }
    return true;
}

bool ValidateRigidBody(const Json& data, const char* context, std::string* error) {
    if (!CheckKeys(data, {"motionType", "linearDamping", "angularDamping"},
                   {"motionType", "linearDamping", "angularDamping"},
                   context, error))
        return false;
    if (!data["motionType"].is_string())
        return Fail(error, std::string(context) + ".motionType must be a string");
    const std::string motion = data["motionType"].get<std::string>();
    if (motion != "Static" && motion != "Kinematic" && motion != "Dynamic")
        return Fail(error, std::string(context) +
                              ".motionType must be Static/Kinematic/Dynamic");
    for (const char* key : {"linearDamping", "angularDamping"}) {
        float value = 0.0f;
        if (!ReadFiniteNumber(data[key], &value) || value < 0.0f)
            return Fail(error, std::string(context) + "." + key +
                                  " must be finite and non-negative");
    }
    return true;
}

bool ValidateSkinningSkeleton(const Json& value, const char* context,
                              std::string* error) {
    if (!value.is_array())
        return Fail(error, std::string(context) + " must be an array");
    for (size_t i = 0; i < value.size(); ++i) {
        const Json& bone = value[i];
        const std::string where = std::string(context) + "[" + std::to_string(i) + "]";
        if (!CheckKeys(bone, {"name", "parent", "bind"},
                       {"name", "parent", "bind"}, where.c_str(), error))
            return false;
        if (!bone["name"].is_string())
            return Fail(error, where + ".name must be a string");
        int64_t parent = 0;
        if (!ReadInteger(bone["parent"], &parent) ||
            parent < -1 || parent >= static_cast<int64_t>(i))
            return Fail(error, where +
                                  ".parent must be -1 or an earlier bone index");
        std::vector<float> bind;
        if (!ReadFiniteArray(bone["bind"], 16, &bind))
            return Fail(error, where + ".bind must be 16 finite numbers");
    }
    return true;
}

bool ValidateSkinningMesh(const Json& value, const char* context,
                          std::string* error) {
    if (!CheckKeys(value, {"boneMapping", "vertices", "indices"},
                   {"boneMapping", "vertices", "indices"}, context, error))
        return false;
    if (!value["boneMapping"].is_array())
        return Fail(error, std::string(context) + ".boneMapping must be an array");
    for (const Json& index : value["boneMapping"]) {
        int64_t ignored = 0;
        if (!ReadInteger(index, &ignored))
            return Fail(error, std::string(context) +
                                  ".boneMapping entries must be integers");
    }
    if (!value["vertices"].is_array())
        return Fail(error, std::string(context) + ".vertices must be an array");
    for (size_t i = 0; i < value["vertices"].size(); ++i) {
        const Json& vertex = value["vertices"][i];
        const std::string where = std::string(context) + ".vertices[" +
                                  std::to_string(i) + "]";
        if (!CheckKeys(vertex, {"pos", "nrm", "uv", "tan", "indices", "weights"},
                       {"pos", "nrm", "uv", "tan", "indices", "weights"},
                       where.c_str(), error))
            return false;
        std::vector<float> values;
        if (!ReadFiniteArray(vertex["pos"], 3, &values) ||
            !ReadFiniteArray(vertex["nrm"], 3, &values) ||
            !ReadFiniteArray(vertex["uv"], 2, &values) ||
            !ReadFiniteArray(vertex["tan"], 3, &values) ||
            !ReadFiniteArray(vertex["weights"], 4, &values))
            return Fail(error, where + " has a malformed numeric array");
        if (!vertex["indices"].is_array() || vertex["indices"].size() != 4)
            return Fail(error, where + ".indices must contain exactly 4 integers");
        for (const Json& index : vertex["indices"]) {
            uint32_t parsed = 0;
            if (!ReadUInt32(index, &parsed))
                return Fail(error, where + ".indices entries must be uint32");
        }
    }
    if (!value["indices"].is_array())
        return Fail(error, std::string(context) + ".indices must be an array");
    for (const Json& index : value["indices"]) {
        uint32_t parsed = 0;
        if (!ReadUInt32(index, &parsed))
            return Fail(error, std::string(context) + ".indices entries must be uint32");
    }
    return true;
}

bool ValidateSkinningAnimation(const Json& value, const char* context,
                               std::string* error) {
    if (!CheckKeys(value,
                   {"duration", "localTime", "playing", "timeScale", "tracks"},
                   {"duration", "localTime", "playing", "timeScale", "tracks"},
                   context, error))
        return false;
    float duration = 0.0f;
    float localTime = 0.0f;
    float timeScale = 0.0f;
    if (!ReadFiniteNumber(value["duration"], &duration) || duration < 0.0f)
        return Fail(error, std::string(context) +
                              ".duration must be finite and non-negative");
    if (!ReadFiniteNumber(value["localTime"], &localTime))
        return Fail(error, std::string(context) + ".localTime must be finite");
    if (!ReadFiniteNumber(value["timeScale"], &timeScale) || timeScale < 0.0f)
        return Fail(error, std::string(context) +
                              ".timeScale must be finite and non-negative");
    if (!value["playing"].is_boolean())
        return Fail(error, std::string(context) + ".playing must be boolean");
    if (!value["tracks"].is_array())
        return Fail(error, std::string(context) + ".tracks must be an array");
    for (size_t i = 0; i < value["tracks"].size(); ++i) {
        const Json& track = value["tracks"][i];
        const std::string where = std::string(context) + ".tracks[" +
                                  std::to_string(i) + "]";
        if (!CheckKeys(track, {"name", "type", "keys"}, {"name", "type", "keys"},
                       where.c_str(), error))
            return false;
        if (!track["name"].is_string())
            return Fail(error, where + ".name must be a string");
        int64_t type = 0;
        if (!ReadInteger(track["type"], &type))
            return Fail(error, where + ".type must be an integer");
        if (!track["keys"].is_array())
            return Fail(error, where + ".keys must be an array");
        for (size_t k = 0; k < track["keys"].size(); ++k) {
            const Json& key = track["keys"][k];
            const std::string keyWhere = where + ".keys[" + std::to_string(k) + "]";
            if (!key.is_array() || key.size() != 2)
                return Fail(error, keyWhere + " must be [time, value]");
            float time = 0.0f;
            if (!ReadFiniteNumber(key[0], &time))
                return Fail(error, keyWhere + ".time must be finite");
            if (key[1].is_number()) {
                float scalar = 0.0f;
                if (!ReadFiniteNumber(key[1], &scalar))
                    return Fail(error, keyWhere + ".value must be finite");
            } else if (key[1].is_array()) {
                if (key[1].size() < 2 || key[1].size() > 4)
                    return Fail(error, keyWhere +
                                          ".value array must have 2..4 numbers");
                for (const Json& component : key[1]) {
                    float scalar = 0.0f;
                    if (!ReadFiniteNumber(component, &scalar))
                        return Fail(error, keyWhere +
                                              ".value array must be finite");
                }
            } else {
                return Fail(error, keyWhere + ".value must be number or array");
            }
        }
    }
    return true;
}

bool ValidateSkinning(const Json& data, const char* context, std::string* error) {
    if (!CheckKeys(data, {"visible", "skeleton", "skinnedMesh", "animation"},
                   {"visible"}, context, error))
        return false;
    if (!data["visible"].is_boolean())
        return Fail(error, std::string(context) + ".visible must be boolean");
    if (data.contains("skeleton") &&
        !ValidateSkinningSkeleton(data["skeleton"],
                                  (std::string(context) + ".skeleton").c_str(),
                                  error))
        return false;
    if (data.contains("skinnedMesh") &&
        !ValidateSkinningMesh(data["skinnedMesh"],
                              (std::string(context) + ".skinnedMesh").c_str(),
                              error))
        return false;
    if (data.contains("animation") &&
        !ValidateSkinningAnimation(data["animation"],
                                   (std::string(context) + ".animation").c_str(),
                                   error))
        return false;
    return true;
}

bool ValidateTransform(const PrefabLocalTransform& transform, std::string* error) {
    const float values[10] = {
        transform.position.x, transform.position.y, transform.position.z,
        transform.rotation.x, transform.rotation.y, transform.rotation.z,
        transform.rotation.w,
        transform.scale.x, transform.scale.y, transform.scale.z};
    for (const float value : values)
        if (!std::isfinite(value))
            return Fail(error, "localTransform contains a non-finite value");
    return true;
}

} // namespace

bool ValidatePrefabComponentData(const PrefabComponentData& component,
                                 std::string* error) {
    if (!IsAllowedComponentType(component.type))
        return Fail(error, "unsupported Prefab component type '" +
                              component.type + "'");
    const std::string context = "component[" + component.type + "]";
    if (!component.data.is_object())
        return Fail(error, context + ".data must be an object");
    if (component.type == "Mesh")
        return ValidateMesh(component.data, context.c_str(), error);
    if (component.type == "Camera")
        return ValidateCamera(component.data, context.c_str(), error);
    if (component.type == "Collider")
        return ValidateCollider(component.data, context.c_str(), error);
    if (component.type == "RigidBody")
        return ValidateRigidBody(component.data, context.c_str(), error);
    if (component.type == "Skinning")
        return ValidateSkinning(component.data, context.c_str(), error);
    return Fail(error, "unsupported Prefab component type '" +
                          component.type + "'");
}

PrefabValidationResult ValidatePrefabModel(const PrefabModel& model) {
    PrefabValidationResult result;
    auto fail = [&](const std::string& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (model.formatVersion != kPrefabFormatVersion)
        return fail("unsupported Prefab formatVersion " +
                    std::to_string(model.formatVersion));
    if (model.prefabGuid.IsNull())
        return fail("Prefab prefabGuid is zero");
    if (model.rootLocalId == 0)
        return fail("Prefab rootLocalId is zero");
    if (model.entities.empty())
        return fail("Prefab has no entities");

    std::unordered_map<uint64_t, const PrefabEntity*> byId;
    for (const PrefabEntity& entity : model.entities) {
        if (entity.localId == 0)
            return fail("Prefab entity localId is zero");
        if (!byId.emplace(entity.localId, &entity).second)
            return fail("duplicate Prefab localId " +
                        std::to_string(entity.localId));
    }
    const PrefabEntity* root = nullptr;
    size_t rootCount = 0;
    for (const PrefabEntity& entity : model.entities) {
        if (!entity.parentLocalId.has_value()) {
            ++rootCount;
            root = &entity;
            continue;
        }
        const uint64_t parentId = *entity.parentLocalId;
        if (parentId == 0)
            return fail("Prefab parentLocalId is zero");
        if (parentId == entity.localId)
            return fail("Prefab entity is its own parent");
        if (byId.find(parentId) == byId.end())
            return fail("Prefab parentLocalId does not exist");
    }
    if (rootCount != 1 || root == nullptr)
        return fail("Prefab must have exactly one root");
    if (root->localId != model.rootLocalId)
        return fail("Prefab rootLocalId does not identify the root entity");

    std::unordered_map<uint64_t, int> state;
    std::function<bool(uint64_t)> visit = [&](uint64_t id) {
        const int current = state[id];
        if (current == 1) return false;  // cycle
        if (current == 2) return true;
        state[id] = 1;
        const PrefabEntity* entity = byId[id];
        if (entity->parentLocalId.has_value()) {
            if (!visit(*entity->parentLocalId)) return false;
        } else if (id != model.rootLocalId) {
            return false;
        }
        state[id] = 2;
        return true;
    };
    for (const PrefabEntity& entity : model.entities) {
        if (!visit(entity.localId))
            return fail("Prefab hierarchy contains a cycle or unreachable root");
    }

    for (const PrefabEntity& entity : model.entities) {
        if (!ValidateTransform(entity.localTransform, &result.error)) {
            result.ok = false;
            return result;
        }
        std::unordered_set<std::string> types;
        for (const PrefabComponentData& component : entity.components) {
            if (!types.insert(component.type).second)
                return fail("duplicate Prefab component type '" +
                            component.type + "' on entity '" + entity.name + "'");
            if (!ValidatePrefabComponentData(component, &result.error)) {
                result.ok = false;
                result.error = "entity '" + entity.name + "': " + result.error;
                return result;
            }
        }
    }
    return result;
}

PrefabCaptureResult CapturePrefabFromScene(
    const Scene& scene, uint64_t rootSceneEntityId,
    const PrefabCaptureOptions& options) {
    PrefabCaptureResult result;
    auto fail = [&](const std::string& message) {
        result.ok = false;
        result.model = {};
        result.localIds.Clear();
        result.error = message;
        return result;
    };
    if (rootSceneEntityId == 0)
        return fail("Prefab capture root entity id is zero");
    if (options.prefabGuid.IsNull())
        return fail("Prefab capture requires a nonzero prefabGuid");

    const GameObject* root = scene.FindByEntityId(rootSceneEntityId);
    if (!root)
        return fail("Prefab capture root entity not found");

    std::vector<const GameObject*> nodes;
    std::unordered_set<const GameObject*> visited;
    std::function<bool(const GameObject*)> collect = [&](const GameObject* node) {
        if (!node) return false;
        if (!visited.insert(node).second) return false;
        nodes.push_back(node);
        for (const auto& child : node->GetChildren()) {
            if (!collect(child.get())) return false;
        }
        return true;
    };
    if (!collect(root))
        return fail("Prefab capture hierarchy contains a cycle");

    PrefabLocalIdMap localIds;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const uint64_t sceneId = nodes[i]->GetSceneEntityId();
        const uint64_t localId = static_cast<uint64_t>(i) + 1;
        localIds.sceneToLocal[sceneId] = localId;
        localIds.localToScene[localId] = sceneId;
    }

    PrefabModel model;
    model.formatVersion = kPrefabFormatVersion;
    model.prefabGuid = options.prefabGuid;
    model.rootLocalId = 1;
    model.entities.reserve(nodes.size());

    for (const GameObject* node : nodes) {
        const uint64_t sceneId = node->GetSceneEntityId();
        if (sceneId == 0)
            return fail("Prefab capture encountered a zero Scene Entity ID");

        if (options.legacyBindings) {
            const auto binding = options.legacyBindings->find(sceneId);
            if (binding != options.legacyBindings->end() &&
                (!binding->second.spriteGuid.IsNull() ||
                 !binding->second.scriptGuid.IsNull()))
                return fail("Prefab capture rejects legacy Sprite/Script binding "
                            "on entity '" + node->GetName() + "'");
        }

        PrefabEntity entity;
        entity.localId = localIds.sceneToLocal.at(sceneId);
        entity.name = node->GetName();
        if (node == root) {
            entity.parentLocalId = std::nullopt;
        } else if (const GameObject* parent = node->GetParent()) {
            const auto parentIt = localIds.sceneToLocal.find(
                parent->GetSceneEntityId());
            if (parentIt == localIds.sceneToLocal.end())
                return fail("Prefab capture found a parent outside the subtree");
            entity.parentLocalId = parentIt->second;
        } else {
            return fail("Prefab capture found a non-root entity without a parent");
        }

        const TransformComponent& transform = node->GetTransform();
        const Vec3 position = transform.GetPosition();
        const Quat rotation = transform.GetRotationQuat();
        const Vec3 scale = transform.GetScale();
        entity.localTransform.position = position;
        entity.localTransform.rotation = rotation;
        entity.localTransform.scale = scale;

        std::unordered_set<std::string> componentTypes;
        bool componentFailure = false;
        std::string componentError;
        node->ForEachComponent([&](const Component& component) {
            if (componentFailure) return;
            const char* typeName = component.GetComponentTypeName();
            if (!typeName || !IsAllowedComponentType(typeName)) {
                componentFailure = true;
                componentError = "unsupported component on entity '" +
                                 node->GetName() + "'";
                return;
            }
            if (!componentTypes.insert(typeName).second) {
                componentFailure = true;
                componentError = "duplicate component type '" +
                                 std::string(typeName) + "'";
                return;
            }
            PrefabComponentData data;
            data.type = typeName;
            try {
                component.Serialize(data.data);
            } catch (const std::exception& ex) {
                componentFailure = true;
                componentError = "component '" + std::string(typeName) +
                                 "' serialization threw: " + ex.what();
                return;
            }
            std::string validationError;
            if (!ValidatePrefabComponentData(data, &validationError)) {
                componentFailure = true;
                componentError = validationError;
                return;
            }
            entity.components.push_back(std::move(data));
        });
        if (componentFailure)
            return fail(componentError);
        std::sort(entity.components.begin(), entity.components.end(),
                  [](const PrefabComponentData& a,
                     const PrefabComponentData& b) { return a.type < b.type; });
        model.entities.push_back(std::move(entity));
    }

    std::sort(model.entities.begin(), model.entities.end(),
              [](const PrefabEntity& a, const PrefabEntity& b) {
                  return a.localId < b.localId;
              });

    const PrefabValidationResult validation = ValidatePrefabModel(model);
    if (!validation.ok)
        return fail("Prefab capture produced an invalid model: " +
                    validation.error);

    result.ok = true;
    result.model = std::move(model);
    result.localIds = std::move(localIds);
    result.error.clear();
    return result;
}

} // namespace Engine::Content