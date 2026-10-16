#pragma once

/**
 * @file VulkanCommon.h
 * @brief Vulkan 公共辅助函数和常量
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <string>

namespace Engine {

// ── 调试宏 ──
#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) { \
    std::fprintf(stderr, "[Vulkan] VK_CHECK failed at %s:%d: %d\n", __FILE__, __LINE__, (int)r); \
    __debugbreak(); \
} } while(0)
#endif

// ── 常量 ──
constexpr uint32_t kMaxFramesInFlight = 3;
constexpr uint32_t kMaxDescriptorCount = 1024;
constexpr uint32_t kDynamicUBOSizePerFrame = 64 * 1024;  // 64KB per frame
constexpr VkCommandPoolCreateFlags kThreadCmdPoolFlags =
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
    VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

// ── 格式转换 ──
VkFormat FormatToVk(RHI::Format format);
    RHI::Format VkToFormat(VkFormat format);
    /** 深度 / 模板判定。CreateTexture 此前一律按颜色格式建图像与视图：
     *  对 D32_Float 之类的深度格式会得到「深度图像 + COLOR_ATTACHMENT 用途 +
     *  COLOR aspect」这种非法组合，深度附件根本无法使用。 */
    bool IsDepthFormat(RHI::Format format) noexcept;
    bool IsStencilFormat(RHI::Format format) noexcept;
    VkImageAspectFlags FormatToAspect(RHI::Format format) noexcept;
    /** RHI 深度比较函数 → Vulkan 比较算子。此前管线里 depthCompareOp 被硬编码为
     *  VK_COMPARE_OP_LESS，完全忽略调用方在 DepthStencilDesc 里选的函数 ——
     *  默认值是 LessEqual，两者对等深度像素的行为并不相同。 */
    VkCompareOp CompareFuncToVk(RHI::CompareFunc f) noexcept;
    /** RHI 描述符类型 → Vulkan 描述符类型。 */
    VkDescriptorType DescriptorKindToVk(RHI::DescriptorKind kind) noexcept;

// ── 屏障辅助 ──
void ConvertBarrierDesc(
    const RHI::ResourceBarrierDesc& src,
    VkImageMemoryBarrier& outBarrier,
    VkPipelineStageFlags& outSrcStage,
    VkPipelineStageFlags& outDstStage);

// ── 实例/设备扩展 ──
std::vector<const char*> GetRequiredInstanceExtensions(bool enableValidation);
    std::vector<const char*> GetRequiredDeviceExtensions();

    // ============================================================
    // 平台 surface（Win32 / X11 / Wayland）
    // ============================================================

    /**
     * 原生窗口句柄。
     *
     * IRHIDevice::Initialize 的 windowHandle 是无类型 void*，无法同时表达
     * X11 的 Display 与 Wayland 的 wl_surface —— 它们的数量就不是一个。
     * 因此把「窗口」与「显示连接」拆开传入：
     *
     *   Windows : display 忽略，window 为 HWND
     *   X11     : display = Display*（xlib）或 xcb_connection_t*（xcb），window = Window XID
     *   Wayland : display = wl_display*，window = wl_surface*
     *
     * 三个字段都为空时按无窗口（headless）处理，不创建 surface。
     */
    /**
     * 原生窗口平台。**必须显式指定**，不能只靠「扩展是否可用」推断：
     * Xlib 的 `Display*` 与 xcb 的 `xcb_connection_t*` 都是不透明指针，按可用性
     * 猜测会把 Xlib 的 Display* 当成 xcb 连接去 reinterpret_cast，类型不匹配且
     * 不会立刻报错，而是产生无法呈现的 surface。
     */
    enum class NativeWindowPlatform : uint8 {
        Auto   = 0,   ///< 按 XCB → Xlib → Wayland 顺序取第一个可用的
        Xlib   = 1,   ///< display = Display*，window = Window XID
        Xcb    = 2,   ///< display = xcb_connection_t*，window = Window XID
        Wayland= 3,   ///< display = wl_display*，window = wl_surface*
    };

    struct NativeWindow {
        void* display = nullptr;
        void* window  = nullptr;
        NativeWindowPlatform platform = NativeWindowPlatform::Auto;
    };

#if !defined(_WIN32)
    /**
     * 返回本机可用的平台 surface 扩展名；一个都不可用时返回 nullptr。
     *
     * 传入 Auto 时按 XCB → Xlib → Wayland 顺序取第一个可用的；传入具体平台时
     * 只返回该平台的扩展，缺失即返回 nullptr。**实例创建与 surface 创建必须
     * 得到同一个结果**，否则会出现「实例未启用该扩展却去创建 surface」。
     *
     * 必须先创建 VkInstance 才能查询 —— 因此它接收 instance。
     */
    const char* GetAvailablePlatformSurfaceExtension(VkInstance instance,
                                                    NativeWindowPlatform requested) noexcept;

    /**
     * 按 NativeWindow 的实际内容选择平台并创建 VkSurfaceKHR。
     * 成功返回 true；平台不支持、扩展缺失或调用失败返回 false。
     *
     * 不透明指针的 reinterpret_cast 在这里是有意的：X11/Wayland 的类型
     * （Display、xcb_connection_t、wl_display、wl_surface）来自各自的 SDK，
     * 而 Vulkan 后端不应为此强制依赖 X11 或 wayland-client 的开发包。
     * Vulkan 头只要求把它们作为不完整类型的前向声明传入。
     */
    bool CreatePlatformSurface(VkInstance instance, const NativeWindow& win,
                               VkSurfaceKHR* outSurface) noexcept;
#endif // !_WIN32

// ── 着色器阶段转换 ──
VkShaderStageFlagBits ShaderStageToVk(ShaderStage stage);

// ── 拓扑转换 ──
VkPrimitiveTopology TopologyToVk(RHI::PrimitiveTopology topology);

// ── 格式大小 ──
uint32_t GetFormatSize(RHI::Format format);

// ── 资源状态转换（Vulkan 专用，在 VulkanCommon.cpp 中实现） ──
VkImageLayout ResourceStateToLayout(RHI::ResourceState state) noexcept;
VkPipelineStageFlags ResourceStateToStage(RHI::ResourceState state) noexcept;
VkAccessFlags ResourceStateToAccess(RHI::ResourceState state) noexcept;

// ── 计算管线辅助（VulkanComputePipelineHelper.cpp） ──
VkPipeline CreateVulkanComputePipeline(
    VkDevice device,
    VkPipelineCache pipelineCache,
    const std::vector<uint32_t>& shaderSPIRV,
    const char* entryPoint = "main");

} // namespace Engine
