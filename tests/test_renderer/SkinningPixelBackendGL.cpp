/**
 * @file SkinningPixelBackendGL.cpp
 * @brief GPU 蒙皮像素验证的 OpenGL 后端
 *
 * 这一份是从 tests/test_renderer/GL46DeviceTest.cpp 原样搬过来的：同样的
 * GLFW 4.6 隐藏窗口、同样的 IGraphicsFactory / IRenderContext 生产链、同样的
 * 私有 FBO 与 glReadPixels。搬动而不是重写，是为了让「搬迁本身不引入行为变化」
 * 可被像素断言检验。
 *
 * 三个前置条件必须显式建立，否则整帧会静默变成全 0：
 *   1. 上下文必须是 current 的。工厂构造器为了加载 glad 会自建并销毁一个临时
 *      上下文，销毁后没有上下文 current，此后每个 GL 调用都是 no-op
 *      （表现为着色器 info log 是空的 0 字节）。
 *   2. OpenGLContext 的尺寸来自 OnResize；没调用它，视口与回读都是 0x0。
 *   3. 渲染目标必须是私有 FBO 而不是窗口的 framebuffer 0：后者回读出未初始化
 *      内存，表现为「每一列都有亮像素」，与假阳性无法区分。
 */
#include "SkinningPixelRig.h"

// 顺序是硬性的，不是风格问题：glfw3.h 默认会 include 系统 <GL/gl.h>（用
// GLFW_INCLUDE_NONE 才不引），而 glad/gl.h 见到 __gl_h_ 已定义就直接 #error。
// 因此 glad 必须排在 GLFW 之前 —— GL46DeviceTest.cpp 之前是靠 GL46AZDODevice.h
// 间接把 glad 先拉进来的，从没显式写过这个顺序，所以一搬出来就踩到。
// glad/gl.h（不是 glad/glad.h）是本工程 vendor 的头，提供 GladGLContext。
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Engine/Core/GameObject/MeshRendererComponent.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Animation/SkinnedMeshDraw.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Engine {
namespace Test {
namespace {

constexpr int kTargetWidth  = 64;
constexpr int kTargetHeight = 64;

GladGLContext& GlOf(IRenderContext& ctx)
{
    return static_cast<OpenGLContext&>(ctx).GetGL();
}

// 定位仓库资源根，使着色器无论进程从仓库根还是从 CMake 测试二进制目录启动都能
// 解析（ctest 的工作目录是构建输出目录）。
std::string ResolveShaderPath(const char* relative)
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

// 着色器编译失败时打印 GLSL info log。生产层只记录一个 Failed 状态，因此没有
// 这段，「文件找不到」与「GLSL 写错了」与断言失败三者无法区分。
void DumpShaderLogs(GladGLContext& gl, const char* vertPath, const char* fragPath)
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

class GLSkinningPixelBackend final : public ISkinningPixelBackend {
public:
    ~GLSkinningPixelBackend() override
    {
        context_.reset();
        factory_.reset();
        if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
        glfwTerminate();
    }

    static std::unique_ptr<GLSkinningPixelBackend> Create(std::string* why);

    const char* Name() const override { return "OpenGL 4.6"; }
    bool ProductPathAvailable() const override { return true; }

    SkinCapture DrawRig(float boneX) override;
    SkinCapture DrawProductPath(float boneX) override;
    bool VerifySkinnedProgram(std::string* detail) override;

private:
    struct Rig {
        std::shared_ptr<Shader>      shader;
        std::shared_ptr<VertexArray> vao;
    };

    bool BuildRig(Rig& rig, std::string* err);
    bool EnsureFbo();
    // 绑定目标、设视口、按 reverse-Z 约定清屏。GL 侧用 reverse-Z（清 0）。
    void BeginPass(SkinCapture& cap);
    SkinCapture FinishPass(const SkinCapture& cap);

    GLFWwindow*                       window_ = nullptr;
    std::unique_ptr<OpenGLGraphicsFactory> factory_;
    std::unique_ptr<IRenderContext>   context_;

    uint32 fbo_       = 0;
    uint32 fboColor_  = 0;
    uint32 fboDepth_  = 0;
    bool   fboReady_  = false;
};

std::unique_ptr<GLSkinningPixelBackend> GLSkinningPixelBackend::Create(std::string* why)
{
    auto backend = std::unique_ptr<GLSkinningPixelBackend>(new GLSkinningPixelBackend());

    if (!glfwInit()) {
        if (why) *why = "glfwInit failed";
        return nullptr;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    backend->window_ = glfwCreateWindow(64, 64, "GPUSkinPixel", nullptr, nullptr);
    if (!backend->window_) {
        if (why) *why = "GL 4.6 core context creation failed (no version hint fallback is "
                        "attempted: the engine requires 4.6 and a lower version would leave the "
                        "DSA entry points the buffer path calls unresolved)";
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(backend->window_);

    backend->factory_ = std::make_unique<OpenGLGraphicsFactory>();
    // CreateRenderContext 内部会把 void* 静态转回 GLFWwindow*，它不是 HWND。
    // 真正加载 glad 的是工厂构造器。
    backend->context_ = backend->factory_->CreateRenderContext(static_cast<void*>(backend->window_));
    if (!backend->context_) {
        if (why) *why = "OpenGLGraphicsFactory::CreateRenderContext returned null";
        glfwTerminate();
        return nullptr;
    }
    // (1) 工厂构造器销毁了它自己的加载上下文，因此重新让我们的上下文 current。
    // (2) OpenGLContext 从 OnResize 取尺寸；在那之前视口与回读都是 0x0。
    glfwMakeContextCurrent(backend->window_);
    backend->context_->Init();
    backend->context_->OnResize(kTargetWidth, kTargetHeight);
    return backend;
}

bool GLSkinningPixelBackend::BuildRig(Rig& rig, std::string* err)
{
    const std::string vertPath = ResolveShaderPath("assets/shaders/skinned_lit.vert");
    const std::string fragPath = ResolveShaderPath("assets/shaders/skinned_lit.frag");

    const std::vector<SkinVertex> verts = SkinBuildQuadVertices();
    uint32_t indexCount = 0;
    const uint32_t* idx = SkinQuadIndices(&indexCount);

    rig.shader = factory_->CreateShader(vertPath, fragPath);
    if (!rig.shader) {
        if (err) *err = "CreateShader returned null (" + vertPath + ")";
        return false;
    }
    if (rig.shader->GetNativeHandle() == 0u) {
        DumpShaderLogs(GlOf(*context_), vertPath.c_str(), fragPath.c_str());
        if (err) *err = "skinned_lit program handle is 0 -> compile or link failed";
        return false;
    }

    auto vb = factory_->CreateVertexBuffer(
        reinterpret_cast<float*>(const_cast<SkinVertex*>(verts.data())),
        static_cast<uint32>(verts.size() * sizeof(SkinVertex)));
    auto ib = factory_->CreateIndexBuffer(const_cast<uint32*>(idx), indexCount);
    rig.vao = factory_->CreateVertexArray();
    if (!vb || !ib || !rig.vao) {
        if (err) *err = "vertex/index buffer or vertex array creation failed";
        return false;
    }

    const VertexAttribute attrs[6] = {
        { 0, 3, sizeof(SkinVertex), offsetof(SkinVertex, position)    },
        { 1, 3, sizeof(SkinVertex), offsetof(SkinVertex, normal)      },
        { 2, 2, sizeof(SkinVertex), offsetof(SkinVertex, texCoord)    },
        { 3, 3, sizeof(SkinVertex), offsetof(SkinVertex, tangent)     },
        { 4, 4, sizeof(SkinVertex), offsetof(SkinVertex, boneIndices) },
        { 5, 4, sizeof(SkinVertex), offsetof(SkinVertex, boneWeights) },
    };
    rig.vao->AddVertexBuffer(vb, attrs, 6);
    rig.vao->SetIndexBuffer(ib);
    return true;
}

bool GLSkinningPixelBackend::EnsureFbo()
{
    GladGLContext& gl = GlOf(*context_);
    if (fboReady_) return true;
    gl.GenFramebuffers(1, &fbo_);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl.GenTextures(1, &fboColor_);
    gl.BindTexture(GL_TEXTURE_2D, fboColor_);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTargetWidth, kTargetHeight, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            fboColor_, 0);
    gl.GenRenderbuffers(1, &fboDepth_);
    gl.BindRenderbuffer(GL_RENDERBUFFER, fboDepth_);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kTargetWidth, kTargetHeight);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth_);
    const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    fboReady_ = ok;
    return ok;
}

void GLSkinningPixelBackend::BeginPass(SkinCapture& cap)
{
    GladGLContext& gl = GlOf(*context_);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl.Viewport(0, 0, kTargetWidth, kTargetHeight);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.ClearDepth(0.0);              // 引擎使用 reverse-Z
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_DEPTH_TEST);
    cap.w = kTargetWidth;
    cap.h = kTargetHeight;
}

SkinCapture GLSkinningPixelBackend::FinishPass(const SkinCapture& cap)
{
    GladGLContext& gl = GlOf(*context_);
    gl.Finish();
    SkinCapture out;
    out.w = cap.w;
    out.h = cap.h;
    out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    gl.ReadPixels(0, 0, out.w, out.h, GL_RGBA, GL_UNSIGNED_BYTE, out.px.data());
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return out;
}

SkinCapture GLSkinningPixelBackend::DrawRig(float boneX)
{
    SkinCapture cap;
    glfwMakeContextCurrent(window_);
    Rig rig;
    std::string err;
    if (!BuildRig(rig, &err)) {
        std::printf("  [OpenGL 4.6] rig build failed: %s\n", err.c_str());
        return cap;
    }
    if (!EnsureFbo()) {
        std::printf("  [OpenGL 4.6] offscreen FBO incomplete\n");
        return cap;
    }
    BeginPass(cap);

    GladGLContext& gl = GlOf(*context_);

    float vp[16];
    SkinMakeTranslate(0.0f, 0.0f, 0.0f, vp);

    // 两根骨骼：0 号承载几何，1 号保持单位阵。
    float bones[32];
    SkinMakeTranslate(boneX, 0.0f, 0.0f, &bones[0]);
    SkinMakeTranslate(0.0f, 0.0f, 0.0f, &bones[16]);

    rig.shader->Bind();
    rig.shader->SetMat4("u_ViewProjection", vp);
    rig.shader->SetMat4Array("u_BoneMatrices", bones, 2);   // 被测调用
    context_->DrawIndexed(rig.vao);

    return FinishPass(cap);
}

SkinCapture GLSkinningPixelBackend::DrawProductPath(float boneX)
{
    SkinCapture cap;
    glfwMakeContextCurrent(window_);
    Rig rig;
    std::string err;
    if (!BuildRig(rig, &err)) {
        std::printf("  [OpenGL 4.6] rig build failed: %s\n", err.c_str());
        return cap;
    }
    if (!EnsureFbo()) {
        std::printf("  [OpenGL 4.6] offscreen FBO incomplete\n");
        return cap;
    }
    BeginPass(cap);

    GladGLContext& gl = GlOf(*context_);

    // 走生产构建资源的那条路：Mesh 持有 VAO 与索引数。
    Mesh mesh(rig.vao, 6);

    // 两根骨骼：0 号承载几何，1 号保持单位阵。
    const auto makeBones = [](float bone1X, std::vector<Mat4>& out) {
        out.assign(2, Mat4());
        for (int i = 0; i < 16; ++i) {
            out[0].Data()[i] = 0.0f;
            out[1].Data()[i] = 0.0f;
        }
        out[0].Data()[0] = out[1].Data()[0] = 1.0f;
        out[0].Data()[5] = out[1].Data()[5] = 1.0f;
        out[0].Data()[10] = out[1].Data()[10] = 1.0f;
        out[0].Data()[15] = out[1].Data()[15] = 1.0f;
        out[0].Data()[12] = bone1X;
    };

    std::vector<Mat4> bones;
    makeBones(boneX, bones);

    Mat4 vp;
    for (int i = 0; i < 16; ++i) vp.Data()[i] = 0.0f;
    vp.Data()[0] = vp.Data()[5] = vp.Data()[10] = vp.Data()[15] = 1.0f;

    // ↓↓↓ 产品绘制分支：EditorDemo 的 RenderSkinnedGameObject 调用的就是这个函数
    const bool issued = DrawSkinnedMeshIndexed(
        *rig.shader, *mesh.VAO, mesh.IndexCount, bones, vp,
        [&gl](uint32 indexCount) {
            gl.DrawElements(GL_TRIANGLES, static_cast<int>(indexCount),
                            GL_UNSIGNED_INT, nullptr);
        });
    // ↑↑↑

    if (!issued) {
        std::printf("  [OpenGL 4.6] product draw did not issue a draw\n");
        return cap;
    }
    return FinishPass(cap);
}

bool GLSkinningPixelBackend::VerifySkinnedProgram(std::string* detail)
{
    Rig rig;
    std::string err;
    if (!BuildRig(rig, &err)) {
        if (detail) *detail = err;
        return false;
    }
    GladGLContext& gl = GlOf(*context_);
    const GLuint prog = static_cast<GLuint>(rig.shader->GetNativeHandle());

    // 若用的不是蒙皮 program，下面两项都不可能解析成功。
    if (gl.GetUniformLocation(prog, "u_BoneMatrices") < 0) {
        if (detail) *detail = "bone matrix array is not addressable in the program in use";
        return false;
    }
    if (gl.GetAttribLocation(prog, "a_BoneWeights") < 0) {
        if (detail) *detail = "bone weight attribute missing -> the custom vertex layout "
                              "did not reach the GPU";
        return false;
    }
    if (detail) *detail = "u_BoneMatrices and a_BoneWeights both resolve in the program in use";
    return true;
}

} // namespace

std::unique_ptr<ISkinningPixelBackend> TryCreateGLSkinningBackend(std::string* why)
{
    return GLSkinningPixelBackend::Create(why);
}

} // namespace Test
} // namespace Engine