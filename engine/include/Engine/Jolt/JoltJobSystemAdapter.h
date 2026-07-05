#pragma once

/**
 * @file JoltJobSystemAdapter.h
 * @brief Jolt Physics ↔ Engine JobSystem 适配器
 *
 * Jolt 内部要求 JobSystem 提供 QueueJob/CreateBarrier/WaitForJobs 接口。
 * 此适配器将 Jolt 的任务派发到 Engine::JobSystem 的线程池执行。
 *
 * 关键设计：
 *   - Barrier::Wait() 使用 Engine::JobHandle::Wait() 等待所有子任务完成
 *   - 支持 Jolt 的多线程分片（Sub-step 的并行约束求解）
 */

#include "Engine/Core/JobSystem.h"
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <atomic>
#include <vector>

namespace Engine {

class JoltJobSystemAdapter : public JPH::JobSystem {
public:
    explicit JoltJobSystemAdapter(uint32 maxJobs = 1024);
    ~JoltJobSystemAdapter() override;

    // ── JPH::JobSystem 接口 ──

    int GetMaxConcurrency() const override {
        return static_cast<int>(Engine::JobSystem::Get()->GetThreadCount());
    }

    JobHandle* CreateJob(const char* name,
                         JPH::ColorArg color,
                         const JobFunction& jobFunction,
                         uint32 numDependencies = 0) override;

    void FreeJob(JobHandle* job) override;

    void QueueJob(JobHandle* job) override;

    void QueueJobs(JobHandle** jobs, uint32 numJobs) override;

    // ── Barrier 支持 ──
    /**
     * @brief Jolt 的 Barrier：等待一组 Job 全部完成后继续
     *
     * Jolt 在约束求解和多阶段模拟中会创建 Barrier。
     * 适配器通过 Engine::JobSystem::Wait() 实现同步。
     */
    class BarrierImpl : public JPH::JobSystem::Barrier {
    public:
        explicit BarrierImpl(JoltJobSystemAdapter* adapter, uint32 numSubJobs);
        ~BarrierImpl() override;

        void AddJob(const JobHandle* job) override;
        void Wait() override;

    private:
        JoltJobSystemAdapter* m_Adapter;
        std::vector<uint64>   m_JobTrackerIDs;
    };

    Barrier* CreateBarrier() override;
    void ReleaseBarrier(Barrier* barrier) override;

private:
    // ── 内部 Job 存储 ──
    struct alignas(64) JobSlot {
        std::atomic<uint32> state{0};  // 0=free, 1=allocated, 2=queued
        JobFunction         function;
        uint32              numDependencies;
        std::atomic<uint32> unfinishedDependencies{0};
        uint64              engineJobID{0};  // Engine::JobHandle::id

        // 依赖计数完成后回调
        void Execute();
    };

    std::vector<JobSlot> m_JobSlots;
    std::atomic<uint32>  m_NextSlot{0};
};

} // namespace Engine