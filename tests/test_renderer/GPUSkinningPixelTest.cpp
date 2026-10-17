/**
 * @file GPUSkinningPixelTest.cpp
 * @brief GPU 蒙皮像素验证的四个用例（后端由能力挑选）
 *
 * 关键变化：**用例不再绑定 GL**。fixture 按能力挑后端（GL 4.6，否则 Vulkan），
 * 断言部分在 SkinningPixelCases.cpp 里只写一份，两个后端跑的是同一段代码。
 *
 * 失败一律是失败。后端建不起来属于环境能力，只有当 runner **显式声明**
 * （GPU_SKINNING_NO_GL46=1）时才跳过，并且必须把两次尝试的实测原因写进日志。
 * Linux runner 声明已在 workflow 中撤掉：那里现在有 lavapipe，
 * Vulkan 建不起来就是缺陷，必须变红而不是被声明盖住。
 */
#include "SkinningPixelCases.h"
#include "SkinningPixelRig.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace Engine {
namespace Test {
namespace {

// runner 对「此处提供不了像素验证所需的后端」的显式 opt-in 声明。
// 缺省或不是 "1" 时保持严格：一台仅仅是 GL 坏掉的机器仍要失败，而不是跳过。
bool RunnerDeclaresNoPixelBackend()
{
    const char* v = std::getenv("GPU_SKINNING_NO_GL46");
    return v != nullptr && v[0] == '1' && v[1] == '\0';
}

} // namespace

class GPUSkinningPixelTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        s_Report.clear();
        s_Backend = SelectSkinningPixelBackend(&s_Report);
    }

    static void TearDownTestSuite()
    {
        s_Backend.reset();
    }

    void SetUp() override
    {
        if (!s_Backend) {
            if (RunnerDeclaresNoPixelBackend()) {
                GTEST_SKIP() << "GPU skinning pixel verification DID NOT RUN. "
                                "This runner declares it cannot provide a backend for the pixel "
                                "verification (GPU_SKINNING_NO_GL46=1). Measured: " << s_Report;
            }
            FAIL() << "no backend could render the skinning rig, and this runner did not "
                      "declare that it cannot. Measured: " << s_Report;
        }
    }

    static std::unique_ptr<ISkinningPixelBackend> s_Backend;
    static std::string                                s_Report;
};

std::unique_ptr<ISkinningPixelBackend> GPUSkinningPixelTest::s_Backend;
std::string GPUSkinningPixelTest::s_Report;

// ── Stage A: 绑定姿势建立像素基线 ───────────────────────────────────────────
TEST_F(GPUSkinningPixelTest, BindPoseDrawsGeometryInCentreBandOnly)
{
    RunBindPoseDrawsGeometryInCentreBandOnly(*s_Backend);
}

// ── Stage B: 骨骼平移产生预期的区域变化 ─────────────────────────────────────
TEST_F(GPUSkinningPixelTest, TranslatedBoneMovesGeometryIntoRightBand)
{
    RunTranslatedBoneMovesGeometryIntoRightBand(*s_Backend);
}

// ── Stage C: 反假阳性 ───────────────────────────────────────────────────────
TEST_F(GPUSkinningPixelTest, SkinnedProgramIsInUseAndBackgroundIsStable)
{
    RunSkinnedProgramIsInUseAndBackgroundIsStable(*s_Backend);
}

// ── 产品绘制分支 ────────────────────────────────────────────────────────────
// 这里验的是 EditorDemo 生产路径调用的那个函数（Engine::DrawSkinnedMeshIndexed），
// 它按签名就绑定在 GL 的 Shader/VertexArray 上。要让它在 Vulkan 上跑，等于把
// 产品的蒙皮绘制路径移植到 Vulkan —— 那是功能移植，不是测试移植，因此本用例
// 在非 GL 后端上显式跳过并写明理由，而不是伪装成通过。
TEST_F(GPUSkinningPixelTest, ProductDrawBranchChangesPixelsWithPose)
{
    if (!s_Backend->ProductPathAvailable()) {
        GTEST_SKIP() << "the product skinning draw branch is GL-only today (it takes a GL "
                        "Shader/VertexArray), and this run is using " << s_Backend->Name()
                     << ". Cases 1-3 ran for real on this backend.";
    }
    RunProductDrawBranchChangesPixelsWithPose(*s_Backend);
}

} // namespace Test
} // namespace Engine