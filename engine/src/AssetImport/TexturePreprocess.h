#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// B4-A texture preprocessing (contract section 8.2 of the P6-P1A plan):
// decode (stb_image, repo-vendored), validate against ImportLimits caps,
// normalize to 8-bit RGB(A) PNG via a repo-internal minimal PNG encoder
// (uncompressed zlib stored blocks + CRC32/adler32 — NOT a third-party
// dependency; stb_image_write is not vendored), and classify colorspace
// per texture role.
//
// Determinism: identical input bytes -> identical output PNG bytes, and
// sidecar bytes. No resize, no lossy compression, no mipmap, no texture
// compression at this stage.

enum class TextureRole : uint8_t {
    BaseColor = 0,      // color, sRGB
    Emissive = 1,       // color, sRGB
    Normal = 2,         // data, linear
    Occlusion = 3,      // data, linear
    MetallicRoughness = 4, // data, linear
    Unknown = 5,
};

inline bool IsColorRole(TextureRole role) {
    return role == TextureRole::BaseColor || role == TextureRole::Emissive;
}

enum class TextureColorspace : uint8_t { Linear = 0, sRGB = 1 };

struct TexturePreprocessResult
{
    bool ok = false;
    std::string error;    // stable ImportDiagnostic code string
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t components = 0;    // 3 or 4 (normalized output channels)
    bool hasAlpha = false;
    TextureColorspace colorspace = TextureColorspace::Linear;
    // Normalized PNG bytes (row order: source rows verbatim; no orientation
    // flip is applied by the encoder).
    std::vector<uint8_t> pngBytes;
};

// In-memory preprocess. Fail-closed on decode failure / cap violations.
TexturePreprocessResult PreprocessTexture(const std::vector<uint8_t>& source,
                                          TextureRole role);

} // namespace Engine::AssetImport
