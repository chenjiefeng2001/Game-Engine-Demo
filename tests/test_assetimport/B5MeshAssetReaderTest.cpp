#include "TestSupport.h"

#include "AssetImport/MeshAssetReader.h"
#include "AssetImport/MeshAssetWriter.h"
#include "AssetImport/ObjParser.h"
#include "AssetImport/GltfParser.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <functional>

#include <cmath>
#include <cstring>
#include <string>

namespace {

using namespace AssetImportTest;
using namespace Engine::AssetImport;

using Json = nlohmann::json;

std::vector<uint8_t> ReadViaWriter(const MeshImportIR& ir,
                                   MeshAssetReadResult* out) {
    const std::string serialized = SerializeMeshAssetJson(ir);
    *out = ReadMeshAssetJson(ToBytes(serialized));
    return ToBytes(serialized);
}

void ExpectSameMesh(const MeshImportIR& ir, const MeshAsset& asset) {
    ASSERT_EQ(asset.vertices.size(), ir.vertices.size());
    for (size_t i = 0; i < ir.vertices.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            EXPECT_FLOAT_EQ(asset.vertices[i].position[k],
                            ir.vertices[i].position[k]);
            EXPECT_FLOAT_EQ(asset.vertices[i].normal[k],
                            ir.vertices[i].normal[k]);
            EXPECT_FLOAT_EQ(asset.vertices[i].tangent[k],
                            ir.vertices[i].tangent[k]);
        }
        for (int k = 0; k < 2; ++k)
            EXPECT_FLOAT_EQ(asset.vertices[i].uv0[k], ir.vertices[i].uv0[k]);
        EXPECT_EQ(asset.vertices[i].flags, ir.vertices[i].flags);
    }
    ASSERT_EQ(asset.indices.size(), ir.indices.size());
    EXPECT_TRUE(asset.indices == ir.indices);
    ASSERT_EQ(asset.primitives.size(), ir.primitives.size());
    for (size_t i = 0; i < ir.primitives.size(); ++i) {
        EXPECT_EQ(asset.primitives[i].firstIndex, ir.primitives[i].firstIndex);
        EXPECT_EQ(asset.primitives[i].indexCount, ir.primitives[i].indexCount);
        EXPECT_EQ(asset.primitives[i].materialIndex,
                  ir.primitives[i].materialIndex);
        EXPECT_EQ(asset.primitives[i].nodeName, ir.primitives[i].nodeName);
        for (int k = 0; k < 16; ++k) {
            EXPECT_FLOAT_EQ(asset.primitives[i].sourceTransform[k],
                            ir.primitives[i].sourceTransform[k]);
        }
    }
    ASSERT_EQ(asset.materialNames.size(), ir.materials.size());
    for (size_t i = 0; i < ir.materials.size(); ++i)
        EXPECT_EQ(asset.materialNames[i], ir.materials[i].name);
    ASSERT_EQ(asset.images.size(), ir.images.size());
    for (size_t i = 0; i < ir.images.size(); ++i) {
        EXPECT_EQ(asset.images[i].uri, ir.images[i].uri);
        EXPECT_EQ(asset.images[i].mimeType, ir.images[i].mimeType);
    }
    EXPECT_EQ(asset.sourceFile, ir.sourceFile);
}

// ── round-trips from real parser outputs ───────────────────────────────

TEST(MeshAssetReaderTest, ObjSinglePrimitiveRoundTrip) {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 1 1\nvn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n"),
        "");
    ASSERT_TRUE(parsed.ok);
    MeshAssetReadResult read;
    ReadViaWriter(parsed.mesh, &read);
    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_EQ(read.mesh.formatVersion, 1u);
    ExpectSameMesh(parsed.mesh, read.mesh);
    // flags are preserved independently of Vertex3D
    EXPECT_NE(read.mesh.vertices[0].flags & kVertexFlagNormalValid, 0u);
    EXPECT_NE(read.mesh.vertices[0].flags & kVertexFlagUvValid, 0u);
}

TEST(MeshAssetReaderTest, ObjMultiPrimitiveSpansAndMaterials) {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("mtllib lib.mtl\n"
                "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
                "usemtl red\nf 1 2 3\n"
                "usemtl blue\nf 1 2 4\n"),
        "");
    // mtllib missing -> fails? No: MTL_LIB_MISSING is warning, synthetic
    // materials created for used names.
    ASSERT_TRUE(parsed.ok) << (parsed.diagnostics.empty()
                                   ? ""
                                   : parsed.diagnostics[0].message);
    MeshAssetReadResult read;
    ReadViaWriter(parsed.mesh, &read);
    ASSERT_TRUE(read.ok) << read.error;
    ExpectSameMesh(parsed.mesh, read.mesh);
    ASSERT_EQ(read.mesh.primitives.size(), 2u);
    EXPECT_EQ(read.mesh.primitives[0].firstIndex, 0u);
    EXPECT_EQ(read.mesh.primitives[1].firstIndex, 3u);
    EXPECT_EQ(read.mesh.materialNames.size(), 2u);
}

TEST(MeshAssetReaderTest, GltfTriangleRoundTrip) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult parsed =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(parsed.ok);
    MeshAssetReadResult read;
    ReadViaWriter(parsed.mesh, &read);
    ASSERT_TRUE(read.ok) << read.error;
    ExpectSameMesh(parsed.mesh, read.mesh);
}

TEST(MeshAssetReaderTest, GlbEmbeddedImageMetadataRoundTrip) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, true);
    built.doc["images"] = Json::array(
        {Json::object({{"bufferView", built.doc["bufferViews"].size()},
                       {"mimeType", "image/png"}})});
    built.doc["bufferViews"].push_back(Json::object(
        {{"buffer", 0},
         {"byteOffset", static_cast<uint32_t>(built.bin.size())},
         {"byteLength", 16}}));
    built.doc["textures"] = Json::array({Json::object({{"source", 0}})});
    built.doc["materials"][0]["pbrMetallicRoughness"] =
        Json::object({{"baseColorTexture", Json::object({{"index", 0}})}});
    std::vector<uint8_t> binAndImage = built.bin;
    for (int i = 0; i < 16; ++i) binAndImage.push_back(0xFF);
    const std::vector<uint8_t> glb = MakeGlb(built.doc.dump(), binAndImage);
    const ImportResult parsed = GltfParser::ParseGlb(glb, "");
    ASSERT_TRUE(parsed.ok) << (parsed.diagnostics.empty()
                                   ? ""
                                   : parsed.diagnostics[0].message);
    ASSERT_EQ(parsed.mesh.images.size(), 1u);
    MeshAssetReadResult read;
    ReadViaWriter(parsed.mesh, &read);
    ASSERT_TRUE(read.ok) << read.error;
    ExpectSameMesh(parsed.mesh, read.mesh);
    EXPECT_EQ(read.mesh.images[0].mimeType, std::string("image/png"));
}

TEST(MeshAssetReaderTest, EmptyMeshIsStructurallyValid) {
    MeshImportIR empty;
    empty.sourceFile = "empty";
    MeshAssetReadResult read;
    ReadViaWriter(empty, &read);
    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_TRUE(read.mesh.vertices.empty());
    EXPECT_TRUE(read.mesh.indices.empty());
    EXPECT_TRUE(read.mesh.primitives.empty());
}

TEST(MeshAssetReaderTest, DeterministicRepeatedRead) {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "");
    ASSERT_TRUE(parsed.ok);
    const std::string bytes = SerializeMeshAssetJson(parsed.mesh);
    const MeshAssetReadResult a = ReadMeshAssetJson(ToBytes(bytes));
    const MeshAssetReadResult b = ReadMeshAssetJson(ToBytes(bytes));
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    ASSERT_EQ(a.mesh.vertices.size(), b.mesh.vertices.size());
    for (size_t i = 0; i < a.mesh.vertices.size(); ++i) {
        EXPECT_EQ(std::memcmp(&a.mesh.vertices[i], &b.mesh.vertices[i],
                              sizeof(MeshAssetVertex)),
                  0);
    }
    EXPECT_TRUE(a.mesh.indices == b.mesh.indices);
    EXPECT_EQ(a.mesh.primitives.size(), b.mesh.primitives.size());
}

// ── negative tests (fail-closed, no partial mesh) ──────────────────────

namespace {

std::vector<uint8_t> Mutate(const std::string& base,
                            const std::function<void(Json&)>& change) {
    Json doc = Json::parse(base);
    change(doc);
    return ToBytes(doc.dump());
}

void ExpectFail(const std::vector<uint8_t>& bytes, const char* code) {
    const MeshAssetReadResult r = ReadMeshAssetJson(bytes);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find(code), std::string::npos) << r.error;
    // no partially usable mesh leaked
    EXPECT_TRUE(r.mesh.vertices.empty());
    EXPECT_TRUE(r.mesh.indices.empty());
    EXPECT_TRUE(r.mesh.primitives.empty());
}

std::string BaseTriangleJson() {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "");
    return SerializeMeshAssetJson(parsed.mesh);
}

} // namespace

TEST(MeshAssetReaderNegativeTest, EmptyBytes) {
    ExpectFail({}, kCodeMeshAssetParse);
}

TEST(MeshAssetReaderNegativeTest, MalformedJson) {
    ExpectFail(ToBytes("{ nope ]"), kCodeMeshAssetParse);
}

TEST(MeshAssetReaderNegativeTest, WrongFormat) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["format"] = "material"; }),
               kCodeMeshAssetParse);
}

TEST(MeshAssetReaderNegativeTest, UnsupportedVersion) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["formatVersion"] = 2; }),
               kCodeMeshAssetUnsupportedVersion);
}

TEST(MeshAssetReaderNegativeTest, MissingRequiredField) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j.erase("vertices"); }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, UnknownTopLevelField) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["extra"] = 1; }),
               kCodeMeshAssetParse);
}

TEST(MeshAssetReaderNegativeTest, NonFiniteFloat) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["vertices"][0]["position"][0] = 1e40; // overflows float
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, WrongFloatType) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["vertices"][0]["position"][0] = "nan";
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, UnknownVertexFlagBits) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["vertices"][0]["flags"] = 8; }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, IndexOutOfRange) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["indices"][0] = 99; }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, SpanExceedsIndexRange) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["primitives"][0]["indexCount"] = 999;
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, SpanZeroCount) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["primitives"][0]["indexCount"] = 0;
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, MaterialIndexOutOfRange) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["primitives"][0]["materialIndex"] = 3;
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, TransformWrongLength) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["primitives"][0]["sourceTransform"] =
                       Json::array({1, 0, 0});
               }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, IndicesNotTriangles) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) { j["indices"].erase(2); }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, MaterialNameWrongType) {
    const std::string base = BaseTriangleJson();
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("mtllib x.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl m\nf 1 2 3\n"),
        "");
    const std::string withMat = SerializeMeshAssetJson(parsed.mesh);
    ExpectFail(Mutate(withMat, [](Json& j) { j["materialNames"][0] = 5; }),
               kCodeMeshAssetInvalid);
}

TEST(MeshAssetReaderNegativeTest, ImageMissingMimeType) {
    const std::string base = BaseTriangleJson();
    ExpectFail(Mutate(base, [](Json& j) {
                   j["imageUris"] = Json::array(
                       {Json::object({{"uri", "a.png"}})});
               }),
               kCodeMeshAssetInvalid);
}

} // namespace
