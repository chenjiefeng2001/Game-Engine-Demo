#include "TexturePreprocess.h"

#include "ImportLimits.h"
#include "ImportDiagnostic.h"

#include <stb_image.h>

#include <cstring>

namespace Engine::AssetImport {

namespace {

void AddError(TexturePreprocessResult& r, const char* code,
              const std::string& message) {
    r.ok = false;
    r.error = std::string(code) + ": " + message;
}

// ── minimal PNG encoder (uncompressed zlib stored blocks) ───────────────
const std::array<uint32_t, 256>& CrcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    return table;
}

uint32_t Crc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    const auto& table = CrcTable();
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void AppendU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24 & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 16 & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8 & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void AppendPngChunk(std::vector<uint8_t>& out, const char* type,
                    const std::vector<uint8_t>& payload) {
    AppendU32BE(out, static_cast<uint32_t>(payload.size()));
    std::vector<uint8_t> crcInput;
    for (int i = 0; i < 4; ++i)
        crcInput.push_back(static_cast<uint8_t>(type[i]));
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    AppendU32BE(out, Crc32(&crcInput[0], crcInput.size()));
}

uint32_t Adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (const uint8_t v : data) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void AppendStoredZlib(std::vector<uint8_t>& out,
                      const std::vector<uint8_t>& raw) {
    out.push_back(0x78);
    out.push_back(0x01); // FLG=1: (0x78<<8 | 0x01) % 31 == 0
    if (raw.empty()) {
        out.push_back(0x01);
        out.push_back(0x00); out.push_back(0x00);
        out.push_back(0xFF); out.push_back(0xFF);
    }
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t chunk =
            (raw.size() - pos > 65535u) ? 65535u : (raw.size() - pos);
        const bool finalBlock = (pos + chunk) >= raw.size();
        out.push_back(finalBlock ? 0x01 : 0x00);
        const uint16_t len = static_cast<uint16_t>(chunk);
        const uint16_t nlen = static_cast<uint16_t>(~len & 0xFFFFu);
        out.push_back(static_cast<uint8_t>(len & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        for (size_t i = 0; i < chunk; ++i) out.push_back(raw[pos + i]);
        pos += chunk;
    }
    AppendU32BE(out, Adler32(raw));
}

bool EncodePng(uint32_t width, uint32_t height, uint32_t channels,
               const uint8_t* rows, std::vector<uint8_t>* out) {
    if (width == 0 || height == 0 || (channels != 3 && channels != 4) ||
        rows == nullptr)
        return false;
    const uint8_t colorType = channels == 4 ? 6 : 2;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out->insert(out->end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    AppendU32BE(ihdr, width);
    AppendU32BE(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(colorType);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendPngChunk(*out, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) *
                (1ull + static_cast<size_t>(width) * channels));
    const size_t rowBytes = static_cast<size_t>(width) * channels;
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rows + y * rowBytes, rows + (y + 1) * rowBytes);
    }
    std::vector<uint8_t> idat;
    AppendStoredZlib(idat, raw);
    AppendPngChunk(*out, "IDAT", idat);
    AppendPngChunk(*out, "IEND", {});
    return true;
}

// Scoped determinism guard: stb's flip flag is process-global; the GL
// runtime sets it to true before its loads. Preprocess needs a known
// orientation for deterministic row output; the guard temporarily sets the
// flag and RESTORES it at scope exit (net-zero global change). Recorded
// in the B4 record; does not touch B1's parse-side UV flip semantics.
struct ScopedStbFlipGuard
{
    ScopedStbFlipGuard() { stbi_set_flip_vertically_on_load(0); }
    ~ScopedStbFlipGuard() { stbi_set_flip_vertically_on_load(1); }
};

} // namespace

TexturePreprocessResult PreprocessTexture(const std::vector<uint8_t>& source,
                                          TextureRole role) {
    TexturePreprocessResult r;
    r.colorspace =
        IsColorRole(role) ? TextureColorspace::sRGB : TextureColorspace::Linear;

    if (source.empty()) {
        AddError(r, kCodeTextureDecodeFail, "zero-byte source image");
        return r;
    }

    ScopedStbFlipGuard flipGuard;

    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(source.data(),
                               static_cast<int>(source.size()), &w, &h,
                               &comp)) {
        AddError(r, kCodeTextureDecodeFail, "unrecognized or corrupt image");
        return r;
    }
    if (w <= 0 || h <= 0) {
        AddError(r, kCodeTextureInvalidDims, "non-positive dimensions");
        return r;
    }
    if (w > static_cast<int>(kMaxTextureDim) ||
        h > static_cast<int>(kMaxTextureDim)) {
        AddError(r, kCodeTextureDimsExceedLimit, "exceeds kMaxTextureDim");
        return r;
    }
    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    if (pixels > kMaxTexturePixels) {
        AddError(r, kCodeTexturePixelsExceedLimit, "exceeds kMaxTexturePixels");
        return r;
    }
    const uint64_t wanted = static_cast<uint64_t>(IsColorRole(role) ? 4 : 3);
    if (pixels > kMaxTexturePixels || wanted * pixels > 0xFFFFFFFFull) {
        AddError(r, kCodeTexturePixelsExceedLimit, "byte budget overflow");
        return r;
    }

    int iw = 0, ih = 0, icomp = 0;
    const int desired = IsColorRole(role) ? 4 : 3;
    unsigned char* data = stbi_load_from_memory(
        source.data(), static_cast<int>(source.size()), &iw, &ih, &icomp,
        desired);
    if (!data) {
        AddError(r, kCodeTextureDecodeFail, "decode failed after info ok");
        return r;
    }
    r.width = static_cast<uint32_t>(iw);
    r.height = static_cast<uint32_t>(ih);
    r.components = static_cast<uint32_t>(desired);
    r.hasAlpha = IsColorRole(role) && icomp == 4;
    const bool encoded =
        EncodePng(r.width, r.height, r.components, data, &r.pngBytes);
    stbi_image_free(data);
    if (!encoded) {
        AddError(r, kCodeTextureDecodeFail, "PNG encode failure");
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace Engine::AssetImport
