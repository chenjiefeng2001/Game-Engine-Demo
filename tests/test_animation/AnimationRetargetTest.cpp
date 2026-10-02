// P1: Animation 子系统测试 —— AnimationRetarget
//
// 关闭 P1-Animation 的最后一个 placeholder。Retarget 的风险点是
// "静默错位"：映射缺失或索引缓存失效时，姿态会被写到错误的骨骼上，
// 而不会报错。因此全部使用不变量断言：
//   - 映射表按 (source,target) 唯一；重复 AddMapping 只更新 scale
//   - AutoMapByName 只映射两侧都存在的骨骼名
//   - HasMappings 需要同时具备映射表**和**两个骨架
//   - 无映射时 RetargetPose 必须是 no-op（不得改动目标骨架）
//   - RetargetPose 不得改变目标骨架的骨骼数量
//   - 写入的平移 == 源平移 × 缩放因子
//   - CalculateBoneScale 在缺失骨架/无效名字/零长度时回退到 1.0
//
// 注意 API 实况（读源码确认，未猜测）：
//   - AnimationRetarget::AddMapping(src, tgt, scaleFactor = 0.0f)
//     —— **默认 0.0**，而 BoneMappingEntry 构造函数默认 1.0f。
//     0 表示"自动"，RetargetPose 内 scale<=0 时改用 CalculateBoneScale。
//   - AnimationRetarget::HasMappings() = 有映射 && 有源骨架 && 有目标骨架
//   - AddMapping 仅在**调用时**两个骨架都已设置，才填充 m_SourceIndices /
//     m_TargetIndices 缓存（见文件末尾 K3 记录）
//   - AnimationRetarget.cpp 为 GBK 编码
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Engine/Animation/AnimationRetarget.h"
#include "Engine/Animation/AnimationPipeline.h"
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

// 骨架：root + child。child 的 bind 平移沿 +Y 长度 boneLen，
// 因此 inverseBind 的平移模长 == boneLen（CalculateBoneScale 依赖此约定）。
Skeleton MakeSkeleton(const char* childName, float boneLen)
{
    Skeleton s;
    s.AddRootBone("root", Mat4());
    s.AddBone(childName, "root", Translation(0.0f, boneLen, 0.0f));
    for (size_t i = 0; i < s.GetBoneCount(); ++i) {
        s.GetBone(static_cast<int32>(i)).ComputeInverseBind();
    }
    return s;
}

PoseLocalData MakePose(size_t n, float tValue)
{
    PoseLocalData p;
    p.Resize(n);
    for (size_t i = 0; i < n; ++i) {
        p.translations[i] = Vec3(tValue, 0.0f, 0.0f);
    }
    return p;
}

Vec3 TranslationOf(const Mat4& m)
{
    return Vec3(m(0, 3), m(1, 3), m(2, 3));
}

} // namespace

// ── 映射表不变量 ───────────────────────────────────────────────────────
TEST(AnimationRetargetTest, InitiallyHasNoMappings)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);

    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    EXPECT_FALSE(rt.HasMappings());
    EXPECT_TRUE(rt.GetMappings().empty());
}

TEST(AnimationRetargetTest, SkeletonPointersRoundTrip)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;

    EXPECT_EQ(rt.GetSourceSkeleton(), nullptr);
    EXPECT_EQ(rt.GetTargetSkeleton(), nullptr);

    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    EXPECT_EQ(rt.GetSourceSkeleton(), &src);
    EXPECT_EQ(rt.GetTargetSkeleton(), &tgt);
}

TEST(AnimationRetargetTest, AddMappingMakesMappingsValid)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    rt.AddMapping("b", "b");
    EXPECT_TRUE(rt.HasMappings());
    ASSERT_EQ(rt.GetMappings().size(), 1u);
    EXPECT_EQ(rt.GetMappings()[0].sourceBone, "b");
    EXPECT_EQ(rt.GetMappings()[0].targetBone, "b");
}

TEST(AnimationRetargetTest, DuplicateMappingUpdatesScaleInsteadOfAppending)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    rt.AddMapping("b", "b", 1.0f);
    rt.AddMapping("b", "b", 3.0f);
    rt.AddMapping("b", "b", 2.5f);

    // (source,target) 组合必须唯一：重复调用只更新，不追加
    ASSERT_EQ(rt.GetMappings().size(), 1u) << "重复映射产生了重复条目";
    EXPECT_FLOAT_EQ(rt.GetMappings()[0].scaleFactor, 2.5f)
        << "重复映射未更新 scaleFactor";
}

TEST(AnimationRetargetTest, DistinctPairsAccumulate)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    rt.AddMapping("root", "root", 1.0f);
    rt.AddMapping("b", "b", 1.0f);
    EXPECT_EQ(rt.GetMappings().size(), 2u);
}

TEST(AnimationRetargetTest, ClearMappingsEmptiesEverything)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b");
    ASSERT_TRUE(rt.HasMappings());

    rt.ClearMappings();
    EXPECT_FALSE(rt.HasMappings());
    EXPECT_TRUE(rt.GetMappings().empty());
}

// ── AutoMapByName ──────────────────────────────────────────────────────
TEST(AnimationRetargetTest, AutoMapByNameMatchesSharedBoneNames)
{
    Skeleton src = MakeSkeleton("b", 2.0f);   // root, b
    Skeleton tgt = MakeSkeleton("b", 2.0f);   // root, b
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    rt.AutoMapByName();
    EXPECT_TRUE(rt.HasMappings());
    // root 与 b 两侧同名 ⇒ 2 条映射
    EXPECT_EQ(rt.GetMappings().size(), 2u);
    for (const auto& m : rt.GetMappings()) {
        EXPECT_EQ(m.sourceBone, m.targetBone) << "AutoMapByName 必须同名对同名";
        EXPECT_FLOAT_EQ(m.scaleFactor, 0.0f) << "0 表示自动计算缩放";
    }
}

TEST(AnimationRetargetTest, AutoMapByNameSkipsBonesMissingOnTarget)
{
    Skeleton src = MakeSkeleton("b", 2.0f);            // root, b
    Skeleton tgt;
    tgt.AddRootBone("root", Mat4());
    tgt.AddBone("different", "root", Translation(0.0f, 2.0f, 0.0f));
    for (size_t i = 0; i < tgt.GetBoneCount(); ++i) {
        tgt.GetBone(static_cast<int32>(i)).ComputeInverseBind();
    }

    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AutoMapByName();

    // 只有 root 同名；源侧的 "b" 在目标中不存在，不得被映射
    ASSERT_EQ(rt.GetMappings().size(), 1u);
    EXPECT_EQ(rt.GetMappings()[0].sourceBone, "root");
}

TEST(AnimationRetargetTest, AutoMapByNameWithoutSkeletonsIsSafe)
{
    AnimationRetarget rt;                    // 未设置任何骨架
    EXPECT_NO_THROW(rt.AutoMapByName());
    EXPECT_FALSE(rt.HasMappings());
}

TEST(AnimationRetargetTest, AutoMapByNameReplacesPriorMappings)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    rt.AddMapping("bogus", "bogus", 1.0f);
    ASSERT_EQ(rt.GetMappings().size(), 1u);

    rt.AutoMapByName();
    // 先前那条 bogus 映射必须已被清除，不得残留
    for (const auto& m : rt.GetMappings()) {
        EXPECT_NE(m.sourceBone, "bogus") << "AutoMapByName 未清除旧映射";
    }
}

// ── CalculateBoneScale ─────────────────────────────────────────────────
TEST(AnimationRetargetTest, BoneScaleFallsBackToOneWithoutSkeletons)
{
    AnimationRetarget rt;
    EXPECT_FLOAT_EQ(rt.CalculateBoneScale("b", "b"), 1.0f);
}

TEST(AnimationRetargetTest, BoneScaleFallsBackToOneForUnknownBones)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    EXPECT_FLOAT_EQ(rt.CalculateBoneScale("nope", "b"), 1.0f);
    EXPECT_FLOAT_EQ(rt.CalculateBoneScale("b", "nope"), 1.0f);
}

TEST(AnimationRetargetTest, BoneScaleIsTargetOverSourceLengthRatio)
{
    // 源骨长 2，目标骨长 4 ⇒ scale = 4/2 = 2
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 4.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    EXPECT_NEAR(rt.CalculateBoneScale("b", "b"), 2.0f, 1e-4f);
}

TEST(AnimationRetargetTest, BoneScaleIsOneWhenLengthsMatch)
{
    Skeleton src = MakeSkeleton("b", 3.0f);
    Skeleton tgt = MakeSkeleton("b", 3.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    EXPECT_NEAR(rt.CalculateBoneScale("b", "b"), 1.0f, 1e-4f);
}

TEST(AnimationRetargetTest, BoneScaleFallsBackToOneForZeroLengthBone)
{
    // 零长度骨骼不得除零
    Skeleton src = MakeSkeleton("b", 0.0f);
    Skeleton tgt = MakeSkeleton("b", 5.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    const float32 s = rt.CalculateBoneScale("b", "b");
    EXPECT_FALSE(std::isnan(s));
    EXPECT_FLOAT_EQ(s, 1.0f);
}

// ── RetargetPose ───────────────────────────────────────────────────────
TEST(AnimationRetargetTest, RetargetPoseWithoutMappingsIsNoOp)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    // 故意不建立映射

    tgt.UpdateWorldPoses();
    const Mat4 before = tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix;

    const auto pose = MakePose(tgt.GetBoneCount(), 5.0f);
    EXPECT_NO_THROW(rt.RetargetPose(tgt, pose));

    EXPECT_EQ(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix(1, 3),
              before(1, 3))
        << "无映射时 RetargetPose 改动了目标骨架";
}

TEST(AnimationRetargetTest, RetargetPoseDoesNotChangeBoneCount)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 1.0f);

    const size_t before = tgt.GetBoneCount();
    const auto pose = MakePose(src.GetBoneCount(), 1.0f);
    rt.RetargetPose(tgt, pose);
    EXPECT_EQ(tgt.GetBoneCount(), before);
}

TEST(AnimationRetargetTest, RetargetPoseAppliesExplicitScaleToTranslation)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 3.0f);          // 显式缩放，跳过自动计算

    const auto pose = MakePose(src.GetBoneCount(), 2.0f);
    rt.RetargetPose(tgt, pose);

    // 源平移 (2,0,0) × scale 3 ⇒ 目标 localPoseMatrix 平移应为 (6,0,0)
    const Vec3 t = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);
    EXPECT_NEAR(t.x, 6.0f, 1e-4f);
    EXPECT_NEAR(t.y, 0.0f, 1e-4f);
    EXPECT_NEAR(t.z, 0.0f, 1e-4f);
}

TEST(AnimationRetargetTest, RetargetPoseWithZeroScaleUsesAutomaticBoneScale)
{
    // scaleFactor = 0 ⇒ 触发 CalculateBoneScale：源长 2 / 目标长 4 ⇒ 2.0
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 4.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b");                // 默认 0.0f => 自动

    const auto pose = MakePose(src.GetBoneCount(), 3.0f);
    rt.RetargetPose(tgt, pose);

    const Vec3 t = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);
    EXPECT_NEAR(t.x, 6.0f, 1e-3f) << "自动缩放未生效（期望 3.0 * 2.0）";
}

TEST(AnimationRetargetTest, RetargetPoseSkipsUnmappedBones)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 1.0f);          // 只映射 b，不映射 root

    const Mat4 rootBefore = tgt.GetBone(tgt.FindBoneIndex("root")).localPoseMatrix;
    const auto pose = MakePose(src.GetBoneCount(), 7.0f);
    rt.RetargetPose(tgt, pose);

    const Mat4& rootAfter = tgt.GetBone(tgt.FindBoneIndex("root")).localPoseMatrix;
    EXPECT_FLOAT_EQ(rootAfter(0, 3), rootBefore(0, 3))
        << "未映射的骨骼被改写了";
}

TEST(AnimationRetargetTest, RetargetPoseToleratesUnmappedBoneNames)
{
    // 映射到目标中不存在的骨骼名 ⇒ FindBoneIndex 返回 -1，必须被跳过而非崩溃
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("doesNotExist", "alsoMissing", 1.0f);

    const auto pose = MakePose(src.GetBoneCount(), 1.0f);
    EXPECT_NO_THROW(rt.RetargetPose(tgt, pose));
    EXPECT_EQ(tgt.GetBoneCount(), 2u) << "无效映射不得改变目标骨架";
}

TEST(AnimationRetargetTest, RetargetPoseToleratesMismatchedPoseSize)
{
    // 源 pose 骨骼数少于映射索引 ⇒ 越界必须被跳过
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 1.0f);

    const auto tinyPose = MakePose(1, 1.0f);   // 只有 1 根骨骼，b 的索引越界
    EXPECT_NO_THROW(rt.RetargetPose(tgt, tinyPose));
}

TEST(AnimationRetargetTest, RetargetPoseIsIdempotent)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 2.0f);

    const auto pose = MakePose(src.GetBoneCount(), 3.0f);
    rt.RetargetPose(tgt, pose);
    const Vec3 first = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);
    rt.RetargetPose(tgt, pose);
    const Vec3 second = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);

    EXPECT_NEAR(first.x, second.x, 1e-5f) << "重复重定向结果不稳定";
    EXPECT_NEAR(first.y, second.y, 1e-5f);
}

// ── RetargetSkeleton ───────────────────────────────────────────────────
TEST(AnimationRetargetTest, RetargetSkeletonCopiesBoneCountAndPose)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);
    rt.AutoMapByName();

    EXPECT_NO_THROW(rt.RetargetSkeleton(tgt, src));
    EXPECT_EQ(tgt.GetBoneCount(), 2u) << "RetargetSkeleton 不得增删目标骨骼";
}

TEST(AnimationRetargetTest, RetargetSkeletonWithoutMappingsIsNoOp)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    const Mat4 before = tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix;
    EXPECT_NO_THROW(rt.RetargetSkeleton(tgt, src));
    EXPECT_FLOAT_EQ(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix(1, 3),
                    before(1, 3));
}

// ── K3 回归：调用顺序无关性 ───────────────────────────────────────────
//
// 历史缺陷：AddMapping 原本只在**调用当下**两个骨架均已设置时才填充索引缓存。
// 于是「先 AddMapping、后 Set*Skeleton」这一顺序下：
//   - m_Mappings 非空 + 两个骨架非空 ⇒ HasMappings() 为 true
//   - 但 m_SourceIndices 为空
// RetargetPose 按 m_SourceIndices[i] 取值（无边界检查的 operator[]），
// 实测进程硬崩溃，退出码 -1073740791 (0xC0000409 __fastfail)。
//
// 修复：Set*Skeleton 重建索引缓存，AddMapping 亦统一走重建路径，
// 使缓存与映射表始终等长同序，对象不再有"陈旧缓存"状态。
//
// 下面刻意保留原先会崩溃的调用顺序，并断言**结果正确**而非仅"没崩"——
// 这样将来若有人退回到 (a) 式打补丁（把非法顺序降级为静默 no-op），
// 本用例会立即失败。
TEST(AnimationRetargetTest, K3_MappingBeforeSk_IsOrderIndependentAndCorrect)
{
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgt = MakeSkeleton("b", 2.0f);
    AnimationRetarget rt;

    // 原先崩溃的顺序：先映射，后骨架
    rt.AddMapping("b", "b", 1.0f);
    rt.SetSourceSkeleton(&src);
    rt.SetTargetSkeleton(&tgt);

    ASSERT_TRUE(rt.HasMappings());

    const auto pose = MakePose(src.GetBoneCount(), 3.0f);
    ASSERT_NO_THROW(rt.RetargetPose(tgt, pose));

    // 必须得到**正确结果**：源平移 (3,0,0) × 显式 scale 1 ⇒ 目标 x=3。
    // 若退化为 no-op，localPoseMatrix 保持初始单位阵，x 会是 0 而失败。
    // 注意：localPoseMatrix 初始为单位阵（bindMatrix 是另一个字段），
    // 因此"未被映射"与"被映射但源 y=0"在 y 上无法区分 —— x 才是判别量。
    const Vec3 t = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);
    EXPECT_NEAR(t.x, 3.0f, 1e-4f)
        << "结果错误：期望平移 x=3，若为 0 则说明 RetargetPose 被静默跳过";
    EXPECT_NEAR(t.y, 0.0f, 1e-4f) << "源 pose y=0，缩放后 y 应仍为 0";
}

TEST(AnimationRetargetTest, K3_BothOrdersProduceIdenticalResult)
{
    // 正序与逆序必须完全等价 —— 这是"顺序无关"的完整表述
    Skeleton src = MakeSkeleton("b", 2.0f);
    Skeleton tgtA = MakeSkeleton("b", 2.0f);
    Skeleton tgtB = MakeSkeleton("b", 2.0f);

    AnimationRetarget forward;
    forward.SetSourceSkeleton(&src);
    forward.SetTargetSkeleton(&tgtA);
    forward.AddMapping("b", "b", 2.0f);

    AnimationRetarget reverse;
    reverse.AddMapping("b", "b", 2.0f);
    reverse.SetSourceSkeleton(&src);
    reverse.SetTargetSkeleton(&tgtB);

    const auto pose = MakePose(src.GetBoneCount(), 1.5f);
    forward.RetargetPose(tgtA, pose);
    reverse.RetargetPose(tgtB, pose);

    const Vec3 a = TranslationOf(tgtA.GetBone(tgtA.FindBoneIndex("b")).localPoseMatrix);
    const Vec3 b = TranslationOf(tgtB.GetBone(tgtB.FindBoneIndex("b")).localPoseMatrix);
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);
    // 且两者都确实是"映射已生效"而非都没跑
    EXPECT_NEAR(a.x, 3.0f, 1e-4f) << "scale 2 × 1.5 = 3";
}

TEST(AnimationRetargetTest, K3_SettingSkeletonTwiceRebuildsIndices)
{
    // 换骨架后缓存必须跟随更新，而不是保留指向旧骨架的索引
    Skeleton srcA = MakeSkeleton("b", 2.0f);
    Skeleton srcB;
    srcB.AddRootBone("root", Mat4());
    srcB.AddBone("other", "root", Translation(0.0f, 7.0f, 0.0f));
    for (size_t i = 0; i < srcB.GetBoneCount(); ++i) {
        srcB.GetBone(static_cast<int32>(i)).ComputeInverseBind();
    }
    Skeleton tgt = MakeSkeleton("b", 2.0f);

    AnimationRetarget rt;
    rt.SetSourceSkeleton(&srcA);
    rt.SetTargetSkeleton(&tgt);
    rt.AddMapping("b", "b", 1.0f);        // 在 srcA 上解析为有效索引

    // 换成不含 "b" 的骨架：映射仍保留，但索引应重建为 -1 并被跳过
    rt.SetSourceSkeleton(&srcB);
    const auto pose = MakePose(srcB.GetBoneCount(), 9.0f);
    EXPECT_NO_THROW(rt.RetargetPose(tgt, pose));

    // srcB 中没有 "b" ⇒ 映射解析为 -1 并被跳过 ⇒ 目标 localPoseMatrix
    // 保持初始单位阵（x=0）。若索引陈旧（仍指向 srcA 的 "b"），
    // 则会写入 srcB pose 的 x=9 —— x 即判别量。
    const Vec3 t = TranslationOf(tgt.GetBone(tgt.FindBoneIndex("b")).localPoseMatrix);
    EXPECT_NEAR(t.x, 0.0f, 1e-4f) << "陈旧索引导致写入了错误的源骨骼";
}
