#pragma once

/**
 * @file JoltJobSystemAdapter.h
 * @brief Jolt Physics ↔ Engine JobSystem 适配器（v5.5）
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

    int GetMaxConcurrency() const override {
        return static_cast<int>(Engine::JobSystem::Get()->GetThreadCount());
    }

    JobHandle CreateJob(const char* name,
                        JPH::ColorArg color,
                        const JobFunction& jobFunction,
                        uint32 numDependencies = 0) override;

    void FreeJob(Job* inJob) override;
    void QueueJob(Job* inJob) override;
    void QueueJobs(Job** inJobs, uint inNumJobs) override;

    class BarrierImpl : public JPH::JobSystem::Barrier {
    public:
        BarrierImpl(JoltJobSystemAdapter* adapter, const char* name, uint32 numSubJobs);
        ~BarrierImpl() override;
        void AddJob(const JobHandle& inJob) override;
        void AddJobs(const JobHandle* inHandles, uint inNumHandles) override;
        void OnJobFinished(Job* inJob) override {}
        void Wait();
        void WaitForBarrierJobs();

    private:
        JoltJobSystemAdapter* m_Adapter;
        std::vector<Engine::JobHandle> m_JobTracker;
        friend class JoltJobSystemAdapter;
    };

    Barrier* CreateBarrier() override;
    void DestroyBarrier(Barrier* inBarrier) override;
    void WaitForJobs(Barrier* inBarrier) override;

private:
    struct alignas(16) JobSlot {
        JobFunction         function;
        uint32              numDependencies;
        std::atomic<uint32> unfinishedDependencies{0};
        Engine::JobHandle   engineJobHandle;
    };

    JPH::FixedSizeFreeList<JobSlot> m_JobPool;
};

} // namespace Engine