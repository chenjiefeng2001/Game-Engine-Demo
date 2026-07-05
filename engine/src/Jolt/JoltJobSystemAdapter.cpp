/**
 * @file JoltJobSystemAdapter.cpp
 * @brief Jolt ↔ Engine JobSystem 适配器实现
 */

#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include <cassert>

namespace Engine {

JoltJobSystemAdapter::JoltJobSystemAdapter(uint32 maxJobs)
    : m_JobSlots(maxJobs)
{
}

JoltJobSystemAdapter::~JoltJobSystemAdapter() = default;

// ── 创建 Job ──
JPH::JobSystem::JobHandle* JoltJobSystemAdapter::CreateJob(
    const char*, JPH::ColorArg,
    const JobFunction& jobFunction,
    uint32 numDependencies)
{
    // 找一个空闲槽
    uint32 slot = m_NextSlot.fetch_add(1, std::memory_order_relaxed);
    slot %= static_cast<uint32>(m_JobSlots.size());

    JobSlot& js = m_JobSlots[slot];
    js.state.store(1, std::memory_order_release);  // allocated
    js.function = jobFunction;
    js.numDependencies = numDependencies;
    js.unfinishedDependencies.store(numDependencies, std::memory_order_relaxed);
    js.engineJobID = 0;

    return reinterpret_cast<JobHandle*>(&js);
}

void JoltJobSystemAdapter::FreeJob(JobHandle* job) {
    auto* js = reinterpret_cast<JobSlot*>(job);
    js->state.store(0, std::memory_order_release);  // free
}

void JoltJobSystemAdapter::QueueJob(JobHandle* job) {
    auto* js = reinterpret_cast<JobSlot*>(job);

    // 检查依赖是否已满足
    if (js->unfinishedDependencies.load(std::memory_order_acquire) > 0) {
        js->state.store(2, std::memory_order_release);  // queued, waiting
        return;
    }

    // 通过 Engine::JobSystem 调度
    js->state.store(3, std::memory_order_release);  // running
    auto handle = Engine::JobSystem::Get()->Schedule([js](uint32_t) {
        js->Execute();
    });
    js->engineJobID = handle.id;
}

void JoltJobSystemAdapter::QueueJobs(JobHandle** jobs, uint32 numJobs) {
    for (uint32 i = 0; i < numJobs; ++i) {
        QueueJob(jobs[i]);
    }
}

// ── Barrier 实现 ──

JoltJobSystemAdapter::BarrierImpl::BarrierImpl(
    JoltJobSystemAdapter* adapter, uint32 numSubJobs)
    : JPH::JobSystem::Barrier()
    , m_Adapter(adapter)
{
    m_JobTrackerIDs.reserve(numSubJobs);
}

JoltJobSystemAdapter::BarrierImpl::~BarrierImpl() = default;

void JoltJobSystemAdapter::BarrierImpl::AddJob(const JobHandle* job) {
    auto* js = reinterpret_cast<JobSlot*>(job);
    m_JobTrackerIDs.push_back(js->engineJobID);

    // 依赖计数 -1，如果归零则触发执行
    if (js->numDependencies > 0) {
        uint32 prev = js->unfinishedDependencies.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            // 所有依赖已满足
            m_Adapter->QueueJob(const_cast<JobHandle*>(job));
        }
    }
}

void JoltJobSystemAdapter::BarrierImpl::Wait() {
    // 等待所有 Engine Job 完成
    for (auto id : m_JobTrackerIDs) {
        if (id != 0) {
            Engine::JobSystem::Get()->Wait(JobHandle{id});
        }
    }
    m_JobTrackerIDs.clear();
}

JPH::JobSystem::Barrier* JoltJobSystemAdapter::CreateBarrier() {
    return new BarrierImpl(this, 64);
}

void JoltJobSystemAdapter::ReleaseBarrier(Barrier* barrier) {
    delete static_cast<BarrierImpl*>(barrier);
}

// ── JobSlot::Execute ──
void JoltJobSystemAdapter::JobSlot::Execute() {
    // 检查依赖是否全部完成
    if (unfinishedDependencies.load(std::memory_order_acquire) > 0) {
        // 不能执行，等待
        return;
    }
    // 执行 Jolt 的函数
    function();
}

} // namespace Engine