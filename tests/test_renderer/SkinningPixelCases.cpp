/**
 * @file SkinningPixelCases.cpp
 * @brief 四个 GPU 蒙皮像素用例的共享实现（断言只有这一份）
 */
#include "SkinningPixelCases.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

namespace Engine {
namespace Test {

void RunBindPoseDrawsGeometryInCentreBandOnly(ISkinningPixelBackend& backend)
{
    const SkinCapture cap = backend.DrawRig(0.0f);
    ASSERT_TRUE(cap.Valid())
        << "readback unusable: w=" << cap.w << " h=" << cap.h
        << " bytes=" << cap.px.size();

    const SkinBands b = MakeSkinBands(cap.w);
    const int centre = SkinLitInColumns(cap, b.centre, cap.w - 1 - b.centre);
    const int left   = SkinLitInColumns(cap, b.left, b.centre - 1);
    const int right  = SkinLitInColumns(cap, b.right, cap.w - 1);
    std::printf("  [%s] bind pose (%dx%d): centre=%d left=%d right=%d total=%d\n",
                backend.Name(), cap.w, cap.h, centre, left, right, SkinCountLit(cap));

    EXPECT_GT(centre, 0) << "bind pose must draw geometry in the centre band";
    EXPECT_EQ(left, 0)   << "bind pose must not draw into the left band";
    EXPECT_EQ(right, 0)  << "bind pose must not draw into the right band";
}

void RunTranslatedBoneMovesGeometryIntoRightBand(ISkinningPixelBackend& backend)
{
    const SkinCapture bindCap  = backend.DrawRig(0.0f);
    const SkinCapture movedCap = backend.DrawRig(0.8f);
    ASSERT_TRUE(bindCap.Valid())  << "bind readback unusable";
    ASSERT_TRUE(movedCap.Valid()) << "moved readback unusable";
    ASSERT_EQ(movedCap.w, bindCap.w);
    ASSERT_EQ(movedCap.h, bindCap.h);

    const SkinBands b = MakeSkinBands(bindCap.w);
    const int bindCentre  = SkinLitInColumns(bindCap, b.centre, bindCap.w - 1 - b.centre);
    const int movedCentre = SkinLitInColumns(movedCap, b.centre, movedCap.w - 1 - b.centre);
    const int movedLeft   = SkinLitInColumns(movedCap, b.left, b.centre - 1);
    const int movedRight  = SkinLitInColumns(movedCap, b.right, movedCap.w - 1);
    std::printf("  [%s] moved pose: centre=%d (bind %d) left=%d right=%d\n",
                backend.Name(), movedCentre, bindCentre, movedLeft, movedRight);

    EXPECT_GT(bindCentre, 0)  << "precondition: bind pose covered the centre band";
    EXPECT_GT(movedRight, 0)  << "after the bone moves, geometry must APPEAR on the right";
    EXPECT_EQ(movedLeft, 0)   << "left band must stay empty; motion is +X only";
    EXPECT_LT(movedCentre, bindCentre)
        << "centre band must lose coverage as the quad leaves it";
    EXPECT_TRUE(SkinAnyPixelDiffers(bindCap, movedCap))
        << "the two poses must not be identical";
}

void RunSkinnedProgramIsInUseAndBackgroundIsStable(ISkinningPixelBackend& backend)
{
    std::string detail;
    EXPECT_TRUE(backend.VerifySkinnedProgram(&detail))
        << "the skinned program could not be proven to be the one in use: " << detail;

    const SkinCapture bindCap  = backend.DrawRig(0.0f);
    const SkinCapture movedCap = backend.DrawRig(0.8f);
    ASSERT_TRUE(bindCap.Valid() && movedCap.Valid());

    // 左下角在两个姿势下都在四边形之外，因此必须逐字节相同。这把「整帧因为无关
    // 原因变了」与「蒙皮生效」区分开。
    const bool stable = SkinCornerByteStable(bindCap, movedCap, 8);
    EXPECT_TRUE(stable) << "background must be byte identical between the two draws";
}

void RunProductDrawBranchChangesPixelsWithPose(ISkinningPixelBackend& backend)
{
    const SkinCapture bindPose  = backend.DrawProductPath(0.0f);
    const SkinCapture movedPose = backend.DrawProductPath(0.5f);
    ASSERT_TRUE(bindPose.Valid())  << "product draw readback unusable (bind pose)";
    ASSERT_TRUE(movedPose.Valid()) << "product draw readback unusable (moved pose)";

    const int bindLit  = SkinCentreBoxLit(bindPose);
    const int movedLit = SkinCentreBoxLit(movedPose);
    std::printf("  [%s] product path: centre box bind=%d moved=%d\n",
                backend.Name(), bindLit, movedLit);

    EXPECT_GT(bindLit, 0)  << "bind pose must cover the centre band";
    EXPECT_GT(movedLit, 0) << "moved pose must still cover the centre band";

    // 姿态变化必须真实反映到像素上：整体像素必须不同
    EXPECT_TRUE(SkinAnyPixelDiffers(bindPose, movedPose))
        << "product draw branch must produce different pixels for different poses";
}

} // namespace Test
} // namespace Engine