#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// C-2 input digest (P6-P1A contract section 6.3 as amended; extended in B4).
//
// Schema history:
//   v1 ("gd-import-digest-v1") — B3: source + parser-read dependencies.
//   v2 ("gd-import-digest-v2") — B4: adds texture entries
//     (|tex:<class>:<identity>:<hash16>) and a trailing preprocessing-rule
//     segment (|rule:texture-preprocess-v1:rgba8-png).
//
// Migration semantics (recorded in B4): v1 → v2 changes the digest for every
// model import. The FIRST re-import after the upgrade therefore produces a
// one-time "migration revision" (new revision, mesh GUID stable via the
// stem-prefix rebind); subsequent unchanged imports hit the unchanged
// fast path. No silent hash-meaning change: the schema token changes.
//
// Determinism: same input bytes -> same hex16. Path keys are caller-
// normalized; bytes hashed FNV-1a-64 (RevisionStore::HashBytes).

enum class DigestSchema : uint8_t { V1 = 1, V2 = 2 };

enum class TextureDigestClass : uint8_t { Color = 0, Data = 1 };

struct DigestDependency
{
    std::string pathKey;      // normalized, caller-provided
    const uint8_t* bytes = nullptr;
    size_t size = 0;
};

// One texture dependency entry (B4). `identity` is caller-provided:
// for external images a normalized relative path; for GLB bufferView images
// a stable inline identity ("glbimg<docImageIndex>"). Deduped by caller.
struct DigestTexture
{
    TextureDigestClass digestClass = TextureDigestClass::Color;
    std::string identity;
    const uint8_t* bytes = nullptr;
    size_t size = 0;
};

struct ImportDigestInput
{
    std::string sourcePathKey;
    const uint8_t* sourceBytes = nullptr;
    size_t sourceSize = 0;
    std::vector<DigestDependency> dependencies; // parser order (deterministic)
    std::vector<DigestTexture> textures;        // B4: deterministic order
    DigestSchema schema = DigestSchema::V2;
};

// Returns lowercase 16-hex digest. Deterministic for identical inputs.
std::string ComputeImportDigest(const ImportDigestInput& input);

} // namespace Engine::AssetImport
