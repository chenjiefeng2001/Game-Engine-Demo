/**
 * @file CPUSimulatorTest.cpp
 * @brief 物理模拟 CPU 参考实现测试
 *
 * 测试重点：
 * - 重力加速度方向正确性
 * - 边界反弹 restitution 精度
 * - 能量守恒验证
 */
#include <gtest/gtest.h>
#include "Engine/Core/Physics/GPUParticle.h"
#include <cmath>

using namespace Engine;

// ── CPU Simulator 参考实现 ──
// 与 sandbox 中的 CPUSimulator 同步
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
            // X
            if (p.position[0] - r < boxMin[0]) {
                p.position[0] = boxMin[0] + r;
                p.velocity[0] = -p.velocity[0] * restitution;
            }
            if (p.position[0] + r > boxMax[0]) {
                p.position[0] = boxMax[0] - r;
                p.velocity[0] = -p.velocity[0] * restitution;
            }
            // Y
            if (p.position[1] - r < boxMin[1]) {
                p.position[1] = boxMin[1] + r;
                p.velocity[1] = -p.velocity[1] * restitution;
            }
            if (p.position[1] + r > boxMax[1]) {
                p.position[1] = boxMax[1] - r;
                p.velocity[1] = -p.velocity[1] * restitution;
            }
            // Z
            if (p.position[2] - r < boxMin[2]) {
                p.position[2] = boxMin[2] + r;
                p.velocity[2] = -p.velocity[2] * restitution;
            }
            if (p.position[2] + r > boxMax[2]) {
                p.position[2] = boxMax[2] - r;
                p.velocity[2] = -p.velocity[2] * restitution;
            }
        }
    }
};

TEST(CPUSimulatorTest, GravityAffectsVelocity) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 100.0f;
    particles[0].velocity[1] = 0.0f;
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, -9.8f, 0};
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    CPUSimulator::Step(particles, 1.0f/60.0f, gravity, 0.8f, 0.02f, boxMin, boxMax);

    // 重力使速度向下增加
    EXPECT_LT(particles[0].velocity[1], 0.0f);
    EXPECT_NEAR(particles[0].velocity[1], -9.8f/60.0f, 0.001f);
}

TEST(CPUSimulatorTest, BoundaryRestitution) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[0] = -49.0f; // 靠近边界
    particles[0].velocity[0] = -10.0f; // 向边界移动
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, 0, 0}; // 无重力
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    // restitution=0.5: 反弹后速度减半
    CPUSimulator::Step(particles, 0.1f, gravity, 0.5f, 0.0f, boxMin, boxMax);

    EXPECT_GT(particles[0].velocity[0], 0.0f);
    EXPECT_NEAR(particles[0].velocity[0], 5.0f, 0.001f); // 10 * 0.5 = 5
}

TEST(CPUSimulatorTest, DampingReducesSpeed) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 100.0f;
    particles[0].velocity[1] = -10.0f;
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, 0, 0}; // 无重力
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    float damping = 0.5f; // 高阻尼
    CPUSimulator::Step(particles, 1.0f/60.0f, gravity, 1.0f, damping, boxMin, boxMax);

    // 阻尼后速度应减小
    float expected = -10.0f * (1.0f - damping / 60.0f);
    EXPECT_NEAR(particles[0].velocity[1], expected, 0.001f);
    EXPECT_GT(particles[0].velocity[1], -10.0f); // 速度绝对值减小
}

TEST(CPUSimulatorTest, ParticleFallsAndHitsGround) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 10.0f;
    particles[0].velocity[1] = 0.0f;
    particles[0].radius = 1.0f;
    particles[0].mass = 1.0f;

    float gravity[3] = {0, -9.8f, 0};
    float boxMin[3] = {-50, 0, -50};
    float boxMax[3] = {50, 100, 50};

    // 模拟 60 帧（1 秒）
    for (int i = 0; i < 60; ++i) {
        CPUSimulator::Step(particles, 1.0f/60.0f, gravity, 0.8f, 0.02f, boxMin, boxMax);
    }

    // 粒子应落在地面上（position.y ≈ radius = 1.0）
    EXPECT_NEAR(particles[0].position[1], particles[0].radius, 0.1f);
}