#pragma once

/**
 * @file ResourceRegistry.h
 * @brief 资源注册表 — GUID→资源映射、交叉引用、生命周期管理
 *
 * 设计要点：
 *   - 全局统一注册表，所有运行时资源均通过 GUID 注册
 *   - ResourceRef<T>：强引用交叉引用（资源 A 引用资源 B，阻止 B 被卸载）
 *   - ResourceWeakRef<T>：弱引用交叉引用（不影响被引用资源的生命周期）
 *   - 生命周期钩子：OnRegister / OnUnload / OnReload
 *   - 与 ResourcePoolAllocator 配合实现零碎片分配
 *
 * 使用示例：
 * @code
 *   ResourceRegistry registry;
 *
 *   // 注册资源
 *   auto guid = registry.Register(myTexture);
 *
 *   // 交叉引用
 *   ResourceRef<Texture> ref(registry, guid);
 *   ref->GetWidth();  // 透明访问
 *
 *   // 弱引用
 *   ResourceWeakRef<Texture> weakRef(registry, guid);
 *   if (auto tex = weakRef.Lock()) { ... }
 *
 *   // 按 GUID 查找
 *   auto tex = registry.Get<Texture>(guid);
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/Resources/ResourcePoolAllocator.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 前向声明
    // ============================================================
    template<typename T> class ResourceRef;
    template<typename T> class ResourceWeakRef;

    // ============================================================
    // 注册条目
    // ============================================================
    struct RegistryEntry {
        ResourceGUID     guid;
        std::string      path;               // 文件路径或逻辑 ID
        ResourceType     type;
        Resource*        resource = nullptr;  // 裸指针（由外部 shared_ptr 持有生存期）
        uint32           refCount   = 0;      // 显式引用计数（ResourceRef 专用）
        ResourceState    state      = ResourceState::Unloaded;
        uint64           loadTimeMs = 0;      // 加载耗时
        int64            lastUsed   = 0;      // 最后使用时间戳

        // 被哪些 ResourceRef 引用（用于 GC/循环检测）
        std::unordered_set<ResourceGUID> referencedBy;
        // 引用了哪些资源
        std::unordered_set<ResourceGUID> references;
    };

    // ============================================================
    // 生命周期事件
    // ============================================================
    enum class LifecycleEvent : uint8 {
        BeforeLoad  = 0,   // 资源加载前
        AfterLoad   = 1,   // 资源加载后
        BeforeUnload= 2,   // 资源卸载前
        AfterUnload = 3,   // 资源卸载后
        BeforeReload= 4,   // 资源重载前
        AfterReload = 5,   // 资源重载后
    };

    using LifecycleCallback = std::function<void(LifecycleEvent event,
                                                  const ResourceGUID& guid)>;

    // ============================================================
    // 资源注册表
    // ============================================================
    class ResourceRegistry {
        friend class ResourceRefBase;
    public:
        ResourceRegistry() = default;
        ~ResourceRegistry() = default;

        ResourceRegistry(const ResourceRegistry&) = delete;
        ResourceRegistry& operator=(const ResourceRegistry&) = delete;

        // ── 注册 / 注销 ──

        /**
         * @brief 注册一个资源到注册表
         * @param resource 资源 shared_ptr
         * @return 分配的 GUID
         *
         * 如果资源已经有 GUID（通过 SetGUID 设置过的），则复用该 GUID。
         * 否则自动生成新的 GUID。
         */
        ResourceGUID Register(std::shared_ptr<Resource> resource);

        /**
         * @brief 以指定 GUID 注册资源
         * @param guid     预设 GUID（必须非空）
         * @param resource 资源 shared_ptr
         * @return 是否成功（如果 GUID 已存在则失败）
         */
        bool RegisterWithGUID(const ResourceGUID& guid,
                              std::shared_ptr<Resource> resource);

        /**
         * @brief 从注册表注销资源
         * @param guid 资源 GUID
         * @param reason 注销原因描述
         */
        void Unregister(const ResourceGUID& guid,
                        const std::string& reason = "");

        /** 清空注册表 */
        void Clear();

        // ── 查询 ──

        /** 通过 GUID 获取资源 shared_ptr */
        template<typename T = Resource>
        std::shared_ptr<T> Get(const ResourceGUID& guid) const {
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto it = m_Entries.find(guid);
            if (it == m_Entries.end()) return nullptr;

            // 通过裸指针 + 自定义删除器构造 shared_ptr
            // 注意：资源由外部 holder 管理生命周期
            if (it->second.resource) {
                if (auto raw = dynamic_cast<T*>(it->second.resource)) {
                    // 返回别名 shared_ptr（不增加引用计数）
                    if (auto holder = GetHolderLock(guid)) {
                        return std::shared_ptr<T>(holder, raw);
                    }
                }
            }
            return nullptr;
        }

        /** 通过路径查找 GUID */
        ResourceGUID FindByPath(const std::string& path) const;

        /** 通过 GUID 查找注册条目 */
        const RegistryEntry* FindEntry(const ResourceGUID& guid) const;

        /** 检查 GUID 是否已注册 */
        bool Has(const ResourceGUID& guid) const;

        /** 检查路径是否已注册 */
        bool HasPath(const std::string& path) const;

        /** 获取所有已注册 GUID */
        std::vector<ResourceGUID> GetAllGuids() const;

        /** 获取指定类型的所有 GUID */
        std::vector<ResourceGUID> GetGuidsByType(ResourceType type) const;

        // ── 状态管理 ──

        /** 更新资源状态 */
        void SetState(const ResourceGUID& guid, ResourceState state);

        /** 获取资源状态 */
        ResourceState GetState(const ResourceGUID& guid) const;

        /** 更新最后使用时间 */
        void Touch(const ResourceGUID& guid);

        // ── 生命周期钩子 ──

        /** 注册生命周期回调 */
        uint32 BindLifecycleCallback(LifecycleCallback callback);

        /** 解绑生命周期回调 */
        void UnbindLifecycleCallback(uint32 id);

        // ── 交叉引用管理（供 ResourceRef/ResourceWeakRef 使用） ──

        /** 增加显式引用计数 */
        void AddRef(const ResourceGUID& guid, const ResourceGUID& referer);

        /** 减少显式引用计数 */
        void ReleaseRef(const ResourceGUID& guid, const ResourceGUID& referer);

        /** 获取显式引用计数 */
        uint32 GetRefCount(const ResourceGUID& guid) const;

        // ── 内存池 ──

        /** 获取分配器引用 */
        ResourcePoolAllocator& GetAllocator() { return m_Allocator; }
        const ResourcePoolAllocator& GetAllocator() const { return m_Allocator; }

        // ── 统计 ──

        size_t GetRegisteredCount() const;
        size_t GetLoadedCount() const;
        void   LogStats() const;

    private:
        /** 获取资源的持有者 shared_ptr（内部辅助） */
        std::shared_ptr<Resource> GetHolderLock(const ResourceGUID& guid) const;

        /** 触发生命周期事件 */
        void FireLifecycleEvent(LifecycleEvent event, const ResourceGUID& guid);

        // ── 数据 ──
        mutable std::mutex m_Mutex;

        // GUID → 注册条目
        std::unordered_map<ResourceGUID, RegistryEntry> m_Entries;

        // 路径 → GUID（用于路径查找）
        std::unordered_map<std::string, ResourceGUID> m_PathIndex;

        // 资源的持有者（确保资源存活）
        std::unordered_map<ResourceGUID, std::shared_ptr<Resource>> m_Holders;

        // 生命周期回调
        struct LifecycleEntry {
            uint32 id;
            LifecycleCallback callback;
        };
        std::vector<LifecycleEntry> m_LifecycleCallbacks;
        uint32 m_NextCallbackId = 1;

        // 内存池分配器
        ResourcePoolAllocator m_Allocator;
    };

    // ============================================================
    // ResourceRefBase — 交叉引用基类
    // ============================================================
    class ResourceRefBase {
    public:
        ResourceRefBase() = default;

        ResourceRefBase(ResourceRegistry& registry, const ResourceGUID& guid)
            : m_Registry(&registry), m_GUID(guid) {
            if (m_Registry && !m_GUID.IsNull())
                m_Registry->AddRef(m_GUID, ResourceGUID::Null);
        }

        ResourceRefBase(const ResourceRefBase& other)
            : m_Registry(other.m_Registry), m_GUID(other.m_GUID) {
            if (m_Registry && !m_GUID.IsNull())
                m_Registry->AddRef(m_GUID, ResourceGUID::Null);
        }

        ResourceRefBase(ResourceRefBase&& other) noexcept
            : m_Registry(other.m_Registry), m_GUID(other.m_GUID) {
            other.m_GUID = ResourceGUID::Null;
        }

        virtual ~ResourceRefBase() {
            if (m_Registry && !m_GUID.IsNull())
                m_Registry->ReleaseRef(m_GUID, ResourceGUID::Null);
        }

        ResourceRefBase& operator=(const ResourceRefBase& other) {
            if (this != &other) {
                if (m_Registry && !m_GUID.IsNull())
                    m_Registry->ReleaseRef(m_GUID, ResourceGUID::Null);
                m_Registry = other.m_Registry;
                m_GUID = other.m_GUID;
                if (m_Registry && !m_GUID.IsNull())
                    m_Registry->AddRef(m_GUID, ResourceGUID::Null);
            }
            return *this;
        }

        ResourceRefBase& operator=(ResourceRefBase&& other) noexcept {
            if (this != &other) {
                if (m_Registry && !m_GUID.IsNull())
                    m_Registry->ReleaseRef(m_GUID, ResourceGUID::Null);
                m_Registry = other.m_Registry;
                m_GUID = other.m_GUID;
                other.m_GUID = ResourceGUID::Null;
            }
            return *this;
        }

        const ResourceGUID& GetGUID() const { return m_GUID; }
        bool IsValid() const { return m_Registry && !m_GUID.IsNull(); }
        void Reset() {
            if (m_Registry && !m_GUID.IsNull())
                m_Registry->ReleaseRef(m_GUID, ResourceGUID::Null);
            m_GUID = ResourceGUID::Null;
        }

    protected:
        ResourceRegistry* m_Registry = nullptr;
        ResourceGUID      m_GUID;
    };

    // ============================================================
    // ResourceRef<T> — 强引用交叉引用
    // ============================================================
    template<typename T = Resource>
    class ResourceRef : public ResourceRefBase {
    public:
        ResourceRef() = default;

        ResourceRef(ResourceRegistry& registry, const ResourceGUID& guid)
            : ResourceRefBase(registry, guid) {}

        ResourceRef(const ResourceRef&) = default;
        ResourceRef(ResourceRef&&) = default;

        ResourceRef& operator=(const ResourceRef&) = default;
        ResourceRef& operator=(ResourceRef&&) = default;

        /** 访问引用的资源 */
        T* operator->() const {
            if (!m_Registry) return nullptr;
            return m_Registry->Get<T>(m_GUID).get();
        }

        /** 获取引用的资源 shared_ptr */
        std::shared_ptr<T> Lock() const {
            if (!m_Registry) return nullptr;
            return m_Registry->Get<T>(m_GUID);
        }

        /** 隐式转换为 bool */
        explicit operator bool() const {
            if (!m_Registry || m_GUID.IsNull()) return false;
            auto res = m_Registry->Get<T>(m_GUID);
            return res && res->IsReady();
        }
    };

    // ============================================================
    // ResourceWeakRef<T> — 弱引用交叉引用
    // ============================================================
    template<typename T = Resource>
    class ResourceWeakRef {
    public:
        ResourceWeakRef() = default;

        ResourceWeakRef(ResourceRegistry& registry, const ResourceGUID& guid)
            : m_Registry(&registry), m_GUID(guid) {}

        ResourceWeakRef(const ResourceWeakRef&) = default;
        ResourceWeakRef(ResourceWeakRef&&) = default;

        ResourceWeakRef& operator=(const ResourceWeakRef&) = default;
        ResourceWeakRef& operator=(ResourceWeakRef&&) = default;

        /** 尝试锁定资源（返回 shared_ptr，可能为空） */
        std::shared_ptr<T> Lock() const {
            if (!m_Registry) return nullptr;
            return m_Registry->Get<T>(m_GUID);
        }

        /** 检查资源是否仍存活 */
        bool IsAlive() const {
            if (!m_Registry || m_GUID.IsNull()) return false;
            return m_Registry->Has(m_GUID);
        }

        const ResourceGUID& GetGUID() const { return m_GUID; }
        void Reset() { m_GUID = ResourceGUID::Null; }

        explicit operator bool() const { return IsAlive(); }

    private:
        ResourceRegistry* m_Registry = nullptr;
        ResourceGUID      m_GUID;
    };

} // namespace Engine
