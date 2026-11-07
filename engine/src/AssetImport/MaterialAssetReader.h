#pragma once

#include <cstdint>
#include <string>

namespace Engine::AssetImport {

// P6-1B B5-G3: CPU-only reader for the B3/B4 Material asset JSON (formatVersion 1).
// Texture references carry the B4-resolved `assetPath` (texture asset path);
// the reader does not touch GL or the registry.

struct MaterialTextureRef
{
    bool present = false;
    uint32_t imageIndex = 0xFFFFFFFFu;
    std::string assetPath; // empty when absent; binding requires it when present
};

struct MaterialAsset
{
    uint32_t formatVersion = 0;
    uint32_t slot = 0;
    std::string name;
    float baseColorFactor[4] = {1, 1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float emissiveFactor[3] = {0, 0, 0};
    float alphaCutoff = 0.5f;
    uint8_t alphaMode = 0; // 0=OPAQUE 1=MASK 2=BLEND
    MaterialTextureRef baseColor;
    MaterialTextureRef normal;
    MaterialTextureRef occlusion;
    MaterialTextureRef emissive;
    MaterialTextureRef metallicRoughness;
};

struct MaterialAssetReadResult
{
    bool ok = false;
    std::string error;
    MaterialAsset material;
};

MaterialAssetReadResult ReadMaterialAssetJson(const std::vector<uint8_t>& bytes);

} // namespace Engine::AssetImport
