#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using namespace AssetImportTest;
using namespace Engine::AssetImport;

// ── GLB container ────────────────────────────────────────────────────────

TEST(AssetImportSecurityTest, GlbBadMagic) {
    std::vector<uint8_t> junk = {'J', 'U', 'N', 'K', 2, 0, 0, 0, 0, 0, 0, 0};
    const ImportResult r = GltfParser::ParseGlb(junk, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeBadMagic));
}

TEST(AssetImportSecurityTest, GlbShorterThanHeader) {
    std::vector<uint8_t> junk = {'g', 'l', 'T', 'F'};
    const ImportResult r = GltfParser::ParseGlb(junk, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeChunkBounds));
}

TEST(AssetImportSecurityTest, GlbVersionMustBeTwo) {
    const std::vector<uint8_t> glb = MakeGlb("{\"asset\":{\"version\":\"2.0\"}}", {0, 0, 1, 0, 1, 0, 0});
    std::vector<uint8_t> v3 = glb;
    v3[4] = 3;
    const ImportResult r = GltfParser::ParseGlb(v3, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeBadVersion));
}

TEST(AssetImportSecurityTest, GlbLengthMismatch) {
    std::vector<uint8_t> glb = MakeGlb("{\"asset\":{\"version\":\"2.0\"}}", {1, 0, 0, 0, 2, 0, 0, 0});
    glb[8] = 0xFF;
    const ImportResult r = GltfParser::ParseGlb(glb, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeLengthMismatch));
}

TEST(AssetImportSecurityTest, GlbChunkUnaligned) {
    std::vector<uint8_t> glb;
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2);
    AppendU32(glb, 12 + 8 + 5);
    AppendU32(glb, 5);
    AppendU32(glb, 0x4E4F534Au);
    const std::string j = "{}xyz";
    for (const char c : j) glb.push_back(static_cast<uint8_t>(c));
    const ImportResult r = GltfParser::ParseGlb(glb, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeChunkUnaligned));
}

TEST(AssetImportSecurityTest, GlbChunkOutOfBounds) {
    std::vector<uint8_t> glb;
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2);
    AppendU32(glb, 20);
    AppendU32(glb, 3000);
    AppendU32(glb, 0x4E4F534Au);
    const ImportResult r = GltfParser::ParseGlb(glb, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeChunkBounds));
}

TEST(AssetImportSecurityTest, GlbFirstChunkMustBeJson) {
    std::vector<uint8_t> glb;
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2);
    AppendU32(glb, 24);
    AppendU32(glb, 4);
    AppendU32(glb, 0x004E4942u);
    for (int i = 0; i < 4; ++i) glb.push_back(0);
    const ImportResult r = GltfParser::ParseGlb(glb, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeChunkOrder));
}

TEST(AssetImportSecurityTest, GlbExtraChunkRejected) {
    std::vector<uint8_t> glb = MakeGlb("{\"asset\":{\"version\":\"2.0\"}}", {1, 2, 3, 4});
    const std::string extra = "zz";
    AppendChunk(glb, 0x54524944u, extra.data(), extra.size()); // 'TRID'
    const uint32_t actual = static_cast<uint32_t>(glb.size());
    std::memcpy(glb.data() + 8, &actual, 4);
    const ImportResult r = GltfParser::ParseGlb(glb, "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeExtraChunks));
}

// ── glTF JSON layer ──────────────────────────────────────────────────────

TEST(AssetImportSecurityTest, MalformedJson) {
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes("{ nope ]{"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeJsonParse));
}

TEST(AssetImportSecurityTest, VersionNot20) {
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes("{\"asset\":{\"version\":\"2.1\"}}"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeBadVersion));
}

TEST(AssetImportSecurityTest, RequiredExtensionRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["extensionsRequired"] = Json::array({"KHR_draco_mesh_compression"});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeRequiredExtensionUnsupported));
}

TEST(AssetImportSecurityTest, DataUriBufferRejected) {
    const ImportResult r = GltfParser::ParseGltfJson(
        ToBytes("{\"asset\":{\"version\":\"2.0\"},"
                "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,AAA\",\"byteLength\":3}],"
                "\"buffers0\":1}"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeDataUriUnsupported));
}

TEST(AssetImportSecurityTest, UriTraversalRejected) {
    const ImportResult r = GltfParser::ParseGltfJson(
        ToBytes("{\"asset\":{\"version\":\"2.0\"},"
                "\"buffers\":[{\"uri\":\"../evil.bin\",\"byteLength\":4}]}"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeUriTraversal));
}

TEST(AssetImportSecurityTest, UriPercentRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["buffers"][0]["uri"] = "tri%2Ebin";
    built.json = built.doc.dump();
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeUriPercentUnsupported));
}

TEST(AssetImportSecurityTest, ExternalBufferMissing) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir; // empty dir: tri.bin absent
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeExternalBufferReadFail));
}

TEST(AssetImportSecurityTest, SparseAccessorRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["accessors"][0]["sparse"] = Json::object();
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeSparseUnsupported));
}

TEST(AssetImportSecurityTest, InterleavedBufferViewRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["bufferViews"][0]["byteStride"] = 20;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeInterleavedUnsupported));
}

TEST(AssetImportSecurityTest, PositionRequiresFloatComponent) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["accessors"][0]["componentType"] = 5123;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAttrComponentUnsupported));
}

TEST(AssetImportSecurityTest, NormalizedUvRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["accessors"][1]["componentType"] = 5121;
    built.doc["accessors"][1]["normalized"] = true;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAttrNormalizedUnsupported));
}

TEST(AssetImportSecurityTest, UnsupportedPrimitiveMode) {
    TriangleData d;
    d.mode = 5; // TRIANGLE_STRIP
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodePrimitiveModeUnsupported));
}

TEST(AssetImportSecurityTest, AccessorBeyondBufferViewBounds) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["accessors"][0]["count"] = 999;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAccessorBounds));
}

TEST(AssetImportSecurityTest, BufferViewBeyondBufferBounds) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["bufferViews"][0]["byteLength"] =
        static_cast<uint32_t>(built.bin.size()) + 4096;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeBufferViewBounds));
}

TEST(AssetImportSecurityTest, IndexValueOutOfRange) {
    TriangleData d;
    d.indices[0] = 42;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAccessorBounds));
}

TEST(AssetImportSecurityTest, MaterialRefMissing) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["meshes"][0]["primitives"][0]["material"] = 5;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeMaterialRefMissing));
}

TEST(AssetImportSecurityTest, PositionMissing) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["meshes"][0]["primitives"][0]["attributes"].erase("POSITION");
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAttributeMissing));
}

TEST(AssetImportSecurityTest, AttributeCountMismatch) {
    TriangleData d;
    d.normalCount = 4;
    d.uvCount = 3;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["accessors"][1]["count"] = 4;
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeAttributeCountMismatch));
}

TEST(AssetImportSecurityTest, NodeDepthLimit) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    // Append a 70-node chain whose nodes reference each other by index;
    // traversal starts at id 1 and exceeds kMaxNodeDepth (64).
    for (int i = 0; i < 70; ++i) {
        built.doc["nodes"].push_back(
            Json::object({{"children",
                           i == 69
                               ? Json::array({static_cast<uint64_t>(0)})
                               : Json::array({static_cast<uint64_t>(2 + i)})}}));
    }
    built.doc["scenes"][0]["nodes"] = Json::array({static_cast<uint64_t>(1)});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeLimitNodeDepth));
}

TEST(AssetImportSecurityTest, VertexColorDroppedWarning) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] =
        built.doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"];
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeVertexColorDropped));
}

// ── determinism ──────────────────────────────────────────────────────────

TEST(AssetImportDeterminismTest, RepeatedParseIsByteIdentical) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    (void)dir;
    const ImportResult a = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    const ImportResult b = GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_TRUE(VerticesEqual(a.mesh, b.mesh));
    EXPECT_EQ(a.mesh.primitives.size(), b.mesh.primitives.size());
}

TEST(AssetImportDeterminismTest, GlbRepeatParseIsByteIdentical) {
    TriangleData d;
    d.hasTangents = true;
    BuiltDoc built = MakeTriangleGltf(d, true);
    const std::vector<uint8_t> glb = MakeGlb(built.json, built.bin);
    const ImportResult a = GltfParser::ParseGlb(glb, "");
    const ImportResult b = GltfParser::ParseGlb(glb, "");
    ASSERT_TRUE(a.ok) << [&] {
        std::string m = "size=" + std::to_string(glb.size()) + " ";
        for (const auto& dg : a.diagnostics)
            m += std::string(dg.code) + " @ " + dg.location + " : " + dg.message + "\n";
        return m;
    }();
    ASSERT_TRUE(b.ok);
    EXPECT_TRUE(VerticesEqual(a.mesh, b.mesh));
}

} // namespace
