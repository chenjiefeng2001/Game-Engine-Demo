#pragma once

/**
 * @file EntityCommandBuffer.h
 * @brief 命令缓冲 — 延迟执行结构性变更（解决遍历安全性）
 *
 * 在 ECS System 遍历 Chunk 时，严禁直接调用 AddComponent / RemoveComponent /
 * DestroyEntity，因为这些操作会导致 Archetype 迁移和 Chunk 数据搬移，
 * 进而使正在遍历的迭代器失效。
 *
 * 解决方案：所有结构性变更通过 ECB 记录为 Command，在帧末（所有 System
 * 遍历完成后）统一执行 Playback。
 *
 * 使用示例：
 * @code
 *   EntityCommandBuffer ecb;
 *   ecb.AddComponent<Health>(entity, Health{100});
 *   ecb.DestroyEntity(otherEntity);
 *   // ... 帧末 ...
 *   ecb.Playback(&entityManager);
 * @endcode
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include <vector>
#include <cstring>

namespace Engine {

class EntityCommandBuffer {
public:
    EntityCommandBuffer() = default;
    ~EntityCommandBuffer() = default;

    EntityCommandBuffer(const EntityCommandBuffer&) = delete;
    EntityCommandBuffer& operator=(const EntityCommandBuffer&) = delete;

    EntityCommandBuffer(EntityCommandBuffer&&) noexcept = default;
    EntityCommandBuffer& operator=(EntityCommandBuffer&&) noexcept = default;

    // ── 命令类型 ──
    enum class CommandType : uint8 {
        Create,
        Destroy,
        AddComponent,
        RemoveComponent,
    };

    // ── 记录命令 ──
    EntityHandle CreateEntity();

    void DestroyEntity(EntityHandle entity);

    template<typename T>
    void AddComponent(EntityHandle entity, const T& component) {
        Command cmd;
        cmd.type = CommandType::AddComponent;
        cmd.entity = entity;
        cmd.componentTypeID = ComponentType<T>::ID();
        cmd.dataSize = sizeof(T);

        if constexpr (sizeof(T) <= 48) {
            // 小对象优化：直接存储在 cmd.data 数组
            std::memcpy(cmd.inlineData, &component, sizeof(T));
        } else {
            // 大对象：堆分配
            cmd.dataPtr = new uint8[sizeof(T)];
            std::memcpy(cmd.dataPtr, &component, sizeof(T));
        }

        m_Commands.push_back(cmd);
    }

    template<typename T>
    void RemoveComponent(EntityHandle entity) {
        Command cmd;
        cmd.type = CommandType::RemoveComponent;
        cmd.entity = entity;
        cmd.componentTypeID = ComponentType<T>::ID();
        cmd.dataSize = 0;
        m_Commands.push_back(cmd);
    }

    // ── 批量执行 ──
    /** 执行所有命令（帧末调用） */
    void Playback(class EntityManager* em);

    /** 清空命令队列 */
    void Clear();

    /** 命令队列是否为空 */
    bool IsEmpty() const { return m_Commands.empty(); }

private:
    // 命令描述
    struct alignas(8) Command {
        CommandType type;
        uint8 pad[3];                // padding
        uint32 dataSize;             // 数据大小
        ComponentTypeID componentTypeID;
        EntityHandle entity;

        // 小对象存储（48 字节 = 最长组件数据）
        static constexpr size_t kInlineSize = 48;
        uint8 inlineData[kInlineSize];

        // 大对象指针（当 sizeof(T) > kInlineSize 时使用）
        uint8* dataPtr = nullptr;

        ~Command() {
            if (dataPtr) delete[] dataPtr;
        }

        Command() = default;
        Command(const Command&) = delete;
        Command& operator=(const Command&) = delete;
        Command(Command&& other) noexcept
            : type(other.type)
            , dataSize(other.dataSize)
            , componentTypeID(other.componentTypeID)
            , entity(other.entity)
            , dataPtr(other.dataPtr)
        {
            std::memcpy(inlineData, other.inlineData, kInlineSize);
            other.dataPtr = nullptr;
        }
        Command& operator=(Command&& other) noexcept {
            if (this != &other) {
                if (dataPtr) delete[] dataPtr;
                type = other.type;
                dataSize = other.dataSize;
                componentTypeID = other.componentTypeID;
                entity = other.entity;
                std::memcpy(inlineData, other.inlineData, kInlineSize);
                dataPtr = other.dataPtr;
                other.dataPtr = nullptr;
            }
            return *this;
        }

        const void* GetData() const {
            return dataPtr ? dataPtr : inlineData;
        }
    };

    std::vector<Command> m_Commands;
};

} // namespace Engine