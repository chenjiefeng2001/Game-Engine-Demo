#include "TestSupport.h"

#include <gtest/gtest.h>

namespace {

using namespace AssetImportTest;
using namespace Engine::AssetImport;

static BuiltDoc MakeDefaultTriangle(bool embedded) {
    TriangleData d;
    return MakeTriangleGltf(d, embedded);
}

TEST(GltfParserTest, MinimalExternalGltfTriangleParses) {
    BuiltDoc built = MakeDefaultTriangle(/*embedded*/ false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());

    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok) << [&] {
        std::string msg;
        for (const auto& dg : result.diagnostics)
            msg += std::string(dg.code) + " @ " + dg.location + " : " + dg.message + "\n";
        return msg;
    }();
    EXPECT_EQ(result.mesh.vertices.size(), 3u);
    ASSERT_EQ(result.mesh.indices.size(), 3u);
    EXPECT_EQ(result.mesh.indices[0], 0u);
    EXPECT_EQ(result.mesh.indices[1], 1u);
    EXPECT_EQ(result.mesh.indices[2], 2u);
    ASSERT_EQ(result.mesh.primitives.size(), 1u);
    EXPECT_EQ(result.mesh.primitives[0].firstIndex, 0u);
    EXPECT_EQ(result.mesh.primitives[0].indexCount, 3u);
    EXPECT_EQ(result.mesh.primitives[0].materialIndex, 0u);
    EXPECT_EQ(result.mesh.primitives[0].nodeName, std::string("nodes[0]"));

    EXPECT_NEAR(result.mesh.vertices[0].position[0], -0.5f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[1].position[0], 0.5f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[2].position[1], 0.5f, 1e-6f);
}

TEST(GltfParserTest, UvFlipSingleConversionPoint) {
    BuiltDoc built = MakeDefaultTriangle(false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    // source (0,0) -> (0,1); (1,0) -> (1,1); (0.5,1) -> (0.5,0)
    EXPECT_NEAR(result.mesh.vertices[0].uv0[1], 1.0f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[1].uv0[1], 1.0f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[2].uv0[1], 0.0f, 1e-6f);
    for (const auto& v : result.mesh.vertices)
        EXPECT_TRUE(v.flags & kVertexFlagUvValid);
}

TEST(GltfParserTest, NormalsPreservedUnderIdentity) {
    BuiltDoc built = MakeDefaultTriangle(false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    for (const auto& v : result.mesh.vertices) {
        EXPECT_TRUE(v.flags & kVertexFlagNormalValid);
        EXPECT_NEAR(v.normal[2], 1.0f, 1e-6f);
    }
}

TEST(GltfParserTest, NodeTransformBakeParentTranslationChildScale) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["nodes"] = Json::array(
        {Json::object({{"translation", Json::array({1.0, 0.0, 0.0})},
                       {"children", Json::array({1})}}),
         Json::object({{"scale", Json::array({2.0, 1.0, 1.0})}, {"mesh", 0}})});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());

    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    // vertex (0.5,0,0) -> child scale (1,0,0) -> parent translate (2,0,0)
    EXPECT_NEAR(result.mesh.vertices[1].position[0], 2.0f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[1].position[1], -0.5f, 1e-6f);
}

TEST(GltfParserTest, NodeRotationQuaternionBakes) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    const double s = std::sqrt(0.5);
    ((*built.doc["nodes"].begin()))["rotation"] =
        Json::array({0.0, 0.0, s, s}); // 90 deg around +Z
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    // (0.5,-0.5,0) rotated 90deg ccw -> (0.5,0.5,0)
    EXPECT_NEAR(result.mesh.vertices[1].position[0], 0.5f, 1e-6f);
    EXPECT_NEAR(result.mesh.vertices[1].position[1], 0.5f, 1e-6f);
}

TEST(GltfParserTest, MultiPrimitiveMaterialSlotsPreserveOrder) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    Json& mesh = built.doc["meshes"][0];
    Json prim2 = Json::object();
    prim2["attributes"] = mesh["primitives"][0]["attributes"];
    prim2["indices"] = mesh["primitives"][0]["indices"];
    prim2["material"] = 1;
    prim2["mode"] = 4;
    mesh["primitives"].push_back(prim2);
    built.doc["materials"].push_back(Json::object({{"name", "Mat1"}}));
    built.json = built.doc.dump();
    ScopedTempDir dir;
    // Primitive 2 reuses same buffer range; indices reference same 3 verts.
    dir.Write("tri.bin", built.bin.data(), built.bin.size());

    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.mesh.primitives.size(), 2u);
    EXPECT_EQ(result.mesh.primitives[0].materialIndex, 0u);
    EXPECT_EQ(result.mesh.primitives[1].materialIndex, 1u);
    EXPECT_EQ(result.mesh.primitives[1].firstIndex, 3u);
    EXPECT_EQ(result.mesh.primitives[1].indexCount, 3u);
    EXPECT_EQ(result.mesh.materials.size(), 2u);
    EXPECT_EQ(result.mesh.materials[1].name, std::string("Mat1"));
}

TEST(GltfParserTest, NonIndexedPrimitiveGeneratesSequentialIndices) {
    TriangleData d;
    d.hasIndices = false;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.mesh.indices.size(), 3u);
    EXPECT_EQ(result.mesh.primitives[0].indexCount, 3u);
    EXPECT_EQ(result.mesh.indices[2], 2u);
}

TEST(GltfParserTest, GlbEmbeddedTriangleAndBufferViewImage) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, /*embedded*/ true);
    built.doc["images"] = Json::array({Json::object(
        {{"bufferView", built.doc["bufferViews"].size()},
         {"mimeType", "image/png"}})});
    // image bufferView: [binLen..binLen+16) zeros at the tail
    built.doc["bufferViews"].push_back(Json::object(
        {{"buffer", 0},
         {"byteOffset", static_cast<uint32_t>(built.bin.size())},
         {"byteLength", 16}}));
    built.doc["textures"] =
        Json::array({Json::object({{"source", 0}})});
    built.doc["materials"][0]["pbrMetallicRoughness"] =
        Json::object({{"baseColorTexture", Json::object({{"index", 0}})}});
    built.json = built.doc.dump();
    std::vector<uint8_t> binAndImage = built.bin;
    for (int i = 0; i < 16; ++i) binAndImage.push_back(0xFF);

    const std::vector<uint8_t> glb = MakeGlb(built.json, binAndImage);
    const ImportResult result = GltfParser::ParseGlb(glb, "");
    ASSERT_TRUE(result.ok) << [&] {
        std::string msg;
        for (const auto& dg : result.diagnostics)
            msg += std::string(dg.code) + " : " + dg.message + "\n";
        return msg;
    }();
    EXPECT_EQ(result.mesh.vertices.size(), 3u);
    EXPECT_EQ(result.mesh.materials.size(), 1u);
    EXPECT_TRUE(result.mesh.materials[0].baseColor.present);
    EXPECT_EQ(result.mesh.materials[0].baseColor.imageIndex, 0u);
    ASSERT_EQ(result.mesh.images.size(), 1u);
    EXPECT_EQ(result.mesh.images[0].mimeType, std::string("image/png"));
    EXPECT_EQ(result.mesh.images[0].uri, std::string(""));
}

TEST(GltfParserTest, MaterialDefaultsAndAlphaModes) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["materials"][0] = Json::object(
        {{"name", "mask-mat"}, {"alphaMode", "MASK"}});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    const MaterialIR& mat = result.mesh.materials[0];
    EXPECT_EQ(mat.name, std::string("mask-mat"));
    EXPECT_EQ(mat.alphaMode, AlphaModeIR::Mask);
    EXPECT_NEAR(mat.baseColorFactor[0], 1.0f, 1e-6f);
    EXPECT_NEAR(mat.metallicFactor, 1.0f, 1e-6f);
    EXPECT_NEAR(mat.roughnessFactor, 1.0f, 1e-6f);
    EXPECT_NEAR(mat.alphaCutoff, 0.5f, 1e-6f);
}

TEST(GltfParserTest, TANGENTParsedWithWWarning) {
    TriangleData d;
    d.hasTangents = true;
    d.tangents[3] = 0.25f;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    EXPECT_NEAR(result.mesh.vertices[0].tangent[0], 1.0f, 1e-6f);
    EXPECT_TRUE(HasDiag(result, kCodeTangentW));
}

TEST(GltfParserTest, CameraNodeAndMultipleScenesProduceWarnings) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["cameras"] = Json::array({Json::object()});
    Json camNode;
    camNode["camera"] = 0;
    camNode["name"] = "Cam";
    built.doc["nodes"].push_back(camNode);
    Json& scene = built.doc["scenes"][0];
    Json& nodes = scene["nodes"];
    nodes.push_back(nodes.size());   // size_t -> unsigned int in nlohmann
    built.doc["scenes"].push_back(Json::object({{"nodes", Json::array()}}));
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(HasDiag(result, kCodeNodeSkipped));
    EXPECT_TRUE(HasDiag(result, kCodeSceneIgnored));
}

TEST(GltfParserTest, UnusedExtensionsUsedProducesWarningOnly) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["extensionsUsed"] =
        Json::array({"KHR_materials_unlit"});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult result =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(HasDiag(result, kCodeExtensionIgnored));
}

} // namespace
