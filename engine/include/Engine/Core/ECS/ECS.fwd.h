#pragma once

/**
 * @file ECS.fwd.h
 * @brief ECS 核心前向声明与公共类型定义
 *
 * 64-bit Generational ID | 16KB Chunks | Archetype-based ECS
 *
 * 架构概览：
 *   EntityManager
 *     ├── SparseSet<EntityID>          — 实体分配池 (Index→Generation)
 *     ├── Archetype[]                   — 按组件签名分类的 Archetype
 *     │     └── Chunk[]                 — 16KB 固定大小内存块
 *     │           ├── Component 段      — 每个组件类型连续排列
 *     │           └── EntityMap         — 行号→EntityID 映射
 *     ├── EntityCommandBuffer           — 延迟结构性变更
 *     └── Query Cache                   — Archetype 匹配缓存
 */

#include "Engine/Types.h"
#include <cstddef>
#include <cstdint>
#include <bitset>

namespace Engine {

// ════════════════════════════════════════════════════════
// Entity 标识符 — 64-bit Generational ID
// ════════════════════════════════════════════════════════

struct alignas(8) EntityHandle {
    uint64 id = 0;  // 高 32-bit = Generation | 低 32-bit = Index

    uint32 Index()     const { return uint32(id & 0xFFFFFFFF); }
    uint32 Generation() const { return uint32(id >> 32); }

    bool IsNull() const { return id == 0; }
    bool IsValid() const { return id != 0; }

    bool operator==(EntityHandle o) const { return id == o.id; }
    bool operator!=(EntityHandle o) const { return id != o.id; }
    bool operator<(EntityHandle o) const { return id < o.id; }

    static EntityHandle Create(uint32 index, uint32 generation) {
        return EntityHandle{ (uint64(index)) | (uint64(generation) << 32) };
    }

    /** 从 index 重建 EntityHandle（Generation 置 0，仅用于映射查找） */
    static EntityHandle FromIndex(uint32 index) {
        return EntityHandle{ uint64(index) };
    }

    static const EntityHandle kNull;  // { 0 }
};

// 空 Entity 常量
inline const EntityHandle kNullEntity{ 0 };

// ════════════════════════════════════════════════════════
// 组件类型标识符 — 编译期唯一 ID
// ════════════════════════════════════════════════════════

using ComponentTypeID = uint32;

// 最大支持的组件类型数（bitset 宽度）
static constexpr size_t kMaxComponentTypes = 64;

// 组件签名 — bitset 标记 Archetype 包含哪些组件
using ComponentSignature = std::bitset<kMaxComponentTypes>;

// ════════════════════════════════════════════════════════
// 编译期组件类型注册
// ════════════════════════════════════════════════════════

namespace Internal {
    inline ComponentTypeID s_NextComponentTypeID = 0;
}

template<typename T>
struct ComponentType {
    static ComponentTypeID ID() {
        static ComponentTypeID s_ID = Internal::s_NextComponentTypeID++;
        return s_ID;
    }
};

// ════════════════════════════════════════════════════════
// 组件元信息（处理非 POD 类型的关键）
// ════════════════════════════════════════════════════════

struct ComponentMeta {
    ComponentTypeID typeID;
    size_t size;
    size_t alignment;
    void (*MoveConstruct)(void* dst, void* src);
    void (*Destruct)(void* ptr);
};

template<typename T>
ComponentMeta MakeComponentMeta() {
    return ComponentMeta{
        .typeID     = ComponentType<T>::ID(),
        .size       = sizeof(T),
        .alignment  = alignof(T),
        .MoveConstruct = [](void* d, void* s) {
            ::new (d) T(std::move(*static_cast<T*>(s)));
        },
        .Destruct   = [](void* p) {
            static_cast<T*>(p)->~T();
        },
    };
}

// ════════════════════════════════════════════════════════
// Chunk 常量
// ════════════════════════════════════════════════════════

// 每个 Chunk 固定 16KB（L1 Cache 友好）
// 现代 CPU L1 Data Cache 典型值为 32KB，16KB 可容纳两个组件流
static constexpr size_t kChunkSize = 16384;

// Chunk 内最大实体数（按最小组件 4B 计算）
static constexpr uint32 kChunkMaxEntities = 512;

// ════════════════════════════════════════════════════════
// 前向声明
// ════════════════════════════════════════════════════════

class Chunk;
class Archetype;
class EntityManager;
class EntityCommandBuffer;
class ECSQuery;
class ECSBridge;

} // namespace Engine