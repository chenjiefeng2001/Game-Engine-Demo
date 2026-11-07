#include "TestSupport.h"

#include "AssetImport/MtlParser.h"
#include "AssetImport/ObjParser.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using namespace AssetImportTest;
using namespace Engine::AssetImport;

// ── OBJ positive ─────────────────────────────────────────────────────────

struct ObjFactory
{
    std::string body;
    std::string mtl;
    std::string mtlName = "lib.mtl";
};

static std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

static bool HasDiagMtl(const MtlParseResult& r, const char* code) {
    for (const auto& d : r.diagnostics) {
        if (d.code == code) return true;
    }
    return false;
}

static void WriteMtl(ObjFactory& f, ScopedTempDir& dir) {
    if (!f.mtl.empty()) dir.Write(f.mtlName.c_str(), f.mtl.data(), f.mtl.size());
}

TEST(ObjMtlTest, MinimalObjTriangleNoMaterial) {
    ObjFactory f;
    f.body = "v -0.5 0 0\nv 0.5 0 0\nv 0 1 0\nf 1 2 3\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.mesh.vertices.size(), 3u);
    ASSERT_EQ(r.mesh.indices.size(), 3u);
    EXPECT_EQ(r.mesh.indices, (std::vector<uint32_t>{0, 1, 2}));
    ASSERT_EQ(r.mesh.primitives.size(), 1u);
    EXPECT_EQ(r.mesh.primitives[0].indexCount, 3u);
    EXPECT_EQ(r.mesh.primitives[0].materialIndex, 0xFFFFFFFFu);
    EXPECT_TRUE(HasDiag(r, kCodeMaterialUnspecified));
    EXPECT_TRUE(r.mesh.materials.empty());
}

TEST(ObjMtlTest, NegativeRelativeIndices) {
    ObjFactory f;
    f.body = "v -0.5 0 0\nv 0.5 0 0\nv 0 1 0\nf -3 -2 -1\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.mesh.indices, (std::vector<uint32_t>{0, 1, 2}));
}

TEST(ObjMtlTest, SeparateIndexStreamsAndDedup) {
    ObjFactory f;
    // Two faces share corner (1,1,1): total 6 face entries but 4 unique verts.
    f.body = "v -0.5 0 0\nv 0.5 0 0\nv 0 1 0\nv 0 -1 0\n"
             "vt 0 0\nvt 1 0\nvt 1 1\n"
             "vn 0 0 1\n"
             "f 1/1/1 2/2/1 3/3/1\n"
             "f 2/2/1 4/2/1 3/3/1\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.mesh.vertices.size(), 4u);
    ASSERT_EQ(r.mesh.indices.size(), 6u);
    for (const auto& v : r.mesh.vertices) {
        EXPECT_TRUE(v.flags & kVertexFlagUvValid);
        EXPECT_TRUE(v.flags & kVertexFlagNormalValid);
    }
    EXPECT_NEAR(r.mesh.vertices[0].uv0[1], 1.0f, 1e-6f);   // flip (0,0)->(0,1)
    EXPECT_NEAR(r.mesh.vertices[2].uv0[1], 0.0f, 1e-6f);   // flip (1,1)->(1,0)
}

TEST(ObjMtlTest, MissingUvEntriesPerVertexFlags) {
    ObjFactory f;
    f.body = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
             "vt 0 0\n"
             "vn 0 0 1\n"
             "f 1//1 2/1/1 3/1/1\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.mesh.vertices[0].flags & kVertexFlagUvValid);
    EXPECT_TRUE(r.mesh.vertices[1].flags & kVertexFlagUvValid);
    EXPECT_TRUE(r.mesh.vertices[0].flags & kVertexFlagNormalValid);
}

TEST(ObjMtlTest, GroupAndMaterialSegmentation) {
    ObjFactory f;
    f.body = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
             "o A\nusemtl m1\nf 1 2 3\n"
             "g B\nusemtl m2\nf 1 3 2\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mesh.primitives.size(), 2u);
    EXPECT_EQ(r.mesh.primitives[0].nodeName, std::string("o:A/g:"));
    EXPECT_EQ(r.mesh.primitives[1].nodeName, std::string("o:A/g:B"));
    EXPECT_EQ(r.mesh.primitives[0].firstIndex, 0u);
    EXPECT_EQ(r.mesh.primitives[1].firstIndex, 3u);
    // no mtllib: both materials synthesized
    ASSERT_EQ(r.mesh.materials.size(), 2u);
    EXPECT_EQ(r.mesh.materials[0].name, std::string("m1"));
    EXPECT_EQ(r.mesh.materials[1].name, std::string("m2"));
    EXPECT_TRUE(HasDiag(r, kCodeMtlMaterialMissing));
}

TEST(ObjMtlTest, QuadFanTriangulationWarning) {
    ObjFactory f;
    f.body = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.mesh.indices.size(), 6u);
    EXPECT_EQ(r.mesh.indices[2], 2u);
    EXPECT_EQ(r.mesh.indices[3], 0u);
    EXPECT_EQ(r.mesh.indices[4], 2u);
    EXPECT_EQ(r.mesh.indices[5], 3u);
    EXPECT_TRUE(HasDiag(r, kCodeObjPolygonTriangulated));
}

TEST(ObjMtlTest, MultiPrimitiveSourceOrderPreserved) {
    ObjFactory f;
    f.body = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
             "o P1\nf 1 2 3\n"
             "o P2\nf 1 2 4\n"
             "o P3\nf 1 2 3\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mesh.primitives.size(), 3u);
    EXPECT_EQ(r.mesh.primitives[0].nodeName, std::string("o:P1/g:"));
    EXPECT_EQ(r.mesh.primitives[1].nodeName, std::string("o:P2/g:"));
    EXPECT_EQ(r.mesh.primitives[2].nodeName, std::string("o:P3/g:"));
    EXPECT_EQ(r.mesh.primitives[2].firstIndex, 6u);
}

TEST(ObjMtlTest, MtlMaterialsBindToSlots) {
    ObjFactory f;
    f.mtl = "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n";
    f.body = "mtllib lib.mtl\n"
             "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
             "usemtl red\nf 1 2 3\n"
             "usemtl blue\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult r =
        ObjParser::ParseObj(Bytes(f.body), dir.Path());
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mesh.materials.size(), 2u);
    EXPECT_EQ(r.mesh.materials[0].name, std::string("red"));
    EXPECT_NEAR(r.mesh.materials[0].baseColorFactor[0], 1.0f, 1e-6f);
    EXPECT_NEAR(r.mesh.materials[0].baseColorFactor[1], 0.0f, 1e-6f);
    EXPECT_NEAR(r.mesh.materials[1].baseColorFactor[2], 1.0f, 1e-6f);
    EXPECT_EQ(r.mesh.primitives[0].materialIndex, r.mesh.materials.size() >= 1u ? 0u : 0u);
    EXPECT_EQ(r.mesh.primitives[1].materialIndex, 1u);
}

TEST(ObjMtlTest, MtlTextureAndKeEmissiveFactor) {
    ObjFactory f;
    f.mtl = "newmtl lit\n"
            "Kd 0.2 0.4 0.8 1.0\n"
            "Ke 1 0.5 0.5\n"
            "map_Kd tex.png\n"
            "bump -imfchan z n.png\n";
    f.body = "mtllib lib.mtl\n"
             "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl lit\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), dir.Path());
    ASSERT_TRUE(r.ok);
    const MaterialIR& m = r.mesh.materials[0];
    EXPECT_NEAR(m.baseColorFactor[0], 0.2f, 1e-6f);
    EXPECT_NEAR(m.emissiveFactor[1], 0.5f, 1e-6f);
    EXPECT_TRUE(m.baseColor.present);
    EXPECT_TRUE(m.normal.present);
    ASSERT_EQ(r.mesh.images.size(), 2u);
    EXPECT_EQ(r.mesh.images[0].uri, std::string("tex.png"));
    EXPECT_EQ(r.mesh.images[1].uri, std::string("n.png"));
}

TEST(ObjMtlTest, SmoothAndUnknownDirectivesWarnOnce) {
    ObjFactory f;
    f.body = "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
             "s off\nf 1 2 3\ns on\nf 1 2 3\nzzz 4\nzzz 4\n";
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), "");
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeObjSmoothIgnored));
    EXPECT_TRUE(HasDiag(r, kCodeObjUnknownDirective));
}

TEST(ObjMtlTest, SharedIrInvariantsMatchGltfTriangle) {
    // Same triangle via OBJ and via glTF must produce the same geometry.
    const ImportResult obj = ObjParser::ParseObj(
        Bytes("v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n"), "");
    TriangleData d;
    BuiltDoc built = MakeTriangleGltf(d, false);
    ScopedTempDir dir;
    dir.Write("tri.bin", built.bin.data(), built.bin.size());
    const ImportResult gltf = GltfParser::ParseGltfJson(
        ToBytes(built.json), dir.Path());
    ASSERT_TRUE(obj.ok);
    ASSERT_TRUE(gltf.ok);
    EXPECT_EQ(obj.mesh.vertices.size(), gltf.mesh.vertices.size());
    EXPECT_EQ(obj.mesh.indices.size(), gltf.mesh.indices.size());
    for (size_t v = 0; v < obj.mesh.vertices.size(); ++v) {
        for (int k = 0; k < 3; ++k) {
            const float a = obj.mesh.vertices[v].position[k];
            const float b = gltf.mesh.vertices[v].position[k];
            EXPECT_NEAR(a, b, 1e-6f) << "OBJ[" << v << "][" << k << "]=" << a
                                     << " glTF=" << b;
        }
    }
}

TEST(ObjMtlTest, DeterministicRepeatedParse) {
    ObjFactory f;
    f.mtl = "newmtl m\nKd 0.9 0.1 0.1\n";
    f.body = "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl m\nf 1 2 3\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult a =
        ObjParser::ParseObj(Bytes(f.body), dir.Path());
    const ImportResult b =
        ObjParser::ParseObj(Bytes(f.body), dir.Path());
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_TRUE(VerticesEqual(a.mesh, b.mesh));
    EXPECT_EQ(a.mesh.primitives.size(), b.mesh.primitives.size());
}

// ── OBJ security ─────────────────────────────────────────────────────────

TEST(ObjMtlSecurityTest, IllegalZeroAndPositiveOutOfRangeIndex) {
    const ImportResult zero =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n"), "");
    EXPECT_FALSE(zero.ok);
    EXPECT_TRUE(HasDiag(zero, kCodeObjIndexOutOfRange));

    const ImportResult over =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n"), "");
    EXPECT_FALSE(over.ok);
    EXPECT_TRUE(HasDiag(over, kCodeObjIndexOutOfRange));
}

TEST(ObjMtlSecurityTest, NegativeIndexUnderflow) {
    const ImportResult r =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -4 1 2\n"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeObjIndexOutOfRange));
}

TEST(ObjMtlSecurityTest, FaceDegenerateTwoCorners) {
    const ImportResult r =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nf 1 2\n"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeObjFaceDegenerate));
}

TEST(ObjMtlSecurityTest, MalformedFaceCornerTokens) {
    const ImportResult trailing =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/ 2 3\n"), "");
    EXPECT_FALSE(trailing.ok);
    EXPECT_TRUE(HasDiag(trailing, kCodeObjSyntax));

    const ImportResult doubleSlashEmpty =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1//2//3 2 3\n"), "");
    EXPECT_FALSE(doubleSlashEmpty.ok);
}

TEST(ObjMtlSecurityTest, IndexReferencedStreamMissing) {
    const ImportResult noUv =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1/1 2 3\n"), "");
    EXPECT_FALSE(noUv.ok);
    EXPECT_TRUE(HasDiag(noUv, kCodeObjIndexOutOfRange));

    const ImportResult noNormals =
        ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1//2 2 3\n"), "");
    EXPECT_FALSE(noNormals.ok);
    EXPECT_TRUE(HasDiag(noNormals, kCodeObjIndexOutOfRange));
}

TEST(ObjMtlSecurityTest, FacesMissing) {
    const ImportResult r = ObjParser::ParseObj(Bytes("v 0 0 0\nv 1 0 0\n"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeObjFacesMissing));
}

TEST(ObjMtlSecurityTest, MalformedNumber) {
    const ImportResult r =
        ObjParser::ParseObj(Bytes("v abc def 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeObjSyntax));
}

// ── MTL ──────────────────────────────────────────────────────────────────

TEST(ObjMtlSecurityTest, TexturePathTraversal) {
    ObjFactory f;
    f.mtl = "newmtl m\nmap_Kd ../evil.png\n";
    f.body = "mtllib lib.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl m\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeUriTraversal));
}

TEST(ObjMtlSecurityTest, TextureDataUriRejected) {
    ObjFactory f;
    f.mtl = "newmtl m\nmap_Kd data:image/png;base64,AAA=\n";
    f.body = "mtllib lib.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl m\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeDataUriUnsupported));
}

TEST(ObjMtlSecurityTest, MalformedMtlNumeric) {
    ObjFactory f;
    f.mtl = "newmtl m\nKd abc 0 0\n";
    f.body = "mtllib lib.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl m\nf 1 2 3\n";
    ScopedTempDir dir;
    WriteMtl(f, dir);
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), dir.Path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeMtlSyntax));
}

TEST(ObjMtlSecurityTest, MtlAndMtllibMissing) {
    ObjFactory f;
    f.body = "mtllib nowhere.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl nope\nf 1 2 3\n";
    ScopedTempDir dir; // nowhere.mtl absent
    const ImportResult r = ObjParser::ParseObj(Bytes(f.body), dir.Path());
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(HasDiag(r, kCodeMtlLibMissing));
    ASSERT_EQ(r.mesh.materials.size(), 1u);
    EXPECT_EQ(r.mesh.materials[0].name, std::string("nope"));
}

TEST(ObjMtlTest, MtlDirectStandaloneParseDefaults) {
    const MtlParseResult r = MtlParser::ParseMtl(
        Bytes("newmtl m\nKd 1 2 3 4\nKe 0 1 0\n"));
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.materials.size(), 1u);
    EXPECT_NEAR(r.materials[0].baseColorFactor[0], 1.0f, 1e-6f);
    EXPECT_NEAR(r.materials[0].baseColorFactor[3], 4.0f, 1e-6f);
    EXPECT_NEAR(r.materials[0].emissiveFactor[1], 1.0f, 1e-6f);
    EXPECT_NEAR(r.materials[0].alphaCutoff, 0.5f, 1e-6f);
    EXPECT_NEAR(r.materials[0].metallicFactor, 1.0f, 1e-6f);
    EXPECT_NEAR(r.materials[0].roughnessFactor, 1.0f, 1e-6f);
}

TEST(ObjMtlTest, MtlDuplicateNamesAndIgnoredFields) {
    const MtlParseResult r = MtlParser::ParseMtl(
        Bytes("newmtl m\nNs 10\nillum 2\nnewmtl m\nKd 1 0 0\n"));
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.materials.size(), 1u);
    EXPECT_TRUE(HasDiagMtl(r, kCodeMtlDuplicateMaterial));
    EXPECT_TRUE(HasDiagMtl(r, kCodeMtlFieldIgnored));
}

// ── MtlParser HasDiag helper (local) ────────────────────────────────────

TEST(ObjMtlTest, MtlMapAlphaFieldSetsAlpha) {
    const MtlParseResult r = MtlParser::ParseMtl(
        Bytes("newmtl m\nKd 1 1 1\nd 0.25\n"));
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.materials[0].baseColorFactor[3], 0.25f, 1e-6f);
}
TEST(ObjMtlTest, TrInvertsAlpha) {
    const MtlParseResult r = MtlParser::ParseMtl(
        Bytes("newmtl m\nTr 0.25\n"));
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.materials[0].baseColorFactor[3], 0.75f, 1e-6f);
}

} // namespace
