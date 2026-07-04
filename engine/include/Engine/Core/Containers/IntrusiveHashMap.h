#pragma once

/**
 * @file IntrusiveHashMap.h
 * @brief 侵入式哈希表 — 节点指针嵌入对象中，零额外分配，O(1) 平均查找
 *
 * 设计思想：
 *   - 对象通过继承 IntrusiveHashMapNode 获得 next 指针（用于链表桶链）
 *   - 桶数组按负载因子扩容，扩容时重新散列所有已链入的节点
 *   - 与 std::unordered_map 相比：零节点堆分配（对象已在外部构造）
 *   - 特别适合 ECS 组件存储、事件监听器链等需要极低分配开销的场景
 *
 * 使用示例：
 * @code
 *   class Entity : public IntrusiveHashMapNode<Entity> {
 *   public:
 *       uint32_t GetKey() const { return m_ID; }
 *       uint32_t m_ID;
 *   };
 *
 *   IntrusiveHashMap<Entity, uint32_t> entityMap;
 *   Entity e;
 *   e.m_ID = 42;
 *   entityMap.Insert(e);
 *   Entity* found = entityMap.Find(42);  // nullptr if not found
 * @endcode
 */

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <cassert>

namespace Engine {

    // ============================================================
    // 侵入式哈希表节点基类
    // ============================================================
    /**
     * @brief 继承此基类的对象可被链入 IntrusiveHashMap 的桶链表
     *
     * @tparam T 派生类型（CRTP）
     */
    template <typename T>
    class IntrusiveHashMapNode {
    public:
        IntrusiveHashMapNode() noexcept = default;
        ~IntrusiveHashMapNode() noexcept = default;

        // 禁止拷贝（链表指针不应被复制）
        IntrusiveHashMapNode(const IntrusiveHashMapNode&) = delete;
        IntrusiveHashMapNode& operator=(const IntrusiveHashMapNode&) = delete;

        // 移动构造：转移链接
        IntrusiveHashMapNode(IntrusiveHashMapNode&& other) noexcept
            : m_Next(other.m_Next) {
            other.m_Next = nullptr;
        }

        IntrusiveHashMapNode& operator=(IntrusiveHashMapNode&& other) noexcept {
            if (this != &other) {
                m_Next = other.m_Next;
                other.m_Next = nullptr;
            }
            return *this;
        }

        /** 返回下一节点指针（供 begin()/end() 迭代器使用） */
        T* GetNext() const noexcept { return m_Next; }

    private:
        T* m_Next = nullptr;  // 桶链下一个节点

        template <typename, typename, typename> friend class IntrusiveHashMap;
    };

    // ============================================================
    // 侵入式哈希表
    // ============================================================
    /**
     * @brief 基于链表桶的侵入式哈希表
     *
     * @tparam T        节点类型（必须继承 IntrusiveHashMapNode<T>）
     * @tparam Key      键类型（由 GetKey() 返回）
     * @tparam Hasher   哈希函数对象（默认 std::hash<Key>）
     *
     * 扩容策略：
     *   - 初始桶数：8
     *   - 负载因子阈值：0.75（节点数/桶数 ≥ 0.75 时扩容 2x）
     *   - 扩容时重新散列已有节点
     *
     * 迭代：
     *   - 开链单向遍历：O(B + N)（B = 桶数，N = 节点数）
     *   - 适合全表遍历（如每帧遍历所有组件），不适合随机访问型频繁遍历
     */
    template <typename T, typename Key,
              typename Hasher = std::hash<Key>>
    class IntrusiveHashMap {
        // 编译期约束：T 必须继承 IntrusiveHashMapNode<T>
        static_assert(std::is_base_of_v<IntrusiveHashMapNode<T>, T>,
                      "T must inherit from IntrusiveHashMapNode<T>");

    public:
        // ============================================================
        // 构造 / 析构
        // ============================================================
        IntrusiveHashMap() { InitializeBuckets(8); }
        explicit IntrusiveHashMap(size_t initialBucketCount) {
            InitializeBuckets(std::max<size_t>(initialBucketCount, 4));
        }

        ~IntrusiveHashMap() = default;

        IntrusiveHashMap(const IntrusiveHashMap&) = delete;
        IntrusiveHashMap& operator=(const IntrusiveHashMap&) = delete;

        IntrusiveHashMap(IntrusiveHashMap&& other) noexcept
            : m_Buckets(std::move(other.m_Buckets))
            , m_Size(other.m_Size)
            , m_BucketMask(other.m_BucketMask) {
            other.m_Size = 0;
            other.m_BucketMask = 0;
            other.m_Buckets.clear();
        }

        IntrusiveHashMap& operator=(IntrusiveHashMap&& other) noexcept {
            if (this != &other) {
                m_Buckets = std::move(other.m_Buckets);
                m_Size = other.m_Size;
                m_BucketMask = other.m_BucketMask;
                other.m_Size = 0;
                other.m_BucketMask = 0;
                other.m_Buckets.clear();
            }
            return *this;
        }

        // ============================================================
        // 查询
        // ============================================================

        /**
         * @brief O(1) 平均查找
         * @param key 键值
         * @return 指向匹配节点的指针，未找到返回 nullptr
         */
        T* Find(const Key& key) noexcept {
            if (m_Buckets.empty() || m_Size == 0) return nullptr;

            size_t idx = BucketIndex(key);
            T* node = m_Buckets[idx];
            while (node) {
                if (node->GetKey() == key) return node;
                node = node->m_Next;
            }
            return nullptr;
        }

        const T* Find(const Key& key) const noexcept {
            return const_cast<IntrusiveHashMap*>(this)->Find(key);
        }

        /** @brief 返回节点数量 */
        size_t Size() const noexcept { return m_Size; }

        /** @brief 返回桶数量 */
        size_t BucketCount() const noexcept { return m_Buckets.size(); }

        /** @brief 当前负载因子 */
        float LoadFactor() const noexcept {
            return m_Buckets.empty() ? 0.0f
                : static_cast<float>(m_Size) / static_cast<float>(m_Buckets.size());
        }

        /** @brief 是否为空 */
        bool IsEmpty() const noexcept { return m_Size == 0; }

        // ============================================================
        // 修改
        // ============================================================

        /**
         * @brief 插入节点到哈希表
         *
         * @param node 要插入的节点引用
         * @return true 表示插入成功，false 表示键已存在（节点未插入）
         *
         * 复杂度：O(1) 平均。扩容时 O(N)。
         * 若负载因子超过 0.75，自动扩容 2x。
         */
        bool Insert(T& node) noexcept {
            // 负载检查
            if (m_Size * 4 >= m_Buckets.size() * 3) {  // load factor ≥ 0.75
                Rehash(m_Buckets.size() * 2);
            }

            const Key& key = node.GetKey();
            size_t idx = BucketIndex(key);

            // 检查是否已存在
            T* existing = m_Buckets[idx];
            while (existing) {
                if (existing->GetKey() == key) return false;  // 重复键
                existing = existing->m_Next;
            }

            // 头插法（O(1)）
            node.m_Next = m_Buckets[idx];
            m_Buckets[idx] = &node;
            ++m_Size;
            return true;
        }

        /**
         * @brief 查找或插入节点
         *
         * @param node 要插入的节点引用
         * @return 已存在的节点指针（nullptr 表示插入成功）
         */
        T* InsertOrFind(T& node) noexcept {
            const Key& key = node.GetKey();
            T* found = Find(key);
            if (found) return found;

            Insert(node);
            return nullptr;
        }

        /**
         * @brief 从哈希表中移除节点（O(1) 平均）
         *
         * @param node 要移除的节点引用
         * @return true 表示移除成功，false 表示节点不在表中
         */
        bool Remove(T& node) noexcept {
            const Key& key = node.GetKey();
            size_t idx = BucketIndex(key);

            T* prev = nullptr;
            T* curr = m_Buckets[idx];

            while (curr) {
                if (curr == &node) {
                    if (prev)
                        prev->m_Next = curr->m_Next;
                    else
                        m_Buckets[idx] = curr->m_Next;
                    curr->m_Next = nullptr;
                    --m_Size;
                    return true;
                }
                prev = curr;
                curr = curr->m_Next;
            }
            return false;
        }

        /** @brief 清空所有节点（仅重置指针，不销毁节点本身） */
        void Clear() noexcept {
            for (auto& bucket : m_Buckets)
                bucket = nullptr;
            m_Size = 0;
        }

        // ============================================================
        // 迭代器
        // ============================================================
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;

            Iterator() noexcept : m_Bucket(nullptr), m_Node(nullptr),
                                  m_BucketEnd(nullptr) {}

            reference operator*() const noexcept { return *m_Node; }
            pointer operator->() const noexcept { return m_Node; }

            Iterator& operator++() noexcept {
                Advance();
                return *this;
            }

            Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                Advance();
                return tmp;
            }

            bool operator==(const Iterator& other) const noexcept {
                return m_Node == other.m_Node;
            }
            bool operator!=(const Iterator& other) const noexcept {
                return m_Node != other.m_Node;
            }

        private:
            friend class IntrusiveHashMap;

            T** m_Bucket;
            T*   m_Node;
            T**  m_BucketEnd;

            Iterator(T** bucket, T* node, T** bucketEnd)
                : m_Bucket(bucket), m_Node(node), m_BucketEnd(bucketEnd) {}

            void Advance() noexcept {
                if (!m_Node) return;

                // 优先沿链表前进
                if (m_Node->m_Next) {
                    m_Node = m_Node->m_Next;
                    return;
                }

                // 链表末尾 → 跳到下一个非空桶
                ++m_Bucket;
                while (m_Bucket != m_BucketEnd) {
                    if (*m_Bucket) {
                        m_Node = *m_Bucket;
                        return;
                    }
                    ++m_Bucket;
                }
                m_Node = nullptr;  // end
            }
        };

        Iterator Begin() noexcept {
            if (m_Size == 0) return End();
            T** start = m_Buckets.data();
            T** end = start + m_Buckets.size();
            while (start != end) {
                if (*start) return Iterator(start, *start, end);
                ++start;
            }
            return End();
        }

        Iterator End() noexcept {
            return Iterator(nullptr, nullptr, nullptr);
        }

        // STL 兼容
        Iterator begin() noexcept { return Begin(); }
        Iterator end() noexcept   { return End(); }

        // ============================================================
        // 桶查询（调试用）
        // ============================================================
        struct BucketInfo {
            T*     head  = nullptr;
            size_t count = 0;
        };

        BucketInfo GetBucket(size_t index) const noexcept {
            if (index >= m_Buckets.size()) return {};
            T* h = m_Buckets[index];
            size_t cnt = 0;
            T* p = h;
            while (p) { ++cnt; p = p->m_Next; }
            return { h, cnt };
        }

    private:
        // ── 数据成员 ──
        std::vector<T*> m_Buckets;
        size_t m_Size = 0;
        size_t m_BucketMask = 0;  // m_Buckets.size() - 1（2^n 桶时用于快速取模）

        // ── 内部方法 ──

        void InitializeBuckets(size_t count) {
            m_Buckets.assign(count, nullptr);
            m_BucketMask = count - 1;
        }

        size_t BucketIndex(const Key& key) const noexcept {
            size_t hash = m_Hasher(key);
            return hash & m_BucketMask;
        }

        void Rehash(size_t newBucketCount) {
            if (newBucketCount < 4) newBucketCount = 4;

            std::vector<T*> newBuckets(newBucketCount, nullptr);
            size_t newMask = newBucketCount - 1;

            // 重新散列所有现有节点
            for (auto* head : m_Buckets) {
                T* node = head;
                while (node) {
                    T* next = node->m_Next;
                    size_t idx = (static_cast<size_t>(m_Hasher(node->GetKey()))) & newMask;
                    node->m_Next = newBuckets[idx];
                    newBuckets[idx] = node;
                    node = next;
                }
            }

            m_Buckets = std::move(newBuckets);
            m_BucketMask = newMask;
        }

        Hasher m_Hasher;
    };

} // namespace Engine