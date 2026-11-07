#pragma once

#include "Engine/Core/Content/MeshAssetResolver.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/RHI/MeshGpuResource.h"
#include "MaterialRuntimeBinding.h"

#include <memory>
#include <string>
#include <vector>

namespace Engine {

class IGraphicsFactory;
class MeshRenderer;
class TextureGpuCache;

// P6-1B B5-G4: committed Mesh GUID -> CPU mesh + GPU mesh + Material/Texture
// binding. All-or-nothing: on failure no partial payload is returned.
struct MeshRuntimeLoadResult {
    bool ok = false;
    MeshRuntimeStatus status = MeshRuntimeStatus::NotRequested;
    std::string error;
    ResourceGUID guid = ResourceGUID::Null;
    int64_t revision = 0;
    std::string assetPath;
    std::string absolutePath;
    std::shared_ptr<Mesh> mesh;
    MeshGpuResourceKey key;
    std::shared_ptr<const AssetImport::MeshMaterialBinding> binding;
    std::vector<std::string> warnings;
};

MeshRuntimeLoadResult LoadMeshRuntimeAsset(
    const ResourceGUID& guid, Engine::Content::MeshAssetResolver& resolver,
    MeshRenderer& renderer, TextureGpuCache& textureCache,
    IGraphicsFactory& factory);

} // namespace Engine
