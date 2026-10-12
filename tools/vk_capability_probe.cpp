/**
 * Vulkan 能力探针 —— 只测量，不改动任何东西。
 *
 * 动机：引擎把 OpenGL 4.6 作为产品承诺，而 Linux CI runner 的 llvmpipe 最高
 * 只提供 4.5，Zink 在 lavapipe 上也无法初始化。于是 headless GPU 像素验证
 * 只剩 Vulkan 一条路。是否值得投入，取决于"lavapipe 能否真正跑起 Vulkan
 * 渲染所需的东西"——这必须先测，不能假设。
 *
 * 这里刻意**不链接引擎**：只测环境能力，与引擎实现解耦，
 * 这样即使引擎侧 Vulkan 后端尚未实现完整呈现层，也能给出环境侧结论。
 * 测量内容：
 *   1. Vulkan 实例能否创建
 *   2. 物理设备能否枚举，驱动与 API 版本
 *   3. 能否创建设备队列族（graphics）
 *   4. 离屏渲染所需的附件格式是否可用
 *   5. 能否真正创建一个设备
 */
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <vector>

static int Fail(const char* what, VkResult r)
{
    std::printf("[VKPROBE] %s FAILED (VkResult=%d)\n", what, static_cast<int>(r));
    return 1;
}

int main()
{
    std::printf("[VKPROBE] === Vulkan capability probe ===\n");

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkprobe";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) return Fail("vkCreateInstance", r);
    std::printf("[VKPROBE] instance OK\n");

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::printf("[VKPROBE] physical devices = %u\n", count);
    if (count == 0) {
        std::printf("[VKPROBE] NO PHYSICAL DEVICE -> lavapipe not usable\n");
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        std::printf("[VKPROBE] device: %s\n", props.deviceName);
        std::printf("[VKPROBE]   driverVersion = %u.%u.%u  apiVersion = %u.%u.%u  type = %d\n",
                    VK_VERSION_MAJOR(props.driverVersion),
                    VK_VERSION_MINOR(props.driverVersion),
                    VK_VERSION_PATCH(props.driverVersion),
                    VK_VERSION_MAJOR(props.apiVersion),
                    VK_VERSION_MINOR(props.apiVersion),
                    VK_VERSION_PATCH(props.apiVersion),
                    static_cast<int>(props.deviceType));

        // 离屏渲染只需要一个支持 graphics 的队列族。
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qs.data());
        for (uint32_t i = 0; i < qcount; ++i) {
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                std::printf("[VKPROBE]   graphics queue family = %u (count=%u)\n", i, qs[i].queueCount);
                if (chosen == VK_NULL_HANDLE) chosen = pd;
                break;
            }
        }

        // 附件格式：离屏颜色目标能否落地
        // 注意：vkGetPhysicalDeviceFormatProperties 的第三参是**输出**参数，
        // 不能传 nullptr（传了必崩）。
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
        const bool colorTarget = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
        std::printf("[VKPROBE]   R8G8B8A8_UNORM color attachment = %s\n", colorTarget ? "YES" : "NO");
        std::printf("[VKPROBE]   maxImageDimension2D = %u\n", props.limits.maxImageDimension2D);
        std::fflush(stdout);
    }

    if (chosen == VK_NULL_HANDLE) {
        std::printf("[VKPROBE] no graphics-capable queue family -> cannot render offscreen\n");
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qcount, qs.data());
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice device = VK_NULL_HANDLE;
    r = vkCreateDevice(chosen, &dci, nullptr, &device);
    if (r != VK_SUCCESS) return Fail("vkCreateDevice", r);
    std::printf("[VKPROBE] logical device OK -> offscreen Vulkan rendering is feasible here\n");

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::printf("[VKPROBE] === probe complete ===\n");
    return 0;
}