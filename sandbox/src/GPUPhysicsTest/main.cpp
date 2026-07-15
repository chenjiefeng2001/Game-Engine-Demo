/**
 * @file GPUPhysicsTest.cpp
 * @brief GPU 物理引擎沙盒测试 — 粒子位置回读 + 物理规律断言验证
 *
 * 验证目标：
 *   1. 粒子数据能正确上传到 RHI Buffer
 *   2. 重力作用下粒子 Y 坐标应随帧下降（物理规律断言）
 *   3. 边界碰撞反弹：粒子应被限制在 box 范围内
 *   4. 每帧统计粒子的平均位置/速度（验证 Compute 是否在运行）
 */

#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/Log.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>
#include <random>

static Engine::Logger s_Log("GPUPhysicsTest");

static constexpr float DT = 1.0f / 60.0f;
static constexpr int MAX_FRAMES = 120;

static void ValidateParticles(const std::vector<Engine::GPUParticleData>& particles,
                               int frame, float boxMinY, float boxMaxY)
{
    if (particles.empty()) return;

    double avgY = 0, avgVelY = 0, avgSpeed = 0;
    int alive = 0;
    for (const auto& p : particles) {
        avgY += p.position[1];
        avgVelY += p.velocity[1];
        float vx = p.velocity[0], vy = p.velocity[1], vz = p.velocity[2];
        avgSpeed += std::sqrt(vx*vx + vy*vy + vz*vz);
        // 检查边界约束：位置不应超出 box 范围（由于 Restitution，允许边界接触）
        if (p.position[1] > boxMinY - 0.01f && p.position[1] < boxMaxY + 0.01f)
            ++alive;
    }
    size_t n = particles.size();
    avgY /= n;
    avgVelY /= n;
    avgSpeed /= n;

    // 打印粒子状态摘要（首粒位置 + 总体统计）
    printf("  Frame %d | First particle pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)\n",
           frame,
           particles[0].position[0], particles[0].position[1], particles[0].position[2],
           particles[0].velocity[0], particles[0].velocity[1], particles[0].velocity[2]);
    printf("  Avg Y=%.2f Avg VelY=%.2f Avg Speed=%.2f InBounds=%d/%zu\n",
           avgY, avgVelY, avgSpeed, alive, n);

    // 物理规律断言
    if (frame == MAX_FRAMES - 1) {
        // 运行 120 帧后，受重力影响的粒子平均 Y 应显著下降
        // 初始 avgY ≈ 10~50（spawnRadius=40，abs(pos)+10），
        // 经过 120 帧 * 9.8 重力，期望 avgY 显著降低
        if (avgY > 10.0f) {
            s_Log.Warn("Particles appear to not be falling - gravity may not be applied");
        } else {
            s_Log.Info("Physics VERIFIED: Particles falling under gravity (avg Y=%.2f)", avgY);
        }
        // 边界约束：至少 90% 粒子应在 box 范围内
        if (alive < n * 0.9f) {
            s_Log.Warn("Boundary violations detected: {}/{} particles outside box", n - alive, n);
        } else {
            s_Log.Info("Boundary constraint VERIFIED: {}/{} in bounds", alive, n);
        }
    }
}

int main(int, char**) {
    s_Log.Info("========================================");
    s_Log.Info(" GPU Physics Engine - Readback + Validation");
    s_Log.Info("========================================");

    // 1. 初始化 GL46 设备
    auto device = std::make_unique<Engine::RHI::GL46Device>();
    if (!device->Initialize(nullptr, 1, 1)) {
        s_Log.Error("GL46Device init failed");
        return 1;
    }
    s_Log.Info("Device: {}", device->GetDeviceName());

    // 2. 初始化 GPU 物理引擎
    Engine::GPUPhysicsConfig config;
    config.particleCount = 65536;
    config.gravity[0] = 0.0f;
    config.gravity[1] = -9.8f;
    config.gravity[2] = 0.0f;
    config.restitution = 0.7f;
    config.stiffness = 500.0f;
    config.damping = 0.01f;
    config.boxMin[0] = -60.0f;
    config.boxMin[1] = 0.0f;
    config.boxMin[2] = -60.0f;
    config.boxMax[0] = 60.0f;
    config.boxMax[1] = 120.0f;
    config.boxMax[2] = 60.0f;
    config.spawnVelocity[0] = 0.0f;
    config.spawnVelocity[1] = 10.0f;
    config.spawnVelocity[2] = 0.0f;
    config.spawnRadius = 40.0f;
    config.workGroupSize = 256;

    auto physics = std::make_unique<Engine::GPUPhysicsEngine>();
    if (!physics->Initialize(device.get(), config)) {
        s_Log.Error("GPUPhysicsEngine init failed");
        return 1;
    }
    s_Log.Info("Engine init OK: {} particles", config.particleCount);

    // 3. 模拟主循环
    printf("\n--- Running {} frames ---\n\n", MAX_FRAMES);
    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        auto cmdList = device->CreateCommandList(Engine::RHI::CommandListType::Direct);
        if (!cmdList) break;

        cmdList->Begin();
        physics->Update(DT, cmdList.get());
        cmdList->End();

        auto* queue = device->GetQueue(Engine::RHI::QueueType::Graphics);
        if (queue) {
            Engine::RHI::IRHICommandList* lists[] = { cmdList.get() };
            queue->ExecuteCommandLists(1, lists);
            queue->WaitIdle();
        }

        // 每隔 30 帧回读粒子数据并验证
        if (frame % 30 == 0 || frame == MAX_FRAMES - 1) {
            // SSBO 回读前 16 个粒子
            std::vector<Engine::GPUParticleData> readback(16);
            physics->ReadbackParticles(0, 16, readback.data());
            ValidateParticles(readback, frame,
                              config.boxMin[1], config.boxMax[1]);
        }
    }

    // 4. 最终回读更多样本做统计分析
    printf("\n--- Final Readback (first 256 particles) ---\n");
    std::vector<Engine::GPUParticleData> finalSample(256);
    physics->ReadbackParticles(0, 256, finalSample.data());
    double finalAvgY = 0;
    for (auto& p : finalSample) finalAvgY += p.position[1];
    finalAvgY /= finalSample.size();
    printf("  Final avg particle Y: %.2f\n", finalAvgY);

    if (finalAvgY < 10.0f) {
        s_Log.Info("SUCCESS: Physics simulation running correctly (avg Y=%.2f < 10)", finalAvgY);
    } else {
        s_Log.Warn("Physics may not be computing: avg Y=%.2f (expect < 10 after gravity)", finalAvgY);
    }

    physics->Shutdown();
    s_Log.Info("Test complete");
    return 0;
}