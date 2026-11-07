#pragma once

#include "ImportDiagnostic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// P6-P1A contract section 2 Canonical Import IR (parser layer only; no
// RevisionStore / asset-registry writes happen here).
//
// Conventions frozen by the contract and upheld by the parser:
//   - right-handed, +Y up, meters (glTF -> engine identity mapping)
//   - CCW front faces preserved from source
//   - node transforms baked into vertices at import (single conversion point)
//   - UV v flipped (v' = 1 - v) to compensate the engine runtime's
//     stbi_set_flip_vertically_on_load(true) (see C-3 note, B0 map U-3)

struct ImportVertex
{
    float position[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    float uv0[2] = {0, 0};
    float tangent[3] = {0, 0, 0};
    uint8_t flags = 0;
};

inline constexpr uint8_t kVertexFlagNormalValid = 1 << 0;
inline constexpr uint8_t kVertexFlagTangentValid = 1 << 1;
inline constexpr uint8_t kVertexFlagUvValid = 1 << 2;

struct MeshPrimitiveIR
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0xFFFFFFFFu; // unspecified material -> sentinel
    float sourceTransform[16] = {};       // column-major: data[col*4+row]
    std::string nodeName;
};

enum class AlphaModeIR : uint8_t { Opaque = 0, Mask = 1, Blend = 2 };

struct TextureReferenceIR { uint32_t imageIndex = 0xFFFFFFFFu; bool present = false;
    // B4: committed texture asset path filled by the model-import commit flow
    // before material serialization; empty in raw parser output.
    std::string resolvedAssetPath; };

struct MaterialIR
{
    std::string name;
    float baseColorFactor[4] = {1, 1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float emissiveFactor[3] = {0, 0, 0};
    float alphaCutoff = 0.5f;
    AlphaModeIR alphaMode = AlphaModeIR::Opaque;
    TextureReferenceIR baseColor;
    TextureReferenceIR normal;
    TextureReferenceIR occlusion;
    TextureReferenceIR emissive;
    TextureReferenceIR metallicRoughness;
};

struct TextureImageIR
{
    std::string uri;         // non-empty when file-backed
    uint32_t bufferViewIndex = 0xFFFFFFFFu; // valid when embedded in GLB BIN
    std::string mimeType;
    uint32_t sourceImageIndex = 0xFFFFFFFFu; // document images[] position
};

struct MeshImportIR
{
    std::string sourceFile;
    std::vector<ImportVertex> vertices;          // world-space baked, CCW preserved
    std::vector<uint32_t> indices;
    std::vector<MeshPrimitiveIR> primitives;     // source/traversal order preserved
    std::vector<MaterialIR> materials;           // source material slot order preserved
    std::vector<TextureImageIR> images;          // only images referenced by materials
    // B4: parallel to images[] by index; non-empty = GLB bufferView-embedded
    // image bytes (exact bufferView range). Empty = external URI texture.
    std::vector<std::vector<uint8_t>> imageBytes;
    // Dependency files actually READ during parsing (mtllib for OBJ; external
    // buffer URIs for glTF). URI strings as validated; NOT included: texture
    // image files (decode is B4 scope).
    std::vector<std::string> dependencyFiles;
};

struct ImportResult
{
    bool ok = false;
    MeshImportIR mesh;
    std::vector<ImportDiagnostic> diagnostics;
};

} // namespace Engine::AssetImport
