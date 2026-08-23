/**
 * @file RenderGraphSG6Test.cpp
 * @brief SG6 — RenderGraph 一次性质检（架构验证，非功能开发）
 *
 * 验收问题（docs/GPU-Physics-v3.0 · SG6）：
 *   Q1 API 是否降低复杂度（对比直连实现）
 *   Q2 依赖推导是否正确（含乱序注册行为）
 *   Q3 资源生命周期/瞬态分配是否真实可用
 *   Q4 ExecuteParallel 在当前 workload 是否有意义
 *   Q5 性能开销是否可接受
 *
 * 真实渲染路径：GL46 设备 + 真 SSBO + 计算内核三段链
 *   P1 Generate (写 Data)  →  P2 Transform (读 Data 写 DataX)  →  P3 Consume (读 DataX)
 * 判定数据：GPU 回读结果 + 编译产物（拓扑序/生命周期/屏障）+ 计时。
 */

#include <gtest/gtest.h>
#include "Engine/Rendering/RenderGraph.h"
#include "Engine/Rendering/TransientHeap.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <memory>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace Engine;
using namespace Engine::RHI;
using namespace Engine::Rendering;

namespace {
    struct GLContext {
        GLFWwindow* window = nullptr;
        std::unique_ptr<GladGLContext> glad;
        bool Init() {
            if (!glfwInit()) return false;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            window = glfwCreateWindow(8, 8, "SG6", nullptr, nullptr);
            if (!window) { glfwTerminate(); return false; }
            glfwMakeContextCurrent(window);
            glad = std::make_unique<GladGLContext>();
            if (!gladLoadGLContext(glad.get(), glfwGetProcAddress)) return false;
            return true;
        }
        void Shutdown() {
            if (window) { glfwDestroyWindow(window); window = nullptr; }
            glfwTerminate();
        }
    };

    GLContext g_ctx;

    // 测试本地计算内核（与 Ring2-D 诊断同款编译路径 —— 引擎 PSO 表仅含生产着色器）
    GLuint CompileKernel(GladGLContext& gl, const char* src) {
        GLuint cs = gl.CreateShader(GL_COMPUTE_SHADER);
        gl.ShaderSource(cs, 1, &src, nullptr);
        gl.CompileShader(cs);
        GLint ok = 0; gl.GetShaderiv(cs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; gl.GetShaderInfoLog(cs, 1024, nullptr, log);
            std::printf("    [SG6] kernel compile error: %s\n", log);
            gl.DeleteShader(cs); return 0; }
        GLuint prog = gl.CreateProgram();
        gl.AttachShader(prog, cs); gl.LinkProgram(prog);
        GLint lok = 0; gl.GetProgramiv(prog, GL_LINK_STATUS, &lok);
        gl.DeleteShader(cs);
        return lok ? prog : 0;
    }

    constexpr int kElems = 64;
}

class RenderGraphSG6 : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!g_ctx.Init()) { std::printf("  [WARN] no GL, SG6 skips\n"); g_ctx.window = nullptr; }
    }
    static void TearDownTestSuite() { g_ctx.Shutdown(); }

    void SetUp() override {
        if (!g_ctx.window) GTEST_SKIP() << "No GL 4.6";
        m_Device = std::make_unique<GL46Device>();
        ASSERT_TRUE(m_Device->InitializeWithGLContext(g_ctx.glad.get(), 8, 8));
    }
    void TearDown() override { m_Device.reset(); }

    std::unique_ptr<GL46Device> m_Device;
};

// ── 核心质检：乱序注册 + 真实 GPU 数据链 ──────────────────────
//
// 故意按【消费方在前】的顺序注册，检验：
//   a) 依赖推导对乱序声明的行为
//   b) Execute 的实际执行次序依据（注册序 vs 拓扑序）
TEST_F(RenderGraphSG6, SG6_Core_OutOfOrderDeclarationAndDataChain) {
    auto& gl = m_Device->GetGL();

    // 外部资源：三个真实 SSBO
    auto makeSSBO = [&](size_t bytes) {
        RHI::RHIBufferDesc d; d.size = bytes; d.memoryUsage = RHI::MemoryUsage::GPU_Only;
        auto buf = m_Device->CreateBuffer(d);
        auto* g = static_cast<GL46Buffer*>(buf.get());
        return buf;   // shared_ptr 保活
    };
    auto dataBuf  = makeSSBO(kElems * sizeof(float));
    auto xformBuf = makeSSBO(kElems * sizeof(float));
    auto outBuf   = makeSSBO(kElems * sizeof(float));
    ASSERT_NE(static_cast<GL46Buffer*>(dataBuf.get())->GetGLHandle(), 0u);

    // 内核
    GLuint kGen = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) buffer O { float v[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; v[i]=float(i)+1.0; }\n");
    GLuint kXfm = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) readonly buffer I { float iv[]; };\n"
        "layout(std430,binding=1) buffer O { float ov[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; ov[i]=iv[i]*10.0; }\n");
    GLuint kCon = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) readonly buffer I { float iv[]; };\n"
        "layout(std430,binding=1) buffer O { float ov[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; ov[i]+=iv[i]; }\n");
    ASSERT_NE(kGen, 0u); ASSERT_NE(kXfm, 0u); ASSERT_NE(kCon, 0u);

    // 清零 outBuf
    std::vector<float> zeros(kElems, 0.f);
    gl.NamedBufferSubData(static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle(),
                          0, kElems * sizeof(float), zeros.data());

    // ── 构图：故意【消费方在前】──
    Rendering::RenderGraph graph;
    TransientHeap heap;
    heap.Initialize(*m_Device);

    StringID sData  = StringID::Runtime("sg6.Data");
    StringID sDataX = StringID::Runtime("sg6.DataX");

    // P3 Consume（先注册！读 DataX，累加进 outBuf）
    graph.AddPass("P3_Consume",
        [&](RenderPassBuilder& b) {
            b.CreateBuffer(sDataX, kElems * sizeof(float), false);
            b.ReadTexture(sDataX);                       // 名称级声明（buffer 复用通道）
        },
        [&](IRHICommandList& cmd) {
            gl.UseProgram(kCon);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(xformBuf.get())->GetGLHandle());
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });

    // P2 Transform（中注册；读 Data 写 DataX）
    graph.AddPass("P2_Transform",
        [&](RenderPassBuilder& b) {
            b.CreateBuffer(sData, kElems * sizeof(float), false);
            b.ReadTexture(sData);
            b.WriteTexture(sDataX);
        },
        [&](IRHICommandList& cmd) {
            gl.UseProgram(kXfm);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(dataBuf.get())->GetGLHandle());
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                static_cast<GL46Buffer*>(xformBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });

    // P1 Generate（最后注册；只写 Data）
    graph.AddPass("P1_Generate",
        [&](RenderPassBuilder& b) {
            b.WriteTexture(sData);
        },
        [&](IRHICommandList& cmd) {
            gl.UseProgram(kGen);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(dataBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });

    // ── Q3: 编译（依赖推导 + 拓扑排序 + 瞬态堆登记）──
    ASSERT_TRUE(graph.Compile(heap));

    // ── 执行 ──
    auto* queue = m_Device->GetQueue(QueueType::Graphics);
    ASSERT_NE(queue, nullptr);
    graph.Execute(*m_Device, *queue);
    m_Device->WaitIdle();

    // ── 回读判定 ──
    std::vector<float> result(kElems, 0.f);
    gl.GetNamedBufferSubData(static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle(),
                             0, kElems * sizeof(float), result.data());
    bool chainOK = true;
    for (int i = 0; i < kElems; ++i)
        if (result[i] != (i + 1) * 10.0f) { chainOK = false;
            std::printf("    [SG6] mismatch @%d: got %.1f want %.1f\n",
                        i, result[i], (i + 1) * 10.0f); break; }
    std::printf("    [Q2] out-of-order declaration → data chain %s\n",
                chainOK ? "CORRECT" : "BROKEN");

    // ── DumpGraph ──
    const std::string dot = graph.DumpGraph();
    std::printf("    [DBG] DOT length=%zu contains P1=%d P2=%d P3=%d\n",
                dot.size(),
                (int)dot.find("P1_Generate"), (int)dot.find("P2_Transform"),
                (int)dot.find("P3_Consume"));
    EXPECT_FALSE(dot.empty());
}


// ── 对照组：顺序注册（生产者在前）→ 应为正确数据链 ──────────
TEST_F(RenderGraphSG6, SG6_InOrderDeclaration_CorrectChain) {
    auto& gl = m_Device->GetGL();
    auto makeSSBO = [&](size_t bytes) {
        RHI::RHIBufferDesc d; d.size = bytes; d.memoryUsage = RHI::MemoryUsage::GPU_Only;
        return m_Device->CreateBuffer(d);
    };
    auto dataBuf = makeSSBO(kElems * sizeof(float));
    auto xformBuf = makeSSBO(kElems * sizeof(float));
    auto outBuf = makeSSBO(kElems * sizeof(float));

    GLuint kGen = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) buffer O { float v[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; v[i]=float(i)+1.0; }\n");
    GLuint kXfm = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) readonly buffer I { float iv[]; };\n"
        "layout(std430,binding=1) buffer O { float ov[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; ov[i]=iv[i]*10.0; }\n");
    GLuint kCon = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) readonly buffer I { float iv[]; };\n"
        "layout(std430,binding=1) buffer O { float ov[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; ov[i]+=iv[i]; }\n");
    ASSERT_NE(kGen, 0u); ASSERT_NE(kXfm, 0u); ASSERT_NE(kCon, 0u);

    std::vector<float> zeros(kElems, 0.f);
    gl.NamedBufferSubData(static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle(),
                          0, kElems * sizeof(float), zeros.data());

    Rendering::RenderGraph graph;
    TransientHeap heap;
    heap.Initialize(*m_Device);

    StringID sData  = StringID::Runtime("sg6i.Data");
    StringID sDataX = StringID::Runtime("sg6i.DataX");

    graph.AddPass("P1_Generate",
        [&](RenderPassBuilder& b) { b.CreateBuffer(sData, kElems * sizeof(float), false); b.WriteTexture(sData); },
        [&](IRHICommandList&) {
            gl.UseProgram(kGen);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(dataBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });
    graph.AddPass("P2_Transform",
        [&](RenderPassBuilder& b) { b.ReadTexture(sData); b.WriteTexture(sDataX); },
        [&](IRHICommandList&) {
            gl.UseProgram(kXfm);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(dataBuf.get())->GetGLHandle());
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                static_cast<GL46Buffer*>(xformBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });
    graph.AddPass("P3_Consume",
        [&](RenderPassBuilder& b) { b.ReadTexture(sDataX); },
        [&](IRHICommandList&) {
            gl.UseProgram(kCon);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GL46Buffer*>(xformBuf.get())->GetGLHandle());
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle());
            gl.DispatchCompute(1, 1, 1);
        });

    ASSERT_TRUE(graph.Compile(heap));

    // Q2 附加证据：拓扑层号（应为 P1:L0 / P2:L1 / P3:L2 由 DumpGraph 或内部推导体现）
    const std::string dot = graph.DumpGraph();
    std::printf("    [DOT-INORDER]\n%s\n", dot.c_str());

    auto* queue = m_Device->GetQueue(QueueType::Graphics);
    graph.Execute(*m_Device, *queue);
    m_Device->WaitIdle();

    std::vector<float> result(kElems, 0.f);
    gl.GetNamedBufferSubData(static_cast<GL46Buffer*>(outBuf.get())->GetGLHandle(),
                             0, kElems * sizeof(float), result.data());
    bool chainOK = true;
    for (int i = 0; i < kElems; ++i)
        if (result[i] != (i + 1) * 10.0f) { chainOK = false; break; }
    std::printf("    [Q2b] in-order declaration → data chain %s\n",
                chainOK ? "CORRECT" : "BROKEN");
    EXPECT_TRUE(chainOK);
}

// ── Q5: 编译+执行开销 vs 直连基线 ───────────────────────────
TEST_F(RenderGraphSG6, SG5_PerfOverhead_Measured) {
    auto& gl = m_Device->GetGL();
    RHI::RHIBufferDesc d; d.size = kElems * sizeof(float); d.memoryUsage = RHI::MemoryUsage::GPU_Only;
    auto buf = m_Device->CreateBuffer(d);

    GLuint kGen = CompileKernel(gl,
        "#version 460 core\n layout(local_size_x=64) in;\n"
        "layout(std430,binding=0) buffer O { float v[]; };\n"
        "void main(){ uint i=gl_GlobalInvocationID.x; v[i]=float(i)+1.0; }\n");
    ASSERT_NE(kGen, 0u);

    const int kIters = 500;

    // 基线：直连立即命令
    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < kIters; ++it) {
        gl.UseProgram(kGen);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GL46Buffer*>(buf.get())->GetGLHandle());
        gl.DispatchCompute(1, 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    }
    gl.Finish();
    double baseMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    // RenderGraph 路径：每帧 AddPass×N + Compile + Execute
    TransientHeap heap;
    heap.Initialize(*m_Device);
    t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < kIters; ++it) {
        Rendering::RenderGraph graph;
        StringID sD = StringID::Runtime("perf.Data");
        graph.AddPass("Gen",
            [&](RenderPassBuilder& b) { b.CreateBuffer(sD, kElems * sizeof(float), false); b.WriteTexture(sD); },
            [&](IRHICommandList&) {
                gl.UseProgram(kGen);
                gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GL46Buffer*>(buf.get())->GetGLHandle());
                gl.DispatchCompute(1, 1, 1);
            });
        if (graph.Compile(heap)) graph.Execute(*m_Device, *m_Device->GetQueue(QueueType::Graphics));
    }
    gl.Finish();
    double rgMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    std::printf("    [Q5] direct=%.3f ms total | rendergraph=%.3f ms total | overhead=%.2fx (%.3f ms/iter)\n",
                baseMs, rgMs, rgMs / baseMs, (rgMs - baseMs) / kIters);
}