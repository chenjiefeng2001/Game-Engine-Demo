#pragma once

/**
 * @file AnimationTestApp.h
 * @brief 动画系统综合测试 — 验证动画控制器/状态机/约束/混合等功能
 *
 * 测试内容：
 *   1. AnimationController — 层管理、全局参数、高电平 API
 *   2. AnimStateMachine — 过渡、子状态机、AnyState、状态行为
 *   3. 动画插槽 — 播放、淡入淡出、循环
 *   4. 约束系统 — 定位器、Point/Orient/Parent/LookAt 约束
 *   5. BlendTree — 混合树求值
 *   6. 跨物体对接 — 定位器导入/导出
 *
 * 运行方式：
 *   cmake --build build --target AnimationTest
 *   ./build/sandbox/AnimationTest/AnimationTest.exe
 */

#include <Engine/Types.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/RHI/MathTypes.h>
#include <Engine/Animation/Skeleton.h>
#include <Engine/Animation/AnimationBlend.h>
#include <Engine/Animation/AnimationPose.h>
#include <Engine/Animation/AnimationController.h>
#include <Engine/Animation/ConstraintSolver.h>
#include <Engine/Animation/AnimationBatch.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <iostream>

namespace Engine {

    class AnimationTestApp {
    public:
        /** 运行所有测试，返回 true 表示全部通过 */
        static bool RunAll();

        // 测试计数器
        static int s_Passed;
        static int s_Failed;

    private:
        // ── 测试辅助 ──
        static void Check(bool condition, const std::string& testName);
        static bool Vec3Approx(const Vec3& a, const Vec3& b, float32 eps = 1e-4f);
        static bool QuatApprox(const Quat& a, const Quat& b, float32 eps = 1e-4f);
        static bool Mat4Approx(const Mat4& a, const Mat4& b, float32 eps = 1e-4f);

        // ── 工具函数：创建测试用骨骼 ──
        static std::shared_ptr<Skeleton> CreateTestSkeleton();
        static std::shared_ptr<AnimationResource> CreateTestResource();

        // ── 1. AnimationController 测试 ──
        static bool TestControllerLayerManagement();
        static bool TestControllerGlobalParams();
        static bool TestControllerCrossFade();
        static bool TestControllerSlots();
        static bool TestControllerEvents();

        // ── 2. AnimStateMachine 测试 ──
        static bool TestStateMachineTransitions();
        static bool TestStateMachineAnyState();
        static bool TestSubStateMachine();
        static bool TestStateBehaviors();

        // ── 3. 约束系统测试 ──
        static bool TestLocatorBoneAttached();
        static bool TestLocatorWorldSpace();
        static bool TestPointConstraint();
        static bool TestOrientConstraint();
        static bool TestParentConstraint();
        static bool TestLookAtConstraint();
        static bool TestCrossObjectDocking();

        // ── 4. BlendTree 测试 ──
        static bool TestBlendTreeBasic();
        static bool TestBlendTreeLERP();

        // ── 5. AnimationBatch 测试 ──
        static bool TestBatchIsRecommended();

        // ── 6. 边界情况测试 ──
        static bool TestEmptySkeleton();
        static bool TestZeroWeightLayers();
    };

} // namespace Engine
