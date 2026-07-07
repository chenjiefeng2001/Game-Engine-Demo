/**
 * @file D3D12Device.cpp
 * @brief D3D12 设备实现 — IRHIDevice 接口完整实现（包含全局 Shader-Visible Heap）
 *
 * 策略：
 *   - 全局根签名 (Global Root Signature) — Space 0: 全局UBO, Space 1: 纹理 Table, Space 2: PushConstant
 *   - 全局 Shader-Visible Descriptor Heap — 避免频繁 SetDescriptorHeaps()
 *   - D3D12MA 管理显存
 *   - PSO 磁盘缓存 (ID3D12PipelineLibrary)
 *   - 强类型句柄 + SlotMap
 */

#include "Engine/Core/RHI/D3D12IRHIDevice.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/Log.h"

#define D3D12MA_D3D12_HEADERS_ALONE 1
#include <d3d12.h>
#include <D3D12MemAlloc.h>
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

// ── D3D12PipelineState ──
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
    ComPtr<ID3D12CommandSignature> indirectDrawSignature; // 间接绘制命令签名
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
    ComPtr<ID3D12RootSignature> globalRootSignature;

    // ── SM 6.6 Bindless 描述符堆 ──
    static constexpr uint32_t kVisibleHeapSize = 65536;
    ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;       // Resource 可见堆
    ComPtr<ID3D12DescriptorHeap> samplerHeap;         // Sampler 可见堆 (独立于资源堆)
    uint32_t                     visibleHeapOffset{0};
    uint32_t                     cbvSrvUavDescriptorSize{0};
    uint32_t                     samplerDescriptorSize{0};
    bool                         supportsSM66{false}; // SM 6.6 能力标志

    // ── Compute 专用 ──
    ComPtr<ID3D12RootSignature> computeRootSignature;
    std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> computePSOCache;

    D3D12Queue* graphicsQueueWrapper{nullptr};
    bool initialized{false};
    std::string deviceName;
    HWND hwnd{nullptr};

    bool CreateComputeRootSignature();
    void LoadPipelineCache();
    void SavePipelineCache();
    bool CreateGlobalRootSignature();
    bool CreateShaderVisibleHeap();
};

// ── D3D12Queue ──
struct D3D12Queue::Impl {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue{0};
    HANDLE fenceEvent{nullptr};
    QueueType type{QueueType::Graphics};
};
D3D12Queue::D3D12Queue() : m_Impl(std::make_unique<Impl>()) {}
D3D12Queue::~D3D12Queue() {
    if (m_Impl->fenceEvent) CloseHandle(m_Impl->fenceEvent);
}
void D3D12Queue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    std::vector<ID3D12CommandList*> cmdLists;
    cmdLists.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        cmdLists.push_back(static_cast<D3D12CommandList*>(lists[i])->GetNativeCommandList());
    m_Impl->queue->ExecuteCommandLists((UINT)cmdLists.size(), cmdLists.data());
}
void D3D12Queue::WaitIdle() {
    if (!m_Impl->fence) return;
    if (!m_Impl->fenceEvent) {
        m_Impl->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }
    uint64_t val = ++m_Impl->fenceValue;
    m_Impl->queue->Signal(m_Impl->fence.Get(), val);
    if (m_Impl->fence->GetCompletedValue() < val) {
        m_Impl->fence->SetEventOnCompletion(val, m_Impl->fenceEvent);
        WaitForSingleObject(m_Impl->fenceEvent, INFINITE);
    }
}
QueueType D3D12Queue::GetType() const noexcept { return m_Impl->type; }

// ── D3D12CommandList ──
struct D3D12CommandList::Impl {
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12CommandAllocator> allocator;
    D3D12Device*     device{nullptr};
    ComPtr<ID3D12DescriptorHeap> visibleHeap;
    uint32_t*        heapOffset{nullptr};
    uint32_t         descriptorSize{0};
    bool             isRecording{false};
    ComPtr<ID3D12CommandSignature> indirectDrawSignature; // 懒初始化
};
D3D12CommandList::D3D12CommandList() : m_Impl(std::make_unique<Impl>()) {}
D3D12CommandList::~D3D12CommandList() = default;

void D3D12CommandList::Begin() {
    if (m_Impl->isRecording) return;
    if (!m_Impl->cmdList || !m_Impl->allocator) return;
    m_Impl->allocator->Reset();
    m_Impl->cmdList->Reset(m_Impl->allocator.Get(), nullptr);
    m_Impl->isRecording = true;

    // 绑定全局 Shader-Visible 描述符堆
    if (m_Impl->visibleHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_Impl->visibleHeap.Get() };
        m_Impl->cmdList->SetDescriptorHeaps(1, heaps);
    }
}
void D3D12CommandList::End() {
    if (!m_Impl->isRecording) return;
    m_Impl->cmdList->Close();
    m_Impl->isRecording = false;
}
void D3D12CommandList::Reset() { m_Impl->isRecording = false; }

void D3D12CommandList::SetPipelineState(IRHIPipelineState* pso) {
    auto* d3dPso = static_cast<D3D12PipelineState*>(pso);
    if (d3dPso && d3dPso->m_Impl->pso)
        m_Impl->cmdList->SetPipelineState(d3dPso->m_Impl->pso.Get());
}
void D3D12CommandList::SetVertexBuffer(uint32 slot, IRHIBuffer* buf, uint32 stride, uint32 off) {
    auto* d = static_cast<D3D12Buffer*>(buf); if (!d) return;
    D3D12_VERTEX_BUFFER_VIEW v = {}; v.BufferLocation = d->GetGPUAddress(); v.StrideInBytes = stride; v.SizeInBytes = (UINT)d->GetSize();
    m_Impl->cmdList->IASetVertexBuffers(slot, 1, &v);
}
void D3D12CommandList::SetIndexBuffer(IRHIBuffer* buf, uint32 offset) {
    auto* d = static_cast<D3D12Buffer*>(buf); if (!d) return;
    D3D12_INDEX_BUFFER_VIEW v = {}; v.BufferLocation = d->GetGPUAddress() + offset; v.SizeInBytes = (UINT)(d->GetSize() - offset); v.Format = DXGI_FORMAT_R32_UINT;
    m_Impl->cmdList->IASetIndexBuffer(&v);
}
void D3D12CommandList::SetPrimitiveTopology(PrimitiveTopology topo) {
    D3D_PRIMITIVE_TOPOLOGY t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topo) {
        case PrimitiveTopology::TriangleList: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case PrimitiveTopology::LineList:    t = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case PrimitiveTopology::PointList:   t = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case PrimitiveTopology::TriangleStrip: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    }
    m_Impl->cmdList->IASetPrimitiveTopology(t);
}
void D3D12CommandList::DrawIndexed(uint32 idxCount, uint32 startIdx, uint32 baseVtx) { m_Impl->cmdList->DrawIndexedInstanced(idxCount, 1, startIdx, baseVtx, 0); }
void D3D12CommandList::Draw(uint32 vtxCount, uint32 startVtx) { m_Impl->cmdList->DrawInstanced(vtxCount, 1, startVtx, 0); }
void D3D12CommandList::DrawIndexedIndirect(IRHIBuffer* buf, uint32 off) {
    auto* d = static_cast<D3D12Buffer*>(buf);
    if (!d || !m_Impl->cmdList) return;
    // 在 CommandList 级别创建命令签名（懒初始化，避免跨对象访问 private 成员）
    if (!m_Impl->indirectDrawSignature) {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        ID3D12Device* rawDevice = nullptr;
        m_Impl->cmdList->GetDevice(IID_PPV_ARGS(&rawDevice));
        if (rawDevice) {
            rawDevice->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&m_Impl->indirectDrawSignature));
            rawDevice->Release();
        }
    }
    if (m_Impl->indirectDrawSignature) {
        m_Impl->cmdList->ExecuteIndirect(m_Impl->indirectDrawSignature.Get(), 1, d->GetD3D12Resource(), off, nullptr, 0);
    }
}
void D3D12CommandList::SetViewport(const Viewport& vp) { D3D12_VIEWPORT v = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth }; m_Impl->cmdList->RSSetViewports(1, &v); }
void D3D12CommandList::SetScissorRect(const Rect& rect) { D3D12_RECT r = { (LONG)rect.x, (LONG)rect.y, (LONG)(rect.x + rect.width), (LONG)(rect.y + rect.height) }; m_Impl->cmdList->RSSetScissorRects(1, &r); }

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
void D3D12CommandList::ResourceBarrier(uint32 count, const ResourceBarrierDesc* b) {
    if (!m_Impl->cmdList || !b) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (b[i].type != ResourceBarrierDesc::Type::Transition) continue;
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = ResourceStateToD3D12(b[i].stateBefore);
        barrier.Transition.StateAfter = ResourceStateToD3D12(b[i].stateAfter);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (b[i].texture) barrier.Transition.pResource = static_cast<ID3D12Resource*>(((D3D12Texture*)b[i].texture)->GetNativeResource());
        else if (b[i].buffer) barrier.Transition.pResource = static_cast<ID3D12Resource*>(((D3D12Buffer*)b[i].buffer)->GetNativeResource());
        if (barrier.Transition.pResource) m_Impl->cmdList->ResourceBarrier(1, &barrier);
    }
}
void D3D12CommandList::SetConstantBuffer(uint32 set, uint32, IRHIBuffer* buffer, uint64_t offset, uint64_t) {
    auto* d = static_cast<D3D12Buffer*>(buffer);
    if (d) m_Impl->cmdList->SetGraphicsRootConstantBufferView(set, d->GetGPUAddress() + offset);
}

// ── SetShaderResource 实现：通过全局 Shader-Visible 堆绑定 SRV ──
void D3D12CommandList::SetShaderResource(uint32 set, uint32 binding, IRHITexture* texture) {
    if (!m_Impl->cmdList || !texture || !m_Impl->visibleHeap || !m_Impl->heapOffset) return;
    auto* d3dTex = static_cast<D3D12Texture*>(texture);

    // 在可见堆中分配槽位
    uint32_t slot = (*m_Impl->heapOffset)++;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_Impl->visibleHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += slot * m_Impl->descriptorSize;

    // 创建 SRV 描述符（使用纹理的全部 mip）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = FormatToDXGI(d3dTex->GetFormat());
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = -1;   // 全部 mip

    // 通过 SetGraphicsRootDescriptorTable 绑定到 Space 1 (Root Param 1)
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_Impl->visibleHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += slot * m_Impl->descriptorSize;
    m_Impl->cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);
}

void D3D12CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) { m_Impl->cmdList->Dispatch(gx, gy, gz); }
void D3D12CommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    auto* d = static_cast<D3D12Buffer*>(buffer);
    if (d && d->GetD3D12Resource()) m_Impl->cmdList->SetComputeRootUnorderedAccessView(slot, d->GetGPUAddress());
}
CommandListType D3D12CommandList::GetType() const noexcept { return CommandListType::Direct; }
ID3D12GraphicsCommandList* D3D12CommandList::GetNativeCommandList() { return m_Impl->cmdList.Get(); }

// ── D3D12Buffer ──
struct D3D12Buffer::Impl {
    ComPtr<ID3D12Resource> resource; D3D12MA::Allocation* allocation{nullptr};
    GPUAllocation gpuAlloc; uint64_t size{0}; D3D12_GPU_VIRTUAL_ADDRESS gpuAddress{0};
    ~Impl() { if (allocation) allocation->Release(); }
};
D3D12Buffer::D3D12Buffer() : m_Impl(std::make_unique<Impl>()) {}
D3D12Buffer::~D3D12Buffer() = default;
uint64_t D3D12Buffer::GetSize() const noexcept { return m_Impl->size; }
const GPUAllocation& D3D12Buffer::GetAllocation() const noexcept { return m_Impl->gpuAlloc; }
void* D3D12Buffer::GetNativeResource() const { return m_Impl->resource.Get(); }
ID3D12Resource* D3D12Buffer::GetD3D12Resource() const { return m_Impl->resource.Get(); }
uint64_t D3D12Buffer::GetGPUAddress() const { return m_Impl->gpuAddress; }

// ── D3D12Texture ──
struct D3D12Texture::Impl {
    ComPtr<ID3D12Resource> resource; D3D12MA::Allocation* allocation{nullptr};
    uint32_t width{0}, height{0}; Format format{Format::Unknown};
    ~Impl() { if (allocation) allocation->Release(); }
};
D3D12Texture::D3D12Texture() : m_Impl(std::make_unique<Impl>()) {}
D3D12Texture::~D3D12Texture() = default;
uint32_t D3D12Texture::GetWidth() const noexcept { return m_Impl->width; }
uint32_t D3D12Texture::GetHeight() const noexcept { return m_Impl->height; }
Format D3D12Texture::GetFormat() const noexcept { return m_Impl->format; }
void* D3D12Texture::GetNativeResource() const { return m_Impl->resource.Get(); }

// ── D3D12SwapChain ──
struct D3D12SwapChain::Impl { D3D12Device* device{nullptr}; uint32_t idx{0}; uint32_t count{3};
    std::vector<std::shared_ptr<D3D12Texture>> backBufferTextures; };
D3D12SwapChain::D3D12SwapChain() : m_Impl(std::make_unique<Impl>()) {}
D3D12SwapChain::~D3D12SwapChain() = default;

// ═══════════════════════════════════════════════════════════════════
// D3D12Device
// ═══════════════════════════════════════════════════════════════════

D3D12Device::D3D12Device() : m_Impl(std::make_unique<Impl>()) {}
D3D12Device::~D3D12Device() { Shutdown(); }

bool D3D12Device::Impl::CreateGlobalRootSignature() {
    // 手动构造根签名描述 — 避免依赖 d3dx12.h
    D3D12_ROOT_PARAMETER params[3] = {};
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 8;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 1;
    
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 1;
    params[2].Constants.RegisterSpace = 0;
    params[2].Constants.Num32BitValues = 16;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = params;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    
    ComPtr<ID3DBlob> serialized, error;
    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
    if (!serialized) return false;
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&globalRootSignature));
    return globalRootSignature != nullptr;
}

bool D3D12Device::Impl::CreateShaderVisibleHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = kVisibleHeapSize;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&cbvSrvUavHeap));
    if (FAILED(hr)) return false;
    cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
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

    // 枚举适配器并创建设备（同时保存适配器指针给 D3D12MA）
    ComPtr<IDXGIAdapter1> adapter;
    for (uint32_t i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> enumAdapter;
        if (m_Impl->factory->EnumAdapters1(i, &enumAdapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc;
        enumAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // 跳过软件适配器
        if (SUCCEEDED(D3D12CreateDevice(enumAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Impl->device)))) {
            adapter = enumAdapter;
            break;
        }
    }
    if (!m_Impl->device || !adapter) return false;

    D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m_Impl->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_Impl->graphicsQueue));
    if (FAILED(hr)) return false;

    D3D12MA::ALLOCATOR_DESC ad = {};
    ad.pDevice = m_Impl->device.Get();
    ad.pAdapter = adapter.Get();
    ad.PreferredBlockSize = 0; // 默认
    hr = D3D12MA::CreateAllocator(&ad, &m_Impl->vmaAllocator);
    if (FAILED(hr)) return false;

    for (uint32_t i = 0; i < Impl::kFrameCount; ++i)
        m_Impl->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Impl->cmdAllocators[i]));

    m_Impl->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Impl->cmdAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_Impl->cmdList));
    m_Impl->cmdList->Close();

    m_Impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Impl->fence));
    m_Impl->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    m_Impl->CreateGlobalRootSignature();
    m_Impl->CreateShaderVisibleHeap();

    m_Impl->pipelineCachePath = fs::current_path().string() + "/" + Impl::kCacheFileName;
    m_Impl->LoadPipelineCache();

    if (windowHandle && width > 0 && height > 0) {
        SwapChainDesc sc; sc.width = width; sc.height = height; sc.format = Format::BGRA8_UNorm;
        sc.bufferCount = Impl::kFrameCount; sc.windowHandle = windowHandle;
        CreateSwapChain(sc);
    }

    m_Impl->graphicsQueueWrapper = new D3D12Queue();
    // 将设备的队列和 fence 共享给 wrapper
    m_Impl->graphicsQueueWrapper->m_Impl->queue = m_Impl->graphicsQueue;
    // 为 wrapper 创建独立的 fence
    m_Impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Impl->graphicsQueueWrapper->m_Impl->fence));
    m_Impl->deviceName = "D3D12 (Hardware)";
    m_Impl->initialized = true;
    return true;
}

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
    // ID3D12PipelineLibrary::Serialize(v1): (void* blob, SIZE_T blobSize)
    // 使用试探法：先尝试 1MB buffer，若不够就按需增长
    constexpr SIZE_T kInitialSize = 1024 * 1024; // 1MB
    std::vector<uint8_t> buffer(kInitialSize);
    HRESULT hr = pipelineLibrary->Serialize(buffer.data(), buffer.size());
    if (SUCCEEDED(hr)) {
        std::ofstream file(pipelineCachePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        }
    }
    // 如果 Serialize 失败（buffer 太小），忽略缓存 — 下次启动重新构建
}

IRHIPipelineState* D3D12Device::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    uint64_t hash = desc.GetHash();
    { auto* c = PSOCache::Get().Find<GraphicsPSODesc>(hash); if (c) return c; }
    if (!m_Impl->globalRootSignature) return nullptr;
    auto* pso = new D3D12PipelineState();
    pso->m_Impl->isCompute = false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = m_Impl->globalRootSignature.Get();
    
    // 手动初始化 rasterizer state
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthBias = 0;
    pd.RasterizerState.DepthBiasClamp = 0.0f;
    pd.RasterizerState.SlopeScaledDepthBias = 0.0f;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.RasterizerState.MultisampleEnable = FALSE;
    pd.RasterizerState.AntialiasedLineEnable = FALSE;
    pd.RasterizerState.ForcedSampleCount = 0;
    pd.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    
    // 手动初始化 blend state (D3D12_DEFAULT)
    pd.BlendState.AlphaToCoverageEnable = FALSE;
    pd.BlendState.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < 8; ++i) {
        pd.BlendState.RenderTarget[i].BlendEnable = FALSE;
        pd.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
        pd.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
        pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    
    // 手动初始化 depth stencil state (D3D12_DEFAULT)
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    pd.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    pd.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pd.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pd.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    pd.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.SampleDesc.Count = 1;
    pd.NumRenderTargets = desc.rtvCount;
    for (uint32 i = 0; i < desc.rtvCount && i < 8; ++i) pd.RTVFormats[i] = FormatToDXGI(desc.rtvFormats[i]);
    if (pd.NumRenderTargets == 0) { pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; }
    if (m_Impl->pipelineLibrary) {
        std::wstring n = L"pso_" + std::to_wstring(hash);
        if (SUCCEEDED(m_Impl->pipelineLibrary->LoadGraphicsPipeline(n.c_str(), &pd, IID_PPV_ARGS(&pso->m_Impl->pso)))) {
            PSOCache::Get().Store<GraphicsPSODesc>(hash, pso); return pso;
        }
    }
    if (FAILED(m_Impl->device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso->m_Impl->pso)))) { delete pso; return nullptr; }
    if (m_Impl->pipelineLibrary) {
        std::wstring n = L"pso_" + std::to_wstring(hash);
        m_Impl->pipelineLibrary->StorePipeline(n.c_str(), pso->m_Impl->pso.Get());
    }
    PSOCache::Get().Store<GraphicsPSODesc>(hash, pso);
    return pso;
}

bool D3D12Device::Impl::CreateComputeRootSignature() {
    // 计算专用根签名: RootParam[0] = CBV(b0), RootParam[1] = UAV/DescriptorTable, RootParam[2] = PushConstants
    D3D12_ROOT_PARAMETER params[3] = {};
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 8;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 1;
    params[2].Constants.RegisterSpace = 0;
    params[2].Constants.Num32BitValues = 16;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = params;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error)))
        return false;
    if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature))))
        return false;
    return true;
}

IRHIPipelineState* D3D12Device::CreateComputePSO(const ComputePSODesc& desc) {
    uint64_t hash = desc.GetHash();
    
    // 检查缓存
    auto cacheIt = m_Impl->computePSOCache.find(hash);
    if (cacheIt != m_Impl->computePSOCache.end()) {
        auto* pso = new D3D12PipelineState();
        pso->m_Impl->isCompute = true;
        pso->m_Impl->pso = cacheIt->second;
        return pso;
    }

    // 确保计算根签名已创建
    if (!m_Impl->computeRootSignature) {
        if (!m_Impl->CreateComputeRootSignature()) {
            return nullptr;
        }
    }

    // 创建计算 PSO（需要从 PSOCache 获取着色器字节码）
    auto* cachedPSO = PSOCache::Get().Find<ComputePSODesc>(hash);
    if (cachedPSO) {
        auto* pso = new D3D12PipelineState();
        pso->m_Impl->isCompute = true;
        pso->m_Impl->pso = static_cast<D3D12PipelineState*>(cachedPSO)->m_Impl->pso;
        m_Impl->computePSOCache[hash] = pso->m_Impl->pso;
        return pso;
    }

    auto* pso = new D3D12PipelineState();
    pso->m_Impl->isCompute = true;
    // PSO 将在运行时通过 SetPipelineState 设置，此处仅创建包装器
    // 实际 D3D12_COMPUTE_PIPELINE_STATE_DESC 创建由外部 ShaderCompiler 提供
    PSOCache::Get().Store<ComputePSODesc>(hash, pso);
    return pso;
}

void D3D12Device::Shutdown() {
    if (!m_Impl->initialized) return;
    WaitIdle();
    m_Impl->SavePipelineCache();
    if (m_Impl->graphicsQueueWrapper) {
        delete m_Impl->graphicsQueueWrapper;
        m_Impl->graphicsQueueWrapper = nullptr;
    }
    if (m_Impl->vmaAllocator) m_Impl->vmaAllocator->Release();
    if (m_Impl->fenceEvent) CloseHandle(m_Impl->fenceEvent);
    m_Impl->initialized = false;
}
void D3D12Device::WaitIdle() {
    m_Impl->graphicsQueue->Signal(m_Impl->fence.Get(), ++m_Impl->fenceValues[m_Impl->frameIndex]);
    m_Impl->fence->SetEventOnCompletion(m_Impl->fenceValues[m_Impl->frameIndex], m_Impl->fenceEvent);
    WaitForSingleObject(m_Impl->fenceEvent, INFINITE);
}

std::shared_ptr<IRHIBuffer> D3D12Device::CreateBuffer(const RHIBufferDesc& desc) {
    auto b = std::make_shared<D3D12Buffer>();
    if (!m_Impl->vmaAllocator) return nullptr;
    D3D12MA::ALLOCATION_DESC a = {}; a.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC r = {}; r.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    r.Width = desc.size; r.Height = 1; r.DepthOrArraySize = 1; r.MipLevels = 1;
    r.Format = DXGI_FORMAT_UNKNOWN; r.SampleDesc.Count = 1; r.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
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
    cmd->m_Impl->visibleHeap = m_Impl->cbvSrvUavHeap;
    cmd->m_Impl->heapOffset = &m_Impl->visibleHeapOffset;
    cmd->m_Impl->descriptorSize = m_Impl->cbvSrvUavDescriptorSize;
    cmd->m_Impl->device = this;
    return cmd;
}
IRHICommandQueue* D3D12Device::GetQueue(QueueType type) { 
    return static_cast<IRHICommandQueue*>(m_Impl->graphicsQueueWrapper); 
}

std::unique_ptr<IRHISwapChain> D3D12Device::CreateSwapChain(const SwapChainDesc& desc) {
    if (!m_Impl->device || !m_Impl->factory) return nullptr;
    DXGI_SWAP_CHAIN_DESC1 sc = {}; sc.Width = desc.width; sc.Height = desc.height;
    sc.Format = FormatToDXGI(desc.format); sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sc.BufferCount = desc.bufferCount;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_Impl->factory->CreateSwapChainForHwnd(m_Impl->graphicsQueue.Get(), (HWND)desc.windowHandle, &sc, nullptr, nullptr, &sc1)))
        return nullptr;
    sc1.As(&m_Impl->swapChain);
    m_Impl->swapChainWidth = desc.width; m_Impl->swapChainHeight = desc.height;
    D3D12_DESCRIPTOR_HEAP_DESC rtv = {}; rtv.NumDescriptors = desc.bufferCount; rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    m_Impl->device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&m_Impl->rtvHeap));
    m_Impl->rtvDescriptorSize = m_Impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE hRtv = m_Impl->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < desc.bufferCount; ++i) {
        m_Impl->swapChain->GetBuffer(i, IID_PPV_ARGS(&m_Impl->backBuffers[i]));
        m_Impl->device->CreateRenderTargetView(m_Impl->backBuffers[i].Get(), nullptr, hRtv);
        hRtv.ptr += m_Impl->rtvDescriptorSize;
    }
    auto sw = std::make_unique<D3D12SwapChain>();
    sw->m_Impl->device = this;
    sw->m_Impl->count = desc.bufferCount;
    for (uint32_t i = 0; i < desc.bufferCount; ++i) {
        auto tex = std::make_shared<D3D12Texture>();
        tex->m_Impl->resource = m_Impl->backBuffers[i]; tex->m_Impl->width = desc.width;
        tex->m_Impl->height = desc.height; tex->m_Impl->format = desc.format;
        sw->m_Impl->backBufferTextures.push_back(tex);
    }
    return sw;
}
const char* D3D12Device::GetDeviceName() const { return m_Impl->deviceName.c_str(); }

// ── D3D12SwapChain methods ──
void D3D12SwapChain::Present() {
    if (!m_Impl->device) return;
    auto& d3dDevice = *m_Impl->device;
    auto& i = *d3dDevice.m_Impl;

    // 提交当前帧
    i.swapChain->Present(1, 0);

    // 获取交换链旋转后的新 BackBuffer 索引
    uint32_t newIdx = i.swapChain->GetCurrentBackBufferIndex();
    m_Impl->idx = newIdx;

    // 为【当前帧】触发 GPU 信号
    uint64_t signalVal = ++i.fenceValues[newIdx];
    i.graphicsQueue->Signal(i.fence.Get(), signalVal);

    // 等待【即将被覆盖的帧】完成（三缓冲循环等待）
    // 等待 (newIdx + 1) % kFrameCount 槽位的上一轮 GPU 工作
    uint32_t waitIdx = (newIdx + 1) % i.kFrameCount;
    uint64_t waitVal = i.fenceValues[waitIdx];
    if (i.fence->GetCompletedValue() < waitVal) {
        i.fence->SetEventOnCompletion(waitVal, i.fenceEvent);
        WaitForSingleObject(i.fenceEvent, INFINITE);
    }

    // 重置即将被录制的帧的命令分配器
    i.cmdAllocators[waitIdx]->Reset();

    // 更新帧索引到【即将被渲染】的帧
    i.frameIndex = waitIdx;
    i.visibleHeapOffset = 0;  // 重置可见堆偏移
}
void D3D12SwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Impl->device) return;
    auto& d3dDevice = *m_Impl->device;
    auto& i = *d3dDevice.m_Impl;
    for (uint32_t j = 0; j < i.kFrameCount; ++j) i.backBuffers[j].Reset();
    i.swapChain->ResizeBuffers(i.kFrameCount, w, h, i.swapChainFormat, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    m_Impl->count = i.kFrameCount;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = i.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t j = 0; j < i.kFrameCount; ++j) {
        i.swapChain->GetBuffer(j, IID_PPV_ARGS(&i.backBuffers[j]));
        i.device->CreateRenderTargetView(i.backBuffers[j].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += i.rtvDescriptorSize;
    }
}
IRHITexture* D3D12SwapChain::GetBackBuffer(uint32_t idx) const {
    return idx < m_Impl->backBufferTextures.size() ? m_Impl->backBufferTextures[idx].get() : nullptr;
}
uint32_t D3D12SwapChain::GetCurrentBackBufferIndex() const { return m_Impl->idx; }
uint32_t D3D12SwapChain::GetBufferCount() const { return m_Impl->count; }

// ── Format ──
static DXGI_FORMAT FormatToDXGI(Format fmt) {
    switch (fmt) {
        case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::R32_Float:   return DXGI_FORMAT_R32_FLOAT;
        case Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::D32_Float:   return DXGI_FORMAT_D32_FLOAT;
        default:                  return DXGI_FORMAT_UNKNOWN;
    }
}
bool HasD3D12Support() noexcept { HMODULE m = LoadLibraryA("d3d12.dll"); if (!m) return false; FreeLibrary(m); return true; }
std::string GetD3D12AdapterInfo() noexcept { return "D3D12 (Hardware)"; }
std::unique_ptr<IRHIDevice> CreateD3D12Device() { return std::make_unique<D3D12Device>(); }

}} // namespace Engine::RHI