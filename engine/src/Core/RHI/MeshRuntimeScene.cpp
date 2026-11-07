#include "Engine/Core/RHI/MeshRuntimeScene.h"

#include "AssetImport/MeshRuntimeLoader.h"

#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/TextureGpuResource.h"
#include "Engine/Core/Scene/Scene.h"

#include <iterator>
#include <set>
#include <unordered_map>
#include <vector>

namespace Engine {

namespace {

const std::string KeyOf(const ResourceGUID& guid) { return guid.ToHex(); }

void CollectMeshComponents(
    GameObject& object,
    std::unordered_map<std::string, std::vector<MeshComponent*>>& out,
    std::vector<MeshComponent*>& unbound) {
    if (auto* mc = object.GetComponent<MeshComponent>()) {
        if (mc->HasMeshGuid()) out[KeyOf(mc->GetMeshGuid())].push_back(mc);
        else unbound.push_back(mc);
    }
    for (const auto& child : object.GetChildren()) {
        if (child) CollectMeshComponents(*child, out, unbound);
    }
}

} // namespace

void MeshRuntimeScene::SetDependencies(Content::MeshAssetResolver* resolver,
                                       MeshRenderer* renderer,
                                       TextureGpuCache* textureCache,
                                       IGraphicsFactory* factory) {
    m_Resolver = resolver;
    m_Renderer = renderer;
    m_TextureCache = textureCache;
    m_Factory = factory;
}

bool MeshRuntimeScene::HasDependencies() const {
    return m_Resolver && m_Renderer && m_TextureCache && m_Factory;
}

MeshRuntimeScene::SyncStats MeshRuntimeScene::Sync(Scene& scene) {
    SyncStats stats;
    if (!HasDependencies()) return stats;

    std::unordered_map<std::string, std::vector<MeshComponent*>> components;
    std::vector<MeshComponent*> unbound;
    for (const auto& object : scene.GetObjects()) {
        if (object) CollectMeshComponents(*object, components, unbound);
    }
    for (MeshComponent* mc : unbound) mc->SetRuntimeUnbound();
    for (const auto& [key, list] : components)
        stats.components += static_cast<uint32_t>(list.size());

    std::set<std::string> referenced;
    const int64_t currentRevision = m_Resolver->CurrentCommittedRevision();

    for (const auto& [key, list] : components) {
        referenced.insert(key);
        const ResourceGUID guid = list.front()->GetMeshGuid();

        auto existing = m_Entries.find(key);
        if (existing != m_Entries.end() &&
            existing->second.revision == currentRevision) {
            for (MeshComponent* mc : list) {
                mc->SetRuntimeReady(guid, existing->second.revision,
                                    existing->second.assetPath,
                                    existing->second.mesh);
            }
            m_Failures.erase(key);
            ++stats.reused;
            continue;
        }

        const auto failed = m_Failures.find(key);
        if (failed != m_Failures.end() &&
            failed->second.revision == currentRevision) {
            for (MeshComponent* mc : list) {
                mc->SetRuntimeFailure(guid, failed->second.status,
                                      failed->second.error, currentRevision);
            }
            ++stats.failed;
            continue;
        }

        MeshRuntimeLoadResult loaded = LoadMeshRuntimeAsset(
            guid, *m_Resolver, *m_Renderer, *m_TextureCache, *m_Factory);
        if (!loaded.ok) {
            m_Failures[key] = Failure{loaded.status, loaded.error,
                                      currentRevision};
            for (MeshComponent* mc : list) {
                mc->SetRuntimeFailure(guid, loaded.status, loaded.error,
                                      currentRevision);
            }
            ++stats.failed;
            continue;
        }

        if (existing != m_Entries.end() &&
            !(existing->second.key == loaded.key)) {
            (void)m_Renderer->RemoveMeshGpuResource(existing->second.key);
        }
        Entry entry;
        entry.guid = guid;
        entry.revision = loaded.revision;
        entry.assetPath = loaded.assetPath;
        entry.mesh = loaded.mesh;
        entry.key = loaded.key;
        entry.binding = loaded.binding;
        m_Renderer->SetMeshMaterialBinding(entry.key, entry.binding);
        m_Entries[key] = entry;
        m_Failures.erase(key);
        for (MeshComponent* mc : list) {
            mc->SetRuntimeReady(guid, entry.revision, entry.assetPath,
                                entry.mesh);
        }
        ++stats.loaded;
    }

    for (auto it = m_Entries.begin(); it != m_Entries.end();) {
        if (referenced.count(it->first) == 0) {
            (void)m_Renderer->RemoveMeshGpuResource(it->second.key);
            it = m_Entries.erase(it);
            ++stats.removed;
        } else {
            ++it;
        }
    }
    for (auto it = m_Failures.begin(); it != m_Failures.end();) {
        it = referenced.count(it->first) == 0 ? m_Failures.erase(it)
                                               : std::next(it);
    }
    return stats;
}

void MeshRuntimeScene::Unload() {
    if (m_Renderer) {
        for (const auto& [key, entry] : m_Entries)
            (void)m_Renderer->RemoveMeshGpuResource(entry.key);
    }
    m_Entries.clear();
    m_Failures.clear();
}

const MeshRuntimeScene::Entry* MeshRuntimeScene::FindForTesting(
    const ResourceGUID& guid) const {
    const auto it = m_Entries.find(KeyOf(guid));
    return it == m_Entries.end() ? nullptr : &it->second;
}

const MeshRuntimeScene::Failure* MeshRuntimeScene::FindFailureForTesting(
    const ResourceGUID& guid) const {
    const auto it = m_Failures.find(KeyOf(guid));
    return it == m_Failures.end() ? nullptr : &it->second;
}

} // namespace Engine
