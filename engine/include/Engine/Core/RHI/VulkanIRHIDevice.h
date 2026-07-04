#pragma once

/**
 * @file VulkanIRHIDevice.h
 * @brief Vulkan 1.3+ 设备实现 — IRHIDevice 接口 + VMA 集成
 *
 * 设计要点：
 *   - 使用 Volk 元加载器 (volkInitialize/volkLoadDevice)
 *   - VMA 管理所有显存分配
 *   - 支持 Bindless Descriptor Indexing (VK_EXT_descriptor_indexing)
 *   - 支持 Timeline Semaphores (VK_KHR_timeline_semaphore)
 *   - 支持 Dynamic Rendering (VK_KHR_dynamic_rendering)
 *
 * 该文件定义了 Vulkan 后端的 IRHIDevice 实现类的接口。
 * 实际实现位于 engine/src/Vulkan/VulkanDevice.cpp
 */

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/BindlessDescriptor.h"
#include <memory>

namespace Engine {
namespace RHI {

    // ══════════════════════════════════════════════════════
    // Vulkan Device 实现
    // ══════════════════════════════════════════════════════
    class VulkanDevice final : public IRHIDevice {
    public:
        VulkanDevice();
        ~VulkanDevice() override;

        // ── IRHIDevice ──
        std::shared_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
        std::shared_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) override;
        IRHIPipelineState* CreateGraphicsPSO(const GraphicsPSODesc& desc) override;
        IRHIPipelineState* CreateComputePSO(const ComputePSODesc& desc) override;
        std::unique_ptr<IRHICommandList> CreateCommandList(CommandListType t) override;
        IRHICommandQueue* GetQueue(QueueType type) override;
        std::unique_ptr<IRHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        void WaitIdle() override;
        const char* GetDeviceName() const override;

        // ── Vulkan 专有 ──
        bool Initialize(void* windowHandle, uint32_t width, uint32_t height);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    /** 查询 Vulkan 扩展是否已注册 */
    bool HasVulkanSupport() noexcept;

    /** 获取可用的 Vulkan 实例和物理设备信息 */
    std::string GetVulkanDeviceInfo() noexcept;

    /** 创建 Vulkan 设备（工厂内部调用） */
    std::unique_ptr<IRHIDevice> CreateVulkanDevice();

    // ══════════════════════════════════════════════════════
    // Vulkan Command List 实现
    // ══════════════════════════════════════════════════════
    class VulkanCommandList final : public IRHICommandList {
    public:
        VulkanCommandList();
        ~VulkanCommandList() override;

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

    // ══════════════════════════════════════════════════════
    // Vulkan Buffer / Texture 实现
    // ══════════════════════════════════════════════════════
    class VulkanBuffer final : public IRHIBuffer {
    public:
        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;
    };

    class VulkanTexture final : public IRHITexture {
    public:
        uint32_t GetWidth()  const noexcept override;
        uint32_t GetHeight() const noexcept override;
        Format   GetFormat() const noexcept override;
    };

    class VulkanPipelineState final : public IRHIPipelineState {
    public:
        VulkanPipelineState() = default;
    };

    // ══════════════════════════════════════════════════════
    // Vulkan SwapChain 实现
    // ══════════════════════════════════════════════════════
    class VulkanSwapChain final : public IRHISwapChain {
    public:
        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;
    };

    // ══════════════════════════════════════════════════════
    // Vulkan Command Queue 实现
    // ══════════════════════════════════════════════════════
    class VulkanQueue final : public IRHICommandQueue {
    public:
        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
    };

} // namespace RHI
} // namespace Engine