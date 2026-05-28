#pragma once

/**
 * @file StackAllocator.h
 * @brief 栈分配器（线性分配器）— 所有子系统内存的分配基础
 *
 * 设计思想：
 *   - 预分配一大块连续内存，按顺序分配，永不归还单个对象
 *   - 通过 marker 实现 LIFO 批量回退，或一次性 Reset()
 *   - O(1) 分配速度，零碎片，极佳缓存局部性
 *   - 适用于「启动时分配，关闭时整块回收」的子系统场景
 */

#include "Engine/Types.h"
#include <cstddef>
#include <new>

namespace Engine {

    class StackAllocator {
    public:
        /**
         * @brief 构造栈分配器并预分配容量
         * @param capacity  分配器总容量（字节）
         */
        explicit StackAllocator(size_t capacity);

        ~StackAllocator();

        // 禁止拷贝移动
        StackAllocator(const StackAllocator&) = delete;
        StackAllocator& operator=(const StackAllocator&) = delete;
        StackAllocator(StackAllocator&&) = delete;
        StackAllocator& operator=(StackAllocator&&) = delete;

        /**
         * @brief 分配一块对齐的内存
         * @param size       请求字节数
         * @param alignment  对齐要求（必须为 2 的幂，默认 max_align_t）
         * @return 指向对齐后内存块的指针，失败返回 nullptr
         */
        void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));

        /**
         * @brief 回退到之前的 marker 位置（LIFO）
         * @param marker  由 GetMarker() 返回的位置标记
         */
        void FreeTo(size_t marker);

        /** @brief 获取当前分配位置标记 */
        size_t GetMarker() const noexcept { return m_Offset; }

        /** @brief 重置整个分配器（等价于释放所有内存） */
        void Reset() noexcept { m_Offset = 0; }

        /** @brief 已使用的字节数 */
        size_t Used() const noexcept { return m_Offset; }

        /** @brief 总容量（字节） */
        size_t Capacity() const noexcept { return m_Capacity; }

        /** @brief 剩余可用字节数 */
        size_t Remaining() const noexcept { return m_Capacity - m_Offset; }

    private:
        std::byte*  m_Memory;     // 预分配的内存块
        size_t      m_Capacity;   // 总容量
        size_t      m_Offset;     // 当前分配偏移（即已使用的字节数）
    };

    // ============================================================
    // 类型安全的辅助函数：在 StackAllocator 上构造对象
    // ============================================================

    /**
     * @brief 在栈分配器上构造单个对象
     * @tparam T      对象类型
     * @tparam Args   构造函数参数类型
     * @param alloc   栈分配器
     * @param args    构造函数参数
     * @return T*     指向新构造对象的指针
     */
    template<typename T, typename... Args>
    T* NewOnStack(StackAllocator& alloc, Args&&... args) {
        void* mem = alloc.Allocate(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * @brief 在栈分配器上构造数组
     */
    template<typename T>
    T* NewArrayOnStack(StackAllocator& alloc, size_t count) {
        void* mem = alloc.Allocate(count * sizeof(T), alignof(T));
        if (!mem) return nullptr;
        T* arr = static_cast<T*>(mem);
        for (size_t i = 0; i < count; ++i)
            ::new (&arr[i]) T();
        return arr;
    }

} // namespace Engine
