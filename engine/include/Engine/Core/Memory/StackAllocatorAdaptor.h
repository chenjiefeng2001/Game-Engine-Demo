#pragma once

/**
 * @file StackAllocatorAdaptor.h
 * @brief 将 StackAllocator 适配为 C++ 标准分配器接口
 *
 * 允许 std::allocate_shared 等使用栈分配器分配对象。
 * deallocate() 为空操作——StackAllocator 在 Reset/析构时批量回收。
 */

#include "Engine/Core/Memory/StackAllocator.h"
#include <memory>

namespace Engine {

    template<typename T>
    struct StackAllocatorAdaptor {
        using value_type = T;

        StackAllocator* allocator = nullptr;

        StackAllocatorAdaptor() noexcept = default;
        explicit StackAllocatorAdaptor(StackAllocator* alloc) noexcept
            : allocator(alloc) {}

        template<typename U>
        StackAllocatorAdaptor(const StackAllocatorAdaptor<U>& other) noexcept
            : allocator(other.allocator) {}

        T* allocate(size_t n) {
            if (!allocator) {
                return static_cast<T*>(::operator new(n * sizeof(T)));
            }
            auto* ptr = allocator->Allocate(n * sizeof(T), alignof(T));
            if (!ptr) throw std::bad_alloc();
            return static_cast<T*>(ptr);
        }

        void deallocate(T*, size_t) noexcept {
            // StackAllocator 在 Reset/析构时批量回收
        }

        template<typename U>
        bool operator==(const StackAllocatorAdaptor<U>& other) const noexcept {
            return allocator == other.allocator;
        }

        template<typename U>
        bool operator!=(const StackAllocatorAdaptor<U>& other) const noexcept {
            return allocator != other.allocator;
        }
    };

} // namespace Engine
