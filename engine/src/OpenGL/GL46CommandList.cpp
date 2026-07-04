/**
 * @file GL46CommandList.cpp
 * @brief GL46 命令列表 — 数据导向间接绘制（存根）
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Engine {
namespace RHI {

struct GL46CommandList::Impl {
    std::vector<DrawElementsIndirectCommand> indirectCmds;
    int currentMode{0};      // GL_TRIANGLES = 0x0004, 接入后替换
    bool isRecording{false};
};

GL46CommandList::GL46CommandList() : m_Impl(std::make_unique<Impl>()) {}
GL46CommandList::~GL46CommandList() = default;

void GL46CommandList::Begin() { m_Impl->isRecording = true; m_Impl->indirectCmds.clear(); }
void GL46CommandList::End() { m_Impl->isRecording = false; }
void GL46CommandList::Reset() { m_Impl->indirectCmds.clear(); }
void GL46CommandList::SetPipelineState(IRHIPipelineState*) {}
void GL46CommandList::SetVertexBuffer(uint32, IRHIBuffer*, uint32, uint32) {}
void GL46CommandList::SetIndexBuffer(IRHIBuffer*, uint32) {}
void GL46CommandList::SetPrimitiveTopology(PrimitiveTopology) {}

void GL46CommandList::DrawIndexed(uint32 indexCount, uint32 startIndex, uint32 baseVertex) {
    if (!m_Impl->isRecording) return;
    DrawElementsIndirectCommand cmd{indexCount, 1, startIndex, static_cast<int32_t>(baseVertex), 0};
    m_Impl->indirectCmds.push_back(cmd);
}

void GL46CommandList::Draw(uint32, uint32) {}
void GL46CommandList::DrawIndexedIndirect(IRHIBuffer*, uint32) {}
void GL46CommandList::SetViewport(const Viewport&) {}
void GL46CommandList::SetScissorRect(const Rect&) {}
void GL46CommandList::ResourceBarrier(uint32, const ResourceBarrierDesc*) {}
CommandListType GL46CommandList::GetType() const noexcept { return CommandListType::Direct; }

void GL46CommandList::ExecuteOnMainThread() {
    // TODO: 接入 GladGLContext 后在此处调用 glMultiDrawElementsIndirect
}

uint32_t GL46CommandList::GetRecordedCommandCount() const noexcept {
    return static_cast<uint32_t>(m_Impl->indirectCmds.size());
}

} // namespace RHI
} // namespace Engine