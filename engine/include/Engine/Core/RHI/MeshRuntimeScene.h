#pragma once

#include "Engine/Core/Content/MeshAssetResolver.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/RHI/MeshGpuResource.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {

class IGraphicsFactory;
class MeshRenderer;
class Scene;
class TextureGpuCache;
namespace AssetImport { struct MeshMaterialBinding; }

// P6-1B B5-G4: runtime scene<->asset binding. Owns the GUID->runtime payload
// map for one open project and reconciles it against the live Scene. Runs only
// on the GL owner thread (GPU uploads/invalidations happen here).
class MeshRuntimeScene {
public:
    struct Entry {
        ResourceGUID guid = ResourceGUID::Null;
        int64_t revision = 0;
        std::string assetPath;
        std::shared_ptr<Mesh> mesh;
        MeshGpuResourceKey key;
        std::shared_ptr<const AssetImport::MeshMaterialBinding> binding;
        uint32_t spanCount = 0;
        uint32_t materialSlotCount = 0;
        std::string warnings;
    };

    struct Failure {
        MeshRuntimeStatus status = MeshRuntimeStatus::NotRequested;
        std::string error;
        int64_t revision = -1;
    };

    struct SyncStats {
        uint32_t loaded = 0;
        uint32_t reused = 0;
        uint32_t failed = 0;
        uint32_t removed = 0;
        uint32_t components = 0;
    };

    MeshRuntimeScene() = default;

    void SetDependencies(Content::MeshAssetResolver* resolver,
                         MeshRenderer* renderer, TextureGpuCache* textureCache,
                         IGraphicsFactory* factory);
    bool HasDependencies() const;

    SyncStats Sync(Scene& scene);

    // Releases renderer-side bindings/GPU resources owned by this runtime map.
    // Requires a current GL context on the renderer cache owner thread.
    void Unload();

    const Entry* FindForTesting(const ResourceGUID& guid) const;
    const Failure* FindFailureForTesting(const ResourceGUID& guid) const;
    size_t TrackedCount() const { return m_Entries.size(); }

private:
    Content::MeshAssetResolver* m_Resolver = nullptr;
    MeshRenderer* m_Renderer = nullptr;
    TextureGpuCache* m_TextureCache = nullptr;
    IGraphicsFactory* m_Factory = nullptr;
    std::unordered_map<std::string, Entry> m_Entries;
    std::unordered_map<std::string, Failure> m_Failures;
};

} // namespace Engine
