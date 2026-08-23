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

    // ── SSBO 多绑定（Phase 2 Ping-Pong：binding0=src, binding1=dst；
    //    Phase 4 Spatial Hash：collide_spatial 使用到 binding5）──
    static constexpr uint32_t kMaxSSBOBindings = 8;
    struct SSBOSlot {
        uint32_t binding = 0;
        uint32_t handle  = 0;
        bool     set     = false;
    };
    SSBOSlot ssbo[kMaxSSBOBindings];

    std::vector<CachedUniform> pendingUniforms;

    // ── GPU 时间戳查询环（Phase 0 Benchmark Baseline）──
    // 模式复用 OpenGLContext::InitGPUQueries 的已验证实现：
    // glGenQueries 池 + glQueryCounter(GL_TIMESTAMP) + glGetQueryObjectui64v 解析
    static constexpr uint32_t kMaxTimestamps = 512;
    GLuint queryPool[kMaxTimestamps] = {};
    uint32_t tsCount = 0;
    bool poolReady = false;
};

GL46CommandList::GL46CommandList() : m_Impl(std::make_unique<Impl>()) {}
GL46CommandList::~GL46CommandList() = default;

void GL46CommandList::Begin() {
    m_Impl->isRecording = true;
    m_Impl->currentComputePSO = nullptr;
    for (auto& s : m_Impl->ssbo) s = {};
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::End() {
    if (m_GL) m_GL->MemoryBarrier(GL_ALL_BARRIER_BITS);
    m_Impl->isRecording = false;
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::Reset() {
    m_Impl->currentComputePSO = nullptr;
    for (auto& s : m_Impl->ssbo) s = {};
    m_Impl->pendingUniforms.clear();
}

void GL46CommandList::SetPipelineState(IRHIPipelineState* pso) {
    if (!m_Impl->isRecording || !m_GL) return;
    if (!pso) return;

    auto* computePSO = dynamic_cast<GL46ComputePipelineState*>(pso);
    if (computePSO && computePSO->program && computePSO->program->program) {
        m_Impl->currentComputePSO = computePSO;
        for (auto& s : m_Impl->ssbo) s.set = false;
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

    // Step 2: Bind SSBO（多绑定支持 —— Phase 2 Ping-Pong: binding0=src, binding1=dst）
    bool anyBound = false;
    for (const auto& s : m_Impl->ssbo) {
        if (!s.set || s.handle == 0) continue;
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, s.binding, s.handle);
        anyBound = true;
    }
    if (!anyBound)
        s_Log.Warn("Dispatch: no SSBO bound");

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

    if (slot >= Impl::kMaxSSBOBindings) {
        s_Log.Warn("SetUnorderedAccess: slot {} exceeds kMaxSSBOBindings", slot);
        return;
    }

    auto& s = m_Impl->ssbo[slot];
    s.binding = slot;
    s.handle  = gl46Buf->GetGLHandle();
    s.set     = true;

    m_GL->BindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, s.handle);
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

// ══════════════════════════════════════════════════════════
// GPU 时间戳（Phase 0 Benchmark Baseline）
// GL 立即模式执行 ⇒ 录制点即 GPU 时间线插入点
// ══════════════════════════════════════════════════════════

uint32_t GL46CommandList::WriteTimestamp() {
    if (!m_GL || !m_Impl->isRecording) return kInvalidTimestamp;

    if (!m_Impl->poolReady) {
        m_GL->GenQueries(Impl::kMaxTimestamps, m_Impl->queryPool);
        m_Impl->poolReady = true;
    }
    if (m_Impl->tsCount >= Impl::kMaxTimestamps) {
        static bool warned = false;
        if (!warned) {
            s_Log.Warn("WriteTimestamp: pool exhausted ({}), call ResetTimestamps after resolve",
                       Impl::kMaxTimestamps);
            warned = true;
        }
        return kInvalidTimestamp;
    }

    const uint32_t idx = m_Impl->tsCount++;
    m_GL->QueryCounter(m_Impl->queryPool[idx], GL_TIMESTAMP);
    return idx;
}

bool GL46CommandList::ResolveTimestampSpan(uint32 beginIdx, uint32 endIdx, double& outMs) {
    if (!m_GL || !m_Impl->poolReady) return false;
    if (beginIdx >= m_Impl->tsCount || endIdx >= m_Impl->tsCount || beginIdx > endIdx) return false;

    GLuint64 startNs = 0, endNs = 0;
    m_GL->GetQueryObjectui64v(m_Impl->queryPool[beginIdx], GL_QUERY_RESULT, &startNs);
    if (endIdx > beginIdx)
        m_GL->GetQueryObjectui64v(m_Impl->queryPool[endIdx], GL_QUERY_RESULT, &endNs);
    else
        endNs = startNs;

    outMs = static_cast<double>(endNs - startNs) / 1.0e6;
    return true;
}

void GL46CommandList::ResetTimestamps() {
    // 查询对象复用语义：WaitIdle 后所有查询已完成，直接回卷计数即可。
    // glQueryCounter 对已 signal 的查询对象重新写入是合法操作。
    if (m_Impl->poolReady) m_Impl->tsCount = 0;
}

void GL46CommandList::ExecuteOnMainThread() {}
uint32_t GL46CommandList::GetRecordedCommandCount() const noexcept { return m_Impl->isRecording ? 1 : 0; }

} // namespace RHI
} // namespace Engine