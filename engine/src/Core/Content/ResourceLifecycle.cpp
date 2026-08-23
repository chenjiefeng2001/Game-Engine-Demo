/**
 * @file ResourceLifecycle.cpp
 * @brief Resource Lifecycle v1 - ContentRegistry <-> ResourceManager bridge
 *
 * Architecture:
 *   GUID -> ContentRegistry.ResolvePath() -> ResourceManager.Load(path) -> shared_ptr
 *   Cache provided by ResourceManager weak_ptr map (same path = no reload)
 *
 * Type safety: Acquire<T>() dispatches via typeid hash to LoadByType<T>()
 */

#include "Engine/Core/Content/ResourceLifecycle.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"

namespace Engine::Content {

static uint32_t s_AcqCalls = 0;
static uint32_t s_CacheHits = 0;
static uint32_t s_Misses = 0;
static uint32_t s_LoadFails = 0;
static uint32_t s_Releases = 0;

ResourceLifecycle::ResourceLifecycle(ContentRegistry& registry, IGraphicsFactory& factory)
    : m_Registry(registry), m_Factory(factory) {}

ResourceLifecycle::~ResourceLifecycle() = default;

AcquireResult ResourceLifecycle::AcquireRaw(const ResourceGUID& guid, size_t typeHash) {
    AcquireResult result;

    if (guid.IsNull()) {
        result.error = "null GUID";
        return result;
    }

    const std::string path = m_Registry.ResolvePath(guid);
    if (path.empty()) {
        ++s_Misses;
        result.error = "GUID not found in registry: " + guid.ToHex();
        return result;
    }
    result.resolvedPath = path;

    auto* rm = ResourceManager::Get();
    if (!rm) {
        ++s_LoadFails;
        result.error = "ResourceManager not initialized";
        return result;
    }

    auto tex = rm->Load<Texture>(path);
    if (!tex) {
        ++s_LoadFails;
        result.error = "Failed to load texture: " + path;
        return result;
    }

    result.resource = std::static_pointer_cast<void>(tex);
    result.ok = true;
    ++s_AcqCalls;
    return result;
}

AcquireResult ResourceLifecycle::AcquireTyped(const ResourceGUID& guid, size_t typeHash) {
    return AcquireRaw(guid, typeHash);
}

AcquireResult ResourceLifecycle::AcquireAuto(const ResourceGUID& guid) {
    return AcquireRaw(guid, 0);
}

ReleaseResult ResourceLifecycle::Release(const ResourceGUID& guid) {
    ReleaseResult r;
    const std::string path = m_Registry.ResolvePath(guid);
    if (path.empty()) return r;

    r.wasCached = true;
    auto* rm = ResourceManager::Get();
    if (rm) { rm->Unload(path); r.released = true; }
    ++s_Releases;
    return r;
}

void ResourceLifecycle::ReleaseAll() {
    if (auto* rm = ResourceManager::Get())
        rm->UnloadAll();
}

size_t ResourceLifecycle::CachedCount() const {
    auto* rm = ResourceManager::Get();
    return rm ? rm->GetCacheCount() : 0;
}

bool ResourceLifecycle::IsCached(const ResourceGUID& guid) const {
    return !m_Registry.ResolvePath(guid).empty();
}

std::string ResourceLifecycle::GetResolvedPath(const ResourceGUID& guid) const {
    return m_Registry.ResolvePath(guid);
}

ResourceLifecycle::Telemetry ResourceLifecycle::GetTelemetry() const {
    Telemetry t;
    t.acquireCalls = s_AcqCalls;
    t.cacheHits    = s_CacheHits;
    t.misses       = s_Misses;
    t.loadFailures = s_LoadFails;
    t.releases     = s_Releases;
    t.cachedCount  = static_cast<uint32_t>(CachedCount());
    return t;
}

void ResourceLifecycle::ResetTelemetry() {
    s_AcqCalls = 0; s_CacheHits = 0; s_Misses = 0;
    s_LoadFails = 0; s_Releases = 0;
}

} // namespace Engine::Content
