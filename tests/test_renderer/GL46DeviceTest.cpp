/**
 * @file GL46DeviceTest.cpp
 * @brief GL46 设备无头 GPU 测试
 *
 * 测试重点：
 * - 设备创建/缓冲分配
 * - 持久映射读写
 * - Compute Shader 执行
 *
 * 注意：无 GL 4.6 GPU 时自动跳过
 */
#include <gtest/gtest.h>
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <GLFW/glfw3.h>
#include <Engine/Core/StringID.h>
#include <memory>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <vector>
#include <filesystem>
#include <string>
// Runner OpenGL capability probe. glad/gl.h (not glad/glad.h) is what this
// project vendors, and it provides the GladGLContext plus gladLoadGL.
#include <glad/gl.h>
// GPU skinning pixel verification uses the production graphics contract.
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Animation/SkinnedMeshDraw.h"
#include "Engine/Core/GameObject/MeshRendererComponent.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/OpenGL/OpenGLContext.h"

using namespace Engine;
using namespace Engine::RHI;

class GL46DeviceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!glfwInit()) { return; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        // Deliberately requests 3.3 core rather than 4.6 with a fallback.
        // Nothing in this verification needs a 4.6 feature: the uniform array,
        // the four bone weighted blend, explicit attribute locations and the
        // offscreen draw are all available at 3.3. Asking for the minimum keeps
        // the local and CI execution paths identical, and the runner was
        // observed refusing a 4.6 core context outright.
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        s_Window = glfwCreateWindow(1, 1, "GL46Test", nullptr, nullptr);
        if (!s_Window) { glfwTerminate(); return; }
        glfwMakeContextCurrent(s_Window);

        s_Device = std::make_unique<GL46Device>();
        if (!s_Device->Initialize(nullptr, 1, 1)) {
            std::printf("  [WARN] GL46Device init failed\n");
            s_Device.reset();
        }
    }

    static void TearDownTestSuite() {
        // GL46Device::Shutdown() 是 private，依赖 unique_ptr 析构自动清理
        s_Device.reset();
        if (s_Window) { glfwDestroyWindow(s_Window); s_Window = nullptr; }
        glfwTerminate();
    }

    void SetUp() override {
        if (!s_Device) GTEST_SKIP() << "No GL 4.6 GPU available";
    }

    static std::unique_ptr<GL46Device> s_Device;
    static GLFWwindow* s_Window;
};

std::unique_ptr<GL46Device> GL46DeviceTest::s_Device = nullptr;
GLFWwindow* GL46DeviceTest::s_Window = nullptr;

TEST_F(GL46DeviceTest, DeviceName) {
    const char* name = s_Device->GetDeviceName();
    EXPECT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0);
    std::printf("  [INFO] GPU: %s\n", name);
}

TEST_F(GL46DeviceTest, CreateBuffer) {
    RHIBufferDesc desc;
    desc.size = 1024;
    desc.memoryUsage = MemoryUsage::GPU_Only;

    auto buffer = s_Device->CreateBuffer(desc);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->GetSize(), 1024);   // IRHIBuffer 契约接口（GetDesc 已移除）
}

TEST_F(GL46DeviceTest, BufferPersistentMapping) {
    RHIBufferDesc desc;
    desc.size = 64;
    desc.memoryUsage = MemoryUsage::GPU_Only;

    auto buffer = s_Device->CreateBuffer(desc);
    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer.get());
    ASSERT_NE(gl46Buf, nullptr);

    void* mapped = gl46Buf->GetPersistentPtr();
    ASSERT_NE(mapped, nullptr);

    const char* testData = "Hello GPU!";
    std::memcpy(mapped, testData, 11);

    char readback[11];
    std::memcpy(readback, mapped, 11);
    EXPECT_EQ(std::memcmp(readback, testData, 11), 0);
}

TEST_F(GL46DeviceTest, CreateCommandList) {
    auto cmdList = s_Device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmdList, nullptr);
}

TEST_F(GL46DeviceTest, CreateAndDispatchComputeShader) {
    auto cmdList = s_Device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmdList, nullptr);

    // 创建 SSBO
    RHIBufferDesc bufDesc;
    bufDesc.size = 64;
    bufDesc.memoryUsage = MemoryUsage::GPU_Only;
    auto buffer = s_Device->CreateBuffer(bufDesc);

    // 创建 Compute PSO
    ComputePSODesc psoDesc;
    psoDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    auto* pso = s_Device->CreateComputePSO(psoDesc);
    ASSERT_NE(pso, nullptr);

    // Dispatch
    cmdList->Begin();
    cmdList->SetPipelineState(pso);
    cmdList->SetUnorderedAccess(0, buffer.get());
    cmdList->SetComputeFloat("u_Dt", 1.0f / 60.0f);
    cmdList->Dispatch(1, 1, 1);
    cmdList->End();
}

TEST_F(GL46DeviceTest, BufferSizeZero) {
    RHIBufferDesc desc;
    desc.size = 0;
    desc.memoryUsage = MemoryUsage::GPU_Only;
    auto buffer = s_Device->CreateBuffer(desc);
    // 大小为零的缓冲可能返回 nullptr 或有效缓冲（取决于实现）
    // 只要不崩溃即可
    SUCCEED();
}
// ============================================================================
// GPU skinning pixel verification (production draw path, real GL context)
// ============================================================================
//
// Closes the gap left open by 04d9215: that commit added the rendering
// contract (matrix array uniform + skinned shader pair) but nothing ever
// compiled or drew them. This drives the real production chain:
//
//   OpenGLGraphicsFactory (loads glad) -> CreateRenderContext -> Init -> OnResize
//     -> CreateShader(skinned_lit.vert/.frag)
//     -> CreateVertexBuffer / CreateIndexBuffer / CreateVertexArray
//     -> VertexArray::AddVertexBuffer(custom layout) + SetIndexBuffer
//     -> Shader::SetMat4Array("u_BoneMatrices", ...)
//     -> IRenderContext::DrawIndexed
//     -> IRenderContext::CaptureFrameBuffer   (real dimensions, real pixels)
//
// Scope notes:
//   * It does NOT use GL46Device::Initialize(nullptr,1,1), which fails on this
//     host and is why the pre-existing GPU cases SKIP.
//   * Failure is FAILURE, never SKIP. A skip here would prove nothing.
//
// Three preconditions are established explicitly rather than left implicit,
// because each one silently produced an all-zero framebuffer otherwise:
//   1. A context must be current. The factory constructor creates and destroys
//      its own temporary context to load glad, which leaves none current, and
//      every GL call then no-ops (observed as a zero byte shader info log).
//   2. OpenGLContext learns its size through OnResize. Until that is called the
//      viewport and the readback are both 0x0.
//   3. Readback dimensions come from CaptureFrameBuffer and are used as-is;
//      nothing assumes 64x64.

namespace {

struct GpuSkinVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
    float tangent[3];
    int   boneIndices[4];
    float boneWeights[4];
};

// Real readback: pixels plus the dimensions they actually arrived with.
struct GpuCapture {
    std::vector<uint8_t> px;
    int w = 0;
    int h = 0;
    bool Valid() const { return w > 0 && h > 0 && !px.empty(); }
    // The driver may hand back fewer complete rows than the reported height
    // (observed 64x64 reported, 48 rows delivered). Derive what is actually
    // addressable from the buffer rather than trusting h.
    int Rows() const
    {
        if (!Valid()) return 0;
        const size_t perRow = static_cast<size_t>(w) * 4;
        const int rows = static_cast<int>(px.size() / perRow);
        return rows < h ? rows : h;
    }
};

void GpuMakeTranslate(float x, float y, float z, float* out)
{
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    out[12] = x; out[13] = y; out[14] = z;
}

// Counts lit pixels in an inclusive column range, using the real dimensions.
int GpuLitInColumns(const GpuCapture& cap, int c0, int c1)
{
    if (!cap.Valid()) return -1;
    const int rows = cap.Rows();
    if (rows <= 0) return -1;
    const int lo = (c0 < 0) ? 0 : c0;
    const int hi = (c1 >= cap.w) ? cap.w - 1 : c1;
    int n = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = lo; x <= hi; ++x) {
            const uint8_t* q = &cap.px[static_cast<size_t>((y * cap.w + x) * 4)];
            if (q[0] > 8 || q[1] > 8 || q[2] > 8) ++n;
        }
    }
    return n;
}

int GpuCountLit(const GpuCapture& cap)
{
    if (!cap.Valid()) return -1;
    int n = 0;
    for (size_t i = 0; i + 3 < cap.px.size(); i += 4) {
        if (cap.px[i] > 8 || cap.px[i + 1] > 8 || cap.px[i + 2] > 8) ++n;
    }
    return n;
}

// Prints the GLSL info log when a program handle is 0. The production layer
// only records a Failed state, so without this the two very different causes
// "file not found" and "bad GLSL" are indistinguishable from the assertion.
void GpuDumpShaderLogs(GladGLContext& gl, const char* vertPath, const char* fragPath)
{
    for (int pass = 0; pass < 2; ++pass) {
        const char* path = pass == 0 ? vertPath : fragPath;
        const uint32 stage = pass == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
        std::FILE* f = std::fopen(path, "rb");
        if (!f) { std::printf("  [GLSL] cannot open %s\n", path); continue; }
        std::string src;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) src.append(buf, n);
        std::fclose(f);

        uint32 sh = gl.CreateShader(stage);
        const char* c = src.c_str();
        gl.ShaderSource(sh, 1, &c, nullptr);
        gl.CompileShader(sh);
        int ok = 0;
        gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (ok) {
            std::printf("  [GLSL] %s compiles\n", path);
        } else {
            char log[8192] = { 0 };
            GLsizei len = 0;
            gl.GetShaderInfoLog(sh, sizeof(log), &len, log);
            std::printf("  [GLSL] %s FAILED (%d bytes):\n%s\n", path, static_cast<int>(len), log);
        }
        gl.DeleteShader(sh);
    }
}

}  // namespace

class GPUSkinningPixelTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        if (!glfwInit()) { std::printf("  [FATAL] glfwInit failed\n"); return; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        s_Window = glfwCreateWindow(64, 64, "GPUSkinPixel", nullptr, nullptr);
        if (!s_Window) { std::printf("  [FATAL] GL 4.6 core context creation failed\n"); return; }
        glfwMakeContextCurrent(s_Window);

        s_Factory = std::make_unique<OpenGLGraphicsFactory>();
        // CreateRenderContext static_casts this back to GLFWwindow*; it is not
        // an HWND. The factory constructor is what loads glad.
        s_Context = s_Factory->CreateRenderContext(static_cast<void*>(s_Window));

        // (1) The factory constructor destroys its own loader context, so make
        //     ours current again before touching GL.
        // (2) OpenGLContext takes its size from OnResize; until then viewport
        //     and readback are 0x0.
        if (s_Context) {
            glfwMakeContextCurrent(s_Window);
            s_Context->Init();
            s_Context->OnResize(64, 64);
        }
    }

    static void TearDownTestSuite()
    {
        s_Context.reset();
        s_Factory.reset();
        if (s_Window) { glfwDestroyWindow(s_Window); s_Window = nullptr; }
        glfwTerminate();
    }

    void SetUp() override
    {
        ASSERT_NE(s_Window, nullptr)
            << "no GL 4.6 core context; GPU skinning verification cannot be skipped";
        ASSERT_NE(s_Context.get(), nullptr)
            << "render context creation failed; GPU skinning verification cannot be skipped";
    }

    // Locates the repository asset root so the shaders resolve whether the
    // process runs from the repo root or from the CMake test binary directory
    // (CI runs ctest with the working directory set to the build output dir).
    static std::string ResolveShaderPath(const char* relative)
    {
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::current_path(ec);
        for (int i = 0; i < 8 && !dir.empty(); ++i) {
            std::filesystem::path candidate = dir / relative;
            if (std::filesystem::exists(candidate)) {
                return candidate.string();
            }
            dir = dir.parent_path();
        }
        return std::string(relative);
    }

    struct Rig {
        std::shared_ptr<Shader>      shader;
        std::shared_ptr<VertexArray> vao;
    };

    Rig BuildRig()
    {
        Rig rig;
        // Deterministic quad: every vertex fully bound to bone 0.
        const float quad[4][2] = { {-0.45f, -0.45f}, { 0.45f, -0.45f},
                                   { 0.45f,  0.45f}, {-0.45f,  0.45f} };
        std::vector<GpuSkinVertex> verts;
        for (int i = 0; i < 4; ++i) {
            GpuSkinVertex v{};
            v.position[0] = quad[i][0]; v.position[1] = quad[i][1]; v.position[2] = 0.0f;
            v.normal[0] = 0.0f; v.normal[1] = 0.0f; v.normal[2] = 1.0f;
            v.texCoord[0] = static_cast<float>(i); v.texCoord[1] = 0.0f;
            v.tangent[0] = 1.0f;
            v.boneIndices[0] = 0; v.boneIndices[1] = 0; v.boneIndices[2] = 0; v.boneIndices[3] = 0;
            v.boneWeights[0] = 1.0f; v.boneWeights[1] = 0.0f;
            v.boneWeights[2] = 0.0f; v.boneWeights[3] = 0.0f;
            verts.push_back(v);
        }
        uint32 idx[6] = { 0, 1, 2, 2, 3, 0 };

        rig.shader = s_Factory->CreateShader(ResolveShaderPath("assets/shaders/skinned_lit.vert"),
                                             ResolveShaderPath("assets/shaders/skinned_lit.frag"));
        auto vb = s_Factory->CreateVertexBuffer(
            reinterpret_cast<float*>(verts.data()),
            static_cast<uint32>(verts.size() * sizeof(GpuSkinVertex)));
        auto ib = s_Factory->CreateIndexBuffer(idx, 6);
        rig.vao = s_Factory->CreateVertexArray();

        const VertexAttribute attrs[6] = {
            { 0, 3, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, position)    },
            { 1, 3, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, normal)      },
            { 2, 2, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, texCoord)    },
            { 3, 3, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, tangent)     },
            { 4, 4, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, boneIndices) },
            { 5, 4, sizeof(GpuSkinVertex), offsetof(GpuSkinVertex, boneWeights) },
        };
        rig.vao->AddVertexBuffer(vb, attrs, 6);
        rig.vao->SetIndexBuffer(ib);
        return rig;
    }

    // Draws the rig with bone 0 translated by boneX and returns the real readback.
    //
    // The render target is a private FBO rather than the default framebuffer.
    // Reading framebuffer 0 of a hidden window returned uninitialised memory in
    // the regions that were never drawn (observed as an even spread of "lit"
    // pixels across every column), which is indistinguishable from a false
    // positive. An FBO is fully defined by us, so every pixel is either the
    // clear colour or something we drew.
    GpuCapture DrawWithBoneX(float boneX, const Rig& rig)
    {
        glfwMakeContextCurrent(s_Window);
        auto& gl = static_cast<OpenGLContext*>(s_Context.get())->GetGL();

        GpuCapture cap;
        if (!EnsureFbo()) return cap;
        cap.w = FboWidth();
        cap.h = FboHeight();

        gl.BindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
        gl.Viewport(0, 0, cap.w, cap.h);
        gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl.ClearDepth(0.0);              // engine uses reverse-Z
        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.Enable(GL_DEPTH_TEST);

        float vp[16];
        GpuMakeTranslate(0.0f, 0.0f, 0.0f, vp);

        // Two bones: bone 0 carries the geometry, bone 1 stays identity.
        float bones[32];
        GpuMakeTranslate(boneX, 0.0f, 0.0f, &bones[0]);
        GpuMakeTranslate(0.0f, 0.0f, 0.0f, &bones[16]);

        rig.shader->Bind();
        rig.shader->SetMat4("u_ViewProjection", vp);
        rig.shader->SetMat4Array("u_BoneMatrices", bones, 2);   // the call under test
        s_Context->DrawIndexed(rig.vao);
        gl.Finish();

        cap.px.assign(static_cast<size_t>(cap.w) * cap.h * 4, 0);
        gl.ReadPixels(0, 0, cap.w, cap.h, GL_RGBA, GL_UNSIGNED_BYTE, cap.px.data());
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return cap;
    }

    bool EnsureFbo()
    {
        auto& gl = static_cast<OpenGLContext*>(s_Context.get())->GetGL();
        if (s_FboReady) return true;
        gl.GenFramebuffers(1, &s_Fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
        gl.GenTextures(1, &s_FboColor);
        gl.BindTexture(GL_TEXTURE_2D, s_FboColor);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FboWidth(), FboHeight(), 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                s_FboColor, 0);
        gl.GenRenderbuffers(1, &s_FboDepth);
        gl.BindRenderbuffer(GL_RENDERBUFFER, s_FboDepth);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, FboWidth(), FboHeight());
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                   s_FboDepth);
        const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        s_FboReady = ok;
        return ok;
    }

    static int FboWidth()  { return 64; }
    static int FboHeight() { return 64; }

    // Bands are derived from the real width, never hard coded.
    struct Bands { int left, centre, right; };
    Bands MakeBands(int w) const
    {
        Bands b;
        b.left   = 0;
        b.centre = w / 4;
        b.right  = (w * 3) / 4;
        return b;
    }

    Rig RequireWorkingRig()
    {
        Rig rig = BuildRig();
        EXPECT_NE(rig.shader, nullptr);
        if (rig.shader && rig.shader->GetNativeHandle() == 0u) {
            GpuDumpShaderLogs(static_cast<OpenGLContext*>(s_Context.get())->GetGL(),
                              ResolveShaderPath("assets/shaders/skinned_lit.vert").c_str(),
                              ResolveShaderPath("assets/shaders/skinned_lit.frag").c_str());
        }
        // Shader failure must FAIL the test, not pass quietly.
        EXPECT_NE(rig.shader ? rig.shader->GetNativeHandle() : 0u, 0u)
            << "skinned_lit program handle is 0 -> compile or link failed";
        return rig;
    }

    static GLFWwindow*                            s_Window;
    static std::unique_ptr<OpenGLGraphicsFactory> s_Factory;
    static std::unique_ptr<IRenderContext>        s_Context;
    static uint32 s_Fbo;
    static uint32 s_FboColor;
    static uint32 s_FboDepth;
    static bool   s_FboReady;
};

GLFWwindow* GPUSkinningPixelTest::s_Window = nullptr;
std::unique_ptr<OpenGLGraphicsFactory> GPUSkinningPixelTest::s_Factory;
std::unique_ptr<IRenderContext> GPUSkinningPixelTest::s_Context;
uint32 GPUSkinningPixelTest::s_Fbo = 0;
uint32 GPUSkinningPixelTest::s_FboColor = 0;
uint32 GPUSkinningPixelTest::s_FboDepth = 0;
bool   GPUSkinningPixelTest::s_FboReady = false;

// ── Stage A: bind pose establishes the pixel baseline ──────────────────────
TEST_F(GPUSkinningPixelTest, BindPoseDrawsGeometryInCentreBandOnly)
{
    Rig rig = RequireWorkingRig();
    GpuCapture cap = DrawWithBoneX(0.0f, rig);
    ASSERT_TRUE(cap.Valid())
        << "readback unusable: w=" << cap.w << " h=" << cap.h
        << " bytes=" << cap.px.size();

    Bands b = MakeBands(cap.w);
    const int centre = GpuLitInColumns(cap, b.centre, cap.w - 1 - b.centre);
    const int left   = GpuLitInColumns(cap, b.left, b.centre - 1);
    const int right  = GpuLitInColumns(cap, b.right, cap.w - 1);
    std::printf("  bind pose (%dx%d): centre=%d left=%d right=%d total=%d\n",
                cap.w, cap.h, centre, left, right, GpuCountLit(cap));

    EXPECT_GT(centre, 0) << "bind pose must draw geometry in the centre band";
    EXPECT_EQ(left, 0)   << "bind pose must not draw into the left band";
    EXPECT_EQ(right, 0)  << "bind pose must not draw into the right band";
}

// ── Stage B: translated bone produces the expected region change ───────────
TEST_F(GPUSkinningPixelTest, TranslatedBoneMovesGeometryIntoRightBand)
{
    Rig rig = RequireWorkingRig();
    GpuCapture bindCap  = DrawWithBoneX(0.0f, rig);
    GpuCapture movedCap = DrawWithBoneX(0.8f, rig);
    ASSERT_TRUE(bindCap.Valid())  << "bind readback unusable";
    ASSERT_TRUE(movedCap.Valid()) << "moved readback unusable";
    ASSERT_EQ(movedCap.w, bindCap.w);
    ASSERT_EQ(movedCap.h, bindCap.h);

    Bands b = MakeBands(bindCap.w);
    const int bindCentre  = GpuLitInColumns(bindCap, b.centre, bindCap.w - 1 - b.centre);
    const int movedCentre = GpuLitInColumns(movedCap, b.centre, movedCap.w - 1 - b.centre);
    const int movedLeft   = GpuLitInColumns(movedCap, b.left, b.centre - 1);
    const int movedRight  = GpuLitInColumns(movedCap, b.right, movedCap.w - 1);
    std::printf("  moved pose: centre=%d (bind %d) left=%d right=%d\n",
                movedCentre, bindCentre, movedLeft, movedRight);

    EXPECT_GT(bindCentre, 0)  << "precondition: bind pose covered the centre band";
    EXPECT_GT(movedRight, 0)  << "after the bone moves, geometry must APPEAR on the right";
    EXPECT_EQ(movedLeft, 0)   << "left band must stay empty; motion is +X only";
    EXPECT_LT(movedCentre, bindCentre)
        << "centre band must lose coverage as the quad leaves it";
    EXPECT_NE(bindCap.px, movedCap.px) << "the two poses must not be identical";
}

// ── Stage C: anti false positive ───────────────────────────────────────────
TEST_F(GPUSkinningPixelTest, SkinnedProgramIsInUseAndBackgroundIsStable)
{
    Rig rig = RequireWorkingRig();
    auto& gl = static_cast<OpenGLContext*>(s_Context.get())->GetGL();
    const GLuint prog = static_cast<GLuint>(rig.shader->GetNativeHandle());

    // If the skinned program were not bound, these could not resolve.
    EXPECT_GE(gl.GetUniformLocation(prog, "u_BoneMatrices"), 0)
        << "bone matrix array must be addressable -> skinned shader is the program in use";
    EXPECT_GE(gl.GetAttribLocation(prog, "a_BoneWeights"), 0)
        << "bone weight attribute must exist -> the custom vertex layout reached the GPU";

    GpuCapture bindCap  = DrawWithBoneX(0.0f, rig);
    GpuCapture movedCap = DrawWithBoneX(0.8f, rig);
    ASSERT_TRUE(bindCap.Valid() && movedCap.Valid());

    // The bottom-left corner is outside the quad in both poses, so it must be
    // byte identical. This catches "the whole frame changed for unrelated
    // reasons" being mistaken for skinning.
    bool stable = true;
    const int span = bindCap.w < 8 ? bindCap.w : 8;
    const int rows = bindCap.Rows() < movedCap.Rows() ? bindCap.Rows() : movedCap.Rows();
    for (int y = 0; y < span && y < rows && stable; ++y) {
        for (int x = 0; x < span; ++x) {
            const size_t o = static_cast<size_t>((y * bindCap.w + x) * 4);
            if (bindCap.px[o] != movedCap.px[o] ||
                bindCap.px[o + 1] != movedCap.px[o + 1] ||
                bindCap.px[o + 2] != movedCap.px[o + 2]) { stable = false; break; }
        }
    }
    EXPECT_TRUE(stable) << "background must be byte identical between the two draws";
}

// ---------------------------------------------------------------------------
// Runner OpenGL capability probe.
//
// Reports what the runner can actually do, so the GLAD loader version question
// is decided by measurement instead of by inference. Two facts are recorded
// separately and must not be conflated:
//
//   1. what a default (no version hint) context reports, and
//   2. whether an explicit 4.5 core context can be created at all.
//
// A default context reporting 4.5 does NOT prove a 4.5 core context can be
// created. The probe never fails the build on capability grounds: it is a
// measurement, and it must stay runnable on machines without a usable driver.
// ---------------------------------------------------------------------------
namespace glcap {

static const char* SafeGetString(GladGLContext& gl, GLenum name)
{
    if (!gl.GetString) return "<no glGetString>";
    const GLubyte* s = gl.GetString(name);
    return s ? reinterpret_cast<const char*>(s) : "<null>";
}

// Returns true only if a core context of the requested version was created.
static bool TryCreateCoreContext(int major, int minor, GLFWwindow** outWindow)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "GL capability probe", nullptr, nullptr);
    if (!w) return false;
    if (outWindow) *outWindow = w;
    return true;
}

} // namespace glcap

TEST(GLRunnerCapabilityProbe, ReportRunnerOpenGLCapability)
{
    if (!glfwInit()) {
        std::printf("[GLPROBE] glfwInit failed; cannot probe\n");
        GTEST_SKIP() << "glfwInit failed";
    }

    // --- Fact 1: what a default context reports -------------------------
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* defWindow = glfwCreateWindow(64, 64, "GL default probe", nullptr, nullptr);
    if (!defWindow) {
        std::printf("[GLPROBE] default context: CREATE FAILED\n");
    } else {
        glfwMakeContextCurrent(defWindow);
        // Load into a local table the same way the engine does; glad/gl.h here
        // is GLAD2, which exposes GladGLContext rather than global symbols.
        GladGLContext probeGl;
        std::memset(&probeGl, 0, sizeof(probeGl));
        if (gladLoadGLContext(&probeGl, glfwGetProcAddress) == 0) {
            std::printf("[GLPROBE] gladLoadGLContext returned 0 on the default context\n");
        }
        std::printf("[GLPROBE] === default context report ===\n");
        std::printf("[GLPROBE] GL_VERSION                  = %s\n", glcap::SafeGetString(probeGl, GL_VERSION));
        std::printf("[GLPROBE] GL_VENDOR                   = %s\n", glcap::SafeGetString(probeGl, GL_VENDOR));
        std::printf("[GLPROBE] GL_RENDERER                 = %s\n", glcap::SafeGetString(probeGl, GL_RENDERER));
        std::printf("[GLPROBE] GL_SHADING_LANGUAGE_VERSION = %s\n", glcap::SafeGetString(probeGl, GL_SHADING_LANGUAGE_VERSION));

        GLint profileMask = 0;
        // GetIntegerv returns void; a zeroed mask means the query did not
        // produce a profile bit, which covers both "not queryable" and
        // "reported no profile".
        if (probeGl.GetIntegerv) {
            probeGl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        }
        if (profileMask != 0) {
            const bool core   = (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
            const bool compat = (profileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
            std::printf("[GLPROBE] GL_CONTEXT_PROFILE_MASK     = 0x%x (%s%s)\n",
                        profileMask,
                        core ? "core" : "",
                        compat ? " compatibility" : "");
        } else {
            std::printf("[GLPROBE] GL_CONTEXT_PROFILE_MASK     = <not queryable>\n");
        }

        // --- Fact 2: can an explicit 4.5 core context be created? --------
        // The engine's GL46Device.cpp calls GL 4.5 DSA entry points
        // (CreateBuffers, NamedBufferStorage, MapNamedBufferRange,
        // NamedBufferSubData), so a 4.5 loader is the real lower bound and
        // 4.5 creation is the fact that decides the handoff.
        GLFWwindow* w45 = nullptr;
        if (glcap::TryCreateCoreContext(4, 5, &w45)) {
            glfwMakeContextCurrent(w45);
            std::printf("[GLPROBE] 4.5 core context: CREATED, reports GL_VERSION = %s\n",
                        glcap::SafeGetString(probeGl, GL_VERSION));
            glfwDestroyWindow(w45);
        } else {
            std::printf("[GLPROBE] 4.5 core context: CREATE FAILED\n");
        }
        GLFWwindow* w46 = nullptr;
        if (glcap::TryCreateCoreContext(4, 6, &w46)) {
            std::printf("[GLPROBE] 4.6 core context: CREATED\n");
            glfwDestroyWindow(w46);
        } else {
            std::printf("[GLPROBE] 4.6 core context: CREATE FAILED\n");
        }

        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(defWindow);
    }

    std::fflush(stdout);
    glfwTerminate();
}
// ===========================================================================
// 产品绘制分支的 headless 像素验证
//
// 这里**不复制**一份绘制流程，而是调用 EditorDemo 生产路径所用的同一个
// Engine::DrawSkinnedMeshIndexed。区别只在于 indexed draw 的具体 GL 调用
// 由本测试注入（生产环境由 OpenGLContext 提供），着色器绑定、uniform 上传
// 与顶点数组绑定全部走产品函数。
//
// 证据边界：这里手动指定骨骼矩阵，因此只证明**渲染接入**（产品绘制分支能
// 画出随骨骼姿势变化的正确像素），不证明 EditorDemo 的动画帧驱动已闭环。
// ===========================================================================
TEST_F(GPUSkinningPixelTest, ProductDrawBranchChangesPixelsWithPose)
{
    // 复用同一套 GPU fixture（真实 context + 离屏 FBO + 真实读回）
    Rig rig = BuildRig();
    ASSERT_NE(rig.shader, nullptr);
    ASSERT_NE(rig.vao, nullptr);
    ASSERT_NE(rig.shader->GetNativeHandle(), 0u)
        << "skinned_lit must compile and link for the product draw path";

    auto& gl = static_cast<OpenGLContext*>(s_Context.get())->GetGL();

    // 走生产构建资源的那条路：Mesh 持有 VAO 与索引数
    Engine::Mesh mesh(rig.vao, 6);

    // 两根骨骼：0 号承载几何，1 号保持单位阵
    const auto makeBones = [](float boneX, std::vector<Engine::Mat4>& out) {
        out.assign(2, Engine::Mat4());
        for (int i = 0; i < 16; ++i) {
            out[0].Data()[i] = 0.0f;
            out[1].Data()[i] = 0.0f;
        }
        out[0].Data()[0] = out[1].Data()[0] = 1.0f;
        out[0].Data()[5] = out[1].Data()[5] = 1.0f;
        out[0].Data()[10] = out[1].Data()[10] = 1.0f;
        out[0].Data()[15] = out[1].Data()[15] = 1.0f;
        out[0].Data()[12] = boneX;
    };

    const auto drawViaProductPath = [&](float boneX, GpuCapture& cap) -> bool {
        glfwMakeContextCurrent(s_Window);
        if (!EnsureFbo()) return false;
        cap.w = FboWidth();
        cap.h = FboHeight();
        gl.BindFramebuffer(GL_FRAMEBUFFER, s_Fbo);
        gl.Viewport(0, 0, cap.w, cap.h);
        gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl.ClearDepth(0.0);
        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.Enable(GL_DEPTH_TEST);

        std::vector<Engine::Mat4> bones;
        makeBones(boneX, bones);

        Engine::Mat4 vp;
        for (int i = 0; i < 16; ++i) vp.Data()[i] = 0.0f;
        vp.Data()[0] = vp.Data()[5] = vp.Data()[10] = vp.Data()[15] = 1.0f;

        // ↓↓↓ 产品绘制分支：EditorDemo 的 RenderSkinnedGameObject 调用的就是这个函数
        const bool issued = Engine::DrawSkinnedMeshIndexed(
            *rig.shader, *mesh.VAO, mesh.IndexCount, bones, vp,
            [&gl](uint32 indexCount) {
                gl.DrawElements(GL_TRIANGLES, static_cast<int>(indexCount),
                                GL_UNSIGNED_INT, nullptr);
            });
        // ↑↑↑

        gl.Finish();
        cap.px.assign(static_cast<size_t>(cap.w) * cap.h * 4, 0);
        gl.ReadPixels(0, 0, cap.w, cap.h, GL_RGBA, GL_UNSIGNED_BYTE, cap.px.data());
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return issued;
    };

    GpuCapture bindPose;
    ASSERT_TRUE(drawViaProductPath(0.0f, bindPose)) << "product draw must issue a draw";

    GpuCapture movedPose;
    ASSERT_TRUE(drawViaProductPath(0.5f, movedPose)) << "product draw must issue a draw";

    const auto centreLit = [](const GpuCapture& c) {
        int n = 0;
        for (int y = 16; y < 48; ++y) {
            for (int x = 16; x < 48; ++x) {
                const size_t o = (static_cast<size_t>(y) * c.w + x) * 4;
                if (c.px[o] + c.px[o + 1] + c.px[o + 2] > 30) ++n;
            }
        }
        return n;
    };

    EXPECT_GT(centreLit(bindPose), 0) << "bind pose must cover the centre band";
    EXPECT_GT(centreLit(movedPose), 0) << "moved pose must still cover the centre band";

    // 姿态变化必须真实反映到像素上：整体像素必须不同
    bool differs = false;
    for (size_t i = 0; i < bindPose.px.size() && !differs; ++i) {
        if (bindPose.px[i] != movedPose.px[i]) { differs = true; }
    }
    EXPECT_TRUE(differs)
        << "product draw branch must produce different pixels for different poses";
}