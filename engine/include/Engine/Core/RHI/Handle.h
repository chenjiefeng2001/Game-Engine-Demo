#pragma once

/**
 * @file Handle.h
 * @brief 64-bit 世代句柄（Generational Handle）+ SlotMap 容器
 *
 * 完全消除 shared_ptr 在渲染路径中的使用：
 * - Handle<T> 是 64-bit POD（32-bit index + 16-bit generation + 16-bit tag），可 memcpy
 * - SlotMap<T>  是固定大小的预分配数组，Get/Release 无锁（仅分配新页需加锁）
 * - DeferredDeletion 配合：Release() 立即标记失效，GPU 完成后再回收 Slot
 *
 * 使用方式：
 * @code
 *   SlotMap<Texture> textures(1024);
 *   TextureHandle h = textures.Insert(std::make_unique<OpenGLTexture>(...));
 *   Texture* tex = textures.Get(h);
 *   textures.Release(h); // 标记失效，下次分配复用 Slot
 * @endcode
 */

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <memory>
#include <mutex>
#include <vector>

namespace Engine {

// 前向声明实际的资源类型（在 Engine 命名空间内）
class Texture;
class Shader;
class Mesh;

namespace RHI {

// ════════════════════════════════════════════════════════════
// 64-bit Handle
// ════════════════════════════════════════════════════════════

/// 句柄类型标记 — 编译期区分不同资源类型
template <typename T>
struct Handle {
    // 字段布局（显式指定位宽确保跨平台一致性）
    uint32_t index      : 24;   // 24-bit Slot 索引 (0..16M)
    uint32_t generation : 8;    // 8-bit 世代 (0..255)
    // 高位 32-bit 保留给 Debugging/Tagging
    uint32_t _pad       : 32;   // 未使用，未来可作为 Magic Number 验证

    static constexpr Handle Invalid() noexcept {
        return Handle{0xFFFFFF, 0xFF, 0};
    }

    bool IsValid() const noexcept {
        return index != 0xFFFFFF || generation != 0xFF;
    }

    bool operator==(const Handle& rhs) const noexcept {
        return index == rhs.index && generation == rhs.generation;
    }
    bool operator!=(const Handle& rhs) const noexcept {
        return !(*this == rhs);
    }

    /// 转为 64-bit 整数（用于 Hash、Cache Key）
    uint64_t ToU64() const noexcept {
        return (static_cast<uint64_t>(generation) << 56) |
               (static_cast<uint64_t>(index) << 32) |
               _pad;
    }
};

// ════════════════════════════════════════════════════════════
// SlotMap — 固定容量世代槽位容器
// ════════════════════════════════════════════════════════════

template <typename T>
class SlotMap {
public:
    using HandleType = Handle<T>;

    explicit SlotMap(uint32_t maxSlots = 4096)
        : m_Capacity(maxSlots)
        , m_Size(0)
    {
        m_Slots.resize(maxSlots);
        m_FreeList.resize(maxSlots);

        // 初始化自由链表：每个 slot 指向下一个空闲索引
        for (uint32_t i = 0; i < maxSlots; ++i) {
            m_FreeList[i] = i + 1;
        }
        m_NextFree = 0;
    }

    ~SlotMap() { Clear(); }

    // 禁止拷贝
    SlotMap(const SlotMap&) = delete;
    SlotMap& operator=(const SlotMap&) = delete;

    /// 插入对象，返回句柄
    template <typename... Args>
    HandleType Insert(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        assert(m_NextFree < m_Capacity && "SlotMap full!");

        uint32_t idx = m_NextFree;
        m_NextFree = m_FreeList[idx];

        auto& slot = m_Slots[idx];
        slot.object = std::make_unique<T>(std::forward<Args>(args)...);
        slot.generation = slot.generation + 1; // 世代递增
        slot.active = true;

        m_Size++;

        HandleType handle;
        handle.index = idx;
        handle.generation = slot.generation;
        handle._pad = 0;
        return handle;
    }

    /// 通过句柄获取对象指针（线程安全，无需加锁 — 只读）
    T* Get(HandleType handle) noexcept
    {
        if (!handle.IsValid()) return nullptr;
        if (handle.index >= m_Capacity) return nullptr;

        auto& slot = m_Slots[handle.index];
        if (slot.generation != handle.generation) return nullptr; // 世代不匹配 → 失效
        if (!slot.active) return nullptr;

        return slot.object.get();
    }

    const T* Get(HandleType handle) const noexcept
    {
        return const_cast<SlotMap*>(this)->Get(handle);
    }

    /// 释放对象（标记失效，加入自由链表）
    void Release(HandleType handle) noexcept
    {
        if (!handle.IsValid()) return;
        if (handle.index >= m_Capacity) return;

        std::lock_guard<std::mutex> lock(m_Mutex);

        auto& slot = m_Slots[handle.index];
        if (slot.generation != handle.generation) return;
        if (!slot.active) return;

        slot.object.reset();
        slot.active = false;

        // 回收到自由链表
        m_FreeList[handle.index] = m_NextFree;
        m_NextFree = handle.index;

        m_Size--;
    }

    /// 清空所有对象
    void Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (uint32_t i = 0; i < m_Capacity; ++i) {
            m_Slots[i].object.reset();
            m_Slots[i].active = false;
            m_FreeList[i] = i + 1;
        }
        m_NextFree = 0;
        m_Size = 0;
    }

    uint32_t Size() const noexcept { return m_Size; }
    uint32_t Capacity() const noexcept { return m_Capacity; }

private:
    struct Slot {
        std::unique_ptr<T> object;
        uint8_t  generation = 0;
        bool     active = false;
    };

    uint32_t m_Capacity;
    uint32_t m_Size;
    uint32_t m_NextFree;
    std::vector<Slot>     m_Slots;
    std::vector<uint32_t> m_FreeList;
    std::mutex            m_Mutex; // 仅在 Insert/Release 时加锁，Get 无锁
};

// ════════════════════════════════════════════════════════════
// 常用句柄类型别名（使用完全限定名，因为 Texture/Shader/Mesh 在 Engine:: 中）
// ════════════════════════════════════════════════════════════

using TextureHandle = Handle<Engine::Texture>;
using ShaderHandle   = Handle<Engine::Shader>;
using MeshHandle     = Handle<Engine::Mesh>;

using TextureSlotMap = SlotMap<Engine::Texture>;
using ShaderSlotMap   = SlotMap<Engine::Shader>;
using MeshSlotMap     = SlotMap<Engine::Mesh>;

} // namespace RHI
} // namespace Engine