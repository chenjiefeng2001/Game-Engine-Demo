#pragma once

#include "MeshImportIR.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// glTF 2.0 / GLB 2.0 static-mesh parser (P6-P1A contract scan). Parser layer
// only: produces MeshImportIR + diagnostics; never writes the asset
// registry/RevisionStore. No new third-party dependencies (nlohmann only).
class GltfParser
{
public:
    GltfParser() = delete;

    // GLB container: JSON chunk parsed after ReadGlb validation;
    // buffers[] entry 0 may use the embedded BIN chunk.
    static ImportResult ParseGlb(std::vector<uint8_t> fileBytes,
                                 const std::string& sourceDir);

    // Plain .gltf with external expected (structural) URIs only.
    // data: URIs rejected per contract section 7 (v1).
    static ImportResult ParseGltfJson(std::vector<uint8_t> fileBytes,
                                      const std::string& sourceDir);
};

} // namespace Engine::AssetImport
