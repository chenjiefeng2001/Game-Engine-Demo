#pragma once

#include "MeshImportIR.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::AssetImport {

// OBJ (Wavefront) static-geometry parser feeding the shared MeshImportIR
// (P6-P1A contract section 3 OBJ/MTL row). Source-order spans, per-file
// vertex dedupe on (v,vt,vn) tuples, negative/relative indices, fan
// triangulation for polygons, per-vertex attribute flags.
//
// Decisions recorded in docs/P6-1B-GateB2-ObjMtl-Record.md: UV flip applies
// once here (B1 protocol), materials resolve through mtllib + usemtl with
// synthetic fallback slots for missing definitions, smoothing groups emit a
// single ignore-warning per file, unknown directives warn once per token.
class ObjParser
{
public:
    ObjParser() = delete;
    // The OBJ source bytes; external files (mtllib) are resolved relative to
    // sourceDir with the same URI rules as the glTF path.
    static ImportResult ParseObj(std::vector<uint8_t> objBytes,
                                 const std::string& sourceDir);
};

} // namespace Engine::AssetImport
