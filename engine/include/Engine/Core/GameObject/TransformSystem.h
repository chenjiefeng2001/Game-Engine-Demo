#pragma once

/**
 * @file TransformSystem.h
 * @brief 变换系统 — 版本号驱动的批量世界矩阵更新
 *
 * 核心优化：
 *   1. 版本号对比（Lazy Validation）：不递归标记后代 dirty
 *   2. 深度优先线性布局：父节点一定先于子节点被遍历
 *   3. 单次线性扫描：O(N) 完成全树世界矩阵更新
 *
 * 原理：
 *   - 每个 Transform 维护 localVersion 和 worldVersion
 *   - 修改局部属性时 localVersion++（O(1)，不传播）
 *   - 需要世界矩阵时对比父节点的 worldVersion
 *   - TransformSystem 每帧线性扫描所有节点
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <cstdint>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// 线性存储的变换节点（取代 Transform 的递归结构）
// ═══════════════════════════════════════════════════════════

struct alignas(16) TransformNode {
    // ── 局部变换数据 ──
    Vec3 position;
    Vec3 rotation;   // 欧拉角（deg），编辑器友好；内部计算时转为 Quat
    Vec3 scale;

    // ── 计算后的矩阵 ──
    Mat4 localMatrix;    // model → parent space
    Mat4 worldMatrix;    // model → world space

    // ── 层级信息（索引化，无指针） ──
    uint32 parentIndex;          // 父节点索引（0xFFFFFFFF = 根节点）
    uint32 firstChildIndex;      // 首个子节点索引
    uint32 nextSiblingIndex;     // 下一个兄弟节点索引

    // ── 版本号系统 ──
    uint32 localVersion;            // 局部属性修改时自增
    uint32 worldVersion;            // 当前世界矩阵版本
    uint32 parentWorldVersion;      // 父节点 worldVersion（用于判断是否需要重算）

    // ── 标识 ──
    bool enabled = true;

    // ── 标记为脏（在 SetPosition/SetRotation/SetScale 中调用） ──
    void MarkDirty() { localVersion++; }
};

// ═══════════════════════════════════════════════════════════
// TransformSystem
// ═══════════════════════════════════════════════════════════

class TransformSystem {
public:
    TransformSystem() = default;
    ~TransformSystem() = default;

    TransformSystem(const TransformSystem&) = delete;
    TransformSystem& operator=(const TransformSystem&) = delete;

    // ── 节点管理 ──

    /** 添加一个根节点 */
    uint32 AddRootNode(const Vec3& pos = Vec3(0,0,0));

    /** 添加子节点 */
    uint32 AddChildNode(uint32 parentIndex, const Vec3& pos = Vec3(0,0,0));

    /** 移除节点（及其所有子节点） */
    void RemoveNode(uint32 nodeIndex);

    /** 获取节点引用 */
    TransformNode& GetNode(uint32 index);
    const TransformNode& GetNode(uint32 index) const;

    /** 节点总数 */
    uint32 GetNodeCount() const { return static_cast<uint32>(m_Nodes.size()); }

    // ── 节点访问 ──

    TransformNode* GetNodes() { return m_Nodes.data(); }
    const TransformNode* GetNodes() const { return m_Nodes.data(); }

    // ── 批量更新 ──

    /**
     * @brief 批量更新所有节点的世界矩阵
     *
     * 由于 m_Nodes 按深度优先排列，父节点始终在子节点之前。
     * 单次线性扫描即可完成全部更新，无递归调用。
     *
     * @param parallax 是否使用并行（暂不使用，预留接口）
     */
    void UpdateWorldMatrices(bool parallel = false);

    /**
     * @brief 获取所有需要更新世界矩阵的节点数量（调试信息）
     */
    uint32 GetDirtyNodeCount() const;

    // ── 层级查询 ──

    /** 获取节点深度（根节点深度 = 0） */
    uint32 GetNodeDepth(uint32 nodeIndex) const;

    /** 遍历子节点 */
    void ForEachChild(uint32 parentIndex, std::function<void(uint32)> callback) const;

private:
    /** 重新计算局部矩阵（从 position/rotation/scale 生成 localMatrix） */
    void RecalcLocalMatrix(TransformNode& node);

    // 深度优先存储：父节点一定在子节点之前
    std::vector<TransformNode> m_Nodes;
};

} // namespace Engine