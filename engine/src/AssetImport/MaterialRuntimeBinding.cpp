#include "MaterialRuntimeBinding.h"

#include "ImportDiagnostic.h"
#include "MeshMaterialResolver.h"
#include "TextureAssetReader.h"

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/Resources/Resource.h"

#include <unordered_set>

namespace Engine::AssetImport {

namespace {

MeshMaterialBindingResult Fail(
    const char* code, const std::string& detail,
    MaterialBindingFailureStage stage = MaterialBindingFailureStage::None) {
    MeshMaterialBindingResult r;
    r.ok = false;
    r.error = std::string(code) + ": " + detail;
    r.stage = stage;
    return r;
}

bool RegistryHasTexture(
    const std::vector<std::pair<std::string, std::string>>& entries,
    const std::string& path) {
    for (const auto& e : entries) {
        if (e.second == "Texture" && e.first == path) return true;
    }
    return false;
}

uint64_t HashString(uint64_t h, const std::string& s) {
    return TextureGpuCache::HashBytes(s.data(), s.size());
}

} // namespace

MeshMaterialBindingResult BuildMeshMaterialBinding(
    const std::string& meshAssetPath,
    const std::vector<std::string>& materialNames,
    const std::vector<std::pair<std::string, std::string>>& registryEntries,
    const MaterialAssetIo& io, const MaterialBindingBuildOptions& options) {
    if (materialNames.empty()) {
        auto empty = std::make_shared<MeshMaterialBinding>();
        empty->slotCount = 0;
        MeshMaterialBindingResult r;
        r.ok = true;
        r.binding = empty;
        return r;
    }
    if (!io.readBytes)
        return Fail(kCodeMaterialBindingInvalid, "no AssetIo",
                    MaterialBindingFailureStage::InvalidInput);

    const MaterialSlotResolution resolved = ResolveMaterialAssetPaths(
        meshAssetPath, static_cast<uint32_t>(materialNames.size()),
        registryEntries);
    if (!resolved.ok)
        return Fail(kCodeMaterialBindingInvalid, resolved.error,
                    MaterialBindingFailureStage::MaterialAsset);

    auto binding = std::make_shared<MeshMaterialBinding>();
    binding->slotCount = static_cast<uint32_t>(materialNames.size());
    binding->slots.resize(binding->slotCount);
    if (binding->slotCount > 1) {
        binding->warnings.push_back(
            "multi-material source: v1 renders the first material; all " +
            std::to_string(binding->slotCount) + " material assets retained");
    }

    auto resolveTexture = [&](uint32_t slot, const char* roleToken,
                              const MaterialTextureRef& ref,
                              MaterialBindingTexture* out,
                              std::vector<std::string>* warnings,
                              bool fatal,
                              MaterialBindingFailureStage* stage) -> bool {
        out->present = ref.present;
        if (!ref.present) return true;
        auto fail = [&](const std::string& why) {
            if (fatal) {
                binding->warnings.push_back(why);
            } else {
                warnings->push_back("slot " + std::to_string(slot) + ": " +
                                    why);
            }
            return !fatal;
        };
        if (ref.assetPath.empty()) {
            *stage = MaterialBindingFailureStage::TextureAsset;
            return fail("texture reference has no assetPath");
        }
        if (!RegistryHasTexture(registryEntries, ref.assetPath)) {
            *stage = MaterialBindingFailureStage::TextureAsset;
            return fail("texture asset not in registry: " + ref.assetPath);
        }
        std::vector<uint8_t> png;
        if (!io.readBytes(ref.assetPath, png)) {
            *stage = MaterialBindingFailureStage::TextureAsset;
            return fail("texture bytes unreadable: " + ref.assetPath);
        }
        std::vector<uint8_t> sidecar;
        if (!io.readBytes(ref.assetPath + ".tex.meta.json", sidecar))
            sidecar.clear(); // legacy compatibility path
        const TextureAssetData decoded = ReadTextureAsset(png, sidecar);
        if (!decoded.ok) {
            *stage = MaterialBindingFailureStage::TextureAsset;
            return fail(decoded.error);
        }
        out->assetPath = ref.assetPath;
        out->srgb = decoded.colorspace == "sRGB";
        if (options.textureCache && options.factory) {
            const std::string abs = io.toAbsolutePath
                                        ? io.toAbsolutePath(ref.assetPath)
                                        : std::string();
            if (abs.empty()) {
                *stage = MaterialBindingFailureStage::TextureAsset;
                return fail("cannot resolve texture OS path");
            }
            TextureGpuResourceKey key;
            key.identity = TextureGpuCache::HashBytes(ref.assetPath.data(),
                                                      ref.assetPath.size());
            key.contentDigest = TextureGpuCache::HashCombine(
                TextureGpuCache::HashBytes(png.data(), png.size()),
                TextureGpuCache::HashBytes(sidecar.data(), sidecar.size()));
            key.uploadSemantics = TextureGpuCache::HashCombine(
                HashString(0, roleToken),
                TextureGpuCache::HashCombine(
                    HashString(0, decoded.colorspace),
                    static_cast<uint64_t>(decoded.ruleVersion)));
            const auto gpu = options.textureCache->Acquire(
                key, abs, decoded.width, decoded.height, out->srgb,
                *options.factory);
            if (!gpu) {
                *stage = MaterialBindingFailureStage::GpuUpload;
                return fail("texture GPU upload failed: " + ref.assetPath);
            }
            out->gpu = gpu;
        }
        out->resolved = true;
        return true;
    };

    MaterialBindingFailureStage failureStage =
        MaterialBindingFailureStage::None;
    for (uint32_t slot = 0; slot < binding->slotCount; ++slot) {
        MaterialBindingEntry& entry = binding->slots[slot];
        entry.slot = slot;
        entry.materialAssetPath = resolved.slotPaths[slot];
        std::vector<uint8_t> materialBytes;
        if (!io.readBytes(entry.materialAssetPath, materialBytes)) {
            if (slot == 0)
                return Fail(kCodeMaterialBindingMissing,
                            "material asset unreadable: " +
                                entry.materialAssetPath);
            binding->warnings.push_back(
                "slot " + std::to_string(slot) +
                " material unreadable (not rendered in v1): " +
                entry.materialAssetPath);
            continue;
        }
        const MaterialAssetReadResult mr =
            ReadMaterialAssetJson(materialBytes);
        if (!mr.ok) {
            if (slot == 0)
                return Fail(kCodeMaterialBindingInvalid, mr.error);
            binding->warnings.push_back("slot " + std::to_string(slot) +
                                        " material invalid: " + mr.error);
            continue;
        }
        if (mr.material.slot != slot) {
            if (slot == 0)
                return Fail(kCodeMaterialBindingInvalid,
                            "material slot field mismatch at slot 0");
            binding->warnings.push_back("slot " + std::to_string(slot) +
                                        " material slot field mismatch");
            continue;
        }
        if (slot < materialNames.size() && mr.material.name != materialNames[slot]) {
            binding->warnings.push_back(
                "slot " + std::to_string(slot) + " material name '" +
                mr.material.name + "' differs from mesh materialNames '" +
                materialNames[slot] + "' (path-based mapping retained)");
        }
        entry.material = mr.material;
        const bool fatal = slot == 0;
        bool ok = true;
        ok &= resolveTexture(slot, "basecolor", mr.material.baseColor,
                             &entry.baseColor, &binding->warnings, fatal,
                             &failureStage);
        ok &= resolveTexture(slot, "normal", mr.material.normal,
                             &entry.normal, &binding->warnings, fatal,
                             &failureStage);
        ok &= resolveTexture(slot, "occlusion", mr.material.occlusion,
                             &entry.occlusion, &binding->warnings, fatal,
                             &failureStage);
        ok &= resolveTexture(slot, "emissive", mr.material.emissive,
                             &entry.emissive, &binding->warnings, fatal,
                             &failureStage);
        ok &= resolveTexture(slot, "metallicroughness",
                             mr.material.metallicRoughness,
                             &entry.metallicRoughness, &binding->warnings,
                             fatal, &failureStage);
        if (!ok) {
            const char* stageName =
                failureStage == MaterialBindingFailureStage::TextureAsset
                    ? "texture-asset"
                    : failureStage == MaterialBindingFailureStage::GpuUpload
                          ? "gpu-upload"
                          : "material-asset";
            std::string detail = "first material texture binding failed (" +
                                 std::string(stageName) + ")";
            if (!binding->warnings.empty())
                detail += ": " + binding->warnings.back();
            return Fail(kCodeMaterialBindingInvalid, detail, failureStage);
        }
        if (slot == 0) binding->firstMaterialAvailable = true;
    }

    MeshMaterialBindingResult result;
    result.ok = true;
    result.binding = binding;
    return result;
}

} // namespace Engine::AssetImport
