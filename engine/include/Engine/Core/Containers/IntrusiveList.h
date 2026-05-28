#pragma once

/**
 * @file IntrusiveList.h
 * @brief 侵入式双向链表 — 链接指针内嵌在对象中，零额外分配
 *
 * 设计目标：
 *   - 节点即对象本身，无需额外的 Node 包装
 *   - 对象必须继承 IntrusiveListNode 或包含 IntrusiveListHook 成员
 *   - O(1) 插入/删除，零堆分配
 *   - 适用于对象池/组件树的快速增删
 *
 * 使用示例：
 * @code
 *   class Entity : public IntrusiveListNode<Entity> {
 *       int id;
 *   };
 *
 *   IntrusiveList<Entity> list;
 *   Entity e1, e2;
 *   list.PushBack(e1);
 *   list.PushBack(e2);
 *   for (auto& e : list) { ... }
 *   list.Remove(e1);  // O(1)
 * @endcode
 */

#include <cstddef>
#include <iterator>

namespace Engine {

    // ============================================================
    // 侵入式链表节点基类 — 对象从此继承即可获得 prev/next 指针
    // ============================================================
    template<typename T>
    class IntrusiveListNode {
    public:
        T* m_Next = nullptr;
        T* m_Prev = nullptr;

    protected:
        IntrusiveListNode() = default;
        ~IntrusiveListNode() = default;

        // 禁止拷贝（链表指针不应被复制）
        IntrusiveListNode(const IntrusiveListNode&) noexcept = delete;
        IntrusiveListNode& operator=(const IntrusiveListNode&) noexcept = delete;

        // 移动时转移链接
        IntrusiveListNode(IntrusiveListNode&& other) noexcept
            : m_Next(other.m_Next)
            , m_Prev(other.m_Prev) {
            if (m_Next) static_cast<T*>(m_Next)->m_Prev = static_cast<T*>(this);
            if (m_Prev) static_cast<T*>(m_Prev)->m_Next = static_cast<T*>(this);
            other.m_Next = nullptr;
            other.m_Prev = nullptr;
        }

        IntrusiveListNode& operator=(IntrusiveListNode&& other) noexcept {
            if (this != &other) {
                Unlink();
                m_Next = other.m_Next;
                m_Prev = other.m_Prev;
                if (m_Next) static_cast<T*>(m_Next)->m_Prev = static_cast<T*>(this);
                if (m_Prev) static_cast<T*>(m_Prev)->m_Next = static_cast<T*>(this);
                other.m_Next = nullptr;
                other.m_Prev = nullptr;
            }
            return *this;
        }

        /** 将节点从所在链表中移除（不销毁对象本身） */
        void Unlink() noexcept {
            if (m_Prev) static_cast<T*>(m_Prev)->m_Next = m_Next;
            if (m_Next) static_cast<T*>(m_Next)->m_Prev = m_Prev;
            m_Next = nullptr;
            m_Prev = nullptr;
        }

        friend class IntrusiveList<T>;
    };

    // ============================================================
    // 侵入式双向链表
    // ============================================================
    template<typename T>
    class IntrusiveList {
        static_assert(std::is_base_of_v<IntrusiveListNode<T>, T>,
                      "T must inherit from IntrusiveListNode<T>");

    public:
        // ============================================================
        // 双向迭代器
        // ============================================================
        class Iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;

            Iterator() noexcept : m_Node(nullptr) {}
            explicit Iterator(T* node) noexcept : m_Node(node) {}

            reference operator*() const noexcept { return *m_Node; }
            pointer   operator->() const noexcept { return m_Node; }

            Iterator& operator++() noexcept {
                m_Node = m_Node->m_Next;
                return *this;
            }
            Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                m_Node = m_Node->m_Next;
                return tmp;
            }

            Iterator& operator--() noexcept {
                m_Node = m_Node->m_Prev;
                return *this;
            }
            Iterator operator--(int) noexcept {
                Iterator tmp = *this;
                m_Node = m_Node->m_Prev;
                return tmp;
            }

            bool operator==(const Iterator& other) const noexcept {
                return m_Node == other.m_Node;
            }
            bool operator!=(const Iterator& other) const noexcept {
                return m_Node != other.m_Node;
            }

        private:
            T* m_Node;
            friend class IntrusiveList;
        };

        // ============================================================
        // 构造 / 析构
        // ============================================================
        IntrusiveList() noexcept = default;
        ~IntrusiveList() = default;

        // 禁止拷贝
        IntrusiveList(const IntrusiveList&) = delete;
        IntrusiveList& operator=(const IntrusiveList&) = delete;

        // 移动（转移头指针，不移动已有节点）
        IntrusiveList(IntrusiveList&& other) noexcept
            : m_Head(other.m_Head)
            , m_Tail(other.m_Tail)
            , m_Size(other.m_Size) {
            other.m_Head = nullptr;
            other.m_Tail = nullptr;
            other.m_Size = 0;
        }

        IntrusiveList& operator=(IntrusiveList&& other) noexcept {
            if (this != &other) {
                m_Head = other.m_Head;
                m_Tail = other.m_Tail;
                m_Size = other.m_Size;
                other.m_Head = nullptr;
                other.m_Tail = nullptr;
                other.m_Size = 0;
            }
            return *this;
        }

        // ============================================================
        // 修改操作
        // ============================================================

        /** @brief 在头部插入节点（O(1)） */
        void PushFront(T& node) noexcept {
            node.m_Prev = nullptr;
            node.m_Next = m_Head;
            if (m_Head) m_Head->m_Prev = &node;
            else        m_Tail = &node;
            m_Head = &node;
            ++m_Size;
        }

        /** @brief 在尾部插入节点（O(1)） */
        void PushBack(T& node) noexcept {
            node.m_Next = nullptr;
            node.m_Prev = m_Tail;
            if (m_Tail) m_Tail->m_Next = &node;
            else        m_Head = &node;
            m_Tail = &node;
            ++m_Size;
        }

        /** @brief 在指定迭代器前插入节点（O(1)） */
        void InsertBefore(Iterator pos, T& node) noexcept {
            if (pos == Begin()) { PushFront(node); return; }
            T* after = pos.m_Node;
            T* before = after->m_Prev;
            node.m_Prev = before;
            node.m_Next = after;
            before->m_Next = &node;
            after->m_Prev = &node;
            ++m_Size;
        }

        /** @brief 在指定迭代器后插入节点（O(1)） */
        void InsertAfter(Iterator pos, T& node) noexcept {
            if (pos.m_Node == m_Tail) { PushBack(node); return; }
            T* before = pos.m_Node;
            T* after = before->m_Next;
            node.m_Prev = before;
            node.m_Next = after;
            before->m_Next = &node;
            after->m_Prev = &node;
            ++m_Size;
        }

        /** @brief 从链表中移除节点（O(1)，不销毁对象本身） */
        void Remove(T& node) noexcept {
            if (node.m_Prev) node.m_Prev->m_Next = node.m_Next;
            else             m_Head = node.m_Next;
            if (node.m_Next) node.m_Next->m_Prev = node.m_Prev;
            else             m_Tail = node.m_Prev;
            node.m_Next = nullptr;
            node.m_Prev = nullptr;
            --m_Size;
        }

        /** @brief 移除迭代器指向的节点，返回下一个迭代器 */
        Iterator Remove(Iterator it) noexcept {
            if (it == End()) return End();
            T* node = it.m_Node;
            Iterator next(node->m_Next);
            Remove(*node);
            return next;
        }

        /** @brief 将 other 的全部节点转移到当前链表尾部 */
        void SpliceBack(IntrusiveList& other) noexcept {
            if (other.IsEmpty()) return;
            if (IsEmpty()) {
                m_Head = other.m_Head;
                m_Tail = other.m_Tail;
            } else {
                m_Tail->m_Next = other.m_Head;
                other.m_Head->m_Prev = m_Tail;
                m_Tail = other.m_Tail;
            }
            m_Size += other.m_Size;
            other.m_Head = nullptr;
            other.m_Tail = nullptr;
            other.m_Size = 0;
        }

        /** @brief 清空链表（仅重置指针，节点仍存活） */
        void Clear() noexcept {
            m_Head = nullptr;
            m_Tail = nullptr;
            m_Size = 0;
        }

        // ============================================================
        // 访问器
        // ============================================================

        Iterator Begin() noexcept { return Iterator(m_Head); }
        Iterator End() noexcept   { return Iterator(nullptr); }

        // STL 兼容
        Iterator begin() noexcept { return Begin(); }
        Iterator end() noexcept   { return End(); }

        T& Front() noexcept { return *m_Head; }
        const T& Front() const noexcept { return *m_Head; }

        T& Back() noexcept { return *m_Tail; }
        const T& Back() const noexcept { return *m_Tail; }

        bool IsEmpty() const noexcept { return m_Head == nullptr; }
        size_t Size() const noexcept { return m_Size; }

    private:
        T*      m_Head = nullptr;
        T*      m_Tail = nullptr;
        size_t  m_Size = 0;
    };

} // namespace Engine
