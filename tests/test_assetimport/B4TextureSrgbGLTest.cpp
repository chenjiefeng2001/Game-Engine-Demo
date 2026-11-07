// P6-1B B4-C runtime internal-format verification. Real GL context + real
// texture upload; asserts glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)
// per colorspace sidecar with legacy default Linear.
//
// Uses the shared DebugDrawGLFixture (same pattern as test_debugdraw and
// test_pick; NOT a mock GL).
#include "../test_debugdraw/DebugDrawGLFixture.h"

#include <gtest/gtest.h>

#include <glad/gl.h>

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Core/RenderResources/Texture.h>

#include <nlohmann/json.hpp>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Build a 2x2 RGBA PNG manually (same pattern as B4TexturePreprocessTest).
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
        for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    auto push32BE = [&](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x >> 24 & 0xFF); v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF); v.push_back(x & 0xFF);
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
            raw.push_back(r); raw.push_back(g); raw.push_back(b);
            if (channels == 4) raw.push_back(255);
        }
    }
    std::vector<uint8_t> idat;
    idat.push_back(0x78); idat.push_back(0x01);
    const uint16_t len = static_cast<uint16_t>(raw.size());
    idat.push_back(0x01);
    idat.push_back(len & 0xFF); idat.push_back(len >> 8);
    idat.push_back(~len & 0xFF); idat.push_back((~len >> 8) & 0xFF);
    idat.insert(idat.end(), raw.begin(), raw.end());
    uint32_t a1 = 1, a2 = 0;
    for (const uint8_t v : raw) { a1 = (a1 + v) % 65521u; a2 = (a2 + a1) % 65521u; }
    const uint32_t adler = (a2 << 16) | a1;
    push32BE(idat, adler);

    std::vector<uint8_t> png;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    push32BE(ihdr, 2); push32BE(ihdr, 2);
    ihdr.push_back(8);
    ihdr.push_back(static_cast<uint8_t>(channels == 4 ? 6 : 2));
    ihdr.push_back(0);
    ihdr.push_back(0); ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", idat);
    appendChunk(png, "IEND", {});
    return png;
}

void WriteSidecar(const fs::path& png, bool srgb) {
    nlohmann::json j;
    j["format"] = "texture-meta";
    j["ruleVersion"] = 1;
    j["role"] = srgb ? "basecolor" : "normal";
    j["colorspace"] = srgb ? "sRGB" : "linear";
    j["width"] = 2; j["height"] = 2; j["components"] = 4;
    std::ofstream f(png.string() + ".tex.meta.json",
                    std::ios::binary | std::ios::trunc);
    f << j.dump();
}

std::string UniqueTempDir(const char* prefix) {
    const auto stamp = std::time(nullptr);
    return std::string(prefix) + std::to_string(stamp);
}

// Binds the texture object's unit via Texture::Bind, then reads the effective
// internal format for the bound texture object through legacy queries on the
// fixture's GladGLContext.
int ReadInternalFormatOf(Engine::Texture& texture, GladGLContext& gl) {
    texture.Bind(0);
    GLint fmt = 0;
    gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                              &fmt);
    return static_cast<int>(fmt);
}

} // namespace

TEST(AssetTextureGLTest, SrgbMetaChoosesSRGBInternalFormat) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    fs::path tmp = fs::temp_directory_path() /
                   UniqueTempDir("gd_b4_srgb_");
    std::error_code ec;
    fs::create_directories(tmp, ec);
    ASSERT_FALSE(ec);
    const fs::path png = tmp / "color.tex.png";
    {
        std::ofstream f(png, std::ios::binary);
        const auto pngBytes = MakeTinyPng2x2(200, 30, 30);
        f.write(reinterpret_cast<const char*>(pngBytes.data()),
                static_cast<std::streamsize>(pngBytes.size()));
    }
    WriteSidecar(png, /*srgb*/ true);

    auto& factory = fixture.Factory();
    std::shared_ptr<Engine::Texture> texture =
        factory.CreateTexture(png.string());
    ASSERT_NE(texture, nullptr);
    const int fmt = ReadInternalFormatOf(*texture, fixture.GL());
    EXPECT_EQ(fmt, static_cast<int>(GL_SRGB8_ALPHA8));
}

TEST(AssetTextureGLTest, LinearMetaKeepsLinearInternalFormat) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    fs::path tmp = fs::temp_directory_path() /
                   UniqueTempDir("gd_b4_lin_");
    std::error_code ec;
    fs::create_directories(tmp, ec);
    ASSERT_FALSE(ec);
    const fs::path png = tmp / "normal.tex.png";
    {
        std::ofstream f(png, std::ios::binary);
        const auto pngBytes = MakeTinyPng2x2(90, 120, 200);
        f.write(reinterpret_cast<const char*>(pngBytes.data()),
                static_cast<std::streamsize>(pngBytes.size()));
    }
    WriteSidecar(png, /*srgb*/ false);

    auto& factory = fixture.Factory();
    std::shared_ptr<Engine::Texture> texture =
        factory.CreateTexture(png.string());
    ASSERT_NE(texture, nullptr);
    const int fmt = ReadInternalFormatOf(*texture, fixture.GL());
    EXPECT_EQ(fmt, static_cast<int>(GL_RGBA8));
}

TEST(AssetTextureGLTest, LinearRgbDataUsesRgb8InternalFormat) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    fs::path tmp = fs::temp_directory_path() /
                   UniqueTempDir("gd_g5_rgb_data_");
    std::error_code ec;
    fs::create_directories(tmp, ec);
    ASSERT_FALSE(ec);
    const fs::path png = tmp / "normal.tex.png";
    {
        std::ofstream f(png, std::ios::binary);
        const auto pngBytes = MakeTinyPng2x2(90, 120, 200, 3);
        f.write(reinterpret_cast<const char*>(pngBytes.data()),
                static_cast<std::streamsize>(pngBytes.size()));
    }
    nlohmann::json meta = {{"format", "texture-meta"},
                           {"ruleVersion", 1},
                           {"role", "normal"},
                           {"colorspace", "linear"},
                           {"width", 2},
                           {"height", 2},
                           {"components", 3},
                           {"sourceIdentity", "test"}};
    {
        std::ofstream f(png.string() + ".tex.meta.json", std::ios::binary);
        const std::string text = meta.dump();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    auto& factory = fixture.Factory();
    std::shared_ptr<Engine::Texture> texture =
        factory.CreateTexture(png.string());
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(ReadInternalFormatOf(*texture, fixture.GL()),
              static_cast<int>(GL_RGB8));
}

TEST(AssetTextureGLTest, NoMetaLegacyDefaultsLinear) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    fs::path tmp = fs::temp_directory_path() /
                   UniqueTempDir("gd_b4_legacy_");
    std::error_code ec;
    fs::create_directories(tmp, ec);
    ASSERT_FALSE(ec);
    const fs::path png = tmp / "legacy.png";
    {
        std::ofstream f(png, std::ios::binary);
        const auto pngBytes = MakeTinyPng2x2(10, 90, 200);
        f.write(reinterpret_cast<const char*>(pngBytes.data()),
                static_cast<std::streamsize>(pngBytes.size()));
    }
    auto& factory = fixture.Factory();
    std::shared_ptr<Engine::Texture> texture =
        factory.CreateTexture(png.string());
    ASSERT_NE(texture, nullptr);
    const int fmt = ReadInternalFormatOf(*texture, fixture.GL());
    EXPECT_EQ(fmt, static_cast<int>(GL_RGBA8));
}
