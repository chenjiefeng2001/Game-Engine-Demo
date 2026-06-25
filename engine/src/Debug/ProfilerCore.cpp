#include "Engine/Debug/ProfilerCore.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <numeric>

namespace Engine { namespace Debug {

    ProfilerCore::ProfilerCore()
        : m_Buffer(m_Capacity) {
    }

    void ProfilerCore::BeginFrame(uint64 frameIndex) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_Head = (m_Head + 1) % m_Capacity;
        m_FrameCount++;

        // 如果正在帧捕获且缓冲区未满，保留样本
        if (m_Capture.isCapturing) {
            m_Capture.frames.push_back(FrameSample{});
            m_CurrentFrame = &m_Capture.frames.back();
        } else {
            m_CurrentFrame = &m_Buffer[m_Head];
        }

        m_CurrentFrame->frameIndex = frameIndex;
        m_CurrentFrame->cpuSamples.clear();
        m_CurrentFrame->gpuSamples.clear();
    }

    void ProfilerCore::EndFrame() {
        if (!m_CurrentFrame) return;

        m_CurrentFrame->frameTimeMs = m_CurrentFrame->cpuTotalMs + m_CurrentFrame->gpuTotalMs;

        // 性能预警
        if (m_WarningCallback && m_CurrentFrame->frameTimeMs > m_WarningThreshold) {
            m_WarningCallback(m_CurrentFrame->frameIndex, m_CurrentFrame->frameTimeMs);
        }

        m_CurrentFrame = nullptr;
    }

    void ProfilerCore::RecordCPUSample(const CPUSample& sample) {
        if (m_CurrentFrame) {
            m_CurrentFrame->cpuSamples.push_back(sample);
            m_CurrentFrame->cpuTotalMs += sample.elapsedMs;
        }
    }

    void ProfilerCore::RecordGPUSample(const GPUSample& sample) {
        if (m_CurrentFrame) {
            m_CurrentFrame->gpuSamples.push_back(sample);
            m_CurrentFrame->gpuTotalMs += sample.elapsedMs;
            m_CurrentFrame->totalDrawCalls += sample.drawCalls;
        }
    }

    void ProfilerCore::RecordMemorySample(const MemorySample& sample) {
        if (m_CurrentFrame) {
            m_CurrentFrame->memorySample = sample;
        }
    }

    void ProfilerCore::RecordPhysicsSample(const PhysicsSample& sample) {
        if (m_CurrentFrame) {
            m_CurrentFrame->physicsSample = sample;
        }
    }

    const FrameSample* ProfilerCore::GetFrame(uint32 index) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (index >= m_FrameCount || m_Buffer.empty()) return nullptr;
        uint32 idx = (index % m_Capacity);
        return &m_Buffer[idx];
    }

    const FrameSample* ProfilerCore::GetLastFrame() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_FrameCount == 0) return nullptr;
        return &m_Buffer[m_Head];
    }

    std::vector<const FrameSample*> ProfilerCore::GetRecentFrames(uint32 count) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<const FrameSample*> result;
        uint32 available = std::min(count, m_FrameCount);
        for (uint32 i = 0; i < available; ++i) {
            uint32 idx = (m_Head + m_Capacity - available + i + 1) % m_Capacity;
            result.push_back(&m_Buffer[idx]);
        }
        return result;
    }

    void ProfilerCore::StartCapture(const std::string& label) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Capture.isCapturing = true;
        m_Capture.label = label;
        m_Capture.captureFrameIndex = m_FrameCount;
        m_Capture.frames.clear();
    }

    void ProfilerCore::StopCapture() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Capture.isCapturing = false;
    }

    ProfilerCore::AggregatedStats ProfilerCore::GetAggregatedStats(uint32 frameCount) const {
        AggregatedStats stats;
        auto frames = GetRecentFrames(frameCount);
        if (frames.empty()) return stats;

        // 初始化
        stats.minFrameTimeMs = std::numeric_limits<double>::max();
        stats.maxFrameTimeMs = 0.0;

        double frameSum = 0.0, cpuSum = 0.0, gpuSum = 0.0, dcSum = 0.0;
        for (const auto* f : frames) {
            frameSum += f->frameTimeMs;
            cpuSum   += f->cpuTotalMs;
            gpuSum   += f->gpuTotalMs;
            dcSum    += f->totalDrawCalls;

            stats.minFrameTimeMs = std::min(stats.minFrameTimeMs, f->frameTimeMs);
            stats.maxFrameTimeMs = std::max(stats.maxFrameTimeMs, f->frameTimeMs);
        }

        uint32 n = static_cast<uint32>(frames.size());
        stats.avgFrameTimeMs = frameSum / n;
        stats.avgCPUms       = cpuSum / n;
        stats.avgGPUms       = gpuSum / n;
        stats.avgDrawCalls   = dcSum / n;
        stats.totalDrawCalls = static_cast<uint32>(dcSum);

        return stats;
    }

}} // namespace Engine::Debug