#include "MeshAssetReader.h"

#include "MeshAssetWriter.h"
#include "ImportLimits.h"
#include "ImportDiagnostic.h"
#include "MeshImportIR.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <set>

namespace Engine::AssetImport {

namespace {

using Json = nlohmann::json;

MeshAssetReadResult Fail(const char* code, const std::string& detail) {
    MeshAssetReadResult r;
    r.ok = false;
    r.error = std::string(code) + ": " + detail;
    return r;
}

bool ReadU32Strict(const Json& j, uint32_t* out) {
    if (j.is_number_unsigned()) {
        const auto v = j.get<uint64_t>();
        if (v > 0xFFFFFFFFull) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    }
    if (j.is_number_integer()) {
        const auto v = j.get<int64_t>();
        if (v < 0 || v > 0xFFFFFFFFll) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    }
    return false;
}

bool ReadFloatStrict(const Json& j, float* out) {
    if (!j.is_number()) return false;
    const double v = j.get<double>();
    if (!std::isfinite(v)) return false;
    const float f = static_cast<float>(v);
    if (!std::isfinite(f)) return false; // overflow to inf
    *out = f;
    return true;
}

bool ReadFloatArray(const Json& j, float* out, size_t count) {
    if (!j.is_array() || j.size() != count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!ReadFloatStrict(j[i], &out[i])) return false;
    }
    return true;
}

bool ReadStringArray(const Json& j, std::vector<std::string>* out) {
    if (!j.is_array()) return false;
    out->clear();
    out->reserve(j.size());
    for (const auto& item : j) {
        if (!item.is_string()) return false;
        out->push_back(item.get<std::string>());
    }
    return true;
}

const std::set<std::string>& AllowedRootKeys() {
    static const std::set<std::string> keys = {
        "format", "formatVersion", "sourceFile", "vertices", "indices",
        "primitives", "materialNames", "imageUris"};
    return keys;
}

} // namespace

MeshAssetReadResult ReadMeshAssetJson(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        return Fail(kCodeMeshAssetParse, "empty asset bytes");
    }
    Json doc;
    try {
        doc = Json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& e) {
        return Fail(kCodeMeshAssetParse, std::string("JSON parse failed: ") + e.what());
    }
    if (!doc.is_object()) {
        return Fail(kCodeMeshAssetParse, "root must be a JSON object");
    }
    for (const auto& item : doc.items()) {
        if (AllowedRootKeys().find(item.key()) == AllowedRootKeys().end()) {
            return Fail(kCodeMeshAssetParse,
                        "unknown top-level field '" + item.key() + "'");
        }
    }
    if (!doc.contains("format") || !doc["format"].is_string() ||
        doc["format"].get<std::string>() != "mesh") {
        return Fail(kCodeMeshAssetParse, "format is not \"mesh\"");
    }
    if (!doc.contains("formatVersion")) {
        return Fail(kCodeMeshAssetUnsupportedVersion,
                    "formatVersion missing");
    }
    uint32_t version = 0;
    if (!ReadU32Strict(doc["formatVersion"], &version)) {
        return Fail(kCodeMeshAssetUnsupportedVersion,
                    "formatVersion is not an unsigned integer");
    }
    if (version != kMeshAssetFormatVersion) {
        return Fail(kCodeMeshAssetUnsupportedVersion,
                    "unsupported formatVersion " + std::to_string(version));
    }

    MeshAsset out;
    out.formatVersion = version;

    if (!doc.contains("sourceFile") || !doc["sourceFile"].is_string()) {
        return Fail(kCodeMeshAssetInvalid, "sourceFile must be a string");
    }
    out.sourceFile = doc["sourceFile"].get<std::string>();

    // ── vertices ─────────────────────────────────────────────────────────
    if (!doc.contains("vertices") || !doc["vertices"].is_array()) {
        return Fail(kCodeMeshAssetInvalid, "vertices must be an array");
    }
    const auto& verts = doc["vertices"];
    if (verts.size() > kMaxTotalVertices) {
        return Fail(kCodeMeshAssetInvalid, "vertex count exceeds limit");
    }
    out.vertices.reserve(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const Json& v = verts[i];
        const std::string where = "vertices[" + std::to_string(i) + "]";
        if (!v.is_object()) {
            return Fail(kCodeMeshAssetInvalid, where + " must be an object");
        }
        static const std::set<std::string> vkeys = {"position", "normal",
                                                    "uv0", "tangent", "flags"};
        for (const auto& item : v.items()) {
            if (vkeys.find(item.key()) == vkeys.end()) {
                return Fail(kCodeMeshAssetInvalid,
                            where + " has unknown field '" + item.key() + "'");
            }
        }
        MeshAssetVertex vertex;
        if (!v.contains("position") ||
            !ReadFloatArray(v["position"], vertex.position, 3)) {
            return Fail(kCodeMeshAssetInvalid, where + ".position invalid");
        }
        if (!v.contains("normal") ||
            !ReadFloatArray(v["normal"], vertex.normal, 3)) {
            return Fail(kCodeMeshAssetInvalid, where + ".normal invalid");
        }
        if (!v.contains("uv0") || !ReadFloatArray(v["uv0"], vertex.uv0, 2)) {
            return Fail(kCodeMeshAssetInvalid, where + ".uv0 invalid");
        }
        if (!v.contains("tangent") ||
            !ReadFloatArray(v["tangent"], vertex.tangent, 3)) {
            return Fail(kCodeMeshAssetInvalid, where + ".tangent invalid");
        }
        uint32_t flags = 0;
        if (!v.contains("flags") || !ReadU32Strict(v["flags"], &flags)) {
            return Fail(kCodeMeshAssetInvalid, where + ".flags invalid");
        }
        constexpr uint32_t kKnownFlags =
            kVertexFlagNormalValid | kVertexFlagTangentValid |
            kVertexFlagUvValid;
        if (flags > kKnownFlags) {
            return Fail(kCodeMeshAssetInvalid, where + ".flags has unknown bits");
        }
        vertex.flags = static_cast<uint8_t>(flags);
        out.vertices.push_back(vertex);
    }

    // ── indices ──────────────────────────────────────────────────────────
    if (!doc.contains("indices") || !doc["indices"].is_array()) {
        return Fail(kCodeMeshAssetInvalid, "indices must be an array");
    }
    const auto& idx = doc["indices"];
    if (idx.size() > kMaxTotalVertices * 3ull) {
        return Fail(kCodeMeshAssetInvalid, "index count exceeds limit");
    }
    out.indices.reserve(idx.size());
    for (size_t i = 0; i < idx.size(); ++i) {
        uint32_t value = 0;
        if (!ReadU32Strict(idx[i], &value)) {
            return Fail(kCodeMeshAssetInvalid,
                        "indices[" + std::to_string(i) + "] invalid");
        }
        if (value >= out.vertices.size()) {
            return Fail(kCodeMeshAssetInvalid,
                        "indices[" + std::to_string(i) +
                            "] out of vertex range");
        }
        out.indices.push_back(value);
    }
    if (out.indices.size() % 3 != 0) {
        return Fail(kCodeMeshAssetInvalid,
                    "index count is not a multiple of 3 (TRIANGLES only)");
    }

    // ── materialNames ────────────────────────────────────────────────────
    if (!doc.contains("materialNames") ||
        !ReadStringArray(doc["materialNames"], &out.materialNames)) {
        return Fail(kCodeMeshAssetInvalid,
                    "materialNames must be an array of strings");
    }
    if (out.materialNames.size() > kMaxMaterials) {
        return Fail(kCodeMeshAssetInvalid, "materialNames exceeds limit");
    }

    // ── primitives ───────────────────────────────────────────────────────
    if (!doc.contains("primitives") || !doc["primitives"].is_array()) {
        return Fail(kCodeMeshAssetInvalid, "primitives must be an array");
    }
    const auto& prims = doc["primitives"];
    if (prims.size() > kMaxPrimitives) {
        return Fail(kCodeMeshAssetInvalid, "primitives exceeds limit");
    }
    out.primitives.reserve(prims.size());
    uint64_t prevEnd = 0;
    for (size_t i = 0; i < prims.size(); ++i) {
        const Json& p = prims[i];
        const std::string where = "primitives[" + std::to_string(i) + "]";
        if (!p.is_object()) {
            return Fail(kCodeMeshAssetInvalid, where + " must be an object");
        }
        static const std::set<std::string> pkeys = {
            "firstIndex", "indexCount", "materialIndex", "nodeName",
            "sourceTransform"};
        for (const auto& item : p.items()) {
            if (pkeys.find(item.key()) == pkeys.end()) {
                return Fail(kCodeMeshAssetInvalid,
                            where + " has unknown field '" + item.key() + "'");
            }
        }
        MeshAssetSpan span;
        uint32_t firstIndex = 0, indexCount = 0;
        if (!p.contains("firstIndex") ||
            !ReadU32Strict(p["firstIndex"], &firstIndex)) {
            return Fail(kCodeMeshAssetInvalid, where + ".firstIndex invalid");
        }
        if (!p.contains("indexCount") ||
            !ReadU32Strict(p["indexCount"], &indexCount) || indexCount == 0) {
            return Fail(kCodeMeshAssetInvalid, where + ".indexCount invalid");
        }
        const uint64_t end =
            static_cast<uint64_t>(firstIndex) + static_cast<uint64_t>(indexCount);
        if (end > out.indices.size()) {
            return Fail(kCodeMeshAssetInvalid, where + " exceeds index range");
        }
        if (static_cast<uint64_t>(firstIndex) < prevEnd) {
            return Fail(kCodeMeshAssetInvalid, where + " overlaps previous span");
        }
        prevEnd = end;
        span.firstIndex = firstIndex;
        span.indexCount = indexCount;
        if (!p.contains("materialIndex")) {
            return Fail(kCodeMeshAssetInvalid, where + ".materialIndex missing");
        }
        if (p["materialIndex"].is_null()) {
            span.materialIndex = 0xFFFFFFFFu;
        } else {
            uint32_t mi = 0;
            if (!ReadU32Strict(p["materialIndex"], &mi) ||
                mi >= out.materialNames.size()) {
                return Fail(kCodeMeshAssetInvalid,
                            where + ".materialIndex out of range");
            }
            span.materialIndex = mi;
        }
        if (!p.contains("nodeName") || !p["nodeName"].is_string()) {
            return Fail(kCodeMeshAssetInvalid, where + ".nodeName invalid");
        }
        span.nodeName = p["nodeName"].get<std::string>();
        if (!p.contains("sourceTransform") ||
            !ReadFloatArray(p["sourceTransform"], span.sourceTransform, 16)) {
            return Fail(kCodeMeshAssetInvalid,
                        where + ".sourceTransform invalid");
        }
        out.primitives.push_back(std::move(span));
    }
    // Structural consistency: empty index stream implies no spans; spans
    // without indices are rejected above (end > size would fire).
    if (out.indices.empty() && !out.primitives.empty()) {
        return Fail(kCodeMeshAssetInvalid, "primitives without indices");
    }

    // ── imageUris ────────────────────────────────────────────────────────
    if (!doc.contains("imageUris") || !doc["imageUris"].is_array()) {
        return Fail(kCodeMeshAssetInvalid, "imageUris must be an array");
    }
    const auto& imgs = doc["imageUris"];
    if (imgs.size() > kMaxImages) {
        return Fail(kCodeMeshAssetInvalid, "imageUris exceeds limit");
    }
    out.images.reserve(imgs.size());
    for (size_t i = 0; i < imgs.size(); ++i) {
        const Json& im = imgs[i];
        const std::string where = "imageUris[" + std::to_string(i) + "]";
        if (!im.is_object()) {
            return Fail(kCodeMeshAssetInvalid, where + " must be an object");
        }
        static const std::set<std::string> ikeys = {"uri", "mimeType"};
        for (const auto& item : im.items()) {
            if (ikeys.find(item.key()) == ikeys.end()) {
                return Fail(kCodeMeshAssetInvalid,
                            where + " has unknown field '" + item.key() + "'");
            }
        }
        MeshAssetImageRef ref;
        if (!im.contains("uri") || !im["uri"].is_string() ||
            !im.contains("mimeType") || !im["mimeType"].is_string()) {
            return Fail(kCodeMeshAssetInvalid, where + " requires uri/mimeType strings");
        }
        ref.uri = im["uri"].get<std::string>();
        ref.mimeType = im["mimeType"].get<std::string>();
        out.images.push_back(std::move(ref));
    }

    MeshAssetReadResult result;
    result.ok = true;
    result.mesh = std::move(out);
    return result;
}

} // namespace Engine::AssetImport
