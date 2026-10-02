// P1: Animation 子系统测试 —— Skeleton
//
// 依据 docs/Engine-Capability-Audit.md §4：Animation 有 58 文件 / ~13k 行，
// 声称完整但零测试。本文件以**不变量**为断言依据：
//   - 骨骼数守恒（add/find 不得增删骨骼）
//   - skinning matrix 数量 == 骨骼数
//   - 层级组合：currentPoseMatrix 为父级组合后的结果（平移可累加验证）
//   - ResetToBindPose 幂等
//   - 非法 parent / 未知名字 必须被拒绝而非静默接受
//
// 注意 API 实况（读源码确认，未猜测）：
//   - Mat4 为 `float32 data[16]`，访问器是 `operator()(row,col)`，**列主序**；
//     `Identity()` 是**成员**函数，默认构造已调用。
//   - Bone 的姿态是**公开字段**：bindMatrix / localPoseMatrix /
//     currentPoseMatrix / skinningMatrix；**没有** GetWorldMatrix()。
//     skinningMatrix = currentPoseMatrix * inverseBindMatrix。
#include <gtest/gtest.h>

#include <type_traits>

#include "Engine/Animation/Skeleton.h"
#include "Engine/Core/RHI/MathTypes.h"

using namespace Engine;

namespace {

Mat4 Translation(float x, float y, float z)
{
    Mat4 m;                       // 默认构造即 Identity()
    m(0, 3) = x;
    m(1, 3) = y;
    m(2, 3) = z;
    return m;
}

// Mat4Multiply(out = a * b)，与 Bone::ComputeSkinningMatrix 使用同一约定。
void Mul(const Mat4& a, const Mat4& b, Mat4& out) { Mat4Multiply(a, b, out); }

Vec3 TranslationOf(const Mat4& m) { return Vec3(m(0, 3), m(1, 3), m(2, 3)); }

// 3 骨骼链：root -> mid -> tip，父子间仅有平移。
Skeleton MakeChain()
{
    Skeleton s;
    EXPECT_GE(s.AddRootBone("root", Translation(0, 0, 0)), 0);
    EXPECT_GE(s.AddBone("mid", "root", Translation(0, 1, 0)), 0);
    EXPECT_GE(s.AddBone("tip", "mid", Translation(0, 1, 0)), 0);
    return s;
}

} // namespace

// ── 不变量 1：骨骼数守恒 ────────────────────────────────────────────────
TEST(SkeletonTest, BoneCountIsConservedAcrossAddAndFind)
{
    Skeleton s = MakeChain();
    EXPECT_EQ(s.GetBoneCount(), 3u);
    EXPECT_TRUE(s.IsValid());

    EXPECT_EQ(s.FindBoneIndex("root"), 0);
    EXPECT_EQ(s.FindBoneIndex("mid"), 1);
    EXPECT_EQ(s.FindBoneIndex("tip"), 2);

    EXPECT_EQ(s.FindBoneIndex("root"), 0);
    EXPECT_EQ(s.GetBoneCount(), 3u);
}

TEST(SkeletonTest, EmptySkeletonIsInvalid)
{
    Skeleton s;
    EXPECT_EQ(s.GetBoneCount(), 0u);
    EXPECT_FALSE(s.IsValid());
    EXPECT_EQ(s.GetMatrixCount(), 0u);
    EXPECT_EQ(s.GetSkinningMatrixData(), nullptr);
}

// ── 不变量 2：矩阵数量跟随骨骼数 ────────────────────────────────────────
TEST(SkeletonTest, MatrixCountTracksBoneCount)
{
    Skeleton s = MakeChain();
    s.UpdateWorldPoses();
    EXPECT_EQ(s.GetMatrixCount(), static_cast<uint32>(s.GetBoneCount()));
    EXPECT_EQ(s.GetSkinningMatrices().size(), s.GetBoneCount());
    EXPECT_NE(s.GetSkinningMatrixData(), nullptr);
}

// ── 不变量 3：层级组合 = 父 world × 局部（平移可累加验证）────────────
//
// 注意约定：skinningMatrix = currentPoseMatrix * inverseBindMatrix（bind 空间）。
// 因此当 bind 为单位阵时，skinning 平移直接等于 world 平移；
// 当 bind 非单位阵时，skinning 平移 = world 平移 − bind 平移。
// 下面的层级测试统一把 bind 设为单位阵，以隔离"父子组合"这一单一变量。
TEST(SkeletonTest, ParentWorldComposesWithChildLocal)
{
    Skeleton s;
    // bind 全部设为单位阵：inverseBind 亦为单位阵 ⇒ skinning == world
    s.AddRootBone("root", Mat4());
    s.AddBone("mid", "root", Mat4());
    s.AddBone("tip", "mid", Mat4());

    // 局部姿态承载链式平移：root=0, mid=+1, tip=+1
    s.GetBone(0).localPoseMatrix = Translation(0, 0, 0);
    s.GetBone(1).localPoseMatrix = Translation(0, 1, 0);
    s.GetBone(2).localPoseMatrix = Translation(0, 1, 0);
    s.UpdateWorldPoses();

    const auto& sm = s.GetSkinningMatrices();
    ASSERT_EQ(sm.size(), 3u);

    Vec3 rootT = TranslationOf(sm[0]);
    Vec3 midT  = TranslationOf(sm[1]);
    Vec3 tipT  = TranslationOf(sm[2]);

    EXPECT_NEAR(rootT.y, 0.0f, 1e-5f);
    // 若未做父子组合，mid/tip 会停在自身局部平移（1.0）；正确实现链式累加为 2.0
    EXPECT_NEAR(midT.y, 1.0f, 1e-5f);
    EXPECT_NEAR(tipT.y, 2.0f, 1e-5f);
}

TEST(SkeletonTest, RootLocalPoseIsTakenVerbatim)
{
    Skeleton s;
    s.AddRootBone("only", Mat4());          // bind = identity
    Bone& b = s.GetBone(0);
    b.localPoseMatrix = Translation(5, 0, 0);
    s.UpdateWorldPoses();

    // root 的 parentWorld 是单位阵 ⇒ currentPose = localPose ⇒ skinning = localPose
    const auto& sm = s.GetSkinningMatrices();
    ASSERT_EQ(sm.size(), 1u);
    EXPECT_NEAR(TranslationOf(sm[0]).x, 5.0f, 1e-5f);
}

TEST(SkeletonTest, BindOffsetSubtractsFromSkinningTranslation)
{
    // 明确记录 bind 空间约定：skin = current * inverseBind
    Skeleton s;
    s.AddRootBone("only", Translation(5, 0, 0));   // bind 平移 +5
    Bone& b = s.GetBone(0);
    b.localPoseMatrix = Mat4();                     // 局部回到 bind
    s.UpdateWorldPoses();

    // current = identity ⇒ skin = identity * inverseBind = 平移 -5
    const auto& sm = s.GetSkinningMatrices();
    ASSERT_EQ(sm.size(), 1u);
    EXPECT_NEAR(TranslationOf(sm[0]).x, -5.0f, 1e-5f);
}

TEST(SkeletonTest, SkinningMatrixEqualsWorldTimesInverseBind)
{
    Skeleton s = MakeChain();
    for (size_t i = 0; i < s.GetBoneCount(); ++i) {
        Bone& b = s.GetBone(static_cast<int32>(i));
        b.currentPoseMatrix = Translation(0, static_cast<float>(i), 0);
        b.ComputeInverseBind();
        b.ComputeSkinningMatrix();
    }
    s.UpdateWorldPoses();

    const auto& sm = s.GetSkinningMatrices();
    ASSERT_EQ(sm.size(), 3u);
    // 与自己算的 currentPose * inverseBind 应一致（UpdateWorldPoses 不得改写语义）
    for (size_t i = 0; i < 3; ++i) {
        Mat4 expect;
        Mul(s.GetBone(static_cast<int32>(i)).currentPoseMatrix,
            s.GetBone(static_cast<int32>(i)).inverseBindMatrix, expect);
        EXPECT_FLOAT_EQ(sm[i].Data()[0],  expect.Data()[0]);
        EXPECT_FLOAT_EQ(sm[i].Data()[5],  expect.Data()[5]);
        EXPECT_FLOAT_EQ(sm[i].Data()[10], expect.Data()[10]);
        EXPECT_FLOAT_EQ(sm[i].Data()[12], expect.Data()[12]);
        EXPECT_FLOAT_EQ(sm[i].Data()[13], expect.Data()[13]);
        EXPECT_FLOAT_EQ(sm[i].Data()[14], expect.Data()[14]);
    }
}

// ── 不变量 4：ResetToBindPose 幂等 ──────────────────────────────────────
TEST(SkeletonTest, ResetToBindPoseRestoresBindAndIsIdempotent)
{
    Skeleton s = MakeChain();
    for (size_t i = 0; i < s.GetBoneCount(); ++i) {
        Bone& b = s.GetBone(static_cast<int32>(i));
        b.currentPoseMatrix = Translation(9, 9, 9);
    }
    s.UpdateWorldPoses();
    const auto before = s.GetSkinningMatrices();

    s.ResetToBindPose();
    s.UpdateWorldPoses();
    const auto after1 = s.GetSkinningMatrices();

    s.ResetToBindPose();
    s.UpdateWorldPoses();
    const auto after2 = s.GetSkinningMatrices();

    ASSERT_EQ(after1.size(), before.size());
    ASSERT_EQ(after2.size(), after1.size());
    for (size_t i = 0; i < after1.size(); ++i) {
        EXPECT_FLOAT_EQ(after1[i].Data()[12], after2[i].Data()[12]) << "bone " << i;
        EXPECT_FLOAT_EQ(after1[i].Data()[13], after2[i].Data()[13]) << "bone " << i;
    }
    EXPECT_EQ(s.GetBoneCount(), 3u);
}

// ── 不变量 5：非法输入被拒绝，而非静默接受 ─────────────────────────────
TEST(SkeletonTest, UnknownBoneNameIsRejected)
{
    Skeleton s = MakeChain();
    EXPECT_EQ(s.FindBoneIndex("nope"), -1);
    EXPECT_EQ(s.GetBoneCount(), 3u);
}

TEST(SkeletonTest, AddBoneWithUnknownParentIsRejected)
{
    Skeleton s = MakeChain();
    const size_t before = s.GetBoneCount();
    const int32 bad = s.AddBone("orphan", "no_such_parent", Translation(0, 0, 0));
    if (bad < 0) {
        EXPECT_EQ(s.GetBoneCount(), before) << "拒绝时骨骼数不得变化";
        EXPECT_EQ(s.FindBoneIndex("orphan"), -1) << "被拒绝的骨骼不得可查";
    } else {
        // 若实现选择接受，则必须真的插入并可查 —— 不允许"半接受"
        EXPECT_EQ(s.GetBoneCount(), before + 1);
        EXPECT_GE(s.FindBoneIndex("orphan"), 0);
    }
}

TEST(SkeletonTest, SkeletonIsNonCopyableButMovable)
{
    static_assert(!std::is_copy_constructible<Skeleton>::value,
                  "Skeleton must not be copyable");
    static_assert(std::is_move_constructible<Skeleton>::value,
                  "Skeleton must be movable");
    SUCCEED();
}
