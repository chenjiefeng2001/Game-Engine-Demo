#pragma once

#include "AssetImport/GltfParser.h"
#include "AssetImport/ImportDiagnostic.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace AssetImportTest {

using Json = nlohmann::json;

inline void AppendLE(std::vector<uint8_t>& out, const void* v, size_t n) {
    const auto* p = static_cast<const uint8_t*>(v);
    for (size_t i = 0; i < n; ++i) out.push_back(p[i]);
}

inline void AppendFloat(std::vector<uint8_t>& out, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    AppendLE(out, &u, 4);
}

inline void AppendU16(std::vector<uint8_t>& out, uint16_t u) { AppendLE(out, &u, 2); }

inline void AppendU32(std::vector<uint8_t>& out, uint32_t u) { AppendLE(out, &u, 4); }

struct TriangleData
{
    float positions[9] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    float normals[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    float uvs[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f};
    float tangents[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    uint16_t indices[3] = {0, 1, 2};
    uint32_t positionCount = 3;
    uint32_t indexCount = 3;
    bool hasNormals = true;
    bool hasUv = true;
    bool hasTangents = false;
    uint32_t normalCount = 3; // decoupled for mismatch tests
    uint32_t uvCount = 3;
    uint32_t tangentCount = 3;
    uint32_t indexComponent = 5123; // 5121 / 5123 / 5125
    uint32_t mode = 4;
    bool hasIndices = true;
};

struct BuiltDoc
{
    std::string json;
    std::vector<uint8_t> bin;
    Json doc;
};

// Packs positions/normals/uv/indices into one buffer, appends
// buffers/bufferViews/accessors/meshes/primitives/scenes/nodes/asset.
inline BuiltDoc MakeTriangleGltf(const TriangleData& d,
                                 bool embeddedBuffer /*GLB*/,   // buffer[0] no uri
                                 uint32_t materialCountPerPrimitiveOffset = 0) {
    BuiltDoc built;
    const uint32_t bpos = 0;
    const uint32_t bnorm = bpos + d.positionCount * 12;
    const uint32_t buv = d.hasNormals ? bnorm + d.positionCount * 12 : bnorm;
    const uint32_t btan = d.hasUv ? buv + d.uvCount * 8 : buv;
    const uint32_t bindices = d.hasTangents ? btan + d.tangentCount * 16 : btan;
    for (uint32_t i = 0; i < d.positionCount; ++i)
        for (int k = 0; k < 3; ++k) AppendFloat(built.bin, d.positions[i * 3 + k]);
    if (d.hasNormals)
        for (uint32_t i = 0; i < d.normalCount; ++i)
            for (int k = 0; k < 3; ++k) AppendFloat(built.bin, d.normals[i * 3 + k]);
    if (d.hasUv)
        for (uint32_t i = 0; i < d.uvCount; ++i)
            for (int k = 0; k < 2; ++k) AppendFloat(built.bin, d.uvs[i * 2 + k]);
    if (d.hasTangents)
        for (uint32_t i = 0; i < d.tangentCount; ++i)
            for (int k = 0; k < 4; ++k) AppendFloat(built.bin, d.tangents[i * 4 + k]);
    const uint32_t indicesStart = bindices;
    for (uint32_t i = 0; i < d.indexCount; ++i) {
        if (d.indexComponent == 5121) AppendLE(built.bin, &d.indices[i], 1);
        else if (d.indexComponent == 5123) AppendU16(built.bin, d.indices[i]);
        else AppendU32(built.bin, static_cast<uint32_t>(d.indices[i]));
    }
    uint32_t indexByte = 1;
    if (d.indexComponent == 5123) indexByte = 2;
    else if (d.indexComponent == 5125) indexByte = 4;

    Json& doc = built.doc;
    doc["asset"] = {{"version", "2.0"}, {"generator", "AssetImportTest"}};
    if (embeddedBuffer) {
        doc["buffers"] = Json::array({Json::object({
            {"byteLength", bindices + indexByte * d.indexCount}})});
    } else {
        doc["buffers"] = Json::array({Json::object(
            {{"uri", "tri.bin"},
             {"byteLength", bindices + indexByte * d.indexCount}})});
    }
    uint32_t nextView = 0;
    uint32_t nextAcc = 0;
    auto addView = [&](uint32_t byteOffset, uint32_t byteLength,
                       uint32_t bufferIdx = 0) -> uint32_t {
        Json v = {{"buffer", bufferIdx},
                  {"byteOffset", byteOffset},
                  {"byteLength", byteLength}};
        doc["bufferViews"].push_back(v);
        return nextView++;
    };
    auto addAcc = [&](uint32_t view, uint32_t comp, const char* type,
                      uint32_t cnt) -> uint32_t {
        Json a = {{"bufferView", view}, {"componentType", comp},
                  {"count", cnt}, {"type", type}};
        doc["accessors"].push_back(a);
        return nextAcc++;
    };
    const uint32_t posView = addView(bpos, d.positionCount * 12);
    const uint32_t posAcc = addAcc(posView, 5126, "VEC3", d.positionCount);
    uint32_t nrmAcc = 0;
    if (d.hasNormals) {
        const uint32_t v = addView(bnorm, d.normalCount * 12);
        nrmAcc = addAcc(v, 5126, "VEC3", d.normalCount);
    }
    uint32_t uvAcc = 0;
    if (d.hasUv) {
        const uint32_t v = addView(buv, d.uvCount * 8);
        uvAcc = addAcc(v, 5126, "VEC2", d.uvCount);
    }
    uint32_t tanAcc = 0;
    if (d.hasTangents) {
        const uint32_t v = addView(btan, d.tangentCount * 16);
        tanAcc = addAcc(v, 5126, "VEC4", d.tangentCount);
    }
    const uint32_t idxView =
        d.hasIndices ? addView(bindices, indexByte * d.indexCount) : 0;
    const uint32_t idxAcc =
        d.hasIndices ? addAcc(idxView, d.indexComponent, "SCALAR", d.indexCount) : 0;

    Json prim = {{"mode", d.mode}};
    Json attrs = {{"POSITION", posAcc}};
    if (d.hasNormals) attrs["NORMAL"] = nrmAcc;
    if (d.hasUv) attrs["TEXCOORD_0"] = uvAcc;
    if (d.hasTangents) attrs["TANGENT"] = tanAcc;
    prim["attributes"] = attrs;
    if (d.hasIndices) prim["indices"] = idxAcc;
    prim["material"] = 0 + materialCountPerPrimitiveOffset;
    Json meshJson;
    meshJson["name"] = "Mesh0";
    meshJson["primitives"] = Json::array({prim});
    doc["meshes"] = Json::array({meshJson});
    Json matJson;
    matJson["name"] = "Mat0";
    doc["materials"] = Json::array({matJson});
    Json rootNode;
    rootNode["mesh"] = 0;
    rootNode["name"] = "Root";
    doc["nodes"] = Json::array({rootNode});
    Json scenJson;
    scenJson["nodes"] = Json::array({0});
    doc["scenes"] = Json::array({scenJson});
    doc["scene"] = 0;

    built.json = doc.dump();
    return built;
}

// Wraps JSON+bin into a GLB 2.0 container per spec (magic, version, chunks).
inline void AppendChunk(std::vector<uint8_t>& glb, uint32_t type,
                        const void* data, size_t len) {
    const size_t padded = (len + 3) / 4 * 4;
    AppendU32(glb, static_cast<uint32_t>(padded));
    AppendU32(glb, type);
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) glb.push_back(p[i]);
    for (size_t pad = len; pad < padded; ++pad) glb.push_back(0);
}

inline std::vector<uint8_t> MakeGlb(const std::string& json,
                                    const std::vector<uint8_t>& bin) {
    std::vector<uint8_t> glb;
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2);
    const size_t jsonPadded = json.size() + ((4 - json.size() % 4) % 4);
    const size_t binPadded = bin.size() + ((4 - bin.size() % 4) % 4);
    const uint32_t head =
        static_cast<uint32_t>(12 + (8 + jsonPadded) + (8 + binPadded));
    AppendU32(glb, head);
    AppendChunk(glb, 0x4E4F534Au, json.data(), json.size());
    if (!bin.empty()) AppendChunk(glb, 0x004E4942u, bin.data(), bin.size());
    else AppendChunk(glb, 0x004E4942u, "\0\0\0\0", 4);
    return glb;
}

inline std::vector<uint8_t> ToBytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

class ScopedTempDir
{
public:
    ScopedTempDir() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("gd_assetimport_" + std::to_string(ticks));
        std::filesystem::create_directories(path);
        native = path.string();
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void Write(const char* name, const void* data, size_t size) {
        std::ofstream out(path / name, std::ios::binary);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    std::string Path() const { return native; }

private:
    std::filesystem::path path;
    std::string native;
};

inline bool HasDiag(const Engine::AssetImport::ImportResult& r,
                    const char* code) {
    for (const auto& d : r.diagnostics) {
        if (d.code == code) return true;
    }
    return false;
}

inline bool VerticesEqual(const Engine::AssetImport::MeshImportIR& a,
                          const Engine::AssetImport::MeshImportIR& b) {
    if (a.vertices.size() != b.vertices.size() ||
        a.indices.size() != b.indices.size() ||
        a.primitives.size() != b.primitives.size() ||
        a.materials.size() != b.materials.size() ||
        a.images.size() != b.images.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i) {
        for (int k = 0; k < 3; ++k)
            if (std::memcmp(&a.vertices[i].position[k], &b.vertices[i].position[k], 4) != 0)
                return false;
        for (int k = 0; k < 3; ++k)
            if (std::memcmp(&a.vertices[i].normal[k], &b.vertices[i].normal[k], 4) != 0)
                return false;
        for (int k = 0; k < 2; ++k)
            if (std::memcmp(&a.vertices[i].uv0[k], &b.vertices[i].uv0[k], 4) != 0)
                return false;
        if (a.vertices[i].flags != b.vertices[i].flags) return false;
    }
    return std::memcmp(a.indices.data(), b.indices.data(),
                       a.indices.size() * 4) == 0;
}

} // namespace AssetImportTest
