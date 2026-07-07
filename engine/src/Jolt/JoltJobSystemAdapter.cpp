/**
 * @file JoltJobSystemAdapter.cpp
 * @brief Jolt ↔ Engine JobSystem 适配器实现（v6.1）
 *
 * 继承 JPH::Job 以正确支持 Ref<Job> 引用计数生命周期管理。
 * 使用 new/delete 分配。
 */

#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include <thread>

namespace Engine {

JoltJobSystemAdapter::JoltJobSystemAdapter(uint32 maxJobs)
    : m_JobPool()
{
    m_JobPool.Init(maxJobs, maxJobs);
}

JoltJobSystemAdapter::~JoltJobSystemAdapter() = default;

JPH::JobSystem::JobHandle JoltJobSystemAdapter::CreateJob(
    const char* name, JPH::ColorArg color,
    const JobFunction& jobFunction,
    uint32 numDependencies)
{
    EngineJob* engineJob = new EngineJob(name, color, this, jobFunction, numDependencies);
    return JobHandle(engineJob);
}

void JoltJobSystemAdapter::FreeJob(Job* inJob) {
    if (!inJob) return;
    delete inJob;
}

void JoltJobSystemAdapter::QueueJob(Job* inJob) {
    if (!inJob) return;

    EngineJob* engineJob = static_cast<EngineJob*>(inJob);
    auto* js = Engine::JobSystem::Get();
    if (js) {
        // 增加引用，确保执行期间不会被销毁
        inJob->AddRef();
        js->Schedule([engineJob](uint32_t) {
            engineJob->Execute();
            engineJob->Release();
        });
    } else {
        engineJob->Execute();
    }
}

void JoltJobSystemAdapter::QueueJobs(Job** inJobs, uint inNumJobs) {
    for (uint i = 0; i < inNumJobs; ++i)
        QueueJob(inJobs[i]);
}

// ── BarrierImpl ──

JoltJobSystemAdapter::BarrierImpl::BarrierImpl(
    JoltJobSystemAdapter* adapter,
    const char*,
    uint32 numSubJobs)
    : JPH::JobSystem::Barrier()
    , m_Adapter(adapter)
{
    m_JobTracker.reserve(numSubJobs);
}

JoltJobSystemAdapter::BarrierImpl::~BarrierImpl() = default;

void JoltJobSystemAdapter::BarrierImpl::AddJob(const JobHandle& inJob) {
    // 通过 JobHandle::RemoveDependency 减少依赖计数
    // 如果归零则自动触发 QueueJob（由 JPH::Job 内部机制处理）
    inJob.RemoveDependency(1);

    m_JobTracker.push_back(inJob.GetPtr());
}

void JoltJobSystemAdapter::BarrierImpl::AddJobs(const JobHandle* inHandles, uint inNumHandles) {
    for (uint i = 0; i < inNumHandles; ++i)
        AddJob(inHandles[i]);
}

void JoltJobSystemAdapter::BarrierImpl::Wait() {
    for (Job* job : m_JobTracker) {
        if (job) {
            while (!job->IsDone()) {
                std::this_thread::yield();
            }
        }
    }
    m_JobTracker.clear();
}

void JoltJobSystemAdapter::BarrierImpl::WaitForBarrierJobs() {
    Wait();
}

JPH::JobSystem::Barrier* JoltJobSystemAdapter::CreateBarrier() {
    return new BarrierImpl(this, "PhysicsBarrier", 64);
}

void JoltJobSystemAdapter::DestroyBarrier(Barrier* inBarrier) {
    delete static_cast<BarrierImpl*>(inBarrier);
}

void JoltJobSystemAdapter::WaitForJobs(Barrier* inBarrier) {
    static_cast<BarrierImpl*>(inBarrier)->WaitForBarrierJobs();
}

} // namespace Engine