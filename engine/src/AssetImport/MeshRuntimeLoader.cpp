#include "MeshRuntimeLoader.h"

#include "MeshAssetReader.h"
#include "MeshUploadAdapter.h"

#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/TextureGpuResource.h"
#include "Engine/Core/Renderer/Mesh.h"

namespace Engine {

namespace {

MeshRuntimeStatus MapResolveStatus(Content::MeshResolveStatus status) {
    switch (status) {
    case Content::MeshResolveStatus::Ok:
        return MeshRuntimeStatus::Ready;
    case Content::MeshResolveStatus::NotCommitted:
        return MeshRuntimeStatus::NotCommitted;
    case Content::MeshResolveStatus::CommittedManifestInvalid:
        return MeshRuntimeStatus::MissingAsset;
    case Content::MeshResolveStatus::InvalidGuid:
        return MeshRuntimeStatus::InvalidAsset;
    case Content::MeshResolveStatus::MissingAsset:
        return MeshRuntimeStatus::MissingAsset;
    case Content::MeshResolveStatus::UnsupportedAssetType:
        return MeshRuntimeStatus::UnsupportedAssetType;
    case Content::MeshResolveStatus::PathRejected:
        return MeshRuntimeStatus::InvalidAsset;
    case Content::MeshResolveStatus::AssetBytesUnreadable:
        return MeshRuntimeStatus::MissingAsset;
    }
    return MeshRuntimeStatus::InvalidAsset;
}

MeshRuntimeLoadResult Fail(MeshRuntimeLoadResult result,
                           MeshRuntimeStatus status, std::string error) {
    result.ok = false;
    result.status = status;
    result.error = std::move(error);
    result.mesh.reset();
    result.binding.reset();
    return result;
}

} // namespace

MeshRuntimeLoadResult LoadMeshRuntimeAsset(
    const ResourceGUID& guid, Engine::Content::MeshAssetResolver& resolver,
    MeshRenderer& renderer, TextureGpuCache& textureCache,
    IGraphicsFactory& factory) {
    MeshRuntimeLoadResult result;
    result.guid = guid;

    const Content::MeshAssetResolveResult resolved = resolver.Resolve(guid);
    if (!resolved.ok) {
        return Fail(std::move(result), MapResolveStatus(resolved.status),
                    resolved.error);
    }
    result.revision = resolved.revision;
    result.assetPath = resolved.assetPath;
    result.absolutePath = resolved.absolutePath;

    const AssetImport::MeshAssetReadResult read =
        AssetImport::ReadMeshAssetJson(resolved.bytes);
    if (!read.ok) {
        return Fail(std::move(result), MeshRuntimeStatus::ReaderFailure,
                    read.error);
    }
    const AssetImport::MeshUploadInputResult upload =
        AssetImport::BuildMeshUploadInput(read.mesh);
    if (!upload.ok) {
        return Fail(std::move(result), MeshRuntimeStatus::InvalidAsset,
                    upload.error);
    }

    AssetImport::MaterialAssetIo io;
    io.readBytes = [&resolver](const std::string& path,
                               std::vector<uint8_t>& out) {
        return resolver.ReadProjectFile(path, out);
    };
    io.toAbsolutePath = [&resolver](const std::string& path) {
        return resolver.AbsoluteProjectPath(path);
    };
    AssetImport::MaterialBindingBuildOptions bindingOptions;
    bindingOptions.textureCache = &textureCache;
    bindingOptions.factory = &factory;
    const AssetImport::MeshMaterialBindingResult binding =
        AssetImport::BuildMeshMaterialBinding(
            resolved.assetPath, read.mesh.materialNames,
            resolver.CommittedEntries(), io, bindingOptions);
    if (!binding.ok) {
        const bool textureFailure =
            binding.stage == AssetImport::MaterialBindingFailureStage::TextureAsset ||
            binding.stage == AssetImport::MaterialBindingFailureStage::GpuUpload;
        return Fail(std::move(result),
                    textureFailure ? MeshRuntimeStatus::TextureBindingFailure
                                   : MeshRuntimeStatus::MaterialBindingFailure,
                    binding.error);
    }

    auto cpuMesh = std::make_shared<Mesh>();
    cpuMesh->SetVertices(upload.input.vertices);
    cpuMesh->SetIndices(upload.input.indices);
    std::vector<MeshSpan> cpuSpans;
    cpuSpans.reserve(upload.input.spans.size());
    for (const MeshGpuSpan& span : upload.input.spans)
        cpuSpans.push_back({span.firstIndex, span.indexCount,
                            span.materialIndex});
    cpuMesh->SetSpans(cpuSpans);
    cpuMesh->SetVertexFlags(upload.input.vertexFlags);
    const std::shared_ptr<const MeshGpuResource> gpu =
        renderer.EnsureGpuMesh(cpuMesh, MeshComponent::FoldGuid(guid));
    if (!gpu) {
        return Fail(std::move(result), MeshRuntimeStatus::GPUUploadFailure,
                    "GPU_UPLOAD_FAILURE: " + resolved.assetPath);
    }
    if (resolver.CurrentCommittedRevision() != resolved.revision) {
        return Fail(std::move(result), MeshRuntimeStatus::NotCommitted,
                    "NOT_COMMITTED: revision changed during load");
    }

    result.ok = true;
    result.status = MeshRuntimeStatus::Ready;
    result.mesh = std::move(cpuMesh);
    result.key = gpu->Key();
    result.binding = binding.binding;
    result.spanCount = static_cast<uint32_t>(upload.input.spans.size());
    result.materialSlotCount = binding.binding->slotCount;
    result.warnings = binding.binding->warnings;
    return result;
}

} // namespace Engine
