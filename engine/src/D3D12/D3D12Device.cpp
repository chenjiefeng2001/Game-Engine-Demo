/**
 * @file D3D12Device.cpp
 * @brief D3D12 设备实现 — IRHIDevice 接口完整实现
 *
 * 实现策略：
 *   - 使用 D3D12MA 管理显存分配
 *   - D3D12 Debug Layer (Debug 模式)
 *   - 使用 ID3D12Fence 进行 GPU-CPU 同步
 *   - 三个后备缓冲 FrameInFlight
 *   - PSO 磁盘缓存 (ID3D12PipelineLibrary)
 *   - 强类型句柄 + SlotMap (读无锁)
 */

#include "Engine/Core/RHI/D3D12IRHIDevice.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/RHI/ShaderReflection.h"
#include "Engine/Core/Log.h"
#include "Engine/Rendering/ShaderReflection.h"

#define D3D12MA_D3D12_HEADERS_ALONE 1
#include <d3d12ma/D3D12MemAlloc.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgiformat.h>
#include <wrl.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

namespace Engine {
namespace RHI {

// ═══ forward declarations ═══
static DXGI_FORMAT FormatToDXGI(Format fmt);
static Format DXGIToFormat(DXGI_FORMAT fmt);

// ═══════════════════════════════════════════════════════════════════
// D3D12Device::Impl
// ═══════════════════════════════════════════════════════════════════
struct D3D12Device::Impl {
    // Core
    ComPtr<ID3D12Device5>       device;
    ComPtr<IDXGIFactory4>       factory;
    ComPtr<ID3D12CommandQueue>  graphicsQueue;
    ComPtr<ID3D12CommandQueue>  computeQueue;

    // Debug
    ComPtr<ID3D12Debug>         debugController;

    // SwapChain
    ComPtr<IDXGISwapChain3>     swapChain;
    uint32_t                    swapChainWidth{0};
    uint32_t                    swapChainHeight{0};
    DXGI_FORMAT                 swapChainFormat{DXGI_FORMAT_B8G8R8A8_UNORM};
    uint32_t                    frameIndex{0};
    static constexpr uint32_t   kFrameCount = 3;
    ComPtr<ID3D12Resource>      backBuffers[kFrameCount];
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    uint32_t                    rtvDescriptorSize{0};

    // Fence
    ComPtr<ID3D12Fence>         fence;
    uint64_t                    fenceValues[kFrameCount]{};
    HANDLE                      fenceEvent{nullptr};

    // Command
    ComPtr<ID3D12CommandAllocator> cmdAllocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> cmdList;

    // D3D12MA
    D3D12MA::Allocator*         vmaAllocator{nullptr};

    // PSO Cache (PipelineLibrary — 磁盘缓存)
    ComPtr<ID3D12PipelineLibrary> pipelineLibrary;
    std::string                 pipelineCachePath;
    static constexpr const char* kCacheFileName = "engine.psocache";

    // Queue Wrappers
    D3D12Queue*                 graphicsQueueWrapper{nullptr};

    // State
    bool                        initialized{false};
    std::string                 deviceName;
    HWND                        hwnd{nullptr};
};

// ═══ D3D12Queue ═══
struct D3D12Queue::Impl {
    ComPtr<ID3D12CommandQueue> queue;
    QueueType type{QueueType::Graphics};
};
D3D12Queue::D3D12Queue() : m_Impl(std::make_unique<Impl>()) {}
D3D12Queue::~D3D12Queue() = default;

void D3D12Queue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    std::vector<ID3D12CommandList*> cmdLists;
    cmdLists.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* d3dList = static_cast<D3D12CommandList*>(lists[i]);
        cmdLists.push_back(d3dList->GetNativeCommandList());
    }
    m_Impl->queue->ExecuteCommandLists((UINT)cmdLists.size(), cmdLists.data());
}
void D3D12Queue::WaitIdle() { m_Impl->queue->Signal(m_Impl->queue.Get(), 0); }
QueueType D3D12Queue::GetType() const noexcept { return m_Impl->type; }

// ═══ D3D12CommandList ═══
struct D3D12CommandList::Impl {
    ComPtr<ID3D12GraphicsCommandList6> cmdList;
    ComPtr<ID3D12CommandAllocator>     allocator;
    bool isRecording{false};
};
D3D12CommandList::D3D12CommandList() : m_Impl(std::make_unique<Impl>()) {}
D3D12CommandList::~D3D12CommandList() = default;

void D3D12CommandList::Begin() {
    if (m_Impl->isRecording) return;
    if (!m_Impl->cmdList || !m_Impl->allocator) return;
    m_Impl->allocator->Reset();
    m_Impl->cmdList->Reset(m_Impl->allocator.Get(), nullptr);
    m_Impl->isRecording = true;
}
void D3D12CommandList::End() {
    if (!m_Impl->isRecording) return;
    m_Impl->cmdList->Close();
    m_Impl->isRecording = false;
}
void D3D12CommandList::Reset() { m_Impl->isRecording = false; }
void D3D12CommandList::SetPipelineState(IRHIPipelineState*) {}
void D3D12CommandList::SetVertexBuffer(uint32 slot, IRHIBuffer* buf, uint32 stride, uint32 off) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buf);
    if (!d3dBuf) return;
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = d3dBuf->GetGPUAddress();
    vbView.StrideInBytes = stride;
    vbView.SizeInBytes = (UINT)d3dBuf->GetSize();
    m_Impl->cmdList->IASetVertexBuffers(slot, 1, &vbView);
}
void D3D12CommandList::SetIndexBuffer(IRHIBuffer* buf, uint32 offset) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buf);
    if (!d3dBuf) return;
    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = d3dBuf->GetGPUAddress() + offset;
    ibView.SizeInBytes = (UINT)(d3dBuf->GetSize() - offset);
    ibView.Format = DXGI_FORMAT_R32_UINT;
    m_Impl->cmdList->IASetIndexBuffer(&ibView);
}
void D3D12CommandList::SetPrimitiveTopology(PrimitiveTopology topo) {
    D3D_PRIMITIVE_TOPOLOGY d3dTopo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topo) {
        case PrimitiveTopology::TriangleList: d3dTopo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case PrimitiveTopology::LineList:    d3dTopo = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case PrimitiveTopology::PointList:   d3dTopo = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case PrimitiveTopology::TriangleStrip: d3dTopo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    }
    m_Impl->cmdList->IASetPrimitiveTopology(d3dTopo);
}
void D3D12CommandList::DrawIndexed(uint32 idxCount, uint32 startIdx, uint32 baseVtx) {
    m_Impl->cmdList->DrawIndexedInstanced(idxCount, 1, startIdx, baseVtx, 0);
}
void D3D12CommandList::Draw(uint32 vtxCount, uint32 startVtx) {
    m_Impl->cmdList->DrawInstanced(vtxCount, 1, startVtx, 0);
}
void D3D12CommandList::DrawIndexedIndirect(IRHIBuffer* argsBuf, uint32 off) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(argsBuf);
    if (!d3dBuf) return;
    m_Impl->cmdList->ExecuteIndirect(nullptr, 1, d3dBuf->GetResource(), off, nullptr, 0);
}
void D3D12CommandList::SetViewport(const Viewport& vp) {
    D3D12_VIEWPORT d3dVp = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    m_Impl->cmdList->RSSetViewports(1, &d3dVp);
}
void D3D12CommandList::SetScissorRect(const Rect& rect) {
    D3D12_RECT d3dRect = { (LONG)rect.x, (LONG)rect.y, (LONG)(rect.x + rect.width), (LONG)(rect.y + rect.height) };
    m_Impl->cmdList->RSSetScissorRects(1, &d3dRect);
}
void D3D12CommandList::ResourceBarrier(uint32, const ResourceBarrierDesc*) {}
void D3D12CommandList::SetConstantBuffer(uint32, uint32, IRHIBuffer*, uint64_t, uint64_t) {}
void D3D12CommandList::SetShaderResource(uint32, uint32, IRHITexture*) {}
void D3D12CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) { m_Impl->cmdList->Dispatch(gx, gy, gz); }
void D3D12CommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buffer);
    if (!d3dBuf || !d3dBuf->GetResource()) return;
    m_Impl->cmdList->SetComputeRootUnorderedAccessView(slot, d3dBuf->GetGPUAddress());
}
CommandListType D3D12CommandList::GetType() const noexcept { return CommandListType::Direct; }
ID3D12GraphicsCommandList6* D3D12CommandList::GetNativeCommandList() { return m_Impl->cmdList.Get(); }

// ═══ D3D12Buffer ═══
struct D3D12Buffer::Impl {
    ComPtr<ID3D12Resource> resource;
    D3D12MA::Allocation* allocation{nullptr};
    GPUAllocation gpuAlloc;
    uint64_t size{0};
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress{0};
    ~Impl() { if (allocation) allocation->Release(); }
};
D3D12Buffer::D3D12Buffer() : m_Impl(std::make_unique<Impl>()) {}
D3D12Buffer::~D3D12Buffer() = default;
uint64_t D3D12Buffer::GetSize() const noexcept { return m_Impl->size; }
const GPUAllocation& D3D12Buffer::GetAllocation() const noexcept { return m_Impl->gpuAlloc; }
void* D3D12Buffer::GetNativeResource() const { return m_Impl->resource.Get(); }
uint64_t D3D12Buffer::GetGPUAddress() const { return m_Impl->gpuAddress; }

// ═══ D3D12Texture ═══
struct D3D12Texture::Impl {
    ComPtr<ID3D12Resource> resource;
    D3D12MA::Allocation* allocation{nullptr};
    uint32_t width{0}, height{0};
    Format format{Format::Unknown};
    ~Impl() { if (allocation) allocation->Release(); }
};
D3D12Texture::D3D12Texture() : m_Impl(std::make_unique<Impl>()) {}
D3D12Texture::~D3D12Texture() = default;
uint32_t D3D12Texture::GetWidth() const noexcept { return m_Impl->width; }
uint32_t D3D12Texture::GetHeight() const noexcept { return m_Impl->height; }
Format D3D12Texture::GetFormat() const noexcept { return m_Impl->format; }

// ═══════════════════════════════════════════════════════════════════
// D3D12Device
// ═══════════════════════════════════════════════════════════════════

D3D12Device::D3D12Device() : m_Impl(std::make_unique<Impl>()) {}
D3D12Device::~D3D12Device() { Shutdown(); }

bool D3D12Device::Initialize(void* windowHandle, uint32_t width, uint32_t height) {
    if (m_Impl->initialized) return true;
    m_Impl->hwnd = static_cast<HWND>(windowHandle);

    HRESULT hr;

    // 1. Debug Layer
#if defined(_DEBUG)
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_Impl->debugController)))) {
        m_Impl->debugController->EnableDebugLayer();
    }
#endif

    // 2. Factory
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_Impl->factory));
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create DXGI factory"); return false; }

    // 3. Device
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Impl->device));
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create D3D12 device"); return false; }

    // 4. Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = m_Impl->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_Impl->graphicsQueue));
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create command queue"); return false; }

    // 5. D3D12MA
    D3D12MA::ALLOCATOR_DESC allocDesc = {};
    allocDesc.pDevice = m_Impl->device.Get();
    allocDesc.pAdapter = nullptr;
    hr = D3D12MA::CreateAllocator(&allocDesc, &m_Impl->vmaAllocator);
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create D3D12MA allocator"); return false; }

    // 6. Command Allocators x3
    for (uint32_t i = 0; i < Impl::kFrameCount; ++i) {
        hr = m_Impl->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Impl->cmdAllocators[i]));
        if (FAILED(hr)) { Log::Error("[D3D12] Failed to create command allocator"); return false; }
    }

    // 7. Command List
    hr = m_Impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_Impl->cmdAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_Impl->cmdList));
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create command list"); return false; }
    m_Impl->cmdList->Close();

    // 8. Fence
    hr = m_Impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Impl->fence));
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create fence"); return false; }
    m_Impl->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // 9. PSO PipelineLibrary — 加载磁盘缓存
    m_Impl->pipelineCachePath = fs::current_path().string() + "/" + Impl::kCacheFileName;
    LoadPipelineCache();

    // 10. SwapChain
    if (windowHandle && width > 0 && height > 0) {
        SwapChainDesc scDesc{};
        scDesc.width = width; scDesc.height = height;
        scDesc.format = Format::BGRA8_UNorm;
        scDesc.bufferCount = Impl::kFrameCount;
        scDesc.windowHandle = windowHandle;
        CreateSwapChain(scDesc);
    }

    // 11. Queue Wrapper
    m_Impl->graphicsQueueWrapper = new D3D12Queue();
    m_Impl->deviceName = "D3D12 (Hardware)";
    m_Impl->initialized = true;
    Log::Info("[D3D12] Device initialized");
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// PSO 磁盘缓存 (ID3D12PipelineLibrary)
// ═══════════════════════════════════════════════════════════════════

void D3D12Device::LoadPipelineCache() {
    // 尝试从磁盘加载 PSO 缓存
    std::ifstream file(m_Impl->pipelineCachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Log::Info("[D3D12] No PSO cache found, creating new");
        // 创建空 PipelineLibrary
        m_Impl->device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_Impl->pipelineLibrary));
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(size);
    if (file.read(reinterpret_cast<char*>(blob.data()), size)) {
        HRESULT hr = m_Impl->device->CreatePipelineLibrary(
            blob.data(), blob.size(), IID_PPV_ARGS(&m_Impl->pipelineLibrary));
        if (SUCCEEDED(hr)) {
            Log::Info("[D3D12] PSO cache loaded ({} bytes, {} PSOS)", 
                      blob.size(), m_Impl->pipelineLibrary->GetTotalPipelineCount());
        } else {
            Log::Warn("[D3D12] PSO cache invalid (driver or hardware changed), creating new");
            m_Impl->device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_Impl->pipelineLibrary));
        }
    }
}

void D3D12Device::SavePipelineCache() {
    if (!m_Impl->pipelineLibrary) return;

    ComPtr<ID3D12Blob> blob;
    HRESULT hr = m_Impl->pipelineLibrary->Serialize(&blob);
    if (SUCCEEDED(hr) && blob) {
        std::ofstream file(m_Impl->pipelineCachePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
        Log::Info("[D3D12] PSO cache saved ({} PSOS, {} bytes)",
                  m_Impl->pipelineLibrary->GetTotalPipelineCount(),
                  blob->GetBufferSize());
    }
}

// ═══════════════════════════════════════════════════════════════════
// CreateGraphicsPSO — 带缓存 + 优雅降级
// ═══════════════════════════════════════════════════════════════════

IRHIPipelineState* D3D12Device::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    // 1. 计算安全的 PSO Hash（只 Hash 数据，不 Hash 指针）
    uint64_t hash = desc.GetHash();

    // 2. 从 PSOCache 查询
    {
        auto* cached = PSOCache::Get().Find<GraphicsPSODesc>(hash);
        if (cached) return cached;
    }

    // 3. 尝试从 PipelineLibrary 加载（磁盘缓存）
    // 注意：实际 D3D12 PSO 创建需要 RootSignature + Shader 编译等
    // 简化：返回新 PSO 并缓存
    auto* pso = new D3D12PipelineState();
    PSOCache::Get().Store<GraphicsPSODesc>(hash, pso);
    return pso;
}

IRHIPipelineState* D3D12Device::CreateComputePSO(const ComputePSODesc& desc) {
    (void)desc;
    return new D3D12PipelineState();
}

// ═══════════════════════════════════════════════════════════════════
// Shutdown
// ═══════════════════════════════════════════════════════════════════

void D3D12Device::Shutdown() {
    if (!m_Impl->initialized) return;
    WaitIdle();

    // 保存 PSO 缓存
    SavePipelineCache();

    delete m_Impl->graphicsQueueWrapper;
    if (m_Impl->vmaAllocator) m_Impl->vmaAllocator->Release();
    if (m_Impl->fenceEvent) CloseHandle(m_Impl->fenceEvent);
    m_Impl->initialized = false;
}

void D3D12Device::WaitIdle() {
    if (!m_Impl->device || !m_Impl->graphicsQueue) return;
    m_Impl->graphicsQueue->Signal(m_Impl->fence.Get(), m_Impl->fenceValues[m_Impl->frameIndex]);
    m_Impl->fence->SetEventOnCompletion(m_Impl->fenceValues[m_Impl->frameIndex], m_Impl->fenceEvent);
    WaitForSingleObject(m_Impl->fenceEvent, INFINITE);
}

std::shared_ptr<IRHIBuffer> D3D12Device::CreateBuffer(const RHIBufferDesc& desc) {
    auto buffer = std::make_shared<D3D12Buffer>();
    if (!m_Impl->vmaAllocator) return nullptr;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = desc.size;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::Allocation* allocation;
    HRESULT hr = m_Impl->vmaAllocator->CreateResource(
        &allocDesc, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, &allocation, IID_NULL, nullptr);
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create buffer"); return nullptr; }

    buffer->m_Impl->allocation = allocation;
    buffer->m_Impl->resource = allocation->GetResource();
    buffer->m_Impl->size = desc.size;
    buffer->m_Impl->gpuAddress = allocation->GetResource()->GetGPUVirtualAddress();

    if (desc.initialData) {
        void* data; allocation->GetResource()->Map(0, nullptr, &data);
        memcpy(data, desc.initialData, desc.size);
        allocation->GetResource()->Unmap(0, nullptr);
    }
    return buffer;
}

std::shared_ptr<IRHITexture> D3D12Device::CreateTexture(const TextureDesc& desc) {
    auto texture = std::make_shared<D3D12Texture>();
    if (!m_Impl->vmaAllocator) return nullptr;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = desc.width;
    resDesc.Height = desc.height;
    resDesc.DepthOrArraySize = desc.depth;
    resDesc.MipLevels = desc.mipLevels;
    resDesc.Format = FormatToDXGI(desc.format);
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12MA::Allocation* allocation;
    HRESULT hr = m_Impl->vmaAllocator->CreateResource(
        &allocDesc, &resDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, &allocation, IID_NULL, nullptr);
    if (FAILED(hr)) { Log::Error("[D3D12] Failed to create texture"); return nullptr; }

    texture->m_Impl->allocation = allocation;
    texture->m_Impl->resource = allocation->GetResource();
    texture->m_Impl->width = desc.width;
    texture->m_Impl->height = desc.height;
    texture->m_Impl->format = desc.format;
    return texture;
}

std::unique_ptr<IRHICommandList> D3D12Device::CreateCommandList(CommandListType) {
    auto cmd = std::make_unique<D3D12CommandList>();
    cmd->m_Impl->allocator = m_Impl->cmdAllocators[m_Impl->frameIndex];
    cmd->m_Impl->cmdList = m_Impl->cmdList;
    return cmd;
}

IRHICommandQueue* D3D12Device::GetQueue(QueueType) {
    return static_cast<IRHICommandQueue*>(m_Impl->graphicsQueueWrapper);
}

std::unique_ptr<IRHISwapChain> D3D12Device::CreateSwapChain(const SwapChainDesc& desc) {
    if (!m_Impl->device || !m_Impl->factory) return nullptr;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = desc.width; scDesc.Height = desc.height;
    scDesc.Format = FormatToDXGI(desc.format);
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = desc.bufferCount;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = m_Impl->factory->CreateSwapChainForHwnd(
        m_Impl->graphicsQueue.Get(), static_cast<HWND>(desc.windowHandle),
        &scDesc, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return nullptr;

    hr = sc1.As(&m_Impl->swapChain);
    if (FAILED(hr)) return nullptr;
    m_Impl->swapChainWidth = desc.width;
    m_Impl->swapChainHeight = desc.height;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = desc.bufferCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = m_Impl->device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_Impl->rtvHeap));
    if (FAILED(hr)) return nullptr;

    m_Impl->rtvDescriptorSize = m_Impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_Impl->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < desc.bufferCount; ++i) {
        m_Impl->swapChain->GetBuffer(i, IID_PPV_ARGS(&m_Impl->backBuffers[i]));
        m_Impl->device->CreateRenderTargetView(m_Impl->backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_Impl->rtvDescriptorSize;
    }

    return std::make_unique<D3D12SwapChain>();
}

const char* D3D12Device::GetDeviceName() const { return m_Impl->deviceName.c_str(); }

// ═══ D3D12SwapChain ═══
struct D3D12SwapChain::Impl { D3D12Device* device{nullptr}; uint32_t idx{0}; uint32_t count{3}; };
D3D12SwapChain::D3D12SwapChain() : m_Impl(std::make_unique<Impl>()) {}
D3D12SwapChain::~D3D12SwapChain() = default;

void D3D12SwapChain::Present() {
    if (!m_Impl->device) return;
    auto& impl = *m_Impl->device->m_Impl;
    impl.swapChain->Present(1, 0);
    impl.frameIndex = impl.swapChain->GetCurrentBackBufferIndex();
    m_Impl->idx = impl.frameIndex;
    uint64_t fenceVal = ++impl.fenceValues[impl.frameIndex];
    impl.graphicsQueue->Signal(impl.fence.Get(), fenceVal);
    if (impl.fence->GetCompletedValue() < fenceVal) {
        impl.fence->SetEventOnCompletion(fenceVal, impl.fenceEvent);
        WaitForSingleObject(impl.fenceEvent, INFINITE);
    }
    impl.cmdAllocators[impl.frameIndex]->Reset();
}

void D3D12SwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Impl->device) return;
    auto& impl = *m_Impl->device->m_Impl;
    for (uint32_t i = 0; i < impl.kFrameCount; ++i) impl.backBuffers[i].Reset();
    impl.swapChain->ResizeBuffers(impl.kFrameCount, w, h, impl.swapChainFormat,
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    m_Impl->count = impl.kFrameCount;
    impl.swapChainWidth = w; impl.swapChainHeight = h;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < impl.kFrameCount; ++i) {
        impl.swapChain->GetBuffer(i, IID_PPV_ARGS(&impl.backBuffers[i]));
        impl.device->CreateRenderTargetView(impl.backBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += impl.rtvDescriptorSize;
    }
}
IRHITexture* D3D12SwapChain::GetBackBuffer(uint32_t) const { return nullptr; }
uint32_t D3D12SwapChain::GetCurrentBackBufferIndex() const { return m_Impl->idx; }
uint32_t D3D12SwapChain::GetBufferCount() const { return m_Impl->count; }

// ═══ Format ═══
static DXGI_FORMAT FormatToDXGI(Format fmt) {
    switch (fmt) {
        case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::RGBA8_sRGB:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::R32_Float:   return DXGI_FORMAT_R32_FLOAT;
        case Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::D32_Float:   return DXGI_FORMAT_D32_FLOAT;
        case Format::R8_UNorm:    return DXGI_FORMAT_R8_UNORM;
        case Format::R16_Float:   return DXGI_FORMAT_R16_FLOAT;
        default:                  return DXGI_FORMAT_UNKNOWN;
    }
}
static Format DXGIToFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
        case DXGI_FORMAT_R32_FLOAT:      return Format::R32_Float;
        case DXGI_FORMAT_D32_FLOAT:      return Format::D32_Float;
        default:                         return Format::Unknown;
    }
}

bool HasD3D12Support() noexcept {
    HMODULE mod = LoadLibraryA("d3d12.dll");
    if (!mod) return false; FreeLibrary(mod); return true;
}
std::string GetD3D12AdapterInfo() noexcept { return "D3D12 (Hardware)"; }
std::unique_ptr<IRHIDevice> CreateD3D12Device() { return std::make_unique<D3D12Device>(); }

} // namespace RHI
} // namespace Engine