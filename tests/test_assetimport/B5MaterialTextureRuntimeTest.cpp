#include "../test_debugdraw/DebugDrawGLFixture.h"
#include "TestSupport.h"

#include "AssetImport/MaterialAssetReader.h"
#include "AssetImport/MaterialRuntimeBinding.h"
#include "AssetImport/MeshMaterialResolver.h"
#include "AssetImport/TextureAssetReader.h"
#include "Engine/Core/RHI/TextureGpuResource.h"
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>

#include <gtest/gtest.h>
#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace {

using namespace Engine::AssetImport;
using AssetImportTest::Json;
using AssetImportTest::ScopedTempDir;

constexpr const char* kMeshPath = "proj/sub/alpaca.89abcdef.mesh";
constexpr const char* kMat0Path = "proj/sub/alpaca.m0.00112233.material.json";
constexpr const char* kMat1Path = "proj/sub/alpaca.m1.44556677.material.json";
constexpr const char* kTexPath =
    "proj/sub/tex/alpaca.basecolor.12345678.tex.png";

std::vector<uint8_t> MakeTinyPng2x2(uint8_t r, uint8_t g, uint8_t b,
                                    int channels = 4) {
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
    auto push32BE = [&](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x >> 24 & 0xFF);
        v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF);
        v.push_back(x & 0xFF);
    };
    auto appendChunk = [&](std::vector<uint8_t>& png, const char* type,
                           const std::vector<uint8_t>& data) {
        push32BE(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> crcIn;
        for (int k = 0; k < 4; ++k) crcIn.push_back(uint8_t(type[k]));
        crcIn.insert(crcIn.end(), data.begin(), data.end());
        for (int k = 0; k < 4; ++k) png.push_back(static_cast<uint8_t>(type[k]));
        png.insert(png.end(), data.begin(), data.end());
        push32BE(png, crc32(&crcIn[0], crcIn.size()));
    };

    std::vector<uint8_t> raw;
    for (uint32_t y = 0; y < 2; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < 2; ++x) {
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            if (channels == 4) raw.push_back(255);
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
    for (const uint8_t v : raw) {
        a1 = (a1 + v) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    push32BE(idat, (a2 << 16) | a1);

    std::vector<uint8_t> png;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    push32BE(ihdr, 2);
    push32BE(ihdr, 2);
    ihdr.push_back(8);
    ihdr.push_back(static_cast<uint8_t>(channels == 4 ? 6 : 2));
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", idat);
    appendChunk(png, "IEND", {});
    return png;
}

std::vector<uint8_t> MakeSidecar(const char* role, const char* colorspace,
                                 int width = 2, int height = 2,
                                 int components = 4) {
    Json j;
    j["format"] = "texture-meta";
    j["ruleVersion"] = 1;
    j["role"] = role;
    j["colorspace"] = colorspace;
    j["width"] = width;
    j["height"] = height;
    j["components"] = components;
    j["sourceIdentity"] = "test";
    const std::string s = j.dump();
    return std::vector<uint8_t>(s.begin(), s.end());
}

Json TexRef(const std::string& assetPath) {
    if (assetPath.empty())
        return Json::object({{"present", false}, {"imageIndex", 4294967295u}});
    return Json::object({{"present", true},
                         {"imageIndex", 0},
                         {"assetPath", assetPath}});
}

std::string MaterialJson(uint32_t slot, const std::string& name,
                         const std::string& baseColorAsset) {
    Json j;
    j["format"] = "material";
    j["formatVersion"] = 1;
    j["slot"] = slot;
    j["name"] = name;
    j["baseColorFactor"] = Json::array({0.25f, 0.5f, 0.75f, 1.0f});
    j["metallicFactor"] = 0.1f;
    j["roughnessFactor"] = 0.9f;
    j["emissiveFactor"] = Json::array({0.0f, 0.0f, 0.0f});
    j["alphaCutoff"] = 0.5f;
    j["alphaMode"] = "OPAQUE";
    j["baseColorTexture"] = TexRef(baseColorAsset);
    j["normalTexture"] = TexRef("");
    j["occlusionTexture"] = TexRef("");
    j["emissiveTexture"] = TexRef("");
    j["metallicRoughnessTexture"] = TexRef("");
    return j.dump();
}

struct FakeAssets
{
    std::unordered_map<std::string, std::vector<uint8_t>> files;
    std::unordered_map<std::string, std::string> diskPaths;
    std::vector<std::pair<std::string, std::string>> registry;

    void Add(const std::string& path, const std::vector<uint8_t>& bytes,
             const char* type) {
        files[path] = bytes;
        registry.emplace_back(path, type);
    }
    void AddText(const std::string& path, const std::string& text,
                 const char* type) {
        Add(path, std::vector<uint8_t>(text.begin(), text.end()), type);
    }
    MaterialAssetIo Io() const {
        MaterialAssetIo io;
        io.readBytes = [this](const std::string& p, std::vector<uint8_t>& out) {
            const auto it = files.find(p);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
        io.toAbsolutePath = [this](const std::string& p) {
            const auto it = diskPaths.find(p);
            return it == diskPaths.end() ? std::string() : it->second;
        };
        return io;
    }
};

FakeAssets MakeBasicAssets(const std::vector<uint8_t>& png,
                           const std::vector<uint8_t>& sidecar,
                           const std::string& mat0 = {},
                           const std::string& mat1 = {}) {
    FakeAssets assets;
    assets.Add("proj/sub/alpaca.89abcdef.mesh", {}, "Mesh");
    assets.AddText(kMat0Path,
                   mat0.empty() ? MaterialJson(0, "MatA", kTexPath) : mat0,
                   "Material");
    assets.AddText(kMat1Path,
                   mat1.empty() ? MaterialJson(1, "MatB", "") : mat1,
                   "Material");
    assets.Add(kTexPath, png, "Texture");
    assets.files[std::string(kTexPath) + ".tex.meta.json"] = sidecar;
    return assets;
}

int ReadBoundInternalFormat(Engine::Texture& texture, GladGLContext& gl) {
    texture.Bind(0);
    GLint fmt = 0;
    gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                              &fmt);
    return static_cast<int>(fmt);
}

} // namespace

TEST(MaterialAssetReaderTest, ValidV1MaterialParsesTextureRefs) {
    const std::string text = MaterialJson(3, "Mat", kTexPath);
    const std::vector<uint8_t> bytes(text.begin(), text.end());
    const auto r = Engine::AssetImport::ReadMaterialAssetJson(bytes);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.material.formatVersion, 1u);
    EXPECT_EQ(r.material.slot, 3u);
    EXPECT_EQ(r.material.name, "Mat");
    ASSERT_TRUE(r.material.baseColor.present);
    EXPECT_EQ(r.material.baseColor.assetPath, kTexPath);
    EXPECT_FALSE(r.material.normal.present);
}

TEST(MaterialAssetReaderTest, RejectsUnknownFieldVersionAndAlphaMode) {
    Json base = Json::parse(MaterialJson(0, "Mat", ""));
    Json unknown = base;
    unknown["junk"] = 1;
    EXPECT_FALSE(Engine::AssetImport::ReadMaterialAssetJson(
                     AssetImportTest::ToBytes(unknown.dump()))
                     .ok);
    Json badVersion = base;
    badVersion["formatVersion"] = 2;
    EXPECT_FALSE(Engine::AssetImport::ReadMaterialAssetJson(
                     AssetImportTest::ToBytes(badVersion.dump()))
                     .ok);
    Json badAlpha = base;
    badAlpha["alphaMode"] = "WEIRD";
    EXPECT_FALSE(Engine::AssetImport::ReadMaterialAssetJson(
                     AssetImportTest::ToBytes(badAlpha.dump()))
                     .ok);
}

TEST(TextureAssetReaderTest, SrgbAndLegacyDecode) {
    const auto png = MakeTinyPng2x2(200, 30, 30);
    const auto srgb = Engine::AssetImport::ReadTextureAsset(
        png, MakeSidecar("basecolor", "sRGB"));
    ASSERT_TRUE(srgb.ok) << srgb.error;
    EXPECT_EQ(srgb.colorspace, "sRGB");
    EXPECT_EQ(srgb.width, 2u);
    EXPECT_EQ(srgb.height, 2u);
    EXPECT_EQ(srgb.rgba8.size(), 16u);
    const auto legacy = Engine::AssetImport::ReadTextureAsset(png, {});
    ASSERT_TRUE(legacy.ok) << legacy.error;
    EXPECT_TRUE(legacy.legacyNoSidecar);
    EXPECT_EQ(legacy.colorspace, "linear");
}

TEST(TextureAssetReaderTest, RgbPngDecodesToRgba8) {
    const auto png = MakeTinyPng2x2(90, 120, 200, 3);
    const auto r = Engine::AssetImport::ReadTextureAsset(
        png, MakeSidecar("normal", "linear", 2, 2, 3));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.hasAlpha);
    EXPECT_EQ(r.sourceComponents, 3u);
    EXPECT_EQ(r.rgba8.size(), 16u);
    EXPECT_EQ(r.rgba8[3], 255);
}

TEST(TextureAssetReaderTest, RejectsRoleColorConflictAndDimMismatch) {
    const auto png = MakeTinyPng2x2(10, 10, 10);
    EXPECT_FALSE(Engine::AssetImport::ReadTextureAsset(
                     png, MakeSidecar("normal", "sRGB"))
                     .ok);
    EXPECT_FALSE(Engine::AssetImport::ReadTextureAsset(
                     png, MakeSidecar("basecolor", "sRGB", 4, 4, 4))
                     .ok);
    EXPECT_FALSE(Engine::AssetImport::ReadTextureAsset({}, {}).ok);
}

TEST(MeshMaterialResolverTest, UniqueMissingAndAmbiguous) {
    const std::vector<std::pair<std::string, std::string>> registry = {
        {kMeshPath, "Mesh"},
        {kMat0Path, "Material"},
        {kMat1Path, "Material"},
        {"proj/other/alpaca.m0.00112233.material.json", "Material"},
        {"proj/sub/alpaca.m0.00112233.material.json.bak", "Script"}};
    const auto multi = Engine::AssetImport::ResolveMaterialAssetPaths(
        kMeshPath, 2, registry);
    ASSERT_TRUE(multi.ok) << multi.error;
    EXPECT_EQ(multi.slotPaths[0], kMat0Path);
    EXPECT_EQ(multi.slotPaths[1], kMat1Path);
    EXPECT_FALSE(Engine::AssetImport::ResolveMaterialAssetPaths(
                     "proj/sub/alpaca.89abcdef.mesh", 1,
                     {{kMeshPath, "Mesh"}})
                     .ok);
    auto ambiguous = registry;
    ambiguous.emplace_back("proj/sub/alpaca.m0.99999999.material.json",
                           "Material");
    EXPECT_FALSE(Engine::AssetImport::ResolveMaterialAssetPaths(
                     kMeshPath, 1, ambiguous)
                     .ok);
    EXPECT_FALSE(Engine::AssetImport::ResolveMaterialAssetPaths(
                     "proj/sub/alpaca.mesh", 1, registry)
                     .ok);
}

TEST(MaterialRuntimeBindingTest, FirstSlotOnlyWithCpuTextureResolution) {
    const auto png = MakeTinyPng2x2(1, 2, 3);
    FakeAssets assets = MakeBasicAssets(png, MakeSidecar("basecolor", "sRGB"));
    const auto r = Engine::AssetImport::BuildMeshMaterialBinding(
        kMeshPath, {"MatA", "MatB"}, assets.registry, assets.Io());
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_NE(r.binding, nullptr);
    EXPECT_EQ(r.binding->slotCount, 2u);
    EXPECT_TRUE(r.binding->firstMaterialAvailable);
    EXPECT_EQ(r.binding->slots[0].material.slot, 0u);
    EXPECT_EQ(r.binding->slots[1].material.name, "MatB");
    ASSERT_TRUE(r.binding->slots[0].baseColor.present);
    EXPECT_TRUE(r.binding->slots[0].baseColor.resolved);
    EXPECT_TRUE(r.binding->slots[0].baseColor.srgb);
    EXPECT_EQ(r.binding->slots[0].baseColor.gpu, nullptr);
    bool multiWarning = false;
    for (const auto& w : r.binding->warnings)
        if (w.find("multi-material") != std::string::npos) multiWarning = true;
    EXPECT_TRUE(multiWarning);
}

TEST(MaterialRuntimeBindingTest, MissingFirstMaterialFailsClosed) {
    const auto png = MakeTinyPng2x2(1, 2, 3);
    FakeAssets assets = MakeBasicAssets(png, MakeSidecar("basecolor", "sRGB"));
    assets.registry.erase(
        std::remove_if(assets.registry.begin(), assets.registry.end(),
                       [](const auto& e) { return e.first == kMat0Path; }),
        assets.registry.end());
    const auto r = Engine::AssetImport::BuildMeshMaterialBinding(
        kMeshPath, {"MatA", "MatB"}, assets.registry, assets.Io());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("MATERIAL_BINDING"), std::string::npos);
}

TEST(MaterialRuntimeBindingTest, UnregisteredFirstTextureFailsClosed) {
    const auto png = MakeTinyPng2x2(1, 2, 3);
    FakeAssets assets = MakeBasicAssets(png, MakeSidecar("basecolor", "sRGB"));
    assets.registry.erase(
        std::remove_if(assets.registry.begin(), assets.registry.end(),
                       [](const auto& e) { return e.first == kTexPath; }),
        assets.registry.end());
    const auto r = Engine::AssetImport::BuildMeshMaterialBinding(
        kMeshPath, {"MatA", "MatB"}, assets.registry, assets.Io());
    EXPECT_FALSE(r.ok);
}

TEST(MaterialRuntimeBindingTest, InvalidLaterSlotIsWarningOnly) {
    const auto png = MakeTinyPng2x2(1, 2, 3);
    FakeAssets assets = MakeBasicAssets(png, MakeSidecar("basecolor", "sRGB"),
                                        {}, "{not json");
    const auto r = Engine::AssetImport::BuildMeshMaterialBinding(
        kMeshPath, {"MatA", "MatB"}, assets.registry, assets.Io());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.binding->firstMaterialAvailable);
    bool slotWarning = false;
    for (const auto& w : r.binding->warnings)
        if (w.find("slot 1") != std::string::npos) slotWarning = true;
    EXPECT_TRUE(slotWarning);
}

TEST(TextureGpuCacheGLTest, SrgbUploadReuseAndSemanticsGuard) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    ScopedTempDir tmp;
    const auto png = MakeTinyPng2x2(200, 30, 30);
    tmp.Write("color.tex.png", png.data(), png.size());
    const auto sidecar = MakeSidecar("basecolor", "sRGB");
    tmp.Write("color.tex.png.tex.meta.json", sidecar.data(), sidecar.size());
    const std::string abs = tmp.Path() + "/color.tex.png";

    Engine::TextureGpuResourceKey key;
    key.identity = Engine::TextureGpuCache::HashBytes(abs.data(), abs.size());
    key.contentDigest = Engine::TextureGpuCache::HashBytes(png.data(), png.size());
    key.uploadSemantics = 7;
    Engine::TextureGpuCache cache;
    const auto first = cache.Acquire(key, abs, 2, 2, true, fixture.Factory());
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->IsValid());
    EXPECT_TRUE(first->IsSrgb());
    EXPECT_EQ(cache.Size(), 1u);
    const auto second = cache.Acquire(key, abs, 2, 2, true, fixture.Factory());
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(cache.Acquire(key, abs, 2, 2, false, fixture.Factory()), nullptr);
    EXPECT_EQ(ReadBoundInternalFormat(*first->GetTexture(), fixture.GL()),
              static_cast<int>(GL_SRGB8_ALPHA8));
    EXPECT_TRUE(cache.Invalidate(key));
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(MaterialRuntimeBindingTest, GpuTexturesResolvedThroughCache) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    ScopedTempDir tmp;
    const auto png = MakeTinyPng2x2(200, 30, 30);
    tmp.Write("color.tex.png", png.data(), png.size());
    const auto sidecar = MakeSidecar("basecolor", "sRGB");
    tmp.Write("color.tex.png.tex.meta.json", sidecar.data(), sidecar.size());

    FakeAssets assets = MakeBasicAssets(png, sidecar);
    assets.diskPaths[kTexPath] = tmp.Path() + "/color.tex.png";
    Engine::TextureGpuCache cache;
    Engine::AssetImport::MaterialBindingBuildOptions options;
    options.textureCache = &cache;
    options.factory = &fixture.Factory();
    const auto r = Engine::AssetImport::BuildMeshMaterialBinding(
        kMeshPath, {"MatA", "MatB"}, assets.registry, assets.Io(), options);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_NE(r.binding->slots[0].baseColor.gpu, nullptr);
    EXPECT_TRUE(r.binding->slots[0].baseColor.gpu->IsSrgb());
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_TRUE(cache.Clear());
}
