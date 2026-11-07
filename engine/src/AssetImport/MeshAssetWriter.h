#pragma once

#include "MeshImportIR.h"

#include <cstdint>
#include <string>

namespace Engine::AssetImport {

// Deterministic JSON serialization of the import IR as *asset bytes* (B3
// staging outputs). Reader/deserializer intentionally deferred to B5
// (runtime mesh loading); byte-determinism tests cover the writer.
constexpr uint32_t kMeshAssetFormatVersion = 1;
constexpr uint32_t kMaterialAssetFormatVersion = 1;

std::string SerializeMeshAssetJson(const MeshImportIR& ir);
std::string SerializeMaterialAssetJson(const MaterialIR& material,
                                       uint32_t slot);
} // namespace Engine::AssetImport
