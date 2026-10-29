/**
 * @file CommandListGpuTimestampProfiler.cpp
 * @brief 后端无关的 GPU 时间戳分段剖析器（T-4）
 *
 * 只依赖 IRHICommandList 的 WriteTimestamp/ResolveTimestampSpan/ResetTimestamps
 * 与 IRHIDevice::SupportsGpuTimestamps()。GL46 与 Vulkan 共用同一实现。
 */

#include "Engine/Core/RHI/GpuTimestampProfiler.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"

#include <set>
#include <utility>

namespace Engine {
namespace RHI {

namespace {

class CommandListGpuTimestampProfiler final : public IGpuTimestampProfiler {
public:
    bool Initialize(IRHIDevice* device, uint32_t maxSegments) override {
        m_Device = device;
        m_MaxSegments = maxSegments;
        m_Available = device != nullptr && maxSegments > 0 &&
                      device->SupportsGpuTimestamps();
        m_Segments.clear();
        m_Results.clear();
        m_Resolved = false;
        return m_Available;
    }

    void Shutdown() override {
        m_Device = nullptr;
        m_MaxSegments = 0;
        m_Available = false;
        m_Segments.clear();
        m_Results.clear();
        m_Resolved = false;
    }

    void BeginFrame() override {
        m_Segments.clear();
        m_Results.clear();
        m_Resolved = false;
    }

    uint32_t BeginSegment(IRHICommandList& cmd, const char* name) override {
        if (!m_Available || m_Segments.size() >= m_MaxSegments) return UINT32_MAX;
        const uint32_t beginIdx = cmd.WriteTimestamp();
        if (beginIdx == IRHICommandList::kInvalidTimestamp) return UINT32_MAX;

        Segment segment;
        segment.list = &cmd;
        segment.name = name ? name : "<unnamed>";
        segment.beginIdx = beginIdx;
        segment.endIdx = beginIdx;
        segment.closed = false;
        m_Segments.push_back(std::move(segment));
        return static_cast<uint32_t>(m_Segments.size() - 1);
    }

    void EndSegment(IRHICommandList& cmd, uint32_t segmentId) override {
        if (!m_Available || segmentId >= m_Segments.size()) return;
        Segment& segment = m_Segments[segmentId];
        if (segment.closed || segment.list != &cmd) return;
        const uint32_t endIdx = cmd.WriteTimestamp();
        if (endIdx == IRHICommandList::kInvalidTimestamp) return;
        segment.endIdx = endIdx;
        segment.closed = true;
    }

    const std::vector<SegmentResult>& Resolve() override {
        if (m_Resolved) return m_Results;
        m_Results.clear();
        std::set<IRHICommandList*> lists;
        for (const Segment& segment : m_Segments) {
            if (!segment.closed || segment.list == nullptr) continue;
            double ms = 0.0;
            const bool valid = segment.list->ResolveTimestampSpan(
                segment.beginIdx, segment.endIdx, ms);
            m_Results.push_back({segment.name, valid ? ms : 0.0, valid});
            lists.insert(segment.list);
        }
        // 解析后再重置查询环；调用方仍持有结果副本。
        for (IRHICommandList* list : lists) list->ResetTimestamps();
        m_Resolved = true;
        return m_Results;
    }

    bool IsAvailable() const override { return m_Available; }

private:
    struct Segment {
        IRHICommandList* list = nullptr;
        std::string name;
        uint32_t beginIdx = 0;
        uint32_t endIdx = 0;
        bool closed = false;
    };

    IRHIDevice* m_Device = nullptr;
    uint32_t m_MaxSegments = 0;
    bool m_Available = false;
    bool m_Resolved = false;
    std::vector<Segment> m_Segments;
    std::vector<SegmentResult> m_Results;
};

} // namespace

std::unique_ptr<IGpuTimestampProfiler> CreateCommandListGpuTimestampProfiler() {
    return std::make_unique<CommandListGpuTimestampProfiler>();
}

} // namespace RHI
} // namespace Engine
