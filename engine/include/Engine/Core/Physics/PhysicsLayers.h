#pragma once

/**
 * @file PhysicsLayers.h
 * @brief Jolt Physics ObjectLayer / BroadPhaseLayer 映射常量
 *
 *   ObjectLayer → BroadPhaseLayer 的映射在 Jolt 初始化时一次性构建，
 *   Layer 过多会显著增加物理过滤开销。
 *   此处预定义 8 个标准 Layer，切勿随意扩展。
 */

#include "Engine/Types.h"
#include <cstdint>

namespace Engine {

// ════════════════════════════════════════════════════════
// 预定义 ObjectLayer（最多 16 层）
// ════════════════════════════════════════════════════════
enum class ObjectLayer : uint16 {
    NON_MOVING = 0,     // 静态世界（地面、墙壁）
    MOVING     = 1,     // 动态物体（角色、投射物）
    DEBRIS     = 2,     // 碎片（碎片只与地面碰撞，不与碎片互碰）
    SENSOR     = 3,     // 传感器（触发器，无物理响应）
    PLAYER     = 4,     // 玩家（可与 ENEMY 碰撞，不与 SENSOR 碰撞）
    ENEMY      = 5,     // 敌人
    RAGDOLL    = 6,     // 布娃娃（与自身碰撞）
    EFFECT     = 7,     // 特效（仅用于射线检测）

    COUNT      = 8,     // 总层数（不超过 16）
};

// ════════════════════════════════════════════════════════
// BroadPhaseLayer — 宽相检测层（比 ObjectLayer 更粗粒度）
// ════════════════════════════════════════════════════════
enum class BroadPhaseLayer : uint8 {
    NON_MOVING = 0,
    MOVING     = 1,
    DEBRIS     = 2,
    SENSOR     = 3,
    COUNT      = 4,
};

// ════════════════════════════════════════════════════════
// ObjectLayer → BroadPhaseLayer 映射表
// ════════════════════════════════════════════════════════
inline BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) {
    switch (layer) {
        case ObjectLayer::NON_MOVING: return BroadPhaseLayer::NON_MOVING;
        case ObjectLayer::MOVING:     return BroadPhaseLayer::MOVING;
        case ObjectLayer::DEBRIS:     return BroadPhaseLayer::DEBRIS;
        case ObjectLayer::SENSOR:     return BroadPhaseLayer::SENSOR;
        case ObjectLayer::PLAYER:     return BroadPhaseLayer::MOVING;
        case ObjectLayer::ENEMY:      return BroadPhaseLayer::MOVING;
        case ObjectLayer::RAGDOLL:    return BroadPhaseLayer::MOVING;
        case ObjectLayer::EFFECT:     return BroadPhaseLayer::MOVING;
        default:                      return BroadPhaseLayer::NON_MOVING;
    }
}

// ════════════════════════════════════════════════════════
// ObjectLayer 两两碰撞过滤器
// ════════════════════════════════════════════════════════
// 返回 true = 需要碰撞检测；false = 跳过
inline bool ShouldCollide(ObjectLayer a, ObjectLayer b) {
    // 静态 vs 静态 → 不碰撞
    if (a == ObjectLayer::NON_MOVING && b == ObjectLayer::NON_MOVING)
        return false;

    // 碎片 vs 碎片 → 不碰撞
    if (a == ObjectLayer::DEBRIS && b == ObjectLayer::DEBRIS)
        return false;

    // 碎片只与 NON_MOVING 碰撞
    if (a == ObjectLayer::DEBRIS || b == ObjectLayer::DEBRIS) {
        return a == ObjectLayer::NON_MOVING || b == ObjectLayer::NON_MOVING;
    }

    // 传感器只与 MOVING/PLAYER/ENEMY 碰撞
    if (a == ObjectLayer::SENSOR || b == ObjectLayer::SENSOR) {
        if (a == ObjectLayer::SENSOR && b == ObjectLayer::SENSOR) return false;
        return true;
    }

    // 其他情况全部碰撞
    return true;
}

} // namespace Engine