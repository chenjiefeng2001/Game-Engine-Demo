#include "MeshUploadAdapter.h"

namespace Engine::AssetImport {

MeshUploadInputResult BuildMeshUploadInput(const MeshAsset& asset) {
    MeshUploadInputResult result;
    MeshUploadInput& in = result.input;

    in.vertices.resize(asset.vertices.size());
    in.vertexFlags.resize(asset.vertices.size());
    for (size_t i = 0; i < asset.vertices.size(); ++i) {
        const MeshAssetVertex& src = asset.vertices[i];
        Vertex3D& dst = in.vertices[i];
        dst.position = Vec3(src.position[0], src.position[1], src.position[2]);
        dst.normal = Vec3(src.normal[0], src.normal[1], src.normal[2]);
        dst.texCoord = Vec2(src.uv0[0], src.uv0[1]);
        dst.tangent = Vec3(src.tangent[0], src.tangent[1], src.tangent[2]);
        in.vertexFlags[i] = src.flags;
    }
    in.indices = asset.indices;
    in.spans.resize(asset.primitives.size());
    for (size_t i = 0; i < asset.primitives.size(); ++i) {
        in.spans[i].firstIndex = asset.primitives[i].firstIndex;
        in.spans[i].indexCount = asset.primitives[i].indexCount;
        in.spans[i].materialIndex = asset.primitives[i].materialIndex;
    }
    in.payloadDigest = MeshGpuCache::ComputePayloadDigest(
        in.vertices, in.indices, in.spans, in.vertexFlags);

    // Structural checks mirroring the cache contract; GL-independent.
    if (asset.vertices.empty() || asset.indices.empty()) {
        result.ok = false;
        result.error = "MESH_UPLOAD_EMPTY: mesh has no vertices/indices";
        return result;
    }
    if (!MeshGpuCache::ValidatePayload(in.vertices, in.indices, in.spans,
                                       in.vertexFlags)) {
        result.ok = false;
        result.error = "MESH_UPLOAD_INVALID: payload violates GPU limits";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace Engine::AssetImport
