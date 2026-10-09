#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/CameraComponent.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Animation/SkinningComponent.h"
#include "Engine/Core/Log.h"

#include <unordered_map>

namespace Engine {
namespace ComponentRegistryGo {

namespace {
    using Map = std::unordered_map<std::string, Factory>;

    Map& Registry() {
        static Map s_Registry;
        return s_Registry;
    }
    bool& EnsureFlag() {
        static bool s_Ensured = false;
        return s_Ensured;
    }
    Logger s_Log("ComponentRegistryGo");
}

void EnsureRegistered() {
    if (EnsureFlag()) return;
    EnsureFlag() = true;
    // 内置契约组件在此收编（F1：Camera；F2：Collider；Animation：Skinning）
    RegisterCameraComponent();
    RegisterColliderComponent();
    SkinningComponent::Register();
}

void Register(const std::string& typeName, Factory factory) {
    if (typeName.empty() || typeName == "Component") {
        s_Log.Warn("refusing to register invalid contract type name '{}'",
                   typeName.empty() ? "(empty)" : typeName);
        return;
    }
    auto& reg = Registry();
    auto it = reg.find(typeName);
    if (it != reg.end() && it->second != nullptr) {
        s_Log.Warn("duplicate registration for contract type '{}'", typeName);
    }
    reg[typeName] = std::move(factory);
}

bool IsRegistered(const std::string& typeName) {
    EnsureRegistered();
    auto& reg = Registry();
    auto it = reg.find(typeName);
    return it != reg.end() && it->second != nullptr;
}

std::shared_ptr<Component> Create(const std::string& typeName) {
    EnsureRegistered();
    auto& reg = Registry();
    auto it = reg.find(typeName);
    if (it == reg.end() || !it->second) return nullptr;
    return it->second();
}

size_t Count() {
    EnsureRegistered();
    return Registry().size();
}

} // namespace ComponentRegistryGo
} // namespace Engine