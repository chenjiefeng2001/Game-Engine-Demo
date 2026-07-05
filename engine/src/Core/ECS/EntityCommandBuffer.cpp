#include "Engine/Core/ECS/EntityCommandBuffer.h"
#include "Engine/Core/ECS/EntityManager.h"

namespace Engine {

// ── 记录创建命令 ──
EntityHandle EntityCommandBuffer::CreateEntity() {
    // 返回一个临时 EntityHandle，后续通过 Playback 分配实际 ID
    // 这里我们使用 kNullEntity 占位，Playback 时分配
    Command cmd;
    cmd.type = CommandType::Create;
    cmd.entity = EntityHandle::kNull;
    cmd.componentTypeID = 0;
    cmd.dataSize = 0;
    m_Commands.push_back(std::move(cmd));
    return cmd.entity;  // Playback 后会用实际 ID 替换
}

void EntityCommandBuffer::DestroyEntity(EntityHandle entity) {
    Command cmd;
    cmd.type = CommandType::Destroy;
    cmd.entity = entity;
    cmd.componentTypeID = 0;
    cmd.dataSize = 0;
    m_Commands.push_back(std::move(cmd));
}

// ── 批量执行 ──
void EntityCommandBuffer::Playback(EntityManager* em) {
    if (!em) return;

    for (auto& cmd : m_Commands) {
        switch (cmd.type) {
            case CommandType::Create:
                em->CreateEntity();
                break;

            case CommandType::Destroy:
                em->DestroyEntity(cmd.entity);
                break;

            case CommandType::AddComponent:
                em->AddComponentRaw(
                    cmd.entity,
                    cmd.componentTypeID,
                    cmd.GetData()
                );
                break;

            case CommandType::RemoveComponent:
                em->RemoveComponentRaw(cmd.entity, cmd.componentTypeID);
                break;
        }
    }

    m_Commands.clear();
}

void EntityCommandBuffer::Clear() {
    m_Commands.clear();
}

} // namespace Engine