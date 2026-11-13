#pragma once

/**
 * @file PrefabAsset.h
 * @brief Committed Prefab Asset model and Scene subtree Capture (G4-1).
 *
 * This is the native value model only. Prefab JSON Reader/Writer, AssetType
 * registration, RevisionStore Publish, ABI, and editor UI are later gates.
 */

#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/RHI/MathTypes.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
    class Scene;
}

namespace Engine::Content {

    inline constexpr int32_t kPrefabFormatVersion = 1;

    struct PrefabLocalTransform {
        Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
        Quat rotation = Quat::Identity();
        Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);
    };

    struct PrefabComponentData {
        std::string type;
        nlohmann::json data = nlohmann::json::object();
    };

    struct PrefabEntity {
        uint64_t localId = 0;
        std::string name;
        /// null for the Prefab root; otherwise a nonzero Local ID.
        std::optional<uint64_t> parentLocalId;
        PrefabLocalTransform localTransform;
        std::vector<PrefabComponentData> components;
    };

    struct PrefabModel {
        int32_t formatVersion = kPrefabFormatVersion;
        ResourceGUID prefabGuid;
        uint64_t rootLocalId = 0;
        std::vector<PrefabEntity> entities;
    };

    /// Optional legacy content binding lookup used by Capture to fail closed.
    struct PrefabLegacyBinding {
        ResourceGUID spriteGuid;
        ResourceGUID scriptGuid;
    };

    struct PrefabCaptureOptions {
        ResourceGUID prefabGuid;
        /// Scene Entity ID -> legacy sprite/script binding. Any non-null binding
        /// on a captured entity rejects the Capture.
        const std::unordered_map<uint64_t, PrefabLegacyBinding>* legacyBindings = nullptr;
    };

    struct PrefabLocalIdMap {
        std::unordered_map<uint64_t, uint64_t> sceneToLocal;
        std::unordered_map<uint64_t, uint64_t> localToScene;

        void Clear() {
            sceneToLocal.clear();
            localToScene.clear();
        }
    };

    struct PrefabValidationResult {
        bool ok = true;
        std::string error;
    };

    struct PrefabCaptureResult {
        bool ok = false;
        PrefabModel model;
        PrefabLocalIdMap localIds;
        std::string error;
    };

    /// Strict G4-A component payload validation. Shared by Capture and G4-2.
    bool ValidatePrefabComponentData(const PrefabComponentData& component,
                                     std::string* error);

    /// Validate a complete model against the frozen G4-A invariants.
    PrefabValidationResult ValidatePrefabModel(const PrefabModel& model);

    /// Capture one Scene subtree under a fresh Prefab identity. Local IDs are
    /// assigned deterministically 1..N by DFS preorder. Failure returns an
    /// empty model and an empty Local ID map.
    PrefabCaptureResult CapturePrefabFromScene(
        const Scene& scene, uint64_t rootSceneEntityId,
        const PrefabCaptureOptions& options);

} // namespace Engine::Content