#pragma once

/**
 * @file ProfilerCore.h
 * @brief 工业级性能分析核心 — CPU/GPU/Memory 多维度采样数据
 *
 * 架构设计（参考 Unreal Profiler + Unity Profiler）：
 *   - 环形缓冲区存储最近 N 帧的采样数据
 *   - 每个样本包含 CPU/GPU/内存/物理多项指标
 *   - 支持帧捕获 = 冻结环形缓冲区供逐帧分析
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

namespace Engine { namespace Debug {

    // ============================================================
    // CPU 采样数据
    // ============================================================
    struct CPUSample {
        std::string name;
        double      startTimeMs  = 0.0;
        double      elapsedMs    = 0.0;
        uint32      callCount    = 0;
        uint32      depth        = 0;
    };

    // ============================================================
    // GPU 采样数据
    // ============================================================
    struct GPUSample {
        std::string passName;
        double      elapsedMs    = 0.0;
        uint32      drawCalls    = 0;
        uint32      vertexCount  = 0;
        uint32      triangleCount = 0;
        uint32      shaderSwitches = 0;
    };

    // ============================================================
    // 物理采样数据
    // ============================================================
    struct PhysicsSample {
        uint32      activeBodies    = 0;
        uint32      collisionTests  = 0;
        uint32      contacts        = 0;
        double      stepTimeMs      = 0.0;
    };

    // ============================================================
    // 内存采样数据
    // ============================================================
    struct MemorySample {
        uint64      totalPhysicalMB   = 0;
        uint64      usedPhysicalMB    = 0;
        uint64      textureVRAMBytes  = 0;
        uint64      bufferVRAMBytes   = 0;
        uint64      totalHeapBytes    = 0;
        uint64      usedHeapBytes     = 0;
        uint32      textureCount      = 0;
        uint32      meshCount         = 0;
        uint32      audioClipCount    = 0;

        // 内存快照对比数据
        struct Diff {
            int64   deltaBytes = 0;
            uint32  allocationCount = 0;
            uint32  deallocationCount = 0;
        };
        Diff diff;
    };

    // ============================================================
    // 完整帧样本
    // ============================================================
    struct FrameSample {
        uint64      frameIndex     = 0;
        double      cpuTotalMs     = 0.0;
        double      gpuTotalMs     = 0.0;
        double      frameTimeMs    = 0.0;
        uint32      totalDrawCalls = 0;

        // 子采样
        std::vector<CPUSample>     cpuSamples;
        std::vector<GPUSample>     gpuSamples;
        MemorySample               memorySample;
        PhysicsSample              physicsSample;
    };

    // ============================================================
    // 帧捕获 — 冻结环形缓冲区
    // ============================================================
    struct FrameCapture {
        uint64                captureFrameIndex = 0;
        std::vector<FrameSample> frames;
        bool                  isCapturing = false;
        std::string           label;
    };

    // ============================================================
    // 性能分析器核心（环形缓冲区）
    // ============================================================
    class ProfilerCore {
    public:
        ProfilerCore();
        ~ProfilerCore() = default;

        ProfilerCore(const ProfilerCore&) = delete;
        ProfilerCore& operator=(const ProfilerCore&) = delete;

        // ── 样本记录（由引擎各个子系统调用） ──
        void BeginFrame(uint64 frameIndex);
        void EndFrame();
        void RecordCPUSample(const CPUSample& sample);
        void RecordGPUSample(const GPUSample& sample);
        void RecordMemorySample(const MemorySample& sample);
        void RecordPhysicsSample(const PhysicsSample& sample);

        // ── 查询 ──
        uint32 GetFrameCount() const { return m_FrameCount; }
        const FrameSample* GetFrame(uint32 index) const;
        const FrameSample* GetLastFrame() const;
        std::vector<const FrameSample*> GetRecentFrames(uint32 count) const;

        // ── 帧捕获 ──
        void StartCapture(const std::string& label = "");
        void StopCapture();
        bool IsCapturing() const { return m_Capture.isCapturing; }
        const FrameCapture& GetCapture() const { return m_Capture; }

        // ── 统计（聚合最近 N 帧） ──
        struct AggregatedStats {
            double avgFrameTimeMs  = 0.0;
            double minFrameTimeMs  = 0.0;
            double maxFrameTimeMs  = 0.0;
            double avgCPUms        = 0.0;
            double avgGPUms        = 0.0;
            double avgDrawCalls    = 0.0;
            uint32 totalDrawCalls  = 0;
        };
        AggregatedStats GetAggregatedStats(uint32 frameCount = 60) const;

        // ── 配置 ──
        void SetCapacity(uint32 frames) { m_Capacity = frames; }
        uint32 GetCapacity() const { return m_Capacity; }

        // ── 性能预警 ──
        using WarningCallback = std::function<void(uint64 frameIndex, double frameTimeMs)>;
        void SetWarningThreshold(double ms) { m_WarningThreshold = ms; }
        void SetWarningCallback(WarningCallback cb) { m_WarningCallback = std::move(cb); }

    private:
        // 环形缓冲区
        uint32 m_Capacity = 1024;
        uint32 m_FrameCount = 0;
        uint32 m_Head = 0;
        std::vector<FrameSample> m_Buffer;

        // 当前正在记录的帧
        FrameSample* m_CurrentFrame = nullptr;

        // 帧捕获
        FrameCapture m_Capture;

        // 性能预警
        double m_WarningThreshold = 33.33; // 30fps 阈值
        WarningCallback m_WarningCallback;

        mutable std::mutex m_Mutex;
    };

}} // namespace Engine::Debug