/**
 * @file D3D12Device.cpp
 * @brief D3D12 设备骨架实现 — 接口存根
 */

#include "Engine/Core/RHI/D3D12IRHIDevice.h"
#include <cstdio>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// D3D12Device
// ════════════════════════════════════════════════════════════

struct D3D12Device::Impl { bool initialized{false}; };

D3D12Device::D3D12Device() : m_Impl(std::make_unique<Impl>()) {}
D3D12Device::~D3D12Device() = default;

bool D3D12Device::Initialize(void*, uint32_t, uint32_t) { return false; }

std::shared_ptr<IRHIBuffer> D3D12Device::CreateBuffer(const RHIBufferDesc&) { return nullptr; }
std::shared_ptr<IRHITexture> D3D12Device::CreateTexture(const TextureDesc&) { return nullptr; }
IRHIPipelineState* D3D12Device::CreateGraphicsPSO(const GraphicsPSODesc&) { return nullptr; }
IRHIPipelineState* D3D12Device::CreateComputePSO(const ComputePSODesc&) { return nullptr; }
std::unique_ptr<IRHICommandList> D3D12Device::CreateCommandList(CommandListType) { return nullptr; }
IRHICommandQueue* D3D12Device::GetQueue(QueueType) { return nullptr; }
std::unique_ptr<IRHISwapChain> D3D12Device::CreateSwapChain(const SwapChainDesc&) { return nullptr; }
void D3D12Device::WaitIdle() {}
const char* D3D12Device::GetDeviceName() const { return "D3D12 (stub)"; }

// ════════════════════════════════════════════════════════════
// D3D12CommandList
// ════════════════════════════════════════════════════════════

struct D3D12CommandList::Impl {};
D3D12CommandList::D3D12CommandList() : m_Impl(std::make_unique<Impl>()) {}
D3D12CommandList::~D3D12CommandList() = default;
void D3D12CommandList::Begin() {}
void D3D12CommandList::End() {}
void D3D12CommandList::Reset() {}
void D3D12CommandList::SetPipelineState(IRHIPipelineState*) {}
void D3D12CommandList::SetVertexBuffer(uint32, IRHIBuffer*, uint32, uint32) {}
void D3D12CommandList::SetIndexBuffer(IRHIBuffer*, uint32) {}
void D3D12CommandList::SetPrimitiveTopology(PrimitiveTopology) {}
void D3D12CommandList::DrawIndexed(uint32, uint32, uint32) {}
void D3D12CommandList::Draw(uint32, uint32) {}
void D3D12CommandList::DrawIndexedIndirect(IRHIBuffer*, uint32) {}
void D3D12CommandList::SetViewport(const Viewport&) {}
void D3D12CommandList::SetScissorRect(const Rect&) {}
void D3D12CommandList::ResourceBarrier(uint32, const ResourceBarrierDesc*) {}
CommandListType D3D12CommandList::GetType() const noexcept { return CommandListType::Direct; }

// ════════════════════════════════════════════════════════════
// D3D12Buffer
// ════════════════════════════════════════════════════════════

struct D3D12Buffer::Impl {};
D3D12Buffer::D3D12Buffer() : m_Impl(std::make_unique<Impl>()) {}
D3D12Buffer::~D3D12Buffer() = default;
uint64_t D3D12Buffer::GetSize() const noexcept { return 0; }
const GPUAllocation& D3D12Buffer::GetAllocation() const noexcept { return NullAllocation(); }

// ════════════════════════════════════════════════════════════
// D3D12Texture
// ════════════════════════════════════════════════════════════

struct D3D12Texture::Impl {};
D3D12Texture::D3D12Texture() : m_Impl(std::make_unique<Impl>()) {}
D3D12Texture::~D3D12Texture() = default;
uint32_t D3D12Texture::GetWidth() const noexcept { return 0; }
uint32_t D3D12Texture::GetHeight() const noexcept { return 0; }
Format D3D12Texture::GetFormat() const noexcept { return Format::Unknown; }

// ════════════════════════════════════════════════════════════
// D3D12SwapChain
// ════════════════════════════════════════════════════════════

struct D3D12SwapChain::Impl {};
D3D12SwapChain::D3D12SwapChain() : m_Impl(std::make_unique<Impl>()) {}
D3D12SwapChain::~D3D12SwapChain() = default;
void D3D12SwapChain::Present() {}
void D3D12SwapChain::Resize(uint32_t, uint32_t) {}
IRHITexture* D3D12SwapChain::GetBackBuffer(uint32_t) const { return nullptr; }
uint32_t D3D12SwapChain::GetCurrentBackBufferIndex() const { return 0; }
uint32_t D3D12SwapChain::GetBufferCount() const { return 2; }

// ════════════════════════════════════════════════════════════
// D3D12Queue
// ════════════════════════════════════════════════════════════

struct D3D12Queue::Impl {};
D3D12Queue::D3D12Queue() : m_Impl(std::make_unique<Impl>()) {}
D3D12Queue::~D3D12Queue() = default;
void D3D12Queue::ExecuteCommandLists(uint32, IRHICommandList**) {}
void D3D12Queue::WaitIdle() {}
QueueType D3D12Queue::GetType() const noexcept { return QueueType::Graphics; }

// ════════════════════════════════════════════════════════════
// 辅助函数
// ════════════════════════════════════════════════════════════

bool HasD3D12Support() noexcept { return false; }
std::string GetD3D12AdapterInfo() noexcept { return "D3D12 (stub)"; }
std::unique_ptr<IRHIDevice> CreateD3D12Device() { return nullptr; }

} // namespace RHI
} // namespace Engine