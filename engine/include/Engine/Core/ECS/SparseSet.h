#pragma once

/**
 * @file SparseSet.h
 * @brief 稀疏集 — ECS 核心数据结构
 *
 * 两层索引表实现 O(1) 插入/删除/查找：
 *
 *   Sparse[Index] → Generation (世代号，0 = 空闲)
 *   Dense[Index]  → 用户数据 T
 *   FreeList       → 可复用的 Index 池
 *
 * 特性：
 *   - 紧凑存储：迭代 Dense 数组即遍历全部活动实体
 *   - 世代号：防止 Use-After-Free / dangling reference
 *   - 缓存友好：Dense 数组连续排列，适合 SIMD
 */

#include "Engine/Types.h"
#include <vector>
#include <cassert>

namespace Engine {

template<typename T>
class SparseSet {
public:
    SparseSet() = default;
    explicit SparseSet(size_t initialCapacity) {
        m_Sparse.resize(initialCapacity, 0);
        m_Dense.reserve(initialCapacity);
    }

    // ── 核心操作 ──

    /** 分配一个新的实体索引，返回 (index, generation) */
    uint32 Allocate() {
        uint32 index;
        uint32 generation;

        if (!m_FreeList.empty()) {
            // 从空闲池复用
            index = m_FreeList.back();
            m_FreeList.pop_back();
            // generation 递增，确保旧引用失效
            generation = (m_Sparse[index] >> 1) + 1;
            // 标记为活跃 (bit0 = 1)
            m_Sparse[index] = (generation << 1) | 1;
        } else {
            // 分配新索引
            index = static_cast<uint32>(m_Dense.size());
            generation = 1;
            if (index >= m_Sparse.size()) {
                m_Sparse.resize(m_Sparse.size() == 0 ? 64 : m_Sparse.size() * 2, 0);
            }
            m_Sparse[index] = (generation << 1) | 1;
            m_Dense.emplace_back();
        }

        m_ActiveCount++;
        return index;
    }

    /** 释放实体索引（标记为可复用） */
    void Free(uint32 index) {
        assert(index < m_Sparse.size());
        if (!IsAlive(index)) return;

        // 标记为死亡 (bit0 = 0)，保留 generation 不变
        m_Sparse[index] &= ~1u;
        m_FreeList.push_back(index);
        m_ActiveCount--;
    }

    /** 检查索引是否存活 */
    bool IsAlive(uint32 index) const {
        if (index >= m_Sparse.size()) return false;
        return (m_Sparse[index] & 1) != 0;
    }

    /** 获取世代号 */
    uint32 GetGeneration(uint32 index) const {
        if (index >= m_Sparse.size()) return 0;
        return m_Sparse[index] >> 1;
    }

    /** 活跃实体计数（非 Dense 大小，Dense 不会被压缩） */
    size_t Size() const { return m_ActiveCount; }

    // ── Dense 数组访问 ──

    T& GetDense(uint32 denseIndex) {
        assert(denseIndex < m_Dense.size());
        return m_Dense[denseIndex];
    }

    const T& GetDense(uint32 denseIndex) const {
        assert(denseIndex < m_Dense.size());
        return m_Dense[denseIndex];
    }

    bool Empty() const { return m_ActiveCount == 0; }

    T* Data() { return m_Dense.data(); }
    const T* Data() const { return m_Dense.data(); }

    size_t DenseSize() const { return m_Dense.size(); }

    // ── 迭代器 ──
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    iterator begin() { return m_Dense.begin(); }
    iterator end()   { return m_Dense.end(); }
    const_iterator begin() const { return m_Dense.begin(); }
    const_iterator end()   const { return m_Dense.end(); }

    void Clear() {
        m_Dense.clear();
        m_FreeList.clear();
        m_ActiveCount = 0;
        std::fill(m_Sparse.begin(), m_Sparse.end(), 0);
    }

private:
    // Sparse[index] = (generation << 1) | alive_bit
    std::vector<uint32> m_Sparse;
    std::vector<T>      m_Dense;
    std::vector<uint32> m_FreeList;
    size_t              m_ActiveCount = 0;
};

} // namespace Engine