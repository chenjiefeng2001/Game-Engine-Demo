#pragma once

/**
 * @file GpuTimestampProfiler.h
 * @brief GPU 时间戳分段剖析器 — Phase 0 Benchmark Baseline 工具
 *
 * 用途（docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md · Phase 0）：
 *   在无外部 profiler（NSight/RenderDoc）时，为 compute 管线提供
 *   Dispatch / Execution / Barrier / Sync 的分步 GPU 耗时基线。
 *
 * 语义约定：
 *   - BeginSegment/EndSegment 必须在命令录制期调用（cmd->Begin() 与 End() 之间）
 *   - Resolve() 必须在 GPU 完成对应命令之后调用（WaitIdle / fence signaled）
 *   - 后端不支持时 IsAvailable() 返回 false，所有接口安全降级为 no-op
 */

#include <memory>
#include <string>
#include <vector>

#include "Engine/Types.h"

namespace Engine {
namespace RHI {

    class IRHIDevice;
    class IRHICommandList;

    class IGpuTimestampProfiler {
    public:
        virtual ~IGpuTimestampProfiler() = default;

        /**
         * @param maxSegments 最大并发分段数（每段消耗 2 个查询槽位）
         * @return false 表示后端不支持时间戳查询（如驱动无 GL_TIMESTAMP 计数位）
         */
        virtual bool Initialize(IRHIDevice* device, uint32_t maxSegments) = 0;
        virtual void Shutdown() = 0;

        /** 清空本帧分段收集（不影响查询对象生命周期） */
        virtual void BeginFrame() = 0;

        /** 开始一个命名分段，返回分段 id；失败返回 UINT32_MAX */
        virtual uint32_t BeginSegment(IRHICommandList& cmd, const char* name) = 0;

        /** 结束分段 */
        virtual void EndSegment(IRHICommandList& cmd, uint32_t segmentId) = 0;

        struct SegmentResult {
            std::string name;
            double gpuMs = 0.0;   ///< 该分段起止时间戳的 GPU 时间差
        };

        /**
         * @brief 解析自上次 ResetTimestamps 以来的全部分段
         * @note  调用前必须保证 GPU 已完成；结果顺序与 BeginSegment 一致
         */
        virtual const std::vector<SegmentResult>& Resolve() = 0;

        virtual bool IsAvailable() const = 0;
    };

    /** 创建后端对应的剖析器实例（当前仅 GL46 实现） */
    std::unique_ptr<IGpuTimestampProfiler> CreateGpuTimestampProfiler();

} // namespace RHI
} // namespace Engine
