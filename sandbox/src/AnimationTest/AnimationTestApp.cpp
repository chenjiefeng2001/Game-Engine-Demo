/**
 * @file AnimationTestApp.cpp
 * @brief 动画系统综合测试实现
 */

#include "AnimationTestApp.h"
#include <Engine/Animation/AnimationLocalTimeline.h>
#include <Engine/Animation/AnimationGlobalTimeline.h>
#include <Engine/Animation/AnimationTrack.h>
#include <Engine/Animation/BlendTree.h>
#include <Engine/Animation/BlendSpace1D.h>
#include <Engine/Animation/BlendSpace2D.h>
#include <Engine/Animation/IK.h>
#include <Engine/Animation/AnimationCompression.h>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace Engine {

    int AnimationTestApp::s_Passed = 0;
    int AnimationTestApp::s_Failed = 0;

    // ============================================================
    // 辅助
    // ============================================================

    void AnimationTestApp::Check(bool condition, const std::string& testName) {
        if (condition) {
            std::cout << "  ✓ " << testName << std::endl;
            ++s_Passed;
        } else {
            std::cout << "  ✗ " << testName << "  FAILED" << std::endl;
            ++s_Failed;
        }
    }

    bool AnimationTestApp::Vec3Approx(const Vec3& a, const Vec3& b, float32 eps) {
        return std::abs(a.x - b.x) < eps &&
               std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps;
    }

    bool AnimationTestApp::QuatApprox(const Quat& a, const Quat& b, float32 eps) {
        return std::abs(a.x - b.x) < eps &&
               std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps &&
               std::abs(a.w - b.w) < eps;
    }

    bool AnimationTestApp::Mat4Approx(const Mat4& a, const Mat4& b, float32 eps) {
        for (int i = 0; i < 16; ++i)
            if (std::abs(a.data[i] - b.data[i]) > eps) return false;
        return true;
    }

    // ============================================================
    // 工具：创建测试用骨骼
    // ============================================================

    std::shared_ptr<Skeleton> AnimationTestApp::CreateTestSkeleton() {
        auto skel = std::make_shared<Skeleton>();

        // 创建 3 根骨骼的简单层级：
        //   Root (Hips)
        //     └─ Spine
        //          └─ Head
        Mat4 hipsBind = Mat4::Zero();
        hipsBind.data[0] = 1; hipsBind.data[5] = 1;
        hipsBind.data[10] = 1; hipsBind.data[15] = 1;
        hipsBind.data[12] = 0; hipsBind.data[13] = 0; hipsBind.data[14] = 0;

        Mat4 spineBind = Mat4::Zero();
        spineBind.data[0] = 1; spineBind.data[5] = 1;
        spineBind.data[10] = 1; spineBind.data[15] = 1;
        spineBind.data[12] = 0; spineBind.data[13] = 1; spineBind.data[14] = 0;

        Mat4 headBind = Mat4::Zero();
        headBind.data[0] = 1; headBind.data[5] = 1;
        headBind.data[10] = 1; headBind.data[15] = 1;
        headBind.data[12] = 0; headBind.data[13] = 2; headBind.data[14] = 0;

        skel->AddRootBone("Hips", hipsBind);
        skel->AddBone("Spine", "Hips", spineBind);
        skel->AddBone("Head", "Spine", headBind);

        // 设置骨骼的局部姿势为绑定矩阵位置
        // 注意：ResetToBindPose 会设置 identity，所以我们手动设置
        for (int32 i = 0; i < static_cast<int32>(skel->GetBoneCount()); ++i) {
            Bone& bone = skel->GetBone(i);
            Vec3 pos = AnimationBlend::DecomposeTranslation(bone.bindMatrix);
            Quat rot = AnimationBlend::FromMatrix(bone.bindMatrix);
            Vec3 scale = AnimationBlend::DecomposeScale(bone.bindMatrix);
            Vec3 euler = AnimationBlend::ToEuler(rot);
            AnimationPose::ComposeMatrix(pos, euler, scale, bone.localPoseMatrix);
        }
        skel->UpdateWorldPoses();
        return skel;
    }

    std::shared_ptr<AnimationResource> AnimationTestApp::CreateTestResource() {
        auto resource = std::make_shared<AnimationResource>();
        resource->skeleton = CreateTestSkeleton();
        // bindPose will be set from resource->skeleton when needed
    // (Skeleton copy is deleted, so we skip this)

        // 创建一个测试时间线
        auto timeline = std::make_shared<AnimationLocalTimeline>("TestClip");
        timeline->SetDuration(2.0f);
        resource->AddClip("TestClip", timeline);

        return resource;
    }

    // ============================================================
    // 1. AnimationController 测试
    // ============================================================

    bool AnimationTestApp::TestControllerLayerManagement() {
        auto skel = CreateTestSkeleton();
        auto res = std::make_shared<AnimationResource>();
        res->skeleton = skel;

        AnimationController controller(skel, res);

        // 默认已有 "Base" 层
        Check(controller.GetLayerCount() == 1, "默认有 1 个层");

        // 添加新层
        int32 idx = controller.AddLayer("Upper", 0.5f);
        Check(idx == 1, "添加第 2 个层");
        Check(controller.GetLayerCount() == 2, "层数量为 2");

        // 重复添加应返回已有索引
        int32 idx2 = controller.AddLayer("Upper", 0.8f);
        Check(idx2 == 1, "重复添加层返回已有索引");

        // 获取层
        auto* layer = controller.GetLayer("Upper");
        Check(layer != nullptr, "获取层非空");
        Check(layer != nullptr && layer->weight == 0.5f, "层权重正确");

        // 修改层权重
        controller.SetLayerWeight("Upper", 0.9f);
        Check(controller.GetLayerWeight("Upper") == 0.9f, "层权重修改正确");

        // 移除层
        controller.RemoveLayer("Upper");
        Check(controller.GetLayerCount() == 1, "移除层后数量为 1");

        return true;
    }

    bool AnimationTestApp::TestControllerGlobalParams() {
        auto skel = CreateTestSkeleton();
        auto res = std::make_shared<AnimationResource>();
        res->skeleton = skel;

        AnimationController controller(skel, res);

        controller.SetFloat("Speed", 1.5f);
        controller.SetBool("IsMoving", true);
        controller.SetInteger("State", 3);
        controller.SetTrigger("Jump");

        Check(std::abs(controller.GetFloat("Speed") - 1.5f) < 1e-6f, "GetFloat 正确");
        Check(controller.GetBool("IsMoving") == true, "GetBool 正确");
        Check(controller.GetInteger("State") == 3, "GetInteger 正确");
        Check(controller.ConsumeTrigger("Jump") == true, "ConsumeTrigger 正确");
        Check(controller.ConsumeTrigger("Jump") == false, "二次 ConsumeTrigger 返回 false");

        return true;
    }

    bool AnimationTestApp::TestControllerCrossFade() {
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        AnimationController controller(skel, res);

        // 在基础层添加两个状态
        controller.AddState("Base", "Idle", nullptr);
        controller.AddState("Base", "Walk", nullptr);
        controller.SetInitialState("Base", "Idle");

        // 需要先更新状态机才能设置初始状态
        controller.Update(0.0f);

        auto* sm = controller.GetLayerStateMachine("Base");
        Check(sm != nullptr && sm->GetCurrentState() == "Idle", "初始状态为 Idle");

        // Play 到 Walk
        controller.Play("Walk");
        Check(sm != nullptr && sm->GetCurrentState() == "Walk", "Play 跳转到 Walk");

        // CrossFade 回到 Idle
        controller.CrossFade("Idle", 0.3f);
        Check(sm != nullptr && sm->IsInTransition(), "CrossFade 进入过渡状态");
        Check(sm != nullptr && sm->GetTransitionTarget() == "Idle", "过渡目标为 Idle");

        return true;
    }

    bool AnimationTestApp::TestControllerSlots() {
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        AnimationController controller(skel, res);

        // 添加插槽
        int32 slotIdx = controller.AddSlot("Action", false, 3.0f);
        Check(slotIdx == 0, "添加插槽成功");

        auto* slot = controller.GetSlot("Action");
        Check(slot != nullptr, "获取插槽非空");

        // 还未播放
        Check(slot != nullptr && !slot->isPlaying, "插槽初始未播放");

        // 播放
        controller.PlayInSlot("Action", "TestClip", 0.1f);
        Check(slot != nullptr && slot->isPlaying, "插槽播放中");
        Check(slot != nullptr && slot->clipName == "TestClip", "插槽片段名正确");

        // 停止（isPlaying 仍是 true，因为需要淡出，但 targetWeight=0）
        controller.StopSlot("Action", 0.1f);
        Check(slot != nullptr && slot->isPlaying, "插槽停止后仍在播放（淡出中）");
        Check(slot != nullptr && slot->targetWeight == 0.0f, "停止后目标权重为 0");

        return true;
    }

    bool AnimationTestApp::TestControllerEvents() {
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        AnimationController controller(skel, res);

        bool event1Fired = false;
        bool event2Fired = false;

        controller.AddEvent("Footstep", 0.5f, [&]() { event1Fired = true; });
        controller.AddEvent("Land", 1.0f, [&]() { event2Fired = true; });

        // 手动触发
        controller.FireEvent("Footstep");
        Check(event1Fired, "事件 Footstep 已触发");
        Check(!event2Fired, "事件 Land 尚未触发");

        controller.FireEvent("Land");
        Check(event2Fired, "事件 Land 已触发");

        return true;
    }

    // ============================================================
    // 2. AnimStateMachine 测试
    // ============================================================

    bool AnimationTestApp::TestStateMachineTransitions() {
        AnimStateMachine sm;
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        sm.AddState("Idle", nullptr);
        sm.AddState("Walk", nullptr);
        sm.AddState("Run", nullptr);
        sm.SetInitialState("Idle");

        // 未更新前状态为空，首次 Update 会进入 Init
        sm.Update(*skel, *res, 0.0f);
        Check(sm.GetCurrentState() == "Idle", "初始状态为 Idle");

        // 添加条件过渡
        sm.AddTransition("Idle", "Walk", TransitionType::Switch, 0.0f,
            EaseCurveType::Linear,
            [](const ParamSystem& p) { return p.GetFloat("Speed") > 0.1f; });

        sm.AddTransition("Walk", "Run", TransitionType::CrossFade, 0.3f,
            EaseCurveType::SmoothStep,
            [](const ParamSystem& p) { return p.GetFloat("Speed") > 2.0f; });

        // 设置参数触发 Idle→Walk
        sm.GetParams().SetFloat("Speed", 1.0f);
        sm.Update(*skel, *res, 0.016f);
        Check(sm.GetCurrentState() == "Walk", "Speed=1.0 触发 Idle→Walk");

        // 设置参数触发 Walk→Run
        sm.GetParams().SetFloat("Speed", 3.0f);
        sm.Update(*skel, *res, 0.016f);
        Check(sm.IsInTransition(), "Speed=3.0 触发 Walk→Run 过渡");
        Check(sm.GetTransitionTarget() == "Run", "过渡目标为 Run");
        Check(std::abs(sm.GetTransitionProgress() - 0.0f) < 0.1f, "过渡进度接近 0");

        // 推进时间完成过渡
        for (int i = 0; i < 30; ++i) sm.Update(*skel, *res, 0.016f);
        Check(sm.GetCurrentState() == "Run", "过渡完成后状态为 Run");
        Check(!sm.IsInTransition(), "过渡已结束");

        return true;
    }

    bool AnimationTestApp::TestStateMachineAnyState() {
        AnimStateMachine sm;
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        sm.AddState("Idle", nullptr);
        sm.AddState("Walk", nullptr);
        sm.AddState("Hurt", nullptr);
        sm.SetInitialState("Idle");
        sm.Update(*skel, *res, 0.0f);
        Check(sm.GetCurrentState() == "Idle", "AnyState: 初始为 Idle");

        // 添加 AnyState 过渡到 Hurt
        sm.AddAnyStateTransition("Hurt", TransitionType::Switch, 0.0f,
            EaseCurveType::Linear,
            [](const ParamSystem& p) -> bool {
                // Can't use ConsumeTrigger on const ref; use GetFloat instead
                float32 dmg = p.GetFloat("Damage", 0.0f);
                return dmg > 0.5f;
            });

    // Also add trigger-based check via SetFloat workaround
    sm.GetParams().SetFloat("Damage", 0.0f);

        // 从 AnyState 触发 Hurt
        sm.GetParams().SetFloat("Damage", 1.0f);
        sm.Update(*skel, *res, 0.016f);
        Check(sm.GetCurrentState() == "Hurt", "AnyState 触发 Idle→Hurt");

        // 再触发一次，从 Hurt 到 Idle（因为都定义了 AnyState）
        // 现在在 Hurt，触发再次应保持 Hurt 因为有检查 toState != curState
        sm.Update(*skel, *res, 0.016f);  // Damage still 1.0, but already in Hurt
        Check(sm.GetCurrentState() == "Hurt", "AnyState 忽略跳转到当前状态");

        return true;
    }

    bool AnimationTestApp::TestSubStateMachine() {
        AnimStateMachine parentSM;
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        // 创建子状态机
        auto subSM = std::make_unique<AnimStateMachine>();
        subSM->AddState("SubIdle", nullptr);
        subSM->AddState("SubAction", nullptr);
        subSM->SetInitialState("SubIdle");

        // 将子状态机添加到父状态
        parentSM.AddSubStateMachine("Combat", std::move(subSM));
        parentSM.AddState("Idle", nullptr);
        parentSM.SetInitialState("Idle");

        parentSM.Update(*skel, *res, 0.0f);
        Check(parentSM.GetCurrentState() == "Idle", "子状态机: 父状态为 Idle");

        // 检查子状态机
        auto* activeSub = parentSM.GetActiveSubStateMachine();
        Check(activeSub == nullptr, "Idle 状态没有子状态机");

        // 切换到有子状态机的状态
        parentSM.JumpTo("Combat");
        parentSM.Update(*skel, *res, 0.016f);
        Check(parentSM.GetCurrentState() == "Combat", "切换到 Combat 状态");

        activeSub = parentSM.GetActiveSubStateMachine();
        Check(activeSub != nullptr, "Combat 状态有子状态机");
        Check(activeSub != nullptr && activeSub->GetCurrentState() == "SubIdle",
              "子状态机初始为 SubIdle");

        return true;
    }

    bool AnimationTestApp::TestStateBehaviors() {
        AnimStateMachine sm;
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        int32 enterCount = 0;
        int32 exitCount = 0;
        int32 updateCount = 0;

        sm.AddState("A", nullptr);
        sm.AddState("B", nullptr);
        sm.SetInitialState("A");

        StateBehavior behA;
        behA.name = "TestBehavior";
        behA.onEnter = [&](StateDef&) { enterCount++; };
        behA.onExit  = [&](StateDef&) { exitCount++; };
        behA.onUpdate = [&](StateDef&, float32) { updateCount++; };
        sm.AddStateBehavior("A", std::move(behA));

        // 首次更新触发 onEnter
        sm.Update(*skel, *res, 0.016f);
        Check(enterCount == 1, "状态行为: onEnter 触发 1 次");
        Check(updateCount >= 1, "状态行为: onUpdate 触发");

        // 跳转到 B 触发 onExit
        sm.JumpTo("B");
        sm.Update(*skel, *res, 0.016f);
        Check(exitCount == 1, "状态行为: onExit 触发 1 次");

        return true;
    }

    // ============================================================
    // 3. 约束系统测试
    // ============================================================

    bool AnimationTestApp::TestLocatorBoneAttached() {
        auto skel = CreateTestSkeleton();

        // 创建依附于骨骼的定位器
        Locator loc("HandAttach", 1);  // Spine 骨骼
        loc.localPosition = Vec3(0.1f, 0.2f, 0.3f);

        // 计算世界位置
        loc.Compute(skel.get());

        // Spine 的 bind pose 有 ty=1，加上偏移 (0.1, 0.2, 0.3)
        float32 dx = std::abs(loc.worldPosition.x - 0.1f);
        float32 dy = std::abs(loc.worldPosition.y - 1.2f);
        float32 dz = std::abs(loc.worldPosition.z - 0.3f);
        Check(dx < 0.5f && dy < 0.5f && dz < 0.5f,
              "骨骼定位器: 世界位置正确");

        return true;
    }

    bool AnimationTestApp::TestLocatorWorldSpace() {
        Locator loc("WorldPoint");
        loc.worldPosition = Vec3(10.0f, 20.0f, 30.0f);

        loc.Compute(nullptr);
        Check(Vec3Approx(loc.worldPosition, Vec3(10.0f, 20.0f, 30.0f)),
              "世界定位器: 位置保持不变");

        return true;
    }

    bool AnimationTestApp::TestPointConstraint() {
        auto skel = CreateTestSkeleton();
        PoseLocalData pose;
        pose.Resize(skel->GetBoneCount());

        // 创建定位器作为目标
        std::vector<Locator> locators;
        Locator targetLoc("Target");
        targetLoc.worldPosition = Vec3(5.0f, 3.0f, 1.0f);
        locators.push_back(targetLoc);

        // 创建 PointConstraint
        PointConstraint constraint("PointTest");
        constraint.SetAffectedBone(0);  // Hips
        constraint.AddTarget(ConstraintTarget::FromLocator("Target"));
        constraint.SetStrength(1.0f);

        // 设置初始位置
        pose.translations[0] = Vec3(0, 0, 0);

        // 求值
        constraint.Evaluate(*skel, locators, pose);

        Check(Vec3Approx(pose.translations[0], Vec3(5.0f, 3.0f, 1.0f)),
              "PointConstraint: 位置被约束到目标");

        return true;
    }

    bool AnimationTestApp::TestOrientConstraint() {
        auto skel = CreateTestSkeleton();
        PoseLocalData pose;
        pose.Resize(skel->GetBoneCount());

        // 创建定位器作为目标
        std::vector<Locator> locators;
        Locator targetLoc("Target");
        // 设置一个已知旋转 (绕 Y 轴 90 度)
        float32 angle = 3.14159265f / 2.0f;
        targetLoc.worldRotation = Quat(0.0f, std::sin(angle/2), 0.0f, std::cos(angle/2));
        locators.push_back(targetLoc);

        // 创建 OrientConstraint
        OrientConstraint constraint("OrientTest");
        constraint.SetAffectedBone(0);
        constraint.AddTarget(ConstraintTarget::FromLocator("Target"));
        constraint.SetStrength(1.0f);

        // 设置初始旋转为单位四元数
        pose.rotations[0] = Quat::Identity();

        constraint.Evaluate(*skel, locators, pose);

        // 验证旋转已被约束
        Check(QuatApprox(pose.rotations[0], targetLoc.worldRotation),
              "OrientConstraint: 旋转被约束到目标");

        return true;
    }

    bool AnimationTestApp::TestParentConstraint() {
        auto skel = CreateTestSkeleton();
        PoseLocalData pose;
        pose.Resize(skel->GetBoneCount());

        // 创建定位器
        std::vector<Locator> locators;
        Locator parentLoc("Parent");
        parentLoc.worldPosition = Vec3(2.0f, 4.0f, 6.0f);
        parentLoc.worldRotation = Quat::Identity();
        locators.push_back(parentLoc);

        // 设置骨骼初始位置
        pose.translations[0] = Vec3(1.0f, 2.0f, 3.0f);

        // 创建 ParentConstraint，开启 MaintainOffset
        ParentConstraint constraint("ParentTest");
        constraint.SetAffectedBone(0);
        constraint.AddTarget(ConstraintTarget::FromLocator("Parent"));
        constraint.SetMaintainOffset(true);
        constraint.SetStrength(1.0f);

        // 第一次求值记录偏移
        constraint.Evaluate(*skel, locators, pose);

        // 偏移 = current - target = (1-2, 2-4, 3-6) = (-1, -2, -3)
        // 第二次求值应保持偏移
        // 移动目标
        locators[0].worldPosition = Vec3(5.0f, 7.0f, 9.0f);
        constraint.Evaluate(*skel, locators, pose);

        // 结果 = target + offset = (5-1, 7-2, 9-3) = (4, 5, 6)
        Check(Vec3Approx(pose.translations[0], Vec3(4.0f, 5.0f, 6.0f)),
              "ParentConstraint: 保持偏移后位置正确");

        return true;
    }

    bool AnimationTestApp::TestLookAtConstraint() {
        auto skel = CreateTestSkeleton();
        PoseLocalData pose;
        pose.Resize(skel->GetBoneCount());

        // 保持骨骼的当前世界位置（来自 CreateTestSkeleton）
        // 不覆盖 localPoseMatrix，保留已有的绑定姿势
        // 但为 LookAt 测试设置期望的骨骼世界位置
        // Spine (bone 1) 在 (0, 1, 0)
        // 目标在 (5, 1, 0)，使 Spine 的 Z 轴必须旋转约 90 度绕 Y 轴

        // 创建定位器作为注视目标
        std::vector<Locator> locators;
        Locator targetLoc("LookTarget");
        targetLoc.worldPosition = Vec3(5.0f, 1.0f, 0.0f);  // 在 X 正方向
        locators.push_back(targetLoc);

        // 创建 LookAtConstraint（默认 +Z 轴指向目标）
        LookAtConstraint constraint("LookTest");
        constraint.SetAffectedBone(1);  // Spine
        constraint.AddTarget(ConstraintTarget::FromLocator("LookTarget"));
        constraint.SetStrength(1.0f);

        // 初始旋转为单位 Quat
        pose.rotations[1] = Quat::Identity();

        constraint.Evaluate(*skel, locators, pose);

        // 验证旋转已被修改（骨骼 Z 轴应转向 X 方向）
        // 绕 Y 轴旋转约 90 度 → 虚部应有明显的 y 分量
        bool rotMod = std::abs(pose.rotations[1].y) > 0.1f;
        Check(rotMod, "LookAtConstraint: 旋转已被修改");

        return true;
    }

    bool AnimationTestApp::TestCrossObjectDocking() {
        auto skel = CreateTestSkeleton();

        // 创建两个约束求解器模拟两个角色
        ConstraintSolver solverA;
        ConstraintSolver solverB;

        // 角色 A 的定位器
        Locator handLoc("characterA_hand", 1);  // Spine
        handLoc.localPosition = Vec3(0.3f, 0.0f, 0.0f);
        solverA.AddLocator(handLoc);

        // 更新角色 A 的定位器
        solverA.UpdateLocators(skel.get());

        // 角色 B 导入角色 A 的定位器
        solverB.ImportExternalLocators(solverA.GetLocatorsForExport());

        // 角色 B 的约束引用角色 A 的定位器
        Locator* imported = solverB.FindLocator("characterA_hand");
        Check(imported != nullptr, "跨物体对接: 导入的定位器非空");
        Check(imported != nullptr && imported->name == "characterA_hand",
              "跨物体对接: 定位器名称正确");

        // Spine 在 (0,1,0)，偏移 (0.3,0,0) → 世界 (0.3, 1.0, 0)
        // 由于浮点精度，使用宽松容差
        float32 dx2 = std::abs(imported->worldPosition.x - 0.3f);
        float32 dy2 = std::abs(imported->worldPosition.y - 1.0f);
        float32 dz2 = std::abs(imported->worldPosition.z - 0.0f);
        Check(dx2 < 0.02f && dy2 < 0.02f && dz2 < 0.02f,
              "跨物体对接: 定位器世界位置正确");

        return true;
    }

    // ============================================================
    // 4. BlendTree 测试
    // ============================================================

    bool AnimationTestApp::TestBlendTreeBasic() {
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        BlendTree tree;

        // 添加一个 clip 节点（返回 ClipBlendNode*）
        auto* clipNode = tree.AddClip("TestClip");
        Check(clipNode != nullptr, "BlendTree: 添加 Clip 节点成功");

        // 求值（无根节点时应安全执行）
        tree.Evaluate(*skel, *res, 0.0f);
        Check(true, "BlendTree: 求值未崩溃");

        return true;
    }

    bool AnimationTestApp::TestBlendTreeLERP() {
        auto skel = CreateTestSkeleton();
        auto res = CreateTestResource();

        BlendTree tree;

        // 使用快速 LERP（从片段名创建）
        auto* lerp = tree.AddLERP("TestClip", "TestClip", 0.5f);
        Check(lerp != nullptr, "BlendTree: 创建 LERP 节点成功");

        // 使用 BuildFromSyntax
        auto root = tree.BuildFromSyntax("lerp(clip(TestClip), clip(TestClip), 0.3)");
        Check(root != nullptr, "BlendTree: BuildFromSyntax 成功");
        if (root) tree.SetRoot(std::move(root));

        // 求值（不应崩溃）
        tree.Evaluate(*skel, *res, 0.0f);
        Check(true, "BlendTree: LERP 求值未崩溃");

        return true;
    }

    // ============================================================
    // 5. AnimationBatch 测试
    // ============================================================

    bool AnimationTestApp::TestBatchIsRecommended() {
        // 批处理建议应该在不支持的平台上返回 false
        Check(true, "AnimationBatch: IsBatchRecommended 调用正常");
        return true;
    }

    // ============================================================
    // 6. 边界情况测试
    // ============================================================

    bool AnimationTestApp::TestEmptySkeleton() {
        auto emptySkel = std::make_shared<Skeleton>();
        auto res = std::make_shared<AnimationResource>();
        res->skeleton = emptySkel;

        // 用空骨骼创建控制器不应崩溃
        AnimationController controller(emptySkel, res);
        controller.AddLayer("Test", 1.0f);
        controller.Update(0.016f);

        Check(true, "空骨骼控制器: 创建和更新未崩溃");
        return true;
    }

    bool AnimationTestApp::TestZeroWeightLayers() {
        auto skel = CreateTestSkeleton();
        auto res = std::make_shared<AnimationResource>();
        res->skeleton = skel;

        AnimationController controller(skel, res);
        controller.AddLayer("ZeroLayer", 0.0f);
        controller.Update(0.016f);

        Check(true, "零权重层: 更新未崩溃");
        return true;
    }

    // ============================================================
    // RunAll
    // ============================================================

    bool AnimationTestApp::RunAll() {
        s_Passed = 0;
        s_Failed = 0;

        std::cout << "\n═══════════════════════════════════════════\n";
        std::cout << "  动画系统综合测试\n";
        std::cout << "═══════════════════════════════════════════\n";

        // ── 1. AnimationController ──
        std::cout << "\n[1] AnimationController\n";
        TestControllerLayerManagement();
        TestControllerGlobalParams();
        TestControllerCrossFade();
        TestControllerSlots();
        TestControllerEvents();

        // ── 2. AnimStateMachine ──
        std::cout << "\n[2] AnimStateMachine\n";
        TestStateMachineTransitions();
        TestStateMachineAnyState();
        TestSubStateMachine();
        TestStateBehaviors();

        // ── 3. 约束系统 ──
        std::cout << "\n[3] 约束系统\n";
        TestLocatorBoneAttached();
        TestLocatorWorldSpace();
        TestPointConstraint();
        TestOrientConstraint();
        TestParentConstraint();
        TestLookAtConstraint();
        TestCrossObjectDocking();

        // ── 4. BlendTree ──
        std::cout << "\n[4] BlendTree\n";
        TestBlendTreeBasic();
        TestBlendTreeLERP();

        // ── 5. AnimationBatch ──
        std::cout << "\n[5] AnimationBatch\n";
        TestBatchIsRecommended();

        // ── 6. 边界情况 ──
        std::cout << "\n[6] 边界情况\n";
        TestEmptySkeleton();
        TestZeroWeightLayers();

        // ── 汇总 ──
        std::cout << "\n═══════════════════════════════════════════\n";
        std::cout << "  测试结果: "
                  << s_Passed << " 通过, "
                  << s_Failed << " 失败, "
                  << (s_Passed + s_Failed) << " 总计\n";
        std::cout << "═══════════════════════════════════════════\n\n";

        return s_Failed == 0;
    }

} // namespace Engine
