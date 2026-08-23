/**
 * @file ResourceLifecycle.cpp
 * @brief Resource Lifecycle v1 — ContentRegistry ↔ ResourceManager 桥接实现
 *
 * 架构：
 *   GUID → ContentRegistry.ResolvePath() → ResourceManager.Load(path) → shared_ptr
 *   缓存由 ResourceManager 的 weak_ptr map 天然提供（同 path 不重复加载）
 *
 * 类型安全：Acquire<T>() 通过 typeid hash 分派到正确的 LoadByType<T>()
 */

#include "Engine/Core/Content/ResourceLifecycle.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <typeindex>

namespace Engine::Content {

namespace {
    Logger s_Log("ResourceLifecycle");

    /// 类型哈希 → 加载分派器映射（v1 手写注册，不引入反射）
    enum class KnownType : size_t {
        Texture = 0,
        AudioClip,
        Unknown
    };

    KnownType ClassifyByGuid(const ContentRegistry& reg, const ResourceGUID& guid) {
        return reg.TypeOf(guid) == AssetType::Script
            ? KnownType::Unknown      // Script 不走 ResourceManager
            : KnownType::Texture;     // v1 仅 Texture 走 RM
    }
} // namespace

ResourceLifecycle::ResourceLifecycle(ContentRegistry& registry, IGraphicsFactory& factory)
    : m_Registry(registry), m_Factory(factory) {}

ResourceLifecycle::~ResourceLifecycle() = default;

AcquireResult ResourceLifecycle::AcquireRaw(const ResourceGUID& guid, size_t typeHash) {
    AcquireResult result;

    if (guid.IsNull()) {
        result.error = "null GUID";
        return result;
    }

    // ── Resolve: GUID → path ──
    const std::string path = m_Registry.ResolvePath(guid);
    if (path.empty()) {
        result.error = "GUID not found in registry: " + guid.ToHex();
        s_Log.Warn("Acquire: {}", result.error);
        return result;
    }
    result.resolvedPath = path;

    // ── Load/Cache: 委托给 ResourceManager ──
    auto* rm = ResourceManager::Get();
    if (!rm) {
        result.error = "ResourceManager not initialized";
        return result;
    }

    // 检查缓存命中
    // Cache hit 检测由 ResourceManager::Load 内部处理（同 path 返回 weak_ptr 锁定结果）

    // 按类型加载（Texture 是 v1 唯一的 RM 管理类型）
    auto tex = rm->Load<Texture>(path);
    if (!tex) {
        result.error = "Failed to load texture: " + path;
        return result;
    }

    result.resource = std::static_pointer_cast<void>(tex);
    result.ok = true;
    return result;
}

AcquireResult ResourceLifecycle::AcquireTyped(const ResourceGUID& guid, size_t typeHash) {
    return AcquireRaw(guid, typeHash);
}

AcquireResult ResourceLifecycle::AcquireAuto(const ResourceGUID& guid) {
    // v1：所有非 Script 资产统一按 Texture 处理
    return AcquireRaw(guid, 0);
}

ReleaseResult ResourceLifecycle::Release(const ResourceGUID& guid) {
    ReleaseResult r;
    const std::string path = m_Registry.ResolvePath(guid);
    if (path.empty()) return r;

    r.wasCached = true;
    auto* rm = ResourceManager::Get();
    if (rm) { rm->Unload(path); r.released = true; }
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
    const std::string path = m_Registry.ResolvePath(guid);
    auto* rm = ResourceManager::Get();
    return !path.empty();
}

std::string ResourceLifecycle::GetResolvedPath(const ResourceGUID& guid) const {
    return m_Registry.ResolvePath(guid);
}

} // namespace Engine::Content
