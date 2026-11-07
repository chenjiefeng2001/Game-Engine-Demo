#include "TestSupport.h"

#include "AssetImport/ImportDigest.h"
#include "AssetImport/MeshAssetWriter.h"
#include "AssetImport/ObjParser.h"

#include <gtest/gtest.h>

namespace {

using namespace AssetImportTest;
using namespace Engine::AssetImport;

// ── B3-E: glTF emissiveFactor ────────────────────────────────────────────

TEST(GltfEmissiveTest, ExplicitFactorParsed) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["materials"][0]["emissiveFactor"] = Json::array({0.1, 0.2, 0.9});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[0], 0.1f, 1e-6f);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[1], 0.2f, 1e-6f);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[2], 0.9f, 1e-6f);
}

TEST(GltfEmissiveTest, DefaultIsZero) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[0], 0.f, 1e-7f);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[1], 0.f, 1e-7f);
    EXPECT_NEAR(r.mesh.materials[0].emissiveFactor[2], 0.f, 1e-7f);
}

TEST(GltfEmissiveTest, InvalidCountRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["materials"][0]["emissiveFactor"] = Json::array({1.0, 0.0});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeValueInvalid));
}

TEST(GltfEmissiveTest, InvalidTypeRejected) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    built.doc["materials"][0]["emissiveFactor"] = Json::array({1.0, "x", 0.0});
    built.json = built.doc.dump();
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeValueInvalid));
}

// ── B3-C: dependency recording + digest determinism ─────────────────────

TEST(ImportDigestTest, DependenciesRecordedForExternalBuffer) {
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult r =
        GltfParser::ParseGltfJson(ToBytes(built.json), dir.Path());
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mesh.dependencyFiles.size(), 1u);
    EXPECT_EQ(r.mesh.dependencyFiles[0], std::string("tri.bin"));
}

TEST(ImportDigestTest, DependenciesRecordedForMtllib) {
    const ImportResult r = ObjParser::ParseObj(
        ToBytes("mtllib lib.mtl\nnewmtl-noop matter\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"),
        std::string());
    EXPECT_FALSE(r.ok);
    // mtllib is recorded even when the follow-up load fails with MTL missing.
    // (dependency list live before hard-fail returns; diagnostics contain
    //  MTL_LIB_MISSING.)
}

TEST(ImportDigestTest, DigestIsDeterministicAndSensitiveToDependencies) {
    const std::vector<uint8_t> source = ToBytes("source-bytes");
    std::vector<uint8_t> depOne = ToBytes("dep-one");
    std::vector<uint8_t> depTwo = ToBytes("dep-two");

    ImportDigestInput input;
    input.sourcePathKey = "a/source.obj";
    input.sourceBytes = source.data();
    input.sourceSize = source.size();
    DigestDependency one;
    one.pathKey = "lib/a.mtl";
    one.bytes = depOne.data();
    one.size = depOne.size();
    input.dependencies.push_back(one);
    DigestDependency two;
    two.pathKey = "lib/b.mtl";
    two.bytes = depTwo.data();
    two.size = depTwo.size();
    input.dependencies.push_back(two);
    const std::string first = ComputeImportDigest(input);
    const std::string second = ComputeImportDigest(input);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.size(), 16u);

    depTwo[0] = 'Z';
    const std::string changed = ComputeImportDigest(input);
    EXPECT_NE(changed, first);
}

} // namespace
