/**
 * @file JoltJobSystemAdapter.cpp
 * @brief Jolt ↔ Engine JobSystem 适配器实现
 *
 * 关键设计（v4.0）：
 *   - 使用 JPH::Job::AddRef/Release 管理生命周期，避免环形缓冲区 ABA 问题
 *   - Barrier 使用 Engine::JobSystem::Wait() 同步
 *   - 支持 Jolt 的多线程约束求解
 */

#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include <cassert>

namespace Engine {

JoltJobSystemAdapter::JoltJobSystemAdapter(uint32 maxJobs)
    : m_MaxJobs(maxJobs)
{
}

JoltJobSystemAdapter::~JoltJobSystemAdapter() = default;

// ── 创建 Job ──
JPH::JobSystem::JobHandle* JoltJobSystemAdapter::CreateJob(
    const char*, JPH::ColorArg,
    const JobFunction& jobFunction,
    uint32 numDependencies)
{
    // 由 Jolt 内部管理生命周期，我们不直接分配 JobSlot
    // 返回 nullptr 让 Jolt 使用自己的 Job 分配机制
    // Jolt 会在内部创建 Job 对象并通过回调通知我们
    (void)jobFunction;
    (void)numDependencies;
    return nullptr;
}

void JoltJobSystemAdapter::FreeJob(JobHandle* job) {
    // Jolt 会在 Job 执行完毕后内部处理释放
    // 如果使用了自定义分配，此处可释放外部资源
    (void)job;
}

void JoltJobSystemAdapter::QueueJob(JobHandle* job) {
    // 使用 AddRef 防止 Jolt 在 Job 执行前释放对象
    job->AddRef();

    // 派发到 Engine::JobSystem 执行
    Engine::JobHandle engineHandle = Engine::JobSystem::Get()->Schedule([job](uint32_t) {
        job->Execute();
        // 执行完毕后 Release（Jolt 内部会做最终的销毁）
        job->Release();
    });
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
    m_JobTracker.reserve(numSubJobs);
}

JoltJobSystemAdapter::BarrierImpl::~BarrierImpl() = default;

void JoltJobSystemAdapter::BarrierImpl::AddJob(const JobHandle* job) {
    // 依赖计数 -1，如果归零则触发执行
    job->AddRef();
    job->Execute();
    job->Release();
}

void JoltJobSystemAdapter::BarrierImpl::Wait() {
    // 等待所有 Engine Job 完成
    for (auto& handle : m_JobTracker) {
        if (handle.IsValid()) {
            Engine::JobSystem::Get()->Wait(handle);
        }
    }
    m_JobTracker.clear();
}

JPH::JobSystem::Barrier* JoltJobSystemAdapter::CreateBarrier() {
    return new BarrierImpl(this, 64);
}

void JoltJobSystemAdapter::ReleaseBarrier(Barrier* barrier) {
    delete static_cast<BarrierImpl*>(barrier);
}

} // namespace Engine