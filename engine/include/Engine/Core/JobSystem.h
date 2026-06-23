#pragma once

/**
 * @file JobSystem.h
 * @brief 通用 Job 系统 — 线程池 + 任务级并行调度
 *
 * 设计目标：
 *   1. 提供简单高效的线程池，将可并行的任务分散到多核执行
 *   2. 支持单 Job 和 ParallelFor 两种调度模式
 *   3. 主线程 Wait() 时执行工作窃取（work stealing），避免死锁
 *   4. 为未来的渲染线程分离、Fiber 调度预留接口
 *
 * 使用方式：
 * @code
 *   // 初始化（Application 启动时调用一次）
 *   JobSystem::Init();
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
 *
 * 未来扩展预留：
 *   - JobPriority::High / Normal / Low     → 优先级调度
 *   - OnMainThread(JobHandle)              → 在主线程执行完成回调
 *   - GetRenderSyncHandle()                → 渲染线程帧同步
 *   - Fiber 纤程集成                       → 更轻量的协程式 Job
 */

#include "Engine/Types.h"
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
// 前向声明
// ============================================================

struct JobInternal;

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
// Job 优先级（为未来预留）
// ============================================================

enum class JobPriority : uint8 {
    // ── 当前实现级别（所有 Job 同优先级，FIFO） ──
    Normal = 0,

    // ── 预留扩展（暂未实现，使用 Normal 回退） ──
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
     * @param threadCount 工作线程数（0 = std::thread::hardware_concurrency()）
     * @param reservedForRender 为渲染线程预留的核心数（默认 0）
     *
     * reservedForRender 用于未来渲染线程分离时，预留 CPU 核心给渲染线程。
     * 当前忽略此参数（渲染尚未分离），但 API 已预留。
     */
    static void Init(uint32 threadCount = 0, uint32 reservedForRender = 0);
    static void Shutdown();
    static JobSystem* Get() { return s_Instance; }
    static bool IsInitialized() { return s_Instance != nullptr; }

    // ── 调度 ──

    /**
     * @brief 调度一个单 Job
     * @param func       Job 函数 void(uint32 threadIndex)
     * @param dependency 前置依赖（当前 Job 完成后才执行此 Job）
     * @param priority   优先级（暂未实现优先级队列，所有 Normal）
     * @return JobHandle 可用于 Wait()
     *
     * 若有 dependency，此 Job 会等到 dependency 完成后才被派发到工作线程。
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
     * 批次大小 = max(1, (end - begin + threadCount - 1) / threadCount)。
     */
    JobHandle ParallelFor(int32 begin, int32 end,
                          ParallelForFunc&& func,
                          JobHandle dependency = {},
                          JobPriority priority = JobPriority::Normal);

    // ── 同步 ──

    /**
     * @brief 等待一个 Job 完成
     * @param handle 要等待的 Job 句柄
     *
     * 如果从主线程调用，会通过 work stealing 执行其他待处理的 Job，
     * 避免死锁并提高吞吐量。如果从工作线程调用，则直接阻塞等待。
     */
    void Wait(JobHandle handle);

    /**
     * @brief 非阻塞检查 Job 是否完成
     */
    bool IsCompleted(JobHandle handle);

    /**
     * @brief 等待所有待处理的 Job 完成
     */
    void WaitAll();

    // ── 主线程辅助（每帧调用） ──

    /**
     * @brief 在主线程执行已完成的 Job 回调
     *
     * 预留接口。当前没有需要主线程执行的回调，但未来可能需要
     * （如资源加载完成后在主线程调用回调）。
     */
    void PollCompleted();

    // ── 统计信息 ──

    uint32 GetThreadCount() const  { return m_ThreadCount; }
    uint32 GetPendingJobCount() const;
    uint32 GetCompletedJobCount() const { return m_TotalCompleted; }

    // ── 为下一步渲染线程分离预留的接口 ──

    /**
     * @brief 获取渲染线程的帧同步 JobHandle
     *
     * 渲染线程可用此 handle 等待游戏线程完成帧更新后，再开始提交渲染命令。
     * 当前返回 Invalid（渲染线程尚未分离），API 已预留。
     */
    JobHandle GetRenderSyncHandle() const { return m_RenderSyncHandle; }

    /**
     * @brief 设置渲染线程帧同步（每次帧开始前调用）
     *
     * 排入一个空的同步 Job，其完成标志着游戏线程帧更新结束。
     * 渲染线程等待此 Job 即可安全读取游戏状态/命令缓冲。
     */
    void SignalRenderSync();

private:
    static JobSystem* s_Instance;

    JobSystem(uint32 threadCount, uint32 reservedForRender);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // ── 内部 Job 表示 ──
    struct Job {
        uint64              id = 0;
        JobFunc             func;
        std::atomic<int32>  unfinishedPrereqs{ 0 };  // 前置依赖计数
        std::vector<uint64> dependents;              // 依赖此 Job 的子 Job
        JobPriority         priority = JobPriority::Normal;
    };

    // ── 工作线程函数 ──
    /** @brief 启动所有工作线程（在构造函数末尾调用，确保所有成员已构造） */
    void StartWorkers();

    /** @brief 停止所有工作线程并等待它们退出（在析构函数中调用） */
    void StopWorkers();

    void WorkerLoop(uint32 threadIndex);

    // ── 内部调度 ──
    uint64 AllocateJob(JobFunc&& func, JobHandle dependency, JobPriority priority);
    void   EnqueueJob(uint64 jobId);
    void   ExecuteJob(uint64 jobId, uint32 threadIndex);
    void   OnJobCompleted(uint64 jobId);
    bool   TryStealJob(uint64& outJobId);  // 工作窃取

    // ── 线程池 ──
    uint32                      m_ThreadCount;
    std::vector<std::thread>    m_Workers;
    std::atomic<bool>           m_Running{ false };

    // ── Job 队列 ──
    std::mutex                  m_QueueMutex;
    std::queue<uint64>          m_JobQueue;
    std::condition_variable     m_WakeCondition;

    // ── Job 存储 ──
    std::mutex                  m_JobMapMutex;
    std::unordered_map<uint64, std::unique_ptr<Job>> m_JobMap;
    std::atomic<uint64>         m_NextJobId{ 1 };

    // ── 完成跟踪 ──
    std::atomic<uint32>         m_PendingCount{ 0 };
    std::atomic<uint32>         m_TotalCompleted{ 0 };

    // ── 为下一步渲染线程分离预留 ──
    uint32                      m_ReservedForRender = 0;   // 预留核心数
    JobHandle                   m_RenderSyncHandle;         // 渲染同步 handle
};

} // namespace Engine
