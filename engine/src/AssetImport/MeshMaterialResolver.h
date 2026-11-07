#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Engine::AssetImport {

// P6-1B B5-G3: deterministic Material slot -> Material asset path resolution.
//
// Provenance rule (frozen by the B3 writer): a Material asset for source
// `<stemDir>/<base>.<digest8>.mesh`, slot k, is always written to
//   <stemDir>/<base>.m<k>.<contentHash8>.material.json
// Therefore slot lookup uses the committed registry paths (type "Material")
// with an exact prefix+shape match. Material NAMES are never used for lookup.
struct MaterialSlotResolution
{
    bool ok = false;
    std::string error;
    std::vector<std::string> slotPaths; // index = slot
};

// registryEntries: (path, typeToken) pairs; typeToken uses the content asset
// type strings ("Mesh"/"Material"/"Texture"/"Script").
MaterialSlotResolution ResolveMaterialAssetPaths(
    const std::string& meshAssetPath, uint32_t slotCount,
    const std::vector<std::pair<std::string, std::string>>& registryEntries);

} // namespace Engine::AssetImport
