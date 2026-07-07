#pragma once

/**
 * @file JoltJobSystemAdapter.h
 * @brief Jolt Physics ↔ Engine JobSystem 适配器（v6.0）
 *
 * 修复：JPH::JobSystem::Job 内部使用 Ref<Job>（引用计数）管理生命周期。
 * JobSlot 必须继承 Job 才能被 JobHandle 正确 AddRef/Release。
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
        auto* js = Engine::JobSystem::Get();
        if (js) {
            return static_cast<int>(js->GetThreadCount());
        }
        return 1;
    }

    JobHandle CreateJob(const char* name,
                        JPH::ColorArg color,
                        const JobFunction& jobFunction,
                        uint32 numDependencies = 0) override;

    void FreeJob(Job* inJob) override;
    void QueueJob(Job* inJob) override;
    void QueueJobs(Job** inJobs, uint inNumJobs) override;

    /// 继承 JPH::JobSystem::Job 的自定义 Job
    /// JPH::Job 内部持有 mReferenceCount, mJobSystem, mJobFunction, mNumDependencies
    /// 我们只需在 Execute() 中转发到自己的调度器
    class EngineJob : public Job {
    public:
        EngineJob(const char* inName, JPH::ColorArg inColor,
                  JoltJobSystemAdapter* inAdapter,
                  const JobFunction& inFunction,
                  uint32 inNumDependencies)
            : Job(inName, inColor, inAdapter, inFunction, inNumDependencies)
            , m_Adapter(inAdapter)
        {
        }

        JoltJobSystemAdapter* GetAdapter() const { return m_Adapter; }

    private:
        JoltJobSystemAdapter* m_Adapter;
    };

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
        std::vector<JPH::JobSystem::Job*> m_JobTracker;
        friend class JoltJobSystemAdapter;
    };

    Barrier* CreateBarrier() override;
    void DestroyBarrier(Barrier* inBarrier) override;
    void WaitForJobs(Barrier* inBarrier) override;

private:
    /// 固定大小的空闲列表，存储 EngineJob 对象
    JPH::FixedSizeFreeList<EngineJob> m_JobPool;
};

} // namespace Engine