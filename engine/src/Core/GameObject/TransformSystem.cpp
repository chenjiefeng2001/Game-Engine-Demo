#include "Engine/Core/GameObject/TransformSystem.h"
#include <cstring>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// 节点管理
// ═══════════════════════════════════════════════════════════

uint32 TransformSystem::AddRootNode(const Vec3& pos) {
    uint32 index = static_cast<uint32>(m_Nodes.size());
    
    TransformNode node;
    node.position = pos;
    node.rotation = Vec3(0, 0, 0);
    node.scale    = Vec3(1, 1, 1);
    node.parentIndex       = UINT32_MAX;
    node.firstChildIndex   = UINT32_MAX;
    node.nextSiblingIndex  = UINT32_MAX;
    node.localVersion      = 1;
    node.worldVersion      = 0;
    node.parentWorldVersion = 0;
    
    RecalcLocalMatrix(node);
    node.worldMatrix = node.localMatrix;
    node.worldVersion = node.localVersion;
    node.parentWorldVersion = node.localVersion;
    
    m_Nodes.push_back(node);
    return index;
}

uint32 TransformSystem::AddChildNode(uint32 parentIndex, const Vec3& pos) {
    if (parentIndex >= m_Nodes.size()) return UINT32_MAX;
    
    uint32 index = static_cast<uint32>(m_Nodes.size());
    
    TransformNode node;
    node.position = pos;
    node.rotation = Vec3(0, 0, 0);
    node.scale    = Vec3(1, 1, 1);
    node.parentIndex       = parentIndex;
    node.firstChildIndex   = UINT32_MAX;
    node.nextSiblingIndex  = UINT32_MAX;
    node.localVersion      = 1;
    node.worldVersion      = 0;
    node.parentWorldVersion = 0;
    
    RecalcLocalMatrix(node);
    
    // 插入到父节点的子节点链表头
    TransformNode& parent = m_Nodes[parentIndex];
    node.nextSiblingIndex = parent.firstChildIndex;
    parent.firstChildIndex = index;
    
    m_Nodes.push_back(node);
    return index;
}

void TransformSystem::RemoveNode(uint32 nodeIndex) {
    if (nodeIndex >= m_Nodes.size()) return;
    
    TransformNode& node = m_Nodes[nodeIndex];
    
    // 从父节点的子节点链表中移除
    if (node.parentIndex != UINT32_MAX) {
        TransformNode& parent = m_Nodes[node.parentIndex];
        if (parent.firstChildIndex == nodeIndex) {
            parent.firstChildIndex = node.nextSiblingIndex;
        } else {
            uint32 prev = parent.firstChildIndex;
            while (prev != UINT32_MAX) {
                TransformNode& sibling = m_Nodes[prev];
                if (sibling.nextSiblingIndex == nodeIndex) {
                    sibling.nextSiblingIndex = node.nextSiblingIndex;
                    break;
                }
                prev = sibling.nextSiblingIndex;
            }
        }
    }
    
    // 标记为已移除（通过 parentIndex = UINT32_MAX + worldVersion = 0）
    // 实际项目中需要更复杂的空洞管理，这里简化处理
    node.enabled = false;
    node.parentIndex = UINT32_MAX;
    
    // 注：向量中不实际删除，避免破坏索引。
    // 生产级实现应使用空洞列表或标记删除 + 延迟压缩
}

TransformNode& TransformSystem::GetNode(uint32 index) {
    return m_Nodes[index];
}

const TransformNode& TransformSystem::GetNode(uint32 index) const {
    return m_Nodes[index];
}

// ═══════════════════════════════════════════════════════════
// 批量更新世界矩阵
// ═══════════════════════════════════════════════════════════

void TransformSystem::UpdateWorldMatrices(bool parallel) {
    (void)parallel;  // 暂不支持并行
    
    uint32 count = static_cast<uint32>(m_Nodes.size());
    
    for (uint32 i = 0; i < count; ++i) {
        TransformNode& node = m_Nodes[i];
        if (!node.enabled) continue;
        
        // 检查局部矩阵是否已过期
        if (node.localVersion != node.worldVersion) {
            RecalcLocalMatrix(node);
        }
        
        if (node.parentIndex == UINT32_MAX) {
            // 根节点：world = local
            if (node.worldVersion != node.localVersion) {
                node.worldMatrix = node.localMatrix;
                node.worldVersion = node.localVersion;
            }
        } else {
            TransformNode& parent = m_Nodes[node.parentIndex];
            // 父节点 worldVersion 变化 → 需要重算世界矩阵
            if (node.parentWorldVersion != parent.worldVersion) {
                Mat4Multiply(parent.worldMatrix, node.localMatrix, node.worldMatrix);
                node.parentWorldVersion = parent.worldVersion;
                node.worldVersion = node.localVersion;  // 更新为匹配
            }
        }
    }
}

uint32 TransformSystem::GetDirtyNodeCount() const {
    uint32 count = 0;
    for (const auto& node : m_Nodes) {
        if (!node.enabled) continue;
        if (node.parentIndex == UINT32_MAX) {
            if (node.worldVersion != node.localVersion) count++;
        } else {
            // 检查父节点版本是否匹配
            if (node.parentIndex < m_Nodes.size()) {
                if (node.parentWorldVersion != m_Nodes[node.parentIndex].worldVersion) count++;
            }
        }
    }
    return count;
}

// ═══════════════════════════════════════════════════════════
// 层级查询
// ═══════════════════════════════════════════════════════════

uint32 TransformSystem::GetNodeDepth(uint32 nodeIndex) const {
    uint32 depth = 0;
    while (nodeIndex != UINT32_MAX && nodeIndex < m_Nodes.size()) {
        nodeIndex = m_Nodes[nodeIndex].parentIndex;
        if (nodeIndex != UINT32_MAX) depth++;
    }
    return depth;
}

void TransformSystem::ForEachChild(
    uint32 parentIndex,
    std::function<void(uint32)> callback
) const {
    if (parentIndex >= m_Nodes.size()) return;
    
    uint32 child = m_Nodes[parentIndex].firstChildIndex;
    while (child != UINT32_MAX && child < m_Nodes.size()) {
        callback(child);
        child = m_Nodes[child].nextSiblingIndex;
    }
}

// ═══════════════════════════════════════════════════════════
// 局部矩阵计算
// ═══════════════════════════════════════════════════════════

void TransformSystem::RecalcLocalMatrix(TransformNode& node) {
    // 使用 glm 计算 T * R * S
    glm::mat4 mat(1.0f);
    
    // 平移
    mat = glm::translate(mat, glm::vec3(node.position.x, node.position.y, node.position.z));
    
    // 旋转（欧拉角 → 四元数 → 矩阵）
    // 注意：使用 ZYX 顺序（适用于大多数游戏引擎）
    glm::quat q = glm::quat(glm::radians(glm::vec3(
        node.rotation.x,
        node.rotation.y,
        node.rotation.z
    )));
    mat = mat * glm::mat4_cast(q);
    
    // 缩放
    mat = glm::scale(mat, glm::vec3(node.scale.x, node.scale.y, node.scale.z));
    
    // 写回
    std::memcpy(node.localMatrix.data, glm::value_ptr(mat), sizeof(float32) * 16);
    
    node.worldVersion = node.localVersion;
}

} // namespace Engine