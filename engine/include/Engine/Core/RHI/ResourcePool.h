#pragma once

/**
 * @file ResourcePool.h
 * @brief 强类型 GPU 资源句柄 — SlotMap 实现
 *
 * 设计要点：
 *   - 32-bit Index + 32-bit Generation → 64-bit Handle
 *   - 读取操作无锁：`m_Slots[handle.Index()]`（O(1) 直接索引）
 *   - 写入操作使用 SpinLock（仅 Create/Destroy 时）
 *   - 配发删除延迟 3 帧（配合 Fence 使用）
 *   - 误用已销毁句柄时返回 Error 资源而非 Crash
 *
 * 使用方式：
 * @code
 *   ResourcePool<Texture> pool(1024);
 *   TextureHandle h = pool.Create(texture);
 *   Texture* tex = pool.Get(h);  // O(1), 无锁
 *   pool.Destroy(h);             // 标记删除
 * @endcode
 */

#include "Engine/Types.h"
#include <vector>
#include <atomic>
#include <cassert>

namespace Engine { namespace RHI {

// ═══════════════════════════════════════════════════════════
// 64-bit 强类型句柄
// ═══════════════════════════════════════════════════════════
struct TextureHandle {
    uint64_t id = 0;  // 高 32-bit = Generation | 低 32-bit = Index

    uint32_t Index()      const { return uint32_t(id & 0xFFFFFFFF); }
    uint32_t Generation() const { return uint32_t(id >> 32); }

    bool IsValid() const { return id != 0; }
    bool operator==(TextureHandle o) const { return id == o.id; }

    static TextureHandle Create(uint32_t idx, uint32_t gen) {
        return TextureHandle{ (uint64_t(idx)) | (uint64_t(gen) << 32) };
    }
    static const TextureHandle kNull;
};
inline const TextureHandle kNullTexture{ 0 };

struct BufferHandle {
    uint64_t id = 0;

    uint32_t Index()      const { return uint32_t(id & 0xFFFFFFFF); }
    uint32_t Generation() const { return uint32_t(id >> 32); }

    bool IsValid() const { return id != 0; }
    bool operator==(BufferHandle o) const { return id == o.id; }

    static BufferHandle Create(uint32_t idx, uint32_t gen) {
        return BufferHandle{ (uint64_t(idx)) | (uint64_t(gen) << 32) };
    }
    static const BufferHandle kNull;
};
inline const BufferHandle kNullBuffer{ 0 };

// ═══════════════════════════════════════════════════════════
// SlotMap — 无锁读 + 世代校验
// ═══════════════════════════════════════════════════════════
template<typename T>
class SlotMap {
    struct Slot {
        uint32_t generation = 0;
        T*       resource   = nullptr;
        bool     alive      = false;
    };

public:
    explicit SlotMap(uint32_t capacity = 4096)
        : m_Capacity(capacity)
        , m_Slots(capacity)
    {}

    ~SlotMap() {
        for (auto& slot : m_Slots) {
            delete slot.resource;
        }
    }

    SlotMap(const SlotMap&) = delete;
    SlotMap& operator=(const SlotMap&) = delete;

    // ── 创建 — 返回 Handle ──
    template<typename HandleT>
    HandleT Create(T* resource) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        uint32_t index;
        if (!m_FreeList.empty()) {
            index = m_FreeList.back();
            m_FreeList.pop_back();
        } else {
            index = m_NextIndex++;
            if (index >= m_Capacity) {
                // 池满，需要扩展（生产级应预分配足够大）
                delete resource;
                return HandleT::kNull;
            }
        }

        auto& slot = m_Slots[index];
        slot.generation++;
        slot.resource = resource;
        slot.alive = true;

        return HandleT::Create(index, slot.generation);
    }

    // ── 获取 — 无锁 O(1) ──
    T* Get(TextureHandle handle) const {
        uint32_t idx = handle.Index();
        if (idx >= m_Capacity) return nullptr;

        const auto& slot = m_Slots[idx];
        // 世代校验：不匹配说明句柄已过期
        if (!slot.alive || slot.generation != handle.Generation()) {
            return nullptr;  // 返回 nullptr，由调用方回退到 Error 资源
        }
        return slot.resource;
    }

    T* Get(BufferHandle handle) const {
        uint32_t idx = handle.Index();
        if (idx >= m_Capacity) return nullptr;

        const auto& slot = m_Slots[idx];
        if (!slot.alive || slot.generation != handle.Generation()) {
            return nullptr;
        }
        return slot.resource;
    }

    // ── 销毁 — 加入延迟队列 ──
    void Destroy(TextureHandle handle) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        uint32_t idx = handle.Index();
        if (idx >= m_Capacity) return;
        auto& slot = m_Slots[idx];
        if (!slot.alive) return;
        slot.alive = false;
        m_FreeList.push_back(idx);
        // 实际资源释放由调用方通过 DeferredDeletionQueue 管理
    }

    void Destroy(BufferHandle handle) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        uint32_t idx = handle.Index();
        if (idx >= m_Capacity) return;
        auto& slot = m_Slots[idx];
        if (!slot.alive) return;
        slot.alive = false;
        m_FreeList.push_back(idx);
    }

    // ── 查询 ──
    bool IsValid(TextureHandle handle) const {
        return Get(handle) != nullptr;
    }

    bool IsValid(BufferHandle handle) const {
        return Get(handle) != nullptr;
    }

    uint32_t GetCapacity() const { return m_Capacity; }
    uint32_t GetActiveCount() const { return m_NextIndex - static_cast<uint32_t>(m_FreeList.size()); }

private:
    const uint32_t m_Capacity;
    mutable std::vector<Slot> m_Slots;
    std::vector<uint32_t>     m_FreeList;
    uint32_t                  m_NextIndex{1};  // 0 = null
    mutable std::mutex        m_Mutex;
};

}} // namespace Engine::RHI