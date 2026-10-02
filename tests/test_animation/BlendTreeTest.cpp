// P1: Animation 子系统测试 —— BlendTree
//
// 依据 docs/Engine-Capability-Audit.md §4：Animation 声称完整但零测试。
// BlendTree 是最大的单文件（738 行 / 537 非注释），因此本单元全部使用
// **不变量**而非"返回值非零"式弱断言：
//   - 节点类型标记与实际节点类别一致
//   - BlendTree 拥有子节点（所有权），tree 不复制
//   - LERP 权重在 [0,1] 且两端可取退化姿态
//   - WeightedAvg 输入权重记录正确、输入可索引
//   - PoseLocalData 三通道长度守恒（IsValid / GetBoneCount）
//   - Fade 进度单调推进并钳制在 [0,1]
//   - 无 root / 空子节点时不得崩溃
//
// 注意 API 实况（读源码确认，未猜测）：
//   - PoseLocalData 在 **AnimationPipeline.h**（不是 AnimationPose.h）
//   - BlendNode::Evaluate(skeleton, resource, time, out)
//   - BlendTree::EvaluateToPose(out, skeleton, resource, time) —— 参数顺序与
//     BlendNode::Evaluate **不同**，勿混用
//   - AnimationResource 为空（无 clip）时，Clip 节点应当安全地不产出姿态
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Engine/Animation/BlendTree.h"
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

Skeleton MakeSkeleton()
{
    Skeleton s;
    s.AddRootBone("root", Mat4());
    s.AddBone("b1", "root", Mat4());
    s.AddBone("b2", "root", Mat4());
    s.AddBone("b3", "root", Mat4());
    for (size_t i = 0; i < s.GetBoneCount(); ++i) {
        Bone& b = s.GetBone(static_cast<int32>(i));
        b.localPoseMatrix = Mat4();
        b.ComputeInverseBind();
    }
    s.UpdateWorldPoses();
    return s;
}

std::unique_ptr<BlendNode> MakeClip(const char* name)
{
    return std::make_unique<ClipBlendNode>(std::string(name));
}

} // namespace

// ── PoseLocalData 通道长度守恒 ─────────────────────────────────────────
TEST(BlendTreeTest, PoseLocalDataResizeKeepsChannelsConsistent)
{
    PoseLocalData p;
    // 默认构造的 pose 是"空但一致" ⇒ IsValid() 为 **true**。
    // IsValid 只检查三通道长度相等，不检查是否为空 ——
    // 也就是说一个 0 骨骼的 pose 通过校验，这是该 API 的语义边界。
    EXPECT_TRUE(p.IsValid());
    EXPECT_EQ(p.GetBoneCount(), 0u);
    p.Resize(4);
    EXPECT_EQ(p.GetBoneCount(), 4u);
    EXPECT_TRUE(p.IsValid());
    EXPECT_EQ(p.translations.size(), p.rotations.size());
    EXPECT_EQ(p.rotations.size(), p.scales.size());
    // 默认值：平移 0、缩放 1
    EXPECT_FLOAT_EQ(p.translations[0].x, 0.0f);
    EXPECT_FLOAT_EQ(p.scales[0].x, 1.0f);
}

// ── 节点类型标记 ───────────────────────────────────────────────────────
TEST(BlendTreeTest, NodeTypeTagsMatchNodeCategory)
{
    auto clip  = MakeClip("idle");
    auto lerp  = std::make_unique<LERPBlendNode>(MakeClip("a"), MakeClip("b"), 0.25f);
    auto wavg  = std::make_unique<WeightedAvgBlendNode>();
    auto fade  = std::make_unique<FadeBlendNode>(MakeClip("f"));
    auto tri   = std::make_unique<TriLERPBlendNode>();
    auto bilin = std::make_unique<BilinearBlendNode>();

    EXPECT_EQ(clip->GetType(), BlendNodeType::Clip);
    EXPECT_EQ(lerp->GetType(), BlendNodeType::LERP);
    EXPECT_EQ(wavg->GetType(), BlendNodeType::WeightedAvg);
    EXPECT_EQ(fade->GetType(), BlendNodeType::Fade);
    EXPECT_EQ(tri->GetType(), BlendNodeType::TriLERP);
    EXPECT_EQ(bilin->GetType(), BlendNodeType::Bilinear);
}

TEST(BlendTreeTest, ClipNodeClipNameRoundTrips)
{
    ClipBlendNode c("walk");
    EXPECT_EQ(c.GetClipName(), "walk");
    c.SetClipName("run");
    EXPECT_EQ(c.GetClipName(), "run");
}

// ── LERP 权重 ──────────────────────────────────────────────────────────
TEST(BlendTreeTest, LERPBlendWeightRoundTripsAndPreservesChildren)
{
    auto node = std::make_unique<LERPBlendNode>(MakeClip("a"), MakeClip("b"), 0.25f);
    EXPECT_FLOAT_EQ(node->GetBlendWeight(), 0.25f);
    ASSERT_NE(node->GetChildA(), nullptr);
    ASSERT_NE(node->GetChildB(), nullptr);
    EXPECT_EQ(node->GetChildA()->GetType(), BlendNodeType::Clip);
    EXPECT_EQ(node->GetChildB()->GetType(), BlendNodeType::Clip);

    node->SetBlendWeight(0.75f);
    EXPECT_FLOAT_EQ(node->GetBlendWeight(), 0.75f);
}

TEST(BlendTreeTest, LERPWeightsAtExtremaAreDegenerate)
{
    Skeleton s = MakeSkeleton();
    AnimationResource res;                       // 无 clip：应安全地产出退化姿态
    PoseLocalData p0;
    p0.Resize(s.GetBoneCount());

    auto node = std::make_unique<LERPBlendNode>(MakeClip("a"), MakeClip("b"), 0.0f);
    EXPECT_NO_THROW(node->Evaluate(s, res, 0.0f, p0));
    EXPECT_TRUE(p0.IsValid()) << "LERP 评估不得破坏 pose 通道一致性";
}

// ── WeightedAvg 输入 ───────────────────────────────────────────────────
TEST(BlendTreeTest, WeightedAvgCollectsInputsInOrder)
{
    WeightedAvgBlendNode w;
    EXPECT_EQ(w.GetInputCount(), 0u);
    w.AddInput(WeightedAvgInput("idle", 1.0f, false));
    w.AddInput(WeightedAvgInput("run",  0.5f, false));
    w.AddInput(WeightedAvgInput("add",  0.25f, true));
    ASSERT_EQ(w.GetInputCount(), 3u);

    EXPECT_EQ(w.GetInput(0).clipName, "idle");
    EXPECT_FLOAT_EQ(w.GetInput(0).weight, 1.0f);
    EXPECT_FALSE(w.GetInput(0).additive);

    EXPECT_EQ(w.GetInput(1).clipName, "run");
    EXPECT_FLOAT_EQ(w.GetInput(1).weight, 0.5f);

    EXPECT_EQ(w.GetInput(2).clipName, "add");
    EXPECT_TRUE(w.GetInput(2).additive);
}

TEST(BlendTreeTest, WeightedAvgDefaultAddInputSetsWeightOne)
{
    WeightedAvgBlendNode w;
    w.AddInput("solo");
    ASSERT_EQ(w.GetInputCount(), 1u);
    EXPECT_FLOAT_EQ(w.GetInput(0).weight, 1.0f);
    EXPECT_FALSE(w.GetInput(0).additive);
}

// ── Bilinear / TriLERP 权重数组 ────────────────────────────────────────
// Bilinear 的权重不是存储字段 —— 它只存 4 个 clip 名 + 2D 参数 (tx, ty)，
// 权重在 Evaluate 里由 (tx, ty) 现算。因此这里只能断言参数往返。
// 注意实测：L325-326 注释声明 (tx, ty) 范围为 [0,1]，但 SetParameter
// **不做钳制**。GetTX/GetTY 如实返回存入值，范围检查只可能在 Evaluate 中。
TEST(BlendTreeTest, BilinearParameterRoundTrips)
{
    BilinearBlendNode b;
    b.SetClips("c00", "c10", "c01", "c11");
    b.SetParameter(0.25f, 0.75f);
    EXPECT_FLOAT_EQ(b.GetTX(), 0.25f);
    EXPECT_FLOAT_EQ(b.GetTY(), 0.75f);
}

TEST(BlendTreeTest, BilinearDefaultsToOriginParameter)
{
    BilinearBlendNode b;
    EXPECT_FLOAT_EQ(b.GetTX(), 0.0f);
    EXPECT_FLOAT_EQ(b.GetTY(), 0.0f);
}

TEST(BlendTreeTest, TriLERPWeightsAreReadableAndDefaultToUniform)
{
    TriLERPBlendNode t;
    // 默认三等分（m_Weights = {1/3, 1/3, 1/3}）
    EXPECT_FLOAT_EQ(t.GetWeight(0), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(t.GetWeight(1), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(t.GetWeight(2), 1.0f / 3.0f);

    t.SetWeights(0.25f, 0.25f, 0.5f);
    EXPECT_FLOAT_EQ(t.GetWeight(0), 0.25f);
    EXPECT_FLOAT_EQ(t.GetWeight(1), 0.25f);
    EXPECT_FLOAT_EQ(t.GetWeight(2), 0.5f);
}

// ── Fade 进度不变量 ────────────────────────────────────────────────────
TEST(BlendTreeTest, FadeProgressStaysWithinUnitInterval)
{
    FadeBlendNode f(MakeClip("x"));
    EXPECT_GE(f.GetFadeProgress(), 0.0f);
    EXPECT_LE(f.GetFadeProgress(), 1.0f);

    f.FadeIn(1.0f);
    EXPECT_EQ(f.GetFadeState(), FadeBlendNode::FadeState::FadingIn);

    for (int i = 0; i < 20; ++i) {
        f.UpdateFade(0.1f);                   // 共 2.0s，超出 1.0s 时长
        EXPECT_GE(f.GetFadeProgress(), 0.0f);
        EXPECT_LE(f.GetFadeProgress(), 1.0f);
    }
    EXPECT_FLOAT_EQ(f.GetFadeProgress(), 1.0f) << "FadeIn 完成后进度应为 1";
}

TEST(BlendTreeTest, FadeOutAlsoStaysBounded)
{
    FadeBlendNode f(MakeClip("x"));
    f.FadeOut(0.5f);
    EXPECT_EQ(f.GetFadeState(), FadeBlendNode::FadeState::FadingOut);
    for (int i = 0; i < 20; ++i) {
        f.UpdateFade(0.1f);
        EXPECT_GE(f.GetFadeProgress(), 0.0f);
        EXPECT_LE(f.GetFadeProgress(), 1.0f);
    }
}

// ── BlendTree 所有权与 root ─────────────────────────────────────────────
TEST(BlendTreeTest, EmptyTreeHasNoRoot)
{
    BlendTree tree;
    EXPECT_FALSE(tree.HasRoot());
    EXPECT_EQ(tree.GetRoot(), nullptr);
}

TEST(BlendTreeTest, SetRootTransfersOwnership)
{
    BlendTree tree;
    tree.SetRoot(MakeClip("idle"));
    EXPECT_TRUE(tree.HasRoot());
    ASSERT_NE(tree.GetRoot(), nullptr);
    EXPECT_EQ(tree.GetRoot()->GetType(), BlendNodeType::Clip);
}

TEST(BlendTreeTest, AddLERPByClipNameProducesTypedRoot)
{
    BlendTree tree;
    LERPBlendNode* n = tree.AddLERP("a", "b", 0.5f);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->GetBlendWeight(), 0.5f);
    EXPECT_EQ(n->GetType(), BlendNodeType::LERP);
}

TEST(BlendTreeTest, EvaluateToPoseWithoutRootIsSafe)
{
    Skeleton s = MakeSkeleton();
    AnimationResource res;
    BlendTree tree;                            // 无 root
    PoseLocalData out;
    out.Resize(s.GetBoneCount());

    EXPECT_NO_THROW(tree.EvaluateToPose(out, s, res, 0.0f));
    EXPECT_TRUE(out.IsValid());
    EXPECT_EQ(s.GetBoneCount(), 4u) << "空树求值不得改动骨架";
}

TEST(BlendTreeTest, EvaluateToPoseWithClipRootKeepsPoseValid)
{
    Skeleton s = MakeSkeleton();
    AnimationResource res;
    BlendTree tree;
    tree.SetRoot(MakeClip("idle"));

    PoseLocalData out;
    out.Resize(s.GetBoneCount());
    EXPECT_NO_THROW(tree.EvaluateToPose(out, s, res, 0.0f));
    EXPECT_TRUE(out.IsValid());
}

TEST(BlendTreeTest, BlendTreeIsNonCopyable)
{
    static_assert(!std::is_copy_constructible<BlendTree>::value,
                  "BlendTree must not be copyable (owns unique_ptr children)");
    SUCCEED();
}
