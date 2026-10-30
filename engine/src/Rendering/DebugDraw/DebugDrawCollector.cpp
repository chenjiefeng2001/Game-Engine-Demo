#include "Engine/Rendering/DebugDraw/DebugDrawCollector.h"

#include "Engine/Core/Log.h"

#include <cmath>
#include <utility>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

namespace {

constexpr float32 kPi2 = 6.28318530718f;
constexpr float32 kQuatEpsilon = 1.0e-6f;
constexpr float32 kLengthEpsilon = 1.0e-6f;

bool IsFinite(float32 value) {
    return std::isfinite(value);
}

bool IsFinite(const Vec3& v) {
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

bool IsValidColor(const Vec4& color) {
    return IsFinite(color.x) && IsFinite(color.y) &&
           IsFinite(color.z) && IsFinite(color.w) &&
           color.x >= 0.0f && color.x <= 1.0f &&
           color.y >= 0.0f && color.y <= 1.0f &&
           color.z >= 0.0f && color.z <= 1.0f &&
           color.w >= 0.0f && color.w <= 1.0f;
}

bool IsValidDuration(float32 durationSeconds) {
    return IsFinite(durationSeconds) && durationSeconds >= 0.0f;
}

bool NormalizeQuat(const Quat& input, Quat& output) {
    if (!IsFinite(input.x) || !IsFinite(input.y) ||
        !IsFinite(input.z) || !IsFinite(input.w)) {
        return false;
    }
    const float32 lengthSq = input.x * input.x + input.y * input.y +
                             input.z * input.z + input.w * input.w;
    if (!IsFinite(lengthSq) || lengthSq <= kQuatEpsilon) {
        return false;
    }
    const float32 invLength = 1.0f / std::sqrt(lengthSq);
    output = Quat(input.x * invLength, input.y * invLength,
                  input.z * invLength, input.w * invLength);
    return true;
}

Vec3 Rotate(const Quat& rotation, const Vec3& v) {
    const Vec3 q(rotation.x, rotation.y, rotation.z);
    const Vec3 uv = Vec3::Cross(q, v);
    const Vec3 uuv = Vec3::Cross(q, uv);
    return Vec3(v.x + 2.0f * (rotation.w * uv.x + uuv.x),
                v.y + 2.0f * (rotation.w * uv.y + uuv.y),
                v.z + 2.0f * (rotation.w * uv.z + uuv.z));
}

void AddLine(std::vector<Vertex>& out, const Vec3& from, const Vec3& to,
             const Vec4& color) {
    out.push_back(Vertex{from, color});
    out.push_back(Vertex{to, color});
}

} // namespace

Status Collector::Initialize(IRenderer* renderer) {
    if (!renderer) return Status::InvalidArgument;
    if (m_Initialized) return Status::AlreadyInitialized;

    m_Renderer = renderer;
    m_OwnerThread = std::this_thread::get_id();
    m_Initialized = true;
    m_InFrame = false;
    m_BudgetWarningIssued = false;
    m_View = View{};
    m_DeltaTimeSeconds = 0.0f;
    m_Persistent.clear();
    m_Current.clear();
    m_PersistentVertices = 0;
    m_CurrentVertices = 0;
    m_Stats = Stats{};
    m_Stats.capacityVertices = kMaxVerticesPerFrame;
    return Status::Ok;
}

Status Collector::Shutdown() {
    if (!m_Initialized) return Status::Ok;
    if (std::this_thread::get_id() != m_OwnerThread) return Status::WrongThread;

    ClearAll();
    m_Initialized = false;
    m_Renderer = nullptr;
    return Status::Ok;
}

Status Collector::CheckCollectState() const {
    if (!m_Initialized) return Status::NotInitialized;
    if (std::this_thread::get_id() != m_OwnerThread) return Status::WrongThread;
    if (!m_InFrame) return Status::FrameNotStarted;
    return Status::Ok;
}

Status Collector::BeginFrame(const View& view, float32 deltaTimeSeconds) {
    if (!m_Initialized) return Status::NotInitialized;
    if (std::this_thread::get_id() != m_OwnerThread) return Status::WrongThread;
    if (m_InFrame) return Status::FrameAlreadyStarted;
    if (!IsFinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0f) {
        return Status::InvalidArgument;
    }
    for (int32 i = 0; i < 16; ++i) {
        if (!IsFinite(view.viewProjection.data[i])) {
            return Status::InvalidArgument;
        }
    }

    AgePersistent(deltaTimeSeconds);

    m_Current.clear();
    m_CurrentVertices = 0;
    m_Stats = Stats{};
    m_Stats.capacityVertices = kMaxVerticesPerFrame;
    m_View = view;
    m_DeltaTimeSeconds = deltaTimeSeconds;
    m_InFrame = true;
    return Status::Ok;
}

Status Collector::EndFrame() {
    if (!m_Initialized) return Status::NotInitialized;
    if (std::this_thread::get_id() != m_OwnerThread) return Status::WrongThread;
    if (!m_InFrame) return Status::FrameNotStarted;
    if (!m_Renderer) {
        m_InFrame = false;
        return Status::BackendFailure;
    }

    const uint32 totalVertices = m_PersistentVertices + m_CurrentVertices;
    std::vector<Vertex> depthVertices;
    std::vector<Vertex> xrayVertices;
    depthVertices.reserve(totalVertices);
    xrayVertices.reserve(totalVertices);

    auto appendCommands = [&](const std::vector<Command>& commands) {
        for (const auto& command : commands) {
            std::vector<Vertex>& target =
                command.depthTest ? depthVertices : xrayVertices;
            target.insert(target.end(), command.vertices.begin(),
                          command.vertices.end());
        }
    };
    appendCommands(m_Persistent);
    appendCommands(m_Current);

    m_Stats.acceptedVertices = static_cast<uint32>(
        depthVertices.size() + xrayVertices.size());
    m_Stats.acceptedCommands = static_cast<uint32>(
        m_Persistent.size() + m_Current.size());
    m_Stats.budgetWarning = m_Stats.droppedCommands > 0;

    FrameGeometry geometry;
    geometry.depthVertices = depthVertices.empty() ? nullptr : depthVertices.data();
    geometry.depthVertexCount = static_cast<uint32>(depthVertices.size());
    geometry.xrayVertices = xrayVertices.empty() ? nullptr : xrayVertices.data();
    geometry.xrayVertexCount = static_cast<uint32>(xrayVertices.size());

    const Status renderStatus = m_Renderer->Render(m_View, geometry);
    m_InFrame = false;
    m_DeltaTimeSeconds = 0.0f;

    if (renderStatus != Status::Ok) {
        // 失败帧不把本帧命令带入持久集合；持久集合本身仍保留已 aging 的结果。
        m_Current.clear();
        m_CurrentVertices = 0;
        return renderStatus;
    }

    for (auto& command : m_Current) {
        if (command.remainingSeconds > 0.0f) {
            m_PersistentVertices += static_cast<uint32>(command.vertices.size());
            m_Persistent.push_back(std::move(command));
        }
    }
    m_Current.clear();
    m_CurrentVertices = 0;
    return Status::Ok;
}

Status Collector::Append(std::vector<Vertex>&& vertices,
                         float32 durationSeconds, bool depthTest) {
    const uint32 incoming = static_cast<uint32>(vertices.size());
    const uint64 used = static_cast<uint64>(m_PersistentVertices) +
                        static_cast<uint64>(m_CurrentVertices);
    if (used + static_cast<uint64>(incoming) >
        static_cast<uint64>(kMaxVerticesPerFrame)) {
        ++m_Stats.droppedCommands;
        m_Stats.droppedVertices += incoming;
        m_Stats.budgetWarning = true;
        if (!m_BudgetWarningIssued) {
            m_BudgetWarningIssued = true;
            ENGINE_LOG_WARN("DebugDraw",
                "vertex budget exceeded: dropped command ({} vertices), "
                "persistent={}, current={}, capacity={}",
                incoming, m_PersistentVertices, m_CurrentVertices,
                kMaxVerticesPerFrame);
        }
        return Status::Ok;
    }

    Command command;
    command.vertices = std::move(vertices);
    command.remainingSeconds = durationSeconds;
    command.depthTest = depthTest;
    m_CurrentVertices += incoming;
    m_Current.push_back(std::move(command));
    return Status::Ok;
}

Status Collector::Line(const Vec3& from, const Vec3& to, const Vec4& color,
                       float32 durationSeconds, bool depthTest) {
    const Status state = CheckCollectState();
    if (state != Status::Ok) return state;
    if (!IsFinite(from) || !IsFinite(to) || !IsValidColor(color) ||
        !IsValidDuration(durationSeconds)) {
        return Status::InvalidArgument;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(2);
    AddLine(vertices, from, to, color);
    return Append(std::move(vertices), durationSeconds, depthTest);
}

Status Collector::WireBox(const Vec3& center, const Vec3& halfExtents,
                          const Quat& rotation, const Vec4& color,
                          float32 durationSeconds, bool depthTest) {
    const Status state = CheckCollectState();
    if (state != Status::Ok) return state;
    if (!IsFinite(center) || !IsFinite(halfExtents) ||
        halfExtents.x <= 0.0f || halfExtents.y <= 0.0f ||
        halfExtents.z <= 0.0f || !IsValidColor(color) ||
        !IsValidDuration(durationSeconds)) {
        return Status::InvalidArgument;
    }

    Quat normalizedRotation;
    if (!NormalizeQuat(rotation, normalizedRotation)) {
        return Status::InvalidArgument;
    }

    Vec3 corners[8];
    for (int32 i = 0; i < 8; ++i) {
        const Vec3 local((i & 1) ? halfExtents.x : -halfExtents.x,
                         (i & 2) ? halfExtents.y : -halfExtents.y,
                         (i & 4) ? halfExtents.z : -halfExtents.z);
        corners[i] = center + Rotate(normalizedRotation, local);
    }

    static constexpr int32 kEdges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0},
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    std::vector<Vertex> vertices;
    vertices.reserve(24);
    for (const auto& edge : kEdges) {
        AddLine(vertices, corners[edge[0]], corners[edge[1]], color);
    }
    return Append(std::move(vertices), durationSeconds, depthTest);
}

Status Collector::WireSphere(const Vec3& center, float32 radius,
                             const Vec4& color, float32 durationSeconds,
                             bool depthTest) {
    const Status state = CheckCollectState();
    if (state != Status::Ok) return state;
    if (!IsFinite(center) || !IsFinite(radius) || radius <= 0.0f ||
        !IsValidColor(color) || !IsValidDuration(durationSeconds)) {
        return Status::InvalidArgument;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(kWireSphereLineSegments * 2);
    for (int32 plane = 0; plane < 3; ++plane) {
        for (uint32 i = 0; i < kWireSphereSegments; ++i) {
            const float32 angle0 = kPi2 * static_cast<float32>(i) /
                                   static_cast<float32>(kWireSphereSegments);
            const float32 angle1 = kPi2 * static_cast<float32>(i + 1) /
                                   static_cast<float32>(kWireSphereSegments);
            const float32 c0 = std::cos(angle0) * radius;
            const float32 s0 = std::sin(angle0) * radius;
            const float32 c1 = std::cos(angle1) * radius;
            const float32 s1 = std::sin(angle1) * radius;

            Vec3 p0 = center;
            Vec3 p1 = center;
            if (plane == 0) {
                p0.x += c0; p0.y += s0;
                p1.x += c1; p1.y += s1;
            } else if (plane == 1) {
                p0.x += c0; p0.z += s0;
                p1.x += c1; p1.z += s1;
            } else {
                p0.y += c0; p0.z += s0;
                p1.y += c1; p1.z += s1;
            }
            AddLine(vertices, p0, p1, color);
        }
    }
    return Append(std::move(vertices), durationSeconds, depthTest);
}

Status Collector::Axis(const Vec3& origin, const Quat& rotation, float32 length,
                       float32 durationSeconds, bool depthTest) {
    const Status state = CheckCollectState();
    if (state != Status::Ok) return state;
    if (!IsFinite(origin) || !IsFinite(length) || length <= 0.0f ||
        !IsValidDuration(durationSeconds)) {
        return Status::InvalidArgument;
    }

    Quat normalizedRotation;
    if (!NormalizeQuat(rotation, normalizedRotation)) {
        return Status::InvalidArgument;
    }

    const Vec3 x = Rotate(normalizedRotation, Vec3(1.0f, 0.0f, 0.0f)) * length;
    const Vec3 y = Rotate(normalizedRotation, Vec3(0.0f, 1.0f, 0.0f)) * length;
    const Vec3 z = Rotate(normalizedRotation, Vec3(0.0f, 0.0f, 1.0f)) * length;

    std::vector<Vertex> vertices;
    vertices.reserve(6);
    AddLine(vertices, origin, origin + x, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    AddLine(vertices, origin, origin + y, Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    AddLine(vertices, origin, origin + z, Vec4(0.0f, 0.0f, 1.0f, 1.0f));
    return Append(std::move(vertices), durationSeconds, depthTest);
}

Status Collector::Frustum(const Vec3* corners, const Vec4& color,
                          float32 durationSeconds, bool depthTest) {
    const Status state = CheckCollectState();
    if (state != Status::Ok) return state;
    if (!corners || !IsValidColor(color) ||
        !IsValidDuration(durationSeconds)) {
        return Status::InvalidArgument;
    }
    for (int32 i = 0; i < 8; ++i) {
        if (!IsFinite(corners[i])) return Status::InvalidArgument;
    }

    static constexpr int32 kEdges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    std::vector<Vertex> vertices;
    vertices.reserve(24);
    for (const auto& edge : kEdges) {
        AddLine(vertices, corners[edge[0]], corners[edge[1]], color);
    }
    return Append(std::move(vertices), durationSeconds, depthTest);
}

void Collector::AgePersistent(float32 deltaTimeSeconds) {
    uint32 remainingVertices = 0;
    for (auto it = m_Persistent.begin(); it != m_Persistent.end();) {
        it->remainingSeconds -= deltaTimeSeconds;
        if (it->remainingSeconds <= 0.0f) {
            it = m_Persistent.erase(it);
            continue;
        }
        remainingVertices += static_cast<uint32>(it->vertices.size());
        ++it;
    }
    m_PersistentVertices = remainingVertices;
}

void Collector::ClearAll() {
    m_Persistent.clear();
    m_Current.clear();
    m_PersistentVertices = 0;
    m_CurrentVertices = 0;
    m_InFrame = false;
    m_DeltaTimeSeconds = 0.0f;
    m_View = View{};
    m_Stats = Stats{};
    m_Stats.capacityVertices = kMaxVerticesPerFrame;
    m_BudgetWarningIssued = false;
}

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
