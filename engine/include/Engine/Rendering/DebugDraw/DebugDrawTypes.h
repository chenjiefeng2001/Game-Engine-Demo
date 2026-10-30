#pragma once

/**
 * @file DebugDrawTypes.h
 * @brief T-5 Generic DebugDraw — public POD types
 *
 * 约束（Gate A）：
 *   - 世界空间、右手坐标系；
 *   - 颜色为线性 RGBA、straight alpha，分量范围 [0,1]；
 *   - durationSeconds == 0 表示仅当前帧，> 0 表示跨帧保留；
 *   - 所有图元最终展开为线段。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {

// ── 服务状态 ──
enum class Status : uint8 {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    FrameNotStarted,
    FrameAlreadyStarted,
    WrongThread,
    InvalidArgument,
    Unsupported,
    BackendFailure
};

inline const char* StatusToString(Status status) {
    switch (status) {
        case Status::Ok:                 return "Ok";
        case Status::NotInitialized:     return "NotInitialized";
        case Status::AlreadyInitialized: return "AlreadyInitialized";
        case Status::FrameNotStarted:    return "FrameNotStarted";
        case Status::FrameAlreadyStarted:return "FrameAlreadyStarted";
        case Status::WrongThread:        return "WrongThread";
        case Status::InvalidArgument:    return "InvalidArgument";
        case Status::Unsupported:        return "Unsupported";
        case Status::BackendFailure:     return "BackendFailure";
        default:                         return "Unknown";
    }
}

// ── 帧视图 ──
struct View {
    Mat4 viewProjection;
};

// ── 展开后的线段顶点 ──
struct Vertex {
    Vec3 position;
    Vec4 color;
};

// ── Collector → Renderer 的帧数据 ──
// 指针只在当前 Render 调用期间有效。
struct FrameGeometry {
    const Vertex* depthVertices = nullptr;
    uint32        depthVertexCount = 0;
    const Vertex* xrayVertices = nullptr;
    uint32        xrayVertexCount = 0;
};

// ── 帧统计 ──
struct Stats {
    uint32 acceptedVertices = 0;
    uint32 acceptedCommands = 0;
    uint32 droppedCommands = 0;
    uint32 droppedVertices = 0;
    uint32 capacityVertices = 50000;
    bool   budgetWarning = false;
};

// ── 冻结参数 ──
constexpr uint32 kMaxVerticesPerFrame = 50000;
constexpr uint32 kWireSphereSegments = 16;
constexpr uint32 kWireSphereLineSegments = 3 * kWireSphereSegments; // 48

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
