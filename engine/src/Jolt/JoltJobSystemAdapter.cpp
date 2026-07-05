/**
 * @file JoltJobSystemAdapter.cpp
 * @brief Jolt ↔ Engine JobSystem 适配器实现（v5.0 — 对象池）
 *
 * 核心机制：
 *   - 使用 JPH::FixedSizeFreeList 预分配 JobSlot，避免堆碎片
 *   - CreateJob 从对象池取出空闲槽，FreeJob 归还
 *   - QueueJob 使用 job->AddRef() + Lambda Capture 确保生命周期
 *   - Barrier 通过 Engine::JobSystem::Wait() 同步所有子任务
 */

#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include <cassert>

namespace Engine {

JoltJobSystemAdapter::JoltJobSystemAdapter(uint32 maxJobs)
    : m_JobPool()
{
    // 初始化固定大小空闲链表：预分配 maxJobs 个 JobSlot
    m_JobPool.Init(maxJobs, maxJobs);
}

JoltJobSystemAdapter::~JoltJobSystemAdapter() = default;

// ── 创建 Job：从对象池分配 ──
JPH::JobSystem::JobHandle* JoltJobSystemAdapter::CreateJob(
    const char*, JPH::ColorArg,
    const JobFunction& jobFunction,
    uint32 numDependencies)
{
    // 从空闲链表取一个 JobSlot
    JobSlot* slot = m_JobPool.Get();
    if (!slot) {
        // 对象池满 — 生产级：阻塞等待或扩展池大小
        // 当前实现：返回 nullptr（Jolt 会自行处理此情况）
        return nullptr;
    }

    slot->function = jobFunction;
    slot->numDependencies = numDependencies;
    slot->unfinishedDependencies.store(numDependencies, std::memory_order_relaxed);
    slot->engineJobHandle = Engine::JobHandle{};

    return reinterpret_cast<JobHandle*>(slot);
}

// ── 释放 Job：归还到对象池 ──
void JoltJobSystemAdapter::FreeJob(JobHandle* job) {
    if (!job) return;
    JobSlot* slot = reinterpret_cast<JobSlot*>(job);
    m_JobPool.Free(slot);
}

// ── 入队 Job：派发到 Engine::JobSystem ──
void JoltJobSystemAdapter::QueueJob(JobHandle* job) {
    if (!job) return;
    JobSlot* slot = reinterpret_cast<JobSlot*>(job);

    // 检查是否还有未完成的依赖
    if (slot->unfinishedDependencies.load(std::memory_order_acquire) > 0) {
        return;  // 依赖未满足，等待 Barrier 通知
    }

    // 通过 AddRef 防止 Jolt 在 Job 执行前释放整个 Job 系统
    job->AddRef();

    // 派发到 Engine::JobSystem 执行
    auto handle = Engine::JobSystem::Get()->Schedule([job, slot](uint32_t) {
        // 执行 Jolt 的 Job 函数
        slot->function();

        // 执行完毕后 Release（归还引用计数）
        job->Release();
    });

    slot->engineJobHandle = handle;
}

// ── 批量入队 ──
void JoltJobSystemAdapter::QueueJobs(JobHandle** jobs, uint32 numJobs) {
    for (uint32 i = 0; i < numJobs; ++i) {
        QueueJob(jobs[i]);
    }
}

// ── Barrier 实现 ──

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

void JoltJobSystemAdapter::BarrierImpl::AddJob(const JobHandle* job) {
    JobSlot* slot = reinterpret_cast<JobSlot*>(job);

    // 依赖计数 -1，如果归零则触发执行
    uint32 prev = slot->unfinishedDependencies.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        // 所有依赖已满足
        m_Adapter->QueueJob(const_cast<JobHandle*>(job));
    }

    // 记录到 tracker 中等待
    m_JobTracker.push_back(slot->engineJobHandle);
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
    return new BarrierImpl(this, "PhysicsBarrier", 64);
}

void JoltJobSystemAdapter::ReleaseBarrier(Barrier* barrier) {
    delete static_cast<BarrierImpl*>(barrier);
}

} // namespace Engine