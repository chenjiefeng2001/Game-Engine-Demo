#include "ImportDigest.h"

#include "Engine/Core/Content/RevisionStore.h"

#include <cstring>

namespace Engine::AssetImport {

namespace {

const char* ClassToken(TextureDigestClass c) {
    return c == TextureDigestClass::Color ? "color" : "data";
}

} // namespace

std::string ComputeImportDigest(const ImportDigestInput& input) {
    std::string skeleton;
    if (input.schema == DigestSchema::V1) {
        skeleton = "gd-import-digest-v1;";
    } else {
        skeleton = "gd-import-digest-v2;";
    }
    skeleton += input.sourcePathKey;
    skeleton += ":";
    skeleton += Engine::Content::RevisionStore::HashBytes(
        std::string_view(reinterpret_cast<const char*>(input.sourceBytes),
                         input.sourceSize));
    for (const DigestDependency& dep : input.dependencies) {
        skeleton += "|dep:";
        skeleton += dep.pathKey;
        skeleton += ":";
        skeleton += Engine::Content::RevisionStore::HashBytes(
            std::string_view(reinterpret_cast<const char*>(dep.bytes),
                             dep.size));
    }
    if (input.schema == DigestSchema::V2) {
        for (const DigestTexture& tex : input.textures) {
            skeleton += "|tex:";
            skeleton += ClassToken(tex.digestClass);
            skeleton += ":";
            skeleton += tex.identity;
            skeleton += ":";
            skeleton += Engine::Content::RevisionStore::HashBytes(
                std::string_view(reinterpret_cast<const char*>(tex.bytes),
                                 tex.size));
        }
        // Preprocessing rule/version participates in the digest so a rule
        // change can trigger revisions (B4 contract).
        skeleton += "|rule:texture-preprocess-v1:rgba8-png";
    }
    return Engine::Content::RevisionStore::HashBytes(skeleton);
}

} // namespace Engine::AssetImport
