/**
 * @file D3D12Device.cpp
 * @brief D3D12 设备实现 — IRHIDevice 接口完整实现
 *
 * 策略：
 *   - 全局根签名 (Global Root Signature) — Space 0: 全局UBO, Space 1: 材质, Space 2: PushConstant
 *   - D3D12MA 管理显存
 *   - PSO 磁盘缓存 (ID3D12PipelineLibrary)
 *   - 强类型句柄 + SlotMap
 */

#include "Engine/Core/RHI/D3D12IRHIDevice.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/Log.h"

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

namespace Engine { namespace RHI {

static DXGI_FORMAT FormatToDXGI(Format fmt);
static Format DXGIToFormat(DXGI_FORMAT fmt);

// ── D3D12PipelineState with native handle ──
struct D3D12PipelineState::Impl {
    ComPtr<ID3D12PipelineState> pso;
    bool isCompute = false;
};
D3D12PipelineState::D3D12PipelineState() : m_Impl(std::make_unique<Impl>()) {}
D3D12PipelineState::~D3D12PipelineState() = default;

// ── D3D12Device::Impl ──
struct D3D12Device::Impl {
    ComPtr<ID3D12Device5> device;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12CommandQueue> graphicsQueue;
    ComPtr<ID3D12Debug> debugController;
    ComPtr<IDXGISwapChain3> swapChain;
    uint32_t swapChainWidth{0}, swapChainHeight{0};
    DXGI_FORMAT swapChainFormat{DXGI_FORMAT_B8G8R8A8_UNORM};
    uint32_t frameIndex{0};
    static constexpr uint32_t kFrameCount = 3;
    ComPtr<ID3D12Resource> backBuffers[kFrameCount];
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    uint32_t rtvDescriptorSize{0};
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValues[kFrameCount]{};
    HANDLE fenceEvent{nullptr};
    ComPtr<ID3D12CommandAllocator> cmdAllocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    D3D12MA::Allocator* vmaAllocator{nullptr};
    ComPtr<ID3D12PipelineLibrary> pipelineLibrary;
    std::string pipelineCachePath;
    static constexpr const char* kCacheFileName = "engine.psocache";

    // ── Global Root Signature (简化 PSO 创建) ──
    // Space 0: b0 = 全局UBO | Space 1: DescriptorTable(材质) | Space 2: PushConstants
    ComPtr<ID3D12RootSignature> globalRootSignature;

    D3D12Queue* graphicsQueueWrapper{nullptr};
    bool initialized{false};
    std::string deviceName;
    HWND hwnd{nullptr};

    // ── 方法声明 ──
    void LoadPipelineCache();
    void SavePipelineCache();
    bool CreateGlobalRootSignature();
};

// ── D3D12Queue ──
struct D3D12Queue::Impl { ComPtr<ID3D12CommandQueue> queue; QueueType type{QueueType::Graphics}; };
D3D12Queue::D3D12Queue() : m_Impl(std::make_unique<Impl>()) {}
D3D12Queue::~D3D12Queue() = default;
void D3D12Queue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    std::vector<ID3D12CommandList*> cmdLists;
    cmdLists.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        cmdLists.push_back(static_cast<D3D12CommandList*>(lists[i])->GetNativeCommandList());
    }
    m_Impl->queue->ExecuteCommandLists((UINT)cmdLists.size(), cmdLists.data());
}
void D3D12Queue::WaitIdle() { m_Impl->queue->Signal(m_Impl->queue.Get(), 0); }
QueueType D3D12Queue::GetType() const noexcept { return m_Impl->type; }

// ── D3D12CommandList ──
struct D3D12CommandList::Impl {
    ComPtr<ID3D12GraphicsCommandList6> cmdList;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12RootSignature> currentRootSig;
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
void D3D12CommandList::SetPipelineState(IRHIPipelineState* pso) {
    auto* d3dPso = static_cast<D3D12PipelineState*>(pso);
    if (d3dPso && d3dPso->m_Impl->pso) {
        m_Impl->cmdList->SetPipelineState(d3dPso->m_Impl->pso.Get());
    }
}
void D3D12CommandList::SetVertexBuffer(uint32 slot, IRHIBuffer* buf, uint32 stride, uint32 off) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buf);
    if (!d3dBuf) return;
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = d3dBuf->GetGPUAddress();
    view.StrideInBytes = stride;
    view.SizeInBytes = (UINT)d3dBuf->GetSize();
    m_Impl->cmdList->IASetVertexBuffers(slot, 1, &view);
}
void D3D12CommandList::SetIndexBuffer(IRHIBuffer* buf, uint32 offset) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buf);
    if (!d3dBuf) return;
    D3D12_INDEX_BUFFER_VIEW view = {};
    view.BufferLocation = d3dBuf->GetGPUAddress() + offset;
    view.SizeInBytes = (UINT)(d3dBuf->GetSize() - offset);
    view.Format = DXGI_FORMAT_R32_UINT;
    m_Impl->cmdList->IASetIndexBuffer(&view);
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
static D3D12_RESOURCE_STATES ResourceStateToD3D12(ResourceState state) {
    switch (state) {
        case ResourceState::RenderTarget:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::DepthStencil:   return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case ResourceState::ShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case ResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case ResourceState::CopySource:     return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case ResourceState::CopyDest:       return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::Present:        return D3D12_RESOURCE_STATE_PRESENT;
        default:                            return D3D12_RESOURCE_STATE_COMMON;
    }
}
void D3D12CommandList::ResourceBarrier(uint32 count, const ResourceBarrierDesc* barriers) {
    if (!m_Impl->cmdList || !barriers) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (barriers[i].type != ResourceBarrierDesc::Type::Transition) continue;
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = ResourceStateToD3D12(barriers[i].stateBefore);
        barrier.Transition.StateAfter = ResourceStateToD3D12(barriers[i].stateAfter);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        // 获取 D3D12 资源
        if (barriers[i].texture) {
            barrier.Transition.pResource = static_cast<ID3D12Resource*>(static_cast<D3D12Texture*>(barriers[i].texture)->GetNativeResource());
        } else if (barriers[i].buffer) {
            barrier.Transition.pResource = static_cast<ID3D12Resource*>(static_cast<D3D12Buffer*>(barriers[i].buffer)->GetNativeResource());
        }
        if (barrier.Transition.pResource)
            m_Impl->cmdList->ResourceBarrier(1, &barrier);
    }
}
void D3D12CommandList::SetConstantBuffer(uint32 set, uint32 binding, IRHIBuffer* buffer, uint64_t offset, uint64_t) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buffer);
    if (d3dBuf) m_Impl->cmdList->SetGraphicsRootConstantBufferView(set, d3dBuf->GetGPUAddress() + offset);
}
void D3D12CommandList::SetShaderResource(uint32, uint32, IRHITexture*) {}
void D3D12CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) { m_Impl->cmdList->Dispatch(gx, gy, gz); }
void D3D12CommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    auto* d3dBuf = static_cast<D3D12Buffer*>(buffer);
    if (d3dBuf && d3dBuf->GetResource())
        m_Impl->cmdList->SetComputeRootUnorderedAccessView(slot, d3dBuf->GetGPUAddress());
}
CommandListType D3D12CommandList::GetType() const noexcept { return CommandListType::Direct; }
ID3D12GraphicsCommandList6* D3D12CommandList::GetNativeCommandList() { return m_Impl->cmdList.Get(); }

// ── D3D12Buffer ──
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

// ── D3D12Texture ──
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
void* D3D12Texture::GetNativeResource() const { return m_Impl->resource.Get(); }

// ═══════════════════════════════════════════════════════════════════
// D3D12Device
// ═══════════════════════════════════════════════════════════════════

D3D12Device::D3D12Device() : m_Impl(std::make_unique<Impl>()) {}
D3D12Device::~D3D12Device() { Shutdown(); }

bool D3D12Device::Impl::CreateGlobalRootSignature() {
    // 定义全局根签名: Space 0 = 全局 UBO, Space 1 = 材质 Table, Space 2 = PushConstants
    CD3DX12_ROOT_PARAMETER params[3];
    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 1); // Space 1, t0-t7
    params[0].InitAsConstantBufferView(0, 0);                       // b0, Space 0
    params[1].InitAsDescriptorTable(1, &srvRange);                   // Space 1
    params[2].InitAsConstants(16, 1, 0);                             // b1, Space 0, 16 constants

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serialized, error;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
    if (!serialized) { Log::Error("[D3D12] Failed to serialize root signature"); return false; }
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&globalRootSignature));
    return globalRootSignature != nullptr;
}

bool D3D12Device::Initialize(void* windowHandle, uint32_t width, uint32_t height) {
    if (m_Impl->initialized) return true;
    m_Impl->hwnd = static_cast<HWND>(windowHandle);
    HRESULT hr;

#if defined(_DEBUG)
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_Impl->debugController))))
        m_Impl->debugController->EnableDebugLayer();
#endif

    hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_Impl->factory));
    if (FAILED(hr)) return false;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Impl->device));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC qDesc = {}; qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m_Impl->device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_Impl->graphicsQueue));
    if (FAILED(hr)) return false;

    D3D12MA::ALLOCATOR_DESC aDesc = {}; aDesc.pDevice = m_Impl->device.Get();
    D3D12MA::CreateAllocator(&aDesc, &m_Impl->vmaAllocator);

    for (uint32_t i = 0; i < Impl::kFrameCount; ++i)
        m_Impl->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Impl->cmdAllocators[i]));

    m_Impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Impl->cmdAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_Impl->cmdList));
    m_Impl->cmdList->Close();

    m_Impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Impl->fence));
    m_Impl->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Global Root Signature
    m_Impl->CreateGlobalRootSignature();

    // PSO Cache
    m_Impl->pipelineCachePath = fs::current_path().string() + "/" + Impl::kCacheFileName;
    m_Impl->LoadPipelineCache();

    if (windowHandle && width > 0 && height > 0) {
        SwapChainDesc sc; sc.width = width; sc.height = height; sc.format = Format::BGRA8_UNorm;
        sc.bufferCount = Impl::kFrameCount; sc.windowHandle = windowHandle;
        CreateSwapChain(sc);
    }

    m_Impl->graphicsQueueWrapper = new D3D12Queue();
    m_Impl->deviceName = "D3D12 (Hardware)";
    m_Impl->initialized = true;
    Log::Info("[D3D12] Device initialized (Global RootSig)");
    return true;
}

// ── PSO 磁盘缓存 ──
void D3D12Device::Impl::LoadPipelineCache() {
    std::ifstream file(pipelineCachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&pipelineLibrary)); return; }
    std::streamsize size = file.tellg(); file.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(size);
    if (file.read((char*)blob.data(), size)) {
        HRESULT hr = device->CreatePipelineLibrary(blob.data(), blob.size(), IID_PPV_ARGS(&pipelineLibrary));
        if (FAILED(hr)) device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&pipelineLibrary));
    }
}
void D3D12Device::Impl::SavePipelineCache() {
    if (!pipelineLibrary) return;
    ComPtr<ID3D12Blob> blob;
    if (SUCCEEDED(pipelineLibrary->Serialize(&blob)) && blob) {
        std::ofstream file(pipelineCachePath, std::ios::binary);
        file.write((const char*)blob->GetBufferPointer(), blob->GetBufferSize());
    }
}

// ── CreateGraphicsPSO ──
IRHIPipelineState* D3D12Device::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    uint64_t hash = desc.GetHash();
    { auto* c = PSOCache::Get().Find<GraphicsPSODesc>(hash); if (c) return c; }

    if (!m_Impl->globalRootSignature) { Log::Error("[D3D12] No global root signature"); return nullptr; }

    auto* pso = new D3D12PipelineState();
    pso->m_Impl->isCompute = false;

    // 使用全局根签名 + PSO 描述符
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_Impl->globalRootSignature.Get();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, 0, 0, 0, TRUE);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    // 从 PSOCache 尝试 PipelineLibrary 加载
    if (m_Impl->pipelineLibrary) {
        std::wstring name = L"pso_" + std::to_wstring(hash);
        HRESULT hr = m_Impl->pipelineLibrary->LoadGraphicsPipeline(name.c_str(), &psoDesc, IID_PPV_ARGS(&pso->m_Impl->pso));
        if (SUCCEEDED(hr)) { PSOCache::Get().Store<GraphicsPSODesc>(hash, pso); return pso; }
    }

    // 编译 PSO
    HRESULT hr = m_Impl->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso->m_Impl->pso));
    if (FAILED(hr)) { Log::Error("[D3D12] CreateGraphicsPipelineState failed"); delete pso; return nullptr; }

    // 存储到缓存
    if (m_Impl->pipelineLibrary) {
        std::wstring name = L"pso_" + std::to_wstring(hash);
        m_Impl->pipelineLibrary->StorePipeline(name.c_str(), pso->m_Impl->pso.Get());
    }
    PSOCache::Get().Store<GraphicsPSODesc>(hash, pso);
    return pso;
}

IRHIPipelineState* D3D12Device::CreateComputePSO(const ComputePSODesc& desc) {
    (void)desc;
    auto* pso = new D3D12PipelineState();
    pso->m_Impl->isCompute = true;
    return pso;
}

void D3D12Device::Shutdown() {
    if (!m_Impl->initialized) return;
    WaitIdle();
    m_Impl->SavePipelineCache();
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
    auto b = std::make_shared<D3D12Buffer>();
    if (!m_Impl->vmaAllocator) return nullptr;
    D3D12MA::ALLOCATION_DESC a = {}; a.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC r = {}; r.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    r.Width = desc.size; r.Height = 1; r.DepthOrArraySize = 1; r.MipLevels = 1;
    r.Format = DXGI_FORMAT_UNKNOWN; r.SampleDesc.Count = 1;
    r.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12MA::Allocation* alloc;
    if (FAILED(m_Impl->vmaAllocator->CreateResource(&a, &r, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &alloc, IID_NULL, nullptr)))
        return nullptr;
    b->m_Impl->allocation = alloc; b->m_Impl->resource = alloc->GetResource();
    b->m_Impl->size = desc.size; b->m_Impl->gpuAddress = alloc->GetResource()->GetGPUVirtualAddress();
    if (desc.initialData) { void* data; alloc->GetResource()->Map(0, nullptr, &data); memcpy(data, desc.initialData, desc.size); alloc->GetResource()->Unmap(0, nullptr); }
    return b;
}
std::shared_ptr<IRHITexture> D3D12Device::CreateTexture(const TextureDesc& desc) {
    auto t = std::make_shared<D3D12Texture>();
    if (!m_Impl->vmaAllocator) return nullptr;
    D3D12MA::ALLOCATION_DESC a = {}; a.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC r = {}; r.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    r.Width = desc.width; r.Height = desc.height; r.DepthOrArraySize = desc.depth;
    r.MipLevels = desc.mipLevels; r.Format = FormatToDXGI(desc.format);
    r.SampleDesc.Count = 1; r.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    r.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12MA::Allocation* alloc;
    if (FAILED(m_Impl->vmaAllocator->CreateResource(&a, &r, D3D12_RESOURCE_STATE_COMMON, nullptr, &alloc, IID_NULL, nullptr)))
        return nullptr;
    t->m_Impl->allocation = alloc; t->m_Impl->resource = alloc->GetResource();
    t->m_Impl->width = desc.width; t->m_Impl->height = desc.height; t->m_Impl->format = desc.format;
    return t;
}
std::unique_ptr<IRHICommandList> D3D12Device::CreateCommandList(CommandListType) {
    auto cmd = std::make_unique<D3D12CommandList>();
    cmd->m_Impl->allocator = m_Impl->cmdAllocators[m_Impl->frameIndex];
    cmd->m_Impl->cmdList = m_Impl->cmdList;
    return cmd;
}
IRHICommandQueue* D3D12Device::GetQueue(QueueType) { return static_cast<IRHICommandQueue*>(m_Impl->graphicsQueueWrapper); }

std::unique_ptr<IRHISwapChain> D3D12Device::CreateSwapChain(const SwapChainDesc& desc) {
    if (!m_Impl->device || !m_Impl->factory) return nullptr;
    DXGI_SWAP_CHAIN_DESC1 sc = {}; sc.Width = desc.width; sc.Height = desc.height;
    sc.Format = FormatToDXGI(desc.format); sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sc.BufferCount = desc.bufferCount;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_Impl->factory->CreateSwapChainForHwnd(m_Impl->graphicsQueue.Get(), static_cast<HWND>(desc.windowHandle), &sc, nullptr, nullptr, &sc1)))
        return nullptr;
    sc1.As(&m_Impl->swapChain);
    m_Impl->swapChainWidth = desc.width; m_Impl->swapChainHeight = desc.height;
    D3D12_DESCRIPTOR_HEAP_DESC rtv = {}; rtv.NumDescriptors = desc.bufferCount;
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    m_Impl->device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&m_Impl->rtvHeap));
    m_Impl->rtvDescriptorSize = m_Impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_Impl->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < desc.bufferCount; ++i) {
        m_Impl->swapChain->GetBuffer(i, IID_PPV_ARGS(&m_Impl->backBuffers[i]));
        m_Impl->device->CreateRenderTargetView(m_Impl->backBuffers[i].Get(), nullptr, h);
        h.ptr += m_Impl->rtvDescriptorSize;
    }
    return std::make_unique<D3D12SwapChain>();
}
const char* D3D12Device::GetDeviceName() const { return m_Impl->deviceName.c_str(); }

// ── D3D12SwapChain ──
struct D3D12SwapChain::Impl { D3D12Device* device{nullptr}; uint32_t idx{0}; uint32_t count{3}; };
D3D12SwapChain::D3D12SwapChain() : m_Impl(std::make_unique<Impl>()) {}
D3D12SwapChain::~D3D12SwapChain() = default;
void D3D12SwapChain::Present() {
    if (!m_Impl->device) return;
    auto& i = *m_Impl->device->m_Impl;
    i.swapChain->Present(1, 0); i.frameIndex = i.swapChain->GetCurrentBackBufferIndex();
    m_Impl->idx = i.frameIndex;
    uint64_t v = ++i.fenceValues[i.frameIndex]; i.graphicsQueue->Signal(i.fence.Get(), v);
    if (i.fence->GetCompletedValue() < v) { i.fence->SetEventOnCompletion(v, i.fenceEvent); WaitForSingleObject(i.fenceEvent, INFINITE); }
    i.cmdAllocators[i.frameIndex]->Reset();
}
void D3D12SwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Impl->device) return;
    auto& i = *m_Impl->device->m_Impl;
    for (uint32_t j = 0; j < i.kFrameCount; ++j) i.backBuffers[j].Reset();
    i.swapChain->ResizeBuffers(i.kFrameCount, w, h, i.swapChainFormat, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    m_Impl->count = i.kFrameCount; i.swapChainWidth = w; i.swapChainHeight = h;
    D3D12_CPU_DESCRIPTOR_HANDLE h = i.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t j = 0; j < i.kFrameCount; ++j) {
        i.swapChain->GetBuffer(j, IID_PPV_ARGS(&i.backBuffers[j]));
        i.device->CreateRenderTargetView(i.backBuffers[j].Get(), nullptr, h); h.ptr += i.rtvDescriptorSize;
    }
}
IRHITexture* D3D12SwapChain::GetBackBuffer(uint32_t idx) const { return nullptr; }
uint32_t D3D12SwapChain::GetCurrentBackBufferIndex() const { return m_Impl->idx; }
uint32_t D3D12SwapChain::GetBufferCount() const { return m_Impl->count; }

// ── Format ──
static DXGI_FORMAT FormatToDXGI(Format fmt) {
    switch (fmt) {
        case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::RGBA8_sRGB:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::R32_Float:   return DXGI_FORMAT_R32_FLOAT;
        case Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::D32_Float:   return DXGI_FORMAT_D32_FLOAT;
        default:                  return DXGI_FORMAT_UNKNOWN;
    }
}
static Format DXGIToFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
        default:                         return Format::Unknown;
    }
}

bool HasD3D12Support() noexcept { HMODULE m = LoadLibraryA("d3d12.dll"); if (!m) return false; FreeLibrary(m); return true; }
std::string GetD3D12AdapterInfo() noexcept { return "D3D12 (Hardware)"; }
std::unique_ptr<IRHIDevice> CreateD3D12Device() { return std::make_unique<D3D12Device>(); }

}} // namespace Engine::RHI