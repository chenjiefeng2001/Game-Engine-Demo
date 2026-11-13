#include "Engine/Core/Content/PrefabOverride.h"

#include <cmath>
#include <limits>

namespace Engine::Content {

namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool IsFiniteNumber(const nlohmann::json& value, float* out = nullptr) {
    if (!value.is_number()) return false;
    double parsed = 0.0;
    try {
        parsed = value.get<double>();
    } catch (const std::exception&) {
        return false;
    }
    if (!std::isfinite(parsed) ||
        std::abs(parsed) > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    if (out) *out = static_cast<float>(parsed);
    return true;
}

bool IsCanonicalGuidOrEmpty(const std::string& hex) {
    if (hex.empty()) return true;
    if (hex.size() != 32) return false;
    for (const char c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return hex.find_first_not_of('0') != std::string::npos;
}

} // namespace

bool ResolvePrefabOverrideField(const std::string& component,
                                const std::string& field,
                                PrefabOverrideFieldSpec* out) {
    if (!out) return false;
    auto resolve = [&](PrefabOverrideValueType type) {
        out->type = type;
        return true;
    };
    if (component == "Transform") {
        if (field == "position.x" || field == "position.y" ||
            field == "position.z" || field == "rotation.x" ||
            field == "rotation.y" || field == "rotation.z" ||
            field == "rotation.w" || field == "scale.x" ||
            field == "scale.y" || field == "scale.z")
            return resolve(PrefabOverrideValueType::Float);
        return false;
    }
    if (component == "Mesh") {
        return field == "meshGuid" &&
               resolve(PrefabOverrideValueType::String);
    }
    if (component == "Camera") {
        if (field == "enabled") return resolve(PrefabOverrideValueType::Bool);
        if (field == "zoom") return resolve(PrefabOverrideValueType::Float);
        if (field == "viewport" || field == "bounds")
            return resolve(PrefabOverrideValueType::String);
        return false;
    }
    if (component == "Collider") {
        if (field == "enabled" || field == "isSensor")
            return resolve(PrefabOverrideValueType::Bool);
        if (field == "shape") return resolve(PrefabOverrideValueType::String);
        if (field == "radius" || field == "halfX" || field == "halfY")
            return resolve(PrefabOverrideValueType::Float);
        if (field == "category" || field == "mask")
            return resolve(PrefabOverrideValueType::Int);
        return false;
    }
    if (component == "RigidBody") {
        if (field == "motionType")
            return resolve(PrefabOverrideValueType::String);
        if (field == "linearDamping" || field == "angularDamping")
            return resolve(PrefabOverrideValueType::Float);
        return false;
    }
    // Skinning, Mesh.visible, Material/Texture, and all other fields are
    // intentionally not part of the G6-A allowlist.
    return false;
}

bool ValidatePrefabOverrideValue(const std::string& component,
                                 const std::string& field,
                                 const nlohmann::json& value,
                                 std::string* error) {
    PrefabOverrideFieldSpec spec;
    if (!ResolvePrefabOverrideField(component, field, &spec))
        return Fail(error, "unsupported Prefab override field '" + component +
                              "." + field + "'");
    const std::string where = component + "." + field;
    switch (spec.type) {
    case PrefabOverrideValueType::Bool:
        if (!value.is_boolean())
            return Fail(error, where + " must be boolean");
        return true;
    case PrefabOverrideValueType::Int: {
        if (!value.is_number_integer())
            return Fail(error, where + " must be an integer");
        int64_t parsed = 0;
        try {
            parsed = value.get<int64_t>();
        } catch (const std::exception&) {
            return Fail(error, where + " is out of integer range");
        }
        if (component == "Collider" &&
            (field == "category" || field == "mask") &&
            (parsed < 0 || parsed > 65535))
            return Fail(error, where + " must be in [0, 65535]");
        return true;
    }
    case PrefabOverrideValueType::Float: {
        float parsed = 0.0f;
        if (!IsFiniteNumber(value, &parsed))
            return Fail(error, where + " must be a finite number");
        if (component == "Camera" && field == "zoom" && parsed <= 0.0f)
            return Fail(error, where + " must be greater than zero");
        if (component == "Collider" &&
            (field == "radius" || field == "halfX" || field == "halfY") &&
            parsed < 0.0f)
            return Fail(error, where + " must be non-negative");
        if (component == "RigidBody" &&
            (field == "linearDamping" || field == "angularDamping") &&
            parsed < 0.0f)
            return Fail(error, where + " must be non-negative");
        return true;
    }
    case PrefabOverrideValueType::String: {
        if (!value.is_string())
            return Fail(error, where + " must be a string");
        const std::string text = value.get<std::string>();
        if (component == "Mesh" && field == "meshGuid" &&
            !IsCanonicalGuidOrEmpty(text))
            return Fail(error, where +
                                  " must be empty or a nonzero 32-hex GUID");
        if (component == "Collider" && field == "shape" &&
            text != "Circle" && text != "Box")
            return Fail(error, where + " must be Circle or Box");
        if (component == "RigidBody" && field == "motionType" &&
            text != "Static" && text != "Kinematic" && text != "Dynamic")
            return Fail(error, where +
                                  " must be Static, Kinematic, or Dynamic");
        return true;
    }
    }
    return Fail(error, where + " has unsupported value type");
}

bool ValidatePrefabOverrideRecord(const PrefabOverrideRecord& record,
                                  std::string* error) {
    if (record.localId == 0)
        return Fail(error, "Prefab override localId is zero");
    if (record.component.empty() || record.field.empty())
        return Fail(error, "Prefab override component/field is empty");
    return ValidatePrefabOverrideValue(record.component, record.field,
                                       record.value, error);
}

} // namespace Engine::Content