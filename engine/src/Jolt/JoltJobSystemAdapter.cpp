/**
 * @file JoltJobSystemAdapter.cpp
 * @brief Jolt ↔ Engine JobSystem 适配器实现（v5.5）
 */

#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include <cassert>

namespace Engine {

JoltJobSystemAdapter::JoltJobSystemAdapter(uint32 maxJobs)
    : m_JobPool()
{
    m_JobPool.Init(maxJobs, maxJobs);
}

JoltJobSystemAdapter::~JoltJobSystemAdapter() = default;

JPH::JobSystem::JobHandle JoltJobSystemAdapter::CreateJob(
    const char*, JPH::ColorArg,
    const JobFunction& jobFunction,
    uint32 numDependencies)
{
    auto* slot = new JobSlot();
    slot->function = jobFunction;
    slot->numDependencies = numDependencies;
    slot->unfinishedDependencies.store(numDependencies, std::memory_order_relaxed);
    slot->engineJobHandle = Engine::JobHandle{};

    return JobHandle(reinterpret_cast<Job*>(slot));
}

void JoltJobSystemAdapter::FreeJob(Job* inJob) {
    delete reinterpret_cast<JobSlot*>(inJob);
}

void JoltJobSystemAdapter::QueueJob(Job* inJob) {
    if (!inJob) return;
    JobSlot* slot = reinterpret_cast<JobSlot*>(inJob);

    if (slot->unfinishedDependencies.load(std::memory_order_acquire) > 0)
        return;

    auto* js = Engine::JobSystem::Get();
    if (js) {
        auto handle = js->Schedule([slot](uint32_t) {
            slot->function();
        });
        slot->engineJobHandle = handle;
    } else {
        // Fallback: execute synchronously if no JobSystem
        slot->function();
    }
}

void JoltJobSystemAdapter::QueueJobs(Job** inJobs, uint inNumJobs) {
    for (uint i = 0; i < inNumJobs; ++i)
        QueueJob(inJobs[i]);
}

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
    JobSlot* slot = reinterpret_cast<JobSlot*>(inJob.GetPtr());

    uint32 prev = slot->unfinishedDependencies.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
        m_Adapter->QueueJob(inJob.GetPtr());

    m_JobTracker.push_back(slot->engineJobHandle);
}

void JoltJobSystemAdapter::BarrierImpl::AddJobs(const JobHandle* inHandles, uint inNumHandles) {
    for (uint i = 0; i < inNumHandles; ++i)
        AddJob(inHandles[i]);
}

void JoltJobSystemAdapter::BarrierImpl::Wait() {
    for (auto& handle : m_JobTracker) {
        if (handle.IsValid()) {
            auto* js = Engine::JobSystem::Get();
            if (js) js->Wait(handle);
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