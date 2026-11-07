#pragma once

#include "MeshImportIR.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// Result of an in-memory .mtl parse (P6-P1A contract section 3, OBJ/MTL row).
// Materials preserve file order; images only the referenced texture URIs in
// first-reference order (uri validated per contract section 7, not read).
struct MtlParseResult
{
    bool ok = false;
    std::vector<MaterialIR> materials;
    std::vector<TextureImageIR> images;
    std::vector<ImportDiagnostic> diagnostics;
};

class MtlParser
{
public:
    MtlParser() = delete;
    static MtlParseResult ParseMtl(std::vector<uint8_t> bytes);
};

} // namespace Engine::AssetImport
