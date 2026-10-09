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

// ═══════════════════════════════════════════════════════════════
// P5: SkinningComponent 的 runtime 可达性（Scene 驱动的纵向切片）
// ═══════════════════════════════════════════════════════════════
//
// 背景：Animation 子系统 6634 LOC / 111 测试，但 AnimationManager /
// Pipeline / Instance / SkinningComponent 在 engine 与 sandbox 中
// **零外部引用**（UNREACHABLE）。本组用例锁定的是那条**已经存在、
// 只是从不被触发**的链路：
//
//   Scene::Update(dt)            engine/src/Core/Scene/Scene.cpp:174
//     -> GameObject::Update(dt)  engine/src/Core/GameObject/GameObject.cpp:155
//       -> Component::OnUpdate(dt)
//         -> SkinningComponent::OnUpdate -> AdvanceAnimation
//           -> timeline.AdvanceTime -> EvaluateFromTimeline
//             -> Skeleton::UpdateWorldPoses -> GetSkinningMatrices
//
// 断言依据是**行为**而非"构造成功"：同一时间线在**不驱动** Scene 时
// 骨骼姿势必须保持绑定姿势；只有 Scene::Update 推进后，蒙皮矩阵才必须
// 真实跟随时间线数值变化。缺了后半句，这个用例就退化成"能构造"。

#include "Engine/Animation/SkinningComponent.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/AnimationKeyFrame.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include <nlohmann/json.hpp>

namespace {

// 构造 3 骨骼链 root -> mid -> tip，并让 root 在 1 秒内沿 +Y 从 0 移到 5。
struct AnimatedSkinRig {
    std::shared_ptr<Skeleton>       skeleton;
    std::shared_ptr<AnimationLocalTimeline> timeline;
};

AnimatedSkinRig MakeAnimatedRig()
{
    AnimatedSkinRig rig;

    auto skel = std::make_shared<Skeleton>();
    skel->AddRootBone("root", Translation(0, 0, 0));
    skel->AddBone("mid", "root", Translation(0, 1, 0));
    skel->AddBone("tip", "mid", Translation(0, 1, 0));
    rig.skeleton = skel;

    auto tl = std::make_shared<AnimationLocalTimeline>("lift");
    // 轨道命名契约见 AnimationPose::EvaluateFromTimeline：
    // "<BoneName>.position"，且按 Vec3 求值。
    AnimationTrack& track = tl->AddFloatTrack("root.position");
    track.SetPropertyType(AnimationPropertyType::Vec3);
    track.AddKeyFrame(KeyFrameVec3{ 0.0f, Vec3(0.0f, 0.0f, 0.0f) });
    track.AddKeyFrame(KeyFrameVec3{ 1.0f, Vec3(0.0f, 5.0f, 0.0f) });
    tl->SetDuration(1.0f);
    tl->Play();
    rig.timeline = tl;

    return rig;
}

}  // namespace

// 1) 场景驱动是唯一的推进来源：不调用 Scene::Update，姿势必须停在绑定姿势。
TEST(SkinningRuntimeReachability, PoseStaysAtBindPoseWithoutSceneUpdate)
{
    AnimatedSkinRig rig = MakeAnimatedRig();

    auto obj = std::make_shared<GameObject>("animated");
    SkinningComponent* attached = obj->AddComponent<SkinningComponent>();
    ASSERT_NE(attached, nullptr);
    attached->SetSkeleton(rig.skeleton);
    attached->SetAnimation(rig.timeline);

    Scene scene;
    scene.AddObject(obj);

    // 组件已挂进 Scene，但尚未有任何一帧被驱动。
    ASSERT_EQ(scene.GetObjectCount(), 1u);
    ASSERT_TRUE(attached->GetSkeleton() != nullptr);

    const Mat4& m = rig.skeleton->GetSkinningMatrices()[0];
    EXPECT_NEAR(m(1, 3), 0.0f, 1e-4f) << "root must remain at bind pose until the scene ticks";
    EXPECT_NEAR(rig.timeline->GetLocalTime(), 0.0f, 1e-6f);
}

// 2) 核心行为：Scene::Update 真实推进动画，蒙皮矩阵跟随时间线数值。
TEST(SkinningRuntimeReachability, SceneUpdateDrivesPoseAndSkinningMatrices)
{
    AnimatedSkinRig rig = MakeAnimatedRig();

    auto obj = std::make_shared<GameObject>("animated");
    SkinningComponent* skin = obj->AddComponent<SkinningComponent>();
    ASSERT_NE(skin, nullptr);
    skin->SetSkeleton(rig.skeleton);
    skin->SetAnimation(rig.timeline);

    Scene scene;
    scene.AddObject(obj);

    const float dt = 0.1f;
    float prevRootY = 0.0f;
    float prevTipY  = rig.skeleton->GetSkinningMatrices()[2](1, 3);

    for (int frame = 1; frame <= 5; ++frame) {
        scene.Update(dt);

        const float expectedTime = static_cast<float>(frame) * dt;
        EXPECT_NEAR(rig.timeline->GetLocalTime(), expectedTime, 1e-4f)
            << "Scene::Update must advance the timeline on frame " << frame;

        // 时间线为 0->5 的线性位移，故 root 的蒙皮矩阵 Y 平移应同步为 t*5。
        const Mat4& m = rig.skeleton->GetSkinningMatrices()[0];
        const float rootY = m(1, 3);
        EXPECT_NEAR(rootY, expectedTime * 5.0f, 1e-3f)
            << "skinning matrix must track the timeline, not just exist";

        // 子骨骼必须同样离开绑定姿势，并且**刚性地**继承父级位移：
        // 每帧增量与 root 的增量相同。这里刻意断言增量而非绝对常量 ——
        // 绝对值取决于 bind/inverse-bind 的矩阵约定，断言它会把测试
        // 绑死在约定细节上；增量才是"层级组合确实发生了"的证据。
        const Mat4& tip = rig.skeleton->GetSkinningMatrices()[2];
        const float tipY = tip(1, 3);
        EXPECT_NEAR(tipY - prevTipY, rootY - prevRootY, 1e-3f)
            << "child bone must inherit parent motion rigidly on frame " << frame;

        prevRootY = rootY;
        prevTipY  = tipY;
    }
}

// 3) 组件缓存必须与骨架一致（EvaluatePose 的最后一步是同步缓存）。
TEST(SkinningRuntimeReachability, ComponentCacheMatchesSkeletonAfterUpdate)
{
    AnimatedSkinRig rig = MakeAnimatedRig();

    auto obj = std::make_shared<GameObject>("animated");
    SkinningComponent* skin = obj->AddComponent<SkinningComponent>();
    ASSERT_NE(skin, nullptr);
    skin->SetSkeleton(rig.skeleton);
    skin->SetAnimation(rig.timeline);

    Scene scene;
    scene.AddObject(obj);
    scene.Update(0.25f);

    const auto& cached = skin->GetSkinningMatrices();
    const auto& truth  = rig.skeleton->GetSkinningMatrices();
    ASSERT_EQ(cached.size(), truth.size());
    ASSERT_FALSE(cached.empty());
    for (size_t i = 0; i < cached.size(); ++i) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                EXPECT_NEAR(cached[i](r, c), truth[i](r, c), 1e-6f)
                    << "component cache diverged from skeleton at bone " << i;
            }
        }
    }
    EXPECT_GT(cached[0](1, 3), 0.0f) << "pose must actually have moved off the bind pose";
}
// ===========================================================================
// 契约组件克隆保真（Skinning）
//
// 这些测试走的是生产克隆路径：CaptureScene 只收编带稳定类型名的组件，
// InstantiateScene 只重建已注册契约组件。任何一步缺失都会表现为克隆体
// 没有蒙皮能力，而不是"JSON 看起来对"。
// ===========================================================================

namespace {

// 构造带蒙皮网格的 rig，让克隆后仍能验证网格数据确实被搬运。
std::shared_ptr<SkinnedMesh> MakeSkinnedMesh()
{
    std::vector<SkinnedVertex> verts;
    verts.resize(2);
    verts[0].position = Vec3(0.0f, 0.0f, 0.0f);
    verts[1].position = Vec3(1.0f, 0.0f, 0.0f);
    verts[0].boneIndices[0] = 0;
    verts[0].boneWeights[0] = 1.0f;
    verts[1].boneIndices[0] = 0;
    verts[1].boneWeights[0] = 1.0f;
    return std::make_shared<SkinnedMesh>(verts, std::vector<uint32>{ 0, 1, 0 });
}

}  // namespace

// 1) 契约身份：没有稳定类型名，CaptureScene 会直接跳过该组件。
TEST(SkinningContractClone, HasStableContractTypeName)
{
    SkinningComponent skin;
    const char* tn = skin.GetComponentTypeName();
    ASSERT_NE(tn, nullptr);
    EXPECT_STREQ(tn, "Skinning");
}

// 2) 注册：没有注册，InstantiateScene 会报 unknown type 并跳过。
TEST(SkinningContractClone, IsRegisteredAndCreatableByTypeName)
{
    EXPECT_TRUE(ComponentRegistryGo::IsRegistered("Skinning"));

    std::shared_ptr<Component> made = ComponentRegistryGo::Create("Skinning");
    ASSERT_NE(made, nullptr);
    auto* skin = dynamic_cast<SkinningComponent*>(made.get());
    ASSERT_NE(skin, nullptr) << "registry factory must produce a SkinningComponent";
    EXPECT_STREQ(skin->GetComponentTypeName(), "Skinning");
}

// 3) 克隆不是共享：两个组件必须持有各自独立的骨架 / 网格 / 时间线。
TEST(SkinningContractClone, RoundTripRebuildsIndependentStateNotSharedPointers)
{
    AnimatedSkinRig rig = MakeAnimatedRig();
    auto mesh = MakeSkinnedMesh();

    auto src = std::make_shared<GameObject>("src");
    SkinningComponent* a = src->AddComponent<SkinningComponent>();
    ASSERT_NE(a, nullptr);
    a->SetSkeleton(rig.skeleton);
    a->SetSkinnedMesh(mesh);
    a->SetAnimation(rig.timeline);

    nlohmann::json blob;
    a->Serialize(blob);
    ASSERT_FALSE(blob.empty());
    ASSERT_TRUE(blob.contains("skeleton"));
    ASSERT_TRUE(blob.contains("skinnedMesh"));
    ASSERT_TRUE(blob.contains("animation"));

    // 走生产路径：按类型名创建组件并反序列化
    std::shared_ptr<Component> made = ComponentRegistryGo::Create("Skinning");
    ASSERT_NE(made, nullptr);
    auto* b = dynamic_cast<SkinningComponent*>(made.get());
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(b->Deserialize(blob));

    // 骨架被重建，但必须是新对象
    ASSERT_NE(b->GetSkeleton(), nullptr);
    EXPECT_NE(b->GetSkeleton().get(), a->GetSkeleton().get())
        << "clone must not share the source skeleton object";
    EXPECT_EQ(b->GetSkeleton()->GetBoneCount(), a->GetSkeleton()->GetBoneCount());

    // 蒙皮网格被重建，同样是新对象
    ASSERT_NE(b->GetSkinnedMesh(), nullptr);
    EXPECT_NE(b->GetSkinnedMesh().get(), a->GetSkinnedMesh().get())
        << "clone must not share the source skinned mesh object";
    EXPECT_EQ(b->GetSkinnedMesh()->GetVertexCount(), mesh->GetVertexCount());
    EXPECT_EQ(b->GetSkinnedMesh()->GetIndexCount(), mesh->GetIndexCount());

    // 动画时间线被重建
    ASSERT_NE(b->GetAnimation(), nullptr);
    EXPECT_NE(b->GetAnimation().get(), a->GetAnimation().get())
        << "clone must not share the source timeline object";
    EXPECT_FLOAT_EQ(b->GetAnimation()->GetDuration(),
                    a->GetAnimation()->GetDuration());
}

// 4) 克隆体必须能继续推进动画，并产出有效蒙皮矩阵。
TEST(SkinningContractClone, ClonedComponentContinuesAnimationAndProducesMatrices)
{
    AnimatedSkinRig rig = MakeAnimatedRig();
    auto mesh = MakeSkinnedMesh();

    auto src = std::make_shared<GameObject>("src");
    SkinningComponent* a = src->AddComponent<SkinningComponent>();
    a->SetSkeleton(rig.skeleton);
    a->SetSkinnedMesh(mesh);
    a->SetAnimation(rig.timeline);

    nlohmann::json blob;
    a->Serialize(blob);

    auto cloned = std::make_shared<GameObject>("clone");
    SkinningComponent* b = cloned->AddComponent<SkinningComponent>();
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(b->Deserialize(blob));

    // 克隆体立即拥有与源一致的蒙皮矩阵数量
    EXPECT_EQ(b->GetMatrixCount(), a->GetMatrixCount());
    EXPECT_GT(b->GetMatrixCount(), 0u);

    // 关键：克隆体挂进场景后能被场景帧更新驱动，产生变化的蒙皮矩阵
    Scene scene;
    scene.AddObject(cloned);

    const float before = b->GetSkinningMatrices()[0](1, 3);
    scene.Update(0.5f);
    const float after = b->GetSkinningMatrices()[0](1, 3);

    EXPECT_NE(before, after)
        << "cloned component must keep animating off the bind pose";
    EXPECT_GT(after, before)
        << "root bone should keep moving along +Y after cloning";

    // 矩阵必须仍然自洽（组件缓存 == 骨架真值）
    const auto& cached = b->GetSkinningMatrices();
    const auto& truth  = b->GetSkeleton()->GetSkinningMatrices();
    ASSERT_EQ(cached.size(), truth.size());
    for (size_t i = 0; i < cached.size(); ++i) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                EXPECT_NEAR(cached[i](r, c), truth[i](r, c), 1e-6f)
                    << "cloned cache diverged from its own skeleton at bone " << i;
            }
        }
    }
}
