#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "DebugDrawGLFixture.h"
#include "Engine/Rendering/DebugDraw/DebugDraw.h"
#include "Engine/Rendering/DebugDraw/OpenGL/OpenGL46DebugDrawRenderer.h"
#include "Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter2D.h"
#include "Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter3D.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {
namespace Test {
namespace {

class FakeRenderer final : public IRenderer {
public:
    Status Initialize() override {
        ++initializeCalls;
        if (initializeStatus == Status::Ok) initialized = true;
        return initializeStatus;
    }

    Status Shutdown() override {
        ++shutdownCalls;
        if (shutdownCounter) ++(*shutdownCounter);
        if (shutdownStatus == Status::Ok) initialized = false;
        return shutdownStatus;
    }

    Status Render(const View&, const FrameGeometry& geometry) override {
        ++renderCalls;
        lastDepthVertexCount = geometry.depthVertexCount;
        lastXrayVertexCount = geometry.xrayVertexCount;
        depthVertices.clear();
        xrayVertices.clear();
        if (geometry.depthVertices && geometry.depthVertexCount > 0) {
            depthVertices.assign(geometry.depthVertices,
                                 geometry.depthVertices + geometry.depthVertexCount);
        }
        if (geometry.xrayVertices && geometry.xrayVertexCount > 0) {
            xrayVertices.assign(geometry.xrayVertices,
                                geometry.xrayVertices + geometry.xrayVertexCount);
        }
        return renderStatus;
    }

    bool IsSupported() const override { return supported; }

    Status initializeStatus = Status::Ok;
    Status shutdownStatus = Status::Ok;
    Status renderStatus = Status::Ok;
    bool supported = true;
    bool initialized = false;
    int initializeCalls = 0;
    int shutdownCalls = 0;
    int renderCalls = 0;
    std::shared_ptr<int> shutdownCounter = std::make_shared<int>(0);
    uint32 lastDepthVertexCount = 0;
    uint32 lastXrayVertexCount = 0;
    std::vector<Vertex> depthVertices;
    std::vector<Vertex> xrayVertices;
};

struct FakeService {
    FakeService() {
        auto renderer = std::make_unique<FakeRenderer>();
        fake = renderer.get();
        EXPECT_EQ(service.Initialize(std::move(renderer)), Status::Ok);
    }

    ~FakeService() {
        (void)service.Shutdown();
    }

    DebugDraw service;
    FakeRenderer* fake = nullptr;
};

View IdentityView() {
    View view;
    view.viewProjection = Mat4{};
    return view;
}

Mat4 OrthoViewProjection() {
    const glm::mat4 projection =
        glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    Mat4 out;
    std::memcpy(out.data, glm::value_ptr(projection), sizeof(out.data));
    return out;
}

bool Near(float32 a, float32 b) {
    return std::fabs(a - b) <= 1.0e-5f;
}

int CountNonBackground(const std::vector<uint8_t>& pixels) {
    int count = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const int dr = static_cast<int>(pixels[i]) - 0;
        const int dg = static_cast<int>(pixels[i + 1]) - 0;
        const int db = static_cast<int>(pixels[i + 2]) - 13;
        if (dr * dr + dg * dg + db * db > 400) ++count;
    }
    return count;
}

int CountRed(const std::vector<uint8_t>& pixels) {
    int count = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] > 200 && pixels[i + 1] < 80 && pixels[i + 2] < 80) {
            ++count;
        }
    }
    return count;
}

void BindAndClear(DebugDrawGLFixture& fixture, float depthClear = 1.0f) {
    fixture.Fbo().Bind();
    fixture.GL().ClearColor(0.0f, 0.0f, 0.05f, 1.0f);
    fixture.GL().ClearDepth(static_cast<GLdouble>(depthClear));
    fixture.GL().Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

std::vector<uint8_t> ReadFbo(DebugDrawGLFixture& fixture) {
    fixture.GL().Finish();
    std::vector<uint8_t> pixels(
        static_cast<size_t>(DebugDrawGLFixture::kWidth) *
        DebugDrawGLFixture::kHeight * 4);
    fixture.Fbo().Bind();
    fixture.GL().ReadPixels(0, 0, DebugDrawGLFixture::kWidth,
                            DebugDrawGLFixture::kHeight, GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels.data());
    fixture.Fbo().Unbind();
    return pixels;
}

class GLService {
public:
    explicit GLService(DebugDrawGLFixture& fixture) {
        auto renderer =
            std::make_unique<OpenGL46DebugDrawRenderer>(fixture.GL());
        initializeStatus_ = service_.Initialize(std::move(renderer));
    }

    ~GLService() {
        (void)service_.Shutdown();
    }

    DebugDraw& Service() { return service_; }
    Status InitializeStatus() const { return initializeStatus_; }

private:
    DebugDraw service_;
    Status initializeStatus_ = Status::NotInitialized;
};

} // namespace

// ============================================================
// CPU / Collector
// ============================================================

TEST(DebugDrawCpuTest, LifecycleAndFrameState) {
    DebugDraw service;
    const View view = IdentityView();

    EXPECT_EQ(service.BeginFrame(view, 0.0f), Status::NotInitialized);
    EXPECT_EQ(service.Initialize(nullptr), Status::InvalidArgument);

    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer* fake = renderer.get();
    const std::shared_ptr<int> shutdownCounter = fake->shutdownCounter;
    ASSERT_EQ(service.Initialize(std::move(renderer)), Status::Ok);
    EXPECT_EQ(service.Initialize(nullptr), Status::AlreadyInitialized);
    EXPECT_EQ(fake->initializeCalls, 1);

    EXPECT_EQ(service.EndFrame(), Status::FrameNotStarted);
    EXPECT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    EXPECT_EQ(service.BeginFrame(view, 0.0f), Status::FrameAlreadyStarted);
    EXPECT_EQ(service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                           Vec4(1, 0, 0, 1)), Status::Ok);
    EXPECT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_EQ(service.EndFrame(), Status::FrameNotStarted);
    EXPECT_EQ(fake->renderCalls, 1);

    EXPECT_EQ(service.Shutdown(), Status::Ok);
    EXPECT_EQ(*shutdownCounter, 1);
    EXPECT_EQ(service.Shutdown(), Status::Ok);
    EXPECT_EQ(*shutdownCounter, 1);
    EXPECT_EQ(service.BeginFrame(view, 0.0f), Status::NotInitialized);
}

TEST(DebugDrawCpuTest, WrongThreadIsRejected) {
    FakeService fixture;
    const View view = IdentityView();

    std::atomic<int> status{static_cast<int>(Status::Ok)};
    std::thread worker([&] {
        status.store(static_cast<int>(fixture.service.BeginFrame(view, 0.0f)));
    });
    worker.join();

    EXPECT_EQ(static_cast<Status>(status.load()), Status::WrongThread);
}

TEST(DebugDrawCpuTest, FivePrimitiveVertexCounts) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    EXPECT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1)), Status::Ok);
    EXPECT_EQ(fixture.service.WireBox(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f),
                                      Quat::Identity(),
                                      Vec4(0, 1, 0, 1)), Status::Ok);
    EXPECT_EQ(fixture.service.WireSphere(Vec3(0, 0, 0), 0.5f,
                                         Vec4(0, 0, 1, 1)), Status::Ok);
    EXPECT_EQ(fixture.service.Axis(Vec3(0, 0, 0), Quat::Identity(), 0.5f),
              Status::Ok);
    Vec3 corners[8] = {
        Vec3(-0.8f, -0.8f, 0.5f), Vec3(0.8f, -0.8f, 0.5f),
        Vec3(0.8f, 0.8f, 0.5f), Vec3(-0.8f, 0.8f, 0.5f),
        Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, -0.5f, -0.5f),
        Vec3(0.5f, 0.5f, -0.5f), Vec3(-0.5f, 0.5f, -0.5f)
    };
    EXPECT_EQ(fixture.service.Frustum(corners, Vec4(1, 1, 0, 1)), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);

    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 152u);
    EXPECT_EQ(fixture.fake->lastXrayVertexCount, 0u);
    EXPECT_EQ(fixture.fake->depthVertices.size(), 152u);
}

TEST(DebugDrawCpuTest, PrimitiveGeometryIsExpandedCorrectly) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(fixture.service.WireSphere(Vec3(1, 2, 3), 2.0f,
                                         Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    ASSERT_EQ(fixture.fake->depthVertices.size(), 96u);

    for (const Vertex& vertex : fixture.fake->depthVertices) {
        const float32 dx = vertex.position.x - 1.0f;
        const float32 dy = vertex.position.y - 2.0f;
        const float32 dz = vertex.position.z - 3.0f;
        EXPECT_TRUE(Near(std::sqrt(dx * dx + dy * dy + dz * dz), 2.0f));
    }

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(fixture.service.Axis(Vec3(0, 0, 0), Quat::Identity(), 1.0f),
              Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    ASSERT_EQ(fixture.fake->depthVertices.size(), 6u);
    EXPECT_EQ(fixture.fake->depthVertices[0].color, Vec4(1, 0, 0, 1));
    EXPECT_EQ(fixture.fake->depthVertices[2].color, Vec4(0, 1, 0, 1));
    EXPECT_EQ(fixture.fake->depthVertices[4].color, Vec4(0, 0, 1, 1));
}

TEST(DebugDrawCpuTest, InvalidArgumentsAreRejected) {
    FakeService fixture;
    const View view = IdentityView();
    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);

    EXPECT_EQ(fixture.service.Line(Vec3(std::nanf(""), 0, 0),
                                   Vec3(1, 0, 0), Vec4(1, 0, 0, 1)),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1.1f, 0, 0, 1)),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1), -1.0f),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.WireSphere(Vec3(0, 0, 0), 0.0f,
                                         Vec4(1, 0, 0, 1)),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.WireBox(Vec3(0, 0, 0), Vec3(0, 1, 1),
                                      Quat::Identity(), Vec4(1, 0, 0, 1)),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.WireBox(Vec3(0, 0, 0), Vec3(1, 1, 1),
                                      Quat(0, 0, 0, 0), Vec4(1, 0, 0, 1)),
              Status::InvalidArgument);
    EXPECT_EQ(fixture.service.Axis(Vec3(0, 0, 0), Quat::Identity(), 0.0f),
              Status::InvalidArgument);

    Vec3 corners[8] = {};
    corners[3].x = std::nanf("");
    EXPECT_EQ(fixture.service.Frustum(corners, Vec4(1, 0, 0, 1)),
              Status::InvalidArgument);

    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 0u);
}

TEST(DebugDrawCpuTest, DurationExpiresAtBeginFrame) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1), 1.0f), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 2u);

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.6f), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 2u);

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.5f), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 0u);
}

TEST(DebugDrawCpuTest, BudgetDropsWholeCommands) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    for (int i = 0; i < 520; ++i) {
        ASSERT_EQ(fixture.service.WireSphere(Vec3(0, 0, 0), 0.1f,
                                             Vec4(1, 0, 0, 1)), Status::Ok);
    }
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(fixture.service.WireBox(Vec3(0, 0, 0), Vec3(0.1f, 0.1f, 0.1f),
                                          Quat::Identity(),
                                          Vec4(0, 1, 0, 1)), Status::Ok);
    }
    EXPECT_EQ(fixture.service.WireSphere(Vec3(0, 0, 0), 0.1f,
                                         Vec4(0, 0, 1, 1)), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);

    const Stats& stats = fixture.service.GetStats();
    EXPECT_EQ(stats.acceptedVertices, 49992u);
    EXPECT_EQ(stats.droppedCommands, 1u);
    EXPECT_EQ(stats.droppedVertices, 96u);
    EXPECT_EQ(stats.capacityVertices, kMaxVerticesPerFrame);
    EXPECT_TRUE(stats.budgetWarning);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 49992u);
}

TEST(DebugDrawCpuTest, BudgetAcceptsExactlyCapacity) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    for (int i = 0; i < 500; ++i) {
        ASSERT_EQ(fixture.service.WireSphere(Vec3(0, 0, 0), 0.1f,
                                             Vec4(1, 0, 0, 1)), Status::Ok);
    }
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                       Vec4(1, 0, 0, 1)), Status::Ok);
    }
    EXPECT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);

    const Stats& stats = fixture.service.GetStats();
    EXPECT_EQ(stats.acceptedVertices, kMaxVerticesPerFrame);
    EXPECT_EQ(stats.droppedCommands, 1u);
    EXPECT_EQ(stats.droppedVertices, 2u);
}

TEST(DebugDrawCpuTest, BudgetIncludesPersistentCommands) {
    FakeService fixture;
    const View view = IdentityView();

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1), 10.0f), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);

    ASSERT_EQ(fixture.service.BeginFrame(view, 0.0f), Status::Ok);
    for (int i = 0; i < 24999; ++i) {
        ASSERT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                       Vec4(1, 0, 0, 1)), Status::Ok);
    }
    EXPECT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);

    const Stats& stats = fixture.service.GetStats();
    EXPECT_EQ(stats.acceptedVertices, kMaxVerticesPerFrame);
    EXPECT_EQ(stats.droppedCommands, 1u);
    EXPECT_EQ(stats.droppedVertices, 2u);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, kMaxVerticesPerFrame);
}

TEST(DebugDrawCpuTest, BackendFailuresAreObservable) {
    auto renderer = std::make_unique<FakeRenderer>();
    renderer->initializeStatus = Status::Unsupported;
    DebugDraw service;
    EXPECT_EQ(service.Initialize(std::move(renderer)), Status::Unsupported);
    EXPECT_FALSE(service.IsInitialized());

    FakeService fixture;
    fixture.fake->renderStatus = Status::BackendFailure;
    ASSERT_EQ(fixture.service.BeginFrame(IdentityView(), 0.0f), Status::Ok);
    ASSERT_EQ(fixture.service.Line(Vec3(0, 0, 0), Vec3(1, 0, 0),
                                   Vec4(1, 0, 0, 1)), Status::Ok);
    EXPECT_EQ(fixture.service.EndFrame(), Status::BackendFailure);
    EXPECT_EQ(fixture.service.BeginFrame(IdentityView(), 0.0f), Status::Ok);
}

// ============================================================
// Physics adapters
// ============================================================

TEST(DebugDrawAdapterTest, TwoDimensionalMappingAndFlags) {
    FakeService fixture;
    ASSERT_EQ(fixture.service.BeginFrame(IdentityView(), 0.0f), Status::Ok);

    PhysicsDebugDrawAdapter2D adapter(fixture.service);
    Mat4 translation;
    translation.data[12] = 1.0f;
    translation.data[13] = 2.0f;
    adapter.SetPhysicsToWorld(translation);

    const Vec2 segmentFrom(0.0f, 0.0f);
    const Vec2 segmentTo(1.0f, 0.0f);
    adapter.DrawSegment(segmentFrom, segmentTo, DebugColor(1, 0, 0, 1));
    ASSERT_EQ(adapter.GetLastStatus(), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    ASSERT_EQ(fixture.fake->depthVertices.size(), 2u);
    EXPECT_TRUE(Near(fixture.fake->depthVertices[0].position.x, 1.0f));
    EXPECT_TRUE(Near(fixture.fake->depthVertices[0].position.y, 2.0f));
    EXPECT_TRUE(Near(fixture.fake->depthVertices[1].position.x, 2.0f));
    EXPECT_TRUE(Near(fixture.fake->depthVertices[1].position.y, 2.0f));

    ASSERT_EQ(fixture.service.BeginFrame(IdentityView(), 0.0f), Status::Ok);
    adapter.SetFlags(DebugDraw_None);
    const Vec2 square[4] = {
        Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1)
    };
    adapter.DrawPolygon(square, 4, DebugColor(0, 1, 0, 1));
    adapter.DrawSolidCircle(Vec2(0, 0), 0.5f, Vec2(1, 0),
                            DebugColor(0, 1, 0, 1));
    adapter.DrawPoint(Vec2(0, 0), 1.0f, DebugColor(1, 1, 1, 1));
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 0u);
}

TEST(DebugDrawAdapterTest, ThreeDimensionalAdapterForwardsLinePrimitives) {
    FakeService fixture;
    ASSERT_EQ(fixture.service.BeginFrame(IdentityView(), 0.0f), Status::Ok);

    PhysicsDebugDrawAdapter3D adapter(fixture.service);
    adapter.DrawLine(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec4(1, 0, 0, 1));
    adapter.DrawTriangle(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0),
                         Vec4(0, 1, 0, 1));
    adapter.DrawSphere(Vec3(0, 0, 0), 0.5f, Vec4(0, 0, 1, 1));
    adapter.DrawBox(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f),
                    Vec4(1, 1, 0, 1));
    adapter.DrawCapsule(Vec3(0, 0, 0), 0.5f, 1.0f, Vec4(1, 0, 0, 1));
    adapter.DrawCylinder(Vec3(0, 0, 0), 0.5f, 1.0f, Vec4(0, 1, 0, 1));
    adapter.DrawAxes(Mat4{}, 0.5f);
    adapter.DrawText3D(Vec3(0, 0, 0), "text", Vec4(1, 1, 1, 1));
    adapter.Clear();
    adapter.Flush();

    ASSERT_EQ(adapter.GetLastStatus(), Status::Ok);
    ASSERT_EQ(fixture.service.EndFrame(), Status::Ok);
    EXPECT_EQ(fixture.fake->lastDepthVertexCount, 286u);
}

// ============================================================
// OpenGL 4.6 backend
// ============================================================

TEST(DebugDrawGLTest, ContextSupportAndLifecycle) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    OpenGL46DebugDrawRenderer renderer(fixture.GL());
    EXPECT_TRUE(renderer.IsSupported());
    EXPECT_EQ(renderer.Initialize(), Status::Ok);
    EXPECT_EQ(renderer.Initialize(), Status::AlreadyInitialized);
    EXPECT_EQ(renderer.Shutdown(), Status::Ok);
    EXPECT_EQ(renderer.Shutdown(), Status::Ok);
}

TEST(DebugDrawGLTest, MissingContextIsReported) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    OpenGL46DebugDrawRenderer renderer(fixture.GL());
    ASSERT_TRUE(fixture.Factory().UncurrentPersistentContext());
    EXPECT_FALSE(renderer.IsSupported());
    EXPECT_EQ(renderer.Initialize(), Status::BackendFailure);
    ASSERT_TRUE(fixture.Factory().MakePersistentContextCurrent());
    EXPECT_EQ(renderer.Shutdown(), Status::Ok);
}

TEST(DebugDrawGLTest, ShutdownWithoutContextFailsThenRetries) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    OpenGL46DebugDrawRenderer renderer(fixture.GL());
    ASSERT_EQ(renderer.Initialize(), Status::Ok);
    ASSERT_TRUE(fixture.Factory().UncurrentPersistentContext());
    EXPECT_EQ(renderer.Shutdown(), Status::BackendFailure);
    ASSERT_TRUE(fixture.Factory().MakePersistentContextCurrent());
    EXPECT_EQ(renderer.Shutdown(), Status::Ok);
}

TEST(DebugDrawGLTest, LineAndAllPrimitivesProducePixels) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    GLService glService(fixture);
    ASSERT_EQ(glService.InitializeStatus(), Status::Ok);
    DebugDraw& service = glService.Service();
    View view;
    view.viewProjection = OrthoViewProjection();

    BindAndClear(fixture);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Line(Vec3(-0.8f, 0.0f, 0.0f),
                           Vec3(0.8f, 0.0f, 0.0f),
                           Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountRed(ReadFbo(fixture)), 0);

    BindAndClear(fixture);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.WireBox(Vec3(0, 0, 0), Vec3(0.4f, 0.4f, 0.4f),
                              Quat::Identity(), Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountRed(ReadFbo(fixture)), 0);

    BindAndClear(fixture);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.WireSphere(Vec3(0, 0, 0), 0.5f,
                                 Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountRed(ReadFbo(fixture)), 0);

    BindAndClear(fixture);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Axis(Vec3(0, 0, 0), Quat::Identity(), 0.5f),
              Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountNonBackground(ReadFbo(fixture)), 0);

    Vec3 corners[8] = {
        Vec3(-0.8f, -0.8f, 0.5f), Vec3(0.8f, -0.8f, 0.5f),
        Vec3(0.8f, 0.8f, 0.5f), Vec3(-0.8f, 0.8f, 0.5f),
        Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, -0.5f, -0.5f),
        Vec3(0.5f, 0.5f, -0.5f), Vec3(-0.5f, 0.5f, -0.5f)
    };
    BindAndClear(fixture);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Frustum(corners, Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountRed(ReadFbo(fixture)), 0);
}

TEST(DebugDrawGLTest, DepthAndXRayBatchesDiffer) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    GLService glService(fixture);
    ASSERT_EQ(glService.InitializeStatus(), Status::Ok);
    DebugDraw& service = glService.Service();
    View view;
    view.viewProjection = OrthoViewProjection();

    BindAndClear(fixture, 0.0f);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Line(Vec3(-0.8f, 0.0f, 0.0f),
                           Vec3(0.8f, 0.0f, 0.0f),
                           Vec4(1, 0, 0, 1), 0.0f, true), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_EQ(CountRed(ReadFbo(fixture)), 0);

    BindAndClear(fixture, 0.0f);
    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Line(Vec3(-0.8f, 0.0f, 0.0f),
                           Vec3(0.8f, 0.0f, 0.0f),
                           Vec4(1, 0, 0, 1), 0.0f, false), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);
    EXPECT_GT(CountRed(ReadFbo(fixture)), 0);

    fixture.GL().ClearDepth(1.0);
}

TEST(DebugDrawGLTest, GLStateIsRestored) {
    DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();

    GLService glService(fixture);
    ASSERT_EQ(glService.InitializeStatus(), Status::Ok);
    DebugDraw& service = glService.Service();
    View view;
    view.viewProjection = OrthoViewProjection();

    BindAndClear(fixture);
    fixture.GL().Enable(GL_BLEND);
    fixture.GL().BlendFunc(GL_ONE, GL_ZERO);
    fixture.GL().BlendEquation(GL_FUNC_SUBTRACT);
    fixture.GL().Disable(GL_DEPTH_TEST);
    fixture.GL().DepthFunc(GL_ALWAYS);
    fixture.GL().DepthMask(GL_FALSE);
    fixture.GL().LineWidth(2.0f);
    fixture.GL().UseProgram(0);
    fixture.GL().BindVertexArray(0);
    fixture.GL().BindBuffer(GL_ARRAY_BUFFER, 0);

    ASSERT_EQ(service.BeginFrame(view, 0.0f), Status::Ok);
    ASSERT_EQ(service.Line(Vec3(-0.8f, 0.0f, 0.0f),
                           Vec3(0.8f, 0.0f, 0.0f),
                           Vec4(1, 0, 0, 1)), Status::Ok);
    ASSERT_EQ(service.EndFrame(), Status::Ok);

    GLboolean blend = GL_FALSE;
    GLboolean depth = GL_TRUE;
    GLboolean depthMask = GL_TRUE;
    GLint depthFunc = 0;
    GLint srcRGB = 0;
    GLint dstRGB = 0;
    GLint eqRGB = 0;
    GLint program = -1;
    GLint vertexArray = -1;
    GLint arrayBuffer = -1;
    GLfloat lineWidth = 0.0f;
    fixture.GL().GetBooleanv(GL_BLEND, &blend);
    fixture.GL().GetBooleanv(GL_DEPTH_TEST, &depth);
    fixture.GL().GetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    fixture.GL().GetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    fixture.GL().GetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
    fixture.GL().GetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
    fixture.GL().GetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB);
    fixture.GL().GetIntegerv(GL_CURRENT_PROGRAM, &program);
    fixture.GL().GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    fixture.GL().GetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    fixture.GL().GetFloatv(GL_LINE_WIDTH, &lineWidth);

    EXPECT_EQ(blend, GL_TRUE);
    EXPECT_EQ(depth, GL_FALSE);
    EXPECT_EQ(depthMask, GL_FALSE);
    EXPECT_EQ(depthFunc, GL_ALWAYS);
    EXPECT_EQ(srcRGB, GL_ONE);
    EXPECT_EQ(dstRGB, GL_ZERO);
    EXPECT_EQ(eqRGB, GL_FUNC_SUBTRACT);
    EXPECT_EQ(program, 0);
    EXPECT_EQ(vertexArray, 0);
    EXPECT_EQ(arrayBuffer, 0);
    EXPECT_TRUE(Near(lineWidth, 2.0f));
}

} // namespace Test
} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
