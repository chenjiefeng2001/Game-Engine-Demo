#include "MeshMaterialResolver.h"

#include <cctype>

namespace Engine::AssetImport {

namespace {

bool IsHex8(const std::string& s) {
    if (s.size() != 8) return false;
    for (const char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// "<stemDir>/<base>.<digest8>.mesh" -> stemDir + base (base contains no dots
// by writer sanitize rule; digest is 8 hex).
bool SplitMeshAssetPath(const std::string& meshPath, std::string* stemDir,
                        std::string* base) {
    const std::string suffix = ".mesh";
    if (meshPath.size() <= suffix.size() ||
        meshPath.compare(meshPath.size() - suffix.size(), suffix.size(),
                         suffix) != 0)
        return false;
    const std::string withoutExt =
        meshPath.substr(0, meshPath.size() - suffix.size());
    const size_t lastDot = withoutExt.find_last_of('.');
    if (lastDot == std::string::npos) return false;
    const std::string digest = withoutExt.substr(lastDot + 1);
    if (!IsHex8(digest)) return false;
    const std::string stem = withoutExt.substr(0, lastDot);
    const size_t slash = stem.find_last_of('/');
    if (slash == std::string::npos) {
        *stemDir = std::string();
        *base = stem;
    } else {
        *stemDir = stem.substr(0, slash);
        *base = stem.substr(slash + 1);
    }
    if (base->empty()) return false;
    for (const char c : *base) {
        if (c == '.') return false; // writer sanitize forbids dots
    }
    return true;
}

} // namespace

MaterialSlotResolution ResolveMaterialAssetPaths(
    const std::string& meshAssetPath, uint32_t slotCount,
    const std::vector<std::pair<std::string, std::string>>& registryEntries) {
    MaterialSlotResolution result;
    std::string stemDir, base;
    if (!SplitMeshAssetPath(meshAssetPath, &stemDir, &base)) {
        result.error =
            "MATERIAL_SLOT_INVALID_MESH_PATH: cannot derive stem from '" +
            meshAssetPath + "'";
        return result;
    }
    if (slotCount == 0) {
        result.ok = true;
        return result;
    }
    result.slotPaths.resize(slotCount);
    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        const std::string prefix =
            (stemDir.empty() ? std::string() : stemDir + "/") + base + ".m" +
            std::to_string(slot) + ".";
        std::vector<std::string> matches;
        for (const auto& entry : registryEntries) {
            if (entry.second != "Material") continue;
            if (entry.first.rfind(prefix, 0) != 0) continue;
            const std::string rest = entry.first.substr(prefix.size());
            // rest must be "<hash8>.material.json"
            const std::string tail = ".material.json";
            if (rest.size() != 8 + tail.size()) continue;
            if (rest.compare(8, tail.size(), tail) != 0) continue;
            if (!IsHex8(rest.substr(0, 8))) continue;
            matches.push_back(entry.first);
        }
        if (matches.size() != 1) {
            result.error =
                std::string("MATERIAL_SLOT_") +
                (matches.empty() ? "MISSING" : "AMBIGUOUS") +
                ": slot " + std::to_string(slot) + " for '" + base +
                "' (matches=" + std::to_string(matches.size()) + ")";
            result.slotPaths.clear();
            return result;
        }
        result.slotPaths[slot] = matches[0];
    }
    result.ok = true;
    return result;
}

} // namespace Engine::AssetImport
