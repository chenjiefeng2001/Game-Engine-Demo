#pragma once

#include <cstddef>
#include <cstdint>

namespace Engine::AssetImport {

// P6-P1A contract section 7 caps; concentration point for all import security limits.
inline constexpr size_t kMaxSourceFileBytes = 512ull * 1024 * 1024;
inline constexpr uint32_t kMaxBuffers = 64;
inline constexpr uint32_t kMaxImages = 64;
inline constexpr uint32_t kMaxMeshes = 128;
inline constexpr uint32_t kMaxPrimitives = 1024;
inline constexpr uint32_t kMaxNodeDepth = 64;
inline constexpr size_t kMaxTotalVertices = 16ull * 1024 * 1024;
inline constexpr uint32_t kMaxSceneNodes = 65536;
inline constexpr uint32_t kMaxMaterials = 4096;
// B4-A reported caps (were missing before this gate; recorded in B4 record):
// dimension cap and total-pixel cap protect decode allocation; RGBA byte size
// derived (x4) stays under ~256MB.
inline constexpr uint32_t kMaxTextureDim = 16384u;
inline constexpr uint64_t kMaxTexturePixels = 67108864ull; // 64 * 1024 * 1024

} // namespace Engine::AssetImport
