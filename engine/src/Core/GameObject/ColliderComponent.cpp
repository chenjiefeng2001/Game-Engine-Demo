#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"

#include <cstring>

namespace Engine {

const char* ColliderShapeToString(ColliderShape shape) {
    switch (shape) {
        case ColliderShape::Circle: return "Circle";
        case ColliderShape::Box:    return "Box";
    }
    return nullptr;
}

bool ParseColliderShape(const char* name, ColliderShape* out) {
    if (!name || !out) return false;
    if (std::strcmp(name, "Circle") == 0) { *out = ColliderShape::Circle; return true; }
    if (std::strcmp(name, "Box")    == 0) { *out = ColliderShape::Box;    return true; }
    return false;
}

namespace {
    // 反射属性固定索引（与 GetPropertyCount/Desc/Get/Set 对齐）
    enum : size_t { kPropEnabled=0, kPropShape=1, kPropRadius=2,
                    kPropHalfX=3, kPropHalfY=4, kPropIsSensor=5,
                    kPropCategory=6, kPropMask=7 };
}

// ── 反射 / 元数据 ───────────────────────────────────────

size_t ColliderComponent::GetPropertyCount() const { return 8; }

bool ColliderComponent::GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const {
    if (!out) return false;
    switch (index) {
        case kPropEnabled:  *out = { "enabled",  ComponentValueType::Bool,   true }; return true;
        case kPropShape:    *out = { "shape",    ComponentValueType::String, true }; return true;
        case kPropRadius:   *out = { "radius",   ComponentValueType::Float,  true }; return true;
        case kPropHalfX:    *out = { "halfX",    ComponentValueType::Float,  true }; return true;
        case kPropHalfY:    *out = { "halfY",    ComponentValueType::Float,  true }; return true;
        case kPropIsSensor: *out = { "isSensor", ComponentValueType::Bool,   true }; return true;
        case kPropCategory: *out = { "category", ComponentValueType::Int,    true }; return true;
        case kPropMask:     *out = { "mask",     ComponentValueType::Int,    true }; return true;
        default: return false;
    }
}

bool ColliderComponent::GetPropertyValue(size_t index, ComponentPropertyValue* out) const {
    if (!out) return false;
    const char* shapeName = ColliderShapeToString(m_Shape);
    switch (index) {
        case kPropEnabled:  out->type = ComponentValueType::Bool;   out->boolValue  = IsEnabled(); return true;
        case kPropShape:    out->type = ComponentValueType::String; out->stringValue= shapeName ? shapeName : ""; return true;
        case kPropRadius:   out->type = ComponentValueType::Float;  out->floatValue = m_Radius;  return true;
        case kPropHalfX:    out->type = ComponentValueType::Float;  out->floatValue = m_HalfX;   return true;
        case kPropHalfY:    out->type = ComponentValueType::Float;  out->floatValue = m_HalfY;   return true;
        case kPropIsSensor: out->type = ComponentValueType::Bool;   out->boolValue  = m_IsSensor; return true;
        case kPropCategory: out->type = ComponentValueType::Int;    out->intValue   = m_Category; return true;
        case kPropMask:     out->type = ComponentValueType::Int;    out->intValue   = m_Mask;     return true;
        default: return false;
    }
}

bool ColliderComponent::SetPropertyValue(size_t index, const ComponentPropertyValue& value) {
    switch (index) {
        case kPropEnabled:
            if (value.type != ComponentValueType::Bool) return false;
            SetEnabled(value.boolValue);
            NotifyChanged("enabled");
            return true;
        case kPropShape: {
            if (value.type != ComponentValueType::String) return false;
            ColliderShape s;
            if (!ParseColliderShape(value.stringValue.c_str(), &s)) return false;
            m_Shape = s;
            NotifyChanged("shape");
            return true;
        }
        case kPropRadius:
            if (value.type != ComponentValueType::Float) return false;
            m_Radius = value.floatValue;
            NotifyChanged("radius");
            return true;
        case kPropHalfX:
            if (value.type != ComponentValueType::Float) return false;
            m_HalfX = value.floatValue;
            NotifyChanged("halfX");
            return true;
        case kPropHalfY:
            if (value.type != ComponentValueType::Float) return false;
            m_HalfY = value.floatValue;
            NotifyChanged("halfY");
            return true;
        case kPropIsSensor:
            if (value.type != ComponentValueType::Bool) return false;
            m_IsSensor = value.boolValue;
            NotifyChanged("isSensor");
            return true;
        case kPropCategory:
            if (value.type != ComponentValueType::Int) return false;
            m_Category = static_cast<uint16>(value.intValue);
            NotifyChanged("category");
            return true;
        case kPropMask:
            if (value.type != ComponentValueType::Int) return false;
            m_Mask = static_cast<uint16>(value.intValue);
            NotifyChanged("mask");
            return true;
        default:
            return false;
    }
}

// ── 运行时变更通知（F2-B4）────────────────────────────────

void ColliderComponent::NotifyChanged(const char* propName) {
    if (m_MutationHook) m_MutationHook(*this, propName);
}

// ── 序列化 ───────────────────────────────────────────────

void ColliderComponent::Serialize(nlohmann::json& json) const {
    const char* shapeName = ColliderShapeToString(m_Shape);
    json["enabled"]   = IsEnabled();
    json["shape"]     = shapeName ? shapeName : "Circle";
    json["radius"]    = m_Radius;
    json["halfX"]     = m_HalfX;
    json["halfY"]     = m_HalfY;
    json["isSensor"]  = m_IsSensor;
    json["category"]  = m_Category;
    json["mask"]      = m_Mask;
}

bool ColliderComponent::Deserialize(const nlohmann::json& json) {
    if (!json.is_object()) return false;
    if (json.contains("enabled") && json["enabled"].is_boolean())
        SetEnabled(json["enabled"].get<bool>());
    if (json.contains("shape") && json["shape"].is_string()) {
        ColliderShape s;
        if (ParseColliderShape(json["shape"].get<std::string>().c_str(), &s))
            m_Shape = s;
    }
    if (json.contains("radius") && json["radius"].is_number())
        m_Radius = json["radius"].get<float>();
    if (json.contains("halfX") && json["halfX"].is_number())
        m_HalfX = json["halfX"].get<float>();
    if (json.contains("halfY") && json["halfY"].is_number())
        m_HalfY = json["halfY"].get<float>();
    if (json.contains("isSensor") && json["isSensor"].is_boolean())
        m_IsSensor = json["isSensor"].get<bool>();
    if (json.contains("category") && json["category"].is_number())
        m_Category = static_cast<uint16>(json["category"].get<int>());
    if (json.contains("mask") && json["mask"].is_number())
        m_Mask = static_cast<uint16>(json["mask"].get<int>());
    return true;
}

// ── 注册 ─────────────────────────────────────────────────

void RegisterColliderComponent() {
    ComponentRegistryGo::Register("Collider", []() -> std::shared_ptr<Component> {
        return std::make_shared<ColliderComponent>();
    });
}

} // namespace Engine