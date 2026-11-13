#include "PixelHarness.h"

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/RHI/MeshGpuResource.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>

namespace {

using namespace PixelTest;
using namespace Engine;
using namespace Engine::Content;

struct MaterialDef {
    std::string name;
    float kd[3] = {1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    bool textured = false;
};

std::string MtlText(const std::string& textureFile,
                    const std::vector<MaterialDef>& materials) {
    std::ostringstream out;
    out << std::setprecision(9);
    for (const MaterialDef& m : materials) {
        out << "newmtl " << m.name << "\n";
        out << "Kd " << m.kd[0] << " " << m.kd[1] << " " << m.kd[2] << "\n";
        if (m.alpha != 1.0f) out << "d " << m.alpha << "\n";
        if (m.textured) out << "map_Kd " << textureFile << "\n";
    }
    return out.str();
}

struct Fixture {
    std::string objRel;
    std::string meshPath;
    std::string guidHex;
    std::vector<AssetRef> assets;
};

Fixture WriteAndImport(PixelProject& project, EditorSessionHandle session,
                       const std::string& stem,
                       const std::vector<std::vector<QuadVertex>>& quads,
                       const std::vector<MaterialDef>& materials,
                       const std::vector<uint8_t>& texturePng) {
    const std::string dir = "assets/models/";
    const std::string texName = stem + ".png";
    const std::string objRel = dir + stem + ".obj";
    project.WriteText(objRel, ObjForQuads(quads, [&] {
                          std::vector<std::string> names;
                          for (const MaterialDef& m : materials)
                              names.push_back(m.name);
                          return names;
                      }(), stem + ".mtl"));
    project.WriteText(dir + stem + ".mtl", MtlText(texName, materials));
    if (!texturePng.empty()) project.WriteBytes(dir + texName, texturePng);
    const ImportedModel model = ImportModel(session, objRel);
    Fixture f;
    f.objRel = objRel;
    f.meshPath = model.meshPath;
    f.guidHex = model.guidHex;
    f.assets = ListAssets(session);
    return f;
}

std::vector<AssetRef> AssetsWithSuffix(const std::vector<AssetRef>& assets,
                                       const std::string& suffix) {
    std::vector<AssetRef> out;
    for (const AssetRef& a : assets) {
        if (a.path.size() >= suffix.size() &&
            a.path.compare(a.path.size() - suffix.size(), suffix.size(),
                           suffix) == 0)
            out.push_back(a);
    }
    return out;
}

const MaterialDef kRedMaterial{"MatRed", {1.0f, 1.0f, 1.0f}, 1.0f, true};
const MaterialDef kBlueMaterial{"MatBlue", {0.0f, 0.0f, 1.0f}, 1.0f, false};

MaterialDef FactorMaterial(float r, float g, float b) {
    return MaterialDef{"MatFactor", {r, g, b}, 1.0f, false};
}

void ExpectBackgroundProbes(const PixelFrame& frame) {
    for (const Probe& p : BackgroundProbes(frame.width, frame.height)) {
        EXPECT_TRUE(IsBackground(frame.At(p.x, p.y), 1))
            << "background probe (" << p.x << "," << p.y << ") = "
            << frame.At(p.x, p.y).r << "," << frame.At(p.x, p.y).g << ","
            << frame.At(p.x, p.y).b;
    }
}

void ExpectObjectProbes(const PixelFrame& frame, const Rgba& expected,
                        int tolerance) {
    for (const Probe& p : SingleQuadProbes(frame.width, frame.height)) {
        const Rgba actual = frame.At(p.x, p.y);
        EXPECT_TRUE(Close(actual, expected, tolerance, true))
            << "probe (" << p.x << "," << p.y << ") expected "
            << expected.r << "," << expected.g << "," << expected.b << ","
            << expected.a << " got " << actual.r << "," << actual.g << ","
            << actual.b << "," << actual.a;
    }
}

void ExpectBackgroundOnly(const PixelFrame& frame) {
    for (const Probe& p : SingleQuadProbes(frame.width, frame.height)) {
        EXPECT_TRUE(IsBackground(frame.At(p.x, p.y), 1))
            << "object probe should stay clear at (" << p.x << "," << p.y
            << ")";
    }
    EXPECT_LT(CountNonBackground(frame, 3), 20000);
}

const Rgba kExpectedSrgb188 = ExpectedAmbient(
    SrgbDecode(188), SrgbDecode(64), SrgbDecode(32), 1.0f);
const Rgba kExpectedGlbBlue = ExpectedAmbient(
    SrgbDecode(128), SrgbDecode(128), SrgbDecode(255), 1.0f);
const Rgba kExpectedAlpha = ExpectedAmbient(
    SrgbDecode(230), SrgbDecode(140), SrgbDecode(40), 128.0f / 255.0f);
const Rgba kExpectedMultiRed =
    ExpectedAmbient(SrgbDecode(230), SrgbDecode(20), SrgbDecode(20), 1.0f);

} // namespace

TEST(MeshProductionPixelTest, ObjTexturedDirectAndRenderGraph) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto png = MakePng(4, 4, 4, 188, 64, 32, 255);

    std::string guid;
    std::vector<AssetRef> assets;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(
            project, session.handle, "pixel_quad",
            {MakeQuad(0.0f, 0.0f, 4.0f)}, {kRedMaterial}, png);
        guid = f.guidHex;
        assets = f.assets;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    ASSERT_EQ(AssetsWithSuffix(assets, ".material.json").size(), 1u);
    ASSERT_EQ(AssetsWithSuffix(assets, ".tex.png").size(), 1u);
    const AssetRef tex = AssetsWithSuffix(assets, ".tex.png")[0];
    const Json sidecar = TextureSidecar(project, tex.path);
    EXPECT_EQ(sidecar["role"], "basecolor");
    EXPECT_EQ(sidecar["colorspace"], "sRGB");
    EXPECT_EQ(sidecar["components"], 4);

    PixelHost host;
    ASSERT_TRUE(host.Open(project.root)) << host.Error();
    PixelFrame direct;
    ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
    EXPECT_FALSE(direct.renderGraph);
    int32_t status = -1;
    ASSERT_TRUE(host.QueryMesh(guid, &status));
    EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
    EXPECT_GT(CountNonBackground(direct), 120000);
    EXPECT_LT(CountNonBackground(direct), 260000);
    ExpectBackgroundProbes(direct);
    ExpectObjectProbes(direct, kExpectedSrgb188, 2);

    PixelFrame rg;
    ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
    EXPECT_TRUE(rg.renderGraph);
    EXPECT_TRUE(host.QueryMesh(guid, &status));
    EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
    ExpectBackgroundProbes(rg);
    ExpectObjectProbes(rg, kExpectedSrgb188, 2);
    for (const Probe& p : SingleQuadProbes(rg.width, rg.height)) {
        EXPECT_TRUE(Close(direct.At(p.x, p.y), rg.At(p.x, p.y), 2, true))
            << "Direct/RG mismatch at (" << p.x << "," << p.y << ")";
    }
}

TEST(MeshProductionPixelTest, SrgbDecodeMatchesLinearFactorMaterial) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto texPng = MakePng(4, 4, 4, 188, 64, 32, 255);
    const float lr = SrgbDecode(188);
    const float lg = SrgbDecode(64);
    const float lb = SrgbDecode(32);
    std::string texturedGuid, factorGuid;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        texturedGuid = WriteAndImport(project, session.handle, "srgb_tex",
                                      {MakeQuad(0.0f, 0.0f, 4.0f)},
                                      {kRedMaterial}, texPng)
                           .guidHex;
        factorGuid = WriteAndImport(project, session.handle, "linear_factor",
                                    {MakeQuad(0.0f, 0.0f, 4.0f)},
                                    {FactorMaterial(lr, lg, lb)}, {})
                         .guidHex;
        session.SetSingleMeshGuid(texturedGuid);
        ASSERT_TRUE(session.Save());
    }

    Rgba texturedProbes[4] = {};
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        const auto probes = SingleQuadProbes(frame.width, frame.height);
        for (size_t i = 0; i < probes.size(); ++i) {
            texturedProbes[i] =
                frame.At(probes[i].x, probes[i].y);
            EXPECT_TRUE(Close(texturedProbes[i], kExpectedSrgb188, 2, true));
        }
    }
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        session.SetSingleMeshGuid(factorGuid);
        ASSERT_TRUE(session.Save());
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        const auto probes = SingleQuadProbes(frame.width, frame.height);
        for (size_t i = 0; i < probes.size(); ++i) {
            const Rgba actual = frame.At(probes[i].x, probes[i].y);
            EXPECT_TRUE(Close(actual, kExpectedSrgb188, 2, true))
                << "factor probe differs from sRGB decode expectation";
            EXPECT_TRUE(Close(actual, texturedProbes[i], 2, true))
                << "textured vs factor mismatch at probe " << i;
        }
    }
}

TEST(MeshProductionPixelTest, GlbEmbeddedColorAndDataTextures) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto sharedPng = MakePng(4, 4, 4, 128, 128, 255, 255);
    project.WriteBytes("assets/models/pixel_glb.glb",
                       MakeGlbQuad(sharedPng, sharedPng));

    std::string guid;
    std::vector<AssetRef> assets;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const ImportedModel model =
            ImportModel(session.handle, "assets/models/pixel_glb.glb");
        ASSERT_GE(model.index, 0);
        guid = model.guidHex;
        assets = ListAssets(session.handle);
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    const auto materials = AssetsWithSuffix(assets, ".material.json");
    const auto textures = AssetsWithSuffix(assets, ".tex.png");
    ASSERT_EQ(materials.size(), 1u);
    ASSERT_EQ(textures.size(), 2u);
    EXPECT_NE(textures[0].path, textures[1].path);
    bool sawColor = false, sawData = false;
    for (const AssetRef& tex : textures) {
        const Json sidecar = TextureSidecar(project, tex.path);
        if (sidecar["role"] == "basecolor") {
            sawColor = true;
            EXPECT_EQ(sidecar["colorspace"], "sRGB");
            EXPECT_EQ(sidecar["components"], 4);
        } else if (sidecar["role"] == "normal") {
            sawData = true;
            EXPECT_EQ(sidecar["colorspace"], "linear");
            EXPECT_EQ(sidecar["components"], 3);
        }
    }
    EXPECT_TRUE(sawColor);
    EXPECT_TRUE(sawData);

    PixelHost host;
    ASSERT_TRUE(host.Open(project.root)) << host.Error();
    PixelFrame direct;
    ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
    int32_t glbStatus = -1;
    ASSERT_TRUE(host.QueryMesh(guid, &glbStatus));
    EXPECT_EQ(glbStatus, static_cast<int32_t>(MeshRuntimeStatus::Ready))
        << host.RuntimeError(guid);
    EXPECT_GT(CountNonBackground(direct), 120000);
    ExpectBackgroundProbes(direct);
    ExpectObjectProbes(direct, kExpectedGlbBlue, 2);

    PixelFrame rg;
    ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
    EXPECT_TRUE(rg.renderGraph);
    ExpectBackgroundProbes(rg);
    ExpectObjectProbes(rg, kExpectedGlbBlue, 2);
}

TEST(MeshProductionPixelTest, MultiSpanRendersDistinctMaterialSlots) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto redPng = MakePng(4, 4, 4, 230, 20, 20, 255);
    std::string guid;
    std::string meshPath;
    std::vector<AssetRef> assets;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(
            project, session.handle, "multi_quad",
            {MakeQuad(-3.0f, 0.0f, 1.5f), MakeQuad(3.0f, 0.0f, 1.5f)},
            {MaterialDef{"MatRed", {1, 1, 1}, 1.0f, true},
             MaterialDef{"MatBlue", {0, 0, 1}, 1.0f, false}},
            redPng);
        guid = f.guidHex;
        meshPath = f.meshPath;
        assets = f.assets;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    ASSERT_EQ(AssetsWithSuffix(assets, ".material.json").size(), 2u);
    const Json mesh = ReadJson(project.root / meshPath);
    ASSERT_EQ(mesh["primitives"].size(), 2u);
    EXPECT_EQ(mesh["primitives"][0]["firstIndex"], 0);
    EXPECT_EQ(mesh["primitives"][0]["indexCount"], 6);
    EXPECT_EQ(mesh["primitives"][0]["materialIndex"], 0);
    EXPECT_EQ(mesh["primitives"][1]["firstIndex"], 6);
    EXPECT_EQ(mesh["primitives"][1]["indexCount"], 6);
    EXPECT_EQ(mesh["primitives"][1]["materialIndex"], 1);
    ASSERT_EQ(mesh["materialNames"].size(), 2u);

    PixelHost host;
    ASSERT_TRUE(host.Open(project.root)) << host.Error();
    PixelFrame direct;
    ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
    EXPECT_GT(CountNonBackground(direct), 30000);
    EXPECT_LT(CountNonBackground(direct), 100000);
    ExpectBackgroundProbes(direct);
    const auto multiProbes = MultiQuadProbes(direct.width, direct.height);
    ASSERT_EQ(multiProbes.size(), 2u);
    EXPECT_TRUE(Close(direct.At(multiProbes[0].x, multiProbes[0].y),
                      kExpectedMultiRed, 2, true))
        << "left span must use material slot 0";
    EXPECT_TRUE(Close(direct.At(multiProbes[1].x, multiProbes[1].y),
                      ExpectedAmbient(0.0f, 0.0f, 1.0f, 1.0f), 2, true))
        << "right span must use material slot 1";
    PixelFrame rg;
    ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
    EXPECT_TRUE(rg.renderGraph);
    EXPECT_TRUE(Close(rg.At(multiProbes[0].x, multiProbes[0].y),
                      kExpectedMultiRed, 2, true));
    EXPECT_TRUE(Close(rg.At(multiProbes[1].x, multiProbes[1].y),
                      ExpectedAmbient(0.0f, 0.0f, 1.0f, 1.0f), 2, true));
}

TEST(MeshProductionPixelTest, MissingLaterMaterialFallsBackToSlotZero) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto redPng = MakePng(4, 4, 4, 230, 20, 20, 255);
    std::string guid;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(
            project, session.handle, "fallback_quad",
            {MakeQuad(-3.0f, 0.0f, 1.5f), MakeQuad(3.0f, 0.0f, 1.5f)},
            {MaterialDef{"MatRed", {1, 1, 1}, 1.0f, true},
             MaterialDef{"MatBlue", {0, 0, 1}, 1.0f, false}},
            redPng);
        guid = f.guidHex;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    Json manifest;
    std::string scene;
    {
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        ASSERT_TRUE(store.ReadCommittedScene("assets/Main.scene", &scene));
        manifest = ReadJson(fs::path(store.CommittedManifestPath()));
    }
    Json filtered = Json::array();
    for (const auto& asset : manifest["assets"]) {
        const std::string path = asset["path"].get<std::string>();
        if (path.find(".m1.") != std::string::npos) continue;
        filtered.push_back(asset);
    }
    manifest["assets"] = filtered;
    {
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        const RevisionPublishResult r =
            store.Publish(project.projectRoot, manifest.dump(), scene);
        ASSERT_TRUE(r.ok && r.committed) << r.diagnostic;
    }
    PixelHost host;
    ASSERT_TRUE(host.Open(project.root)) << host.Error();
    PixelFrame direct;
    ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
    int32_t status = -1;
    ASSERT_TRUE(host.QueryMesh(guid, &status));
    EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
    const auto probes = MultiQuadProbes(direct.width, direct.height);
    ASSERT_EQ(probes.size(), 2u);
    EXPECT_TRUE(Close(direct.At(probes[0].x, probes[0].y),
                      kExpectedMultiRed, 2, true));
    EXPECT_TRUE(Close(direct.At(probes[1].x, probes[1].y),
                      kExpectedMultiRed, 2, true))
        << "missing slot 1 must fall back to slot 0";
    PixelFrame rg;
    ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
    EXPECT_TRUE(rg.renderGraph);
    EXPECT_TRUE(Close(rg.At(probes[1].x, probes[1].y),
                      kExpectedMultiRed, 2, true));
}

TEST(MeshProductionPixelTest, MaterialEditUpdatesProductionPixels) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    std::string guid;
    int32_t materialIndex = -1;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(
            project, session.handle, "edit_material",
            {MakeQuad(0.0f, 0.0f, 4.0f)},
            {MaterialDef{"MatWhite", {1, 1, 1}, 1.0f, false}}, {});
        guid = f.guidHex;
        for (const AssetRef& a : f.assets)
            if (a.path.find(".material.json") != std::string::npos)
                materialIndex = a.index;
        ASSERT_GE(materialIndex, 0);
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    PixelHost host;
    ASSERT_TRUE(host.Open(project.root)) << host.Error();
    PixelFrame before;
    ASSERT_TRUE(host.Pump(false, &before)) << host.Error();
    ExpectObjectProbes(before, ExpectedAmbient(1.0f, 1.0f, 1.0f, 1.0f), 2);

    ASSERT_TRUE(host.UpdateMaterial(
        materialIndex, R"({"baseColorFactor":[1.0,0.0,0.0,1.0]})"))
        << host.Error();
    PixelFrame after;
    ASSERT_TRUE(host.Pump(false, &after)) << host.Error();
    ExpectObjectProbes(after, ExpectedAmbient(1.0f, 0.0f, 0.0f, 1.0f), 2);
    PixelFrame rg;
    ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
    EXPECT_TRUE(rg.renderGraph);
    ExpectObjectProbes(rg, ExpectedAmbient(1.0f, 0.0f, 0.0f, 1.0f), 2);
}

TEST(MeshProductionPixelTest, AlphaPreservedWithoutExtraGamma) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto alphaPng = MakePng(4, 4, 4, 230, 140, 40, 128);
    const auto opaquePng = MakePng(4, 4, 3, 230, 140, 40);
    std::string alphaGuid, opaqueGuid;
    std::vector<AssetRef> assets;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        alphaGuid = WriteAndImport(project, session.handle, "alpha_quad",
                                   {MakeQuad(0.0f, 0.0f, 4.0f)},
                                   {kRedMaterial}, alphaPng)
                        .guidHex;
        opaqueGuid = WriteAndImport(project, session.handle, "opaque_quad",
                                    {MakeQuad(0.0f, 0.0f, 4.0f)},
                                    {kRedMaterial}, opaquePng)
                         .guidHex;
        assets = ListAssets(session.handle);
        session.SetSingleMeshGuid(alphaGuid);
        ASSERT_TRUE(session.Save());
    }
    bool sawAlpha = false, sawOpaque = false;
    for (const AssetRef& tex : AssetsWithSuffix(assets, ".tex.png")) {
        const Json sidecar = TextureSidecar(project, tex.path);
        if (sidecar["role"] != "basecolor") continue;
        if (tex.path.find("alpha_quad") != std::string::npos) {
            sawAlpha = true;
            EXPECT_EQ(sidecar["components"], 4);
        }
        if (tex.path.find("opaque_quad") != std::string::npos) {
            sawOpaque = true;
            EXPECT_EQ(sidecar["components"], 4);
        }
    }
    EXPECT_TRUE(sawAlpha);
    EXPECT_TRUE(sawOpaque);

    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        ExpectObjectProbes(frame, kExpectedAlpha, 2);
        for (const Probe& p : BackgroundProbes(frame.width, frame.height)) {
            EXPECT_EQ(frame.At(p.x, p.y).a, 255);
        }
    }
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        session.SetSingleMeshGuid(opaqueGuid);
        ASSERT_TRUE(session.Save());
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        ExpectObjectProbes(frame, ExpectedAmbient(SrgbDecode(230),
                                                  SrgbDecode(140),
                                                  SrgbDecode(40), 1.0f),
                           2);
        for (const Probe& p : SingleQuadProbes(frame.width, frame.height)) {
            EXPECT_EQ(frame.At(p.x, p.y).a, 255);
        }
    }
}

TEST(MeshProductionPixelTest, ReimportKeepsGuidAndUpdatesPixels) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const std::string objDir = "assets/models/";
    const std::string objPath = objDir + "reimport.obj";
    project.WriteText(objPath, ObjForQuads(
                                    {MakeQuad(0.0f, 0.0f, 4.0f)},
                                    {"MatRed"}, "reimport.mtl"));
    project.WriteText(objDir + "reimport.mtl",
                      MtlText("reimport.png", {kRedMaterial}));
    project.WriteBytes(objDir + "reimport.png",
                       MakePng(4, 4, 4, 188, 64, 32, 255));

    std::string guid;
    std::string firstTexturePath;
    int64_t revision1 = -1;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const ImportedModel model = ImportModel(session.handle, objPath);
        ASSERT_GE(model.index, 0);
        guid = model.guidHex;
        const auto teximages = AssetsWithSuffix(ListAssets(session.handle),
                                                ".tex.png");
        ASSERT_EQ(teximages.size(), 1u);
        firstTexturePath = teximages[0].path;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
        revision1 = CurrentRevision(project.root);
    }
    ASSERT_GT(revision1, 0);

    Rgba firstProbes[4] = {};
    int firstCoverage = 0;
    uint64_t firstIdentity = 0, firstDigest = 0;
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame direct;
        ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMeshKey(guid, &status, &firstIdentity,
                                      &firstDigest));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
        const auto probes = SingleQuadProbes(direct.width, direct.height);
        for (size_t i = 0; i < probes.size(); ++i)
            firstProbes[i] = direct.At(probes[i].x, probes[i].y);
        ExpectObjectProbes(direct, kExpectedSrgb188, 2);
        firstCoverage = CountNonBackground(direct);
        PixelFrame rg;
        ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
        EXPECT_TRUE(rg.renderGraph);
        ExpectObjectProbes(rg, kExpectedSrgb188, 2);
    }

    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const ImportedModel again = ImportModel(session.handle, objPath);
        EXPECT_EQ(again.guidHex, guid);
        EXPECT_EQ(CurrentRevision(project.root), revision1);
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        uint64_t identity = 0, digest = 0;
        ASSERT_TRUE(host.QueryMeshKey(guid, &status, &identity, &digest));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
        EXPECT_EQ(identity, firstIdentity);
        EXPECT_EQ(digest, firstDigest);
        const auto probes = SingleQuadProbes(frame.width, frame.height);
        for (size_t i = 0; i < probes.size(); ++i)
            EXPECT_TRUE(Close(frame.At(probes[i].x, probes[i].y),
                              firstProbes[i], 1, true));
        EXPECT_EQ(CountNonBackground(frame), firstCoverage);
    }

    project.WriteText(objPath, ObjForQuads(
                                    {MakeQuad(0.0f, 0.0f, 4.2f)},
                                    {"MatRed"}, "reimport.mtl"));
    project.WriteBytes(objDir + "reimport.png",
                       MakePng(4, 4, 4, 40, 200, 80, 255));
    int64_t revision2 = -1;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const ImportedModel changed = ImportModel(session.handle, objPath);
        EXPECT_EQ(changed.guidHex, guid);
        const auto teximages = AssetsWithSuffix(ListAssets(session.handle),
                                                ".tex.png");
        bool sawNewTexture = false;
        for (const AssetRef& tex : teximages)
            if (tex.path != firstTexturePath) sawNewTexture = true;
        EXPECT_TRUE(sawNewTexture);
        revision2 = CurrentRevision(project.root);
        EXPECT_GT(revision2, revision1);
    }
    const Rgba expectedChanged = ExpectedAmbient(
        SrgbDecode(40), SrgbDecode(200), SrgbDecode(80), 1.0f);
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame direct;
        ASSERT_TRUE(host.Pump(false, &direct)) << host.Error();
        int32_t status = -1;
        uint64_t identity = 0, digest = 0;
        ASSERT_TRUE(host.QueryMeshKey(guid, &status, &identity, &digest));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
        EXPECT_EQ(identity, firstIdentity);
        EXPECT_NE(digest, firstDigest);
        ExpectBackgroundProbes(direct);
        ExpectObjectProbes(direct, expectedChanged, 2);
        PixelFrame rg;
        ASSERT_TRUE(host.Pump(true, &rg)) << host.Error();
        EXPECT_TRUE(rg.renderGraph);
        ExpectObjectProbes(rg, expectedChanged, 2);
    }
}

TEST(MeshProductionPixelTest, MissingAndInvalidAssetsProduceNoFakePixels) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject missingProject;

    const std::string missingGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    {
        Session session;
        ASSERT_TRUE(session.Open(missingProject.root));
        session.SetSingleMeshGuid(missingGuid);
        ASSERT_TRUE(session.Save());
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(missingProject.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMesh(missingGuid, &status));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::MissingAsset));
        EXPECT_EQ(host.GpuMeshCount(), 0u);
        ExpectBackgroundOnly(frame);
        ExpectBackgroundProbes(frame);
    }

    PixelProject invalidProject;
    invalidProject.WriteText("assets/bad.11223344.mesh", "not-a-mesh");
    const std::string invalidGuid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    {
        Session session;
        ASSERT_TRUE(session.Open(invalidProject.root));
        session.SetSingleMeshGuid(invalidGuid);
        ASSERT_TRUE(session.Save());
    }
    {
        RevisionStore store;
        (void)store.Open(invalidProject.projectRoot);
        std::string scene;
        ASSERT_TRUE(store.ReadCommittedScene("assets/Main.scene", &scene));
        ContentRegistry registry;
        ASSERT_TRUE(registry.RegisterExplicit(
            ResourceGUID::FromHex(invalidGuid), "assets/bad.11223344.mesh",
            Content::AssetType::Mesh));
        const RevisionPublishResult published = store.Publish(
            invalidProject.projectRoot, registry.SerializeManifest(), scene);
        ASSERT_TRUE(published.ok && published.committed)
            << published.diagnostic;
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(invalidProject.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMesh(invalidGuid, &status));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::ReaderFailure));
        EXPECT_EQ(host.GpuMeshCount(), 0u);
        ExpectBackgroundOnly(frame);
        ExpectBackgroundProbes(frame);
    }
}

TEST(MeshProductionPixelTest, MissingMaterialTextureAndDecodeFailure) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto png = MakePng(4, 4, 4, 188, 64, 32, 255);
    std::string guid;
    std::string materialPath;
    std::string texturePath;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(project, session.handle, "fail_quad",
                                         {MakeQuad(0.0f, 0.0f, 4.0f)},
                                         {kRedMaterial}, png);
        guid = f.guidHex;
        materialPath = AssetsWithSuffix(f.assets, ".material.json")[0].path;
        texturePath = AssetsWithSuffix(f.assets, ".tex.png")[0].path;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    std::string scene;
    {
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        ASSERT_TRUE(store.ReadCommittedScene("assets/Main.scene", &scene));
    }
    Json originalManifest;
    {
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        originalManifest = ReadJson(
            fs::path(store.CommittedManifestPath()));
    }

    auto publishFiltered = [&](const std::string& excludedSuffix) {
        Json manifest = originalManifest;
        Json filtered = Json::array();
        for (const auto& asset : manifest["assets"]) {
            const std::string path = asset["path"].get<std::string>();
            if (path.size() >= excludedSuffix.size() &&
                path.compare(path.size() - excludedSuffix.size(),
                             excludedSuffix.size(), excludedSuffix) == 0)
                continue;
            filtered.push_back(asset);
        }
        manifest["assets"] = filtered;
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        const RevisionPublishResult r = store.Publish(
            project.projectRoot, manifest.dump(), scene);
        EXPECT_TRUE(r.ok && r.committed) << r.diagnostic;
    };
    auto expectFailure = [&](MeshRuntimeStatus expected) {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMesh(guid, &status));
        EXPECT_EQ(status, static_cast<int32_t>(expected));
        EXPECT_EQ(host.GpuMeshCount(), 0u);
        ExpectBackgroundOnly(frame);
        ExpectBackgroundProbes(frame);
    };

    publishFiltered(".material.json");
    expectFailure(MeshRuntimeStatus::MaterialBindingFailure);

    publishFiltered(".tex.png");
    expectFailure(MeshRuntimeStatus::TextureBindingFailure);

    {
        RevisionStore store;
        (void)store.Open(project.projectRoot);
        const RevisionPublishResult restored = store.Publish(
            project.projectRoot, originalManifest.dump(), scene);
        ASSERT_TRUE(restored.ok && restored.committed) << restored.diagnostic;
    }
    project.WriteText(texturePath, "not-a-png");
    expectFailure(MeshRuntimeStatus::TextureBindingFailure);
}

TEST(MeshProductionPixelTest, GpuUploadFailureThenRecovery) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    PixelProject project;
    const auto png = MakePng(4, 4, 4, 188, 64, 32, 255);
    std::string guid;
    {
        Session session;
        ASSERT_TRUE(session.Open(project.root));
        const Fixture f = WriteAndImport(project, session.handle, "gpu_quad",
                                         {MakeQuad(0.0f, 0.0f, 4.0f)},
                                         {kRedMaterial}, png);
        guid = f.guidHex;
        session.SetSingleMeshGuid(guid);
        ASSERT_TRUE(session.Save());
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        host.SetGpuUploadFailure(
            static_cast<int32_t>(MeshGpuUploadStage::AfterVertexArray));
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMesh(guid, &status));
        EXPECT_EQ(status,
                  static_cast<int32_t>(MeshRuntimeStatus::GPUUploadFailure));
        EXPECT_EQ(host.GpuMeshCount(), 0u);
        ExpectBackgroundOnly(frame);
        host.SetGpuUploadFailure(
            static_cast<int32_t>(MeshGpuUploadStage::None));
    }
    {
        PixelHost host;
        ASSERT_TRUE(host.Open(project.root)) << host.Error();
        PixelFrame frame;
        ASSERT_TRUE(host.Pump(false, &frame)) << host.Error();
        int32_t status = -1;
        ASSERT_TRUE(host.QueryMesh(guid, &status));
        EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
        EXPECT_EQ(host.GpuMeshCount(), 1u);
        ExpectObjectProbes(frame, kExpectedSrgb188, 2);
    }
}
