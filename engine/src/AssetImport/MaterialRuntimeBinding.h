#pragma once

#include "Engine/Core/RHI/TextureGpuResource.h"
#include "MaterialAssetReader.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Engine {
class IGraphicsFactory;
}

namespace Engine::AssetImport {

// P6-1B B5-G3: runtime binding of committed Material/Texture assets to a mesh.
// CPU resolution is always performed; GPU texture resources are optional
// (requires a TextureGpuCache + factory + current GL context).
struct MaterialBindingTexture
{
    bool present = false;         // reference exists in the material
    bool resolved = false;        // valid asset + decoded + (optionally) GPU
    std::string assetPath;
    std::shared_ptr<const TextureGpuResource> gpu;
    bool srgb = false;
};

struct MaterialBindingEntry
{
    uint32_t slot = 0;
    std::string materialAssetPath;
    MaterialAsset material;
    MaterialBindingTexture baseColor;
    MaterialBindingTexture normal;
    MaterialBindingTexture occlusion;
    MaterialBindingTexture emissive;
    MaterialBindingTexture metallicRoughness;
};

struct MeshMaterialBinding
{
    uint32_t slotCount = 0;
    std::vector<MaterialBindingEntry> slots; // index = slot
    std::vector<std::string> warnings;
    // v1/B0 policy: all geometry is drawn with the first material.
    bool firstMaterialAvailable = false;
};

struct MaterialAssetIo
{
    // Reads a project-relative asset file (texture/PNG or sidecar). Returns
    // false when missing/unreadable.
    std::function<bool(const std::string& assetPath,
                       std::vector<uint8_t>& out)> readBytes;
    // Converts a committed asset path to an OS path usable by the GL factory.
    std::function<std::string(const std::string& assetPath)> toAbsolutePath;
};

struct MaterialBindingBuildOptions
{
    TextureGpuCache* textureCache = nullptr; // optional (CPU-only when null)
    IGraphicsFactory* factory = nullptr;
};

enum class MaterialBindingFailureStage : uint8_t {
    None = 0,
    MaterialAsset,
    TextureAsset,
    GpuUpload,
    InvalidInput,
};

struct MeshMaterialBindingResult
{
    bool ok = false;
    std::string error;
    MaterialBindingFailureStage stage = MaterialBindingFailureStage::None;
    std::shared_ptr<const MeshMaterialBinding> binding;
};

MeshMaterialBindingResult BuildMeshMaterialBinding(
    const std::string& meshAssetPath, const std::vector<std::string>& materialNames,
    const std::vector<std::pair<std::string, std::string>>& registryEntries,
    const MaterialAssetIo& io, const MaterialBindingBuildOptions& options = {});

} // namespace Engine::AssetImport
