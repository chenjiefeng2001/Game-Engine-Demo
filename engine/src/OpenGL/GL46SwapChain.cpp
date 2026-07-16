/**
 * @file GL46SwapChain.cpp
 * @brief GL46 交换链 + 队列实现
 *
 * 注意：GL46Buffer 析构现在在 GL46Device.cpp 中
 */

#include "Engine/Core/RHI/GL46AZDODevice.h"

namespace Engine {
namespace RHI {

// GL46Buffer 的析构在 GL46Device.cpp 中实现
// 此文件仅包含无冲突的函数

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
        auto* cmd = static_cast<GL46CommandList*>(lists[i]);
        cmd->ExecuteOnMainThread();
    }
    // Dispatch 等 GL 命令已由 CommandList 方法直接执行。
    // ExecuteCommandLists 后需要确保 GPU 可见性
}

void GL46Queue::WaitIdle() {
    // 等待由调用方通过 GL46Device::WaitIdle() 或 glFinish 处理
}

QueueType GL46Queue::GetType() const noexcept { return QueueType::Graphics; }

} // namespace RHI
} // namespace Engine