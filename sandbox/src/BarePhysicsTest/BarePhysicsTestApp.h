#pragma once

/**
 * @file BarePhysicsTestApp.h
 * @brief 自制物理引擎 (Bare2DPhysicsWorld) 完整功能测试
 *
 * 测试内容：
 *   1. 重力 + 力发生器
 *   2. Circle/Box/Polygon 碰撞
 *   3. SAT 多边形碰撞
 *   4. Sleeping 休眠机制
 *   5. Warm Starting + 约束求解器
 *   6. 光线投射
 *   7. 形状投射 (Shapecast)
 *   8. Trigger 传感器
 *   9. 材质系统 (摩擦/弹性)
 *   10. CCD 连续碰撞检测
 */

#include <Engine/Core/Physics/Bare2DPhysicsWorld.h>
#include <iostream>
#include <cmath>
#include <cassert>

namespace Engine {

    class BarePhysicsTestApp {
    public:
        BarePhysicsTestApp();
        ~BarePhysicsTestApp() = default;

        /// 运行所有测试，返回 true 表示全部通过
        bool RunAllTests();

    private:
        // ── 测试用例 ──
        bool TestGravityAndForceGenerators();
        bool TestCircleCollision();
        bool TestBoxCollision();
        bool TestPolygonCollision();    // SAT 测试
        bool TestStackingStability();   // 堆叠 + Warm Starting + Sleeping
        bool TestSleeping();
        bool TestRayCast();
        bool TestShapeCast();
        bool TestTriggerSensor();
        bool TestMaterialSystem();
        bool TestCCD();
        bool TestBodyTypeChange();
        bool TestMultiFixture();
        bool TestCollisionFiltering();

        // ── 断言辅助 ──
        bool Assert(bool condition, const std::string& testName, const std::string& message);

        int32  m_Passed  = 0;
        int32  m_Failed  = 0;
        float32 m_Epsilon = 1e-5f;

        // ── 配置 ──
        static constexpr float32 FIXED_DT = 1.0f / 60.0f;
    };

} // namespace Engine