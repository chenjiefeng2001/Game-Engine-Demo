#pragma once

/**
 * @file LockFreeEventQueue.h
 * @brief MPSC (Multi-Producer Single-Consumer) 无锁事件队列
 *
 * 用于 Jolt Physics 碰撞回调 — 多个 Worker 线程同时 Push，主线程 Pop 处理。
 *
 * 设计要点（v3.1）：
 *   - MPSC 而非 SPSC：Jolt 的 OnContactBegin 由多个 Worker 线程同时触发
 *   - 无锁（lock-free）：基于原子 CAS 操作，绝不阻塞 Worker 线程
 *   - 固定容量环形缓冲区：无动态分配，适合实时系统
 *   - Cache Line 隔离：避免 False Sharing
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <atomic>
#include <cstddef>

namespace Engine {

// ════════════════════════════════════════════════════════
// 碰撞事件类型
// ════════════════════════════════════════════════════════
struct CollisionEvent {
    enum class Type : uint8 {
        Begin,      // 碰撞开始
        End,        // 碰撞结束
        Persist     // 碰撞持续中（含冲量信息）
    };

    Type type = Type::Begin;

    // 两个碰撞体的 EntityHandle ID（传输 uint64 避免暴露 EntityHandle 头文件）
    uint64 entityAID = 0;
    uint64 entityBID = 0;

    // 碰撞信息
    Vec3  contactPoint  = {0, 0, 0};
    Vec3  contactNormal = {0, 0, 0};
    float penetration   = 0.0f;
    float totalImpulse  = 0.0f;   // Persist 事件专用
};

// ════════════════════════════════════════════════════════
// MPSC 无锁队列
// ════════════════════════════════════════════════════════
class LockFreeEventQueue {
public:
    explicit LockFreeEventQueue(size_t capacity = 256);

    ~LockFreeEventQueue();

    LockFreeEventQueue(const LockFreeEventQueue&) = delete;
    LockFreeEventQueue& operator=(const LockFreeEventQueue&) = delete;

    // ── 生产者（Worker 线程调用，多线程安全） ──

    /**
     * @brief 入队一个碰撞事件
     * @param event 事件数据
     * @return true=成功, false=队列已满（降级：丢弃该帧事件）
     *
     * 使用原子 CAS 操作，无锁。
     * 队列满时直接丢弃事件以避免阻塞工作线程。
     */
    bool Push(const CollisionEvent& event);

    // ── 消费者（主线程调用，非线程安全） ──

    /**
     * @brief 出队一个碰撞事件
     * @param outEvent 输出参数
     * @return true=成功获取, false=队列空
     */
    bool Pop(CollisionEvent& outEvent);

    /** 当前队列中待处理的事件数量 */
    size_t Size() const;

    /** 队列是否为空 */
    bool IsEmpty() const;

    /** 清空队列（主线程调用） */
    void Clear();

    /** 获取总容量 */
    size_t Capacity() const { return m_Capacity; }

private:
    // 获取下一个幂等容量
    static size_t NextPow2(size_t n);

    // ── 数据 ──
    const size_t m_Capacity;
    const size_t m_Mask;        // capacity - 1（用于位掩码索引）

    // Cache Line 隔离：生产者和消费者的原子变量不在同一个 Cache Line
    // 生产者端（多个 Worker 线程共享）
    alignas(64) std::atomic<size_t> m_Head{0};

    // 消费者端（主线程专用）
    alignas(64) std::atomic<size_t> m_Tail{0};

    // 缓冲区
    CollisionEvent* m_Buffer;
};

} // namespace Engine