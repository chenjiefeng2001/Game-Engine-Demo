#pragma once

#include "EditorSession.h"
#include "EngineHost.h"

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/RHI/MeshGpuResource.h"
#include "Engine/Core/Scene/Scene.h"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace PixelTest {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace Engine;

inline constexpr uint8_t kBackgroundR = 13;
inline constexpr uint8_t kBackgroundG = 13;
inline constexpr uint8_t kBackgroundB = 18;

inline const float kForward[3] = {0.61237244f, -0.5f, 0.61237244f};
inline const float kRight[3] = {-0.70710678f, 0.0f, 0.70710678f};
inline const float kUp[3] = {0.35355339f, 0.86602540f, 0.35355339f};

struct Rgba {
    int r = 0, g = 0, b = 0, a = 0;
};

struct Probe {
    int x = 0;
    int y = 0; // GL bottom-left origin
};

struct PixelFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    bool renderGraph = false;

    Rgba At(int x, int y) const {
        const size_t i =
            (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4u;
        return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
    }
};

inline bool IsBackground(const Rgba& p, int tolerance = 0) {
    return std::abs(p.r - kBackgroundR) <= tolerance &&
           std::abs(p.g - kBackgroundG) <= tolerance &&
           std::abs(p.b - kBackgroundB) <= tolerance;
}

inline bool Close(const Rgba& a, const Rgba& b, int tolerance, bool alpha) {
    return std::abs(a.r - b.r) <= tolerance &&
           std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance &&
           (!alpha || std::abs(a.a - b.a) <= tolerance);
}

inline int CountNonBackground(const PixelFrame& f, int tolerance = 3) {
    int count = 0;
    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            if (!IsBackground(f.At(x, y), tolerance)) ++count;
        }
    }
    return count;
}

inline float SrgbDecode(uint8_t v) {
    const float c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f
                        : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline Rgba ExpectedAmbient(float linR, float linG, float linB, float alpha) {
    auto q = [](float v) {
        return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) *
                                            255.0f));
    };
    return {q(0.15f * linR), q(0.15f * linG), q(0.20f * linB), q(alpha)};
}

inline std::array<int, 2> ProjectAt(float u, float v, int width, int height) {
    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);
    const float tanHalf = std::tan(glm::radians(30.0f));
    const float ndcX = u / (10.0f * tanHalf * aspect);
    const float ndcY = v / (10.0f * tanHalf);
    return {static_cast<int>(std::lround((0.5f + 0.5f * ndcX) * width)),
            static_cast<int>(std::lround((0.5f + 0.5f * ndcY) * height))};
}

inline std::vector<Probe> SingleQuadProbes(int w, int h) {
    std::vector<Probe> out;
    for (float v : {-3.2f, 3.2f}) {
        for (float u : {-3.2f, 3.2f}) {
            const auto p = ProjectAt(u, v, w, h);
            out.push_back({p[0], p[1]});
        }
    }
    return out;
}

inline std::vector<Probe> MultiQuadProbes(int w, int h) {
    std::vector<Probe> out;
    for (float u : {-3.0f, 3.0f}) {
        const auto p = ProjectAt(u, 0.0f, w, h);
        out.push_back({p[0], p[1]});
    }
    return out;
}

inline std::vector<Probe> BackgroundProbes(int w, int h) {
    return {{5, 5}, {w - 6, 5}, {5, h - 6}, {w - 6, h - 6}};
}

struct QuadVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

inline std::vector<QuadVertex> MakeQuad(float uCenter, float vCenter,
                                        float half) {
    std::vector<QuadVertex> out;
    const float us[4] = {-half, half, half, -half};
    const float vs[4] = {-half, -half, half, half};
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        QuadVertex vx{};
        for (int k = 0; k < 3; ++k) {
            vx.position[k] = kRight[k] * (uCenter + us[i]) +
                             kUp[k] * (vCenter + vs[i]);
            vx.normal[k] = -kForward[k];
        }
        vx.uv[0] = uvs[i][0];
        vx.uv[1] = uvs[i][1];
        out.push_back(vx);
    }
    return out;
}

inline std::vector<uint8_t> MakePng(int width, int height, int channels,
                                    uint8_t r, uint8_t g, uint8_t b,
                                    uint8_t a = 255) {
    uint32_t crcTable[256];
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    auto crc32 = [&](const uint8_t* d, size_t n) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i)
            c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    auto push32BE = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x >> 24 & 0xFF);
        v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF);
        v.push_back(x & 0xFF);
    };
    auto chunk = [&](std::vector<uint8_t>& png, const char* type,
                     const std::vector<uint8_t>& data) {
        push32BE(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> crcIn;
        for (int k = 0; k < 4; ++k) crcIn.push_back(uint8_t(type[k]));
        crcIn.insert(crcIn.end(), data.begin(), data.end());
        for (int k = 0; k < 4; ++k) png.push_back(static_cast<uint8_t>(type[k]));
        png.insert(png.end(), data.begin(), data.end());
        push32BE(png, crc32(crcIn.data(), crcIn.size()));
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            if (channels == 4) raw.push_back(a);
        }
    }
    std::vector<uint8_t> idat;
    idat.push_back(0x78);
    idat.push_back(0x01);
    const uint16_t len = static_cast<uint16_t>(raw.size());
    idat.push_back(0x01);
    idat.push_back(len & 0xFF);
    idat.push_back(len >> 8);
    idat.push_back(~len & 0xFF);
    idat.push_back((~len >> 8) & 0xFF);
    idat.insert(idat.end(), raw.begin(), raw.end());
    uint32_t a1 = 1, a2 = 0;
    for (uint8_t v : raw) {
        a1 = (a1 + v) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    push32BE(idat, (a2 << 16) | a1);
    std::vector<uint8_t> png;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    push32BE(ihdr, static_cast<uint32_t>(width));
    push32BE(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(static_cast<uint8_t>(channels == 4 ? 6 : 2));
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", idat);
    chunk(png, "IEND", {});
    return png;
}

inline std::string ObjForQuads(const std::vector<std::vector<QuadVertex>>& quads,
                               const std::vector<std::string>& materials,
                               const std::string& mtlName) {
    std::string obj = "mtllib " + mtlName + "\n";
    int vertexBase = 1;
    for (size_t q = 0; q < quads.size(); ++q) {
        for (const QuadVertex& v : quads[q]) {
            obj += "v " + std::to_string(v.position[0]) + " " +
                   std::to_string(v.position[1]) + " " +
                   std::to_string(v.position[2]) + "\n";
        }
        for (const QuadVertex& v : quads[q]) {
            obj += "vt " + std::to_string(v.uv[0]) + " " +
                   std::to_string(v.uv[1]) + "\n";
        }
        for (const QuadVertex& v : quads[q]) {
            obj += "vn " + std::to_string(v.normal[0]) + " " +
                   std::to_string(v.normal[1]) + " " +
                   std::to_string(v.normal[2]) + "\n";
        }
        obj += "usemtl " + materials[q] + "\n";
        const int b = vertexBase;
        obj += "f " + std::to_string(b) + "/" + std::to_string(b) + "/" +
               std::to_string(b) + " " +
               std::to_string(b + 1) + "/" + std::to_string(b + 1) + "/" +
               std::to_string(b + 1) + " " +
               std::to_string(b + 2) + "/" + std::to_string(b + 2) + "/" +
               std::to_string(b + 2) + "\n";
        obj += "f " + std::to_string(b) + "/" + std::to_string(b) + "/" +
               std::to_string(b) + " " +
               std::to_string(b + 2) + "/" + std::to_string(b + 2) + "/" +
               std::to_string(b + 2) + " " +
               std::to_string(b + 3) + "/" + std::to_string(b + 3) + "/" +
               std::to_string(b + 3) + "\n";
        vertexBase += 4;
    }
    return obj;
}

inline void AppendLe(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

inline void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    AppendLe(out, &v, 4);
}

inline size_t AppendAligned(std::vector<uint8_t>& out,
                            const std::vector<uint8_t>& bytes) {
    while (out.size() % 4 != 0) out.push_back(0);
    const size_t offset = out.size();
    out.insert(out.end(), bytes.begin(), bytes.end());
    return offset;
}

inline std::vector<uint8_t> MakeGlbQuad(const std::vector<uint8_t>& colorPng,
                                        const std::vector<uint8_t>& normalPng) {
    const auto quad = MakeQuad(0.0f, 0.0f, 4.0f);
    std::vector<uint8_t> bin;
    std::vector<float> positions, normals, uvs;
    for (const QuadVertex& v : quad) {
        positions.insert(positions.end(), v.position, v.position + 3);
        normals.insert(normals.end(), v.normal, v.normal + 3);
        uvs.insert(uvs.end(), v.uv, v.uv + 2);
    }
    const size_t posOff = AppendAligned(
        bin, std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(positions.data()),
                                  reinterpret_cast<const uint8_t*>(positions.data()) +
                                      positions.size() * sizeof(float)));
    const size_t nrmOff = AppendAligned(
        bin, std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(normals.data()),
                                  reinterpret_cast<const uint8_t*>(normals.data()) +
                                      normals.size() * sizeof(float)));
    const size_t uvOff = AppendAligned(
        bin, std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(uvs.data()),
                                  reinterpret_cast<const uint8_t*>(uvs.data()) +
                                      uvs.size() * sizeof(float)));
    const std::vector<uint16_t> indices = {0, 1, 2, 0, 2, 3};
    const std::vector<uint8_t> indexBytes(
        reinterpret_cast<const uint8_t*>(indices.data()),
        reinterpret_cast<const uint8_t*>(indices.data()) +
            indices.size() * sizeof(uint16_t));
    const size_t idxOff = AppendAligned(bin, indexBytes);
    const size_t colorOff = AppendAligned(bin, colorPng);
    const size_t normalOff = AppendAligned(bin, normalPng);

    Json doc;
    doc["asset"] = {{"version", "2.0"}};
    doc["buffers"] = Json::array({Json::object(
        {{"byteLength", static_cast<uint64_t>(bin.size())}})});
    auto view = [](size_t off, size_t len) {
        return Json{{"buffer", 0},
                    {"byteOffset", static_cast<uint64_t>(off)},
                    {"byteLength", static_cast<uint64_t>(len)}};
    };
    doc["bufferViews"] = Json::array(
        {view(posOff, positions.size() * 4),
         view(nrmOff, normals.size() * 4), view(uvOff, uvs.size() * 4),
         view(idxOff, indexBytes.size()),
         view(colorOff, colorPng.size()), view(normalOff, normalPng.size())});
    Json posAcc = {{"bufferView", 0},
                   {"componentType", 5126},
                   {"count", 4},
                   {"type", "VEC3"},
                   {"min", {-6.0f, -6.0f, -6.0f}},
                   {"max", {6.0f, 6.0f, 6.0f}}};
    doc["accessors"] = Json::array(
        {posAcc,
         Json{{"bufferView", 1},
              {"componentType", 5126},
              {"count", 4},
              {"type", "VEC3"}},
         Json{{"bufferView", 2},
              {"componentType", 5126},
              {"count", 4},
              {"type", "VEC2"}},
         Json{{"bufferView", 3},
              {"componentType", 5123},
              {"count", 6},
              {"type", "SCALAR"}}});
    doc["images"] = Json::array(
        {Json{{"bufferView", 4}, {"mimeType", "image/png"}},
         Json{{"bufferView", 5}, {"mimeType", "image/png"}}});
    doc["textures"] = Json::array(
        {Json::object({{"source", 0}}), Json::object({{"source", 1}})});
    doc["materials"] = Json::array(
        {Json{{"pbrMetallicRoughness",
               {{"baseColorFactor", {1.0f, 1.0f, 1.0f, 1.0f}},
                {"baseColorTexture", {{"index", 0}}},
                {"metallicFactor", 0.0f},
                {"roughnessFactor", 1.0f}}},
              {"normalTexture", {{"index", 1}}},
              {"alphaMode", "OPAQUE"}}});
    doc["meshes"] = Json::array(
        {Json{{"primitives",
               Json::array({Json{{"attributes",
                                  {{"POSITION", 0},
                                   {"NORMAL", 1},
                                   {"TEXCOORD_0", 2}}},
                                 {"indices", 3},
                                 {"material", 0},
                                 {"mode", 4}}})}}});
    doc["nodes"] = Json::array({Json{{"mesh", 0}, {"name", "Quad"}}});
    doc["scenes"] = Json::array({Json{{"nodes", {0}}}});
    doc["scene"] = 0;
    const std::string json = doc.dump();

    std::vector<uint8_t> glb;
    auto pushChunk = [&](uint32_t type, const uint8_t* data, size_t len) {
        const size_t padded = (len + 3) / 4 * 4;
        AppendU32(glb, static_cast<uint32_t>(padded));
        AppendU32(glb, type);
        glb.insert(glb.end(), data, data + len);
        for (size_t i = len; i < padded; ++i) glb.push_back(0);
    };
    const uint32_t magic = 0x46546C67u;
    AppendU32(glb, magic);
    AppendU32(glb, 2);
    const size_t jsonPadded = (json.size() + 3) / 4 * 4;
    const size_t binPadded = (bin.size() + 3) / 4 * 4;
    AppendU32(glb, static_cast<uint32_t>(12 + 8 + jsonPadded + 8 + binPadded));
    pushChunk(0x4E4F534Au,
              reinterpret_cast<const uint8_t*>(json.data()), json.size());
    pushChunk(0x004E4942u, bin.data(), bin.size());
    return glb;
}

struct PixelProject {
    fs::path root;
    Engine::Content::ProjectRoot projectRoot;

    PixelProject() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("gd_g5_pixel_" + std::to_string(stamp));
        fs::create_directories(root / "assets");
        WriteText("assets/manifest.json", R"({"version":1,"assets":[]})");
        WriteText("assets/Main.scene",
                  R"({"format":"engine.scene","version":1,"entities":[]})");
        Engine::Content::PathError err = Engine::Content::PathError::None;
        std::string diag;
        if (!Engine::Content::ProjectRoot::Create(root.string(),
                                                  &projectRoot, &err, &diag))
            throw std::runtime_error("ProjectRoot: " + diag);
    }
    ~PixelProject() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void WriteText(const std::string& rel, const std::string& text) {
        WriteBytes(rel, std::vector<uint8_t>(text.begin(), text.end()));
    }
    void WriteBytes(const std::string& rel, const std::vector<uint8_t>& bytes) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
};

struct AssetRef {
    int32_t index = -1;
    std::string path;
    std::string guidHex;
};

inline std::string AssetPathAt(EditorSessionHandle s, int32_t index) {
    char buf[1024] = {};
    if (EditorSession_GetAssetPath(s, index, buf, sizeof(buf)) < 0) return {};
    return buf;
}

inline std::string AssetGuidAt(EditorSessionHandle s, int32_t index) {
    char buf[64] = {};
    if (EditorSession_GetAssetGuid(s, index, buf, sizeof(buf)) < 0) return {};
    return buf;
}

inline std::vector<AssetRef> ListAssets(EditorSessionHandle s) {
    std::vector<AssetRef> out;
    const int32_t count = EditorSession_GetAssetCount(s);
    for (int32_t i = 0; i < count; ++i) {
        AssetRef a;
        a.index = i;
        a.path = AssetPathAt(s, i);
        a.guidHex = AssetGuidAt(s, i);
        out.push_back(std::move(a));
    }
    return out;
}

struct Session {
    EditorSessionHandle handle = nullptr;

    bool Open(const fs::path& root) {
        handle = EditorSession_Create();
        if (!handle) return false;
        return EditorSession_OpenProjectRooted(
                   handle, root.string().c_str(), "assets/manifest.json",
                   "assets/Main.scene") == 1;
    }
    ~Session() {
        if (handle) EditorSession_Destroy(handle);
    }
    bool Save() { return EditorSession_SaveProject(handle) == 1; }
    void SetSingleMeshGuid(const std::string& guidHex) {
        auto* native = static_cast<editor_bridge::EditorSession*>(handle);
        Engine::Scene* scene = native->GetEditScene();
        if (!scene) throw std::runtime_error("no edit scene");
        std::shared_ptr<GameObject> object;
        if (scene->GetObjects().empty()) {
            object = std::make_shared<GameObject>("PixelMesh");
            scene->AddObject(object);
        } else {
            object = scene->GetObjects()[0];
        }
        auto* mc = object->GetComponent<MeshComponent>();
        if (!mc) mc = object->AddComponent<MeshComponent>();
        mc->SetMeshGuid(ResourceGUID::FromHex(guidHex));
        mc->m_Visible = true;
    }
    void ClearMeshGuid() {
        auto* native = static_cast<editor_bridge::EditorSession*>(handle);
        Engine::Scene* scene = native->GetEditScene();
        if (scene && !scene->GetObjects().empty()) {
            if (auto* mc =
                    scene->GetObjects()[0]->GetComponent<MeshComponent>())
                mc->SetMeshGuid(ResourceGUID::Null);
        }
    }
};

struct ImportedModel {
    int32_t index = -1;
    std::string guidHex;
    std::string meshPath;
};

inline ImportedModel ImportModel(EditorSessionHandle session,
                                 const std::string& relPath) {
    ImportedModel m;
    m.index = EditorSession_ImportAsset(session, relPath.c_str(), 2);
    if (m.index < 0) return m;
    m.guidHex = AssetGuidAt(session, m.index);
    m.meshPath = AssetPathAt(session, m.index);
    return m;
}

inline int64_t CurrentRevision(const fs::path& root) {
    std::ifstream f(root / ".engine-content" / "current");
    if (!f) return -1;
    Json j;
    f >> j;
    return j.value("revision", -1);
}

inline std::string ReadFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

inline Json ReadJson(const fs::path& path) {
    std::ifstream f(path);
    Json j;
    f >> j;
    return j;
}

inline Json TextureSidecar(const PixelProject& project,
                           const std::string& texPath) {
    return ReadJson(project.root / (texPath + ".tex.meta.json"));
}

class PixelHost {
public:
    PixelHost() = default;
    ~PixelHost() { Close(); }

    bool Open(const fs::path& root) {
        RuntimeResult result{};
        host_ = EngineHost_Create(&result);
        if (!host_) return false;
        if (EngineHost_OpenProjectRooted(host_, root.string().c_str(),
                                         "assets/manifest.json",
                                         "assets/Main.scene", &result) != 0) {
            error_ = LastDiagnostic();
            return false;
        }
        return true;
    }

    bool Pump(bool renderGraph, PixelFrame* out) {
        if (!host_) return false;
        EngineHost_SetProductionUseRenderGraph(host_, renderGraph ? 1 : 0);
        RuntimeResult result{};
        if (EngineHost_PumpOneFrame(host_, 0.016f, &result) != 0) {
            error_ = LastDiagnostic();
            return false;
        }
        auto* native = static_cast<editor_bridge::EngineHost*>(host_);
        PixelFrame frame;
        frame.width = native->PresentationTargetWidth();
        frame.height = native->PresentationTargetHeight();
        if (frame.width <= 0 || frame.height <= 0) return false;
        const int32_t read = native->ReadPresentationPixels(frame.rgba);
        const size_t expected =
            static_cast<size_t>(frame.width) * frame.height * 4u;
        if (read <= 0 || frame.rgba.size() != expected) return false;
        frame.renderGraph = native->LastProductionUsedRenderGraphForTesting();
        *out = std::move(frame);
        return true;
    }

    bool QueryMesh(const std::string& guidHex, int32_t* status) const {
        uint64_t identity = 0, digest = 0;
        return QueryMeshKey(guidHex, status, &identity, &digest);
    }

    bool QueryMeshKey(const std::string& guidHex, int32_t* status,
                      uint64_t* identity, uint64_t* digest) const {
        if (!host_) return false;
        auto* native = static_cast<editor_bridge::EngineHost*>(host_);
        return native->QueryRuntimeMeshForTesting(
            guidHex.c_str(), status, identity, digest);
    }

    std::string RuntimeError(const std::string& guidHex) const {
        if (!host_) return {};
        char buf[2048] = {};
        if (!static_cast<editor_bridge::EngineHost*>(host_)
                 ->QueryRuntimeMeshErrorForTesting(guidHex.c_str(), buf,
                                                   sizeof(buf)))
            return {};
        return buf;
    }

    size_t GpuMeshCount() const {
        return host_ ? static_cast<editor_bridge::EngineHost*>(host_)
                           ->GpuMeshCountForTesting()
                     : 0;
    }

    void SetGpuUploadFailure(int stage) {
        if (!host_) return;
        static_cast<editor_bridge::EngineHost*>(host_)
            ->SetMeshGpuUploadFailureStageForTesting(stage);
    }

    void Close() {
        if (!host_) return;
        RuntimeResult result{};
        (void)EngineHost_Shutdown(host_, &result);
        EngineHost_Destroy(host_);
        host_ = nullptr;
    }

    const std::string& Error() const { return error_; }

private:
    std::string LastDiagnostic() {
        if (!host_) return {};
        char buf[1024] = {};
        EngineHost_GetLastDiagnostic(host_, buf, sizeof(buf));
        return buf;
    }
    EngineHostHandle host_ = nullptr;
    std::string error_;
};

} // namespace PixelTest
