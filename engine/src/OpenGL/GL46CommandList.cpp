/**
 * @file GL46CommandList.cpp
 * @brief GL46 命令列表 — 真实施行后端
 *
 * 实现所有 IRHICommandList 接口，直接在 GladGLContext 上执行 GL 调用。
 * 用于 Compute Shader 调度（Dispatch/SetUnorderedAccess/ResourceBarrier）。
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <vector>

namespace Engine {
namespace RHI {

static Logger s_Log("GL46CommandList");

struct GL46CommandList::Impl {
    bool isRecording{false};
    GL46ComputePipelineState* currentComputePSO{nullptr};
    uint32_t ssboBinding{0};
    uint32_t ssboHandle{0};
};

GL46CommandList::GL46CommandList() : m_Impl(std::make_unique<Impl>()) {}
GL46CommandList::~GL46CommandList() = default;

void GL46CommandList::Begin() {
    m_Impl->isRecording = true;
    m_Impl->currentComputePSO = nullptr;
    m_Impl->ssboBinding = 0;
    m_Impl->ssboHandle = 0;
}

void GL46CommandList::End() {
    if (m_GL) m_GL->MemoryBarrier(GL_ALL_BARRIER_BITS);
    m_Impl->isRecording = false;
}

void GL46CommandList::Reset() {
    m_Impl->currentComputePSO = nullptr;
    m_Impl->ssboBinding = 0;
    m_Impl->ssboHandle = 0;
}

void GL46CommandList::SetPipelineState(IRHIPipelineState* pso) {
    if (!m_Impl->isRecording || !m_GL) return;
    if (!pso) return;

    auto* computePSO = dynamic_cast<GL46ComputePipelineState*>(pso);
    if (computePSO && computePSO->program && computePSO->program->program) {
        m_Impl->currentComputePSO = computePSO;
        m_Impl->ssboHandle = 0;
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
            m_GL->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
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

    m_GL->UseProgram(prog->program);

    if (m_Impl->ssboHandle != 0)
        m_GL->BindBufferBase(GL_SHADER_STORAGE_BUFFER, m_Impl->ssboBinding, m_Impl->ssboHandle);
    else
        s_Log.Warn("Dispatch: ssboHandle=0");

    fprintf(stdout, "    [DISPATCH EXECUTE] prog=%u groups=(%u,%u,%u)\n",
            prog->program, groupX, groupY, groupZ);
    fflush(stdout);

    m_GL->DispatchCompute(groupX, groupY, groupZ);
    // 必须包含 CLIENT_MAPPED_BUFFER 位，确保 GPU 写入对 CPU 持久映射可见
    m_GL->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                        GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
}

void GL46CommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    if (!m_Impl->isRecording || !m_GL) return;
    if (!buffer) return;

    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer);
    if (!gl46Buf) return;

    m_Impl->ssboBinding = slot;
    m_Impl->ssboHandle = gl46Buf->GetGLHandle();

    // 立即绑定 SSBO，确保着色器能看到它
    m_GL->BindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, m_Impl->ssboHandle);
}

void GL46CommandList::SetComputeFloat(const char* name, float value) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0) m_GL->Uniform1f(loc, value);
}

void GL46CommandList::SetComputeVec3(const char* name, float x, float y, float z) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0) m_GL->Uniform3f(loc, x, y, z);
}

void GL46CommandList::SetComputeInt(const char* name, int32_t value) {
    if (!m_GL || !m_Impl->currentComputePSO) return;
    if (!m_Impl->currentComputePSO->program) return;
    uint32 prog = m_Impl->currentComputePSO->program->program;
    if (!prog) return;
    GLint loc = m_GL->GetUniformLocation(prog, name);
    if (loc >= 0) m_GL->Uniform1i(loc, value);
}

CommandListType GL46CommandList::GetType() const noexcept { return CommandListType::Direct; }

void GL46CommandList::ExecuteOnMainThread() {}
uint32_t GL46CommandList::GetRecordedCommandCount() const noexcept { return m_Impl->isRecording ? 1 : 0; }

} // namespace RHI
} // namespace Engine