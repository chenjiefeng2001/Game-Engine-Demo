/**
 * @file CPUSimulatorTest.cpp
 * @brief 物理模拟 CPU 参考实现测试
 *
 * 测试重点：
 * - 重力加速度方向正确性（稳健相对断言）
 * - 边界反弹 restitution 精度
 * - 阻尼效果验证
 */
#include <gtest/gtest.h>
#include "Engine/Core/Physics/GPUParticle.h"
#include <cmath>
#include <cstdio>

using namespace Engine;

// ── CPU Simulator 参考实现 ──
class CPUSimulator {
public:
    static void Step(std::vector<GPUParticleData>& particles, float dt,
                     const float gravity[3], float restitution, float damping,
                     const float boxMin[3], const float boxMax[3]) {
        for (auto& p : particles) {
            p.velocity[0] += gravity[0] * dt;
            p.velocity[1] += gravity[1] * dt;
            p.velocity[2] += gravity[2] * dt;
            p.velocity[0] *= (1.0f - damping * dt);
            p.velocity[1] *= (1.0f - damping * dt);
            p.velocity[2] *= (1.0f - damping * dt);
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;
            float r = p.radius;
            if (p.position[0] - r < boxMin[0]) { p.position[0] = boxMin[0] + r; p.velocity[0] = -p.velocity[0] * restitution; }
            if (p.position[0] + r > boxMax[0]) { p.position[0] = boxMax[0] - r; p.velocity[0] = -p.velocity[0] * restitution; }
            if (p.position[1] - r < boxMin[1]) { p.position[1] = boxMin[1] + r; p.velocity[1] = -p.velocity[1] * restitution; }
            if (p.position[1] + r > boxMax[1]) { p.position[1] = boxMax[1] - r; p.velocity[1] = -p.velocity[1] * restitution; }
            if (p.position[2] - r < boxMin[2]) { p.position[2] = boxMin[2] + r; p.velocity[2] = -p.velocity[2] * restitution; }
            if (p.position[2] + r > boxMax[2]) { p.position[2] = boxMax[2] - r; p.velocity[2] = -p.velocity[2] * restitution; }
        }
    }
};

TEST(CPUSimulatorTest, GravityPullsParticleDown) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 100.0f;
    particles[0].velocity[1] = 0.0f;
    particles[0].mass = 1.0f;

    float initialVy = particles[0].velocity[1];
    float gravity[3] = {0, -9.8f, 0};
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    CPUSimulator::Step(particles, 1.0f / 60.0f, gravity, 0.8f, 0.02f, boxMin, boxMax);

    std::printf("    [Gravity] vy before=%.4f vy after=%.4f\n", initialVy, particles[0].velocity[1]);

    EXPECT_LT(particles[0].velocity[1], initialVy)
        << "Gravity should increase downward velocity";
    EXPECT_LT(particles[0].velocity[1], 0.0f);
}

TEST(CPUSimulatorTest, BoundaryRestitution) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[0] = -49.0f;
    particles[0].velocity[0] = -10.0f;
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, 0, 0};
    float dt = 0.1f;
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    CPUSimulator::Step(particles, dt, gravity, 0.5f, 0.0f, boxMin, boxMax);

    std::printf("    [Restitution] vx after=%.4f (expected +5.0)\n", particles[0].velocity[0]);
    EXPECT_GT(particles[0].velocity[0], 0.0f);
    EXPECT_NEAR(particles[0].velocity[0], 5.0f, 0.001f);
}

TEST(CPUSimulatorTest, DampingReducesSpeed) {
    std::vector<GPUParticleData> particles(1);
    particles[0].velocity[1] = -20.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, 0, 0};
    float dt = 1.0f / 60.0f;
    float damping = 0.5f;
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    float speedBefore = std::abs(particles[0].velocity[1]);
    CPUSimulator::Step(particles, dt, gravity, 1.0f, damping, boxMin, boxMax);
    float speedAfter = std::abs(particles[0].velocity[1]);

    std::printf("    [Damping] speed before=%.4f after=%.4f\n", speedBefore, speedAfter);
    EXPECT_LT(speedAfter, speedBefore);
}

TEST(CPUSimulatorTest, ParticleFallsAndHitsGround) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 10.0f;
    particles[0].velocity[1] = 0.0f;
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, -9.8f, 0};

    // 500 帧，足够静止在地面附近
    for (int i = 0; i < 500; ++i) {
        float boxMin[3] = {-50, 0, -50};
        float boxMax[3] = {50, 100, 50};
        CPUSimulator::Step(particles, 1.0f / 60.0f, gravity, 0.8f, 0.02f, boxMin, boxMax);
    }

    std::printf("    [Fall] pos.y=%.4f (radius=%.2f)\n", particles[0].position[1], particles[0].radius);
    // 粒子应在地面附近（阻尼+restitution 导致最终静止在高于半径的位置）
    EXPECT_LT(particles[0].position[1], 5.0f);
    EXPECT_GT(particles[0].position[1], 0.5f);
}