#include "Engine/Core/ECS/ComponentRegistry.h"
#include <cassert>

namespace Engine {

namespace {
    struct ComponentRegistry {
        std::unordered_map<ComponentTypeID, ComponentMeta> metas;
    };

    ComponentRegistry& GetRegistry() {
        static ComponentRegistry reg;
        return reg;
    }
}

ComponentMeta* GetComponentMetaByTypeID(ComponentTypeID typeID) {
    auto& reg = GetRegistry();
    auto it = reg.metas.find(typeID);
    if (it != reg.metas.end()) {
        return &it->second;
    }
    return nullptr;
}

void RegisterMeta(const ComponentMeta& meta) {
    auto& reg = GetRegistry();
    auto it = reg.metas.find(meta.typeID);
    if (it == reg.metas.end()) {
        reg.metas[meta.typeID] = meta;
    }
}

} // namespace Engine