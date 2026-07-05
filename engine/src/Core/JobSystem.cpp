/**
 * @file JobSystem.cpp
 * @brief Job 系统实现 — 线程池 + 任务并行调度 + Per-Worker Deque + 仿射
 *
 * v2 变更：
 *   - Per-Worker 本地双端队列：减少全局锁争用
 *   - WorkerLoop 入口核心绑定（SetThreadAffinity + SetThreadName）
 *   - 工作窃取（TrySteal）遍历 peer workers 的本地队列顶部
 *   - 全局队列作为 fallback（主线程入队时的目标路由）
 */

#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
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

void JobSystem::Init(uint32 threadCount, const ThreadPoolConfig& config) {
    if (s_Instance) {
        s_Log.Warn("Already initialized, skipping.");
        return;
    }

    if (threadCount == 0) {
        uint32 totalCores = Threading::TopologyInfo::GetCoreCount();
        if (totalCores >= 4)
            threadCount = totalCores - 2;  // 留 2 核给 Main + Render
        else if (totalCores <= 2)
            threadCount = 1;
        else
            threadCount = 1;  // 3 核: 1 worker
    }

    s_Instance = new JobSystem(threadCount, config);
    s_Instance->StartWorkers();
    s_Log.Info("Initialized with {} worker threads (base core={})",
               threadCount, config.workerBaseCore);
}

void JobSystem::Shutdown() {
    if (!s_Instance) return;
    s_Log.Info("Shutting down...");
    delete s_Instance;
    s_Instance = nullptr;
    s_Log.Info("Shutdown complete.");
}

JobSystem::JobSystem(uint32 threadCount, const ThreadPoolConfig& config)
    : m_ThreadCount(threadCount)
    , m_Running(true)
    , m_WorkerBaseCore(config.workerBaseCore)
{
    // PerWorkerQueue 含 std::mutex（不可移动/拷贝），用 emplace_back 逐个构造
    for (uint32 i = 0; i < threadCount; ++i)
        m_WorkerQueues.emplace_back();
    m_Workers.reserve(threadCount);
}

void JobSystem::StartWorkers() {
    for (uint32 i = 0; i < m_ThreadCount; ++i) {
        m_Workers.emplace_back(&JobSystem::WorkerLoop, this, i);
    }
}

JobSystem::~JobSystem() {
    StopWorkers();
}

void JobSystem::StopWorkers() {
    m_Running = false;
    m_WakeCondition.notify_all();
    for (auto& t : m_Workers) {
        if (t.joinable()) t.join();
    }
    m_Workers.clear();
}

// ============================================================
// 调度
// ============================================================

JobHandle JobSystem::Schedule(JobFunc&& func,
                              JobHandle dependency,
                              JobPriority priority) {
    uint64 jobId = AllocateJob(std::move(func), dependency, priority);

    if (!dependency.IsValid()) {
        // 就绪 Job → 入队到全局队列（无需指定 target worker）
        EnqueueGlobal(jobId);
    }
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

    // 创建计数 Job
    uint64 counterId = AllocateJob(nullptr, {}, priority);
    auto* counterJob = [&]() -> Job* {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(counterId);
        return (it != m_JobMap.end()) ? it->second.get() : nullptr;
    }();
    if (!counterJob) return JobHandle::Invalid;

    counterJob->unfinishedPrereqs.store(numBatches, std::memory_order_relaxed);

    bool hasDependency = dependency.IsValid();
    if (hasDependency) {
        counterJob->unfinishedPrereqs.fetch_add(1, std::memory_order_relaxed);
    }

    // 派发各批次（分散到不同 worker 的本地队列）
    for (int32 batch = 0; batch < numBatches; ++batch) {
        int32 bBegin = begin + batch * batchSize;
        int32 bEnd   = std::min(bBegin + batchSize, end);

        auto batchFunc = [func, bBegin, bEnd](uint32 threadIndex) {
            for (int32 i = bBegin; i < bEnd; ++i) {
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
            job->dependents.push_back(counterId);
            m_JobMap[id] = std::move(job);
            m_PendingCount.fetch_add(1, std::memory_order_release);
            return id;
        }();

        if (hasDependency) {
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
            // 分散到各 worker 本地队列
            EnqueueToWorker(static_cast<uint32>(batch % m_ThreadCount), batchId);
        }
    }

    if (hasDependency) {
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
    if (IsCompleted(handle)) return;

    while (!IsCompleted(handle)) {
        uint64 stolenId = 0;
        if (TrySteal(UINT32_MAX, stolenId)) {
            ExecuteJob(stolenId, UINT32_MAX);
        } else {
            std::this_thread::yield();
        }
    }
}

bool JobSystem::IsCompleted(JobHandle handle) {
    if (!handle.IsValid()) return true;
    std::lock_guard lock(m_JobMapMutex);
    return m_JobMap.find(handle.id) == m_JobMap.end();
}

void JobSystem::WaitAll() {
    while (m_PendingCount.load(std::memory_order_acquire) > 0) {
        uint64 stolenId = 0;
        if (TrySteal(UINT32_MAX, stolenId)) {
            ExecuteJob(stolenId, UINT32_MAX);
        } else {
            std::this_thread::yield();
        }
    }
}

void JobSystem::PollCompleted() {}

void JobSystem::EndFrame() {}

// ============================================================
// 渲染线程同步
// ============================================================

void JobSystem::SignalRenderSync() {
    auto syncFunc = [](uint32) {};
    m_RenderSyncHandle.id = AllocateJob(std::move(syncFunc), {}, JobPriority::High);
    EnqueueGlobal(m_RenderSyncHandle.id);
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

    if (dependency.IsValid()) {
        job->unfinishedPrereqs.fetch_add(1, std::memory_order_relaxed);
    }

    {
        std::lock_guard lock(m_JobMapMutex);
        m_JobMap[id] = std::move(job);
    }

    if (dependency.IsValid()) {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(dependency.id);
        if (it != m_JobMap.end()) {
            it->second->dependents.push_back(id);
        } else {
            std::lock_guard lock2(m_JobMapMutex);
            auto jt = m_JobMap.find(id);
            if (jt != m_JobMap.end()) {
                if (jt->second->unfinishedPrereqs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    EnqueueGlobal(id);
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
    // 保持向后兼容：委托到全局队列
    EnqueueGlobal(jobId);
}

void JobSystem::EnqueueToWorker(uint32 workerIndex, uint64 jobId) {
    if (workerIndex >= m_ThreadCount) {
        EnqueueGlobal(jobId);
        return;
    }

    auto& queue = m_WorkerQueues[workerIndex];
    {
        std::lock_guard lock(queue.mutex);
        queue.jobs.push_back(jobId);
    }
    queue.approximateSize.fetch_add(1, std::memory_order_release);
    m_WakeCondition.notify_one();
}

void JobSystem::EnqueueGlobal(uint64 jobId) {
    {
        std::lock_guard lock(m_GlobalQueueMutex);
        m_GlobalQueue.push(jobId);
    }
    m_WakeCondition.notify_one();
}

// ============================================================
// 内部：工作线程循环（v2：仿射绑定 + per-worker deque）
// ============================================================

void JobSystem::WorkerLoop(uint32 threadIndex) {
    // ── 1. 核心绑定 ──
    uint32 targetCore = m_WorkerBaseCore + threadIndex;

    Threading::SetCurrentThreadCategory(Threading::ThreadCategory::Worker);
    Threading::SetThreadName(
        ("Engine_Worker_" + std::to_string(threadIndex)).c_str());

    // 获取当前线程原生句柄进行仿射绑定
    // 使用 m_Workers[threadIndex] 获取当前线程的 std::thread 对象，然后取 native_handle
    // 注意：WorkerLoop 运行在 m_Workers[threadIndex] 线程的上下文中
    if (threadIndex < m_Workers.size()) {
        auto nativeHandle = m_Workers[threadIndex].native_handle();
        Threading::SetThreadAffinity(nativeHandle, targetCore);
    }

    // 仅在绑定时设置理想处理器（Windows 特有）
    Threading::SetThreadIdealProcessor(targetCore);

    // ── 2. 主循环：优先本地队列，次选窃取，末选全局队列 ──
    while (m_Running.load(std::memory_order_acquire)) {
        uint64 jobId = 0;
        bool gotJob = false;

        // 尝试出队自己的队列
        gotJob = TryPopLocal(threadIndex, jobId);

        // 自己的队列空了 → 尝试窃取
        if (!gotJob) {
            gotJob = TrySteal(threadIndex, jobId);
        }

        // 窃取失败 → 从全局队列取
        if (!gotJob) {
            {
                std::lock_guard lock(m_GlobalQueueMutex);
                if (!m_GlobalQueue.empty()) {
                    jobId = m_GlobalQueue.front();
                    m_GlobalQueue.pop();
                    gotJob = true;
                }
            }
        }

        if (gotJob) {
            ExecuteJob(jobId, threadIndex);
        } else {
            // 全部队列空 → 条件变量休眠
            std::unique_lock lock(m_GlobalQueueMutex);
            m_WakeCondition.wait_for(lock, std::chrono::milliseconds(1), [this]() {
                return !m_Running.load(std::memory_order_acquire);
            });
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

    if (job->func) {
        job->func(threadIndex);
    }

    OnJobCompleted(jobId);
}

// ============================================================
// 内部：Job 完成处理
// ============================================================

void JobSystem::OnJobCompleted(uint64 jobId) {
    std::vector<uint64> dependents;
    {
        std::lock_guard lock(m_JobMapMutex);
        auto it = m_JobMap.find(jobId);
        if (it == m_JobMap.end()) return;

        dependents = std::move(it->second->dependents);
        m_JobMap.erase(it);
    }

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
            EnqueueGlobal(depId);
        }
    }

    m_PendingCount.fetch_sub(1, std::memory_order_release);
    m_TotalCompleted.fetch_add(1, std::memory_order_release);
}

// ============================================================
// 内部：Per-Worker 操作
// ============================================================

bool JobSystem::TryPopLocal(uint32 workerIndex, uint64& outJobId) {
    if (workerIndex >= m_ThreadCount) return false;

    auto& queue = m_WorkerQueues[workerIndex];
    {
        std::lock_guard lock(queue.mutex);
        if (queue.jobs.empty()) return false;
        outJobId = queue.jobs.front();
        queue.jobs.pop_front();
    }
    queue.approximateSize.fetch_sub(1, std::memory_order_release);
    return true;
}

bool JobSystem::TrySteal(uint32 thiefIndex, uint64& outJobId) {
    if (m_ThreadCount <= 1) return false;

    // 轮询下一个 victim（round-robin + atomic 随机化起点）
    uint32 startVictim = m_NextVictim.fetch_add(1, std::memory_order_relaxed) % m_ThreadCount;

    for (uint32 i = 0; i < m_ThreadCount; ++i) {
        uint32 victim = (startVictim + i) % m_ThreadCount;
        if (victim == thiefIndex) continue;  // 不窃取自己

        auto& queue = m_WorkerQueues[victim];
        {
            std::lock_guard lock(queue.mutex);
            if (queue.jobs.empty()) continue;
            // 从顶部窃取（减少与 victim 的冲突，victim 从 front 出队）
            outJobId = queue.jobs.back();
            queue.jobs.pop_back();
        }
        queue.approximateSize.fetch_sub(1, std::memory_order_release);
        return true;
    }

    return false;
}

} // namespace Engine