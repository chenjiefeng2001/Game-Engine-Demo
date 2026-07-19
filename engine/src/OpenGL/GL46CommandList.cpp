/**
 * @file GL46CommandList.cpp
 * @brief GL46 命令列表 - 真实施行后端
 *
 * 实现所有 IRHICommandList 接口，直接在 GladGLContext 上执行 GL 调用。
 * 用于 Compute Shader 调度（Dispatch/SetUnorderedAccess/ResourceBarrier）。
 *
 * Uniform 传递策略（修复记录 v4 - 最终方案）：
 *   使用 glProgramUniform* (DSA) 接口直接设置 uniform。
 *   glProgramUniform* 不依赖当前 program 是否 active，
 *   直接将值写入指定 program 对象，完美绕过所有程序切换问题。
 *   在 SetCompute* 中立即施加，在 Dispatch 中冗余施加。
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <vector>
#include <string>

namespace Engine {
namespace RHI {

static Logger s_Log("GL46CommandList");

// Uniform 值类型枚举
enum class UniformType : uint8_t {
    Float,
    Vec3,
    Int
};

// 缓存的 Uniform 值
struct CachedUniform {
    std::string name;
    UniformType type;
    union {
        float floatVal[4];
        int32_t intVal;
    } data;
};

struct GL46CommandList::Impl {
    bool isRecording{false};
    GL46ComputePipelineState* currentComputePSO{nullptr};
    uint32_t ssboBinding{0};
    uint32_t ssboHandle{0};
    std::vector<CachedUniform> pendingUniforms;
};

GL46CommandList::GL46CommandList() : m_Impl(std::make_unique<Impl>()) {}
GL46CommandList::~GL46CommandList() = default;

void GL46CommandList::Begin() {
    m_Impl->isRecording = true;
    m_Impl->currentComputePSO = nullptr;
    m_Impl->ssboBinding = 0;
    m_Impl->ssboHandle = 0;
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::End() {
    if (m_GL) m_GL->MemoryBarrier(GL_ALL_BARRIER_BITS);
    m_Impl->isRecording = false;
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::Reset() {
    m_Impl->currentComputePSO = nullptr;
    m_Impl->ssboBinding = 0;
    m_Impl->ssboHandle = 0;
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::SetPipelineState(IRHIPipelineState* pso) {
    if (!m_Impl->isRecording || !m_GL) return;
    if (!pso) return;

    auto* computePSO = dynamic_cast<GL46ComputePipelineState*>(pso);
    if (computePSO && computePSO->program && computePSO->program->program) {
        m_Impl->currentComputePSO = computePSO;
        m_Impl->ssboHandle = 0;
        m_Impl->pendingUniforms.clear();
        m_GL->UseProgram(computePSO->program->program);
    }
}

void GL46CommandList::SetVertexBuffer(uint32, IRHIBuffer*, uint32, uint32) {}
void GL46CommandList::SetIndexBuffer(IRHIBuffer*, uint32) {}
void GL46CommandList::SetPrimitiveTopology(PrimitiveTopology) {}
void GL46CommandList::DrawIndexed(uint32, uint32, uint32) {}
void GL46CommandList::Draw(uint32, uint32) {}
void GL46CommandList::DrawIndexedIndirect(IRHIBuffer*, uint32) {}
void GL46CommandList::SetViewport(const Viewport&) {}
void GL46CommandList::SetScissorRect(const Rect&) {}

void GL46CommandList::ResourceBarrier(uint32 count, const ResourceBarrierDesc* barriers) {
    if (!m_Impl->isRecording || !m_GL) return;
    for (uint32 i = 0; i < count; ++i) {
        const auto& b = barriers[i];
        if (b.type == ResourceBarrierDesc::Type::UAV)
            // 必须包含 CLIENT_MAPPED_BUFFER_BARRIER_BIT 以确保持久映射的 CPU 端
            // 能正确看到 GPU compute shader 的写入结果（COHERENT 映射需要此屏障）
            m_GL->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                                GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                                GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        else if (b.type == ResourceBarrierDesc::Type::Transition)
            m_GL->MemoryBarrier(GL_ALL_BARRIER_BITS);
    }
}

void GL46CommandList::SetConstantBuffer(uint32, uint32, IRHIBuffer*, uint64_t, uint64_t) {}
void GL46CommandList::SetShaderResource(uint32, uint32, IRHITexture*) {}

void GL46CommandList::Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
    if (!m_Impl->isRecording || !m_GL) {
        fprintf(stdout, "    [Dispatch SKIP] recording=%d gl=%p\n", m_Impl->isRecording, (void*)m_GL);
        fflush(stderr);
        return;
    }

    auto* pso = m_Impl->currentComputePSO;
    GL46ComputeProgram* prog = pso ? pso->program : nullptr;
    if (!prog || !prog->program) {
        s_Log.Warn("Dispatch: no valid compute PSO");
        return;
    }

    GladGLContext& gl = *m_GL;
    uint32_t program = prog->program;

    // Step 1: Activate target program
    gl.UseProgram(program);

    // Step 2: Bind SSBO
    if (m_Impl->ssboHandle != 0)
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, m_Impl->ssboBinding, m_Impl->ssboHandle);
    else
        s_Log.Warn("Dispatch: ssboHandle=0");

    // Step 3: Apply uniform via glUniform* (traditional approach)
    // glProgramUniform* (DSA) had issues with uint uniform types on NVIDIA drivers.
    // Since glUseProgram is already called above, glUniform* is safe.
    for (const auto& u : m_Impl->pendingUniforms) {
        GLint loc = gl.GetUniformLocation(program, u.name.c_str());
        if (loc < 0) continue;
        switch (u.type) {
            case UniformType::Float:
                gl.Uniform1f(loc, u.data.floatVal[0]);
                break;
            case UniformType::Vec3:
                gl.Uniform3f(loc, u.data.floatVal[0], u.data.floatVal[1], u.data.floatVal[2]);
                break;
            case UniformType::Int:
                gl.Uniform1ui(loc, static_cast<GLuint>(u.data.intVal));
                break;
        }
    }

    // Step 4: Dispatch
    gl.DispatchCompute(groupX, groupY, groupZ);

    // Step 5: Memory barrier
    gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                     GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
}

void GL46CommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    if (!m_Impl->isRecording || !m_GL) return;
    if (!buffer) return;

    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer);
    if (!gl46Buf) return;

    m_Impl->ssboBinding = slot;
    m_Impl->ssboHandle = gl46Buf->GetGLHandle();

    m_GL->BindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, m_Impl->ssboHandle);
}

// SetCompute* use glProgramUniform* (DSA) - no glUseProgram required
void GL46CommandList::SetComputeFloat(const char* name, float value) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;

    // Immediately apply via glUniform* (program is already active from SetPipelineState)
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0)
        m_GL->Uniform1f(loc, value);

    // Cache for redispatch redundancy
    for (auto& existing : m_Impl->pendingUniforms) {
        if (existing.name == name && existing.type == UniformType::Float) {
            existing.data.floatVal[0] = value;
            return;
        }
    }
    CachedUniform cu;
    cu.name = name;
    cu.type = UniformType::Float;
    cu.data.floatVal[0] = value;
    m_Impl->pendingUniforms.push_back(std::move(cu));
}

void GL46CommandList::SetComputeVec3(const char* name, float x, float y, float z) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;

    // Immediately apply via glUniform* (program is already active from SetPipelineState)
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0)
        m_GL->Uniform3f(loc, x, y, z);

    // Cache
    for (auto& existing : m_Impl->pendingUniforms) {
        if (existing.name == name && existing.type == UniformType::Vec3) {
            existing.data.floatVal[0] = x;
            existing.data.floatVal[1] = y;
            existing.data.floatVal[2] = z;
            return;
        }
    }
    CachedUniform cu;
    cu.name = name;
    cu.type = UniformType::Vec3;
    cu.data.floatVal[0] = x;
    cu.data.floatVal[1] = y;
    cu.data.floatVal[2] = z;
    m_Impl->pendingUniforms.push_back(std::move(cu));
}

void GL46CommandList::SetComputeInt(const char* name, int32_t value) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;

    // Immediately apply via glUniform* (program is already active from SetPipelineState)
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0)
        m_GL->Uniform1ui(loc, static_cast<GLuint>(value));

    // Cache
    for (auto& existing : m_Impl->pendingUniforms) {
        if (existing.name == name && existing.type == UniformType::Int) {
            existing.data.intVal = value;
            return;
        }
    }
    CachedUniform cu;
    cu.name = name;
    cu.type = UniformType::Int;
    cu.data.intVal = value;
    m_Impl->pendingUniforms.push_back(std::move(cu));
}

CommandListType GL46CommandList::GetType() const noexcept { return CommandListType::Direct; }

void GL46CommandList::ExecuteOnMainThread() {}
uint32_t GL46CommandList::GetRecordedCommandCount() const noexcept { return m_Impl->isRecording ? 1 : 0; }

} // namespace RHI
} // namespace Engine