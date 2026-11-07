#pragma once

#include "ImportDiagnostic.h"

#include <cstdint>
#include <vector>

namespace Engine::AssetImport {

struct GlbChunkView
{
    uint32_t type = 0;
    uint32_t length = 0;
    const uint8_t* data = nullptr;
};

struct GlbReadResult
{
    bool ok = false;
    const char* errorCode = nullptr;
    std::vector<ImportDiagnostic> diagnostics;

    const uint8_t* jsonData = nullptr; // null-terminated not guaranteed; use length
    uint32_t jsonLength = 0;
    bool hasBin = false;
    GlbChunkView bin;
};

// Parses the GLB 2.0 binary container (12-byte header + chunks).
// Validation is fail-closed: magic/version/length/bounds/alignment/order are
// all checked; unknown chunks are rejected (P6-P1A section 3).
GlbReadResult ReadGlb(const uint8_t* data, size_t size);

inline constexpr uint32_t kGlbMagicTfGlu = 0x46546C67u; // 'glTF'
inline constexpr uint32_t kGlbChunkJson = 0x4E4F534Au;  // 'JSON'
inline constexpr uint32_t kGlbChunkBin = 0x004E4942u;   // 'BIN\0'

} // namespace Engine::AssetImport
