#include "Engine/Core/GameObject/RigidBodyComponent.h"

#include "Engine/Core/GameObject/ComponentRegistry_Go.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Engine {

namespace {
    // 反射属性固定索引（与 GetPropertyCount/Desc/Get/Set 对齐）
    enum : size_t { kPropMotionType = 0, kPropLinearDamping = 1,
                    kPropAngularDamping = 2 };

    bool IsValidDamping(float value) {
        return std::isfinite(value) && value >= 0.0f;
    }

    bool ReadDamping(const nlohmann::json& value, float* out) {
        if (!out || !value.is_number()) return false;
        double raw = 0.0;
        try {
            raw = value.get<double>();
        } catch (const std::exception&) {
            return false;
        }
        if (!std::isfinite(raw) || raw < 0.0 ||
            raw > static_cast<double>(std::numeric_limits<float>::max())) {
            return false;
        }
        *out = static_cast<float>(raw);
        return true;
    }
}

const char* RigidBodyMotionTypeToString(RigidBodyMotionType type) {
    switch (type) {
        case RigidBodyMotionType::Static:    return "Static";
        case RigidBodyMotionType::Kinematic: return "Kinematic";
        case RigidBodyMotionType::Dynamic:   return "Dynamic";
    }
    return nullptr;
}

bool ParseRigidBodyMotionType(const char* name, RigidBodyMotionType* out) {
    if (!name || !out) return false;
    if (std::strcmp(name, "Static") == 0) {
        *out = RigidBodyMotionType::Static;
        return true;
    }
    if (std::strcmp(name, "Kinematic") == 0) {
        *out = RigidBodyMotionType::Kinematic;
        return true;
    }
    if (std::strcmp(name, "Dynamic") == 0) {
        *out = RigidBodyMotionType::Dynamic;
        return true;
    }
    return false;
}

bool RigidBodyComponent::SetMotionType(RigidBodyMotionType type) {
    switch (type) {
        case RigidBodyMotionType::Static:
        case RigidBodyMotionType::Kinematic:
        case RigidBodyMotionType::Dynamic:
            m_MotionType = type;
            NotifyChanged("motionType");
            return true;
    }
    return false;
}

bool RigidBodyComponent::SetLinearDamping(float damping) {
    if (!IsValidDamping(damping)) return false;
    m_LinearDamping = damping;
    NotifyChanged("linearDamping");
    return true;
}

bool RigidBodyComponent::SetAngularDamping(float damping) {
    if (!IsValidDamping(damping)) return false;
    m_AngularDamping = damping;
    NotifyChanged("angularDamping");
    return true;
}

void RigidBodyComponent::NotifyChanged(const char* propName) {
    if (m_MutationHook) m_MutationHook(*this, propName);
}

size_t RigidBodyComponent::GetPropertyCount() const { return 3; }

bool RigidBodyComponent::GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const {
    if (!out) return false;
    switch (index) {
        case kPropMotionType:
            *out = { "motionType", ComponentValueType::String, true };
            return true;
        case kPropLinearDamping:
            *out = { "linearDamping", ComponentValueType::Float, true };
            return true;
        case kPropAngularDamping:
            *out = { "angularDamping", ComponentValueType::Float, true };
            return true;
        default:
            return false;
    }
}

bool RigidBodyComponent::GetPropertyValue(size_t index, ComponentPropertyValue* out) const {
    if (!out) return false;
    switch (index) {
        case kPropMotionType: {
            const char* name = RigidBodyMotionTypeToString(m_MotionType);
            if (!name) return false;
            out->type = ComponentValueType::String;
            out->stringValue = name;
            return true;
        }
        case kPropLinearDamping:
            out->type = ComponentValueType::Float;
            out->floatValue = m_LinearDamping;
            return true;
        case kPropAngularDamping:
            out->type = ComponentValueType::Float;
            out->floatValue = m_AngularDamping;
            return true;
        default:
            return false;
    }
}

bool RigidBodyComponent::SetPropertyValue(size_t index, const ComponentPropertyValue& value) {
    switch (index) {
        case kPropMotionType: {
            if (value.type != ComponentValueType::String) return false;
            RigidBodyMotionType parsed;
            if (!ParseRigidBodyMotionType(value.stringValue.c_str(), &parsed)) return false;
            return SetMotionType(parsed);
        }
        case kPropLinearDamping:
            if (value.type != ComponentValueType::Float) return false;
            return SetLinearDamping(value.floatValue);
        case kPropAngularDamping:
            if (value.type != ComponentValueType::Float) return false;
            return SetAngularDamping(value.floatValue);
        default:
            return false;
    }
}

void RigidBodyComponent::Serialize(nlohmann::json& json) const {
    const char* name = RigidBodyMotionTypeToString(m_MotionType);
    json["motionType"] = name ? name : "Dynamic";
    json["linearDamping"] = m_LinearDamping;
    json["angularDamping"] = m_AngularDamping;
}

bool RigidBodyComponent::Deserialize(const nlohmann::json& json) {
    if (!json.is_object()) return false;

    // 先验证全部已出现的字段，再一次性应用；失败不得产生部分写入。
    RigidBodyMotionType motion = m_MotionType;
    float linear = m_LinearDamping;
    float angular = m_AngularDamping;

    if (json.contains("motionType")) {
        if (!json["motionType"].is_string()) return false;
        const std::string text = json["motionType"].get<std::string>();
        if (!ParseRigidBodyMotionType(text.c_str(), &motion)) return false;
    }
    if (json.contains("linearDamping") &&
        !ReadDamping(json["linearDamping"], &linear)) {
        return false;
    }
    if (json.contains("angularDamping") &&
        !ReadDamping(json["angularDamping"], &angular)) {
        return false;
    }

    m_MotionType = motion;
    m_LinearDamping = linear;
    m_AngularDamping = angular;
    return true;
}

void RegisterRigidBodyComponent() {
    ComponentRegistryGo::Register("RigidBody", []() -> std::shared_ptr<Component> {
        return std::make_shared<RigidBodyComponent>();
    });
}

} // namespace Engine
