#include "GltfParser.h"

#include "GlbContainerReader.h"
#include "ImportLimits.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <fstream>

namespace Engine::AssetImport {

namespace {

using Json = nlohmann::json;

constexpr uint32_t kComponentTypeByte = 5121;
constexpr uint32_t kComponentTypeUShort = 5123;
constexpr uint32_t kComponentTypeUInt = 5125;
constexpr uint32_t kComponentTypeFloat = 5126;
constexpr uint32_t kPrimitiveModeTriangles = 4;
constexpr uint32_t kMaterialNone = 0xFFFFFFFFu;

struct ParserState
{
    const Json* doc = nullptr;
    std::vector<std::vector<uint8_t>> buffers; // external files live here
    const uint8_t* glbBin = nullptr;
    size_t glbBinSize = 0;
    bool glbMode = false;
    std::string sourceDir;
    MeshImportIR* out = nullptr;
    std::vector<ImportDiagnostic>* diag = nullptr;
    size_t totalInstancedVertices = 0;
    uint32_t totalInstancedPrimitives = 0;

    size_t BufferSize(uint32_t bufferIndex) const {
        if (glbMode && bufferIndex == 0) return glbBinSize;
        return bufferIndex < buffers.size() ? buffers[bufferIndex].size() : 0;
    }
    const uint8_t* BufferData(uint32_t bufferIndex) const {
        if (glbMode && bufferIndex == 0) return glbBin;
        if (glbMode && bufferIndex > 0) return nullptr;
        return bufferIndex < buffers.size() ? buffers[bufferIndex].data() : nullptr;
    }
};

void Diag(ParserState& s, ImportSeverity sev, const char* code,
          const std::string& location, const std::string& message) {
    ImportDiagnostic d;
    d.severity = sev;
    d.code = code;
    d.location = location;
    d.message = message;
    s.diag->push_back(std::move(d));
}

bool HasError(const std::vector<ImportDiagnostic>& d) {
    for (const auto& x : d) {
        if (x.severity == ImportSeverity::Error) return true;
    }
    return false;
}

bool GetU32(const Json& obj, const char* field, uint32_t* out) {
    if (!obj.contains(field)) return false;
    const Json& v = obj[field];
    if (v.is_number_unsigned()) {
        const auto u = v.get<uint64_t>();
        if (u > 0xFFFFFFFFull) return false;
        *out = static_cast<uint32_t>(u);
        return true;
    }
    if (v.is_number_integer()) {
        const auto value = v.get<int64_t>();
        if (value < 0 || value > 0xFFFFFFFFll) return false;
        *out = static_cast<uint32_t>(value);
        return true;
    }
    return false;
}

float GetFloatOr(const Json& obj, const char* field, float fallback) {
    if (!obj.contains(field) || !obj[field].is_number()) return fallback;
    return static_cast<float>(obj[field].get<double>());
}

bool GetString(const Json& obj, const char* field, std::string* out) {
    if (!obj.contains(field) || !obj[field].is_string()) return false;
    *out = obj[field].get<std::string>();
    return true;
}

uint32_t CountOf(const Json& arr) {
    return arr.is_array() ? static_cast<uint32_t>(arr.size()) : 0u;
}

std::string Loc(const char* a, uint32_t i) {
    return std::string(a) + "[" + std::to_string(i) + "]";
}

std::string LocJoin(const std::string& a, const char* b) {
    return a + "." + b;
}

// ── URI rules (contract section 7) ────────────────────────────────────────

enum class UriStatus : uint8_t { Ok, DataUri, Scheme, Traversal, Percent, Invalid };

UriStatus ValidateUri(const std::string& uri) {
    if (uri.empty()) return UriStatus::Invalid;
    if (uri.rfind("data:", 0) == 0) return UriStatus::DataUri;
    if (uri.front() == '/' || uri.front() == '\\') return UriStatus::Traversal;
    for (const char c : uri) {
        if (c == '%') return UriStatus::Percent;
        if (c == '\\') return UriStatus::Traversal;
        if (c == ':') return UriStatus::Scheme;
        if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || c == 0) return UriStatus::Invalid;
    }
    size_t start = 0;
    while (true) {
        const size_t sep = uri.find('/', start);
        const size_t end = (sep == std::string::npos) ? uri.size() : sep;
        if (end - start == 2 && uri.compare(start, 2, "..") == 0)
            return UriStatus::Traversal;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return UriStatus::Ok;
}

void ReportUriFailure(ParserState& s, const std::string& uri,
                      const std::string& location) {
    switch (ValidateUri(uri)) {
        case UriStatus::Ok:
            break;
        case UriStatus::DataUri:
            Diag(s, ImportSeverity::Error, kCodeDataUriUnsupported, location,
                 "data: URIs unsupported in v1 (use GLB bufferViews or same-directory relative URIs): " + uri);
            break;
        case UriStatus::Scheme:
            Diag(s, ImportSeverity::Error, kCodeUriSchemeUnsupported, location,
                 "absolute or scheme URIs are rejected: " + uri);
            break;
        case UriStatus::Traversal:
            Diag(s, ImportSeverity::Error, kCodeUriTraversal, location,
                 "path traversal / backslash URIs are rejected: " + uri);
            break;
        case UriStatus::Percent:
            Diag(s, ImportSeverity::Error, kCodeUriPercentUnsupported, location,
                 "percent-encoded URIs unsupported in v1: " + uri);
            break;
        case UriStatus::Invalid:
        default:
            Diag(s, ImportSeverity::Error, kCodeUriInvalid, location,
                 "empty or invalid characters in URI: " + uri);
            break;
    }
}

bool ReadExternalFile(ParserState& s, const std::string& uri,
                      std::vector<uint8_t>* bytesOut) {
    const std::string loc = "uri=" + uri;
    ReportUriFailure(s, uri, loc);
    if (HasError(*s.diag)) return false;
    std::string dir = s.sourceDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::string path = dir.empty() ? uri : (dir + "/" + uri);
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        Diag(s, ImportSeverity::Error, kCodeExternalBufferReadFail, loc,
             "cannot open external file: " + path);
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        Diag(s, ImportSeverity::Error, kCodeExternalBufferReadFail, loc,
             "cannot determine file size: " + path);
        return false;
    }
    if (static_cast<size_t>(size) > kMaxSourceFileBytes) {
        Diag(s, ImportSeverity::Error, kCodeLimitSourceSize, loc,
             "external file exceeds kMaxSourceFileBytes");
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytesOut->resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytesOut->data()), size);
        if (in.gcount() != size) {
            Diag(s, ImportSeverity::Error, kCodeExternalBufferReadFail, loc,
                 "short read: " + path);
            return false;
        }
    }
    return true;
}

// ── asset header / extensions / global caps ──────────────────────────────

bool ValidateAssetHeader(ParserState& s) {
    const Json& doc = *s.doc;
    if (!doc.contains("asset") || !doc["asset"].is_object()) {
        Diag(s, ImportSeverity::Error, kCodeMissingField, "asset", "asset object missing");
        return false;
    }
    std::string version;
    if (!GetString(doc["asset"], "version", &version) || version != "2.0") {
        Diag(s, ImportSeverity::Error, kCodeBadVersion, "asset.version",
             version.empty() ? "glTF asset.version must be \"2.0\", got: (missing/invalid)"
                             : "glTF asset.version must be \"2.0\", got: " + version);
        return false;
    }
    return true;
}

void ValidateExtensions(ParserState& s) {
    const Json& doc = *s.doc;
    if (doc.contains("extensionsRequired") && doc["extensionsRequired"].is_array()) {
        for (const auto& e : doc["extensionsRequired"]) {
            if (e.is_string()) {
                Diag(s, ImportSeverity::Error, kCodeRequiredExtensionUnsupported,
                     "extensionsRequired",
                     "unsupported required glTF extension: " + e.get<std::string>());
            }
        }
    }
    if (doc.contains("extensionsUsed") && doc["extensionsUsed"].is_array()) {
        for (const auto& e : doc["extensionsUsed"]) {
            if (e.is_string()) {
                Diag(s, ImportSeverity::Warning, kCodeExtensionIgnored,
                     "extensionsUsed",
                     "extension ignored (no semantic effect in v1): " + e.get<std::string>());
            }
        }
    }
}

void ValidateGlobalCaps(ParserState& s) {
    const Json& doc = *s.doc;
    if (doc.contains("meshes") && CountOf(doc["meshes"]) > kMaxMeshes) {
        Diag(s, ImportSeverity::Error, kCodeLimitMeshes, "meshes",
             "mesh count exceeds kMaxMeshes (" + std::to_string(kMaxMeshes) + ")");
    }
    if (doc.contains("nodes") && CountOf(doc["nodes"]) > kMaxSceneNodes) {
        Diag(s, ImportSeverity::Error, kCodeLimitNodeDepth, "nodes",
             "node count exceeds kMaxSceneNodes");
    }
    if (doc.contains("images") && CountOf(doc["images"]) > kMaxImages) {
        Diag(s, ImportSeverity::Error, kCodeLimitImages, "images",
             "images count exceeds kMaxImages");
    }
}

// ── buffers ──────────────────────────────────────────────────────────────

bool ParseBuffers(ParserState& s) {
    const Json& doc = *s.doc;
    if (!doc.contains("buffers") || !doc["buffers"].is_array() ||
        CountOf(doc["buffers"]) == 0) {
        Diag(s, ImportSeverity::Error, kCodeMissingField, "buffers",
             "buffers array missing or empty");
        return false;
    }
    const uint32_t count = CountOf(doc["buffers"]);
    if (count > kMaxBuffers) {
        Diag(s, ImportSeverity::Error, kCodeLimitBuffers, "buffers",
             "buffer count exceeds kMaxBuffers (" + std::to_string(kMaxBuffers) + ")");
        return false;
    }
    s.buffers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const Json& b = doc["buffers"][i];
        const std::string loc = Loc("buffers", i);
        if (!b.is_object()) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid, loc, "buffer entry is not an object");
            return false;
        }
        uint32_t declared = 0;
        if (!GetU32(b, "byteLength", &declared) || declared == 0) {
            Diag(s, ImportSeverity::Error, kCodeMissingField, LocJoin(loc, "byteLength"),
                 "buffer byteLength missing or zero");
            return false;
        }
        if (s.glbMode && i == 0 && !b.contains("uri")) {
            if (static_cast<size_t>(declared) > s.glbBinSize) {
                Diag(s, ImportSeverity::Error, kCodeBufferViewBounds, loc,
                     "declared buffer byteLength exceeds GLB BIN chunk");
                return false;
            }
            s.buffers[i].assign(s.glbBin, s.glbBin + declared);
            continue;
        }
        std::string uri;
        if (!GetString(b, "uri", &uri)) {
            Diag(s, ImportSeverity::Error, kCodeBufferNoUri, loc,
                 "buffer without uri (only GLB buffer 0 may omit it)");
            return false;
        }
        if (!ReadExternalFile(s, uri, &s.buffers[i])) return false;
        s.out->dependencyFiles.push_back(uri);
        if (static_cast<size_t>(declared) > s.buffers[i].size()) {
            Diag(s, ImportSeverity::Error, kCodeBufferViewBounds, loc,
                 "declared buffer byteLength exceeds actual file bytes");
            return false;
        }
    }
    return true;
}

// ── bufferViews / accessors ──────────────────────────────────────────────

struct AccessorView
{
    const uint8_t* data = nullptr;
    size_t byteCount = 0;
    uint32_t componentType = 0;
    uint32_t componentCount = 0;
    uint32_t count = 0;
};

uint32_t ComponentSize(uint32_t componentType) {
    switch (componentType) {
        case kComponentTypeByte: return 1;
        case kComponentTypeUShort: return 2;
        case kComponentTypeUInt: return 4;
        case kComponentTypeFloat: return 4;
        default: return 0;
    }
}

uint32_t ComponentsOf(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    return 0;
}

bool ResolveAccessor(ParserState& s, uint32_t accessorIndex, AccessorView* view,
                     const std::string& location) {
    const Json& doc = *s.doc;
    if (!doc.contains("accessors") || !doc["accessors"].is_array() ||
        accessorIndex >= CountOf(doc["accessors"])) {
        Diag(s, ImportSeverity::Error, kCodeAccessorBounds, location,
             "accessor index out of range");
        return false;
    }
    const Json& acc = doc["accessors"][accessorIndex];
    if (!acc.is_object()) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, location, "accessor entry is not an object");
        return false;
    }
    if (acc.contains("sparse")) {
        Diag(s, ImportSeverity::Error, kCodeSparseUnsupported, location,
             "sparse accessors unsupported in v1");
        return false;
    }
    uint32_t count = 0;
    if (!GetU32(acc, "count", &count) || count == 0) {
        Diag(s, ImportSeverity::Error, kCodeMissingField, LocJoin(location, "count"),
             "accessor count missing or zero");
        return false;
    }
    uint32_t compType = 0;
    if (!GetU32(acc, "componentType", &compType) || ComponentSize(compType) == 0) {
        Diag(s, ImportSeverity::Error, kCodeAttrComponentUnsupported,
             LocJoin(location, "componentType"), "unknown componentType");
        return false;
    }
    std::string typeStr;
    if (!GetString(acc, "type", &typeStr) || ComponentsOf(typeStr) == 0) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, LocJoin(location, "type"),
             "unknown accessor type");
        return false;
    }
    if (acc.contains("normalized") && acc["normalized"].is_boolean() &&
        acc["normalized"].get<bool>() && compType != kComponentTypeFloat) {
        Diag(s, ImportSeverity::Error, kCodeAttrNormalizedUnsupported,
             LocJoin(location, "normalized"),
             "normalized integer accessors unsupported in v1");
        return false;
    }
    uint32_t viewIdx = 0;
    if (!GetU32(acc, "bufferView", &viewIdx)) {
        Diag(s, ImportSeverity::Error, kCodeMissingField, LocJoin(location, ".bufferView"),
             "bufferView missing (sparse excluded in v1)");
        return false;
    }
    const Json& doc2 = *s.doc;
    if (!doc2.contains("bufferViews") || !doc2["bufferViews"].is_array() ||
        viewIdx >= CountOf(doc2["bufferViews"])) {
        Diag(s, ImportSeverity::Error, kCodeBufferViewBounds, LocJoin(location, ".bufferView"),
             "bufferView index out of range");
        return false;
    }
    const Json& viewJson = doc2["bufferViews"][viewIdx];
    if (!viewJson.is_object()) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, Loc("bufferViews", viewIdx),
             "bufferView entry is not an object");
        return false;
    }
    uint32_t bufferIdx = 0;
    if (!GetU32(viewJson, "buffer", &bufferIdx)) {
        Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
             Loc("bufferViews", viewIdx), "buffer reference missing");
        return false;
    }
    if (bufferIdx >= s.buffers.size()) {
        Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
             Loc("bufferViews", viewIdx), "buffer reference out of range");
        return false;
    }
    uint32_t viewOffset = 0;
    GetU32(viewJson, "byteOffset", &viewOffset);
    uint32_t viewLen = 0;
    if (!GetU32(viewJson, "byteLength", &viewLen) || viewLen == 0) {
        Diag(s, ImportSeverity::Error, kCodeMissingField,
             LocJoin(Loc("bufferViews", viewIdx), "byteLength"),
             "byteLength missing or zero");
        return false;
    }
    uint32_t stride = 0;
    GetU32(viewJson, "byteStride", &stride);
    if (stride != 0) {
        Diag(s, ImportSeverity::Error, kCodeInterleavedUnsupported,
             Loc("bufferViews", viewIdx),
             "interleaved (non-zero byteStride) views unsupported in v1");
        return false;
    }
    const size_t bufSize = s.BufferSize(bufferIdx);
    const uint64_t viewOff64 = static_cast<uint64_t>(viewOffset);
    const uint64_t viewLen64 = static_cast<uint64_t>(viewLen);
    if (viewOff64 + viewLen64 > static_cast<uint64_t>(bufSize)) {
        Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
             Loc("bufferViews", viewIdx), "bufferView range exceeds buffer size");
        return false;
    }
    uint32_t accOffset = 0;
    GetU32(acc, "byteOffset", &accOffset);
    const uint32_t comps = ComponentsOf(typeStr);
    const uint32_t compSize = ComponentSize(compType);
    const size_t byteStart = static_cast<size_t>(viewOffset) + static_cast<size_t>(accOffset);
    if (compSize > 0 && byteStart % compSize != 0) {
        Diag(s, ImportSeverity::Error, kCodeBadAlignment, location,
             "accessor byteOffset misaligned to component size");
        return false;
    }
    const size_t elems = static_cast<size_t>(count) * comps;
    const size_t byteLen = elems * compSize;
    if (static_cast<uint64_t>(accOffset) + static_cast<uint64_t>(byteLen) >
        static_cast<uint64_t>(viewLen)) {
        Diag(s, ImportSeverity::Error, kCodeAccessorBounds, location,
             "accessor byte range exceeds bufferView");
        return false;
    }
    view->data = s.BufferData(bufferIdx) + byteStart;
    view->byteCount = byteLen;
    view->componentType = compType;
    view->componentCount = comps;
    view->count = count;
    return true;
}

template <uint32_t N>
bool ResolveFloatAttribute(ParserState& s, uint32_t accessorIndex,
                           std::vector<float>* out, uint32_t* outCount,
                           const std::string& location) {
    if (accessorIndex == 0xFFFFFFFFu) {
        *outCount = 0;
        return true;
    }
    AccessorView v;
    if (!ResolveAccessor(s, accessorIndex, &v, location)) return false;
    if (v.componentType != kComponentTypeFloat) {
        Diag(s, ImportSeverity::Error, kCodeAttrComponentUnsupported, location,
             "attribute requires FLOAT componentType (5126)");
        return false;
    }
    if (v.componentCount != N) {
        Diag(s, ImportSeverity::Error, kCodeAttrComponentUnsupported, location,
             "attribute component count mismatch");
        return false;
    }
    out->resize(static_cast<size_t>(v.count) * N);
    std::memcpy(out->data(), v.data, static_cast<size_t>(v.count) * N * sizeof(float));
    *outCount = v.count;
    return true;
}

bool ResolveIndices(ParserState& s, uint32_t accessorIndex,
                    std::vector<uint32_t>* out, uint32_t* outCount,
                    const std::string& location) {
    if (accessorIndex == 0xFFFFFFFFu) {
        *outCount = 0;
        return true;
    }
    AccessorView v;
    if (!ResolveAccessor(s, accessorIndex, &v, location)) return false;
    if (v.componentCount != 1) {
        Diag(s, ImportSeverity::Error, kCodeAttrComponentUnsupported, location,
             "indices must be SCALAR");
        return false;
    }
    const uint32_t componentSize = ComponentSize(v.componentType);
    out->resize(v.count);
    for (uint32_t i = 0; i < v.count; ++i) {
        uint32_t value = 0;
        std::memcpy(&value, v.data + static_cast<size_t>(i) * componentSize,
                    componentSize);
        (*out)[i] = value;
    }
    *outCount = v.count;
    return true;
}

// ── materials / images / textures ────────────────────────────────────────

bool ParseImageEntry(ParserState& s, const Json& img, const std::string& loc,
                     bool glbMode, TextureImageIR* entry) {
    if (!img.is_object()) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, loc, "image entry is not an object");
        return false;
    }
    const bool hasUri = img.contains("uri");
    const bool hasView = img.contains("bufferView");
    if (hasUri && hasView) {
        Diag(s, ImportSeverity::Error, kCodeTexImageMissing, loc,
             "image declares both uri and bufferView");
        return false;
    }
    if (hasUri) {
        std::string uri;
        GetString(img, "uri", &uri);
        ReportUriFailure(s, uri, LocJoin(loc, "uri"));
        if (HasError(*s.diag)) return false;
        entry->uri = uri;
    } else if (hasView) {
        if (!glbMode) {
            Diag(s, ImportSeverity::Error, kCodeTexImageMissing, loc,
                 "embedded image (bufferView) only valid in GLB container");
            return false;
        }
        uint32_t v = 0;
        if (!GetU32(img, "bufferView", &v)) {
            Diag(s, ImportSeverity::Error, kCodeTexImageMissing, LocJoin(loc, "bufferView"),
                 "bufferView index invalid");
            return false;
        }
        entry->bufferViewIndex = v;
    } else {
        Diag(s, ImportSeverity::Error, kCodeTexImageMissing, loc,
             "image has neither uri nor bufferView");
        return false;
    }
    GetString(img, "mimeType", &entry->mimeType);
    entry->sourceImageIndex = 0;
    return true;
}

bool ParseMaterials(ParserState& s, bool glbMode) {
    MeshImportIR& out = *s.out;
    const Json& doc = *s.doc;
    if (!doc.contains("materials") || !doc["materials"].is_array()) return true;
    const Json& materials = doc["materials"];
    out.materials.resize(CountOf(materials));
    std::vector<int32_t> imageOfTexture;
    if (doc.contains("textures") && doc["textures"].is_array()) {
        imageOfTexture.assign(CountOf(doc["textures"]), -1);
        for (uint32_t i = 0; i < CountOf(doc["textures"]); ++i) {
            const Json& t = doc["textures"][i];
            if (!t.is_object()) continue;
            uint32_t src = 0;
            if (GetU32(t, "source", &src)) imageOfTexture[i] = static_cast<int32_t>(src);
        }
    }
    (void)glbMode;
    struct RefToFill { TextureReferenceIR* ref; };
    for (uint32_t m = 0; m < out.materials.size(); ++m) {
        const Json& jm = materials[m];
        const std::string loc = Loc("materials", m);
        if (!jm.is_object()) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid, loc,
                 "material entry is not an object");
            return false;
        }
        MaterialIR& mat = out.materials[m];
        GetString(jm, "name", &mat.name);
        if (jm.contains("emissiveFactor")) {
            const Json& ef = jm["emissiveFactor"];
            bool ok = ef.is_array() && ef.size() == 3;
            for (int i = 0; ok && i < 3; ++i) ok = ef[i].is_number();
            if (!ok) {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                     LocJoin(loc, "emissiveFactor"),
                     "emissiveFactor must be an array of 3 numbers");
                return false;
            }
            for (int i = 0; i < 3; ++i)
                mat.emissiveFactor[i] = static_cast<float>(ef[i].get<double>());
        }
        std::string alphaMode;
        if (GetString(jm, "alphaMode", &alphaMode)) {
            if (alphaMode == "OPAQUE") mat.alphaMode = AlphaModeIR::Opaque;
            else if (alphaMode == "MASK") mat.alphaMode = AlphaModeIR::Mask;
            else if (alphaMode == "BLEND") mat.alphaMode = AlphaModeIR::Blend;
            else {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid, LocJoin(loc, "alphaMode"),
                     "unknown alphaMode: " + alphaMode);
                return false;
            }
        }
        if (jm.contains("pbrMetallicRoughness") && jm["pbrMetallicRoughness"].is_object()) {
            const Json& pbr = jm["pbrMetallicRoughness"];
            if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() &&
                pbr["baseColorFactor"].size() >= 4) {
                for (int i = 0; i < 4; ++i) {
                    if (!pbr["baseColorFactor"][i].is_number()) {
                        Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                             LocJoin(loc, "pbrMetallicRoughness.baseColorFactor"),
                             "baseColorFactor has non-number component");
                        return false;
                    }
                    mat.baseColorFactor[i] =
                        static_cast<float>(pbr["baseColorFactor"][i].get<double>());
                }
            }
            mat.metallicFactor = GetFloatOr(pbr, "metallicFactor", 1.0f);
            mat.roughnessFactor = GetFloatOr(pbr, "roughnessFactor", 1.0f);
            mat.alphaCutoff = GetFloatOr(jm, "alphaCutoff", 0.5f);

            struct FieldSpec { const char* subject; bool onObjMetallic; };
            for (FieldSpec spec : { FieldSpec{"baseColorTexture", true},
                                    FieldSpec{"metallicRoughnessTexture", true},
                                    FieldSpec{"normalTexture", false},
                                    FieldSpec{"occlusionTexture", false},
                                    FieldSpec{"emissiveTexture", false} }) {
                const Json& host = spec.onObjMetallic ? pbr : jm;
                if (!host.contains(spec.subject) || !host[spec.subject].is_object())
                    continue;
                const Json& jref = host[spec.subject];
                TextureReferenceIR ref;
                uint32_t texIdx = 0, coord = 0;
                if (!GetU32(jref, "index", &texIdx)) {
                    Diag(s, ImportSeverity::Error, kCodeTexImageMissing,
                         loc + "." + spec.subject + ".index", "texture index invalid");
                    return false;
                }
                GetU32(jref, "texCoord", &coord);
                if (coord != 0) {
                    Diag(s, ImportSeverity::Error, kCodeTexCoordSetUnsupported, loc,
                         "multi-UV sets unsupported in v1");
                    return false;
                }
                if (texIdx >= imageOfTexture.size()) {
                    Diag(s, ImportSeverity::Error, kCodeTexImageMissing, loc,
                         "texture index out of range");
                    return false;
                }
                const int32_t imgIdx = imageOfTexture[texIdx];
                if (imgIdx < 0) {
                    Diag(s, ImportSeverity::Error, kCodeTexImageMissing, loc,
                         "texture lacks image source");
                    return false;
                }
                ref.imageIndex = static_cast<uint32_t>(imgIdx);
                ref.present = true;
                switch (spec.subject[0]) {
                    case 'b': mat.baseColor = ref; break;
                    case 'm': mat.metallicRoughness = ref; break;
                    case 'n': mat.normal = ref; break;
                    case 'o': mat.occlusion = ref; break;
                    default: mat.emissive = ref; break;
                }
            }
        }
    }

    // Collect referenced images in document order, then remap indices.
    if (doc.contains("images") && doc["images"].is_array()) {
        for (uint32_t i = 0; i < CountOf(doc["images"]); ++i) {
            bool referenced = false;
            for (const MaterialIR& mat : out.materials) {
                referenced = referenced ||
                             (mat.baseColor.present && mat.baseColor.imageIndex == i) ||
                             (mat.normal.present && mat.normal.imageIndex == i) ||
                             (mat.occlusion.present && mat.occlusion.imageIndex == i) ||
                             (mat.emissive.present && mat.emissive.imageIndex == i) ||
                             (mat.metallicRoughness.present && mat.metallicRoughness.imageIndex == i);
            }
            if (!referenced) continue;
            TextureImageIR img;
            img.sourceImageIndex = i;
            if (!ParseImageEntry(s, doc["images"][i], Loc("images", i), glbMode, &img))
                return false;
            out.images.push_back(std::move(img));
        }
        // B4: parallel imageBytes for GLB-embedded images (exact bufferView
        // bytes, bounds-checked); empty entry marks an external URI image.
        out.imageBytes.resize(out.images.size());
        for (uint32_t k = 0; k < out.images.size(); ++k) {
            const TextureImageIR& entry = out.images[k];
            if (entry.uri.empty() &&
                entry.bufferViewIndex != 0xFFFFFFFFu) {
                if (!doc.contains("bufferViews") ||
                    entry.bufferViewIndex >= CountOf(doc["bufferViews"])) {
                    Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
                         Loc("images", entry.sourceImageIndex),
                         "embedded image bufferView out of range");
                    return false;
                }
                const Json& v = doc["bufferViews"][entry.bufferViewIndex];
                uint32_t bufferIdx = 0, byteOffset = 0, byteLength = 0;
                if (!GetU32(v, "buffer", &bufferIdx) ||
                    bufferIdx >= s.buffers.size()) {
                    Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
                         Loc("images", entry.sourceImageIndex),
                         "image bufferView buffer reference invalid");
                    return false;
                }
                GetU32(v, "byteOffset", &byteOffset);
                if (!GetU32(v, "byteLength", &byteLength) || byteLength == 0) {
                    Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
                         Loc("images", entry.sourceImageIndex),
                         "embedded image requires non-empty byteLength");
                    return false;
                }
                const size_t bufSize = s.BufferSize(bufferIdx);
                if (static_cast<uint64_t>(byteOffset) + byteLength >
                    static_cast<uint64_t>(bufSize)) {
                    Diag(s, ImportSeverity::Error, kCodeBufferViewBounds,
                         Loc("images", entry.sourceImageIndex),
                         "embedded image range exceeds buffer bytes");
                    return false;
                }
                const uint8_t* base = s.BufferData(bufferIdx);
                out.imageBytes[k].assign(
                    base + byteOffset, base + byteOffset + byteLength);
            }
        }
        for (MaterialIR& mat : out.materials) {
            auto remap = [&](TextureReferenceIR& ref) {
                if (!ref.present) return;
                for (uint32_t k = 0; k < out.images.size(); ++k) {
                    if (out.images[k].sourceImageIndex == ref.imageIndex) {
                        ref.imageIndex = k;
                        break;
                    }
                }
            };
            remap(mat.baseColor);
            remap(mat.normal);
            remap(mat.occlusion);
            remap(mat.emissive);
            remap(mat.metallicRoughness);
        }
    }
    return true;
}

// ── column-major 4x4 math (contract section 5) ──────────────────────────

struct MatCM
{
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

void MatFromMatrixField(const Json& node, MatCM* out, ParserState& s,
                        const std::string& loc) {
    const Json& j = node["matrix"];
    if (!j.is_array() || j.size() < 16) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, LocJoin(loc, "matrix"),
             "matrix must be a 16-length array");
        return;
    }
    for (int i = 0; i < 16; ++i) {
        if (!j[i].is_number()) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                 LocJoin(loc, "matrix"), "matrix entries must be numbers");
            return;
        }
        out->m[i] = static_cast<float>(j[i].get<double>());
    }
}

void MakeTRS(const Json& node, MatCM* out, ParserState& s, const std::string& loc) {
    float t[3] = {0, 0, 0};
    float q[4] = {0, 0, 0, 1};
    float sc[3] = {1, 1, 1};
    if (node.contains("translation") && node["translation"].is_array() &&
        node["translation"].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            if (!node["translation"][i].is_number()) {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                     LocJoin(loc, "translation"), "translation must be numbers");
                return;
            }
            t[i] = static_cast<float>(node["translation"][i].get<double>());
        }
    }
    if (node.contains("rotation") && node["rotation"].is_array() &&
        node["rotation"].size() == 4) {
        for (int i = 0; i < 4; ++i) {
            if (!node["rotation"][i].is_number()) {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                     LocJoin(loc, "rotation"), "rotation must be numbers");
                return;
            }
            q[i] = static_cast<float>(node["rotation"][i].get<double>());
        }
    }
    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            if (!node["scale"][i].is_number()) {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                     LocJoin(loc, "scale"), "scale must be numbers");
                return;
            }
            sc[i] = static_cast<float>(node["scale"][i].get<double>());
        }
    }
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const float r00 = static_cast<float>(1 - 2 * (y * y + z * z));
    const float r01 = static_cast<float>(2 * (x * y - w * z));
    const float r02 = static_cast<float>(2 * (x * z + w * y));
    const float r10 = static_cast<float>(2 * (x * y + w * z));
    const float r11 = static_cast<float>(1 - 2 * (x * x + z * z));
    const float r12 = static_cast<float>(2 * (y * z - w * x));
    const float r20 = static_cast<float>(2 * (x * z - w * y));
    const float r21 = static_cast<float>(2 * (y * z + w * x));
    const float r22 = static_cast<float>(1 - 2 * (x * x + y * y));
    float rs[16] = {
        r00 * sc[0], r10 * sc[0], r20 * sc[0], 0,
        r01 * sc[1], r11 * sc[1], r21 * sc[1], 0,
        r02 * sc[2], r12 * sc[2], r22 * sc[2], 0,
        0, 0, 0, 1};
    for (int r = 0; r < 3; ++r) rs[12 + r] = t[r];
    std::memcpy(out->m, rs, sizeof(rs));
}

void MatMultiply(MatCM& out, const MatCM& a, const MatCM& b) {
    float r[16] = {};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float acc = 0.f;
            for (int k = 0; k < 4; ++k) acc += a.m[k * 4 + row] * b.m[c * 4 + k];
            r[c * 4 + row] = acc;
        }
    }
    std::memcpy(out.m, r, sizeof(r));
}

void TransformPoint(const MatCM& world, const float p[3], float* out3) {
    out3[0] = world.m[0] * p[0] + world.m[4] * p[1] + world.m[8] * p[2] + world.m[12];
    out3[1] = world.m[1] * p[0] + world.m[5] * p[1] + world.m[9] * p[2] + world.m[13];
    out3[2] = world.m[2] * p[0] + world.m[6] * p[1] + world.m[10] * p[2] + world.m[14];
}

void TransformDirection(const MatCM& world, const float d[3], float* out3) {
    out3[0] = world.m[0] * d[0] + world.m[4] * d[1] + world.m[8] * d[2];
    out3[1] = world.m[1] * d[0] + world.m[5] * d[1] + world.m[9] * d[2];
    out3[2] = world.m[2] * d[0] + world.m[6] * d[1] + world.m[10] * d[2];
}

// ── instancing / baking ──────────────────────────────────────────────────

bool InstantiatePrimitive(ParserState& s, const Json& mesh, uint32_t primIdx,
                          const MatCM& world, const std::string& nodeName) {
    MeshImportIR& out = *s.out;
    const std::string loc = "primitives[" + std::to_string(primIdx) + "]";
    if (!mesh.contains("primitives") || !mesh["primitives"].is_array() ||
        CountOf(mesh["primitives"]) == 0) return true;
    const Json& prim = mesh["primitives"][primIdx];
    if (!prim.is_object()) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, loc, "primitive is not an object");
        return false;
    }
    uint32_t mode = kPrimitiveModeTriangles;
    if (prim.contains("mode")) {
        uint32_t m = 0;
        if (!GetU32(prim, "mode", &m)) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid, LocJoin(loc, "mode"),
                 "primitive mode invalid");
            return false;
        }
        mode = m;
    }
    if (mode != kPrimitiveModeTriangles) {
        Diag(s, ImportSeverity::Error, kCodePrimitiveModeUnsupported, loc,
             "unsupported primitive mode (only TRIANGLES=4 in v1)");
        return false;
    }
    if (prim.contains("extensions")) {
        for (const auto& ext : prim["extensions"].items()) {
            Diag(s, ImportSeverity::Error, kCodeRequiredExtensionUnsupported, loc,
                 "primitive extensions unsupported in v1: " + ext.key());
            return false;
        }
    }
    if (!prim.contains("attributes") || !prim["attributes"].is_object()) {
        Diag(s, ImportSeverity::Error, kCodeAttributeMissing, LocJoin(loc, "attributes"),
             "attributes object missing");
        return false;
    }
    const Json& attrs = prim["attributes"];
    for (const auto& a : attrs.items()) {
        if (a.key().rfind("COLOR_", 0) == 0) {
            Diag(s, ImportSeverity::Warning, kCodeVertexColorDropped, loc,
                 "vertex colors dropped in v1");
        } else if (a.key().rfind("TEXCOORD_", 0) == 0 && a.key() != "TEXCOORD_0") {
            Diag(s, ImportSeverity::Error, kCodeTexCoordSetUnsupported, loc,
                 "multi-UV sets unsupported in v1: " + a.key());
            return false;
        }
    }
    uint32_t posIdx = 0, nrmIdx = 0xFFFFFFFFu, tanIdx = 0xFFFFFFFFu,
             uvIdx = 0xFFFFFFFFu;
    if (!GetU32(attrs, "POSITION", &posIdx)) {
        Diag(s, ImportSeverity::Error, kCodeAttributeMissing, LocJoin(loc, "POSITION"),
             "POSITION accessor required");
        return false;
    }
    GetU32(attrs, "NORMAL", &nrmIdx);
    GetU32(attrs, "TANGENT", &tanIdx);
    GetU32(attrs, "TEXCOORD_0", &uvIdx);

    std::vector<float> positions, normals, uvs, tangents;
    uint32_t posCount = 0, nrmCount = 0, uvCount = 0, tanCount = 0;
    if (!ResolveFloatAttribute<3>(s, posIdx, &positions, &posCount,
                                  LocJoin(loc, "POSITION"))) return false;
    if (!ResolveFloatAttribute<3>(s, nrmIdx, &normals, &nrmCount,
                                  LocJoin(loc, "NORMAL"))) return false;
    if (!ResolveFloatAttribute<2>(s, uvIdx, &uvs, &uvCount,
                                  LocJoin(loc, "TEXCOORD_0"))) return false;
    if (!ResolveFloatAttribute<4>(s, tanIdx, &tangents, &tanCount,
                                  LocJoin(loc, "TANGENT"))) return false;
    if (nrmCount != 0 && nrmCount != posCount) {
        Diag(s, ImportSeverity::Error, kCodeAttributeCountMismatch, loc,
             "NORMAL count differs from POSITION");
        return false;
    }
    if (uvCount != 0 && uvCount != posCount) {
        Diag(s, ImportSeverity::Error, kCodeAttributeCountMismatch, loc,
             "TEXCOORD_0 count differs from POSITION");
        return false;
    }
    if (tanCount != 0 && tanCount != posCount) {
        Diag(s, ImportSeverity::Error, kCodeAttributeCountMismatch, loc,
             "TANGENT count differs from POSITION");
        return false;
    }

    uint32_t indicesCount = 0;
    std::vector<uint32_t> localIndices;
    if (prim.contains("indices")) {
        uint32_t ia = 0;
        if (!GetU32(prim, "indices", &ia)) {
            Diag(s, ImportSeverity::Error, kCodeAccessorBounds, LocJoin(loc, "indices"),
                 "indices reference invalid");
            return false;
        }
        if (!ResolveIndices(s, ia, &localIndices, &indicesCount,
                            LocJoin(loc, "indices"))) return false;
        for (const uint32_t value : localIndices) {
            if (value >= posCount) {
                Diag(s, ImportSeverity::Error, kCodeAccessorBounds,
                     LocJoin(loc, "indices"), "index value out of vertex range");
                return false;
            }
        }
    } else {
        indicesCount = posCount;
        localIndices.resize(posCount);
        for (uint32_t i = 0; i < posCount; ++i) localIndices[i] = i;
    }

    if (s.totalInstancedPrimitives >= kMaxPrimitives) {
        Diag(s, ImportSeverity::Error, kCodeLimitPrimitives, loc,
             "instanced primitive count exceeds kMaxPrimitives");
        return false;
    }
    if (s.totalInstancedVertices + posCount > kMaxTotalVertices) {
        Diag(s, ImportSeverity::Error, kCodeLimitVertices, loc,
             "total instanced vertex count exceeds kMaxTotalVertices");
        return false;
    }
    s.totalInstancedPrimitives++;

    MeshPrimitiveIR span;
    span.firstIndex = static_cast<uint32_t>(out.indices.size());
    span.indexCount = indicesCount;
    if (prim.contains("material")) {
        uint32_t mi = 0;
        if (!GetU32(prim, "material", &mi) || mi >= out.materials.size()) {
            Diag(s, ImportSeverity::Error, kCodeMaterialRefMissing,
                 LocJoin(loc, "material"), "material reference missing/out of range");
            return false;
        }
        span.materialIndex = mi;
    } else {
        Diag(s, ImportSeverity::Warning, kCodeMaterialUnspecified, loc,
             "primitive has no material");
        span.materialIndex = kMaterialNone;
    }
    std::memcpy(span.sourceTransform, world.m, sizeof(world.m));
    span.nodeName = nodeName;

    const uint32_t vertexBase = static_cast<uint32_t>(out.vertices.size());
    for (uint32_t vi = 0; vi < posCount; ++vi) {
        ImportVertex v;
        float p[3] = {positions[vi * 3], positions[vi * 3 + 1], positions[vi * 3 + 2]};
        float baked[3];
        TransformPoint(world, p, baked);
        v.position[0] = baked[0];
        v.position[1] = baked[1];
        v.position[2] = baked[2];
        if (nrmCount == posCount) {
            float n[3] = {normals[vi * 3], normals[vi * 3 + 1], normals[vi * 3 + 2]};
            float bn[3];
            TransformDirection(world, n, bn);
            const float len = std::sqrt(bn[0] * bn[0] + bn[1] * bn[1] +
                                       bn[2] * bn[2]);
            if (len > 1e-8f) {
                v.normal[0] = bn[0] / len;
                v.normal[1] = bn[1] / len;
                v.normal[2] = bn[2] / len;
                v.flags |= kVertexFlagNormalValid;
            } else {
                Diag(s, ImportSeverity::Warning, kCodeNormalDegenerate, loc,
                     "degenerate normal after transform");
            }
        }
        if (uvCount == posCount) {
            v.uv0[0] = uvs[vi * 2];
            v.uv0[1] = 1.0f - uvs[vi * 2 + 1]; // single conversion point (C-3)
            v.flags |= kVertexFlagUvValid;
        }
        if (tanCount == posCount) {
            v.tangent[0] = tangents[vi * 4];
            v.tangent[1] = tangents[vi * 4 + 1];
            v.tangent[2] = tangents[vi * 4 + 2];
            v.flags |= kVertexFlagTangentValid;
            const float w = tangents[vi * 4 + 3];
            if (w != 1.0f && w != -1.0f) {
                Diag(s, ImportSeverity::Warning, kCodeTangentW, loc,
                     "tangent w not ±1 (handedness ignored in v1)");
            }
        }
        out.vertices.push_back(v);
    }
    for (const uint32_t value : localIndices) {
        out.indices.push_back(vertexBase + value);
    }
    out.primitives.push_back(std::move(span));
    return true;
}

bool TraverseNode(ParserState& s, uint32_t nodeIdx, const MatCM& parentWorld,
                  uint32_t depth) {
    if (depth > kMaxNodeDepth) {
        Diag(s, ImportSeverity::Error, kCodeLimitNodeDepth, Loc("nodes", nodeIdx),
             "node hierarchy depth exceeds kMaxNodeDepth");
        return false;
    }
    const Json& doc = *s.doc;
    if (!doc.contains("nodes") || !doc["nodes"].is_array() ||
        nodeIdx >= CountOf(doc["nodes"])) {
        Diag(s, ImportSeverity::Error, kCodeNodeMeshMissing, Loc("nodes", nodeIdx),
             "node index out of range");
        return false;
    }
    const Json& node = doc["nodes"][nodeIdx];
    if (!node.is_object()) {
        Diag(s, ImportSeverity::Error, kCodeValueInvalid, Loc("nodes", nodeIdx),
             "node entry is not an object");
        return false;
    }
    const std::string loc = Loc("nodes", nodeIdx);
    MatCM local;
    if (node.contains("matrix")) MatFromMatrixField(node, &local, s, loc);
    else MakeTRS(node, &local, s, loc);
    MatCM world;
    MatMultiply(world, parentWorld, local);

    if (node.contains("mesh")) {
        uint32_t meshIdx = 0;
        if (!GetU32(node, "mesh", &meshIdx) || !doc.contains("meshes") ||
            !doc["meshes"].is_array() || meshIdx >= CountOf(doc["meshes"])) {
            Diag(s, ImportSeverity::Error, kCodeNodeMeshMissing,
                 LocJoin(loc, "mesh"), "mesh reference missing/out of range");
            return false;
        }
        const Json& mesh = doc["meshes"][meshIdx];
        if (!mesh.is_object()) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid, Loc("meshes", meshIdx),
                 "mesh is not an object");
            return false;
        }
        const uint32_t primCount =
            CountOf(mesh.contains("primitives") ? mesh["primitives"] : Json());
        for (uint32_t p = 0; p < primCount; ++p) {
            if (!InstantiatePrimitive(s, mesh, p, world, loc)) return false;
        }
    } else {
        Diag(s, ImportSeverity::Warning, kCodeNodeSkipped, loc,
             "non-geometry node skipped (camera/light/empty)");
    }
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            Json tmp;
            tmp["c"] = child;
            uint32_t childIdx = 0;
            if (!GetU32(tmp, "c", &childIdx)) {
                Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                     LocJoin(loc, "children"), "child index invalid");
                return false;
            }
            if (!TraverseNode(s, childIdx, world, depth + 1)) return false;
        }
    }
    return true;
}

bool TraverseScenes(ParserState& s) {
    const Json& doc = *s.doc;
    if (!doc.contains("scenes") || !doc["scenes"].is_array() ||
        CountOf(doc["scenes"]) == 0) {
        Diag(s, ImportSeverity::Error, kCodeMissingField, "scenes",
             "scenes array missing/empty");
        return false;
    }
    uint32_t sceneIdx = 0;
    if (doc.contains("scene")) {
        uint32_t chosen = 0;
        if (!GetU32(doc, "scene", &chosen) || chosen >= CountOf(doc["scenes"])) {
            Diag(s, ImportSeverity::Warning, kCodeSceneIgnored, "scene",
                 "default scene index invalid; falling back to scenes[0]");
            sceneIdx = 0;
        } else {
            sceneIdx = chosen;
        }
    }
    if (CountOf(doc["scenes"]) > 1) {
        Diag(s, ImportSeverity::Warning, kCodeSceneIgnored, "scenes",
             "multiple scenes; only the selected scene is imported");
    }
    const Json& scene = doc["scenes"][sceneIdx];
    if (!scene.is_object() || !scene.contains("nodes") || !scene["nodes"].is_array()) {
        Diag(s, ImportSeverity::Error, kCodeMissingField,
             LocJoin(Loc("scenes", sceneIdx), "nodes"), "scene nodes missing");
        return false;
    }
    MatCM identity;
    for (const auto& n : scene["nodes"]) {
        Json tmp;
        tmp["c"] = n;
        uint32_t rootIdx = 0;
        if (!GetU32(tmp, "c", &rootIdx)) {
            Diag(s, ImportSeverity::Error, kCodeValueInvalid,
                 LocJoin(Loc("scenes", sceneIdx), "nodes"),
                 "scene node index invalid");
            return false;
        }
        if (!TraverseNode(s, rootIdx, identity, 0)) return false;
    }
    return true;
}

} // namespace

ImportResult GltfParser::ParseGlb(std::vector<uint8_t> fileBytes,
                                  const std::string& sourceDir) {
    ImportResult result;
    if (fileBytes.size() > kMaxSourceFileBytes) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeLimitSourceSize;
        d.location = "file";
        d.message = "source exceeds kMaxSourceFileBytes";
        result.diagnostics.push_back(std::move(d));
        return result;
    }
    const GlbReadResult glb = ReadGlb(fileBytes.data(), fileBytes.size());
    result.diagnostics = glb.diagnostics;
    if (!glb.ok) return result;

    Json doc;
    try {
        doc = Json::parse(reinterpret_cast<const char*>(glb.jsonData),
                          reinterpret_cast<const char*>(glb.jsonData) + glb.jsonLength);
    } catch (const std::exception& e) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeJsonParse;
        d.location = "glb.json";
        d.message = std::string("JSON parse failed: ") + e.what();
        result.diagnostics.push_back(std::move(d));
        return result;
    }

    ParserState s;
    s.doc = &doc;
    s.glbBin = glb.hasBin ? glb.bin.data : nullptr;
    s.glbBinSize = glb.hasBin ? glb.bin.length : 0;
    s.glbMode = true;
    s.sourceDir = sourceDir;
    s.out = &result.mesh;
    s.diag = &result.diagnostics;

    ValidateAssetHeader(s);
    ValidateExtensions(s);
    ValidateGlobalCaps(s);
    if (HasError(result.diagnostics)) return result;
    if (!ParseBuffers(s)) return result;
    if (!ParseMaterials(s, true)) return result;
    if (!TraverseScenes(s)) return result;
    result.ok = !HasError(result.diagnostics);
    return result;
}

ImportResult GltfParser::ParseGltfJson(std::vector<uint8_t> fileBytes,
                                       const std::string& sourceDir) {
    ImportResult result;
    if (fileBytes.size() > kMaxSourceFileBytes) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeLimitSourceSize;
        d.location = "file";
        d.message = "source exceeds kMaxSourceFileBytes";
        result.diagnostics.push_back(std::move(d));
        return result;
    }
    Json doc;
    try {
        doc = Json::parse(reinterpret_cast<const char*>(fileBytes.data()),
                          reinterpret_cast<const char*>(fileBytes.data()) + fileBytes.size());
    } catch (const std::exception& e) {
        ImportDiagnostic d;
        d.severity = ImportSeverity::Error;
        d.code = kCodeJsonParse;
        d.location = "gltf.json";
        d.message = std::string("JSON parse failed: ") + e.what();
        result.diagnostics.push_back(std::move(d));
        return result;
    }

    ParserState s;
    s.doc = &doc;
    s.glbMode = false;
    s.sourceDir = sourceDir;
    s.out = &result.mesh;
    s.diag = &result.diagnostics;

    ValidateAssetHeader(s);
    ValidateExtensions(s);
    ValidateGlobalCaps(s);
    if (HasError(result.diagnostics)) return result;
    if (!ParseBuffers(s)) return result;
    if (!ParseMaterials(s, false)) return result;
    if (!TraverseScenes(s)) return result;
    result.ok = !HasError(result.diagnostics);
    return result;
}

} // namespace Engine::AssetImport
