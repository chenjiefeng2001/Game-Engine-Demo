/**
 * @file GL46Device.cpp
 * @brief OpenGL 4.6 AZDO 设备实现（暂为存根，需接入 GladGLContext）
 *
 * 注意：当前项目 glad2 使用 MX（多上下文）模式，所有 GL 函数通过
 * GladGLContext 结构体访问（如 m_GL.CreateBuffers），非全局符号。
 * 完整实现在接入现有 GladGLContext 后激活。
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include <cstdio>

namespace Engine {
namespace RHI {

struct GL46Device::Impl {
    bool initialized{false};
};

GL46Device::GL46Device() : m_Impl(std::make_unique<Impl>()) {}
GL46Device::~GL46Device() = default;
void GL46Device::Shutdown() { m_Impl->initialized = false; }

bool GL46Device::Initialize(void*, uint32_t, uint32_t) {
    if (m_Impl->initialized) return true;
    std::fprintf(stdout, "[GL46] Device stub initialized\n");
    m_Impl->initialized = true;
    return true;
}

std::shared_ptr<IRHIBuffer> GL46Device::CreateBuffer(const RHIBufferDesc& desc) {
    auto buffer = std::make_shared<GL46Buffer>();
    GPUAllocation alloc;
    alloc.size = desc.size;
    buffer->m_Allocation = alloc;
    buffer->m_Size = desc.size;
    return buffer;
}

std::shared_ptr<IRHITexture> GL46Device::CreateTexture(const TextureDesc& desc) {
    return std::make_shared<GL46Texture>();
}

IRHIPipelineState* GL46Device::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    auto* pso = new GL46PipelineState();
    pso->Desc = desc;
    return pso;
}

IRHIPipelineState* GL46Device::CreateComputePSO(const ComputePSODesc&) {
    return new GL46PipelineState();
}

std::unique_ptr<IRHICommandList> GL46Device::CreateCommandList(CommandListType) {
    return std::make_unique<GL46CommandList>();
}

IRHICommandQueue* GL46Device::GetQueue(QueueType) {
    static GL46Queue queue;
    return &queue;
}

std::unique_ptr<IRHISwapChain> GL46Device::CreateSwapChain(const SwapChainDesc&) {
    return std::make_unique<GL46SwapChain>();
}

void GL46Device::WaitIdle() {}
const char* GL46Device::GetDeviceName() const { return "OpenGL 4.6 AZDO (stub)"; }

} // namespace RHI
} // namespace Engine