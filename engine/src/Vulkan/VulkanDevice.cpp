/**
 * @file VulkanDevice.cpp
 * @brief Vulkan 设备实现 — IRHIDevice 接口完整实现
 *
 * 实现流程：
 *   Initialize():
 *     1. Volk 初始化
 *     2. 创建 VkInstance（含 Validation Layer）
 *     3. 选择 VkPhysicalDevice
 *     4. 创建 VkDevice（含队列 + 扩展）
 *     5. 初始化 VMA
 *     6. 获取队列
 *     7. 创建 FrameResource（3 套资源）
 *     8. 创建交换链
 */

#include "Engine/Vulkan/VulkanLoader.h"
#include "Engine/Vulkan/VulkanFrameResource.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/RHI/GPUAllocation.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <set>
#include <mutex>

#include <windows.h>

namespace Engine {
namespace RHI {

// 前向声明：在 VulkanPipelineState.cpp 中实现的工厂函数
IRHIPipelineState* CreateGraphicsPipeline(
    VkDevice device, VkPipelineLayout pipelineLayout,
    const GraphicsPSODesc& desc,
    VkShaderModule vsModule, VkShaderModule fsModule);

IRHIPipelineState* CreateComputePipeline(
    VkDevice device, VkPipelineLayout pipelineLayout, VkShaderModule csModule);

// ════════════════════════════════════════════════════════════
// 内部结构 — Pimpl
// ════════════════════════════════════════════════════════════

struct VulkanDevice::Impl {
    // ── 核心 Vulkan 对象 ──
    VkInstance           instance{VK_NULL_HANDLE};
    VkPhysicalDevice     physicalDevice{VK_NULL_HANDLE};
    VkDevice             device{VK_NULL_HANDLE};

    // ── 队列 ──
    VkQueue              graphicsQueue{VK_NULL_HANDLE};
    VkQueue              computeQueue{VK_NULL_HANDLE};
    VkQueue              transferQueue{VK_NULL_HANDLE};
    uint32_t             graphicsQueueIndex{UINT32_MAX};
    uint32_t             computeQueueIndex{UINT32_MAX};
    uint32_t             transferQueueIndex{UINT32_MAX};

    // ── VMA ──
    VmaAllocator         vmaAllocator{VK_NULL_HANDLE};

    // ── 交换链 ──
    VkSwapchainKHR       swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR         surface{VK_NULL_HANDLE};
    uint32_t             swapChainWidth{0};
    uint32_t             swapChainHeight{0};
    VkFormat             swapChainFormat{VK_FORMAT_B8G8R8A8_UNORM};

    // ── 交换链图像 ──
    std::vector<VkImage> swapChainImages;
    std::vector<std::shared_ptr<VulkanTexture>> swapChainTextures;

    // ── 帧资源 ──
    VulkanFrameContext   frameContext;

    // ── 调试 ──
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};

    // ── 线程命令池 ──
    std::unordered_map<std::thread::id, VkCommandPool> threadCmdPools;
    std::mutex threadPoolMutex;

    // ── 队列包装 ──
    VulkanQueue*         graphicsQueueWrapper{nullptr};
    VulkanQueue*         computeQueueWrapper{nullptr};
    VulkanQueue*         transferQueueWrapper{nullptr};

    // ── Pipeline Layout 缓存 ──
    VulkanPipelineLayoutCache* pipelineLayoutCache{nullptr};

    // ── Bindless 描述符 ──
    BindlessAllocator* bindlessAllocator{nullptr};

    // ── 初始化标志 ──
    bool                 initialized{false};

    // ── 设备名称 ──
    std::string          deviceName;

    // ── 窗口句柄 ──
    void*                windowHandle{nullptr};

    ~Impl() {
        delete pipelineLayoutCache;
        delete graphicsQueueWrapper;
        delete computeQueueWrapper;
        delete transferQueueWrapper;
    }
};

// ════════════════════════════════════════════════════════════
// VulkanQueue 实现
// ════════════════════════════════════════════════════════════

struct VulkanQueue::Impl {
    VkQueue queue{VK_NULL_HANDLE};
    VulkanDevice* device{nullptr};
    QueueType type{QueueType::Graphics};
};

VulkanQueue::VulkanQueue() : m_Impl(std::make_unique<Impl>()) {}
VulkanQueue::~VulkanQueue() = default;

void VulkanQueue::ExecuteCommandLists(uint32 count, IRHICommandList** lists) {
    std::vector<VkCommandBuffer> cmdBuffers;
    cmdBuffers.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* vkCmdList = static_cast<VulkanCommandList*>(lists[i]);
        cmdBuffers.push_back(vkCmdList->GetVkCommandBuffer());
    }
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = (uint32_t)cmdBuffers.size();
    submitInfo.pCommandBuffers = cmdBuffers.data();
    vkQueueSubmit(m_Impl->queue, 1, &submitInfo, VK_NULL_HANDLE);
}

void VulkanQueue::WaitIdle() { vkQueueWaitIdle(m_Impl->queue); }

QueueType VulkanQueue::GetType() const noexcept { return m_Impl->type; }

// ════════════════════════════════════════════════════════════
// VulkanDevice 构造函数/析构函数
// ════════════════════════════════════════════════════════════

VulkanDevice::VulkanDevice() : m_Impl(std::make_unique<Impl>()) {}
VulkanDevice::~VulkanDevice() { Shutdown(); }

// ════════════════════════════════════════════════════════════
// 内部辅助函数
// ════════════════════════════════════════════════════════════

namespace {

static VKAPI_ATTR VkBool32 VKAPI_CALL
DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT,
                       const VkDebugUtilsMessengerCallbackDataEXT* data,
                       void*) {
    const char* severityStr = "Info";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) severityStr = "ERROR";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) severityStr = "WARNING";
    std::fprintf(stderr, "[Vulkan %s] %s\n", severityStr, data ? data->pMessage : "null");
#ifdef _DEBUG
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) __debugbreak();
#endif
    return VK_FALSE;
}

VkInstance CreateVulkanInstance(bool enableValidation) {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "GameEngineDemo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "GameEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = GetRequiredInstanceExtensions(enableValidation);
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (enableValidation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    if (enableValidation) {
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugMessengerCallback;
        createInfo.pNext = &debugCreateInfo;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanDevice] Failed to create instance: %d\n", (int)result);
        return VK_NULL_HANDLE;
    }
    return instance;
}

bool PickPhysicalDevice(VkInstance instance, VkPhysicalDevice& outDevice,
                        uint32_t& outGfxQueueIndex, uint32_t& outComputeQueueIndex,
                        uint32_t& outTransferQueueIndex) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) { std::fprintf(stderr, "[VulkanDevice] No Vulkan-capable GPUs found\n"); return false; }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    int bestScore = -1;
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, queueFamilies.data());

        uint32_t gfxIdx = UINT32_MAX, cmpIdx = UINT32_MAX, trfIdx = UINT32_MAX;
        for (uint32_t q = 0; q < queueFamilyCount; ++q) {
            if (queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) { if (gfxIdx == UINT32_MAX) gfxIdx = q; }
            if (queueFamilies[q].queueFlags & VK_QUEUE_COMPUTE_BIT) { if (cmpIdx == UINT32_MAX) cmpIdx = q; }
            if (queueFamilies[q].queueFlags & VK_QUEUE_TRANSFER_BIT) { if (trfIdx == UINT32_MAX) trfIdx = q; }
        }
        if (gfxIdx == UINT32_MAX) continue;

        int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100 : 0)) + (int)props.limits.maxImageDimension2D;
        if (score > bestScore) {
            bestScore = score; outDevice = devices[i]; outGfxQueueIndex = gfxIdx;
            outComputeQueueIndex = cmpIdx; outTransferQueueIndex = trfIdx;
        }
    }
    if (bestScore < 0) { std::fprintf(stderr, "[VulkanDevice] No suitable GPU found\n"); return false; }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(outDevice, &props);
    std::fprintf(stdout, "[VulkanDevice] Selected GPU: %s\n", props.deviceName);
    return true;
}

VkDevice CreateLogicalDevice(VkPhysicalDevice physicalDevice,
                             uint32_t gfxQIndex, uint32_t cmpQIndex, uint32_t trfQIndex) {
    std::set<uint32_t> uniqueQueueFamilies = {gfxQIndex};
    if (cmpQIndex != UINT32_MAX) uniqueQueueFamilies.insert(cmpQIndex);
    if (trfQIndex != UINT32_MAX) uniqueQueueFamilies.insert(trfQIndex);

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t family : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(qci);
    }

    auto deviceExtensions = GetRequiredDeviceExtensions();

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    descriptorIndexingFeatures.pNext = &dynamicRenderingFeatures;
    descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features13.pNext = &descriptorIndexingFeatures;
    features13.dynamicRendering = VK_TRUE; features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.pNext = &features13;
    features12.descriptorIndexing = VK_TRUE; features12.bufferDeviceAddress = VK_TRUE; features12.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features12;
    createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] Failed to create logical device: %d\n", (int)result); return VK_NULL_HANDLE; }
    return device;
}

VkSurfaceKHR CreateSurface(VkInstance instance, void* windowHandle) {
    if (!windowHandle) return VK_NULL_HANDLE;
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandle(nullptr); sci.hwnd = static_cast<HWND>(windowHandle);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] Failed to create Win32 surface\n"); return VK_NULL_HANDLE; }
    return surface;
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════
// VulkanDevice::Initialize
// ════════════════════════════════════════════════════════════

bool VulkanDevice::Initialize(void* windowHandle, uint32_t width, uint32_t height) {
    if (m_Impl->initialized) return true;

    if (!VulkanLoader::Initialize()) { std::fprintf(stderr, "[VulkanDevice] Volk initialization failed\n"); return false; }
    m_Impl->windowHandle = windowHandle;

#ifdef _DEBUG
    bool enableValidation = true;
#else
    bool enableValidation = false;
#endif

    m_Impl->instance = CreateVulkanInstance(enableValidation);
    if (m_Impl->instance == VK_NULL_HANDLE) return false;
    VulkanLoader::LoadInstance(m_Impl->instance);

    if (enableValidation) {
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Impl->instance, "vkCreateDebugUtilsMessengerEXT");
        if (vkCreateDebugUtilsMessengerEXT) {
            VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            ci.pfnUserCallback = DebugMessengerCallback;
            vkCreateDebugUtilsMessengerEXT(m_Impl->instance, &ci, nullptr, &m_Impl->debugMessenger);
        }
    }

    if (!PickPhysicalDevice(m_Impl->instance, m_Impl->physicalDevice,
                            m_Impl->graphicsQueueIndex, m_Impl->computeQueueIndex, m_Impl->transferQueueIndex))
        return false;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_Impl->physicalDevice, &props);
    m_Impl->deviceName = props.deviceName;

    m_Impl->device = CreateLogicalDevice(m_Impl->physicalDevice, m_Impl->graphicsQueueIndex,
                                          m_Impl->computeQueueIndex, m_Impl->transferQueueIndex);
    if (m_Impl->device == VK_NULL_HANDLE) return false;
    VulkanLoader::LoadDevice(m_Impl->device);

    vkGetDeviceQueue(m_Impl->device, m_Impl->graphicsQueueIndex, 0, &m_Impl->graphicsQueue);
    if (m_Impl->computeQueueIndex != UINT32_MAX) vkGetDeviceQueue(m_Impl->device, m_Impl->computeQueueIndex, 0, &m_Impl->computeQueue);
    if (m_Impl->transferQueueIndex != UINT32_MAX) vkGetDeviceQueue(m_Impl->device, m_Impl->transferQueueIndex, 0, &m_Impl->transferQueue);

#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (windowHandle) m_Impl->surface = CreateSurface(m_Impl->instance, windowHandle);
#endif

    VmaVulkanFunctions vmaFuncs{};
    vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocInfo.physicalDevice = m_Impl->physicalDevice;
    allocInfo.device = m_Impl->device;
    allocInfo.instance = m_Impl->instance;
    allocInfo.pVulkanFunctions = &vmaFuncs;
    VkResult vmaResult = vmaCreateAllocator(&allocInfo, &m_Impl->vmaAllocator);
    if (vmaResult != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] VMA init failed\n"); return false; }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) CreateFrameResource(m_Impl->frameContext.frames[i]);

    if (windowHandle && width > 0 && height > 0) {
        SwapChainDesc scDesc{}; scDesc.width = width; scDesc.height = height;
        scDesc.format = Format::BGRA8_UNorm; scDesc.bufferCount = 3; scDesc.vSync = true; scDesc.windowHandle = windowHandle;
        auto swapChain = CreateSwapChain(scDesc);
        if (!swapChain) { std::fprintf(stderr, "[VulkanDevice] Swap chain creation failed\n"); return false; }
        // 保存交换链纹理引用
        for (uint32_t i = 0; i < swapChain->GetBufferCount(); ++i) {
            // 交换链纹理已在 VulkanSwapChain 内部管理
        }
    }

    // 创建队列包装
    if (!m_Impl->graphicsQueueWrapper) {
        m_Impl->graphicsQueueWrapper = new VulkanQueue();
        m_Impl->graphicsQueueWrapper->m_Impl->queue = m_Impl->graphicsQueue;
        m_Impl->graphicsQueueWrapper->m_Impl->device = this;
        m_Impl->graphicsQueueWrapper->m_Impl->type = QueueType::Graphics;
    }
    if (!m_Impl->computeQueueWrapper && m_Impl->computeQueue != VK_NULL_HANDLE) {
        m_Impl->computeQueueWrapper = new VulkanQueue();
        m_Impl->computeQueueWrapper->m_Impl->queue = m_Impl->computeQueue;
        m_Impl->computeQueueWrapper->m_Impl->device = this;
        m_Impl->computeQueueWrapper->m_Impl->type = QueueType::Compute;
    }

    // Pipeline Layout 缓存
    m_Impl->pipelineLayoutCache = new VulkanPipelineLayoutCache(this);

    m_Impl->initialized = true;
    return true;
}

// ════════════════════════════════════════════════════════════
// 帧资源创建/销毁
// ════════════════════════════════════════════════════════════

void VulkanDevice::CreateFrameResource(VulkanFrameResource& frame) {
    VkDevice dev = m_Impl->device;

    VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(dev, &fenceCI, nullptr, &frame.fence));

    VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(dev, &semCI, nullptr, &frame.imageAvailable));
    VK_CHECK(vkCreateSemaphore(dev, &semCI, nullptr, &frame.renderFinished));

    VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCI.queueFamilyIndex = m_Impl->graphicsQueueIndex;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(dev, &poolCI, nullptr, &frame.commandPool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = frame.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(dev, &allocInfo, &frame.commandBuffer));

    VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufCI.size = kDynamicUBOSizePerFrame;
    bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo vmaAllocCI{};
    vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    vmaAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocResult;
    VkResult res = vmaCreateBuffer(m_Impl->vmaAllocator, &bufCI, &vmaAllocCI, &frame.dynamicUBO, &frame.dynamicUBOAlloc, &allocResult);
    VK_CHECK(res);
    frame.dynamicUBOMapped = allocResult.pMappedData;
    frame.dynamicUBOOffset = 0;

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorCount},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxDescriptorCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorCount},
    };
    VkDescriptorPoolCreateInfo poolCreateCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCreateCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCreateCI.maxSets = kMaxDescriptorCount;
    poolCreateCI.poolSizeCount = 3;
    poolCreateCI.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(dev, &poolCreateCI, nullptr, &frame.descriptorPool);
}

void VulkanDevice::DestroyFrameResource(VulkanFrameResource& frame) {
    VkDevice dev = m_Impl->device;
    if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(dev, frame.fence, nullptr);
    if (frame.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(dev, frame.imageAvailable, nullptr);
    if (frame.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(dev, frame.renderFinished, nullptr);
    if (frame.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(dev, frame.commandPool, nullptr);
    if (frame.dynamicUBO != VK_NULL_HANDLE) vmaDestroyBuffer(m_Impl->vmaAllocator, frame.dynamicUBO, frame.dynamicUBOAlloc);
    if (frame.descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, frame.descriptorPool, nullptr);
}

void VulkanDevice::Shutdown() {
    if (!m_Impl->initialized) return;
    if (m_Impl->device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_Impl->device);

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) DestroyFrameResource(m_Impl->frameContext.frames[i]);

    {
        std::lock_guard<std::mutex> lock(m_Impl->threadPoolMutex);
        for (auto& [id, pool] : m_Impl->threadCmdPools) vkDestroyCommandPool(m_Impl->device, pool, nullptr);
        m_Impl->threadCmdPools.clear();
    }

    if (m_Impl->swapChain != VK_NULL_HANDLE) vkDestroySwapchainKHR(m_Impl->device, m_Impl->swapChain, nullptr);
    if (m_Impl->surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_Impl->instance, m_Impl->surface, nullptr);
    if (m_Impl->vmaAllocator != VK_NULL_HANDLE) vmaDestroyAllocator(m_Impl->vmaAllocator);

    if (m_Impl->debugMessenger != VK_NULL_HANDLE) {
        auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Impl->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(m_Impl->instance, m_Impl->debugMessenger, nullptr);
    }
    if (m_Impl->device != VK_NULL_HANDLE) vkDestroyDevice(m_Impl->device, nullptr);
    if (m_Impl->instance != VK_NULL_HANDLE) vkDestroyInstance(m_Impl->instance, nullptr);
    m_Impl->initialized = false;
}

VkCommandPool VulkanDevice::GetOrCreateThreadCommandPool() {
    auto id = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(m_Impl->threadPoolMutex);
    auto it = m_Impl->threadCmdPools.find(id);
    if (it != m_Impl->threadCmdPools.end()) return it->second;

    VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCI.queueFamilyIndex = m_Impl->graphicsQueueIndex;
    poolCI.flags = kThreadCmdPoolFlags;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkResult res = vkCreateCommandPool(m_Impl->device, &poolCI, nullptr, &pool);
    if (res != VK_SUCCESS) return VK_NULL_HANDLE;
    m_Impl->threadCmdPools[id] = pool;
    return pool;
}

void VulkanDevice::ResetAllThreadCommandPools() {
    std::lock_guard<std::mutex> lock(m_Impl->threadPoolMutex);
    for (auto& [id, pool] : m_Impl->threadCmdPools) vkResetCommandPool(m_Impl->device, pool, 0);
}

// ════════════════════════════════════════════════════════════
// IRHIDevice 接口实现
// ════════════════════════════════════════════════════════════

std::shared_ptr<IRHIBuffer> VulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    auto buffer = std::make_shared<VulkanBuffer>();
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = desc.size;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.memoryUsage) {
        case MemoryUsage::CPU_To_GPU:
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case MemoryUsage::GPU_To_CPU:
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            break;
        case MemoryUsage::CPU_Only:
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            break;
        default: break;
    }

    VkBuffer vkBuffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocResult;
    VkResult result = vmaCreateBuffer(m_Impl->vmaAllocator, &bufInfo, &allocInfo, &vkBuffer, &allocation, &allocResult);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] Failed to create buffer (size=%llu)\n", (unsigned long long)desc.size); return nullptr; }

    buffer->SetVkBuffer(vkBuffer);
    buffer->SetSize(desc.size);
    GPUAllocation gpuAlloc;
    gpuAlloc.apiMemoryHandle = allocResult.deviceMemory;
    gpuAlloc.apiResource = vkBuffer;
    gpuAlloc.mappedPtr = allocResult.pMappedData;
    gpuAlloc.size = desc.size;
    gpuAlloc.offset = allocResult.offset;
    buffer->SetAllocation(gpuAlloc);

    if (desc.initialData && allocResult.pMappedData) {
        std::memcpy(allocResult.pMappedData, desc.initialData, desc.size);
        if (!(allocResult.memoryType & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            vmaFlushAllocation(m_Impl->vmaAllocator, allocation, 0, desc.size);
    }
    return buffer;
}

std::shared_ptr<IRHITexture> VulkanDevice::CreateTexture(const TextureDesc& desc) {
    auto texture = std::make_shared<VulkanTexture>();
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = FormatToVk(desc.format);
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = (VkSampleCountFlagBits)desc.sampleCount;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VkImage vkImage = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocResult;
    VkResult result = vmaCreateImage(m_Impl->vmaAllocator, &imageInfo, &allocInfo, &vkImage, &allocation, &allocResult);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] Failed to create texture (%ux%u)\n", desc.width, desc.height); return nullptr; }

    texture->SetVkImage(vkImage);
    texture->SetWidth(desc.width);
    texture->SetHeight(desc.height);
    texture->SetFormat(desc.format);
    return texture;
}

IRHIPipelineState* VulkanDevice::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    // 1. 查缓存
    uint64_t hash = desc.GetHash();
    {
        auto hit = PSOCache::Get().Find<GraphicsPSODesc>(hash);
        if (hit) return hit;
    }
    // 2. 实际创建
    auto* pso = CreateGraphicsPSOInternal(desc);
    if (pso) PSOCache::Get().Store(hash, pso);
    return pso;
}

IRHIPipelineState* VulkanDevice::CreateGraphicsPSOInternal(const GraphicsPSODesc& desc) {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (m_Impl->pipelineLayoutCache && desc.vertexShader.size > 0) {
        const uint32_t* spirv = reinterpret_cast<const uint32_t*>(desc.vertexShader.data);
        layout = m_Impl->pipelineLayoutCache->GetOrCreateLayout(spirv, desc.vertexShader.size);
    }
    if (layout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        vkCreatePipelineLayout(m_Impl->device, &plCI, nullptr, &layout);
    }

    // 从 SPIR-V 创建 Shader Modules
    VkShaderModule vsModule = VK_NULL_HANDLE, fsModule = VK_NULL_HANDLE;
    if (desc.vertexShader.size > 0) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = desc.vertexShader.size;
        ci.pCode = reinterpret_cast<const uint32_t*>(desc.vertexShader.data);
        vkCreateShaderModule(m_Impl->device, &ci, nullptr, &vsModule);
    }
    if (desc.fragmentShader.size > 0) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = desc.fragmentShader.size;
        ci.pCode = reinterpret_cast<const uint32_t*>(desc.fragmentShader.data);
        vkCreateShaderModule(m_Impl->device, &ci, nullptr, &fsModule);
    }

    auto* pso = CreateGraphicsPipeline(m_Impl->device, layout, desc, vsModule, fsModule);

    // ShaderModule 创建 PSO 后即可销毁（VkPipeline 已编译它们）
    if (vsModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_Impl->device, vsModule, nullptr);
    if (fsModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_Impl->device, fsModule, nullptr);
    return pso;
}

IRHIPipelineState* VulkanDevice::CreateComputePSO(const ComputePSODesc& desc) {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (m_Impl->pipelineLayoutCache && desc.computeShader.size > 0) {
        const uint32_t* spirv = reinterpret_cast<const uint32_t*>(desc.computeShader.data);
        layout = m_Impl->pipelineLayoutCache->GetOrCreateLayout(spirv, desc.computeShader.size);
    }

    VkShaderModule csModule = VK_NULL_HANDLE;
    if (desc.computeShader.size > 0) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = desc.computeShader.size;
        ci.pCode = reinterpret_cast<const uint32_t*>(desc.computeShader.data);
        vkCreateShaderModule(m_Impl->device, &ci, nullptr, &csModule);
    }

    auto* pso = CreateComputePipeline(m_Impl->device, layout, csModule);
    if (csModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_Impl->device, csModule, nullptr);
    return pso;
}

std::unique_ptr<IRHICommandList> VulkanDevice::CreateCommandList(CommandListType type) {
    auto cmdList = std::make_unique<VulkanCommandList>();
    
    // 从当前帧资源分配命令缓冲
    auto& frame = m_Impl->frameContext;
    VulkanFrameResource& fr = frame.frames[frame.currentFrame];
    
    cmdList->SetDevice(this);
    if (fr.commandBuffer != VK_NULL_HANDLE) {
        cmdList->SetVkCommandBuffer(fr.commandBuffer);
    } else {
        // 备用：从线程池分配
        VkCommandPool pool = GetOrCreateThreadCommandPool();
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer altCmdBuf = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Impl->device, &allocInfo, &altCmdBuf);
        cmdList->SetVkCommandBuffer(altCmdBuf);
    }
    
    return cmdList;
}

IRHICommandQueue* VulkanDevice::GetQueue(QueueType type) {
    switch (type) {
        case QueueType::Graphics: return static_cast<IRHICommandQueue*>(m_Impl->graphicsQueueWrapper);
        case QueueType::Compute:  return static_cast<IRHICommandQueue*>(m_Impl->computeQueueWrapper);
        default:                  return static_cast<IRHICommandQueue*>(m_Impl->graphicsQueueWrapper);
    }
}

std::unique_ptr<IRHISwapChain> VulkanDevice::CreateSwapChain(const SwapChainDesc& desc) {
    if (!m_Impl->surface) { std::fprintf(stderr, "[VulkanDevice] No surface for swap chain\n"); return nullptr; }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_Impl->physicalDevice, m_Impl->graphicsQueueIndex, m_Impl->surface, &supported);
    if (!supported) { std::fprintf(stderr, "[VulkanDevice] Surface not supported\n"); return nullptr; }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &formatCount, formats.data());

    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        { selectedFormat = fmt; break; }
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_Impl->physicalDevice, m_Impl->surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_Impl->physicalDevice, m_Impl->surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto mode : presentModes) {
        if (!desc.vSync && mode == VK_PRESENT_MODE_IMMEDIATE_KHR) { presentMode = mode; break; }
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = mode;
    }

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Impl->physicalDevice, m_Impl->surface, &caps);
    uint32_t imageCount = std::max(caps.minImageCount, std::min(desc.bufferCount, caps.maxImageCount));

    VkSwapchainCreateInfoKHR swapCI{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapCI.surface = m_Impl->surface; swapCI.minImageCount = imageCount;
    swapCI.imageFormat = selectedFormat.format; swapCI.imageColorSpace = selectedFormat.colorSpace;
    swapCI.imageExtent = {desc.width, desc.height}; swapCI.imageArrayLayers = 1;
    swapCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapCI.preTransform = caps.currentTransform; swapCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapCI.presentMode = presentMode; swapCI.clipped = VK_TRUE;
    swapCI.oldSwapchain = m_Impl->swapChain;

    VkResult result = vkCreateSwapchainKHR(m_Impl->device, &swapCI, nullptr, &m_Impl->swapChain);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "[VulkanDevice] Failed to create swap chain\n"); return nullptr; }
    m_Impl->swapChainFormat = selectedFormat.format; m_Impl->swapChainWidth = desc.width; m_Impl->swapChainHeight = desc.height;

    uint32_t imageCountActual = 0;
    vkGetSwapchainImagesKHR(m_Impl->device, m_Impl->swapChain, &imageCountActual, nullptr);
    m_Impl->swapChainImages.resize(imageCountActual);
    vkGetSwapchainImagesKHR(m_Impl->device, m_Impl->swapChain, &imageCountActual, m_Impl->swapChainImages.data());
    m_Impl->swapChainTextures.clear();
    for (auto vkImage : m_Impl->swapChainImages) {
        auto tex = std::make_shared<VulkanTexture>();
        tex->SetVkImage(vkImage); tex->SetWidth(desc.width); tex->SetHeight(desc.height);
        tex->SetFormat(VkToFormat(selectedFormat.format));
        m_Impl->swapChainTextures.push_back(tex);
    }

    auto swapChain = std::make_unique<VulkanSwapChain>();
    swapChain->m_Impl->device = this;
    swapChain->m_Impl->swapChain = m_Impl->swapChain;
    swapChain->m_Impl->surface = m_Impl->surface;
    swapChain->m_Impl->bufferCount = imageCountActual;
    swapChain->m_Impl->format = selectedFormat.format;
    swapChain->m_Impl->presentMode = presentMode;
    swapChain->m_Impl->width = desc.width;
    swapChain->m_Impl->height = desc.height;
    swapChain->m_Impl->images = m_Impl->swapChainImages;
    for (auto tex : m_Impl->swapChainTextures)
        swapChain->m_Impl->backBufferTextures.push_back(tex.get());
    return swapChain;
}

void VulkanDevice::WaitIdle() { if (m_Impl->device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_Impl->device); }

const char* VulkanDevice::GetDeviceName() const { return m_Impl->deviceName.c_str(); }

VkDevice VulkanDevice::GetVkDevice() const noexcept { return m_Impl->device; }
VmaAllocator VulkanDevice::GetVmaAllocator() const noexcept { return m_Impl->vmaAllocator; }
VkPhysicalDevice VulkanDevice::GetVkPhysicalDevice() const noexcept { return m_Impl->physicalDevice; }
VkInstance VulkanDevice::GetVkInstance() const noexcept { return m_Impl->instance; }
VkQueue VulkanDevice::GetGraphicsQueue() const noexcept { return m_Impl->graphicsQueue; }
uint32_t VulkanDevice::GetGraphicsQueueIndex() const noexcept { return m_Impl->graphicsQueueIndex; }
VulkanFrameContext& VulkanDevice::GetFrameContext() noexcept { return m_Impl->frameContext; }

bool HasVulkanSupport() noexcept {
    HMODULE mod = LoadLibraryA("vulkan-1.dll");
    if (!mod) return false; FreeLibrary(mod); return true;
}

std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3 (Hardware)"; }

std::unique_ptr<IRHIDevice> CreateVulkanDevice() { return std::make_unique<VulkanDevice>(); }

} // namespace RHI
} // namespace Engine