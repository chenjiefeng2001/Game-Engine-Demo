#include "Engine/Core/ECS/ECSBridge.h"

namespace Engine {

ECSBridge::BridgeData& ECSBridge::GetData() {
    static BridgeData data;
    return data;
}

EntityHandle ECSBridge::GetEntityHandle(GameObject* go) {
    auto& data = GetData();
    auto it = data.goToEntity.find(reinterpret_cast<uint64>(go));
    if (it != data.goToEntity.end()) {
        return it->second;
    }
    return kNullEntity;
}

void ECSBridge::SetEntityHandle(GameObject* go, EntityHandle entity) {
    auto& data = GetData();
    data.goToEntity[reinterpret_cast<uint64>(go)] = entity;
}

bool ECSBridge::HasGameObject(EntityHandle entity) {
    auto& data = GetData();
    return data.entityToGO.find(entity.id) != data.entityToGO.end();
}

GameObject* ECSBridge::GetGameObject(EntityHandle entity) {
    auto& data = GetData();
    auto it = data.entityToGO.find(entity.id);
    if (it != data.entityToGO.end()) {
        return it->second;
    }
    return nullptr;
}

void ECSBridge::Link(GameObject* go, EntityHandle entity) {
    auto& data = GetData();
    uint64 goKey = reinterpret_cast<uint64>(go);
    data.goToEntity[goKey] = entity;
    data.entityToGO[entity.id] = go;
}

void ECSBridge::Unlink(GameObject* go) {
    auto& data = GetData();
    uint64 goKey = reinterpret_cast<uint64>(go);
    auto it = data.goToEntity.find(goKey);
    if (it != data.goToEntity.end()) {
        data.entityToGO.erase(it->second.id);
        data.goToEntity.erase(it);
    }
}

} // namespace Engine