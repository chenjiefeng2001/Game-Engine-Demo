/**
 * @file GL46Device.cpp
 * @brief OpenGL 4.6 AZDO 设备实现 — 真实 GPU 计算后端
 *
 * 功能：
 *   - 支持两种初始化模式：
 *     1. Initialize() — 传统方式（需外部创建窗口/GL 上下文）
 *     2. InitializeWithGLContext() — 复用已有 GladGLContext
 *   - DSA 缓冲创建（glCreateBuffers + glNamedBufferStorage + 持久映射）
 *   - 计算着色器编译和缓存
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/RHI/MemoryTypes.h"
#include "Engine/Core/Log.h"
#include "GL46ComputeShaders.inl"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace Engine {
namespace RHI {

static Logger s_Log("GL46Device");

struct GL46Device::Impl { bool initialized{false}; };

static constexpr uint64_t HashString64(const char* str) noexcept {
    uint64_t h = 0xCBF29CE484222325ull;
    while (*str) { h ^= (uint8_t)*str++; h *= 0x100000001B3ull; }
    return h;
}

static uint32 CompileGLShader(GladGLContext& gl, uint32 type, const char* source) {
    uint32 id = gl.CreateShader(type);
    if (id == 0) return 0;
    gl.ShaderSource(id, 1, &source, nullptr);
    gl.CompileShader(id);
    int success = 0;
    gl.GetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        gl.GetShaderInfoLog(id, 1024, nullptr, log);
        s_Log.Error("Shader compile error: {}", log);
        gl.DeleteShader(id);
        return 0;
    }
    return id;
}

static uint32 LinkComputeProgram(GladGLContext& gl, uint32 cs) {
    uint32 prog = gl.CreateProgram();
    gl.AttachShader(prog, cs);
    gl.LinkProgram(prog);
    int success = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        gl.GetProgramInfoLog(prog, 1024, nullptr, log);
        s_Log.Error("Compute program link error: {}", log);
        gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

GL46Device::GL46Device() : m_Impl(std::make_unique<Impl>()) {}
GL46Device::~GL46Device() { Shutdown(); }

void GL46Device::Shutdown() {
    if (m_GL) {
        for (auto& [hash, prog] : m_ComputePrograms) {
            if (prog.program) { m_GL->DeleteProgram(prog.program); prog.program = 0; }
        }
    }
    m_ComputePrograms.clear();
    m_IsStub = true;
    if (m_Impl) m_Impl->initialized = false;
}

bool GL46Device::Initialize(void* windowHandle, uint32_t width, uint32_t height) {
    (void)windowHandle; (void)width; (void)height;
    if (m_Impl->initialized) return true;
    if (!m_GL) {
        std::fprintf(stdout, "[GL46] Device stub initialized (no GL context)\n");
        m_Impl->initialized = true; m_IsStub = true;
        m_DeviceName = "OpenGL 4.6 AZDO (stub)";
        return true;
    }
    return true;
}

bool GL46Device::InitializeWithGLContext(GladGLContext* glContext, uint32_t width, uint32_t height) {
    if (!glContext) return false;
    m_GL = glContext; m_OwnsContext = false;

    const char* vendor   = (const char*)m_GL->GetString(GL_VENDOR);
    const char* renderer = (const char*)m_GL->GetString(GL_RENDERER);
    const char* version  = (const char*)m_GL->GetString(GL_VERSION);
    std::stringstream ss;
    ss << (renderer ? renderer : "Unknown") << " (" << (vendor ? vendor : "Unknown")
       << ") — OpenGL " << (version ? version : "?");
    m_DeviceName = ss.str();
    std::fprintf(stdout, "[GL46] Device initialized: %s\n", m_DeviceName.c_str());

    if (!CompileComputeShaders()) {
        s_Log.Error("Failed to compile compute shaders — stub fallback");
        m_IsStub = true; m_Impl->initialized = true; return true;
    }
    m_IsStub = false; m_Impl->initialized = true; return true;
}

bool GL46Device::CompileComputeShaders() {
    { // gpu_physics_integrate
        GL46ComputeProgram prog;
        const char* name = "gpu_physics_integrate";
        uint32 cs = CompileGLShader(*m_GL, GL_COMPUTE_SHADER, s_IntegrateCS);
        if (!cs) return false;
        prog.program = LinkComputeProgram(*m_GL, cs); m_GL->DeleteShader(cs);
        if (!prog.program) return false;
        prog.nameHash = HashString64(name);
        m_ComputePrograms[prog.nameHash] = std::move(prog);
        s_Log.Info("Compute program '{}' compiled", name);
    }
    { // gpu_physics_collide
        GL46ComputeProgram prog;
        const char* name = "gpu_physics_collide";
        uint32 cs = CompileGLShader(*m_GL, GL_COMPUTE_SHADER, s_CollideCS);
        if (!cs) return false;
        prog.program = LinkComputeProgram(*m_GL, cs); m_GL->DeleteShader(cs);
        if (!prog.program) return false;
        prog.nameHash = HashString64(name);
        m_ComputePrograms[prog.nameHash] = std::move(prog);
        s_Log.Info("Compute program '{}' compiled", name);
    }
    return true;
}

GL46ComputeProgram* GL46Device::GetComputeProgram(uint64_t nameHash) const {
    auto it = m_ComputePrograms.find(nameHash);
    return (it != m_ComputePrograms.end()) ? const_cast<GL46ComputeProgram*>(&it->second) : nullptr;
}

// ── Buffer 创建 ──
std::shared_ptr<IRHIBuffer> GL46Device::CreateBuffer(const RHIBufferDesc& desc) {
    auto buffer = std::make_shared<GL46Buffer>();
    GPUAllocation alloc; alloc.size = desc.size;
    buffer->m_Allocation = alloc; buffer->m_Size = desc.size;

    if (m_IsStub || !m_GL) {
        buffer->m_MappedPtr = std::malloc(desc.size > 0 ? desc.size : 1);
        if (buffer->m_MappedPtr) std::memset(buffer->m_MappedPtr, 0, desc.size);
        buffer->m_Persistent = false;
    } else {
        GladGLContext& gl = *m_GL; uint32_t glBuf = 0;
        gl.CreateBuffers(1, &glBuf);

        GLbitfield storageFlags = GL_DYNAMIC_STORAGE_BIT;
        GLbitfield mapFlags = 0;

        // MemoryUsage: GPU_Only, CPU_To_GPU, GPU_To_CPU, CPU_Only
        // 必须使用 GL_MAP_COHERENT_BIT 确保 CPU 写入 → GPU 可见，GPU 写入 → CPU 可见
        MemoryUsage u = desc.memoryUsage;
        if (u == MemoryUsage::CPU_To_GPU || u == MemoryUsage::CPU_Only) {
            storageFlags |= GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            buffer->m_Persistent = true;
        } else if (u == MemoryUsage::GPU_To_CPU) {
            storageFlags |= GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            mapFlags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            buffer->m_Persistent = true;
        } else {
            storageFlags |= GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            mapFlags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            buffer->m_Persistent = true;
        }

        uint64_t bufSize = desc.size > 0 ? desc.size : 1;
        const void* initData = desc.initialData;
        gl.NamedBufferStorage(glBuf, (GLsizeiptr)bufSize, initData, storageFlags);

        if (buffer->m_Persistent) {
            void* mapped = gl.MapNamedBufferRange(glBuf, 0, (GLsizeiptr)bufSize, mapFlags);
            if (mapped) {
                buffer->m_MappedPtr = mapped;
                if (!initData) std::memset(mapped, 0, (size_t)bufSize);
            } else { buffer->m_Persistent = false; }
        }
        buffer->m_GLBuffer = glBuf;
        if (!initData && !buffer->m_MappedPtr) {
            std::vector<uint8_t> zero(bufSize, 0);
            gl.NamedBufferSubData(glBuf, 0, (GLsizeiptr)bufSize, zero.data());
        }
    }
    return buffer;
}

std::shared_ptr<IRHITexture> GL46Device::CreateTexture(const TextureDesc&) {
    return std::make_shared<GL46Texture>();
}

IRHIPipelineState* GL46Device::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    auto* pso = new GL46PipelineState(); pso->Desc = desc; return pso;
}

IRHIPipelineState* GL46Device::CreateComputePSO(const ComputePSODesc& desc) {
    auto* pso = new GL46ComputePipelineState();
    uint64_t hash = desc.computeShader.Value();
    pso->program = GetComputeProgram(hash);
    std::fprintf(stdout, "[GL46] CreateComputePSO: hash=0x%016llx, program=%p, glprog=%u\n",
                 (unsigned long long)hash, (void*)pso->program,
                 pso->program ? pso->program->program : 0);
    std::fflush(stdout);
    if (!pso->program) s_Log.Warn("Compute program not found for shader hash 0x{:016x}", hash);
    return pso;
}

std::unique_ptr<IRHICommandList> GL46Device::CreateCommandList(CommandListType) {
    auto cmd = std::make_unique<GL46CommandList>(); cmd->SetGLContext(m_GL); return cmd;
}

IRHICommandQueue* GL46Device::GetQueue(QueueType) { static GL46Queue queue(m_GL); return &queue; }

std::unique_ptr<IRHISwapChain> GL46Device::CreateSwapChain(const SwapChainDesc&) {
    return std::make_unique<GL46SwapChain>();
}

void GL46Device::WaitIdle() { if (m_GL && !m_IsStub) m_GL->Finish(); }
const char* GL46Device::GetDeviceName() const { return m_DeviceName.c_str(); }

GL46Buffer::~GL46Buffer() {}
uint64_t GL46Buffer::GetSize() const noexcept { return m_Size; }
const GPUAllocation& GL46Buffer::GetAllocation() const noexcept { return m_Allocation; }

} // namespace RHI
} // namespace Engine