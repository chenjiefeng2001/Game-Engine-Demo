#pragma once

/**
 * @file SmallVector.h
 * @brief 小向量 — 栈上预分配 N 个元素，超出时堆分配
 *
 * 设计目标：
 *   - 小数据量时零堆分配，所有元素在栈上连续存储
 *   - 超出 N 后自动切换到堆分配（行为类似 std::vector）
 *   - 适用于「绝大多数情况很小，偶尔变大」的集合
 *   - 完美缓存局部性（小数据时在栈上连续，大数据时在堆上连续）
 *
 * 使用示例：
 * @code
 *   SmallVector<int, 8> vec;       // 最多 8 个元素在栈上
 *   vec.PushBack(42);
 *   for (int i = 0; i < 100; ++i)
 *       vec.PushBack(i);           // 超过 8 后自动堆分配
 * @endcode
 */

#include <cstddef>
#include <cstring>
#include <new>
#include <utility>
#include <iterator>
#include <initializer_list>

namespace Engine {

    template<typename T, size_t InlineCapacity = 8>
    class SmallVector {
    public:
        // ============================================================
        // 类型定义
        // ============================================================
        using value_type      = T;
        using reference       = T&;
        using const_reference = const T&;
        using pointer         = T*;
        using const_pointer   = const T*;
        using iterator        = T*;
        using const_iterator  = const T*;
        using size_type       = size_t;

        // ============================================================
        // 构造 / 析构
        // ============================================================
        SmallVector() noexcept : m_Data(InlinePtr()), m_Capacity(InlineCapacity) {}

        SmallVector(std::initializer_list<T> init) : SmallVector() {
            Reserve(init.size());
            for (auto& v : init)
                PushBack(std::move(v));
        }

        SmallVector(const SmallVector& other) : SmallVector() {
            Reserve(other.m_Size);
            for (size_t i = 0; i < other.m_Size; ++i)
                PushBack(other.m_Data[i]);
        }

        SmallVector(SmallVector&& other) noexcept
            : m_Data(other.m_Data)
            , m_Size(other.m_Size)
            , m_Capacity(other.m_Capacity) {
            if (other.IsUsingInline()) {
                // 迁移内联数据
                for (size_t i = 0; i < m_Size; ++i) {
                    new (InlinePtr() + i) T(std::move(other.m_Data[i]));
                    other.m_Data[i].~T();
                }
                m_Data = InlinePtr();
            }
            other.m_Data = other.InlinePtr();
            other.m_Size = 0;
            other.m_Capacity = InlineCapacity;
        }

        SmallVector& operator=(const SmallVector& other) {
            if (this != &other) {
                Clear();
                Reserve(other.m_Size);
                for (size_t i = 0; i < other.m_Size; ++i)
                    PushBack(other.m_Data[i]);
            }
            return *this;
        }

        SmallVector& operator=(SmallVector&& other) noexcept {
            if (this != &other) {
                Clear();
                if (!IsUsingInline()) {
                    ::operator delete(m_Data, m_Capacity * sizeof(T),
                                      static_cast<std::align_val_t>(alignof(T)));
                }
                m_Data = other.m_Data;
                m_Size = other.m_Size;
                m_Capacity = other.m_Capacity;
                if (other.IsUsingInline()) {
                    for (size_t i = 0; i < m_Size; ++i) {
                        new (InlinePtr() + i) T(std::move(other.m_Data[i]));
                        other.m_Data[i].~T();
                    }
                    m_Data = InlinePtr();
                }
                other.m_Data = other.InlinePtr();
                other.m_Size = 0;
                other.m_Capacity = InlineCapacity;
            }
            return *this;
        }

        ~SmallVector() {
            Clear();
            if (!IsUsingInline()) {
                ::operator delete(m_Data, m_Capacity * sizeof(T),
                                  static_cast<std::align_val_t>(alignof(T)));
            }
        }

        // ============================================================
        // 元素访问
        // ============================================================

        reference operator[](size_t index) noexcept { return m_Data[index]; }
        const_reference operator[](size_t index) const noexcept { return m_Data[index]; }

        reference Front() noexcept { return m_Data[0]; }
        const_reference Front() const noexcept { return m_Data[0]; }

        reference Back() noexcept { return m_Data[m_Size - 1]; }
        const_reference Back() const noexcept { return m_Data[m_Size - 1]; }

        pointer Data() noexcept { return m_Data; }
        const_pointer Data() const noexcept { return m_Data; }

        // ============================================================
        // 迭代器
        // ============================================================

        iterator begin() noexcept { return m_Data; }
        iterator end() noexcept   { return m_Data + m_Size; }

        const_iterator begin() const noexcept { return m_Data; }
        const_iterator end() const noexcept   { return m_Data + m_Size; }

        // ============================================================
        // 容量
        // ============================================================

        bool IsEmpty() const noexcept { return m_Size == 0; }
        size_t Size() const noexcept { return m_Size; }
        size_t Capacity() const noexcept { return m_Capacity; }

        /** @brief 预分配容量，避免多次堆分配 */
        void Reserve(size_t newCapacity) {
            if (newCapacity <= m_Capacity) return;
            Reallocate(newCapacity);
        }

        /** @brief 收缩到当前大小（释放多余内存） */
        void ShrinkToFit() {
            if (IsUsingInline()) return;
            if (m_Size <= InlineCapacity) {
                // 迁移回内联存储
                T* newData = InlinePtr();
                for (size_t i = 0; i < m_Size; ++i) {
                    new (newData + i) T(std::move(m_Data[i]));
                    m_Data[i].~T();
                }
                ::operator delete(m_Data, m_Capacity * sizeof(T),
                                  static_cast<std::align_val_t>(alignof(T)));
                m_Data = newData;
                m_Capacity = InlineCapacity;
            } else {
                Reallocate(m_Size);
            }
        }

        // ============================================================
        // 修改操作
        // ============================================================

        /** @brief 在末尾构造元素 */
        template<typename... Args>
        reference EmplaceBack(Args&&... args) {
            if (m_Size >= m_Capacity) {
                size_t newCap = m_Capacity * 2;
                if (newCap < InlineCapacity * 2) newCap = InlineCapacity * 2;
                Reallocate(newCap);
            }
            T* ptr = m_Data + m_Size;
            ::new (ptr) T(std::forward<Args>(args)...);
            ++m_Size;
            return *ptr;
        }

        void PushBack(const T& value) { EmplaceBack(value); }
        void PushBack(T&& value)      { EmplaceBack(std::move(value)); }

        /** @brief 在指定位置插入元素 */
        iterator Insert(iterator pos, const T& value) {
            return Emplace(pos, value);
        }

        template<typename... Args>
        iterator Emplace(iterator pos, Args&&... args) {
            size_t index = pos - m_Data;
            if (m_Size >= m_Capacity) {
                size_t newCap = m_Capacity * 2;
                if (newCap < InlineCapacity * 2) newCap = InlineCapacity * 2;
                Reallocate(newCap);
                pos = m_Data + index;
            }
            // 后移元素
            for (size_t i = m_Size; i > index; --i) {
                ::new (m_Data + i) T(std::move(m_Data[i - 1]));
                m_Data[i - 1].~T();
            }
            ::new (pos) T(std::forward<Args>(args)...);
            ++m_Size;
            return m_Data + index;
        }

        /** @brief 移除末尾元素 */
        void PopBack() noexcept {
            if (m_Size > 0) {
                --m_Size;
                m_Data[m_Size].~T();
            }
        }

        /** @brief 移除指定位置的元素 */
        iterator Erase(iterator pos) noexcept {
            if (pos < begin() || pos >= end()) return end();
            pos->~T();
            for (iterator it = pos + 1; it != end(); ++it) {
                ::new (it - 1) T(std::move(*it));
                it->~T();
            }
            --m_Size;
            return pos;
        }

        /** @brief 清空所有元素 */
        void Clear() noexcept {
            for (size_t i = 0; i < m_Size; ++i)
                m_Data[i].~T();
            m_Size = 0;
        }

        /** @brief 调整大小 */
        void Resize(size_t newSize) {
            if (newSize > m_Size) {
                Reserve(newSize);
                for (size_t i = m_Size; i < newSize; ++i)
                    ::new (m_Data + i) T();
            } else if (newSize < m_Size) {
                for (size_t i = newSize; i < m_Size; ++i)
                    m_Data[i].~T();
            }
            m_Size = newSize;
        }

    private:
        // ============================================================
        // 内部实现
        // ============================================================

        /** 内联存储 */
        alignas(T) std::byte m_Inline[InlineCapacity * sizeof(T)];

        T*      m_Data;      // 当前数据指针（指向 Inline 或堆）
        size_t  m_Size = 0;
        size_t  m_Capacity;

        T* InlinePtr() noexcept {
            return reinterpret_cast<T*>(m_Inline);
        }

        bool IsUsingInline() const noexcept {
            return m_Data == reinterpret_cast<const T*>(m_Inline);
        }

        void Reallocate(size_t newCapacity) {
            T* newData = static_cast<T*>(::operator new(
                newCapacity * sizeof(T),
                static_cast<std::align_val_t>(alignof(T))));
            // 移动旧元素到新内存
            for (size_t i = 0; i < m_Size; ++i) {
                ::new (newData + i) T(std::move(m_Data[i]));
                m_Data[i].~T();
            }
            // 释放旧堆内存（如果是堆分配）
            if (!IsUsingInline()) {
                ::operator delete(m_Data, m_Capacity * sizeof(T),
                                  static_cast<std::align_val_t>(alignof(T)));
            }
            m_Data = newData;
            m_Capacity = newCapacity;
        }
    };

} // namespace Engine
