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
#include "Engine/Vulkan/ShaderRegistry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <mutex>
#define NOMINMAX
// Win32 只用于**带窗口的呈现路径**（surface 创建）。headless 离屏渲染不需要它，
// 而此前这两个头文件是无条件包含的 —— 于是整个 Vulkan 后端在 Linux 上根本
// 无法编译（find_package(Vulkan) 一旦成功就会把 src/Vulkan/*.cpp 纳入构建）。
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <dlfcn.h>
#endif

#define VK_CHECK(x) do { VkResult _e = (x); if (_e) { std::fprintf(stderr, "[Vulkan] Error %d\n", _e); abort(); } } while(0)

namespace Engine {
namespace RHI {
namespace {

// ── Validation layer 支持 ──────────────────────────────────────────────────
//
// 注意边界：validation **抓不到**「忘记设置动态视口」这类问题 —— 未设置动态状态
// 是合法的 API 调用，layer 不会报错，回读只会得到全 0。
// 它真正能抓的是本工程的实际风险面：离屏回读路径上的 barrier / image layout 转换、
// 命令缓冲生命周期、对象销毁次序。像素异常时这些是第二类候选根因。
//
// 环境变量（均为可选，缺省关闭，不影响正常构建与发布）：
//   ENGINE_VULKAN_VALIDATION=1        请求 VK_LAYER_KHRONOS_validation
//   ENGINE_VULKAN_VALIDATION_VERBOSE=1 额外打印 VERBOSE 级别
//   ENGINE_VULKAN_SYNC_VALIDATION=1   额外启用同步验证（barrier / 竞争检测）

bool EnvFlag(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '1' && v[1] == '\0';
}

const char* SeverityName(VkDebugUtilsMessageSeverityFlagBitsEXT s) {
    switch (s) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "INFO";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "ERROR";
        default: return "?";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    // 只打引擎关心的两类：状态机/对象生命周期 与 同步。前者噪声太大反而淹没信号。
    const bool relevant = (types & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    if (!relevant) return VK_FALSE;
    std::fprintf(stderr, "[Vulkan][validation] %s: %s\n",
                 SeverityName(severity), data && data->pMessage ? data->pMessage : "(no message)");
    return VK_FALSE;
}

bool LayerAvailable(const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkLayerProperties> layers(count);
    if (count && vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
    for (uint32 i = 0; i < count; ++i) {
        if (std::strcmp(layers[i].layerName, name) == 0) return true;
    }
    return false;
}

bool InstanceExtensionAvailable(const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> exts(count);
    if (count && vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data()) != VK_SUCCESS) return false;
    for (uint32 i = 0; i < count; ++i) {
        if (std::strcmp(exts[i].extensionName, name) == 0) return true;
    }
    return false;
}

}  // namespace

IRHIPipelineState* CreateGraphicsPipeline(VkDevice, VkPipelineLayout, const GraphicsPSODesc&, VkShaderModule, VkShaderModule);
IRHIPipelineState* CreateComputePipeline(VkDevice, VkPipelineLayout, VkShaderModule);

struct VulkanDevice::Impl {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    VkQueue transferQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueIndex{UINT32_MAX};
    uint32_t transferQueueIndex{UINT32_MAX};
    // 设备级命令池：供一次性命令缓冲使用（离屏回读、初始化上传等）。
    // 帧渲染另有 VulkanFrameContext::commandPool。
    VkCommandPool cmdPool{VK_NULL_HANDLE};
    VmaAllocator vmaAllocator{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    // 原生窗口句柄：X11 的 Display / Wayland 的 wl_display。必须在 Initialize 之前设置，
    // 否则非 Windows 上无法创建 surface。
    NativeWindow nativeWindow{};
    std::vector<VkImage> swapChainImages;
    VulkanFrameContext frameContext;
    VulkanQueue* graphicsQueueWrapper{nullptr};
    VulkanPipelineLayoutCache* pipelineLayoutCache{nullptr};
    VulkanDeferredDeletion* deletionQueue{nullptr};
    uint32_t uboAlignment{256}; // 默认 256，初始化时从设备获取
    bool initialized{false};
    std::string deviceName;

    // ── 调试 / validation（可选，由环境变量开启）──
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    bool enableValidation{false};
    bool syncValidation{false};
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

void VulkanDevice::SetNativeWindow(const NativeWindow& win) noexcept {
    m_Impl->nativeWindow = win;
}

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
#ifdef _WIN32
        instanceExts.push_back("VK_KHR_win32_surface");
#else
        // XCB / Xlib / Wayland 三者按可用性择一。必须在 vkCreateInstance 之前决定，
        // 此刻还没有 instance 可查询，因此退而查询**全局**实例扩展列表。
        if (const char* platformExt = GetAvailablePlatformSurfaceExtension(VK_NULL_HANDLE)) {
            instanceExts.push_back(platformExt);
        }
#endif
    }
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    ici.ppEnabledExtensionNames = instanceExts.data();

    // ── Validation layer：可选，按环境变量开启 ──
    // 只有 layer 与 VK_EXT_debug_utils 都真实存在时才请求，否则 vkCreateInstance
    // 会以 VK_ERROR_LAYER_NOT_PRESENT 直接失败 —— 缺 layer 必须降级而不是报错。
    std::vector<const char*> instanceLayers;
    VkValidationFeaturesEXT validationFeatures{};
    validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    if (EnvFlag("ENGINE_VULKAN_VALIDATION")) {
        const bool hasLayer = LayerAvailable("VK_LAYER_KHRONOS_validation");
        const bool hasUtils = InstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (hasLayer && hasUtils) {
            instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
            instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            ici.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());

            VkValidationFeatureEnableEXT enables[2]{};
            uint32_t enableCount = 0;
            enables[enableCount++] = VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT;
            if (EnvFlag("ENGINE_VULKAN_SYNC_VALIDATION")) {
                validationFeatures.enabledValidationFeatureCount = enableCount + 1;
                enables[enableCount++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            } else {
                validationFeatures.enabledValidationFeatureCount = enableCount;
            }
            validationFeatures.pEnabledValidationFeatures = enables;
            ici.pNext = &validationFeatures;
            ici.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
            ici.ppEnabledLayerNames = instanceLayers.data();
            m_Impl->enableValidation = true;
            m_Impl->syncValidation = EnvFlag("ENGINE_VULKAN_SYNC_VALIDATION");
            std::fprintf(stderr, "[Vulkan] validation layer enabled (sync validation: %s)\n",
                         m_Impl->syncValidation ? "on" : "off");
        } else {
            std::fprintf(stderr, "[Vulkan] ENGINE_VULKAN_VALIDATION set but layer/extension missing "
                                 "(layer=%d debug_utils=%d); continuing without validation\n",
                         hasLayer ? 1 : 0, hasUtils ? 1 : 0);
        }
    }

    if (vkCreateInstance(&ici, nullptr, &m_Impl->instance) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create instance\n");
        return false;
    }
    VulkanLoader::LoadInstance(m_Impl->instance);

    // ── Debug messenger：必须在实例创建之后、任何其它 Vulkan 调用之前挂上，
    //    否则 layer 已经产生的消息无处可去。
    if (m_Impl->enableValidation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_Impl->instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT dci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dci.messageSeverity = EnvFlag("ENGINE_VULKAN_VALIDATION_VERBOSE")
                ? (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT   |
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
                : (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
            dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dci.pfnUserCallback = DebugCallback;
            if (create(m_Impl->instance, &dci, nullptr, &m_Impl->debugMessenger) != VK_SUCCESS) {
                m_Impl->debugMessenger = VK_NULL_HANDLE;
                std::fprintf(stderr, "[Vulkan] failed to create debug messenger\n");
            }
        }
    }

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
    for (uint32 i = 0; i < qCnt; ++i) {
        if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) m_Impl->graphicsQueueIndex = i;
        // 优先选择专门的传输队列（只有 TRANSFER 位，没有 GRAPHICS/COMPUTE），
        // 如果没有则退而使用 graphics 队列
        if ((qProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(qProps[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) &&
            m_Impl->transferQueueIndex == UINT32_MAX) {
            m_Impl->transferQueueIndex = i;
        }
    }
    // 如果没有独立传输队列，复用 graphics 队列
    if (m_Impl->transferQueueIndex == UINT32_MAX) {
        m_Impl->transferQueueIndex = m_Impl->graphicsQueueIndex;
    }

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
#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        sci.hinstance = GetModuleHandle(nullptr); sci.hwnd = (HWND)windowHandle;
        vkCreateWin32SurfaceKHR(m_Impl->instance, &sci, nullptr, &m_Impl->surface);
#else
        // Linux/macOS：windowHandle 单独一个指针不足以建 surface，需要 Display 或
        // wl_display。这些句柄必须由调用方经 SetNativeWindow 提供 ——
        // RHIWindow 在非 Windows 上把 GetHWND() 显式置空，因此这里常常拿不到。
        NativeWindow native;
        native.window = windowHandle;
        native.display = m_Impl->nativeWindow.display;
        if (native.display) {
            if (!CreatePlatformSurface(m_Impl->instance, native, &m_Impl->surface)) {
                std::fprintf(stderr,
                             "[Vulkan] window handle given but no surface could be created; "
                             "call SetNativeWindow() with the Display/wl_display before Initialize()\n");
            }
        } else {
            std::fprintf(stderr,
                         "[Vulkan] window handle given without a display connection; "
                         "no surface will be created (present path unavailable)\n");
        }
#endif
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

    // 设备级命令池。一次性命令缓冲（离屏回读等）需要一个可分配的池；
    // 此前 GetOrCreateThreadCommandPool() 直接返回 VK_NULL_HANDLE，
    // 使得任何依赖它的路径都无法工作。
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = m_Impl->graphicsQueueIndex;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
               VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(m_Impl->device, &pci, nullptr, &m_Impl->cmdPool) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create device command pool\n");
        m_Impl->cmdPool = VK_NULL_HANDLE;
        return false;
    }

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
    if (m_Impl->cmdPool) vkDestroyCommandPool(m_Impl->device, m_Impl->cmdPool, nullptr);
    m_Impl->cmdPool = VK_NULL_HANDLE;
    if (m_Impl->swapChain) vkDestroySwapchainKHR(m_Impl->device, m_Impl->swapChain, nullptr);
    if (m_Impl->surface) vkDestroySurfaceKHR(m_Impl->instance, m_Impl->surface, nullptr);
    // VMA 要求：销毁前所有由它分配的资源必须已释放
    if (m_Impl->vmaAllocator) vmaDestroyAllocator(m_Impl->vmaAllocator);
    m_Impl->vmaAllocator = VK_NULL_HANDLE;
    if (m_Impl->device) vkDestroyDevice(m_Impl->device, nullptr);
    // messenger 依赖实例，必须在 vkDestroyInstance 之前销毁
    if (m_Impl->debugMessenger != VK_NULL_HANDLE) {
        if (auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_Impl->instance, "vkDestroyDebugUtilsMessengerEXT"))) {
            destroy(m_Impl->instance, m_Impl->debugMessenger, nullptr);
        }
        m_Impl->debugMessenger = VK_NULL_HANDLE;
    }
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

    // 创建 image view：渲染作用域（vkCmdBeginRendering）绑定的是 view 而非 image。
    // 此前 CreateTexture 只给 image，view 恒为 VK_NULL_HANDLE，因此通过
    // IRHITexture* 构造的任何渲染目标都会被跳过 —— 命令列表的渲染路径无法用于离屏。
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = vkImg;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(m_Impl->device, &vi, nullptr, &view) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkCreateImageView failed in CreateTexture\n");
        vmaDestroyImage(m_Impl->vmaAllocator, vkImg, alloc);
        return nullptr;
    }
    tex->SetVkImageView(view);
    tex->SetVkDevice(m_Impl->device);
    return tex;
}

IRHIPipelineState* VulkanDevice::CreateGraphicsPSO(const GraphicsPSODesc& desc) {
    uint64_t hash = desc.GetHash();
    if (auto* hit = PSOCache::Get().Find<GraphicsPSODesc>(hash)) return hit;
    VkPipelineLayout layout; VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_Impl->device, &plCI, nullptr, &layout);

    // 着色器解析：此前这里传的是两个 VK_NULL_HANDLE，而 CreateGraphicsPipeline
    // 对空模块的处理是 stages.empty() → 返回 nullptr，于是图形管线**永远无法创建**。
    // GraphicsPSODesc 用 StringID 引用着色器，StringID 是单向哈希无法反解回名字，
    // 因此走运行时注册表（加载期填充）。
    VkShaderModule vsModule = VK_NULL_HANDLE;
    VkShaderModule fsModule = VK_NULL_HANDLE;

    if (const auto* vs = ShaderRegistry::Get().Find(desc.vertexShader, ShaderStageType::Vertex)) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = vs->size() * sizeof(uint32_t);
        ci.pCode = vs->data();
        if (vkCreateShaderModule(m_Impl->device, &ci, nullptr, &vsModule) != VK_SUCCESS)
            vsModule = VK_NULL_HANDLE;
    } else {
        std::fprintf(stderr, "[Vulkan] vertex shader not registered (id=%llu)\n",
                     static_cast<unsigned long long>(desc.vertexShader.Value()));
    }

    if (const auto* fs = ShaderRegistry::Get().Find(desc.pixelShader, ShaderStageType::Fragment)) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = fs->size() * sizeof(uint32_t);
        ci.pCode = fs->data();
        if (vkCreateShaderModule(m_Impl->device, &ci, nullptr, &fsModule) != VK_SUCCESS)
            fsModule = VK_NULL_HANDLE;
    } else {
        std::fprintf(stderr, "[Vulkan] pixel shader not registered (id=%llu)\n",
                     static_cast<unsigned long long>(desc.pixelShader.Value()));
    }

    auto* pso = CreateGraphicsPipeline(m_Impl->device, layout, desc, vsModule, fsModule);
    // 模块只用于创建管线，创建完即可销毁（管线内部已持有）。
    if (vsModule) vkDestroyShaderModule(m_Impl->device, vsModule, nullptr);
    if (fsModule) vkDestroyShaderModule(m_Impl->device, fsModule, nullptr);
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

IRHICommandQueue* VulkanDevice::GetQueue(QueueType type) {
    if (type == QueueType::Transfer && m_Impl->transferQueueIndex != m_Impl->graphicsQueueIndex) {
        // 有独立传输队列 — 创建专用 wrapper（懒初始化）
        if (!m_Impl->graphicsQueueWrapper) {
            m_Impl->graphicsQueueWrapper = new VulkanQueue();
        }
        return m_Impl->graphicsQueueWrapper;
    }
    return m_Impl->graphicsQueueWrapper;
}

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
        tex->SetFormat(VkToFormat(selFmt.format));
        // 初始 layout：交换链图像创建后处于 PRESENT_SRC_KHR
        tex->SetLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        // 渲染作用域（vkCmdBeginRendering）绑定的是 image **view**，不是 image。
        // 此前这里只设了 image，导致 backbuffer 的 view 恒为 VK_NULL_HANDLE，
        // 任何以 IRHITexture* 构造的渲染附件都会被跳过 —— 窗口路径永远画不出东西。
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = vkImg;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = selFmt.format;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(m_Impl->device, &vci, nullptr, &view) != VK_SUCCESS || view == VK_NULL_HANDLE) {
            std::fprintf(stderr, "[Vulkan] vkCreateImageView failed for a swapchain backbuffer\n");
            vkDestroySwapchainKHR(m_Impl->device, m_Impl->swapChain, nullptr);
            m_Impl->swapChain = VK_NULL_HANDLE;
            return nullptr;
        }
        tex->SetVkImageView(view);
        tex->SetVkDevice(m_Impl->device);   // view 由纹理自己析构销毁
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
VkCommandPool VulkanDevice::GetOrCreateThreadCommandPool() {
    // 单一设备级池。真正的 per-thread 池需要 thread_local 存储，
    // 但 headless 一次性命令缓冲（离屏回读等）只需一个可重置的池。
    return m_Impl ? m_Impl->cmdPool : VK_NULL_HANDLE;
}
void VulkanDevice::ResetAllThreadCommandPools() {}

bool HasVulkanSupport() noexcept {
        // 跨平台探测：Windows 走 vulkan-1.dll，POSIX 走 libvulkan.so。
        // 原实现只用 LoadLibraryA，在非 Windows 上必然编译失败。
#ifdef _WIN32
        HMODULE m = LoadLibraryA("vulkan-1.dll");
        if (!m) return false;
        FreeLibrary(m);
        return true;
#else
        void* h = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!h) h = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
        if (!h) return false;
        dlclose(h);
        return true;
#endif
    }
std::string GetVulkanDeviceInfo() noexcept { return "Vulkan 1.3"; }
std::unique_ptr<IRHIDevice> CreateVulkanDevice() {
    auto dev = std::make_unique<VulkanDevice>();
    dev->Initialize(nullptr, 0, 0);
    return dev;
}

} // namespace RHI
} // namespace Engine