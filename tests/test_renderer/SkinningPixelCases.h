/**
 * @file SkinningPixelCases.h
 * @brief 四个 GPU 蒙皮像素用例的共享实现
 *
 * 断言只写一遍，GL 与 Vulkan 两个后端跑的是同一段代码。这样「像素验证」这件事
 * 才有一个定义，而不是两份会各自漂移的副本。
 *
 * 失败一律是**失败**，不是跳过。后端建不起来属于「环境能力」，由 fixture 决定
 * 显式跳过并写明实测原因；一旦后端建起来了，任何断言不成立都必须变红。
 */
#pragma once

#include "SkinningPixelRig.h"

namespace Engine {
namespace Test {

/** 绑定姿势：几何只出现在中间带。 */
void RunBindPoseDrawsGeometryInCentreBandOnly(ISkinningPixelBackend& backend);

/** 骨骼平移后几何进入右带。 */
void RunTranslatedBoneMovesGeometryIntoRightBand(ISkinningPixelBackend& backend);

/** 反假阳性：用的确实是蒙皮着色器，且背景逐字节稳定。 */
void RunSkinnedProgramIsInUseAndBackgroundIsStable(ISkinningPixelBackend& backend);

/** 产品绘制分支：姿势变化必须真实反映到像素上。 */
void RunProductDrawBranchChangesPixelsWithPose(ISkinningPixelBackend& backend);

} // namespace Test
} // namespace Engine