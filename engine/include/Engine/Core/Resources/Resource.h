#pragma once

/**
 * @file Resource.h
 * @brief 资源基类 — 所有可管理的资源类型共同基类
 *
 * 提供统一的资源标识符（路径/ID）、引用计数、类型信息。
 * 配合 ResourceManager 实现统一生命周期管理。
 *
 * 设计原则：
 *   - 路径即标识：每个资源通过唯一的文件路径或逻辑 ID 标识
 *   - shared_ptr/weak_ptr 生命周期：引用计数自动管理，无需手动 AddRef/Release
 *   - 类型安全：通过 ResourceType 枚举和 dynamic_pointer_cast 实现运行时类型查询
 *
 * 使用示例：
 * @code
 *   // 所有资源类型均通过 ResourceManager 获取
 *   auto tex = ResourceManager::Get()->Load<Texture>("assets/textures/player.png");
 *   auto snd = ResourceManager::Get()->Load<AudioClip>("assets/audio/explosion.wav");
 *
 *   // 弱引用观察（用于缓存自动清理）
 *   std::weak_ptr<Resource> weakRef = tex;
 *   if (auto res = weakRef.lock()) {
 *       // 资源仍存活
 *   }
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>

namespace Engine {

    // 前向声明
    class IGraphicsFactory;

    // ============================================================
    // 资源类型枚举
    // ============================================================
    enum class ResourceType : uint8 {
        Unknown     = 0,
        Texture     = 1,
        Shader      = 2,
        AudioClip   = 3,
        Font        = 4,
        Mesh        = 5,
        Material    = 6,
        COUNT
    };

    inline const char* ResourceTypeName(ResourceType type) {
        switch (type) {
            case ResourceType::Texture:   return "Texture";
            case ResourceType::Shader:    return "Shader";
            case ResourceType::AudioClip: return "AudioClip";
            case ResourceType::Font:      return "Font";
            case ResourceType::Mesh:      return "Mesh";
            case ResourceType::Material:  return "Material";
            default:                      return "Unknown";
        }
    }

    // ============================================================
    // 资源加载状态
    // ============================================================
    enum class ResourceState : uint8 {
        Unloaded  = 0,  // 尚未加载
        Loading   = 1,  // 正在异步加载中
        Loaded    = 2,  // 自身数据已加载（元数据就绪）
        Resolving = 3,  // 正在解析依赖
        Ready     = 4,  // 自身数据 + 所有依赖已就绪，完全可用
        Failed    = 5   // 加载失败
    };

    inline const char* ResourceStateName(ResourceState state) {
        switch (state) {
            case ResourceState::Unloaded:  return "Unloaded";
            case ResourceState::Loading:   return "Loading";
            case ResourceState::Loaded:    return "Loaded";
            case ResourceState::Resolving: return "Resolving";
            case ResourceState::Ready:     return "Ready";
            case ResourceState::Failed:    return "Failed";
            default:                       return "???";
        }
    }

    // ============================================================
    // 资源基类
    // ============================================================
    class Resource : public std::enable_shared_from_this<Resource> {
    public:
        Resource() = delete;
        Resource(std::string path, ResourceType type)
            : m_Path(std::move(path)), m_Type(type) {}

        virtual ~Resource() = default;

        // 禁止拷贝
        Resource(const Resource&) = delete;
        Resource& operator=(const Resource&) = delete;

        // 允许移动（派生类如 AudioClip 需要移动语义）
        Resource(Resource&& other) noexcept
            : m_Path(std::move(other.m_Path))
            , m_Type(other.m_Type)
            , m_GUID(other.m_GUID)
            , m_State(other.m_State.load(std::memory_order_acquire))
            , m_ReloadVersion(other.m_ReloadVersion.load(std::memory_order_acquire)) {}
        Resource& operator=(Resource&& other) noexcept {
            if (this != &other) {
                m_Path = std::move(other.m_Path);
                m_Type = other.m_Type;
                m_GUID = other.m_GUID;
                m_State.store(other.m_State.load(std::memory_order_acquire),
                              std::memory_order_release);
                m_ReloadVersion.store(other.m_ReloadVersion.load(std::memory_order_acquire),
                                      std::memory_order_release);
            }
            return *this;
        }

        // ── 标识 ──
        /** 获取资源唯一路径/ID */
        const std::string& GetPath()  const noexcept { return m_Path; }
        /** 获取资源类型 */
        ResourceType       GetType()  const noexcept { return m_Type; }
        /** 获取资源类型名称字符串 */
        const char*        GetTypeName() const noexcept { return ResourceTypeName(m_Type); }

        // ── GUID 支持 ──
        /** 获取资源的全局唯一标识符 */
        const ResourceGUID& GetGUID() const noexcept { return m_GUID; }
        /** 设置资源的 GUID（应在注册时由 ResourceRegistry 调用） */
        void SetGUID(const ResourceGUID& guid) noexcept { m_GUID = guid; }
        /** 是否已分配 GUID */
        bool HasGUID() const noexcept { return !m_GUID.IsNull(); }

        // ── 状态 ──
        /** 获取当前加载状态 */
        ResourceState GetState() const noexcept { return m_State.load(std::memory_order_acquire); }
        /** 设置加载状态（线程安全） */
        void SetState(ResourceState state) { m_State.store(state, std::memory_order_release); }

        /** 是否已成功加载（仅自身数据就绪，依赖可能尚未解析） */
        bool IsLoaded() const noexcept { return GetState() == ResourceState::Loaded; }
        /** 是否完全就绪（自身数据 + 所有依赖已解析） */
        bool IsReady() const noexcept { return GetState() == ResourceState::Ready; }
        /** 是否加载失败 */
        bool IsFailed() const noexcept { return GetState() == ResourceState::Failed; }
        /** 是否正在加载或解析依赖 */
        bool IsLoading() const noexcept {
            auto s = GetState();
            return s == ResourceState::Loading || s == ResourceState::Resolving;
        }

        // ── 引用计数诊断 ──
        /** 当前 shared_ptr 引用计数（含 ResourceManager 内部弱引用锁定的临时计数） */
        long RefCount() const noexcept {
            return shared_from_this().use_count();
        }
        /** 是否有外部持有者（引用计数 > 1 说明除 cache 外还有使用者） */
        bool IsReferenced() const noexcept { return RefCount() > 1; }
        // ── 依赖解析 ──

        /**
         * @brief 返回此资源依赖的其他资源路径列表
         * @return 依赖路径列表
         *
         * 由 ResourceManager::Load<T>() 在加载完成后自动调用。
         * 派生类（如 Material）应重写此方法返回其引用的子资源路径。
         * 默认返回空列表（无依赖）。
         */
        virtual std::vector<std::string> GetDependencies() const { return {}; }

        /**
         * @brief 递归解析所有依赖
         * @param rm  ResourceManager 引用，用于加载依赖资源
         * @return true 表示所有依赖已就绪
         *
         * 由 ResourceManager::Load<T>() 在自身数据加载完成后自动调用。
         * 默认实现直接返回 true（无依赖）。
         * 派生类可重写此方法以实现自定义加载逻辑，
         * 通常遍历 GetDependencies() 并调用 rm.Load<T>(depPath)。
         */
        virtual bool ResolveDependencies(class ResourceManager& rm) {
            (void)rm;
            return true;
        }

        // ── 加载后初始化 ──

        /**
         * @brief 资源加载后的初始化钩子
         * @param factory 可选的图形工厂指针（为 nullptr 时表示纯 CPU 资源）
         * @return true 表示初始化成功
         *
         * 由 ResourceManager::Load<T>() 在依赖解析完成后自动调用。
         * 此时所有依赖已就绪，可安全执行 GPU 上传等需要完整依赖的初始化。
         * 默认实现返回 true（无操作）。
         */
        virtual bool PostLoad(IGraphicsFactory* factory) {
            (void)factory;
            return true;
        }

        // ── 资源大小估算（供内存统计使用，子类可重写） ──
        virtual size_t EstimatedMemoryBytes() const noexcept { return 0; }

        // ── 热加载 ──
        /**
         * @brief 从磁盘重新加载此资源
         * @return 是否成功
         *
         * 子类必须重写此方法以释放旧的 GPU/API 对象并重新创建。
         * 默认实现返回 false（不支持热加载的资源类型）。
         * 此操作不应在后台线程调用（OpenGL 等需要主线程 API 上下文）。
         */
        virtual bool Reload() {
            // 默认不支持热加载
            return false;
        }

        /** 获取资源版本号（每次 Reload 后递增） */
        uint32 GetReloadVersion() const noexcept {
            return m_ReloadVersion.load(std::memory_order_acquire);
        }
        /** 递增版本号（由 ResourceManager 和子类在 Reload 成功后调用） */
        void BumpReloadVersion() {
            m_ReloadVersion.fetch_add(1, std::memory_order_release);
        }

    protected:
        /** 更新资源路径（仅派生类在加载时使用，如 LoadFromFile） */
        void SetPath(const std::string& path) { m_Path = path; }

    private:
        std::string                m_Path;
        ResourceType               m_Type;
        ResourceGUID               m_GUID;     // 全局唯一标识符
        std::atomic<ResourceState> m_State{ ResourceState::Unloaded };
        std::atomic<uint32>        m_ReloadVersion{ 0 };
    };

    // ============================================================
    // 便捷类型别名
    // ============================================================
    template<typename T>
    using ResourceHandle = std::shared_ptr<T>;

    template<typename T>
    using ResourceWeakHandle = std::weak_ptr<T>;

    // 前向声明，避免循环依赖
    class Texture;
    class Shader;
    class AudioClip;
    class Font;

    using TextureHandle   = ResourceHandle<Texture>;
    using ShaderHandle    = ResourceHandle<Shader>;
    using AudioClipHandle = ResourceHandle<AudioClip>;
    using FontHandle      = ResourceHandle<Font>;

} // namespace Engine
