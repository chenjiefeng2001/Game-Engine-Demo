#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// P6-1B B5-G3: CPU-only reader for B4 texture assets (normalized PNG) plus
// sidecar metadata. No CPU gamma, no preprocessing; decode mirrors the runtime
// OpenGLTexture orientation convention (stb flip=true) so callers never need a
// second conversion.
struct TextureAssetData
{
    bool ok = false;
    std::string error;
    std::string role;        // sidecar role token; "legacy" when no sidecar
    std::string colorspace;  // "sRGB" | "linear"
    uint32_t ruleVersion = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sourceComponents = 0; // channels in encoded PNG (3 or 4)
    bool hasAlpha = false;
    bool legacyNoSidecar = false;
    std::vector<uint8_t> rgba8; // always 4-channel upload-ready pixels
};

// sidecarBytes may be empty (B4 legacy compatibility: Linear, no role).
TextureAssetData ReadTextureAsset(const std::vector<uint8_t>& encodedPng,
                                  const std::vector<uint8_t>& sidecarBytes);

} // namespace Engine::AssetImport
