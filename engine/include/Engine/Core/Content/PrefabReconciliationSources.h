#pragma once

#include "Engine/Core/Content/PrefabAsset.h"
#include "Engine/Core/Content/PrefabOverride.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::Content {

    struct PrefabSourceSnapshot {
        ResourceGUID prefabGuid;
        int64_t projectRevision = 0;
        std::string contentDigest;
        std::string contentPath;
        PrefabModel model;
    };

    struct PrefabLocalComponentSnapshot {
        std::string type;
        nlohmann::json data = nlohmann::json::object();
        bool supported = false;
    };

    struct PrefabLocalEntitySnapshot {
        uint64_t localId = 0;
        uint64_t sceneEntityId = 0;
        std::string name;
        std::optional<uint64_t> parentLocalId;
        PrefabLocalTransform localTransform;
        std::vector<PrefabLocalComponentSnapshot> components;
    };

    struct PrefabLocalInstanceSnapshot {
        uint64_t rootEntityId = 0;
        std::unordered_map<uint64_t, uint64_t> localToScene;
        std::vector<PrefabLocalEntitySnapshot> entities;
        std::vector<PrefabOverrideRecord> overrides;
        PrefabSyncState syncState = PrefabSyncState::Clean;
    };

    struct PrefabReconciliationSources {
        PrefabSourceSnapshot base;
        PrefabSourceSnapshot latest;
        PrefabLocalInstanceSnapshot local;
        bool outOfDate = false;
    };

} // namespace Engine::Content