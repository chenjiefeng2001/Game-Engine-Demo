/**
 * @file VulkanDevice.cpp
 * @brief Vulkan 1.3 设备实现 — VMA + Bindless + PushDescriptor
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Vulkan/VulkanLoader.h"
#include "Engine/Vulkan/VulkanFrameResource.h"
#include "Engine/Vulkan/VulkanPipelineLayoutCache.h"
#include "Engine/Vulkan/VulkanDeferredDeletion.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <mutex>
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>

#define VK_CHECK(x) do { VkResult _e = (x); if (_e) { std::fprintf(stderr, "[Vulkan] Error %d\n", _e); abort(); } } while(0)

namespace Engine {
namespace RHI {

IRHIPipelineState* CreateGraphicsPipeline(VkDevice, VkPipelineLayout, const GraphicsPSODesc&, VkShaderModule, VkShaderModule);
IRHIPipelineState* CreateComputePipeline(VkDevice, VkPipelineLayout, VkShaderModule);

struct VulkanDevice::Impl {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueIndex{UINT32_MAX};
    VmaAllocator vmaAllocator{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    std::vector<VkImage> swapChainImages;
    VulkanFrameContext frameContext;
    VulkanQueue* graphicsQueueWrapper{nullptr};
    VulkanPipelineLayoutCache* pipelineLayoutCache{nullptr};
    VulkanDeferredDeletion* deletionQueue{nullptr};
    uint32_t uboAlignment{256}; // 默认 256，初始化时从设备获取
    bool initialized{false};
    std::string deviceName;
    ~Impl() {
        delete pipelineLayoutCache;
        delete graphicsQueueWrapper;
        if (deletionQueue) {
            deletionQueue->FlushAll();
            delete deletionQueue;
        }
    }
};

struct VulkanQueue::Impl {
    VkQueue queue;
    QueueType type{QueueType::Graphics};

    // 帧同步支持（可选，由 SetFrameSync 设置后使用）
    VkSemaphore waitSemaphore{VK_NULL_HANDLE};
    VkSemaphore signalSemaphore{VK_NULL_HANDLE};
    VkFence     fence{VK_NULL_HANDLE};
};

VulkanQueue::VulkanQueue() : m_Impl(std::make_unique<Impl>()) {}
VulkanQueue::~VulkanQueue() = default;

void VulkanQueue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    std::vector<VkCommandBuffer> bufs(count);
    for (uint32 i = 0; i < count; ++i)
        bufs[i] = static_cast<VulkanCommandList*>(lists[i])->GetVkCommandBuffer();

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = count;
    si.pCommandBuffers = bufs.data();

    // 如果有等待信号量，则等待图像可用
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (m_Impl->waitSemaphore != VK_NULL_HANDLE) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_Impl->waitSemaphore;
        si.pWaitDstStageMask = &waitStage;
    }
    // 如果有信号信号量，则渲染完通知
    if (m_Impl->signalSemaphore != VK_NULL_HANDLE) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &m_Impl->signalSemaphore;
    }

    vkQueueSubmit(m_Impl->queue, 1, &si, m_Impl->fence);
}

void VulkanQueue::SetFrameSync(VkSemaphore wait, VkSemaphore signal, VkFence fence) noexcept {
    m_Impl->waitSemaphore = wait;
    m_Impl->signalSemaphore = signal;
    m_Impl->fence = fence;
}
void VulkanQueue::WaitIdle() { vkQueueWaitIdle(m_Impl->queue); }
QueueType VulkanQueue::GetType() const noexcept { return m_Impl->type; }

VulkanDevice::VulkanDevice() : m_Impl(std::make_unique<Impl>()) {}
VulkanDevice::~VulkanDevice() { Shutdown(); }

bool VulkanDevice::Initialize(void* windowHandle, uint32_t width, uint32_t height) {
    if (m_Impl->initialized) return true;
    if (!VulkanLoader::Initialize()) return false;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Engine"; app.apiVersion = VK_API_VERSION_1_3;

    // ── Instance 扩展：Surface 相关扩展仅在有窗口时启用 ──
    // 注意：VK_KHR_dynamic_rendering 是设备扩展，不是实例扩展，不在此处添加
    std::vector<const char*> instanceExts;
    if (windowHandle) {
        instanceExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExts.push_back("VK_KHR_win32_surface");
    }
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    ici.ppEnabledExtensionNames = instanceExts.data();
    if (vkCreateInstance(&ici, nullptr, &m_Impl->instance) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create instance\n");
        return false;
    }
    VulkanLoader::LoadInstance(m_Impl->instance);

    uint32_t devCnt = 0;
    vkEnumeratePhysicalDevices(m_Impl->instance, &devCnt, nullptr);
    if (devCnt == 0) return false;
    std::vector<VkPhysicalDevice> devs(devCnt);
    vkEnumeratePhysicalDevices(m_Impl->instance, &devCnt, devs.data());
    m_Impl->physicalDevice = devs[0];

    uint32_t qCnt = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_Impl->physicalDevice, &qCnt, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCnt);
    vkGetPhysicalDeviceQueueFamilyProperties(m_Impl->physicalDevice, &qCnt, qProps.data());
    for (uint32 i = 0; i < qCnt; ++i)
        if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) m_Impl->graphicsQueueIndex = i;

    float qp = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_Impl->graphicsQueueIndex; qci.queueCount = 1; qci.pQueuePriorities = &qp;

    // ── Device 扩展：仅在 Headless 模式下排除 Swapchain 扩展 ──
    std::vector<const char*> deviceExts = { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
    if (windowHandle) {
        deviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    VkPhysicalDeviceDynamicRenderingFeatures drf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    drf.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &drf; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    dci.ppEnabledExtensionNames = deviceExts.data();
    if (vkCreateDevice(m_Impl->physicalDevice, &dci, nullptr, &m_Impl->device) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create device\n");
        return false;
    }
    VulkanLoader::LoadDevice(m_Impl->device);

    vkGetDeviceQueue(m_Impl->device, m_Impl->graphicsQueueIndex, 0, &m_Impl->graphicsQueue);

    if (windowHandle) {
        VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        sci.hinstance = GetModuleHandle(nullptr); sci.hwnd = (HWND)windowHandle;
        vkCreateWin32SurfaceKHR(m_Impl->instance, &sci, nullptr, &m_Impl->surface);
    }

    VmaVulkanFunctions vf{};
    vf.vkGetInstanceProcAddr = vkGetInstanceProcAddr; vf.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo aci{}; aci.vulkanApiVersion = VK_API_VERSION_1_3;
    aci.physicalDevice = m_Impl->physicalDevice; aci.device = m_Impl->device;
    aci.instance = m_Impl->instance; aci.pVulkanFunctions = &vf;
    vmaCreateAllocator(&aci, &m_Impl->vmaAllocator);

    // 保存 GPU 硬件名称到 deviceName
    if (m_Impl->physicalDevice != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_Impl->physicalDevice, &props);
        m_Impl->deviceName = props.deviceName;
    }

    // 创建 Vulkan 延迟销毁队列（GPU 资源安全释放）
    m_Impl->deletionQueue = new VulkanDeferredDeletion(m_Impl->device);

    m_Impl->pipelineLayoutCache = new VulkanPipelineLayoutCache(this);
    m_Impl->graphicsQueueWrapper = new VulkanQueue();
    m_Impl->graphicsQueueWrapper->m_Impl->queue = m_Impl->graphicsQueue;
    m_Impl->graphicsQueueWrapper->m_Impl->type = QueueType::Graphics;

    for (uint32 i = 0; i < kMaxFramesInFlight; ++i) CreateFrameResource(m_Impl->frameContext.frames[i]);
    m_Impl->initialized = true;
    return true;
}

void VulkanDevice::CreateFrameResource(VulkanFrameResource& frame) {
    VkDevice dev = m_Impl->device;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(dev, &fci, nullptr, &frame.fence));
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(dev, &sci, nullptr, &frame.imageAvailable));
    VK_CHECK(vkCreateSemaphore(dev, &sci, nullptr, &frame.renderFinished));
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = m_Impl->graphicsQueueIndex; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(dev, &pci, nullptr, &frame.commandPool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = frame.commandPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(dev, &ai, &frame.commandBuffer));
}

void VulkanDevice::DestroyFrameResource(VulkanFrameResource& frame) {
    VkDevice dev = m_Impl->device;
    if (frame.fence) vkDestroyFence(dev, frame.fence, nullptr);
    if (frame.imageAvailable) vkDestroySemaphore(dev, frame.imageAvailable, nullptr);
    if (frame.renderFinished) vkDestroySemaphore(dev, frame.renderFinished, nullptr);
    if (frame.commandPool) vkDestroyCommandPool(dev, frame.commandPool, nullptr);
    if (frame.dynamicUBO) vmaDestroyBuffer(m_Impl->vmaAllocator, frame.dynamicUBO, frame.dynamicUBOAlloc);
}

void VulkanDevice::Shutdown() {
    if (!m_Impl->initialized) return;
    vkDeviceWaitIdle(m_Impl->device);
    // 清理延迟销毁队列中的待删资源
    if (m_Impl->deletionQueue) {
        m_Impl->deletionQueue->FlushAll();
        delete m_Impl->deletionQueue;
        m_Impl->deletionQueue = nullptr;
    }
    // 清理全局 PSO 缓存（确保没有资源引用残留）
    PSOCache::Get().Clear();
    for (uint32 i = 0; i < kMaxFramesInFlight; ++i) DestroyFrameResource(m_Impl->frameContext.frames[i]);
    if (m_Impl->swapChain) vkDestroySwapchainKHR(m_Impl->device, m_Impl->swapChain, nullptr);
    if (m_Impl->surface) vkDestroySurfaceKHR(m_Impl->instance, m_Impl->surface, nullptr);
    // VMA 要求：销毁前所有由它分配的资源必须已释放
    if (m_Impl->vmaAllocator) vmaDestroyAllocator(m_Impl->vmaAllocator);
    m_Impl->vmaAllocator = VK_NULL_HANDLE;
    if (m_Impl->device) vkDestroyDevice(m_Impl->device, nullptr);
    if (m_Impl->instance) vkDestroyInstance(m_Impl->instance, nullptr);
    m_Impl->initialized = false;
}

std::shared_ptr<IRHIBuffer> VulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    auto buf = std::make_shared<VulkanBuffer>();
    if (!m_Impl || !m_Impl->vmaAllocator) {
        std::fprintf(stderr, "[Vulkan] CreateBuffer called before Initialize()\n");
        return buf;
    }
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = desc.size; bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaAllocationCreateInfo ai{}; ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    VkBuffer vkBuf; VmaAllocation alloc; VmaAllocationInfo ari;
    if (vmaCreateBuffer(m_Impl->vmaAllocator, &bi, &ai, &vkBuf, &alloc, &ari) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] CreateBuffer failed\n");
        return buf;
    }
    buf->SetVkBuffer(vkBuf); buf->SetSize(desc.size);
    buf->SetAllocator(m_Impl->vmaAllocator); // VMA RAII 析构需要
    buf->SetVmaAllocation(alloc); // 【核心修复】保存 VmaAllocation 句柄给 RAII 析构
    GPUAllocation ga; ga.mappedPtr = ari.pMappedData; buf->SetAllocation(ga);
    if (desc.initialData && ari.pMappedData) std::memcpy(ari.pMappedData, desc.initialData, desc.size);
    return buf;
}

std::shared_ptr<IRHITexture> VulkanDevice::CreateTexture(const TextureDesc& desc) {
    auto tex = std::make_shared<VulkanTexture>();
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = FormatToVk(desc.format);
    ii.extent = {desc.width, desc.height, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImage vkImg; VmaAllocation alloc;
    vmaCreateImage(m_Impl->vmaAllocator, &ii, nullptr, &vkImg, &alloc, nullptr);
    tex->SetVkImage(vkImg); tex->SetWidth(desc.width); tex->SetHeight(desc.height); tex->SetFormat(desc.format);
    tex->SetAllocator(m_Impl->vmaAllocator); // VMA RAII 析构需要
    tex->SetAllocation(alloc);
    return tex;
}

IRHIPipelineState* VulkanDevice::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    uint64_t hash = desc.GetHash();
    if (auto* hit = PSOCache::Get().Find<GraphicsPSODesc>(hash)) return hit;
    VkPipelineLayout layout; VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_Impl->device, &plCI, nullptr, &layout);
    auto* pso = CreateGraphicsPipeline(m_Impl->device, layout, desc, VK_NULL_HANDLE, VK_NULL_HANDLE);
    if (pso) PSOCache::Get().Store<GraphicsPSODesc>(hash, pso);
    return pso;
}

IRHIPipelineState* VulkanDevice::CreateComputePSO(const ComputePSODesc& desc) {
    VkPipelineLayout layout; VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_Impl->device, &plCI, nullptr, &layout);
    return CreateComputePipeline(m_Impl->device, layout, VK_NULL_HANDLE);
}

std::unique_ptr<IRHICommandList> VulkanDevice::CreateCommandList(CommandListType) {
    auto cmd = std::make_unique<VulkanCommandList>();
    auto& fr = m_Impl->frameContext.frames[m_Impl->frameContext.currentFrame];
    cmd->SetDevice(this); cmd->SetVkCommandBuffer(fr.commandBuffer);
    return cmd;
}

IRHICommandQueue* VulkanDevice::GetQueue(QueueType) { return m_Impl->graphicsQueueWrapper; }

std::unique_ptr<IRHISwapChain> VulkanDevice::CreateSwapChain(const SwapChainDesc& desc) {
    if (!m_Impl->surface) return nullptr;
    uint32_t fmtCnt; vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &fmtCnt, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCnt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &fmtCnt, fmts.data());
    auto selFmt = fmts[0];
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Impl->physicalDevice, m_Impl->surface, &caps);
    uint32_t imgCnt = std::max(caps.minImageCount, std::min(desc.bufferCount, caps.maxImageCount));
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = m_Impl->surface; sci.minImageCount = imgCnt;
    sci.imageFormat = selFmt.format; sci.imageColorSpace = selFmt.colorSpace;
    sci.imageExtent = {desc.width, desc.height}; sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.preTransform = caps.currentTransform; sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_Impl->device, &sci, nullptr, &m_Impl->swapChain) != VK_SUCCESS) return nullptr;
    uint32_t actualCnt; vkGetSwapchainImagesKHR(m_Impl->device, m_Impl->swapChain, &actualCnt, nullptr);
    m_Impl->swapChainImages.resize(actualCnt);
    vkGetSwapchainImagesKHR(m_Impl->device, m_Impl->swapChain, &actualCnt, m_Impl->swapChainImages.data());
    auto sc = std::make_unique<VulkanSwapChain>();
    sc->SetDevice(this); sc->SetSwapChain(m_Impl->swapChain);
    sc->SetSwapChainImages(m_Impl->swapChainImages);
    for (auto vkImg : m_Impl->swapChainImages) {
        auto tex = std::make_shared<VulkanTexture>();
        tex->SetVkImage(vkImg); tex->SetWidth(desc.width); tex->SetHeight(desc.height);
        // 初始布局：交换链图像创建后处于 PRESENT_SRC_KHR
        tex->SetLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        sc->AddBackBuffer(tex);
    }
    return sc;
}

void VulkanDevice::WaitIdle() { if (m_Impl->device) vkDeviceWaitIdle(m_Impl->device); }
const char* VulkanDevice::GetDeviceName() const { return m_Impl->deviceName.c_str(); }
VkDevice VulkanDevice::GetVkDevice() const noexcept { return m_Impl->device; }
VmaAllocator VulkanDevice::GetVmaAllocator() const noexcept { return m_Impl->vmaAllocator; }
VkPhysicalDevice VulkanDevice::GetVkPhysicalDevice() const noexcept { return m_Impl->physicalDevice; }
VkInstance VulkanDevice::GetVkInstance() const noexcept { return m_Impl->instance; }
VkQueue VulkanDevice::GetGraphicsQueue() const noexcept { return m_Impl->graphicsQueue; }
uint32_t VulkanDevice::GetGraphicsQueueIndex() const noexcept { return m_Impl->graphicsQueueIndex; }
VulkanFrameContext& VulkanDevice::GetFrameContext() noexcept { return m_Impl->frameContext; }
uint32_t VulkanDevice::GetUBOAlignment() const noexcept { return m_Impl->uboAlignment; }
VulkanDeferredDeletion* VulkanDevice::GetDeletionQueue() const noexcept { return m_Impl->deletionQueue; }
VkCommandPool VulkanDevice::GetOrCreateThreadCommandPool() { return VK_NULL_HANDLE; }
void VulkanDevice::ResetAllThreadCommandPools() {}

bool HasVulkanSupport() noexcept { HMODULE m = LoadLibraryA("vulkan-1.dll"); if (!m) return false; FreeLibrary(m); return true; }
std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3"; }
std::unique_ptr<IRHIDevice> CreateVulkanDevice() {
    auto dev = std::make_unique<VulkanDevice>();
    dev->Initialize(nullptr, 0, 0);
    return dev;
}

} // namespace RHI
} // namespace Engine