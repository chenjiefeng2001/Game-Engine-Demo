#include "MaterialAssetReader.h"

#include "MeshAssetWriter.h"
#include "ImportDiagnostic.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>

namespace Engine::AssetImport {

namespace {

using Json = nlohmann::json;

MaterialAssetReadResult Fail(const char* code, const std::string& detail) {
    MaterialAssetReadResult r;
    r.ok = false;
    r.error = std::string(code) + ": " + detail;
    return r;
}

bool ReadU32(const Json& j, uint32_t* out) {
    if (j.is_number_unsigned()) {
        const auto v = j.get<uint64_t>();
        if (v > 0xFFFFFFFFull) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    }
    if (j.is_number_integer()) {
        const auto v = j.get<int64_t>();
        if (v < 0) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    }
    return false;
}

bool ReadFloat(const Json& j, float* out) {
    if (!j.is_number()) return false;
    const double v = j.get<double>();
    if (!std::isfinite(v)) return false;
    const float f = static_cast<float>(v);
    if (!std::isfinite(f)) return false;
    *out = f;
    return true;
}

bool ReadFloatArray(const Json& j, float* out, size_t count) {
    if (!j.is_array() || j.size() != count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!ReadFloat(j[i], &out[i])) return false;
    }
    return true;
}

bool ReadTextureRef(const Json& j, MaterialTextureRef* out,
                    const char* where) {
    if (!j.is_object()) return false;
    static const std::set<std::string> keys = {"present", "imageIndex",
                                               "assetPath"};
    for (const auto& item : j.items()) {
        if (keys.find(item.key()) == keys.end()) return false;
    }
    bool present = false;
    if (!j.contains("present") || !j["present"].is_boolean()) return false;
    present = j["present"].get<bool>();
    out->present = present;
    if (!present) return true;
    if (!j.contains("imageIndex") || !ReadU32(j["imageIndex"], &out->imageIndex))
        return false;
    if (j.contains("assetPath")) {
        if (!j["assetPath"].is_string()) return false;
        out->assetPath = j["assetPath"].get<std::string>();
    }
    return true;
}

} // namespace

MaterialAssetReadResult ReadMaterialAssetJson(
    const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return Fail(kCodeMaterialAssetParse, "empty bytes");
    Json doc;
    try {
        doc = Json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& e) {
        return Fail(kCodeMaterialAssetParse,
                    std::string("JSON parse failed: ") + e.what());
    }
    if (!doc.is_object()) return Fail(kCodeMaterialAssetParse, "root not object");

    static const std::set<std::string> rootKeys = {
        "format", "formatVersion", "slot", "name", "baseColorFactor",
        "metallicFactor", "roughnessFactor", "emissiveFactor", "alphaCutoff",
        "alphaMode", "baseColorTexture", "normalTexture", "occlusionTexture",
        "emissiveTexture", "metallicRoughnessTexture"};
    for (const auto& item : doc.items()) {
        if (rootKeys.find(item.key()) == rootKeys.end())
            return Fail(kCodeMaterialAssetParse,
                        "unknown top-level field '" + item.key() + "'");
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "material")
        return Fail(kCodeMaterialAssetParse, "format is not \"material\"");
    MaterialAsset out;
    if (!doc.contains("formatVersion") ||
        !ReadU32(doc["formatVersion"], &out.formatVersion))
        return Fail(kCodeMaterialAssetUnsupportedVersion,
                    "formatVersion missing/invalid");
    if (out.formatVersion != kMaterialAssetFormatVersion)
        return Fail(kCodeMaterialAssetUnsupportedVersion,
                    "unsupported formatVersion " +
                        std::to_string(out.formatVersion));
    if (!doc.contains("slot") || !ReadU32(doc["slot"], &out.slot))
        return Fail(kCodeMaterialAssetInvalid, "slot invalid");
    if (!doc.contains("name") || !doc["name"].is_string())
        return Fail(kCodeMaterialAssetInvalid, "name invalid");
    out.name = doc["name"].get<std::string>();
    if (!doc.contains("baseColorFactor") ||
        !ReadFloatArray(doc["baseColorFactor"], out.baseColorFactor, 4))
        return Fail(kCodeMaterialAssetInvalid, "baseColorFactor invalid");
    if (!doc.contains("metallicFactor") ||
        !ReadFloat(doc["metallicFactor"], &out.metallicFactor))
        return Fail(kCodeMaterialAssetInvalid, "metallicFactor invalid");
    if (!doc.contains("roughnessFactor") ||
        !ReadFloat(doc["roughnessFactor"], &out.roughnessFactor))
        return Fail(kCodeMaterialAssetInvalid, "roughnessFactor invalid");
    if (!doc.contains("emissiveFactor") ||
        !ReadFloatArray(doc["emissiveFactor"], out.emissiveFactor, 3))
        return Fail(kCodeMaterialAssetInvalid, "emissiveFactor invalid");
    if (!doc.contains("alphaCutoff") ||
        !ReadFloat(doc["alphaCutoff"], &out.alphaCutoff))
        return Fail(kCodeMaterialAssetInvalid, "alphaCutoff invalid");
    std::string alpha;
    if (!doc.contains("alphaMode") || !doc["alphaMode"].is_string())
        return Fail(kCodeMaterialAssetInvalid, "alphaMode invalid");
    alpha = doc["alphaMode"].get<std::string>();
    if (alpha == "OPAQUE") out.alphaMode = 0;
    else if (alpha == "MASK") out.alphaMode = 1;
    else if (alpha == "BLEND") out.alphaMode = 2;
    else return Fail(kCodeMaterialAssetInvalid,
                     "unknown alphaMode '" + alpha + "'");

    if (!ReadTextureRef(doc.contains("baseColorTexture")
                            ? doc["baseColorTexture"]
                            : Json::object(),
                        &out.baseColor, "baseColorTexture"))
        return Fail(kCodeMaterialAssetInvalid, "baseColorTexture invalid");
    if (!ReadTextureRef(doc.contains("normalTexture")
                            ? doc["normalTexture"]
                            : Json::object(),
                        &out.normal, "normalTexture"))
        return Fail(kCodeMaterialAssetInvalid, "normalTexture invalid");
    if (!ReadTextureRef(doc.contains("occlusionTexture")
                            ? doc["occlusionTexture"]
                            : Json::object(),
                        &out.occlusion, "occlusionTexture"))
        return Fail(kCodeMaterialAssetInvalid, "occlusionTexture invalid");
    if (!ReadTextureRef(doc.contains("emissiveTexture")
                            ? doc["emissiveTexture"]
                            : Json::object(),
                        &out.emissive, "emissiveTexture"))
        return Fail(kCodeMaterialAssetInvalid, "emissiveTexture invalid");
    if (!ReadTextureRef(doc.contains("metallicRoughnessTexture")
                            ? doc["metallicRoughnessTexture"]
                            : Json::object(),
                        &out.metallicRoughness, "metallicRoughnessTexture"))
        return Fail(kCodeMaterialAssetInvalid,
                    "metallicRoughnessTexture invalid");

    MaterialAssetReadResult result;
    result.ok = true;
    result.material = std::move(out);
    return result;
}

} // namespace Engine::AssetImport
