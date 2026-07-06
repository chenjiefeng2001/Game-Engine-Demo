/**
 * @file ECSPerformanceTest.cpp
 * @brief ECS 性能基准测试（纯 CPU，与渲染后端无关）
 *
 * 测试内容：
 *   1. ECS 实体创建性能：创建 10,000 个球体实体
 *   2. ECS 查询迭代性能：遍历所有实体更新位置（模拟 100 帧运动）
 *   3. 内存与性能统计打印
 */

#include "Rendering3DTest.h"

#include <Engine/Core/ECS/ECS.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/RHI/VulkanIRHIDevice.h>
#include <Engine/Core/RHI/D3D12IRHIDevice.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>

namespace Engine {

// ═══════════════════════════════════════════════════════════════
// ECS 组件类型（必须在 namespace 作用域，用于 ComponentType<T>::ID() 注册）
// ═══════════════════════════════════════════════════════════════

struct Position3D {
    Vec3 pos;
};

struct SphereDataComponent {
    Vec3 velocity;      // 运动速度
    Vec3 homePosition;  // 初始位置（用于摆动计算）
    float phase;        // 相位偏移
    float amplitude;    // 摆动振幅
};

// ═══════════════════════════════════════════════════════════════
// ECS 创建 + 迭代性能测试
// ═══════════════════════════════════════════════════════════════

struct ECSPerfResult {
    double createTime_ms = 0.0;
    double queryTime_ms  = 0.0;
    double totalTime_ms  = 0.0;
    uint32 entityCount   = 0;
    bool   passed        = false;
};

static ECSPerfResult RunECSPerformanceTest(const char* backendName) {
    ECSPerfResult result;
    auto startTotal = std::chrono::high_resolution_clock::now();

    // ── 注册 ECS 组件类型（必须在使用前注册） ──
    RegisterComponentType<Position3D>();
    RegisterComponentType<SphereDataComponent>();

    // ── 创建 EntityManager ──
    EntityManager em;

    // ── 批量创建 10,000 个实体 ──
    constexpr uint32 kEntityCount = 10000;
    std::vector<EntityHandle> entities;
    entities.reserve(kEntityCount);

    auto startCreate = std::chrono::high_resolution_clock::now();

    std::mt19937 rng(42); // 固定种子，可复现
    for (uint32 i = 0; i < kEntityCount; ++i) {
        EntityHandle e = em.CreateEntity();

        float x = (float(i % 100) - 50.0f) * 2.0f;
        float y = (float((i / 100) % 100) - 50.0f) * 2.0f;
        float z = (float(i / 10000) - 0.5f) * 2.0f;

        em.AddComponent<Position3D>(e, Position3D{{x, y, z}});

        SphereDataComponent sfc;
        sfc.velocity     = Vec3(0, 0, 0);
        sfc.homePosition = Vec3(x, y, z);
        sfc.phase        = std::uniform_real_distribution<float>(0, 6.2832f)(rng);
        sfc.amplitude    = std::uniform_real_distribution<float>(0.5f, 3.0f)(rng);
        em.AddComponent<SphereDataComponent>(e, sfc);

        entities.push_back(e);
    }

    auto endCreate = std::chrono::high_resolution_clock::now();
    result.createTime_ms = std::chrono::duration<double, std::milli>(endCreate - startCreate).count();
    result.entityCount = kEntityCount;

    // ── 查询迭代：更新所有球体位置（模拟 100 帧运动） ──
    auto startQuery = std::chrono::high_resolution_clock::now();

    auto query = em.Query()
        .With<Position3D>()
        .With<SphereDataComponent>()
        .Build();

    constexpr int kIterationCount = 100; // 模拟 100 帧
    float time = 0.0f;

    for (int frame = 0; frame < kIterationCount; ++frame) {
        time += 1.0f / 60.0f; // 每帧 16.67ms

        // 遍历查询结果：每个 ChunkRange 包含 chunk 指针、起始行和实体数量
        for (const auto& range : query) {
            Chunk* chunk = range.chunk;
            uint32 startRow = range.startRow;
            uint32 count = range.count;

            auto positions = chunk->GetComponentSpan<Position3D>();
            auto sphereData = chunk->GetComponentSpan<SphereDataComponent>();

            for (uint32 i = 0; i < count; ++i) {
                auto& pos = positions[startRow + i];
                auto& sd  = sphereData[startRow + i];

                // 球体做正弦摆动
                float wave = sinf(time * 2.0f + sd.phase) * sd.amplitude;
                pos.pos.x = sd.homePosition.x + wave;
                pos.pos.y = sd.homePosition.y + cosf(time * 1.5f + sd.phase * 0.7f) * sd.amplitude * 0.5f;
            }
        }
    }

    auto endQuery = std::chrono::high_resolution_clock::now();
    result.queryTime_ms = std::chrono::duration<double, std::milli>(endQuery - startQuery).count();

    auto endTotal = std::chrono::high_resolution_clock::now();
    result.totalTime_ms = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

    // ── 验证数据完整性 ──
    result.passed = true;
    if (em.GetEntityCount() != kEntityCount) {
        std::fprintf(stderr, "[%s] ECS: Entity count mismatch: %u != %u\n",
                     backendName, em.GetEntityCount(), kEntityCount);
        result.passed = false;
    }

    if (!query.IsValid() || query.Size() == 0) {
        std::fprintf(stderr, "[%s] ECS: Query returned no results!\n", backendName);
        result.passed = false;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// 性能打印
// ═══════════════════════════════════════════════════════════════

static void PrintECSPerfResult(const char* backendName, const ECSPerfResult& r) {
    std::printf("  [ECS %s] %u entities: create=%.2fms, iterate(%d frames)=%.2fms, total=%.2fms\n",
                backendName, r.entityCount, r.createTime_ms, 100, r.queryTime_ms, r.totalTime_ms);
}

// ═══════════════════════════════════════════════════════════════
// 公共入口
// ═══════════════════════════════════════════════════════════════

bool Rendering3DTest::TestECSOnly(RHI::IRHIDevice* device, const char* backendName) {
    (void)device; // 纯 CPU 测试，与后端无关

    ECSPerfResult ecsResult = RunECSPerformanceTest(backendName);
    PrintECSPerfResult(backendName, ecsResult);

    return ecsResult.passed;
}

} // namespace Engine