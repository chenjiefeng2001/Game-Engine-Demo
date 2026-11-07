#include "GlbContainerReader.h"

#include <cstring>

namespace Engine::AssetImport {

namespace {

void AddError(std::vector<ImportDiagnostic>& out, const char* code,
              const char* message) {
    ImportDiagnostic d;
    d.severity = ImportSeverity::Error;
    d.code = code;
    d.location = "glb.container";
    d.message = message;
    out.push_back(std::move(d));
}

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

bool Fail(GlbReadResult& r, const char* code, const char* message) {
    AddError(r.diagnostics, code, message);
    r.ok = false;
    r.errorCode = code;
    return false;
}

} // namespace

GlbReadResult ReadGlb(const uint8_t* data, size_t size) {
    GlbReadResult r;

    if (!data || size < 12) {
        Fail(r, kCodeChunkBounds, "GLB container shorter than 12-byte header");
        return r;
    }

    if (ReadU32(data) != kGlbMagicTfGlu) {
        Fail(r, kCodeBadMagic, "unexpected GLB magic");
        return r;
    }

    const uint32_t version = ReadU32(data + 4);
    if (version != 2) {
        Fail(r, kCodeBadVersion, "GLB container version must be 2");
        return r;
    }

    const uint32_t declaredLength = ReadU32(data + 8);
    if (declaredLength != size) {
        Fail(r, kCodeLengthMismatch, "declared GLB length differs from byte count");
        return r;
    }

    size_t cursor = 12;
    bool jsonSeen = false;
    bool binSeen = false;

    while (cursor < size) {
        if (size - cursor < 8) {
            Fail(r, kCodeChunkBounds, "truncated chunk header");
            return r;
        }
        const uint32_t chunkLength = ReadU32(data + cursor);
        const uint32_t chunkType = ReadU32(data + cursor + 4);
        cursor += 8;

        if (chunkLength % 4 != 0) {
            Fail(r, kCodeChunkUnaligned, "chunk length must be a multiple of 4");
            return r;
        }
        if (chunkLength > size - cursor) {
            Fail(r, kCodeChunkBounds, "chunk payload exceeds container bounds");
            return r;
        }

        if (!jsonSeen) {
            if (chunkType != kGlbChunkJson) {
                Fail(r, kCodeChunkOrder, "first GLB chunk must be JSON");
                return r;
            }
            r.jsonData = data + cursor;
            r.jsonLength = chunkLength;
            jsonSeen = true;
        } else if (!binSeen) {
            if (chunkType != kGlbChunkBin) {
                Fail(r, kCodeChunkOrder, "second GLB chunk must be BIN");
                return r;
            }
            r.bin.type = chunkType;
            r.bin.length = chunkLength;
            r.bin.data = data + cursor;
            r.hasBin = true;
            binSeen = true;
        } else {
            Fail(r, kCodeExtraChunks, "unknown chunk after JSON/BIN");
            return r;
        }

        cursor += chunkLength;
    }

    if (!jsonSeen) {
        Fail(r, kCodeChunkBounds, "GLB JSON chunk missing");
        return r;
    }

    r.ok = true;
    return r;
}

} // namespace Engine::AssetImport
