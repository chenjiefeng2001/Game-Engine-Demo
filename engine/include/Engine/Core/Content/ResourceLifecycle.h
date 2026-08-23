#pragma once

/**
 * @file ResourceLifecycle.h
 * @brief Resource Lifecycle v1 — ContentRegistry ↔ ResourceManager 统一桥接
 *        （Milestone 2 · Ring15: Resource Golden Gate）
 *
 * 职责表（审计结论）：
 *   ContentRegistry    → Asset Identity / GUID / Manifest     🔒 冻结
 *   ResourceManager    → Runtime Load / Cache / Lifetime       ✅ 保留（已被音频使用）
 *   ResourceGUID       → 128-bit primitive                    🔒 冻结
 *   AssetDatabase      → Editor metadata                       ⏸ 延后（编辑器专用）
 *   ResourceRegistry   → Lifecycle tracking                   ❌ 无生产消费者
 *   AssetMetaDb        → Import settings                      ❌ 零消费者
 *   AssetPipeline      → Rule-based processing                ❌ 零消费者
 *
 * v1 契约：
 *   Acquire(guid)  → Resolve GUID→path → ResourceManager.Load(path) → shared_ptr
 *   Release(guid)  → 卸载缓存条目
 *   Missing(guid)  → 返回 nullptr + 明确错误信息（不崩溃）
 *   Cache hit      → 同一 GUID 二次 Acquire 返回同一 shared_ptr（零重加载）
 *
 * 不做（v1 红线）：
 *   异步加载 / 引用计数回调 / 自动卸载 / 资源依赖图 / 加密 / 打包
 */

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/Resource.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <string>
#include <memory>

namespace Engine::Content {

    /// 资源获取结果
    struct AcquireResult {
        std::shared_ptr<void> resource;   ///< 类型擦除的运行时资源
        std::string           resolvedPath;
        bool                  fromCache = false;
        bool                  ok = false;
        std::string           error;       ///< 非 ok 时非空
    };

    /// 释放结果
    struct ReleaseResult {
        bool wasCached = false;
        bool released  = false;
    };

    class ResourceLifecycle {
    public:
        explicit ResourceLifecycle(ContentRegistry& registry,
                                   class IGraphicsFactory& factory);
        ~ResourceLifecycle();

        // ── 类型化 Acquire（编译期安全） ──

        template<typename T>
        std::shared_ptr<T> Acquire(const ResourceGUID& guid, AcquireResult* out = nullptr);

        template<typename T>
        std::shared_ptr<T> Acquire(const ResourceGUID& guid) {
            return std::static_pointer_cast<T>(AcquireRaw(guid, typeid(T).hash_code()));
        }

        // ── 类型擦除 Acquire（跨层/测试用）──
        AcquireResult AcquireTyped(const ResourceGUID& guid, size_t typeHash);

        /// 通用入口：根据注册表中的 AssetType 自动分派
        AcquireResult AcquireAuto(const ResourceGUID& guid);

        // ── 释放 ──
        ReleaseResult Release(const ResourceGUID& guid);
        void          ReleaseAll();
        size_t        CachedCount() const;

        // ── 查询 ──
        bool IsCached(const ResourceGUID& guid) const;
        std::string GetResolvedPath(const ResourceGUID& guid) const;

        // ── D2-5 Telemetry（Console 可查询）──
        struct Telemetry {
            uint32_t acquireCalls = 0;
            uint32_t cacheHits    = 0;
            uint32_t misses       = 0;   ///< GUID not found in registry
            uint32_t loadFailures = 0;   ///< path resolved but load failed
            uint32_t releases     = 0;
            uint32_t cachedCount  = 0;
        };
        Telemetry GetTelemetry() const;
        void ResetTelemetry();

    private:
        AcquireResult AcquireRaw(const ResourceGUID& guid, size_t typeHash);
        ContentRegistry& m_Registry;
        IGraphicsFactory& m_Factory;
    };

} // namespace Engine::Content
