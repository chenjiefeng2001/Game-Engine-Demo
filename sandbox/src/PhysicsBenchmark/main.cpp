/**
 * @file PhysicsBenchmark.cpp
 * @brief CPU vs GPU 物理引擎性能对比测试
 *
 * 测试目标�?
 *   1. 相同算法（半隐式欧拉 + 边界碰撞 + 惩罚力粒子碰撞）�?CPU �?GPU 上的性能差距
 *   2. 不同粒子数量级下的吞吐量对比�?K / 4K / 16K / 64K / 256K�?
 *   3. CPU 多线�?vs GPU 数千核心的加速比
 *   4. 验证 GPU 物理�?Zero-Copy 渲染优势（无需 PCIe 回读�?
 *
 * 运行方式�?
 *   编译后直接运行：./build/sandbox/Debug/sandbox.exe --benchmark
 *   测试完成后输�?CSV 格式对比数据
 *
 * 测试环境建议�?
 *   - CPU: 任意现代 x86-64 处理器（测试自动检测核心数�?
 *   - GPU: GTX 1060 或更高（OpenGL 4.3+ 以支�?Compute Shader�?
 *   - 驱动: 更新到最新版�?
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/GpuTimestampProfiler.h"
#include "Engine/Core/Log.h"
#include "Engine/Application.h"
#include "Engine/Core/FileSystem.h"
#include "Engine/Core/TaskGraph.h"
#include "Engine/Core/JobSystem.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <random>
#include <memory>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>

// ══════════════════════════════════════════════════════════�?
// 基准测试配置
// ══════════════════════════════════════════════════════════�?

struct BenchmarkConfig {
    // 粒子数量级（每个量级�?5 帧取平均�?
    std::vector<uint32_t> particleCounts = {1024, 4096, 16384, 65536, 262144};

    // 每量级的测试帧数
    uint32_t framesPerTest = 5;

    // 是否启用粒子间碰撞（CPU �?O(N²) 在大量级下极慢，可选择关闭�?
    bool enableCollision = true;

    // CPU 端使用的线程数（0 = 自动检测）
    uint32_t cpuThreadCount = 0;

    // 输出 CSV 文件路径
    std::string outputPath = "logs/physics_benchmark.csv";
};

// ══════════════════════════════════════════════════════════�?
// CPU 端粒子物理实现（�?GPU Compute Shader 算法一致）
// ══════════════════════════════════════════════════════════�?

struct CPUParticle {
    float position[3];
    float radius;
    float velocity[3];
    float mass;
    float color[4];
};

struct CPUPhysicsConfig {
    uint32_t particleCount = 65536;
    float gravity[3] = {0.0f, -9.8f, 0.0f};
    float restitution = 0.7f;
    float stiffness = 500.0f;
    float damping = 0.01f;
    float boxMin[3] = {-60.0f, 0.0f, -60.0f};
    float boxMax[3] = {60.0f, 120.0f, 60.0f};
    float dt = 0.016f;
};

/**
 * @brief CPU 端粒子物理模拟（单线程版本，�?GPU Compute Shader 算法完全一致）
 */
static void CPUSimulateParticles(CPUParticle* particles, uint32_t count,
                                  const CPUPhysicsConfig& config, bool enableCollision) {
    const float dt = config.dt;
    const float damping = config.damping;
    const float restitution = config.restitution;
    float stiffness = config.stiffness;
    (void)particles;
    (void)count;
    (void)stiffness;

    // Pass 1: 半隐式欧拉积�?+ 边界碰撞
    for (uint32_t i = 0; i < count; ++i) {
        auto& p = particles[i];

        // 积分
        p.velocity[0] += config.gravity[0] * dt;
        p.velocity[1] += config.gravity[1] * dt;
        p.velocity[2] += config.gravity[2] * dt;

        // 阻尼
        p.velocity[0] *= (1.0f - damping * dt);
        p.velocity[1] *= (1.0f - damping * dt);
        p.velocity[2] *= (1.0f - damping * dt);

        p.position[0] += p.velocity[0] * dt;
        p.position[1] += p.velocity[1] * dt;
        p.position[2] += p.velocity[2] * dt;

        // 边界碰撞
        if (p.position[0] - p.radius < config.boxMin[0]) {
            p.position[0] = config.boxMin[0] + p.radius;
            p.velocity[0] = -p.velocity[0] * restitution;
        }
        if (p.position[0] + p.radius > config.boxMax[0]) {
            p.position[0] = config.boxMax[0] - p.radius;
            p.velocity[0] = -p.velocity[0] * restitution;
        }
        if (p.position[1] - p.radius < config.boxMin[1]) {
            p.position[1] = config.boxMin[1] + p.radius;
            p.velocity[1] = -p.velocity[1] * restitution;
        }
        if (p.position[1] + p.radius > config.boxMax[1]) {
            p.position[1] = config.boxMax[1] - p.radius;
            p.velocity[1] = -p.velocity[1] * restitution;
        }
        if (p.position[2] - p.radius < config.boxMin[2]) {
            p.position[2] = config.boxMin[2] + p.radius;
            p.velocity[2] = -p.velocity[2] * restitution;
        }
        if (p.position[2] + p.radius > config.boxMax[2]) {
            p.position[2] = config.boxMax[2] - p.radius;
            p.velocity[2] = -p.velocity[2] * restitution;
        }
    }

    // Pass 2: 粒子间碰撞（仅启用时�?
    if (!enableCollision) return;

    for (uint32_t i = 0; i < count; ++i) {
        float fx = 0, fy = 0, fz = 0;

        for (uint32_t j = 0; j < count; ++j) {
            if (i == j) continue;

            float dx = particles[i].position[0] - particles[j].position[0];
            float dy = particles[i].position[1] - particles[j].position[1];
            float dz = particles[i].position[2] - particles[j].position[2];

            float distSq = dx*dx + dy*dy + dz*dz;
            if (distSq < 0.0001f) continue;

            float dist = std::sqrt(distSq);
            float minDist = particles[i].radius + particles[j].radius;

            if (dist < minDist) {
                float overlap = minDist - dist;
                float invDist = 1.0f / dist;
                fx += dx * invDist * overlap * stiffness;
                fy += dy * invDist * overlap * stiffness;
                fz += dz * invDist * overlap * stiffness;
            }
        }

        if (particles[i].mass > 0.0f) {
            particles[i].velocity[0] += (fx / particles[i].mass) * dt;
            particles[i].velocity[1] += (fy / particles[i].mass) * dt;
            particles[i].velocity[2] += (fz / particles[i].mass) * dt;
        }
    }
}

/**
 * @brief CPU 端粒子物理模拟（多线程并行版本）
 * 使用简单的分块并行，每个线程处理一块连续的粒子范围�?
 */
static void CPUSimulateParticlesParallel(CPUParticle* particles, uint32_t count,
                                          const CPUPhysicsConfig& config,
                                          uint32_t threadCount, bool enableCollision) {
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
    }

    const float dt = config.dt;
    const float damping = config.damping;
    const float restitution = config.restitution;

    // ── Pass 1: 积分 + 边界（完全可并行�?──
    {
        std::vector<std::thread> threads;
        uint32_t chunkSize = (count + threadCount - 1) / threadCount;

        for (uint32_t t = 0; t < threadCount; ++t) {
            threads.emplace_back([&, t]() {
                uint32_t start = t * chunkSize;
                uint32_t end = std::min(start + chunkSize, count);

                for (uint32_t i = start; i < end; ++i) {
                    auto& p = particles[i];

                    p.velocity[0] += config.gravity[0] * dt;
                    p.velocity[1] += config.gravity[1] * dt;
                    p.velocity[2] += config.gravity[2] * dt;

                    p.velocity[0] *= (1.0f - damping * dt);
                    p.velocity[1] *= (1.0f - damping * dt);
                    p.velocity[2] *= (1.0f - damping * dt);

                    p.position[0] += p.velocity[0] * dt;
                    p.position[1] += p.velocity[1] * dt;
                    p.position[2] += p.velocity[2] * dt;

                    // 边界碰撞逻辑...
                    if (p.position[0] - p.radius < config.boxMin[0]) {
                        p.position[0] = config.boxMin[0] + p.radius;
                        p.velocity[0] = -p.velocity[0] * restitution;
                    }
                    if (p.position[0] + p.radius > config.boxMax[0]) {
                        p.position[0] = config.boxMax[0] - p.radius;
                        p.velocity[0] = -p.velocity[0] * restitution;
                    }
                    if (p.position[1] - p.radius < config.boxMin[1]) {
                        p.position[1] = config.boxMin[1] + p.radius;
                        p.velocity[1] = -p.velocity[1] * restitution;
                    }
                    if (p.position[1] + p.radius > config.boxMax[1]) {
                        p.position[1] = config.boxMax[1] - p.radius;
                        p.velocity[1] = -p.velocity[1] * restitution;
                    }
                    if (p.position[2] - p.radius < config.boxMin[2]) {
                        p.position[2] = config.boxMin[2] + p.radius;
                        p.velocity[2] = -p.velocity[2] * restitution;
                    }
                    if (p.position[2] + p.radius > config.boxMax[2]) {
                        p.position[2] = config.boxMax[2] - p.radius;
                        p.velocity[2] = -p.velocity[2] * restitution;
                    }
                }
            });
        }

        for (auto& t : threads) t.join();
    }

    // ── Pass 2: 碰撞（仅启用时） ──
    if (!enableCollision) return;

    // 碰撞检测的并行化较复杂（读写竞争），简化处理：
    // 使用原子操作累计力的分量
    std::vector<std::atomic<float>> forceX(count);
    std::vector<std::atomic<float>> forceY(count);
    std::vector<std::atomic<float>> forceZ(count);
    for (uint32_t i = 0; i < count; ++i) {
        forceX[i] = 0.0f;
        forceY[i] = 0.0f;
        forceZ[i] = 0.0f;
    }

    {
        std::vector<std::thread> threads;
        uint32_t chunkSize = (count + threadCount - 1) / threadCount;

        for (uint32_t t = 0; t < threadCount; ++t) {
            threads.emplace_back([&, t]() {
                uint32_t start = t * chunkSize;
                uint32_t end = std::min(start + chunkSize, count);

                for (uint32_t i = start; i < end; ++i) {
                    for (uint32_t j = 0; j < count; ++j) {
                        if (i == j) continue;

                        float dx = particles[i].position[0] - particles[j].position[0];
                        float dy = particles[i].position[1] - particles[j].position[1];
                        float dz = particles[i].position[2] - particles[j].position[2];

                        float distSq = dx*dx + dy*dy + dz*dz;
                        if (distSq < 0.0001f) continue;

                        float dist = std::sqrt(distSq);
                        float minDist = particles[i].radius + particles[j].radius;

                        if (dist < minDist) {
                            float overlap = minDist - dist;
                            float invDist = 1.0f / dist;
                            float f = overlap * config.stiffness;
                            forceX[i].fetch_add(dx * invDist * f, std::memory_order_relaxed);
                            forceY[i].fetch_add(dy * invDist * f, std::memory_order_relaxed);
                            forceZ[i].fetch_add(dz * invDist * f, std::memory_order_relaxed);
                        }
                    }
                }
            });
        }

        for (auto& t : threads) t.join();
    }

    // 应用�?
    for (uint32_t i = 0; i < count; ++i) {
        if (particles[i].mass > 0.0f) {
            particles[i].velocity[0] += (forceX[i].load() / particles[i].mass) * dt;
            particles[i].velocity[1] += (forceY[i].load() / particles[i].mass) * dt;
            particles[i].velocity[2] += (forceZ[i].load() / particles[i].mass) * dt;
        }
    }
}

// ══════════════════════════════════════════════════════════�?
// 基准测试引擎
// ══════════════════════════════════════════════════════════�?

struct BenchmarkResult {
    uint32_t particleCount;
    double cpuSingleMs;      // CPU 单线程耗时 (ms)
    double cpuMultiMs;       // CPU 多线程耗时 (ms)
    double gpuComputeMs;     // GPU 持续吞吐均摊（ms/帧，连续提交模式�?
    double gpuIntegrateMs;   // Integrate Pass 分段 GPU 耗时 (ms)
    double gpuCollideMs;     // Collide Pass 分段 GPU 耗时 (ms)
    double gpuBarrierMs;     // 独立 UAV Barrier 分段 GPU 耗时 (ms)
    double gpuUploadUs;      // UploadInitialData 持久映射带宽探测 (us)
    double cpuSingleFPS;     // CPU 单线程等�?FPS
    double cpuMultiFPS;      // CPU 多线程等�?FPS
    double gpuFPS;           // GPU 等效 FPS（由 gpuComputeMs 换算�?
    double speedupMulti;     // CPU 多线�?vs 单线程加速比
    double speedupGPU;        // GPU vs CPU 多线程加速比
    bool collisionEnabled;
};

// ══════════════════════════════════════════════════════════�?
// GPU 上下文（v3.0 · Phase 0：真�?GPU Benchmark Baseline�?
// 复用 GPUPhysicsTest 的隐藏窗�?+ glad + GL46Device 配方
// ══════════════════════════════════════════════════════════�?

namespace gpuctx {
    static GLFWwindow* window = nullptr;
    static std::unique_ptr<GladGLContext> glad;
    static std::unique_ptr<Engine::RHI::GL46Device> device;
    static std::unique_ptr<Engine::RHI::IGpuTimestampProfiler> profiler;
    static bool valid = false;

    static bool Init() {
        if (!glfwInit()) { printf("  glfwInit failed - GPU benchmarks disabled\n"); return false; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(1, 1, "PhysicsBenchmark (hidden)", nullptr, nullptr);
        if (!window) { printf("  hidden GLFW window failed\n"); glfwTerminate(); return false; }
        glfwMakeContextCurrent(window);

        glad = std::make_unique<GladGLContext>();
        if (!gladLoadGLContext(glad.get(), glfwGetProcAddress)) {
            printf("  gladLoadGLContext failed\n");
            glfwDestroyWindow(window); window = nullptr; glfwTerminate();
            return false;
        }

        device = std::make_unique<Engine::RHI::GL46Device>();
        if (!device->InitializeWithGLContext(glad.get(), 1, 1)) {
            printf("  GL46Device init failed\n");
            device.reset(); glad.reset();
            glfwDestroyWindow(window); window = nullptr; glfwTerminate();
            return false;
        }
        printf("  GPU Device: %s\n", device->GetDeviceName());

        profiler = Engine::RHI::CreateGpuTimestampProfiler();
        const bool profOk = profiler->Initialize(device.get(), 192);
        printf("  Timestamp Profiler: %s\n", profOk ? "available" : "unavailable (segmented metrics N/A)");
        if (!profOk) profiler.reset();

        valid = true;
        return true;
    }

    static void Shutdown() {
        if (!valid) return;
        profiler.reset();
        device.reset();   // ~GL46Device 内部执行私有 Shutdown()
        glad.reset();
        if (window) { glfwDestroyWindow(window); window = nullptr; }
        glfwTerminate();
        valid = false;
    }

    /// 真实 GPU 排空�?
    /// 注意：GL46Queue::WaitIdle() 当前为空实现（见 GL46SwapChain.cpp:35），
    /// 必须�?Device::WaitIdle()（内�?glFinish）才能获得同步语�?
    static void SyncGpu() {
        if (device) device->WaitIdle();
    }
} // namespace gpuctx

/**
 * @brief �?GPUPhysicsEngine::ResetParticles 完全一致的种子生成（seed=42�?
 * @note  用于纯上传带宽探测：生成与引擎内部相同的数据，隔�?mt19937 生成耗时
 */
static std::vector<Engine::GPUParticleData> GenerateSeedParticles(
        const Engine::GPUPhysicsConfig& cfg) {
    std::vector<Engine::GPUParticleData> ps(cfg.particleCount);
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> posDist(-cfg.spawnRadius, cfg.spawnRadius);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    for (auto& p : ps) {
        p.position[0] = posDist(gen);
        p.position[1] = std::abs(posDist(gen)) + 10.0f;
        p.position[2] = posDist(gen);
        p.radius = radiusDist(gen);
        p.velocity[0] = cfg.spawnVelocity[0];
        p.velocity[1] = cfg.spawnVelocity[1];
        p.velocity[2] = cfg.spawnVelocity[2];
        p.mass = massDist(gen);
        p.color[0] = colorDist(gen);
        p.color[1] = colorDist(gen);
        p.color[2] = colorDist(gen);
        p.color[3] = 1.0f;
    }
    return ps;
}

static bool g_useSpatialHash = false;

static Engine::GPUPhysicsConfig MakeGpuConfig(uint32_t count, Engine::GPUCollisionBackend backend) {
    Engine::GPUPhysicsConfig cfg;
    cfg.particleCount = count;
    cfg.collisionBackend = backend;
    cfg.gravity[0] = 0.0f;  cfg.gravity[1] = -9.8f; cfg.gravity[2] = 0.0f;
    cfg.restitution = 0.7f;
    cfg.stiffness   = 500.0f;
    cfg.damping     = 0.01f;
    cfg.boxMin[0] = -60.0f; cfg.boxMin[1] = 0.0f;   cfg.boxMin[2] = -60.0f;
    cfg.boxMax[0] =  60.0f; cfg.boxMax[1] = 120.0f; cfg.boxMax[2] =  60.0f;
    return cfg;
}

static double NowMsWall() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 单量级真�?GPU 测量（v3.0 Phase 0 核心�?
 *
 * 三段式：
 *   1. Warmup      �?10 帧逐帧排空，触发驱动着色器编译/PSO 路径预热
 *   2. Segmented   �?自适应帧数连续录制（时间戳累积、不逐帧排空），单次排空后解�?
 *                    Integrate / Barrier01 / Collide 分段均�?
 *                    �?队列保持满载，规�?录制点即执行�?的气泡偏�?
 *   3. Throughput  �?自适应帧数连续提交，GPU 首尾时间戳跨度均�?
 *                    �?可持续吞吐（gpuComputeMs 口径，参与加速比计算�?
 *
 * 时间预算：探针估算单帧成本后�?kBudgetMs 收缩帧数�?
 *          单帧成本 > kSlowFrameThresholdMs 时跳过分段（避免分钟级卡顿）
 */
static void RunGpuBenchmarkForTier(uint32_t particleCount, BenchmarkResult& r) {
    constexpr float   kDt                    = 1.0f / 60.0f;
    constexpr int     kWarmupFrames          = 10;
    constexpr int     kMaxSegFrames          = 48;
    constexpr int     kMinSegFrames          = 8;
    constexpr double  kBudgetMs              = 2000.0;
    constexpr double  kSlowFrameThresholdMs  = 500.0;

    // 每量级独立实�?—�?GPUPhysicsEngine 不支持运行时 resize（见原注释）
    auto phys = std::make_unique<Engine::GPUPhysicsEngine>();
    const Engine::GPUCollisionBackend be = g_useSpatialHash
        ? Engine::GPUCollisionBackend::SpatialHash : Engine::GPUCollisionBackend::BruteForce;
    const Engine::GPUPhysicsConfig cfg = MakeGpuConfig(particleCount, be);
    if (!phys->Initialize(gpuctx::device.get(), cfg)) {
        printf("  [GPU %u] Initialize failed\n", particleCount);
        return;
    }

    auto cmd = gpuctx::device->CreateCommandList(Engine::RHI::CommandListType::Direct);
    auto* q  = gpuctx::device->GetQueue(Engine::RHI::QueueType::Graphics);
    if (!cmd || !q) { printf("  [GPU %u] command list/queue unavailable\n", particleCount); return; }
    Engine::RHI::IRHICommandList* ls[] = { cmd.get() };

    // ── 状态初始化（不计时�?──
    phys->ResetParticles();

    // ── Upload 带宽探测：持久映�?memcpy，剥�?mt19937 生成耗时 ──
    {
        auto seed = GenerateSeedParticles(cfg);
        double t0 = NowMsWall();
        phys->UploadInitialData(seed.data(), particleCount);
        r.gpuUploadUs = (NowMsWall() - t0) * 1000.0;
    }

    // ── Warmup ──
    for (int i = 0; i < kWarmupFrames; ++i) {
        cmd->Begin();
        phys->Update(kDt, cmd.get());
        cmd->End();
        q->ExecuteCommandLists(1, ls);
        gpuctx::SyncGpu();
    }
    cmd->ResetTimestamps();

    // ── 探针�? 帧同步测墙钟，估算单帧成本并推导各阶段帧�?──
    double probe = 0.0;
    for (int i = 0; i < 3; ++i) {
        double t0 = NowMsWall();
        cmd->Begin();
        phys->Update(kDt, cmd.get());
        cmd->End();
        q->ExecuteCommandLists(1, ls);
        gpuctx::SyncGpu();
        probe += NowMsWall() - t0;
    }
    probe /= 3.0;
    printf("  [GPU %u] probe: %.2f ms/frame\n", particleCount, probe);
    fflush(stdout);

    // 极慢层级护栏：暴�?O(N²) 在超大粒子数下单帧可达秒级，
    // 跳过精细测量，直接以探针值作为吞吐口�?
    if (probe > kSlowFrameThresholdMs) {
        r.gpuComputeMs = probe;
        printf("  [GPU %u] slow-tier guard: skipping segmented/throughput sessions "
               "(%.0f ms/frame > %.0f threshold)\n",
               particleCount, probe, kSlowFrameThresholdMs);
        fflush(stdout);
        return;
    }

    // ── Segmented：连续录�?+ 单次排空 + 统一解析 ──
    if (gpuctx::profiler && gpuctx::profiler->IsAvailable()) {
        auto& prof = *gpuctx::profiler;
        int segFrames = (int)(kBudgetMs / std::max(probe, 0.001));
        segFrames = std::max(kMinSegFrames, std::min(segFrames, kMaxSegFrames));

        // Phase 2 Ping-Pong: explicit src/dst slots, no flips inside the loop,
        // keeping A = stable input / B = write target for steady-state timing
        auto* bufA = phys->GetStateBuffer();
        auto* bufB = phys->GetScratchBuffer();

        prof.BeginFrame();
        cmd->Begin();

        for (int f = 0; f < segFrames; ++f) {
            uint32_t sI = prof.BeginSegment(*cmd, "Integrate");
            phys->RecordIntegratePass(kDt, bufA, bufB, cmd.get());
            prof.EndSegment(*cmd, sI);

            // 显式屏障分段：测量一次独�?glMemoryBarrier �?GPU 排序开销
            uint32_t sB = prof.BeginSegment(*cmd, "Barrier01");
            Engine::RHI::ResourceBarrierDesc uav;
            uav.type = Engine::RHI::ResourceBarrierDesc::Type::UAV;
            uav.buffer = bufB;
            cmd->ResourceBarrier(1, &uav);
            prof.EndSegment(*cmd, sB);

            uint32_t sC = prof.BeginSegment(*cmd, "Collide");
            phys->RecordCollidePass(bufB, bufA, cmd.get());
            prof.EndSegment(*cmd, sC);
        }

        cmd->End();
        q->ExecuteCommandLists(1, ls);
        gpuctx::SyncGpu();

        double integrate = 0.0, barrier = 0.0, collide = 0.0;
        int nI = 0, nB = 0, nC = 0;
        for (const auto& res : prof.Resolve()) {
            if (res.name == "Integrate")  { integrate += res.gpuMs; ++nI; }
            else if (res.name == "Barrier01") { barrier += res.gpuMs; ++nB; }
            else if (res.name == "Collide")   { collide += res.gpuMs; ++nC; }
        }
        if (nI > 0) r.gpuIntegrateMs = integrate / nI;
        if (nB > 0) r.gpuBarrierMs   = barrier / nB;
        if (nC > 0) r.gpuCollideMs   = collide / nC;

        printf("  [GPU %u] segmented: %d frames, integrate=%.3f collide=%.3f barrier=%.4f ms\n",
               particleCount, segFrames, r.gpuIntegrateMs, r.gpuCollideMs, r.gpuBarrierMs);
        fflush(stdout);

        cmd->ResetTimestamps();
        prof.BeginFrame();
    }

    // ── Throughput：自适应帧数 + GPU 首尾时间戳跨�?──
    {
        int tFrames = (int)(kBudgetMs / std::max(probe, 0.001));
        tFrames = std::max(kMinSegFrames, std::min(tFrames, 400));

        cmd->Begin();
        double wall0 = NowMsWall();
        uint32_t tA = cmd->WriteTimestamp();
        for (int f = 0; f < tFrames; ++f)
            phys->Update(kDt, cmd.get());
        uint32_t tB = cmd->WriteTimestamp();
        q->ExecuteCommandLists(1, ls);
        gpuctx::SyncGpu();
        double wall1 = NowMsWall();

        double gpuSpan = 0.0;
        const bool haveGpuSpan =
            (tA != Engine::RHI::IRHICommandList::kInvalidTimestamp &&
             tB != Engine::RHI::IRHICommandList::kInvalidTimestamp &&
             cmd->ResolveTimestampSpan(tA, tB, gpuSpan));

        r.gpuComputeMs = haveGpuSpan ? (gpuSpan / tFrames) : ((wall1 - wall0) / tFrames);
        printf("  [GPU %u] throughput: %.3f ms/frame (%d frames, src=%s)\n",
               particleCount, r.gpuComputeMs, tFrames,
               haveGpuSpan ? "gpu-ts" : "wall-clock");
        fflush(stdout);
        cmd->ResetTimestamps();
    }
}


/**
 * @brief 运行单个量级的基准测�?
 */
static BenchmarkResult RunSingleBenchmark(uint32_t particleCount,
                                           uint32_t frames,
                                           uint32_t threadCount,
                                           bool enableCollision) {
    BenchmarkResult result = {};
    result.particleCount = particleCount;
    result.collisionEnabled = enableCollision;

    // ── 准备初始数据（CPU �?GPU 使用相同种子，确保一致性） ──
    std::mt19937 gen(42);  // 固定种子
    std::uniform_real_distribution<float> posDist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> radiusDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> massDist(0.5f, 2.0f);

    std::vector<CPUParticle> cpuParticles(particleCount);
    for (uint32_t i = 0; i < particleCount; ++i) {
        cpuParticles[i].position[0] = posDist(gen);
        cpuParticles[i].position[1] = std::abs(posDist(gen)) + 10.0f;
        cpuParticles[i].position[2] = posDist(gen);
        cpuParticles[i].radius = radiusDist(gen);
        cpuParticles[i].velocity[0] = posDist(gen) * 0.3f;
        cpuParticles[i].velocity[1] = std::abs(posDist(gen)) * 0.3f + 5.0f;
        cpuParticles[i].velocity[2] = posDist(gen) * 0.3f;
        cpuParticles[i].mass = massDist(gen);
        cpuParticles[i].color[0] = 0.5f;
        cpuParticles[i].color[1] = 0.7f;
        cpuParticles[i].color[2] = 1.0f;
        cpuParticles[i].color[3] = 1.0f;
    }

    CPUPhysicsConfig cpuConfig;
    cpuConfig.particleCount = particleCount;
    cpuConfig.dt = 0.016f;

    // ── 预热�? 帧） ──
    CPUSimulateParticles(cpuParticles.data(), particleCount, cpuConfig, enableCollision);

    // ── CPU 单线程测�?──
    {
        // 重新初始�?
        gen.seed(42);
        for (uint32_t i = 0; i < particleCount; ++i) {
            cpuParticles[i].position[0] = posDist(gen);
            cpuParticles[i].position[1] = std::abs(posDist(gen)) + 10.0f;
            cpuParticles[i].position[2] = posDist(gen);
        }

        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t f = 0; f < frames; ++f) {
            cpuConfig.dt = 0.016f;
            CPUSimulateParticles(cpuParticles.data(), particleCount, cpuConfig, enableCollision);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        result.cpuSingleMs = totalMs / frames;
        result.cpuSingleFPS = 1000.0 / result.cpuSingleMs;
    }

    // ── CPU 多线程测�?──
    if (particleCount >= 4096) {  // 小量级下多线程开销可能大于收益
        gen.seed(42);
        for (uint32_t i = 0; i < particleCount; ++i) {
            cpuParticles[i].position[0] = posDist(gen);
            cpuParticles[i].position[1] = std::abs(posDist(gen)) + 10.0f;
            cpuParticles[i].position[2] = posDist(gen);
        }

        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t f = 0; f < frames; ++f) {
            cpuConfig.dt = 0.016f;
            CPUSimulateParticlesParallel(cpuParticles.data(), particleCount, cpuConfig,
                                          threadCount, enableCollision);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        result.cpuMultiMs = totalMs / frames;
        result.cpuMultiFPS = 1000.0 / result.cpuMultiMs;
    } else {
        result.cpuMultiMs = result.cpuSingleMs;
        result.cpuMultiFPS = result.cpuSingleFPS;
    }

    // ── GPU 测试（v3.0 Phase 0：真�?Dispatch + 分段剖析 + 吞吐口径）──
    if (gpuctx::valid) {
        RunGpuBenchmarkForTier(particleCount, result);
        if (result.gpuComputeMs > 0.001)
            result.gpuFPS = 1000.0 / result.gpuComputeMs;
    }

    // 计算加速比
    result.speedupMulti = result.cpuSingleMs / result.cpuMultiMs;
    if (result.gpuComputeMs > 0.001) {
        result.speedupGPU = result.cpuSingleMs / result.gpuComputeMs;
    } else {
        result.speedupGPU = 0.0;
    }

    return result;
}

/**
 * @brief 打印结果表头�?CSV
 */
static void PrintCSVHeader(std::ofstream& csv) {
    csv << "ParticleCount,CPUSingle_ms,CPUMulti_ms,GPU_ms,"
        << "GPU_Integrate_ms,GPU_Barrier01_ms,GPU_Collide_ms,GPU_Upload_us,"
        << "CPUSingle_FPS,CPUMulti_FPS,GPU_FPS,"
        << "Speedup_Multi,Speedup_GPU,Collision\n";
}

/**
 * @brief 打印结果行到 CSV
 */
static void PrintCSVRow(std::ofstream& csv, const BenchmarkResult& r) {
    csv << r.particleCount << ","
        << std::fixed << std::setprecision(3) << r.cpuSingleMs << ","
        << std::fixed << std::setprecision(3) << r.cpuMultiMs << ","
        << std::fixed << std::setprecision(3) << r.gpuComputeMs << ","
        << std::fixed << std::setprecision(3) << r.gpuIntegrateMs << ","
        << std::fixed << std::setprecision(4) << r.gpuBarrierMs << ","
        << std::fixed << std::setprecision(3) << r.gpuCollideMs << ","
        << std::fixed << std::setprecision(1) << r.gpuUploadUs << ","
        << std::fixed << std::setprecision(1) << r.cpuSingleFPS << ","
        << std::fixed << std::setprecision(1) << r.cpuMultiFPS << ","
        << std::fixed << std::setprecision(1) << r.gpuFPS << ","
        << std::fixed << std::setprecision(2) << r.speedupMulti << ","
        << std::fixed << std::setprecision(2) << r.speedupGPU << ","
        << (r.collisionEnabled ? "Yes" : "No") << "\n";
    csv.flush();
}

/**
 * @brief 打印人类可读的结果表
 */
static void PrintHumanReadable(const std::vector<BenchmarkResult>& results) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("�?          CPU vs GPU Physics Engine Benchmark Results                       ║\n");
    printf("╠════════════╦════════════╦════════════╦════════════╦════════════╦════════════╣\n");
    printf("�?Particles  �?CPU 1T ms  �?CPU MT ms  �?GPU ms     �?Speedup MT �?Speedup GPU║\n");
    printf("╠════════════╬════════════╬════════════╬════════════╬════════════╬════════════╣\n");

    for (const auto& r : results) {
        printf("�?%-9u �?%-9.3f �?%-9.3f �?%-9.3f �?%-9.2f �?%-9.2f ║\n",
               r.particleCount,
               r.cpuSingleMs,
               r.cpuMultiMs,
               r.gpuComputeMs,
               r.speedupMulti,
               r.speedupGPU);
    }

    printf("╚════════════╩════════════╩════════════╩════════════╩════════════╩════════════╝\n");
    printf("\n");
    printf("  Collision: %s\n", results.empty() || results[0].collisionEnabled ? "Enabled" : "Disabled");
    printf("  CPU Threads: %u (detected: %u)\n",
           results.empty() ? 0 : std::thread::hardware_concurrency(),
           std::thread::hardware_concurrency());

    // FPS 对比
    printf("\n");
    printf("╔════════════╦════════════╦════════════╦════════════╗\n");
    printf("�?Particles  �?CPU 1T FPS �?CPU MT FPS �?GPU FPS    ║\n");
    printf("╠════════════╬════════════╬════════════╬════════════╣\n");
    for (const auto& r : results) {
        printf("�?%-9u �?%-9.1f �?%-9.1f �?%-9.1f ║\n",
               r.particleCount,
               r.cpuSingleFPS,
               r.cpuMultiFPS,
               r.gpuFPS);
    }
    printf("╚════════════╩════════════╩════════════╩════════════╝\n");

    // GPU 分段剖析（v3.0 Phase 0 �?流水化分段均值，Phase 4 空间哈希的对照基线）
    bool hasSegments = false;
    for (const auto& r : results)
        if (r.gpuIntegrateMs > 0.0 || r.gpuCollideMs > 0.0) hasSegments = true;
    if (hasSegments) {
        printf("\n");
        printf("GPU Pass Breakdown (segmented, pipelined):\n");
        printf("╔════════════╦═════════════╦═════════════╦═════════════╦════════════╗\n");
        printf("�?Particles  �?Integrate   �?Barrier01   �?Collide     �?Upload us  ║\n");
        printf("╠════════════╬═════════════╬═════════════╬═════════════╬════════════╣\n");
        for (const auto& r : results) {
            printf("�?%-10u �?%8.3f ms �?%8.4f ms �?%8.3f ms �?%9.1f  ║\n",
                   r.particleCount,
                   r.gpuIntegrateMs,
                   r.gpuBarrierMs,
                   r.gpuCollideMs,
                   r.gpuUploadUs);
        }
        printf("╚════════════╩═════════════╩═════════════╩═════════════╩════════════╝\n");
    }
    printf("\n");
}

/**
 * @brief 系统信息打印
 */
static void PrintSystemInfo(uint32_t threadCount) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("�?       System Information                    ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("�? CPU Threads (config)  : %-20u  ║\n", threadCount);
    printf("�? CPU Threads (hardware): %-20u  ║\n", std::thread::hardware_concurrency());

    printf("�? GPU Device Info : via RHI backend            ║\n");
    printf("�? (System info requires external init)         ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n");
}

// ══════════════════════════════════════════════════════════�?
// 主入�?
// ══════════════════════════════════════════════════════════�?

int RunPhysicsBenchmark(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // 管道重向下保证进度实时可�?
    setvbuf(stdout, nullptr, _IONBF, 0);

    Engine::Logger s_Log("PhysicsBenchmark");
    s_Log.Info("========================================");
    s_Log.Info(" CPU vs GPU Physics Engine Benchmark");
    s_Log.Info("========================================");

    // ── 解析配置 ──
    BenchmarkConfig config;
    uint32_t threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;

    // 从命令行参数解析
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-collision") {
            config.enableCollision = false;
        } else if (arg == "--threads" && i + 1 < argc) {
            threadCount = static_cast<uint32_t>(std::atoi(argv[++i]));
            if (threadCount == 0) threadCount = 1;
        } else if (arg == "--frames" && i + 1 < argc) {
            config.framesPerTest = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--backend" && i + 1 < argc) {
            std::string be = argv[++i];
            g_useSpatialHash = (be == "hash" || be == "spatial" || be == "spatialhash");
        } else if (arg == "--counts" && i + 1 < argc) {
            // 子集运行�?-counts 1024,4096,65536
            config.particleCounts.clear();
            std::string countsArg(argv[++i]);
            std::stringstream ss(countsArg);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (!tok.empty())
                    config.particleCounts.push_back(
                        static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 10)));
            }
            if (config.particleCounts.empty())
                config.particleCounts = {1024, 4096, 16384, 65536, 262144};
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        }
    }
    config.cpuThreadCount = threadCount;

    s_Log.Info("Config: collision={}, threads={}, frames={}",
               config.enableCollision, threadCount, config.framesPerTest);

    // ── 初始�?GPU 上下文（v3.0 Phase 0）──
    // 隐藏 GLFW 窗口 + glad + GL46Device；无 GL 环境时自动降级为 CPU-only
    printf("Initializing GPU context...\n");
    gpuctx::Init();
    if (!gpuctx::valid) {
        s_Log.Warn("GPU context unavailable - GPU benchmarks will be N/A");
    }

    // ── 打印系统信息 ──
    // 需�?Application 完全初始化以获取 GL 驱动信息
    // 此处跳过，在完整集成时通过 RHI 设备获取

    // ── 运行所有量级的测试 ──
    std::vector<BenchmarkResult> results;
    results.reserve(config.particleCounts.size());

    // 打开 CSV 输出
    std::ofstream csvFile(config.outputPath);
    if (csvFile.is_open()) {
        PrintCSVHeader(csvFile);
        s_Log.Info("CSV output: {}", config.outputPath);
    } else {
        s_Log.Warn("Cannot open CSV: {}", config.outputPath);
    }

    s_Log.Info("");
    s_Log.Info("Running benchmarks...");

    for (size_t idx = 0; idx < config.particleCounts.size(); ++idx) {
        uint32_t count = config.particleCounts[idx];

        // 如果碰撞开启且粒子�?> 16384，CPU 单线程会极慢，跳�?
        bool runCPU = true;
        if (config.enableCollision && count > 16384) {
            s_Log.Warn("Skipping CPU single-thread for {} particles (collision O(N²) too slow)", count);
            // 仍然跑多线程，但跳过单线�?
        }

        s_Log.Info("[{}/{}] Testing {} particles...",
                   idx + 1, config.particleCounts.size(), count);

        auto result = RunSingleBenchmark(
            count, config.framesPerTest, threadCount,
            config.enableCollision);

        results.push_back(result);

        // 输出�?CSV
        if (csvFile.is_open()) {
            PrintCSVRow(csvFile, result);
        }

        // 打印单行摘要
        printf("  %6u: CPU1T=%.1fms CPU%dT=%.1fms GPU=%.1fms | Speedup: MT=%.1fx GPU=%.1fx\n",
               count,
               result.cpuSingleMs, threadCount, result.cpuMultiMs,
               result.gpuComputeMs,
               result.speedupMulti,
               result.speedupGPU);
    }

    csvFile.close();

    // ── 打印结果 ──
    PrintHumanReadable(results);

    // ── 清理 ──
    gpuctx::Shutdown();

    s_Log.Info("");
    s_Log.Info("Benchmark complete. Results saved to: {}", config.outputPath);
    s_Log.Info("");
    s_Log.Info("Upload this CSV to a spreadsheet or use the analysis script:");
    s_Log.Info("  python tools/analyze_benchmark.py {}", config.outputPath);

    return 0;
}

// ══════════════════════════════════════════════════════════�?
// 简易入口（如果作为独立程序编译�?
// ══════════════════════════════════════════════════════════�?

#ifndef ENGINE_MAIN_INJECTED
int main(int argc, char** argv) {
    return RunPhysicsBenchmark(argc, argv);
}
#endif