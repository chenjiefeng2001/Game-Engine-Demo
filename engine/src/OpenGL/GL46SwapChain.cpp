/**
 * @file GL46SwapChain.cpp
 * @brief GL46 交换链 + 队列 + Buffer 析构（存根）
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"

namespace Engine {
namespace RHI {

GL46Buffer::~GL46Buffer() {
    if (m_GLBuffer) {
        // TODO: 通过 GladGLContext 调用 glDeleteBuffers
        m_GLBuffer = 0;
    }
}

uint64_t GL46Buffer::GetSize() const noexcept { return m_Size; }
const GPUAllocation& GL46Buffer::GetAllocation() const noexcept { return m_Allocation; }

uint32_t GL46Texture::GetWidth() const noexcept { return 0; }
uint32_t GL46Texture::GetHeight() const noexcept { return 0; }
Format GL46Texture::GetFormat() const noexcept { return Format::RGBA8_UNorm; }

void GL46SwapChain::Present() {}
void GL46SwapChain::Resize(uint32_t, uint32_t) {}
IRHITexture* GL46SwapChain::GetBackBuffer(uint32_t) const { return nullptr; }
uint32_t GL46SwapChain::GetCurrentBackBufferIndex() const { return 0; }
uint32_t GL46SwapChain::GetBufferCount() const { return 1; }

void GL46Queue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    for (uint32_t i = 0; i < count; ++i) {
        static_cast<GL46CommandList*>(lists[i])->ExecuteOnMainThread();
    }
}

void GL46Queue::WaitIdle() {}
QueueType GL46Queue::GetType() const noexcept { return QueueType::Graphics; }

} // namespace RHI
} // namespace Engine