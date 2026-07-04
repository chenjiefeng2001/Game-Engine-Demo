#pragma once

/**
 * @file JobSystem.h
 * @brief 通用 Job 系统 — 线程池 + 任务级并行调度 + Per-Worker Deque + 仿射
 *
 * 设计目标：
 *   1. 提供简单高效的线程池，将可并行的任务分散到多核执行
 *   2. 支持单 Job、ParallelFor 两种调度模式
 *   3. 主线程 Wait() 时执行工作窃取（work stealing），避免死锁
 *   4. Per-worker 本地队列：减少锁争用，提升高并发吞吐量
 *   5. 线程核心绑定（仿射）：Windows/Linux/macOS 多平台
 *
 * 使用方式：
 * @code
 *   // 初始化（Application 启动时调用一次）
 *   JobSystem::Init(0);  // 0 = 自动检测核心数
 *
 *   // ParallelFor — 并行遍历 [0, count)
 *   JobHandle handle = JobSystem::Get()->ParallelFor(0, particleCount,
 *       [&](int32 i) { m_Particles[i].Update(dt); }
 *   );
 *
 *   // 做其他串行工作...
 *   m_PhysicsWorld->Step(dt);
 *
 *   // 等待粒子更新完成
 *   JobSystem::Get()->Wait(handle);
 *
 *   // 销毁
 *   JobSystem::Shutdown();
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/ThreadAffinity.h"
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

namespace Engine {

// ============================================================
// 线程池配置（必须在 JobSystem 类之前定义，供默认参数使用）
// ============================================================

struct ThreadPoolConfig {
    uint32  mainThreadCore   = 0;   ///< 主线程绑定的逻辑核（0-based）
    uint32  renderThreadCore = 1;   ///< 渲染线程核心
    uint32  workerBaseCore   = 2;   ///< 工作线程起始核心
    uint32  ioThreadCore     = 0;   ///< IO 线程核心（0 = 不绑定）

    ThreadPoolConfig() {
        // 从 ThreadAffinity 获取推荐配置
        mainThreadCore   = Threading::GetRecommendedCore(Threading::ThreadCategory::Main, 0);
        renderThreadCore = Threading::GetRecommendedCore(Threading::ThreadCategory::Render, 0);
        workerBaseCore   = Threading::GetRecommendedCore(Threading::ThreadCategory::Worker, 0);
        ioThreadCore     = Threading::GetRecommendedCore(Threading::ThreadCategory::IO, 0);
    }
};

// ============================================================
// Job 句柄 — 不透明 ID，用于等待/查询
// ============================================================

struct JobHandle {
    uint64 id = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const JobHandle& other) const { return id == other.id; }
    bool operator!=(const JobHandle& other) const { return id != other.id; }

    static const JobHandle Invalid;
};

// ============================================================
// Job 优先级
// ============================================================

enum class JobPriority : uint8 {
    Normal = 0,
    High   = 1,   // 预留：渲染/输入等高优先级
    Low    = 2,   // 预留：后台加载等低优先级
};

// ============================================================
// Job 函数签名
// ============================================================

using JobFunc         = std::function<void(uint32 threadIndex)>;
using ParallelForFunc = std::function<void(int32 index)>;

// ============================================================
// Job 系统 — 主 API
// ============================================================

class JobSystem {
public:
    // ── 单例生命周期 ──

    /**
     * @brief 初始化 Job 系统
     * @param threadCount 工作线程数（0 = 自动检测）
     * @param config 线程池配置（默认值使用推荐的核心分配策略）
     *
     * 自动检测策略：
     *   - 总核心 ≥ 4: workerCount = 总核心 - 2（留 2 核给 Main + Render）
     *   - 总核心 ≤ 2: workerCount = 1
     *   - 3 核心:   workerCount = 1
     */
    static void Init(uint32 threadCount = 0,
                     const ThreadPoolConfig& config = {});

    static void Shutdown();
    static JobSystem* Get() { return s_Instance; }
    static bool IsInitialized() { return s_Instance != nullptr; }

    // ── 调度 ──

    /**
     * @brief 调度一个单 Job
     * @param func       Job 函数 void(uint32 threadIndex)
     * @param dependency 前置依赖
     * @param priority   优先级
     * @return JobHandle 可用于 Wait()
     */
    JobHandle Schedule(JobFunc&& func,
                       JobHandle dependency = {},
                       JobPriority priority = JobPriority::Normal);

    /**
     * @brief 调度并行 for 循环
     * @param begin      起始索引（包含）
     * @param end        结束索引（不包含）
     * @param func       处理函数 void(int32 index)
     * @param dependency 前置依赖
     * @param priority   优先级
     * @return JobHandle 可用于 Wait()
     *
     * 自动将 [begin, end) 划分为多个批次，每个批次作为一个 Job 派发。
     * 批次大小根据线程数和 LLC 缓存大小自适应调整。
     */
    JobHandle ParallelFor(int32 begin, int32 end,
                          ParallelForFunc&& func,
                          JobHandle dependency = {},
                          JobPriority priority = JobPriority::Normal);

    // ── 同步 ──

    /**
     * @brief 等待一个 Job 完成
     *
     * 如果从主线程调用，会通过 work stealing 执行其他待处理的 Job，
     * 避免死锁并提高吞吐量。如果从工作线程调用，则直接阻塞等待。
     */
    void Wait(JobHandle handle);

    /** @brief 非阻塞检查 Job 是否完成 */
    bool IsCompleted(JobHandle handle);

    /** @brief 等待所有待处理的 Job 完成 */
    void WaitAll();

    /** @brief 每帧调用，处理完成的回调等 */
    void PollCompleted();

    // ── 统计信息 ──

    uint32 GetThreadCount() const  { return m_ThreadCount; }
    uint32 GetPendingJobCount() const;
    uint32 GetCompletedJobCount() const { return m_TotalCompleted; }

    /** 每帧末尾调用：推进 FrameTransient 等 */
    void EndFrame();

    // ── 渲染线程同步 ──

    JobHandle GetRenderSyncHandle() const { return m_RenderSyncHandle; }
    void SignalRenderSync();

private:
    static JobSystem* s_Instance;

    JobSystem(uint32 threadCount, const ThreadPoolConfig& config);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // ── 内部 Job 表示 ──
    struct Job {
        uint64              id = 0;
        JobFunc             func;
        std::atomic<int32>  unfinishedPrereqs{ 0 };
        std::vector<uint64> dependents;
        JobPriority         priority = JobPriority::Normal;
    };

    // ── Per-Worker 本地队列 ──
    struct PerWorkerQueue {
        std::mutex          mutex;
        std::deque<uint64>  jobs;           // 本地双端队列
        std::atomic<uint32> approximateSize{ 0 };  // 近似数量（减少锁内 size() 调用）
    };

    // ── 线程池 ──

    /** @brief 启动所有工作线程（在构造函数末尾调用） */
    void StartWorkers();

    /** @brief 停止所有工作线程并等待退出 */
    void StopWorkers();

    void WorkerLoop(uint32 threadIndex);

    // ── 内部调度 ──

    uint64 AllocateJob(JobFunc&& func, JobHandle dependency, JobPriority priority);
    void   EnqueueJob(uint64 jobId);

    /** 入队到指定 worker 的本地队列 */
    void   EnqueueToWorker(uint32 workerIndex, uint64 jobId);

    /** 提交到全局队列 */
    void   EnqueueGlobal(uint64 jobId);

    void   ExecuteJob(uint64 jobId, uint32 threadIndex);
    void   OnJobCompleted(uint64 jobId);

    // ── Per-Worker 操作 ──

    /** 从自己的本地队列出队 */
    bool   TryPopLocal(uint32 workerIndex, uint64& outJobId);

    /** 从其他 worker 的队列顶部窃取 */
    bool   TrySteal(uint32 thiefIndex, uint64& outJobId);

    // ── 工作线程 ──

    uint32                      m_ThreadCount;
    std::vector<std::thread>    m_Workers;
    std::atomic<bool>           m_Running{ false };

    // ── Per-Worker 队列 ──
    // 使用 deque 替代 vector，因为 PerWorkerQueue 含 std::mutex（不可移动/拷贝）
    // deque 以块为单元分配，元素本身不会被 reallocate 时移动
    std::deque<PerWorkerQueue> m_WorkerQueues;

    // 窃取循环起始 victim（轮询 + 随机化减少争用）
    std::atomic<uint32>         m_NextVictim{ 0 };

    // ── 全局队列（Fallback：主线程提交时未知 target worker） ──
    std::mutex                  m_GlobalQueueMutex;
    std::queue<uint64>          m_GlobalQueue;
    std::condition_variable     m_WakeCondition;

    // ── Job 存储 ──
    std::mutex                  m_JobMapMutex;
    std::unordered_map<uint64, std::unique_ptr<Job>> m_JobMap;
    std::atomic<uint64>         m_NextJobId{ 1 };

    // ── 完成跟踪 ──
    std::atomic<uint32>         m_PendingCount{ 0 };
    std::atomic<uint32>         m_TotalCompleted{ 0 };

    // ── 线程池配置 ──
    uint32                      m_WorkerBaseCore = 2;   // Worker 池起始核心
    uint32                      m_IoBaseCore     = 0;   // IO 池起始核心

    // ── 渲染同步 ──
    JobHandle                   m_RenderSyncHandle;
};

// ============================================================

} // namespace Engine