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
        std::shared_ptr<IRHIPipelineState> CreateGraphicsPSO(const GraphicsPSODesc& desc) override;
        std::shared_ptr<IRHIPipelineState> CreateComputePSO(const ComputePSODesc& desc) override;
        std::unique_ptr<IRHICommandList> CreateCommandList(CommandListType t) override;
        IRHICommandQueue* GetQueue(QueueType type) override;
        std::unique_ptr<IRHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        void WaitIdle() override;
        const char* GetDeviceName() const override;

        bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
    /**
     * 提供原生窗口句柄。
     *
     * 必须**在 Initialize 之前**调用，否则 surface 不会创建。
     *
     * IRHIDevice::Initialize 的 windowHandle 是单个 void*，无法同时表达 X11 的
     * Display 与 Wayland 的 wl_surface，因此这里额外接收显示连接。
     * 在 Linux 上这是唯一可行路径：RHIWindow 的 GetHWND() 显式返回 nullptr，
     * 所以 windowHandle 会是空的。
     */
    void SetNativeWindow(const Engine::NativeWindow& win) noexcept;
        /** 与 SetNativeWindow 成对：Initialize 会用参数覆盖其中的 window 字段，
         *  调用方常需要取回最终生效的句柄。 */
        const Engine::NativeWindow& GetNativeWindow() const noexcept;
        /**
         * 从设备描述符池分配一个描述符集。
         *
         * 命令列表在绑定 uniform buffer / 纹理时调用。此前描述符池根本不存在，
         * 也没有任何分配入口，SetConstantBuffer 因此永远写不进东西。
         */
        VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout) noexcept;
        /** 设备共享采样器（线性过滤 + 钳制边缘）。组合式图像采样描述符需要它，
         *  而此前引擎里根本没有任何采样器。 */
        VkSampler GetSampler() const noexcept;
        /**
         * 回读任意 RGBA8 纹理（图像 → 暂存缓冲 → CPU）。
         *
         * 此前引擎只能读回 VulkanOffscreenTarget 自带的那一张，于是「渲染到自建
         * 纹理再验证」这类路径根本做不了 —— 深度测试、离屏渲染的验证都卡在这里。
         * 回读后图像被交还到 COLOR_ATTACHMENT_OPTIMAL，可继续使用。
         */
        bool ReadbackTexture(IRHITexture* texture, std::vector<uint8_t>& out) noexcept;

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
          void Submit() override;
          void Reset() override;
        void BeginRendering(const RenderTargetDesc& desc) override;
        void EndRendering() override;
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
          /**
           * 挂到一个**调用方已开始录制**的命令缓冲上。
           *
           * 引擎里存在两种互不相容的录制所有权模型：VulkanCommandList 由调用方
           * Begin()/End()，而 VulkanOffscreenTarget::BeginOneShot() 返回的缓冲已经
           * 处于录制状态。对后者再调 Begin() 会得到
           * VUID-vkBeginCommandBuffer-commandBuffer-00049。此入口显式声明「我接手一个
           * 已在录制的缓冲」，End() 仍会正确地结束它。
           */
          void AttachToRecording(VkCommandBuffer cmdBuf) noexcept;

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
        /** 持久映射指针。CPU 可访问的用途（CPU_To_GPU / GPU_To_CPU / CPU_Only）
         * 在 CreateBuffer 里用 VMA 显式映射，此处必须透出，否则 RHI 侧拿到的
         * 顶点缓冲虽已正确上传，读写接口却恒为 nullptr。GPU_ONLY 返回 nullptr。 */
        void* GetMappedPtr() const noexcept override;
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
    /** 附件视图。渲染作用域（vkCmdBeginRendering）必须绑定 image view 而非 image，
     *  此前只有 image 的 getter，导致无法从 IRHITexture* 构造渲染附件。 */
    VkImageView GetVkImageView() const noexcept;
    void SetVkImageView(VkImageView view);
    /** 析构时销毁 view 需要 VkDevice。此前 view 无人销毁（析构里留了空块），
     *  于是每个带 view 的纹理都会泄漏一个 image view。 */
    void SetVkDevice(VkDevice device);
        void SetWidth(uint32_t w);
        void SetHeight(uint32_t h);
        void SetFormat(Format fmt);
        void SetAllocator(VmaAllocator allocator);
        void SetAllocation(VmaAllocation alloc);
        // 布局追踪（用于 ResourceBarrier 的正确 oldLayout）
        void SetLayout(VkImageLayout layout) noexcept;
        VkImageLayout GetLayout() const noexcept;
        /** 供共享的附件准备助手就地改写布局（它需要按引用回写）。
         *  返回引用而不是拷贝，是为了让 VulkanCommandList 与 VulkanOffscreenTarget
         *  走同一份「声明 + 转换」逻辑而不必各自再 SetLayout 一次。 */
        VkImageLayout& GetLayoutRef() noexcept { return GetLayoutRefImpl(); }
        VkImageLayout& GetLayoutRefImpl() noexcept;
        /**
         * 是否为交换链图像。
         *
         * EndRendering 必须知道目标是不是可呈现图像：普通渲染目标留在
         * COLOR_ATTACHMENT_OPTIMAL 即可，而交换链图像必须转到
         * VK_IMAGE_LAYOUT_PRESENT_SRC_KHR，否则 vkQueuePresentKHR 违反
         * VUID-VkPresentInfoKHR-pImageIndices-01430。这两者在 IRHITexture
         * 接口上没有区别，所以由 VulkanDevice::CreateSwapChain 标记。
         */
        void SetIsSwapChainImage(bool value) noexcept;
        bool IsSwapChainImage() const noexcept;
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
        /** 本 PSO 的描述符 set 布局（无描述符时为 VK_NULL_HANDLE）。 */
        void SetDescriptorSetLayout(VkDescriptorSetLayout layout) noexcept;
        VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class VulkanSwapChain final : public IRHISwapChain {
    public:
        VulkanSwapChain();
        ~VulkanSwapChain() override;
        /**
         * 显式获取下一个交换链图像，返回索引；失败返回 UINT32_MAX。
         *
         * 必须**在录制之前**调用：录制时要绑定哪个 backbuffer 取决于本次 acquire
         * 的结果。此前 Present() 内部才 acquire，而它提交的是调用方**事先**录好的
         * 命令缓冲，于是录制时目标 backbuffer 尚未确定 —— 该时序无法正确工作。
         */
        uint32_t AcquireNextImage();
        /** 提交本帧已录制的命令并呈现。不再内部 acquire。 */
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
        bool     m_ImageAcquired = false;
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