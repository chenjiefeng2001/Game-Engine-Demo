#pragma once

/**
 * @file StackList.h
 * @brief 栈分配器单向链表 — 节点从 StackAllocator 连续分配
 *
 * 设计目标：
 *   - 所有节点分配在 StackAllocator 上，连续内存 → 缓存友好
 *   - 无需逐节点析构，StackAllocator::Reset() 时批量回收
 *   - O(1) push_front，O(n) 遍历
 *   - 适用于「启动时构建，运行时只读遍历」的场景
 */

#include "Engine/Core/Memory/StackAllocator.h"
#include <cstddef>
#include <iterator>

namespace Engine {

    template<typename T>
    class StackList {
    public:
        // ============================================================
        // 节点定义
        // ============================================================
        struct Node {
            T     value;
            Node* next = nullptr;

            template<typename... Args>
            explicit Node(Args&&... args) : value(std::forward<Args>(args)...) {}
        };

        // ============================================================
        // 正向迭代器
        // ============================================================
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;

            Iterator() noexcept : m_Node(nullptr) {}
            explicit Iterator(Node* node) noexcept : m_Node(node) {}

            reference operator*() const noexcept { return m_Node->value; }
            pointer   operator->() const noexcept { return &m_Node->value; }

            Iterator& operator++() noexcept {
                m_Node = m_Node->next;
                return *this;
            }
            Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                m_Node = m_Node->next;
                return tmp;
            }

            bool operator==(const Iterator& other) const noexcept {
                return m_Node == other.m_Node;
            }
            bool operator!=(const Iterator& other) const noexcept {
                return m_Node != other.m_Node;
            }

            Node* GetNode() const noexcept { return m_Node; }

        private:
            Node* m_Node;
        };

        using ConstIterator = Iterator;  // 简化：ConstIterator 暂同 Iterator

        // ============================================================
        // 构造 / 析构
        // ============================================================
        explicit StackList(StackAllocator& allocator) noexcept
            : m_Alloc(allocator) {}

        // 禁止拷贝（节点生命周期由 StackAllocator 管理）
        StackList(const StackList&) = delete;
        StackList& operator=(const StackList&) = delete;

        // 移动构造（转移节点所有权）
        StackList(StackList&& other) noexcept
            : m_Alloc(other.m_Alloc)
            , m_Head(other.m_Head)
            , m_Size(other.m_Size) {
            other.m_Head = nullptr;
            other.m_Size = 0;
        }

        StackList& operator=(StackList&& other) noexcept {
            if (this != &other) {
                m_Head = other.m_Head;
                m_Size = other.m_Size;
                other.m_Head = nullptr;
                other.m_Size = 0;
            }
            return *this;
        }

        // ~StackList() 不做任何事 — StackAllocator::Reset() 批量回收

        // ============================================================
        // 修改操作
        // ============================================================

        /**
         * @brief 在头部插入元素（节点从 StackAllocator 分配）
         */
        template<typename... Args>
        Node& EmplaceFront(Args&&... args) {
            auto* node = NewOnStack<Node>(m_Alloc, std::forward<Args>(args)...);
            node->next = m_Head;
            m_Head = node;
            ++m_Size;
            return *node;
        }

        void PushFront(const T& value) { EmplaceFront(value); }
        void PushFront(T&& value)      { EmplaceFront(std::move(value)); }

        /**
         * @brief 清空链表（仅重置指针，不释放内存——由 StackAllocator 管理）
         */
        void Clear() noexcept {
            m_Head = nullptr;
            m_Size = 0;
        }

        /**
         * @brief 将链表头指针转移到另一个 StackList（共享同一个 StackAllocator）
         */
        void SpliceFront(StackList& other) noexcept {
            if (other.m_Head) {
                Node* tail = other.m_Head;
                while (tail->next) tail = tail->next;
                tail->next = m_Head;
                m_Head = other.m_Head;
                m_Size += other.m_Size;
                other.m_Head = nullptr;
                other.m_Size = 0;
            }
        }

        // ============================================================
        // 访问器
        // ============================================================

        Iterator Begin() noexcept { return Iterator(m_Head); }
        Iterator End() noexcept   { return Iterator(nullptr); }

        // STL 兼容
        Iterator begin() noexcept { return Begin(); }
        Iterator end() noexcept   { return End(); }

        T& Front() noexcept { return m_Head->value; }
        const T& Front() const noexcept { return m_Head->value; }

        bool IsEmpty() const noexcept { return m_Head == nullptr; }
        size_t Size() const noexcept { return m_Size; }

        /** 获取底层 StackAllocator 引用 */
        StackAllocator& GetAllocator() const noexcept { return m_Alloc; }

    private:
        StackAllocator& m_Alloc;
        Node*           m_Head = nullptr;
        size_t          m_Size = 0;
    };

} // namespace Engine
