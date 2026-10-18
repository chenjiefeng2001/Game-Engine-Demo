// HRC-3 D3 Step 3.1 — Native Known-Pixel Pick Proof
//
// 目标：用**可执行证据**证明 D3 的空间语义正确，而不是"resources ready + GL clean"。
// 本 fixture 是 native-only：**不经过 C ABI、不接 managed/UI、不含 resize**。
//
// 它要证明的东西（逐条对应 D3 Step 2 冻结的 contract）：
//   D3-C1  color pass 与 pick pass 的几何一致（同一批 RenderCommand / 同一 camera）
//   D3-C2  sentinel = -1；有效 ID >= 1；0 永不成为有效 hit
//   D3-C3  坐标映射由**已知像素**确定，而不是从旧实现里挑一个公式
//   D3-C6  no-hit / invalid / outside / not-ready 全部可区分（readback 失败 ≠ no-hit）
//   废止条款 旧 ReadPixelID 已移除；无 max(0,...) clamp；clear 后确实读回 -1
//
// 关键设计：先从 **color attachment**（attachment 0）实测精灵的屏幕包围盒，
// 再去 ID attachment 取样。这样"已知像素"来自真实帧内容本身，
// 测试不需要复制一份投影矩阵 —— 而复制投影矩阵正是让 color/pick 静默错位的元凶。

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Application.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/Platform/GlfwLifetime.h"

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr const char* kSpriteTexture = "assets/gp01/tex/arrow_n.png";


// ── 一个确定性的 production frame 宿主（复刻 EngineHost 的 GL 搭建顺序，
//    但不经过任何 C ABI 入口）──
class PickFrameHost {
public:
    bool Setup() {
        // 持久化 GLFW 必须先声明 main thread。经由 C ABI 时这一步是
        // EditorBridge 的副作用（capi.cpp:184）；本 fixture 是 native-only，
        // 不经过 C ABI，因此必须显式声明，否则 AcquireGlfwLifetime 会直接拒绝。
        if (!Engine::RegisterGlfwOwnerThread()) {
            m_FailStep = "RegisterGlfwOwnerThread";
            return false;
        }

        m_Factory = std::make_unique<Engine::OpenGLGraphicsFactory>(true);
        if (!m_Factory->InitializePersistentHiddenContext(64, 64)) {
            m_FailStep = "InitializePersistentHiddenContext: " +
                         m_Factory->GetLastError();
            return false;
        }
        if (!m_Factory->MakePersistentContextCurrent()) {
            m_FailStep = "MakePersistentContextCurrent";
            return false;
        }

        m_RenderContext = std::make_unique<Engine::OpenGLContext>(
            m_Factory->GetPersistentWindow(), m_Factory->GetGLContext());

        m_Fbo = std::make_unique<Engine::OpenGLFramebuffer>(m_Factory->GetGLContext());
        m_Fbo->Resize(kWidth, kHeight);
        if (!m_Fbo->IsValid()) {
            m_FailStep = "Fbo->Resize/IsValid";
            return false;
        }
        if (!m_Fbo->HasPickTexture()) {
            m_FailStep = "Fbo->HasPickTexture";
            return false;
        }

        m_App = std::make_unique<Engine::Application>(*m_Factory, true);
        if (!m_App->InitializeHeadless()) {
            m_FailStep = "Application::InitializeHeadless";
            return false;
        }
        // 显式打开 pick pass：生产路径默认关闭（没有请求通道，见 U2 冻结语义）。
        // 本 fixture 就是那个"已知像素证明"场景，所以由它来显式开启。
        m_App->SetProductionPickPassEnabled(true);

        m_Texture = m_Factory->CreateTexture(kSpriteTexture);
        if (!m_Texture) {
            m_FailStep = "CreateTexture (check CWD / asset path)";
            return false;
        }
        return true;
    }

    const std::string& FailStep() const { return m_FailStep; }

    void Teardown() {
        // 顺序至关重要：Texture 由 Application 的 StackAllocator 分配。
        // 若先销毁 Application（释放 arena）再释放 shared_ptr<Texture>，
        // 引用计数会落在已释放的内存上（ASan: heap-use-after-free）。
        m_Scene.reset();     // 先释放场景持有的 texture 引用
        m_Texture.reset();   // 再释放本 fixture 自己的引用（此时 arena 仍存活）
        m_App.reset();       // 然后才销毁 Application / StackAllocator
        m_Fbo.reset();
        m_RenderContext.reset();
        if (m_Factory) {
            m_Factory->UncurrentPersistentContext();
            m_Factory.reset();
        }
    }

    // 建立两个上下分离的精灵：世界 +y 的 A 在屏幕上方，世界 -y 的 B 在屏幕下方。
    // 两者 ID 不同，因此**任何 Y 翻转错误都会把 A/B 读反**，测试立刻失败。
    bool BuildTwoSpriteScene(uint32_t& idA, uint32_t& idB) {
        m_Scene = std::make_unique<Engine::Scene>("D3PickFixture");
        m_Scene->SetActive(true);

        auto a = std::make_shared<Engine::GameObject>("pick_A_upper");
        auto sa = a->AddComponent<Engine::SpriteComponent>(m_Texture);
        sa->SetColor(1.0f, 0.0f, 0.0f, 1.0f);
        a->GetTransform().SetPosition(0.0f, 0.6f, 0.0f);
        a->GetTransform().SetScale(0.5f);
        a->SetActive(true);

        auto b = std::make_shared<Engine::GameObject>("pick_B_lower");
        auto sb = b->AddComponent<Engine::SpriteComponent>(m_Texture);
        sb->SetColor(0.0f, 1.0f, 0.0f, 1.0f);
        b->GetTransform().SetPosition(0.0f, -0.6f, 0.0f);
        b->GetTransform().SetScale(0.5f);
        b->SetActive(true);

        m_Scene->AddObject(a);
        m_Scene->AddObject(b);

        idA = a->GetID();
        idB = b->GetID();
        // D3-C2 / U5：有效 ID 恒 >= 1
        return idA >= 1 && idB >= 1 && idA != idB;
    }

    bool RenderFrame() {
        m_Fbo->Clear(0.0f, 0.0f, 0.0f, 1.0f);
        m_Fbo->Bind();
        m_Factory->GetGLContext().Viewport(0, 0, kWidth, kHeight);
        const bool ok = m_App->RenderProductionFrame(
            *m_RenderContext, *m_Scene, kWidth, kHeight);
        m_Fbo->Unbind();
        return ok;
    }

    Engine::OpenGLFramebuffer& Fbo() { return *m_Fbo; }
    Engine::OpenGLGraphicsFactory& Factory() { return *m_Factory; }
    uint32_t RenderableCount() const {
        return m_App->LastProductionRenderableCount();
    }
    Engine::Application& App() { return *m_App; }

private:
    std::unique_ptr<Engine::OpenGLGraphicsFactory> m_Factory;
    std::unique_ptr<Engine::OpenGLContext> m_RenderContext;
    std::unique_ptr<Engine::OpenGLFramebuffer> m_Fbo;
    std::unique_ptr<Engine::Application> m_App;
    std::unique_ptr<Engine::Scene> m_Scene;
    std::shared_ptr<Engine::Texture> m_Texture;
    std::string m_FailStep;
};

// ── 直接读 attachment 1 的原始 ID 行（GL 底层，绕过 ReadPickPixel，
//    用来交叉验证 ReadPickPixel 没有篡改 sentinel）──
int32_t ReadRawIdAt(Engine::OpenGLGraphicsFactory& factory, Engine::OpenGLFramebuffer& fbo,
                    int glX, int glY) {
    auto& gl = factory.GetGLContext();
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo.GetFramebufferID());
    gl.ReadBuffer(GL_COLOR_ATTACHMENT1);
    int32 value = 0x7FFFFFFF;  // 哨兵：万一没写成功也能被发现
    gl.ReadPixels(glX, glY, 1, 1, GL_RED_INTEGER, GL_INT, &value);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return value;
}

// 从 color attachment 实测精灵的屏幕包围盒。
// 这让"已知像素"来自真实帧内容，测试无需复制投影矩阵。
struct Box {
    int minX = kWidth, maxX = -1, minY = kHeight, maxY = -1;
    bool valid() const { return maxX >= minX && maxY >= minY; }
};

Box MeasureColorBox(Engine::OpenGLGraphicsFactory& factory, Engine::OpenGLFramebuffer& fbo) {
    Box box;
    std::vector<uint8_t> pixels(kWidth * kHeight * 4);
    auto& gl = factory.GetGLContext();
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo.GetFramebufferID());
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const size_t o = (static_cast<size_t>(y) * kWidth + x) * 4;
            // 背景是纯黑 (0,0,0,255)
            if (pixels[o] || pixels[o + 1] || pixels[o + 2]) {
                box.minX = std::min(box.minX, x);
                box.maxX = std::max(box.maxX, x);
                box.minY = std::min(box.minY, y);
                box.maxY = std::max(box.maxY, y);
            }
        }
    }
    return box;
}

// 宿主 fixture：每个 TEST 独立搭建/拆除，避免 GL 资源跨用例残留。
class D3PickProof : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(host_.Setup())
            << "native GL production-frame setup failed at step: ["
            << host_.FailStep() << "]";
    }
    void TearDown() override { host_.Teardown(); }

    PickFrameHost host_;
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 1. 核心：已知像素 → 精确 ID
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, KnownInteriorPixel_ReturnsExactEntityId) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());
    ASSERT_GE(host_.RenderableCount(), 2u)
        << "production frame must contain real renderables (D3-C1)";

    const Box box = MeasureColorBox(host_.Factory(), host_.Fbo());
    ASSERT_TRUE(box.valid()) << "color attachment must contain real pixels";

    // 精灵 A（世界 +y）应出现在 attachment 的**上半部**（GL y 较大）。
    const int cx = (box.minX + box.maxX) / 2;
    const int upperGlY = (box.minY + box.maxY) / 2 + (box.maxY - box.minY) / 4;
    const int lowerGlY = (box.minY + box.maxY) / 2 - (box.maxY - box.minY) / 4;

    const int32 rawUpper = ReadRawIdAt(host_.Factory(), host_.Fbo(), cx, upperGlY);
    const int32 rawLower = ReadRawIdAt(host_.Factory(), host_.Fbo(), cx, lowerGlY);

    ASSERT_EQ(rawUpper, static_cast<int32_t>(idA))
        << "upper region must carry the world +y sprite's GameObject::ID";
    ASSERT_EQ(rawLower, static_cast<int32_t>(idB))
        << "lower region must carry the world -y sprite's GameObject::ID";

    // 现在走正式 readback：canonical 坐标为 **top-left 原点**（UI 语义）。
    // D3-C3：这一步确定坐标公式，而不是假设它。
    const auto upper = host_.Fbo().ReadPickPixel(
        static_cast<float>(cx), static_cast<float>(kHeight - 1 - upperGlY), 0.0f, 0.0f);
    const auto lower = host_.Fbo().ReadPickPixel(
        static_cast<float>(cx), static_cast<float>(kHeight - 1 - lowerGlY), 0.0f, 0.0f);

    ASSERT_EQ(upper.status, Engine::OpenGLFramebuffer::PickReadbackStatus::Hit);
    ASSERT_EQ(lower.status, Engine::OpenGLFramebuffer::PickReadbackStatus::Hit);
    EXPECT_EQ(upper.entityId, idA);
    EXPECT_EQ(lower.entityId, idB);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Y 公式判别：证明是 height-1-y，而不是 height-y
//    这是 D3-C3 冻结前必须由 executable evidence 决定的唯一未决项。
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, CoordinateMapping_DiscriminatesHeightMinusOneVsHeight) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    const Box box = MeasureColorBox(host_.Factory(), host_.Fbo());
    ASSERT_TRUE(box.valid());
    const int cx = (box.minX + box.maxX) / 2;

    // 关键：用**实测的精灵上边缘**做判别，而不是"取四分之一位置"这种粗取点。
    // 两个候选公式只差一行；若取点落在精灵内部，两者都会命中，测试就失去判别力。
    // 上边缘正好是"差一行就出界"的位置，因此能真正区分 height-1-y 与 height-y。
    int topGlY = -1;
    for (int y = kHeight - 1; y >= 0; --y) {
        if (ReadRawIdAt(host_.Factory(), host_.Fbo(), cx, y) >= 1) {
            topGlY = y;
            break;
        }
    }
    ASSERT_GE(topGlY, 0) << "could not locate the sprite's top edge in attachment 1";
    ASSERT_LT(topGlY, kHeight - 1) << "sprite touches the top edge; no room to discriminate";

    const int32 truth = ReadRawIdAt(host_.Factory(), host_.Fbo(), cx, topGlY);
    ASSERT_EQ(truth, static_cast<int32_t>(idA));

    // 不去"推导"哪个公式对，而是**实测**：沿中列从 canonical y=0 向下扫，
    // 找到第一个返回 Hit 的 canonical y。再由它反推实现实际用的映射，
    // 最后断言实现等于 contract 冻结的 height-1-y。
    int firstHitCanonical = -1;
    for (int y = 0; y < kHeight; ++y) {
        if (host_.Fbo().ReadPickPixel(static_cast<float>(cx),
                                      static_cast<float>(y), 0.0f, 0.0f).status ==
            Engine::OpenGLFramebuffer::PickReadbackStatus::Hit) {
            firstHitCanonical = y;
            break;
        }
    }
    ASSERT_GE(firstHitCanonical, 0)
        << "no canonical y in the center column ever hit; fixture is not discriminating";

    // 候选 A：canonical -> glY = H - 1 - y
    const int predictedByMinusOne = kHeight - 1 - firstHitCanonical;
    // 候选 B：canonical -> glY = H - y
    const int predictedByPlainHeight = kHeight - firstHitCanonical;

    const bool isMinusOneFormula = (predictedByMinusOne == topGlY);
    const bool isPlainHeightFormula = (predictedByPlainHeight == topGlY);

    // 判别力自检：两个候选必须**恰好一个**成立，否则本 fixture 无法定案。
    ASSERT_TRUE(isMinusOneFormula != isPlainHeightFormula)
        << "the two candidate formulas are indistinguishable at this edge "
        << "(topGlY=" << topGlY << ", firstHitCanonical=" << firstHitCanonical
        << ", H-1-y predicts " << predictedByMinusOne
        << ", H-y predicts " << predictedByPlainHeight << ")";

    // D3-C3 冻结的是 height-1-y（top-left canonical 原点的严格逆映射）。
    EXPECT_TRUE(isMinusOneFormula)
        << "production readback implements 'H - y' (off by one: canonical row 0 is "
           "unreachable and every sample is shifted one row), not the frozen "
           "'H - 1 - y'";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. 背景像素 → NoHit，且 sentinel 确实是 -1（废止条款 3 的执行证明）
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, BackgroundPixel_ReadsBackSentinelMinusOne) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    const Box box = MeasureColorBox(host_.Factory(), host_.Fbo());
    ASSERT_TRUE(box.valid());

    // 左上角，远离两个精灵。
    const int cornerX = 2;
    const int cornerGlY = kHeight - 3;

    // 原始 GL 读取：必须**精确**是 -1。
    // 若 clear 用的是普通 glClear，这里会是 0 —— 即"未命中变假命中"的风险点。
    const int32 raw = ReadRawIdAt(host_.Factory(), host_.Fbo(), cornerX, cornerGlY);
    EXPECT_EQ(raw, -1)
        << "attachment 1 must be explicitly cleared to the -1 sentinel, not 0";

    const auto pick = host_.Fbo().ReadPickPixel(
        static_cast<float>(cornerX), static_cast<float>(3), 0.0f, 0.0f);
    EXPECT_EQ(pick.status, Engine::OpenGLFramebuffer::PickReadbackStatus::NoHit);
    EXPECT_EQ(pick.entityId, 0u) << "NoHit must not carry an entity ID";
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. 0 永远不会成为有效 hit（废止条款 4）
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, ZeroIsNeverAValidHit) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    // 扫描中列若干行，确认没有任何一个有效 hit 的 ID 是 0，
    // 且不存在 0 与 -1 混用导致的歧义。
    for (int y = 0; y < kHeight; y += 7) {
        const int32 raw = ReadRawIdAt(host_.Factory(), host_.Fbo(), kWidth / 2, y);
        EXPECT_NE(raw, 0)
            << "attachment 1 contained a 0 at row " << y
            << "; 0 is reserved/invalid and must never be written as a hit";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. 越界：显式拒绝，绝不 clamp（废止条款 5 / D3-C3）
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, OutOfRange_RejectsWithoutClamping) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    const auto left = host_.Fbo().ReadPickPixel(-50.0f, 300.0f, 0.0f, 0.0f);
    const auto right = host_.Fbo().ReadPickPixel(99999.0f, 300.0f, 0.0f, 0.0f);
    const auto top = host_.Fbo().ReadPickPixel(400.0f, -7.0f, 0.0f, 0.0f);
    const auto bottom = host_.Fbo().ReadPickPixel(400.0f, 99999.0f, 0.0f, 0.0f);

    EXPECT_EQ(left.status, Engine::OpenGLFramebuffer::PickReadbackStatus::OutsideViewport);
    EXPECT_EQ(right.status, Engine::OpenGLFramebuffer::PickReadbackStatus::OutsideViewport);
    EXPECT_EQ(top.status, Engine::OpenGLFramebuffer::PickReadbackStatus::OutsideViewport);
    EXPECT_EQ(bottom.status, Engine::OpenGLFramebuffer::PickReadbackStatus::OutsideViewport);

    // clamp 的后果正是"返回边缘像素的实体 ID"。这里必须一个都没有。
    for (const auto& r : {left, right, top, bottom}) {
        EXPECT_NE(r.status, Engine::OpenGLFramebuffer::PickReadbackStatus::Hit)
            << "out-of-range coordinate was clamped into a hit";
        EXPECT_EQ(r.entityId, 0u);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. 非法坐标：显式拒绝
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, NonFiniteCoordinate_RejectsAsInvalid) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_EQ(host_.Fbo().ReadPickPixel(nan, 300.0f, 0.0f, 0.0f).status,
              Engine::OpenGLFramebuffer::PickReadbackStatus::InvalidCoordinate);
    EXPECT_EQ(host_.Fbo().ReadPickPixel(400.0f, nan, 0.0f, 0.0f).status,
              Engine::OpenGLFramebuffer::PickReadbackStatus::InvalidCoordinate);
    EXPECT_EQ(host_.Fbo().ReadPickPixel(inf, 300.0f, 0.0f, 0.0f).status,
              Engine::OpenGLFramebuffer::PickReadbackStatus::InvalidCoordinate);
    EXPECT_EQ(host_.Fbo().ReadPickPixel(400.0f, -inf, 0.0f, 0.0f).status,
              Engine::OpenGLFramebuffer::PickReadbackStatus::InvalidCoordinate);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. not-ready：无 pick 纹理 → NotReady，**不得**退化成 NoHit（D3-C6）
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, MissingPickTexture_ReportsNotReadyNotNoHit) {
    Engine::OpenGLFramebuffer bare(host_.Factory().GetGLContext());  // 从未 Resize
    const auto pick = bare.ReadPickPixel(10.0f, 10.0f, 0.0f, 0.0f);

    EXPECT_EQ(pick.status, Engine::OpenGLFramebuffer::PickReadbackStatus::NotReady);
    EXPECT_NE(pick.status, Engine::OpenGLFramebuffer::PickReadbackStatus::NoHit)
        << "readback failure must stay distinguishable from no-hit";
    EXPECT_EQ(pick.entityId, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. 废止条款执行证明：pick 资源就绪 + 真实写入发生
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(D3PickProof, ProductionPickPass_ActuallyWroteIds) {
    uint32_t idA = 0, idB = 0;
    ASSERT_TRUE(host_.BuildTwoSpriteScene(idA, idB));
    ASSERT_TRUE(host_.RenderFrame());

    // 至少有一行携带真实 entity ID —— 证明专用 pick shader 真的被绘制了，
    // 而不是"资源创建成功但什么都没写"的静默失败。
    bool sawRealId = false;
    for (int y = 0; y < kHeight && !sawRealId; ++y) {
        const int32 raw = ReadRawIdAt(host_.Factory(), host_.Fbo(), kWidth / 2, y);
        if (raw >= 1) sawRealId = true;
    }
    EXPECT_TRUE(sawRealId)
        << "no pixel carried a valid entity ID; the pick pass did not write";
}
