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

// 前向声明 Vulkan 类型
struct VkDevice_T;
struct VmaAllocator_T;
union VkQueue_T;
union VkInstance_T;
union VkPhysicalDevice_T;

namespace Engine {
namespace RHI {

    // 前向声明 Vulkan 帧资源
    struct VulkanFrameContext;

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

        // ── Vulkan 内部查询 ──
        VkDevice_T*     GetVkDevice() const noexcept;
        VmaAllocator_T* GetVmaAllocator() const noexcept;
        VkPhysicalDevice_T* GetVkPhysicalDevice() const noexcept;
        VkInstance_T*   GetVkInstance() const noexcept;
        VkQueue_T*      GetGraphicsQueue() const noexcept;
        uint32_t        GetGraphicsQueueIndex() const noexcept;
        VulkanFrameContext& GetFrameContext() noexcept;

        // ── 线程命令池 ──
        VkCommandPool_T* GetOrCreateThreadCommandPool();
        void            ResetAllThreadCommandPools();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        // 内部辅助
        void CreateFrameResource(struct VulkanFrameResource& frame);
        void DestroyFrameResource(struct VulkanFrameResource& frame);
        void Shutdown();
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

        // ── Vulkan 专有 ──
        VkCommandBuffer_T* GetVkCommandBuffer() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ══════════════════════════════════════════════════════
    // Vulkan Buffer / Texture / PipelineState 实现
    // ══════════════════════════════════════════════════════
    class VulkanBuffer final : public IRHIBuffer {
    public:
        VulkanBuffer();
        ~VulkanBuffer() override;

        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;

        // ── Vulkan 专有 ──
        VkBuffer_T* GetVkBuffer() const noexcept;
        void SetAllocation(const GPUAllocation& alloc);
        void SetVkBuffer(VkBuffer_T* buffer);
        void SetSize(uint64_t size);

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

        // ── Vulkan 专有 ──
        VkImage_T* GetVkImage() const noexcept;
        void SetVkImage(VkImage_T* image);
        void SetWidth(uint32_t w);
        void SetHeight(uint32_t h);
        void SetFormat(Format fmt);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class VulkanPipelineState final : public IRHIPipelineState {
    public:
        VulkanPipelineState();
        ~VulkanPipelineState() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ══════════════════════════════════════════════════════
    // Vulkan SwapChain 实现
    // ══════════════════════════════════════════════════════
    class VulkanSwapChain final : public IRHISwapChain {
    public:
        VulkanSwapChain();
        ~VulkanSwapChain() override;

        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ══════════════════════════════════════════════════════
    // Vulkan Command Queue 实现
    // ══════════════════════════════════════════════════════
    class VulkanQueue final : public IRHICommandQueue {
    public:
        VulkanQueue();
        ~VulkanQueue() override;

        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;

        // ── Vulkan 专有 ──
        void SetVkQueue(VkQueue_T* queue) noexcept;
        void SetDevice(VulkanDevice* device) noexcept;
        void SetType(QueueType type) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

} // namespace RHI
} // namespace Engine