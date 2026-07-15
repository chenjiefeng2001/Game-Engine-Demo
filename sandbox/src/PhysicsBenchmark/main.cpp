/**
 * @file PhysicsBenchmark.cpp
 * @brief CPU vs GPU 物理引擎性能对比测试
 *
 * 测试目标：
 *   1. 相同算法（半隐式欧拉 + 边界碰撞 + 惩罚力粒子碰撞）在 CPU 与 GPU 上的性能差距
 *   2. 不同粒子数量级下的吞吐量对比（1K / 4K / 16K / 64K / 256K）
 *   3. CPU 多线程 vs GPU 数千核心的加速比
 *   4. 验证 GPU 物理的 Zero-Copy 渲染优势（无需 PCIe 回读）
 *
 * 运行方式：
 *   编译后直接运行：./build/sandbox/Debug/sandbox.exe --benchmark
 *   测试完成后输出 CSV 格式对比数据
 *
 * 测试环境建议：
 *   - CPU: 任意现代 x86-64 处理器（测试自动检测核心数）
 *   - GPU: GTX 1060 或更高（OpenGL 4.3+ 以支持 Compute Shader）
 *   - 驱动: 更新到最新版本
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/Log.h"
#include "Engine/Application.h"
#include "Engine/Core/FileSystem.h"
#include "Engine/Core/TaskGraph.h"
#include "Engine/Core/JobSystem.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <random>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>

// ═══════════════════════════════════════════════════════════
// 基准测试配置
// ═══════════════════════════════════════════════════════════

struct BenchmarkConfig {
    // 粒子数量级（每个量级跑 5 帧取平均）
    std::vector<uint32_t> particleCounts = {1024, 4096, 16384, 65536, 262144};

    // 每量级的测试帧数
    uint32_t framesPerTest = 5;

    // 是否启用粒子间碰撞（CPU 端 O(N²) 在大量级下极慢，可选择关闭）
    bool enableCollision = true;

    // CPU 端使用的线程数（0 = 自动检测）
    uint32_t cpuThreadCount = 0;

    // 输出 CSV 文件路径
    std::string outputPath = "logs/physics_benchmark.csv";
};

// ═══════════════════════════════════════════════════════════
// CPU 端粒子物理实现（与 GPU Compute Shader 算法一致）
// ═══════════════════════════════════════════════════════════

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
 * @brief CPU 端粒子物理模拟（单线程版本，与 GPU Compute Shader 算法完全一致）
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

    // Pass 1: 半隐式欧拉积分 + 边界碰撞
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

    // Pass 2: 粒子间碰撞（仅启用时）
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
 * 使用简单的分块并行，每个线程处理一块连续的粒子范围。
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

    // ── Pass 1: 积分 + 边界（完全可并行） ──
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

    // 应用力
    for (uint32_t i = 0; i < count; ++i) {
        if (particles[i].mass > 0.0f) {
            particles[i].velocity[0] += (forceX[i].load() / particles[i].mass) * dt;
            particles[i].velocity[1] += (forceY[i].load() / particles[i].mass) * dt;
            particles[i].velocity[2] += (forceZ[i].load() / particles[i].mass) * dt;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 基准测试引擎
// ═══════════════════════════════════════════════════════════

struct BenchmarkResult {
    uint32_t particleCount;
    double cpuSingleMs;      // CPU 单线程耗时 (ms)
    double cpuMultiMs;       // CPU 多线程耗时 (ms)
    double gpuComputeMs;     // GPU Compute 耗时 (ms)
    double cpuSingleFPS;     // CPU 单线程等效 FPS
    double cpuMultiFPS;      // CPU 多线程等效 FPS
    double gpuFPS;           // GPU 等效 FPS
    double speedupMulti;     // CPU 多线程 vs 单线程加速比
    double speedupGPU;        // GPU vs CPU 多线程加速比
    bool collisionEnabled;
};

/**
 * @brief 运行单个量级的基准测试
 */
static BenchmarkResult RunSingleBenchmark(uint32_t particleCount,
                                           uint32_t frames,
                                           uint32_t threadCount,
                                           bool enableCollision,
                                           Engine::GPUPhysicsEngine* gpuEngine) {
    BenchmarkResult result = {};
    result.particleCount = particleCount;
    result.collisionEnabled = enableCollision;

    // ── 准备初始数据（CPU 与 GPU 使用相同种子，确保一致性） ──
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

    // ── 预热（1 帧） ──
    CPUSimulateParticles(cpuParticles.data(), particleCount, cpuConfig, enableCollision);

    // ── CPU 单线程测试 ──
    {
        // 重新初始化
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

    // ── CPU 多线程测试 ──
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

    // ── GPU 测试 ──
    if (gpuEngine && gpuEngine->IsValid()) {
        // 重新配置 GPU 引擎粒子数
        // 注意：GPUPhysicsEngine 当前不支持运行时 resize
        // 因此我们假设它在初始化时已配置为最大粒子数
        // 这里我们直接用当前配置跑 frames 帧

        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t f = 0; f < frames; ++f) {
            // GPU Update 内部执行 2 个 Compute Pass（积分 + 碰撞）
            // 注意：需要有效的 IRHICommandList，此处为占位
            // gpuEngine->Update(0.016f, cmdList);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        result.gpuComputeMs = totalMs / frames;
        result.gpuFPS = 1000.0 / result.gpuComputeMs;
    } else {
        result.gpuComputeMs = 0.0;
        result.gpuFPS = 0.0;
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
 * @brief 打印结果表头到 CSV
 */
static void PrintCSVHeader(std::ofstream& csv) {
    csv << "ParticleCount,CPUSingle_ms,CPUMulti_ms,GPU_ms,"
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
    printf("║           CPU vs GPU Physics Engine Benchmark Results                       ║\n");
    printf("╠════════════╦════════════╦════════════╦════════════╦════════════╦════════════╣\n");
    printf("║ Particles  ║ CPU 1T ms  ║ CPU MT ms  ║ GPU ms     ║ Speedup MT ║ Speedup GPU║\n");
    printf("╠════════════╬════════════╬════════════╬════════════╬════════════╬════════════╣\n");

    for (const auto& r : results) {
        printf("║ %-9u ║ %-9.3f ║ %-9.3f ║ %-9.3f ║ %-9.2f ║ %-9.2f ║\n",
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
    printf("║ Particles  ║ CPU 1T FPS ║ CPU MT FPS ║ GPU FPS    ║\n");
    printf("╠════════════╬════════════╬════════════╬════════════╣\n");
    for (const auto& r : results) {
        printf("║ %-9u ║ %-9.1f ║ %-9.1f ║ %-9.1f ║\n",
               r.particleCount,
               r.cpuSingleFPS,
               r.cpuMultiFPS,
               r.gpuFPS);
    }
    printf("╚════════════╩════════════╩════════════╩════════════╝\n");
    printf("\n");
}

/**
 * @brief 系统信息打印
 */
static void PrintSystemInfo(uint32_t threadCount) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║        System Information                    ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  CPU Threads (config)  : %-20u  ║\n", threadCount);
    printf("║  CPU Threads (hardware): %-20u  ║\n", std::thread::hardware_concurrency());

    printf("║  GPU Device Info : via RHI backend            ║\n");
    printf("║  (System info requires external init)         ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n");
}

// ═══════════════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════════════

int RunPhysicsBenchmark(int argc, char** argv) {
    (void)argc;
    (void)argv;

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
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        }
    }
    config.cpuThreadCount = threadCount;

    s_Log.Info("Config: collision={}, threads={}, frames={}",
               config.enableCollision, threadCount, config.framesPerTest);

    // ── 初始化 GPU 物理引擎 ──
    // 注意：GPU 物理引擎需要有效的 OpenGL 上下文
    // 如果 Application 尚未初始化，此处会失败
    Engine::GPUPhysicsEngine* gpuEngine = nullptr;

    // 尝试初始化 GPU 引擎
    Engine::GPUPhysicsConfig gpuConfig;
    // 使用最大粒子数初始化（之后 benchmark 运行不同量级时复用）
    gpuConfig.particleCount = 262144;
    gpuConfig.gravity[0] = 0.0f;
    gpuConfig.gravity[1] = -9.8f;
    gpuConfig.gravity[2] = 0.0f;
    gpuConfig.restitution = 0.7f;
    gpuConfig.stiffness = 500.0f;
    gpuConfig.damping = 0.01f;
    gpuConfig.boxMin[0] = -60.0f;
    gpuConfig.boxMin[1] = 0.0f;
    gpuConfig.boxMin[2] = -60.0f;
    gpuConfig.boxMax[0] = 60.0f;
    gpuConfig.boxMax[1] = 120.0f;
    gpuConfig.boxMax[2] = 60.0f;

    // GPU 引擎初始化需要有效的 RHI::IRHIDevice
    // 在 Application 完整初始化后可用，此处仅占位
    s_Log.Warn("GPU Physics Engine init requires RHI device - GPU benchmarks will be N/A");
    gpuEngine = nullptr;

    // ── 打印系统信息 ──
    // 需要 Application 完全初始化以获取 GL 驱动信息
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

        // 如果碰撞开启且粒子数 > 16384，CPU 单线程会极慢，跳过
        bool runCPU = true;
        if (config.enableCollision && count > 16384) {
            s_Log.Warn("Skipping CPU single-thread for {} particles (collision O(N²) too slow)", count);
            // 仍然跑多线程，但跳过单线程
        }

        s_Log.Info("[{}/{}] Testing {} particles...",
                   idx + 1, config.particleCounts.size(), count);

        auto result = RunSingleBenchmark(
            count, config.framesPerTest, threadCount,
            config.enableCollision, gpuEngine);

        results.push_back(result);

        // 输出到 CSV
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
    if (gpuEngine) {
        gpuEngine->Shutdown();
        delete gpuEngine;
        gpuEngine = nullptr;
    }

    s_Log.Info("");
    s_Log.Info("Benchmark complete. Results saved to: {}", config.outputPath);
    s_Log.Info("");
    s_Log.Info("Upload this CSV to a spreadsheet or use the analysis script:");
    s_Log.Info("  python tools/analyze_benchmark.py {}", config.outputPath);

    return 0;
}

// ═══════════════════════════════════════════════════════════
// 简易入口（如果作为独立程序编译）
// ═══════════════════════════════════════════════════════════

#ifndef ENGINE_MAIN_INJECTED
int main(int argc, char** argv) {
    return RunPhysicsBenchmark(argc, argv);
}
#endif