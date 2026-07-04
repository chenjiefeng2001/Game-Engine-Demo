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
        const char* GetDeviceName() const override;

        bool Initialize(void* windowHandle, uint32_t width, uint32_t height);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
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
        CommandListType GetType() const noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class D3D12Buffer final : public IRHIBuffer {
    public:
        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;
    };

    class D3D12Texture final : public IRHITexture {
    public:
        uint32_t GetWidth()  const noexcept override;
        uint32_t GetHeight() const noexcept override;
        Format   GetFormat() const noexcept override;
    };

    class D3D12PipelineState final : public IRHIPipelineState {};
    class D3D12SwapChain final : public IRHISwapChain {
    public:
        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;
    };

    class D3D12Queue final : public IRHICommandQueue {
    public:
        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
    };

} // namespace RHI
} // namespace Engine