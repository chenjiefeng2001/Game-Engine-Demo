#include "TextureAssetReader.h"

#include "ImportDiagnostic.h"
#include "ImportLimits.h"

#include <stb_image.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <set>

namespace Engine::AssetImport {

namespace {

using Json = nlohmann::json;

TextureAssetData Fail(const char* code, const std::string& detail) {
    TextureAssetData d;
    d.ok = false;
    d.error = std::string(code) + ": " + detail;
    return d;
}

bool ColorRoleForToken(const std::string& role, bool* isColor) {
    if (role == "basecolor" || role == "emissive") {
        *isColor = true;
        return true;
    }
    if (role == "normal" || role == "occlusion" ||
        role == "metallicroughness") {
        *isColor = false;
        return true;
    }
    return false;
}

} // namespace

TextureAssetData ReadTextureAsset(const std::vector<uint8_t>& encodedPng,
                                  const std::vector<uint8_t>& sidecarBytes) {
    if (encodedPng.empty())
        return Fail(kCodeTextureAssetParse, "empty encoded bytes");
    if (encodedPng.size() > kMaxSourceFileBytes)
        return Fail(kCodeTextureAssetInvalid, "encoded bytes exceed cap");

    TextureAssetData out;
    bool isColor = false;
    if (sidecarBytes.empty()) {
        out.legacyNoSidecar = true;
        out.role = "legacy";
        out.colorspace = "linear"; // B4 frozen legacy rule
        out.ruleVersion = 0;
    } else {
        Json j;
        try {
            j = Json::parse(sidecarBytes.begin(), sidecarBytes.end());
        } catch (const std::exception& e) {
            return Fail(kCodeTextureAssetParse,
                        std::string("sidecar JSON failed: ") + e.what());
        }
        if (!j.is_object())
            return Fail(kCodeTextureAssetParse, "sidecar root not object");
        static const std::set<std::string> keys = {
            "format", "ruleVersion", "role", "colorspace", "width",
            "height", "components", "sourceIdentity"};
        for (const auto& item : j.items()) {
            if (keys.find(item.key()) == keys.end())
                return Fail(kCodeTextureAssetParse,
                            "unknown sidecar field '" + item.key() + "'");
        }
        if (!j.contains("format") || !j["format"].is_string() ||
            j["format"].get<std::string>() != "texture-meta")
            return Fail(kCodeTextureAssetParse, "sidecar format invalid");
        if (!j.contains("ruleVersion") || !j["ruleVersion"].is_number_unsigned())
            return Fail(kCodeTextureAssetUnsupportedVersion,
                        "sidecar ruleVersion invalid");
        out.ruleVersion = j["ruleVersion"].get<uint32_t>();
        if (out.ruleVersion != 1)
            return Fail(kCodeTextureAssetUnsupportedVersion,
                        "unsupported texture preprocess ruleVersion " +
                            std::to_string(out.ruleVersion));
        if (!j.contains("role") || !j["role"].is_string())
            return Fail(kCodeTextureAssetInvalid, "sidecar role invalid");
        out.role = j["role"].get<std::string>();
        if (!ColorRoleForToken(out.role, &isColor))
            return Fail(kCodeTextureAssetInvalid,
                        "unknown sidecar role '" + out.role + "'");
        if (!j.contains("colorspace") || !j["colorspace"].is_string())
            return Fail(kCodeTextureAssetInvalid, "sidecar colorspace invalid");
        out.colorspace = j["colorspace"].get<std::string>();
        if (out.colorspace != "sRGB" && out.colorspace != "linear")
            return Fail(kCodeTextureAssetInvalid,
                        "unknown colorspace '" + out.colorspace + "'");
        if (isColor != (out.colorspace == "sRGB"))
            return Fail(kCodeTextureAssetInvalid,
                        "sidecar role/colorspace conflict");
        if (!j.contains("width") || !j.contains("height") ||
            !j.contains("components"))
            return Fail(kCodeTextureAssetInvalid,
                        "sidecar dimensions/components missing");
    }

    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(encodedPng.data(),
                               static_cast<int>(encodedPng.size()), &w, &h,
                               &comp))
        return Fail(kCodeTextureAssetParse, "unrecognized or corrupt PNG");
    if (w <= 0 || h <= 0)
        return Fail(kCodeTextureAssetInvalid, "non-positive dimensions");
    if (w > static_cast<int>(kMaxTextureDim) ||
        h > static_cast<int>(kMaxTextureDim))
        return Fail(kCodeTextureAssetInvalid, "dimensions exceed limit");
    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    if (pixels > kMaxTexturePixels)
        return Fail(kCodeTextureAssetInvalid, "pixels exceed limit");
    if (pixels > (0xFFFFFFFFull / 4ull))
        return Fail(kCodeTextureAssetInvalid, "RGBA byte budget overflow");
    if (comp != 3 && comp != 4)
        return Fail(kCodeTextureAssetInvalid,
                    "unsupported encoded channel layout");

    if (!out.legacyNoSidecar) {
        // Re-parse sidecar dimensions now that actuals are known.
        Json j = Json::parse(sidecarBytes.begin(), sidecarBytes.end());
        const int sw = j["width"].get<int>();
        const int sh = j["height"].get<int>();
        const int sc = j["components"].get<int>();
        if (sw != w || sh != h)
            return Fail(kCodeTextureAssetInvalid,
                        "sidecar dimensions mismatch encoded image");
        if (sc != comp)
            return Fail(kCodeTextureAssetInvalid,
                        "sidecar components mismatch encoded image");
    }

    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    out.sourceComponents = static_cast<uint32_t>(comp);
    out.hasAlpha = comp == 4;

    // Same decoder convention as the runtime OpenGLTexture path.
    stbi_set_flip_vertically_on_load(1);
    int dw = 0, dh = 0, dc = 0;
    unsigned char* data = stbi_load_from_memory(
        encodedPng.data(), static_cast<int>(encodedPng.size()), &dw, &dh, &dc,
        4);
    if (!data)
        return Fail(kCodeTextureAssetParse, "decode failed after info ok");
    if (dw != w || dh != h) {
        stbi_image_free(data);
        return Fail(kCodeTextureAssetInvalid, "decoded layout mismatch");
    }
    out.rgba8.assign(
        data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    stbi_image_free(data);
    out.ok = true;
    return out;
}

} // namespace Engine::AssetImport
