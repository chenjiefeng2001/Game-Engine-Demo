#pragma once

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/BindlessDescriptor.h"
#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Vulkan/VulkanFrameResource.h"
#include "vk_mem_alloc.h"
#include <memory>

namespace Engine {
namespace RHI {

class VulkanDeferredDeletion;


    class VulkanDevice final : public IRHIDevice {
    public:
        VulkanDevice();
        ~VulkanDevice() override;

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

        VkDevice        GetVkDevice() const noexcept;
        VmaAllocator    GetVmaAllocator() const noexcept;
        VkPhysicalDevice GetVkPhysicalDevice() const noexcept;
        VkInstance      GetVkInstance() const noexcept;
        VkQueue         GetGraphicsQueue() const noexcept;
        uint32_t        GetGraphicsQueueIndex() const noexcept;
        VulkanFrameContext& GetFrameContext() noexcept;
        uint32_t        GetUBOAlignment() const noexcept;
        VulkanDeferredDeletion* GetDeletionQueue() const noexcept;

        VkCommandPool GetOrCreateThreadCommandPool();
        void            ResetAllThreadCommandPools();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        void CreateFrameResource(VulkanFrameResource& frame);
        void DestroyFrameResource(VulkanFrameResource& frame);
        void Shutdown();

        friend class VulkanQueue;
        friend class VulkanSwapChain;
    };

    bool HasVulkanSupport() noexcept;
    std::string GetVulkanDeviceInfo() noexcept;
    std::unique_ptr<IRHIDevice> CreateVulkanDevice();

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
        void SetConstantBuffer(uint32 set, uint32 binding, IRHIBuffer* buffer, uint64_t offset, uint64_t size) override;
        void SetShaderResource(uint32 set, uint32 binding, IRHITexture* texture) override;
        void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
        void SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) override;
        CommandListType GetType() const noexcept override;

        VkCommandBuffer GetVkCommandBuffer() const noexcept;
        void SetVkCommandBuffer(VkCommandBuffer cmdBuf) noexcept;
        void SetVkPipelineState(VkPipeline pipeline, VkPipelineLayout layout) noexcept;
        void SetDevice(VulkanDevice* device) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class VulkanDevice;
    };

    class VulkanBuffer final : public IRHIBuffer {
    public:
        VulkanBuffer();
        ~VulkanBuffer() override;
        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;
        VkBuffer GetVkBuffer() const noexcept;
        void SetAllocation(const GPUAllocation& alloc);
        void SetVkBuffer(VkBuffer buffer);
        void SetSize(uint64_t size);
        void SetAllocator(VmaAllocator allocator);
        void SetVmaAllocation(VmaAllocation alloc);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class VulkanTexture final : public IRHITexture {
    public:
        VulkanTexture();
        ~VulkanTexture() override;
        uint32_t GetWidth()  const noexcept override;
        uint32_t GetHeight() const noexcept override;
        Format   GetFormat() const noexcept override;
        VkImage GetVkImage() const noexcept;
        void SetVkImage(VkImage image);
        void SetWidth(uint32_t w);
        void SetHeight(uint32_t h);
        void SetFormat(Format fmt);
        void SetAllocator(VmaAllocator allocator);
        void SetAllocation(VmaAllocation alloc);
        // 布局追踪（用于 ResourceBarrier 的正确 oldLayout）
        void SetLayout(VkImageLayout layout) noexcept;
        VkImageLayout GetLayout() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class VulkanDevice;
    };

    class VulkanPipelineState final : public IRHIPipelineState {
    public:
        VulkanPipelineState();
        ~VulkanPipelineState() override;
        VkPipeline GetVkPipeline() const noexcept;
        VkPipelineLayout GetVkPipelineLayout() const noexcept;
        bool IsCompute() const noexcept;
        void SetNativeHandles(VkPipeline pipeline, VkPipelineLayout layout, VkDevice device) noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class VulkanSwapChain final : public IRHISwapChain {
    public:
        VulkanSwapChain();
        ~VulkanSwapChain() override;
        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;
        void SetDevice(VulkanDevice* dev) { m_Device = dev; }
        void SetSwapChain(VkSwapchainKHR sc) { m_SwapChain = sc; }
        void SetSwapChainImages(const std::vector<VkImage>& images);
        void AddBackBuffer(std::shared_ptr<VulkanTexture> tex) { m_BackBuffers.push_back(tex); }
        VulkanDevice* GetDevice() const { return m_Device; }
        VkSwapchainKHR GetSwapChain() const { return m_SwapChain; }
    private:
        VulkanDevice* m_Device = nullptr;
        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<VulkanTexture>> m_BackBuffers;
        std::vector<VkImage> m_SwapChainImages;
        uint32_t m_ImageIndex = 0;
        friend class VulkanDevice;
    };

    class VulkanQueue final : public IRHICommandQueue {
    public:
        VulkanQueue();
        ~VulkanQueue() override;
        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
        void SetVkQueue(VkQueue queue) noexcept;
        void SetDevice(VulkanDevice* device) noexcept;
        void SetType(QueueType type) noexcept;
        // 帧同步：设置 WaitSemaphore/SignalSemaphore/Fence 用于 Present 同步
        void SetFrameSync(VkSemaphore wait, VkSemaphore signal, VkFence fence) noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        friend class VulkanDevice;
    };

} // namespace RHI
} // namespace Engine