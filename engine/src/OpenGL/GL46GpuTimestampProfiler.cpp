/**
 * @file GL46GpuTimestampProfiler.cpp
 * @brief GL46 GPU 时间戳分段剖析器 — 复用 OpenGLContext::InitGPUQueries 的已验证模式
 *
 * 实现要点：
 *   - GL_TIMESTAMP 查询需要驱动支持非零 QUERY_COUNTER_BITS（NVIDIA/AMD/Intel 均满足）
 *   - 每个分段消耗一对查询对象（begin/end），并持有其命令列表引用用于解析
 *   - 时间戳解析依赖 GPU 完成，Resolve() 前调用方必须 WaitIdle 或 fence signal
 *   - 与 GL46CommandList::ResetTimestamps 配合实现查询环复用
 */

#include "Engine/Core/RHI/GpuTimestampProfiler.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/Log.h"

#include <cstdint>

namespace Engine {
namespace RHI {

namespace {
    constexpr uint32_t kTimestampsPerSegment = 2;

    /// 单命令列表时间戳池上限（GL46CommandList::Impl::kMaxTimestamps）
    constexpr uint32_t kMaxListTimestamps = 512;
}

class GL46GpuTimestampProfiler final : public IGpuTimestampProfiler {
public:
    ~GL46GpuTimestampProfiler() override { Shutdown(); }

    bool Initialize(IRHIDevice* device, uint32_t maxSegments) override {
        m_Segments.clear();
        m_Results.clear();

        if (!device || maxSegments == 0) return false;
        auto* gl46 = dynamic_cast<GL46Device*>(device);
        if (!gl46) return false;

        // 驱动能力探测：GL_TIMESTAMP 计数位为 0 表示不支持
        GLint bits = 0;
        gl46->GetGL().GetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        if (bits == 0) return false;

        if (maxSegments * kTimestampsPerSegment > kMaxListTimestamps)
            maxSegments = kMaxListTimestamps / kTimestampsPerSegment;

        m_Device = device;
        m_MaxSegments = static_cast<uint32_t>(maxSegments);
        m_Available = true;
        return true;
    }

    void Shutdown() override {
        m_Device = nullptr;
        m_MaxSegments = 0;
        m_Available = false;
        m_Segments.clear();
        m_Results.clear();
    }

    void BeginFrame() override { m_Segments.clear(); }

    uint32_t BeginSegment(IRHICommandList& cmd, const char* name) override {
        if (!m_Available || m_Segments.size() >= m_MaxSegments) return UINT32_MAX;

        const uint32_t beginIdx = cmd.WriteTimestamp();
        if (beginIdx == IRHICommandList::kInvalidTimestamp) return UINT32_MAX;

        Segment seg;
        seg.list = &cmd;
        seg.name = name ? name : "<unnamed>";
        seg.beginIdx = beginIdx;
        seg.endIdx   = beginIdx;   // 占位，EndSegment 时覆盖
        seg.closed   = false;
        m_Segments.push_back(seg);
        return static_cast<uint32_t>(m_Segments.size() - 1);
    }

    void EndSegment(IRHICommandList& cmd, uint32_t segmentId) override {
        if (!m_Available || segmentId >= m_Segments.size()) return;
        Segment& seg = m_Segments[segmentId];
        if (seg.closed || seg.list != &cmd) return;

        const uint32_t endIdx = cmd.WriteTimestamp();
        if (endIdx == IRHICommandList::kInvalidTimestamp) return;

        seg.endIdx = endIdx;
        seg.closed = true;
    }

    const std::vector<SegmentResult>& Resolve() override {
        m_Results.clear();
        for (const Segment& seg : m_Segments) {
            if (!seg.closed || !seg.list) continue;
            double ms = 0.0;
            if (!seg.list->ResolveTimestampSpan(seg.beginIdx, seg.endIdx, ms)) continue;
            m_Results.push_back({seg.name, ms});
        }
        return m_Results;
    }

    bool IsAvailable() const override { return m_Available; }

private:
    struct Segment {
        IRHICommandList* list = nullptr;   ///< 录制该分段的命令列表（解析来源）
        std::string name;
        uint32_t beginIdx = 0;
        uint32_t endIdx   = 0;
        bool closed = false;
    };

    IRHIDevice*  m_Device = nullptr;
    uint32_t     m_MaxSegments = 0;
    bool         m_Available = false;
    std::vector<Segment> m_Segments;
    std::vector<SegmentResult> m_Results;
};

std::unique_ptr<IGpuTimestampProfiler> CreateGpuTimestampProfiler() {
    return std::make_unique<GL46GpuTimestampProfiler>();
}

} // namespace RHI
} // namespace Engine
