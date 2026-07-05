/**
 * @file LockFreeEventQueue.cpp
 * @brief MPSC 无锁事件队列实现
 */

#include "Engine/Core/Physics/LockFreeEventQueue.h"
#ifdef _WIN32
#include <malloc.h>   // _aligned_malloc / _aligned_free
#else
#include <cstdlib>    // std::aligned_alloc (C11)
#endif
#include <cstring>
#include <cassert>

namespace Engine {

LockFreeEventQueue::LockFreeEventQueue(size_t capacity)
    : m_Capacity(NextPow2(capacity))
    , m_Mask(m_Capacity - 1)
{
#ifdef _WIN32
    m_Buffer = static_cast<CollisionEvent*>(
        _aligned_malloc(m_Capacity * sizeof(CollisionEvent), alignof(CollisionEvent))
    );
#else
    m_Buffer = static_cast<CollisionEvent*>(
        std::aligned_alloc(alignof(CollisionEvent), m_Capacity * sizeof(CollisionEvent))
    );
#endif
    std::memset(m_Buffer, 0, m_Capacity * sizeof(CollisionEvent));
}

LockFreeEventQueue::~LockFreeEventQueue() {
#ifdef _WIN32
    _aligned_free(m_Buffer);
#else
    std::free(m_Buffer);
#endif
}


// ── MPSC Push: 多生产者使用原子 CAS ──
bool LockFreeEventQueue::Push(const CollisionEvent& event) {
    size_t head = m_Head.load(std::memory_order_relaxed);
    size_t tail = m_Tail.load(std::memory_order_acquire);

    // 队列满检查
    if (head - tail >= m_Capacity) {
        return false;  // 队列满，丢弃事件
    }

    // 使用 CAS 原子推进 Head（多个生产者竞争）
    size_t nextHead = head + 1;
    while (!m_Head.compare_exchange_weak(
        head, nextHead,
        std::memory_order_acq_rel,
        std::memory_order_relaxed
    )) {
        nextHead = head + 1;
        // 再次检查是否满
        if (head - m_Tail.load(std::memory_order_acquire) >= m_Capacity) {
            return false;
        }
    }

    // 写入数据（此时 head 已被我们独占）
    m_Buffer[head & m_Mask] = event;

    // 确保数据写入对其他线程可见后再推进 Tail（仅消费者需要看到）
    // 对于 MPSC，我们不需要在 Push 中更新 Tail
    // Tail 由消费者(Single Consumer)在 Pop 时更新

    return true;
}

// ── Pop: 单消费者 ──
bool LockFreeEventQueue::Pop(CollisionEvent& outEvent) {
    size_t tail = m_Tail.load(std::memory_order_relaxed);
    size_t head = m_Head.load(std::memory_order_acquire);

    if (tail == head) {
        return false;  // 队列空
    }

    // 读取数据
    outEvent = m_Buffer[tail & m_Mask];

    // 推进 Tail（消费者独占，无需 CAS）
    m_Tail.store(tail + 1, std::memory_order_release);

    return true;
}

size_t LockFreeEventQueue::Size() const {
    size_t head = m_Head.load(std::memory_order_acquire);
    size_t tail = m_Tail.load(std::memory_order_acquire);
    return head - tail;
}

bool LockFreeEventQueue::IsEmpty() const {
    return m_Head.load(std::memory_order_acquire) ==
           m_Tail.load(std::memory_order_acquire);
}

void LockFreeEventQueue::Clear() {
    // 消费者独占
    m_Tail.store(m_Head.load(std::memory_order_acquire), std::memory_order_release);
}

size_t LockFreeEventQueue::NextPow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

} // namespace Engine