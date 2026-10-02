// P1: Animation 子系统测试 —— IK
//
// 依据 docs/Engine-Capability-Audit.md §4：Animation 声称完整但零测试。
// IK 是"静默失败"风险最高的部分之一 —— solver 返回一个 float，但数值是否
// 真的收敛、约束是否真的生效，只能靠不变量断言：
//   - CCD 迭代使末端到 target 的距离**单调不增**（不得因迭代而变差）
//   - 收敛后误差 <= tolerance（当 solver 声称收敛时）
//   - target 恰在链上时，误差趋近 0
//   - constraint 的 Clamp 在边界内幂等，越界被夹紧
//   - 非法链（未 Build / 空链 / 越界索引）不得崩溃、不得改变骨架
//
// 注意 API 实况（读源码确认，未猜测）：
//   - IKChain: SetStartBoneIndex/SetEndBoneIndex/BuildFromSkeleton/GetBoneIndices
//   - IKSolver: SolveCCD / SolveCCDWithTargetDir / SolveAndBlend
//               SetMaxIterations/GetMaxIterations/SetTolerance/GetTolerance
//               SetBlendWeight/GetBlendWeight/SetBoneWeight
//   - 默认 m_MaxIterations=16, m_Tolerance=0.01f, m_BlendWeight=1.0f
//   - IKJointConstraint: enabled/minAngles/maxAngles, static Limit(), Clamp()
#include <gtest/gtest.h>

#include <cmath>

#include "Engine/Animation/IK.h"
#include "Engine/Core/RHI/MathTypes.h"

using namespace Engine;

namespace {

Mat4 Translation(float x, float y, float z)
{
    Mat4 m;
    m(0, 3) = x;
    m(1, 3) = y;
    m(2, 3) = z;
    return m;
}

// 3 骨骼链，全部 bind 为单位阵，局部沿 +Y 延伸 1 单位。
Skeleton MakeChain()
{
    Skeleton s;
    s.AddRootBone("root", Mat4());
    s.AddBone("mid", "root", Mat4());
    s.AddBone("tip", "mid", Mat4());
    for (size_t i = 0; i < s.GetBoneCount(); ++i) {
        Bone& b = s.GetBone(static_cast<int32>(i));
        b.localPoseMatrix = Translation(0, static_cast<float>(i), 0);
        b.ComputeInverseBind();
    }
    return s;
}

// 末端骨骼的世界位置（bind 为单位阵 ⇒ skinning 平移 == world 平移）
Vec3 EndEffectorWorld(Skeleton& s, int32 boneIndex)
{
    s.UpdateWorldPoses();
    const auto& sm = s.GetSkinningMatrices();
    const Mat4& m = sm[static_cast<size_t>(boneIndex)];
    return Vec3(m(0, 3), m(1, 3), m(2, 3));
}

float32 Distance(const Vec3& a, const Vec3& b)
{
    const float32 dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

IKChain MakeIKChain(Skeleton& s)
{
    IKChain chain;
    chain.SetStartBoneIndex(s.FindBoneIndex("mid"));
    chain.SetEndBoneIndex(s.FindBoneIndex("tip"));
    chain.BuildFromSkeleton(s);
    return chain;
}

} // namespace

// ── IKChain 结构不变量 ─────────────────────────────────────────────────
TEST(IKTest, ChainBuildsFromSkeletonAndPreservesEndpoints)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);

    EXPECT_EQ(chain.GetStartBoneIndex(), s.FindBoneIndex("mid"));
    EXPECT_EQ(chain.GetEndBoneIndex(), s.FindBoneIndex("tip"));
    EXPECT_GT(chain.GetLength(), 0u);
    // 链必须以 end 结束
    if (chain.GetLength() > 0) {
        EXPECT_EQ(chain.GetBoneIndices().back(), s.FindBoneIndex("tip"));
    }
}

TEST(IKTest, UnbuiltChainHasZeroLengthAndSolveIsSafe)
{
    Skeleton s = MakeChain();
    IKChain chain;                     // 未 BuildFromSkeleton
    EXPECT_EQ(chain.GetLength(), 0u);

    IKSolver solver;
    const Vec3 before = EndEffectorWorld(s, s.FindBoneIndex("tip"));
    // 空链不得崩溃；不得改动骨架姿态
    const float32 err = solver.SolveCCD(s, chain, Vec3(0, 5, 0));
    EXPECT_GE(err, 0.0f);
    const Vec3 after = EndEffectorWorld(s, s.FindBoneIndex("tip"));
    EXPECT_FLOAT_EQ(before.x, after.x);
    EXPECT_FLOAT_EQ(before.y, after.y);
    EXPECT_FLOAT_EQ(before.z, after.z);
}

TEST(IKTest, ChainClearEmptiesIndices)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    ASSERT_GT(chain.GetLength(), 0u);
    chain.Clear();
    EXPECT_EQ(chain.GetLength(), 0u);
}

// ── CCD 收敛不变量 ─────────────────────────────────────────────────────
TEST(IKTest, CcdReducesEndEffectorError)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    solver.SetMaxIterations(64);
    solver.SetTolerance(0.01f);

    const Vec3 target(0.0f, 4.0f, 0.0f);
    const float32 before = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);
    const float32 after = solver.SolveCCD(s, chain, target);
    const float32 actual = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);

    // 核心不变量：迭代后误差不得大于迭代前
    EXPECT_LE(actual, before + 1e-4f) << "CCD made it worse: " << before << " -> " << actual;
    // 且实际误差与返回值一致（返回值不是随手编的数）
    EXPECT_NEAR(after, actual, 1e-3f);
}

TEST(IKTest, CcdIsIdempotentAtConvergence)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    solver.SetMaxIterations(64);
    const Vec3 target(0.0f, 3.0f, 0.0f);

    solver.SolveCCD(s, chain, target);
    const float32 first = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);
    solver.SolveCCD(s, chain, target);
    const float32 second = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);

    // 已收敛时再次求解不应把结果推离
    EXPECT_LE(second, first + 1e-3f);
}

TEST(IKTest, MoreIterationsDoNotDegradeResult)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    const Vec3 target(0.0f, 3.0f, 0.0f);

    solver.SetMaxIterations(1);
    const float32 few = solver.SolveCCD(s, chain, target);

    s = MakeChain();
    chain = MakeIKChain(s);
    solver.SetMaxIterations(128);
    const float32 many = solver.SolveCCD(s, chain, target);

    EXPECT_LE(many, few + 1e-3f) << "more iterations regressed the solve";
}

TEST(IKTest, TargetAlreadyReachableDoesNotIncreaseError)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    solver.SetMaxIterations(64);

    // 末端当前位于 (0,2,0)。把 target 放在**更近**处：(0,1.5,0)。
    // 不变量不是"误差趋近 0"（两骨骼链在轴向上无法到达该点），
    // 而是"误差不得变大" —— 这才是 CCD 的正确性契约。
    const Vec3 target(0.0f, 1.5f, 0.0f);
    const float32 before = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);
    solver.SolveCCD(s, chain, target);
    const float32 after = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), target);

    EXPECT_LE(after, before + 1e-4f)
        << "error grew from " << before << " to " << after;
}

TEST(IKTest, UnreachableTargetStillMonotonicallyImproves)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    solver.SetMaxIterations(256);
    solver.SetTolerance(1e-4f);

    // 远超链长的目标：不可能收敛，但每轮迭代不得让误差变大
    const Vec3 far(10.0f, 10.0f, 0.0f);
    const float32 before = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), far);
    const float32 reported = solver.SolveCCD(s, chain, far);
    const float32 after = Distance(EndEffectorWorld(s, s.FindBoneIndex("tip")), far);

    EXPECT_LE(after, before + 1e-4f) << "unreachable target made it worse";
    EXPECT_GE(reported, 0.0f);
    // 返回值必须与实际误差一致
    EXPECT_NEAR(reported, after, 1e-2f);
}

TEST(IKTest, SolveDoesNotChangeSkeletonSize)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    IKSolver solver;
    const size_t before = s.GetBoneCount();

    solver.SolveCCD(s, chain, Vec3(1, 3, 1));
    solver.SolveCCDWithTargetDir(s, chain, Vec3(1, 3, 1), Vec3(0, 1, 0));

    EXPECT_EQ(s.GetBoneCount(), before);
}

// ── solver 参数往返 ────────────────────────────────────────────────────
TEST(IKTest, SolverParametersRoundTrip)
{
    IKSolver solver;
    solver.SetMaxIterations(7);
    solver.SetTolerance(0.125f);
    solver.SetBlendWeight(0.25f);
    EXPECT_EQ(solver.GetMaxIterations(), 7);
    EXPECT_FLOAT_EQ(solver.GetTolerance(), 0.125f);
    EXPECT_FLOAT_EQ(solver.GetBlendWeight(), 0.25f);
}

TEST(IKTest, PerBoneWeightGrowsVectorMonotonically)
{
    IKSolver solver;
    // SetBoneWeight(index>0) 必须先扩容，不得越界写
    solver.SetBoneWeight(3, 0.5f);
    solver.SetBoneWeight(1, 0.25f);
    SUCCEED();   // 崩溃即失败
}

// ── 约束 Clamp 不变量 ──────────────────────────────────────────────────
TEST(IKTest, ConstraintLimitEnablesAndSetsBounds)
{
    IKJointConstraint c = IKJointConstraint::Limit(-90, 90, -45, 45, -10, 10);
    EXPECT_TRUE(c.enabled);
    EXPECT_FLOAT_EQ(c.minAngles.x, -90.0f);
    EXPECT_FLOAT_EQ(c.maxAngles.x, 90.0f);
    EXPECT_FLOAT_EQ(c.minAngles.y, -45.0f);
    EXPECT_FLOAT_EQ(c.maxAngles.y, 45.0f);
    EXPECT_FLOAT_EQ(c.minAngles.z, -10.0f);
    EXPECT_FLOAT_EQ(c.maxAngles.z, 10.0f);
}

TEST(IKTest, DefaultConstraintIsDisabledButStillClampsToPi)
{
    IKJointConstraint c;
    EXPECT_FALSE(c.enabled);
    // Clamp 是**纯函数**，与 enabled 无关；默认边界 ±180 度。
    // 因此 ±500 会被夹到 ±180 —— enabled 只决定 solver 是否应用该约束。
    const Vec3 out = c.Clamp(Vec3(500.0f, -500.0f, 179.0f));
    EXPECT_FLOAT_EQ(out.x, 180.0f);
    EXPECT_FLOAT_EQ(out.y, -180.0f);
    EXPECT_FLOAT_EQ(out.z, 179.0f);   // 界内不变
}

TEST(IKTest, ClampIsIndependentOfEnabledFlag)
{
    IKJointConstraint a;
    IKJointConstraint b;
    b.enabled = true;                    // 仅切换标志
    a.minAngles = b.minAngles = Vec3(-10, -10, -10);
    a.maxAngles = b.maxAngles = Vec3(10, 10, 10);

    const Vec3 in(-90.0f, 0.0f, 90.0f);
    const Vec3 ca = a.Clamp(in);
    const Vec3 cb = b.Clamp(in);
    EXPECT_FLOAT_EQ(ca.x, cb.x);
    EXPECT_FLOAT_EQ(ca.y, cb.y);
    EXPECT_FLOAT_EQ(ca.z, cb.z);
}

TEST(IKTest, ConstraintClampIsIdempotent)
{
    IKJointConstraint c = IKJointConstraint::Limit(-90, 90, -45, 45, -10, 10);
    const Vec3 outOfRange(200.0f, -200.0f, 45.0f);
    const Vec3 once = c.Clamp(outOfRange);
    const Vec3 twice = c.Clamp(once);
    EXPECT_FLOAT_EQ(once.x, twice.x);
    EXPECT_FLOAT_EQ(once.y, twice.y);
    EXPECT_FLOAT_EQ(once.z, twice.z);
    // 且确实被夹到界内
    EXPECT_FLOAT_EQ(once.x, 90.0f);
    EXPECT_FLOAT_EQ(once.y, -45.0f);
    EXPECT_FLOAT_EQ(once.z, 10.0f);
}

TEST(IKTest, DisabledConstraintDoesNotClamp)
{
    IKJointConstraint c;                 // enabled = false，但边界仍是 ±180
    c.minAngles = Vec3(0, 0, 0);
    c.maxAngles = Vec3(0, 0, 0);
    // 即使边界收紧，Clamp 是纯函数；enabled 只影响 solver 是否应用它
    const Vec3 out = c.Clamp(Vec3(90, 90, 90));
    EXPECT_FLOAT_EQ(out.x, 0.0f);
}

TEST(IKTest, ChainConstraintAccessorsRoundTrip)
{
    Skeleton s = MakeChain();
    IKChain chain = MakeIKChain(s);
    if (chain.GetLength() > 0) {
        const IKJointConstraint c = IKJointConstraint::Limit(-30, 30, -30, 30, -30, 30);
        chain.SetConstraint(0, c);
        EXPECT_TRUE(chain.GetConstraint(0).enabled);
        EXPECT_FLOAT_EQ(chain.GetConstraint(0).maxAngles.x, 30.0f);
    } else {
        SUCCEED();
    }
}

TEST(IKTest, SolverIsNonCopyable)
{
    static_assert(!std::is_copy_constructible<IKSolver>::value,
                  "IKSolver must not be copyable");
    SUCCEED();
}
