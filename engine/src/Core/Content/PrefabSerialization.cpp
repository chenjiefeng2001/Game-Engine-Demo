#include "Engine/Core/Content/PrefabSerialization.h"

#include "Engine/Core/Content/RevisionStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Engine::Content {

namespace {

using Json = nlohmann::json;

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
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

bool ReadUInt64Positive(const Json& value, uint64_t* out) {
    if (!out) return false;
    if (value.is_number_unsigned()) {
        uint64_t parsed = 0;
        try {
            parsed = value.get<uint64_t>();
        } catch (const std::exception&) {
            return false;
        }
        if (parsed == 0) return false;
        *out = parsed;
        return true;
    }
    if (value.is_number_integer()) {
        int64_t parsed = 0;
        try {
            parsed = value.get<int64_t>();
        } catch (const std::exception&) {
            return false;
        }
        if (parsed <= 0) return false;
        *out = static_cast<uint64_t>(parsed);
        return true;
    }
    return false;
}

bool ReadFiniteFloat(const Json& value, float* out) {
    if (!out || !value.is_number()) return false;
    double parsed = 0.0;
    try {
        parsed = value.get<double>();
    } catch (const std::exception&) {
        return false;
    }
    if (!std::isfinite(parsed) ||
        std::abs(parsed) > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    *out = static_cast<float>(parsed);
    return true;
}

bool ReadTransformArray(const Json& value, size_t count, float* out) {
    if (!value.is_array() || value.size() != count) return false;
    for (size_t i = 0; i < count; ++i)
        if (!ReadFiniteFloat(value[i], &out[i])) return false;
    return true;
}

bool ReadCanonicalGuid(const Json& value, ResourceGUID* out) {
    if (!value.is_string()) return false;
    const std::string hex = value.get<std::string>();
    if (hex.size() != 32) return false;
    for (const char c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    const ResourceGUID parsed = ResourceGUID::FromHex(hex);
    if (parsed.IsNull()) return false;
    if (out) *out = parsed;
    return true;
}

bool ReadFormatVersion(const Json& value, int32_t* out) {
    if (!out) return false;
    if (value.is_number_unsigned()) {
        uint64_t parsed = 0;
        try {
            parsed = value.get<uint64_t>();
        } catch (const std::exception&) {
            return false;
        }
        if (parsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
            return false;
        *out = static_cast<int32_t>(parsed);
        return true;
    }
    if (value.is_number_integer()) {
        int64_t parsed = 0;
        try {
            parsed = value.get<int64_t>();
        } catch (const std::exception&) {
            return false;
        }
        if (parsed < std::numeric_limits<int32_t>::min() ||
            parsed > std::numeric_limits<int32_t>::max())
            return false;
        *out = static_cast<int32_t>(parsed);
        return true;
    }
    return false;
}

bool ParseStrictJson(std::string_view bytes, Json* out, std::string* error) {
    if (!out) return Fail(error, "Prefab JSON output is null");
    std::vector<std::unordered_set<std::string>> keyStack;
    bool duplicateKey = false;
    std::string callbackError;
    Json parsed;
    try {
        parsed = Json::parse(
            bytes.begin(), bytes.end(),
            [&](int, Json::parse_event_t event, Json& value) {
                if (event == Json::parse_event_t::object_start) {
                    keyStack.emplace_back();
                } else if (event == Json::parse_event_t::key) {
                    if (keyStack.empty()) {
                        callbackError = "JSON object key outside object";
                        return false;
                    }
                    try {
                        if (!keyStack.back().insert(value.get<std::string>())
                                 .second) {
                            duplicateKey = true;
                            return false;
                        }
                    } catch (const std::exception&) {
                        callbackError = "JSON object key is not a string";
                        return false;
                    }
                } else if (event == Json::parse_event_t::object_end) {
                    if (!keyStack.empty()) keyStack.pop_back();
                }
                return true;
            },
            false);
    } catch (const std::exception& ex) {
        return Fail(error, std::string("Prefab JSON parse failed: ") + ex.what());
    }
    if (!callbackError.empty()) return Fail(error, callbackError);
    if (duplicateKey) return Fail(error, "Prefab JSON has duplicate object key");
    if (parsed.is_discarded())
        return Fail(error, "Prefab JSON is invalid, truncated, or has trailing data");
    *out = std::move(parsed);
    return true;
}

bool ReadTransform(const Json& value, const char* context,
                   PrefabLocalTransform* out, std::string* error) {
    if (!CheckKeys(value, {"position", "rotation", "scale"},
                   {"position", "rotation", "scale"}, context, error))
        return false;
    float position[3] = {};
    float rotation[4] = {};
    float scale[3] = {};
    if (!ReadTransformArray(value["position"], 3, position))
        return Fail(error, std::string(context) +
                              ".position must be exactly 3 finite numbers");
    if (!ReadTransformArray(value["rotation"], 4, rotation))
        return Fail(error, std::string(context) +
                              ".rotation must be exactly 4 finite numbers");
    if (!ReadTransformArray(value["scale"], 3, scale))
        return Fail(error, std::string(context) +
                              ".scale must be exactly 3 finite numbers");
    out->position = Vec3(position[0], position[1], position[2]);
    out->rotation = Quat(rotation[0], rotation[1], rotation[2], rotation[3]);
    out->scale = Vec3(scale[0], scale[1], scale[2]);
    return true;
}

} // namespace

std::string ComputePrefabDigest(std::string_view bytes) {
    return RevisionStore::HashBytes(bytes);
}

PrefabWriteResult WritePrefab(const PrefabModel& model) {
    PrefabWriteResult result;
    const PrefabValidationResult validation = ValidatePrefabModel(model);
    if (!validation.ok) {
        result.error = validation.error;
        return result;
    }

    PrefabModel canonical = model;
    std::sort(canonical.entities.begin(), canonical.entities.end(),
              [](const PrefabEntity& a, const PrefabEntity& b) {
                  return a.localId < b.localId;
              });
    for (PrefabEntity& entity : canonical.entities) {
        std::sort(entity.components.begin(), entity.components.end(),
                  [](const PrefabComponentData& a,
                     const PrefabComponentData& b) { return a.type < b.type; });
    }

    Json root = Json::object();
    root["format"] = "engine.prefab";
    root["formatVersion"] = canonical.formatVersion;
    root["prefabGuid"] = canonical.prefabGuid.ToHex();
    root["rootLocalId"] = canonical.rootLocalId;
    Json entities = Json::array();
    for (const PrefabEntity& entity : canonical.entities) {
        Json entityJson = Json::object();
        entityJson["localId"] = entity.localId;
        entityJson["name"] = entity.name;
        if (entity.parentLocalId.has_value())
            entityJson["parentLocalId"] = *entity.parentLocalId;
        else
            entityJson["parentLocalId"] = nullptr;
        Json transform = Json::object();
        transform["position"] = {entity.localTransform.position.x,
                                 entity.localTransform.position.y,
                                 entity.localTransform.position.z};
        transform["rotation"] = {entity.localTransform.rotation.x,
                                 entity.localTransform.rotation.y,
                                 entity.localTransform.rotation.z,
                                 entity.localTransform.rotation.w};
        transform["scale"] = {entity.localTransform.scale.x,
                              entity.localTransform.scale.y,
                              entity.localTransform.scale.z};
        entityJson["localTransform"] = std::move(transform);
        Json components = Json::array();
        for (const PrefabComponentData& component : entity.components) {
            Json componentJson = Json::object();
            componentJson["type"] = component.type;
            componentJson["data"] = component.data;
            components.push_back(std::move(componentJson));
        }
        entityJson["components"] = std::move(components);
        entities.push_back(std::move(entityJson));
    }
    root["entities"] = std::move(entities);
    result.bytes = root.dump(2);
    result.digest = ComputePrefabDigest(result.bytes);
    result.ok = true;
    return result;
}

PrefabReadResult ReadPrefab(std::string_view bytes,
                            const ResourceGUID* expectedGuid) {
    PrefabReadResult result;
    auto fail = [&](const std::string& message) {
        result.ok = false;
        result.model = {};
        result.error = message;
        return result;
    };

    Json doc;
    std::string parseError;
    if (!ParseStrictJson(bytes, &doc, &parseError)) return fail(parseError);
    if (!doc.is_object()) return fail("Prefab root must be an object");
    if (!CheckKeys(doc, {"format", "formatVersion", "prefabGuid",
                         "rootLocalId", "entities"},
                   {"format", "formatVersion", "prefabGuid", "rootLocalId",
                    "entities"},
                   "Prefab root", &result.error))
        return fail(result.error);
    if (!doc["format"].is_string() ||
        doc["format"].get<std::string>() != "engine.prefab")
        return fail("Prefab format must be \"engine.prefab\"");
    int32_t version = 0;
    if (!ReadFormatVersion(doc["formatVersion"], &version) ||
        version != kPrefabFormatVersion)
        return fail("unsupported Prefab formatVersion");
    ResourceGUID prefabGuid;
    if (!ReadCanonicalGuid(doc["prefabGuid"], &prefabGuid))
        return fail("Prefab prefabGuid must be a nonzero canonical 32-hex GUID");
    if (expectedGuid && prefabGuid != *expectedGuid)
        return fail("Prefab prefabGuid does not match the expected asset GUID");
    uint64_t rootLocalId = 0;
    if (!ReadUInt64Positive(doc["rootLocalId"], &rootLocalId))
        return fail("Prefab rootLocalId must be a nonzero uint64");
    if (!doc["entities"].is_array())
        return fail("Prefab entities must be an array");
    if (doc["entities"].empty())
        return fail("Prefab entities must not be empty");

    PrefabModel model;
    model.formatVersion = version;
    model.prefabGuid = prefabGuid;
    model.rootLocalId = rootLocalId;
    model.entities.reserve(doc["entities"].size());
    for (size_t i = 0; i < doc["entities"].size(); ++i) {
        const Json& entityJson = doc["entities"][i];
        const std::string where = "Prefab entities[" + std::to_string(i) + "]";
        if (!CheckKeys(entityJson, {"localId", "name", "parentLocalId",
                                    "localTransform", "components"},
                       {"localId", "name", "parentLocalId", "localTransform",
                        "components"},
                       where.c_str(), &result.error))
            return fail(result.error);
        PrefabEntity entity;
        if (!ReadUInt64Positive(entityJson["localId"], &entity.localId))
            return fail(where + ".localId must be a nonzero uint64");
        if (!entityJson["name"].is_string())
            return fail(where + ".name must be a string");
        entity.name = entityJson["name"].get<std::string>();
        if (entityJson["parentLocalId"].is_null()) {
            entity.parentLocalId = std::nullopt;
        } else {
            uint64_t parent = 0;
            if (!ReadUInt64Positive(entityJson["parentLocalId"], &parent))
                return fail(where +
                            ".parentLocalId must be null or a nonzero uint64");
            entity.parentLocalId = parent;
        }
        if (!ReadTransform(entityJson["localTransform"],
                           (where + ".localTransform").c_str(),
                           &entity.localTransform, &result.error))
            return fail(result.error);
        if (!entityJson["components"].is_array())
            return fail(where + ".components must be an array");
        std::unordered_set<std::string> componentTypes;
        for (size_t j = 0; j < entityJson["components"].size(); ++j) {
            const Json& componentJson = entityJson["components"][j];
            const std::string componentWhere = where + ".components[" +
                                                std::to_string(j) + "]";
            if (!CheckKeys(componentJson, {"type", "data"}, {"type", "data"},
                           componentWhere.c_str(), &result.error))
                return fail(result.error);
            if (!componentJson["type"].is_string())
                return fail(componentWhere + ".type must be a string");
            PrefabComponentData component;
            component.type = componentJson["type"].get<std::string>();
            if (!componentTypes.insert(component.type).second)
                return fail(where + " has duplicate component type '" +
                            component.type + "'");
            if (!componentJson["data"].is_object())
                return fail(componentWhere + ".data must be an object");
            component.data = componentJson["data"];
            if (!ValidatePrefabComponentData(component, &result.error))
                return fail(result.error);
            entity.components.push_back(std::move(component));
        }
        model.entities.push_back(std::move(entity));
    }

    const PrefabValidationResult validation = ValidatePrefabModel(model);
    if (!validation.ok) return fail(validation.error);
    result.ok = true;
    result.model = std::move(model);
    result.error.clear();
    return result;
}

bool PrefabModelsEqual(const PrefabModel& a, const PrefabModel& b) {
    if (a.formatVersion != b.formatVersion || a.prefabGuid != b.prefabGuid ||
        a.rootLocalId != b.rootLocalId ||
        a.entities.size() != b.entities.size())
        return false;
    for (size_t i = 0; i < a.entities.size(); ++i) {
        const PrefabEntity& ea = a.entities[i];
        const PrefabEntity& eb = b.entities[i];
        if (ea.localId != eb.localId || ea.name != eb.name ||
            ea.parentLocalId != eb.parentLocalId ||
            ea.localTransform.position.x != eb.localTransform.position.x ||
            ea.localTransform.position.y != eb.localTransform.position.y ||
            ea.localTransform.position.z != eb.localTransform.position.z ||
            ea.localTransform.rotation.x != eb.localTransform.rotation.x ||
            ea.localTransform.rotation.y != eb.localTransform.rotation.y ||
            ea.localTransform.rotation.z != eb.localTransform.rotation.z ||
            ea.localTransform.rotation.w != eb.localTransform.rotation.w ||
            ea.localTransform.scale.x != eb.localTransform.scale.x ||
            ea.localTransform.scale.y != eb.localTransform.scale.y ||
            ea.localTransform.scale.z != eb.localTransform.scale.z ||
            ea.components.size() != eb.components.size())
            return false;
        for (size_t j = 0; j < ea.components.size(); ++j) {
            if (ea.components[j].type != eb.components[j].type ||
                ea.components[j].data != eb.components[j].data)
                return false;
        }
    }
    return true;
}

} // namespace Engine::Content