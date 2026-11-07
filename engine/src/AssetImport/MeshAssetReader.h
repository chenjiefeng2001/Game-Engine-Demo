#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// P6-1B B5-G1: CPU-only reader for the Mesh asset JSON emitted by the B3/B4
// writer (`MeshAssetWriter` formatVersion 1). Input is asset bytes; the
// reader never touches GL, caches, components, or the asset registry.
//
// Vertex validity flags are preserved in a dedicated CPU field; they are NOT
// mapped into the 44-byte Vertex3D layout (runtime mapping is a later gate).

struct MeshAssetVertex
{
    float position[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    float uv0[2] = {0, 0};
    float tangent[3] = {0, 0, 0};
    uint8_t flags = 0; // kVertexFlag* bits (MeshImportIR.h); unknown bits rejected
};

struct MeshAssetSpan
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0xFFFFFFFFu; // 0xFFFFFFFF = unspecified
    std::string nodeName;
    float sourceTransform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct MeshAssetImageRef
{
    std::string uri;
    std::string mimeType;
};

struct MeshAsset
{
    uint32_t formatVersion = 0;
    std::string sourceFile;
    std::vector<MeshAssetVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshAssetSpan> primitives;
    std::vector<std::string> materialNames;
    std::vector<MeshAssetImageRef> images;
};

struct MeshAssetReadResult
{
    bool ok = false;
    std::string error; // "<CODE>: detail"
    MeshAsset mesh;    // only meaningful when ok == true
};

// Fail-closed: any parse/type/range/overflow violation returns ok=false with
// a stable error code; no partially usable mesh is returned.
MeshAssetReadResult ReadMeshAssetJson(const std::vector<uint8_t>& bytes);

} // namespace Engine::AssetImport
