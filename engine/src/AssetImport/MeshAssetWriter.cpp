#include "MeshAssetWriter.h"

#include <nlohmann/json.hpp>

namespace Engine::AssetImport {

namespace {

using Json = nlohmann::json;

Json Vec3(const float* v) { return Json::array({v[0], v[1], v[2]}); }
Json Vec2(const float* v) { return Json::array({v[0], v[1]}); }
Json Vec4(const float* v) { return Json::array({v[0], v[1], v[2], v[3]}); }
Json Mat16(const float* m) {
    return Json::array({m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8],
                        m[9], m[10], m[11], m[12], m[13], m[14], m[15]});
}
Json TexRef(const TextureReferenceIR& ref) {
    Json j = Json::object({{"present", ref.present},
                           {"imageIndex", ref.present ? static_cast<uint64_t>(ref.imageIndex)
                                                   : 0xFFFFFFFFull}});
    if (!ref.resolvedAssetPath.empty())
        j["assetPath"] = ref.resolvedAssetPath;
    return j;
}

} // namespace

std::string SerializeMeshAssetJson(const MeshImportIR& ir) {
    Json doc;
    doc["format"] = "mesh";
    doc["formatVersion"] = kMeshAssetFormatVersion;
    doc["sourceFile"] = ir.sourceFile;
    Json verts = Json::array();
    for (const ImportVertex& v : ir.vertices) {
        Json jv = Json::object();
        jv["position"] = Vec3(v.position);
        jv["normal"] = Vec3(v.normal);
        jv["uv0"] = Vec2(v.uv0);
        jv["tangent"] = Vec3(v.tangent);
        jv["flags"] = static_cast<uint64_t>(v.flags);
        verts.push_back(jv);
    }
    doc["vertices"] = verts;
    Json indices = Json::array();
    for (const uint32_t i : ir.indices)
        indices.push_back(static_cast<uint64_t>(i));
    doc["indices"] = indices;
    Json prims = Json::array();
    for (const MeshPrimitiveIR& p : ir.primitives) {
        Json jp = Json::object();
        jp["firstIndex"] = static_cast<uint64_t>(p.firstIndex);
        jp["indexCount"] = static_cast<uint64_t>(p.indexCount);
        if (p.materialIndex != 0xFFFFFFFFu) {
            jp["materialIndex"] = static_cast<uint64_t>(p.materialIndex);
        } else {
            jp["materialIndex"] = Json();
        }
        jp["nodeName"] = p.nodeName;
        jp["sourceTransform"] = Mat16(p.sourceTransform);
        prims.push_back(jp);
    }
    doc["primitives"] = prims;
    Json mats = Json::array();
    for (const MaterialIR& m : ir.materials) {
        mats.push_back(m.name);
    }
    doc["materialNames"] = mats;
    Json images = Json::array();
    for (const TextureImageIR& img : ir.images) {
        Json ji = Json::object({{"uri", img.uri},
                                {"mimeType", img.mimeType}});
        images.push_back(ji);
    }
    doc["imageUris"] = images;
    return doc.dump();
}

std::string SerializeMaterialAssetJson(const MaterialIR& material,
                                       uint32_t slot) {
    Json doc;
    doc["format"] = "material";
    doc["formatVersion"] = kMeshAssetFormatVersion;
    doc["slot"] = static_cast<uint64_t>(slot);
    doc["name"] = material.name;
    doc["baseColorFactor"] = Vec4(material.baseColorFactor);
    doc["metallicFactor"] = static_cast<double>(material.metallicFactor);
    doc["roughnessFactor"] = static_cast<double>(material.roughnessFactor);
    doc["emissiveFactor"] = Vec3(material.emissiveFactor);
    doc["alphaCutoff"] = static_cast<double>(material.alphaCutoff);
    const char* am = "OPAQUE";
    if (material.alphaMode == AlphaModeIR::Mask) am = "MASK";
    else if (material.alphaMode == AlphaModeIR::Blend) am = "BLEND";
    doc["alphaMode"] = am;
    doc["baseColorTexture"] = TexRef(material.baseColor);
    doc["normalTexture"] = TexRef(material.normal);
    doc["occlusionTexture"] = TexRef(material.occlusion);
    doc["emissiveTexture"] = TexRef(material.emissive);
    doc["metallicRoughnessTexture"] = TexRef(material.metallicRoughness);
    return doc.dump();
}

} // namespace Engine::AssetImport
