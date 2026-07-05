#pragma once

/**
 * @file JoltJobSystemAdapter.h
 * @brief Jolt Physics ↔ Engine JobSystem 适配器（v5.0 — 对象池版本）
 *
 * 关键设计：
 *   - 预分配 `JPH::FixedSizeFreeList` 管理 Job 生命周期
 *   - 避免 `new`/`delete` 导致的堆内存碎片和全局锁竞争
 *   - Barrier::Wait() 使用 Engine::JobSystem::Wait() 同步
 *
 * 参考实现：Jolt Physics 官方的 JobSystemThreadPool
 */

#include "Engine/Core/JobSystem.h"
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <atomic>
#include <vector>

namespace Engine {

class JoltJobSystemAdapter : public JPH::JobSystem {
public:
    explicit JoltJobSystemAdapter(uint32 maxJobs = 4096);
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
    class BarrierImpl : public JPH::JobSystem::Barrier {
    public:
        BarrierImpl(JoltJobSystemAdapter* adapter, const char* name, uint32 numSubJobs);
        ~BarrierImpl() override;
        void AddJob(const JobHandle* job) override;
        void Wait() override;

    private:
        JoltJobSystemAdapter* m_Adapter;
        std::vector<Engine::JobHandle> m_JobTracker;
    };

    Barrier* CreateBarrier() override;
    void ReleaseBarrier(Barrier* barrier) override;

private:
    // ── 预分配 Job 对象池 ──
    // JPH::FixedSizeFreeList 是 Jolt 内置的无锁空闲链表
    // 我们存储的对象是 JobSlot（包含 Engine::JobHandle 的包装器）
    struct alignas(JPH::JOB_ALIGNMENT) JobSlot {
        JobFunction         function;
        uint32              numDependencies;
        std::atomic<uint32> unfinishedDependencies{0};
        Engine::JobHandle   engineJobHandle;
    };

    JPH::FixedSizeFreeList<JobSlot> m_JobPool;
};

} // namespace Engine