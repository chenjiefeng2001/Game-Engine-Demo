#include "Engine/Core/GameObject/CameraComponent.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"

namespace Engine {

namespace {
    // 反射属性的固定索引（与 GetPropertyCount/GetPropertyDesc/Get/Set 对齐）
    enum : size_t { kPropEnabled = 0, kPropZoom = 1, kPropViewport = 2, kPropBounds = 3 };
}

// ── 反射 / 元数据 ───────────────────────────────────────

bool CameraComponent::GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const {
    if (!out) return false;
    switch (index) {
        case kPropEnabled:  *out = { "enabled",  ComponentValueType::Bool,   true }; return true;
        case kPropZoom:     *out = { "zoom",     ComponentValueType::Float,  true }; return true;
        case kPropViewport: *out = { "viewport", ComponentValueType::String, true }; return true;
        case kPropBounds:   *out = { "bounds",   ComponentValueType::String, true }; return true;
        default: return false;
    }
}

bool CameraComponent::GetPropertyValue(size_t index, ComponentPropertyValue* out) const {
    if (!out) return false;
    switch (index) {
        case kPropEnabled:  out->type = ComponentValueType::Bool;   out->boolValue   = IsEnabled(); return true;
        case kPropZoom:     out->type = ComponentValueType::Float;  out->floatValue  = m_Zoom;      return true;
        case kPropViewport: out->type = ComponentValueType::String; out->stringValue = m_Viewport;  return true;
        case kPropBounds:   out->type = ComponentValueType::String; out->stringValue = m_Bounds;    return true;
        default: return false;
    }
}

bool CameraComponent::SetPropertyValue(size_t index, const ComponentPropertyValue& value) {
    switch (index) {
        case kPropEnabled:
            if (value.type != ComponentValueType::Bool) return false;
            SetEnabled(value.boolValue);
            return true;
        case kPropZoom:
            if (value.type != ComponentValueType::Float) return false;
            m_Zoom = value.floatValue;
            return true;
        case kPropViewport:
            if (value.type != ComponentValueType::String) return false;
            m_Viewport = value.stringValue;
            return true;
        case kPropBounds:
            if (value.type != ComponentValueType::String) return false;
            m_Bounds = value.stringValue;
            return true;
        default:
            return false;
    }
}

// ── 序列化 ───────────────────────────────────────────────

void CameraComponent::Serialize(nlohmann::json& json) const {
    json["enabled"]  = IsEnabled();
    json["zoom"]     = m_Zoom;
    json["viewport"] = m_Viewport;
    json["bounds"]   = m_Bounds;
}

bool CameraComponent::Deserialize(const nlohmann::json& json) {
    if (!json.is_object()) return false;
    if (json.contains("enabled") && json["enabled"].is_boolean())
        SetEnabled(json["enabled"].get<bool>());
    if (json.contains("zoom") && json["zoom"].is_number())
        m_Zoom = json["zoom"].get<float>();
    if (json.contains("viewport") && json["viewport"].is_string())
        m_Viewport = json["viewport"].get<std::string>();
    if (json.contains("bounds") && json["bounds"].is_string())
        m_Bounds = json["bounds"].get<std::string>();
    return true;
}

// ── Runtime ──────────────────────────────────────────────

OrthographicCamera CameraComponent::MakeOrthographic(float aspectRatio) const {
    // 基础视野半高 0.9（镜像 OrthographicCamera 默认 16:9 视野）；zoom 缩放两者。
    const float halfH = 0.9f / m_Zoom;
    const float halfW = (aspectRatio > 0.0f) ? halfH * aspectRatio : halfH;
    return OrthographicCamera(-halfW, halfW, -halfH, halfH);
}

// ── 注册 ─────────────────────────────────────────────────

void RegisterCameraComponent() {
    ComponentRegistryGo::Register("Camera", []() -> std::shared_ptr<Component> {
        return std::make_shared<CameraComponent>();
    });
}

} // namespace Engine