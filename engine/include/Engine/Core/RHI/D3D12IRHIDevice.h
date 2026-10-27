#pragma once

/**
 * @file D3D12IRHIDevice.h
 * @brief Direct3D 12 设备实现 — IRHIDevice 接口 + D3D12MA 集成
 *
 * 设计要点：
 *   - D3D12MA (D3D12 Memory Allocator) 管理所有显存分配
 *   - 根签名 (Root Signature) 由 PSOCache 自动推导
 *   - 描述符堆 (Descriptor Heap) 按 Bindless 模式组织
 *   - 支持 Enhanced Barriers (D3D12 Enhanced Barriers) 自动转换
 *   - 支持 GPU-Based Validation (GBV) 调试
 */

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/ShaderReflection.h"
#include <d3d12.h>
#include <memory>

namespace Engine {
namespace RHI {

    /** 查询 D3D12 运行时是否可用 */
    bool HasD3D12Support() noexcept;

    /** 创建 D3D12 设备 */
    std::unique_ptr<IRHIDevice> CreateD3D12Device();

    /** 获取 D3D12 适配器信息 */
    std::string GetD3D12AdapterInfo() noexcept;

    // ══════════════════════════════════════════════════════
    // D3D12 Device
    // ══════════════════════════════════════════════════════
    class D3D12Device final : public IRHIDevice {
    public:
        D3D12Device();
        ~D3D12Device() override;

        std::shared_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
        std::shared_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) override;
        IRHIPipelineState* CreateGraphicsPSO(const GraphicsPSODesc& desc) override;
        IRHIPipelineState* CreateComputePSO(const ComputePSODesc& desc) override;
        std::unique_ptr<IRHICommandList> CreateCommandList(CommandListType t) override;
        IRHICommandQueue* GetQueue(QueueType type) override;
        std::unique_ptr<IRHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        void WaitIdle() override;
        void Shutdown();
        const char* GetDeviceName() const override;

        bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
        /** 获取全局根签名（D3D12CommandList::Begin 需要绑定） */
        ID3D12RootSignature* GetGlobalRootSignature() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12SwapChain;
    };

    // ══════════════════════════════════════════════════════
    // D3D12 Command List
    // ══════════════════════════════════════════════════════
    class D3D12CommandList final : public IRHICommandList {
    public:
        D3D12CommandList();
        ~D3D12CommandList() override;

        void Begin() override;
        void End() override;
        void Reset() override;
        void SetPipelineState(IRHIPipelineState* pso) override;
        void SetVertexBuffer(uint32 slot, IRHIBuffer* buf, uint32 stride, uint32 off) override;
        void SetIndexBuffer(IRHIBuffer* buf, uint32 off) override;
        void SetPrimitiveTopology(PrimitiveTopology topo) override;
        void DrawIndexed(uint32 idxCount, uint32 startIdx, uint32 baseVtx) override;
        void Draw(uint32 vtxCount, uint32 startVtx) override;
        void DrawIndexedIndirect(IRHIBuffer* argsBuf, uint32 off) override;
        void SetViewport(const Viewport& vp) override;
        void SetScissorRect(const Rect& rect) override;
        void ResourceBarrier(uint32 count, const ResourceBarrierDesc* barriers) override;
        void SetConstantBuffer(uint32 set, uint32 binding, IRHIBuffer* buffer, uint64_t offset, uint64_t size) override;
        void SetShaderResource(uint32 set, uint32 binding, IRHITexture* texture) override;
        void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
        void SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) override;
        CommandListType GetType() const noexcept override;
        ID3D12GraphicsCommandList* GetNativeCommandList();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
    };

    class D3D12Buffer final : public IRHIBuffer {
    public:
        D3D12Buffer();
        ~D3D12Buffer() override;
        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;
        void* GetNativeResource() const;
        ID3D12Resource* GetD3D12Resource() const;
        uint64_t GetGPUAddress() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
    };

    class D3D12Texture final : public IRHITexture {
    public:
        D3D12Texture();
        ~D3D12Texture() override;
        uint32_t GetWidth()  const noexcept override;
        uint32_t GetHeight() const noexcept override;
        Format   GetFormat() const noexcept override;
        void* GetNativeResource() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
    };

    class D3D12PipelineState final : public IRHIPipelineState {
    public:
        D3D12PipelineState();
        ~D3D12PipelineState() override;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
        friend class D3D12CommandList;
    };
    class D3D12SwapChain final : public IRHISwapChain {
    public:
        D3D12SwapChain();
        ~D3D12SwapChain() override;
        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
    };

    class D3D12Queue final : public IRHICommandQueue {
    public:
        D3D12Queue();
        ~D3D12Queue() override;
        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class D3D12Device;
    };

} // namespace RHI
} // namespace Engine