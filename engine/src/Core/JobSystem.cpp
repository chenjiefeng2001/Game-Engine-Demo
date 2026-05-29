/**
 * @file JobSystem.cpp
 * @brief Job 系统实现 — 线程池 + 任务并行调度
 *
 * 实现要点：
 *   - 工作线程通过条件变量等待任务，无任务时休眠，零 CPU 占用
 *   - 主线程 Wait() 时执行工作窃取，避免死锁
 *   - ParallelFor 自动分片，每片作为独立 Job 派发
 *   - 所有 Job 数据通过 std::unordered_map 按 ID 索引，O(1) 查找
 */

#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <queue>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <algorithm>

namespace Engine {

namespace {
    Logger s_Log("JobSystem");
}

// ============================================================
// 静态成员
// ============================================================

JobSystem* JobSystem::s_Instance = nullptr;
const JobHandle JobHandle::Invalid{ 0 };

// ============================================================
// 单例生命周期
// ============================================================

void JobSystem::Init(uint32 threadCount, uint32 reservedForRender) {
    if (s_Instance) {
        s_Log.Warn("Already initialized, skipping.");
        return;
    }

    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount < 1) threadCount = 1;
        // 如果只预留了渲染核心且总核心>1，为游戏逻辑保留至少1核
        if (reservedForRender > 0 && threadCount > reservedForRender + 1) {
            threadCount -= reservedForRender;
        }
    }

    s_Instance = new JobSystem(threadCount, reservedForRender);
    s_Log.Info("Initialized with {} worker threads", threadCount);
}

void JobSystem::Shutdown() {
    if (!s_Instance) return;
    s_Log.Info("Shutting down...");
    delete s_Instance;
    s_Instance = nullptr;
    s_Log.Info("Shutdown complete.");
}

JobSystem::JobSystem(uint32 threadCount, uint32 reservedForRender)
    : m_ThreadCount(threadCount)
    , m_Running(true)
    , m_ReservedForRender(reservedForRender)
{
    m_Workers.reserve(threadCount);
    for (uint32 i = 0; i < threadCount; ++i) {
        m_Workers.emplace_back(&JobSystem::WorkerLoop, this, i);
    }
}

JobSystem::~JobSystem() {
    m_Running = false;
    m_WakeCondition.notify_all();
    for (auto& t : m_Workers) {
        if (t.joinable()) t.join();
    }
}

// ============================================================
// 调度
// ============================================================

JobHandle JobSystem::Schedule(JobFunc&& func,
                              JobHandle dependency,
                              JobPriority priority) {
    uint64 jobId = AllocateJob(std::move(func), dependency, priority);

    if (!dependency.IsValid()) {
        // 无依赖 → 立即入队
        EnqueueJob(jobId);
    }
    // 有依赖 → 等依赖完成时自动入队（由 OnJobCompleted 处理）

    return JobHandle{ jobId };
}

JobHandle JobSystem::ParallelFor(int32 begin, int32 end,
                                 ParallelForFunc&& func,
                                 JobHandle dependency,
                                 JobPriority priority) {
    if (begin >= end) return JobHandle::Invalid;

    int32 count      = end - begin;
    int32 batchSize  = std::max<int32>(1, (count + static_cast<int32>(m_ThreadCount) - 1) / static_cast<int32>(m_ThreadCount));
    int32 numBatches = (count + batchSize - 1) / batchSize;

    // ── 创建一个计数 Job，等所有批次完成后它才标记完成 ──
    uint64 counterId = AllocateJob(nullptr, {}, priority);
    auto* counterJob = [&]() -> Job* {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(counterId);
        return (it != m_JobMap.end()) ? it->second.get() : nullptr;
    }();
    if (!counterJob) return JobHandle::Invalid;

    counterJob->unfinishedPrereqs.store(numBatches, std::memory_order_relaxed);

    // ── 检查依赖 ──
    bool hasDependency = dependency.IsValid();
    if (hasDependency) {
        counterJob->unfinishedPrereqs.fetch_add(1, std::memory_order_relaxed);
    }

    // ── 派发各批次 ──
    for (int32 batch = 0; batch < numBatches; ++batch) {
        int32 batchBegin = begin + batch * batchSize;
        int32 batchEnd   = std::min(batchBegin + batchSize, end);

        auto batchFunc = [func, batchBegin, batchEnd](uint32 threadIndex) {
            for (int32 i = batchBegin; i < batchEnd; ++i) {
                func(i);
            }
        };

        uint64 batchId = [&]() -> uint64 {
            std::lock_guard lock(m_JobMapMutex);
            uint64 id = m_NextJobId++;
            auto job = std::make_unique<Job>();
            job->id       = id;
            job->func     = std::move(batchFunc);
            job->priority = priority;
            // 批次完成后通知 counterJob
            job->dependents.push_back(counterId);
            m_JobMap[id] = std::move(job);
            m_PendingCount.fetch_add(1, std::memory_order_release);  // AllocateJob 也会增加
            return id;
        }();

        if (hasDependency) {
            // 批次 Job 依赖外部的 dependency
            auto depJob = [&]() -> Job* {
                std::lock_guard lock(m_JobMapMutex);
                auto it = m_JobMap.find(dependency.id);
                return (it != m_JobMap.end()) ? it->second.get() : nullptr;
            }();
            if (depJob) {
                std::lock_guard lock(m_JobMapMutex);
                depJob->dependents.push_back(batchId);
            }
        }

        if (!hasDependency) {
            EnqueueJob(batchId);
        }
    }

    // 如果有外部依赖，计数 Job 的 prereqs 需要等依赖完成
    if (hasDependency) {
        // 让 dependency 完成后通知 counterJob
        auto depJob = [&]() -> Job* {
            std::lock_guard lock(m_JobMapMutex);
            auto it = m_JobMap.find(dependency.id);
            return (it != m_JobMap.end()) ? it->second.get() : nullptr;
        }();
        if (depJob) {
            std::lock_guard lock(m_JobMapMutex);
            depJob->dependents.push_back(counterId);
        }
    }

    return JobHandle{ counterId };
}

// ============================================================
// 同步
// ============================================================

void JobSystem::Wait(JobHandle handle) {
    if (!handle.IsValid()) return;

    // 快速路径：如果已经完成，直接返回
    if (IsCompleted(handle)) return;

    // 检查当前是否在工作线程中
    // 工作线程 ID 检测的简化方式：检查是否在 m_Workers 中
    // 但由于无法直接获取当前线程 ID 比较，我们统一使用 work stealing
    // 主线程执行待处理 Job，工作线程忙等

    while (!IsCompleted(handle)) {
        // 尝试窃取一个 Job 在主线程执行
        uint64 stolenId = 0;
        if (TryStealJob(stolenId)) {
            ExecuteJob(stolenId, UINT32_MAX);  // UINT32_MAX 表示主线程
        } else {
            // 没有可窃取的 Job → 让出 CPU 时间片
            std::this_thread::yield();
        }
    }
}

bool JobSystem::IsCompleted(JobHandle handle) {
    if (!handle.IsValid()) return true;

    // 如果 Job 不在 ）map 中，说明已被 OnJobCompleted 清理 → 完成
    // 如果 Job 在 map 中，说明尚未执行完毕（等待依赖 / 在队列中 / 正在执行
    std::lock_guard lock(m_JobMapMutex);
    return m_JobMap.find(handle.id) == m_JobMap.end();
}

void JobSystem::WaitAll() {
    while (m_PendingCount.load(std::memory_order_acquire) > 0) {
        uint64 stolenId = 0;
        if (TryStealJob(stolenId)) {
            ExecuteJob(stolenId, UINT32_MAX);
        } else {
            std::this_thread::yield();
        }
    }
}

void JobSystem::PollCompleted() {
    // 预留：未来可在此处理需要在主线程执行的完成回调
    // 当前无操作
}

// ============================================================
// 渲染线程同步（预留接口）
// ============================================================

void JobSystem::SignalRenderSync() {
    // 排入一个空 Job，用于渲染线程同步
    auto syncFunc = [](uint32) { /* 渲染同步标记 */ };
    m_RenderSyncHandle.id = AllocateJob(std::move(syncFunc), {}, JobPriority::High);
    EnqueueJob(m_RenderSyncHandle.id);
}

// ============================================================
// 统计
// ============================================================

uint32 JobSystem::GetPendingJobCount() const {
    return m_PendingCount.load(std::memory_order_acquire);
}

// ============================================================
// 内部：分配 Job
// ============================================================

uint64 JobSystem::AllocateJob(JobFunc&& func,
                              JobHandle dependency,
                              JobPriority priority) {
    uint64 id = m_NextJobId.fetch_add(1, std::memory_order_relaxed);

    auto job = std::make_unique<Job>();
    job->id       = id;
    job->func     = std::move(func);
    job->priority = priority;
    job->unfinishedPrereqs.store(0, std::memory_order_relaxed);

    // 处理依赖
    if (dependency.IsValid()) {
        job->unfinishedPrereqs.fetch_add(1, std::memory_order_relaxed);
    }

    {
        std::lock_guard lock(m_JobMapMutex);
        m_JobMap[id] = std::move(job);
    }

    // 注册到依赖 Job 的 dependent 列表中
    if (dependency.IsValid()) {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(dependency.id);
        if (it != m_JobMap.end()) {
            it->second->dependents.push_back(id);
        } else {
            // 依赖 Job 已消失（已完成并清理）→ 立即减掉 prereq
            std::lock_guard lock2(m_JobMapMutex);
            auto jt = m_JobMap.find(id);
            if (jt != m_JobMap.end()) {
                if (jt->second->unfinishedPrereqs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    // prereq 降为 0，直接入队
                    EnqueueJob(id);
                }
            }
        }
    }

    m_PendingCount.fetch_add(1, std::memory_order_release);
    return id;
}

// ============================================================
// 内部：入队
// ============================================================

void JobSystem::EnqueueJob(uint64 jobId) {
    {
        std::lock_guard lock(m_QueueMutex);
        m_JobQueue.push(jobId);
    }
    m_WakeCondition.notify_one();
}

// ============================================================
// 内部：工作线程循环
// ============================================================

void JobSystem::WorkerLoop(uint32 threadIndex) {
    while (m_Running.load(std::memory_order_acquire)) {
        uint64 jobId = 0;

        {
            std::unique_lock lock(m_QueueMutex);
            m_WakeCondition.wait(lock, [this]() {
                return !m_JobQueue.empty() || !m_Running.load(std::memory_order_acquire);
            });

            if (!m_Running.load(std::memory_order_acquire)) break;

            if (!m_JobQueue.empty()) {
                jobId = m_JobQueue.front();
                m_JobQueue.pop();
            }
        }

        if (jobId != 0) {
            ExecuteJob(jobId, threadIndex);
        }
    }
}

// ============================================================
// 内部：执行 Job
// ============================================================

void JobSystem::ExecuteJob(uint64 jobId, uint32 threadIndex) {
    Job* job = nullptr;
    {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(jobId);
        if (it == m_JobMap.end()) return;
        job = it->second.get();
    }

    // 执行
    if (job->func) {
        job->func(threadIndex);
    }

    OnJobCompleted(jobId);
}

// ============================================================
// 内部：Job 完成处理
// ============================================================

void JobSystem::OnJobCompleted(uint64 jobId) {
    // 收集依赖者列表（在锁外修改前获取一份拷贝）
    std::vector<uint64> dependents;
    {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(jobId);
        if (it == m_JobMap.end()) return;

        dependents = std::move(it->second->dependents);
        // 清理已完成的 Job
        m_JobMap.erase(it);
    }

    // 递减每个依赖者的 prereq 计数
    for (uint64 depId : dependents) {
        bool shouldEnqueue = false;
        {
            std::lock_guard lock(m_JobMapMutex);
            auto it = m_JobMap.find(depId);
            if (it != m_JobMap.end()) {
                if (it->second->unfinishedPrereqs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    shouldEnqueue = true;
                }
            }
        }
        if (shouldEnqueue) {
            EnqueueJob(depId);
        }
    }

    m_PendingCount.fetch_sub(1, std::memory_order_release);
    m_TotalCompleted.fetch_add(1, std::memory_order_release);
}

// ============================================================
// 内部：工作窃取
// ============================================================

bool JobSystem::TryStealJob(uint64& outJobId) {
    std::lock_guard lock(m_QueueMutex);
    if (m_JobQueue.empty()) return false;

    outJobId = m_JobQueue.front();
    m_JobQueue.pop();
    return true;
}

} // namespace Engine
