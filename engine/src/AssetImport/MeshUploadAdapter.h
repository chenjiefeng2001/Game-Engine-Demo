#pragma once

#include "Engine/Core/RHI/MeshGpuResource.h"
#include "MeshAssetReader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// P6-1B B5-G2: single centralized adapter from the G1 CPU MeshAsset to the
// Renderer-side GPU upload input. No second JSON parser, no reinterpret_cast.
//
// Flags are preserved alongside the vertex stream (never folded into the
// 44-byte Vertex3D layout). No normal/tangent synthesis: numeric values are
// mapped verbatim; missing-attribute intent remains visible via flags.
struct MeshUploadInput
{
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshGpuSpan> spans;
    std::vector<uint8_t> vertexFlags;
    uint64_t payloadDigest = 0;
};

struct MeshUploadInputResult
{
    bool ok = false;
    std::string error;
    MeshUploadInput input;
};

MeshUploadInputResult BuildMeshUploadInput(const MeshAsset& asset);

} // namespace Engine::AssetImport
