#pragma once
/**
 * @file VulkanOffscreen.h
 * @brief 无窗口离屏渲染目标：color image + view + 渲染作用域 + 回读
 *
 * 为什么是这个形状：引擎的 Vulkan 管线创建走的是 **dynamic rendering**
 * （VulkanPipelineState.cpp 用 VkPipelineRenderingCreateInfo 挂到
 * VkGraphicsPipelineCreateInfo::pNext），也就是说管线不带 render pass。
 * 所以离屏设施不能提供 render pass + framebuffer —— 那种管线与引擎已建的
 * 管线互不兼容。这里提供的正是缺失的那一半：一张颜色附件 image，以及
 * vkCmdBeginRendering / vkCmdEndRendering 的渲染作用域。
 *
 * 为什么不扩展 IRHIDevice：给渲染作用域/回读加抽象会同时牵动 GL 与 D3D12，
 * 那是一次跨后端接口设计，而非本设施的目的。这里直接使用 VulkanDevice 已
 * 公开的裸句柄，不改动 RHI 接口。
 *
 * 用途：让 headless 环境（无 swapchain）真正渲染并读回像素。整条链路已在
 * lavapipe 上用像素断言验证可行，见 tools/vk_offscreen_probe.cpp。
 */
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Engine {
namespace RHI {

class VulkanOffscreenTarget {
public:
    /** 创建离屏目标；任一 Vulkan 调用失败返回 nullptr（不抛异常）。 */
    static std::unique_ptr<VulkanOffscreenTarget> Create(
        VulkanDevice& device, uint32_t width, uint32_t height);

    ~VulkanOffscreenTarget();

    VulkanOffscreenTarget(const VulkanOffscreenTarget&) = delete;
    VulkanOffscreenTarget& operator=(const VulkanOffscreenTarget&) = delete;

    uint32_t Width() const noexcept { return m_Width; }
    uint32_t Height() const noexcept { return m_Height; }
    uint32_t RowPitch() const noexcept { return m_RowPitch; }
    VkDeviceSize ReadbackBufferSize() const noexcept { return m_ReadbackSize; }

    VkImage     GetColorImage() const noexcept { return m_ColorImage; }
    VkImageView GetColorView()  const noexcept { return m_ColorView; }

    /** 开启动态渲染作用域。clearColor 为 RGBA 浮点。 */
    void BeginRendering(VkCommandBuffer cmd, const float clearColor[4]) const;

    /** 结束渲染作用域。 */
    void EndRendering(VkCommandBuffer cmd) const;

    /**
     * 分配一个一次性命令缓冲并开始录制（供录制绘制使用）。
     * VulkanCommandList 目前没有暴露渲染作用域 API，所以绘制路径由调用方
     * 用裸 Vulkan 命令填充这个缓冲。返回 VK_NULL_HANDLE 表示失败。
     */
    VkCommandBuffer BeginOneShot() const;

    /** 结束录制、提交到图形队列并等待完成。 */
    bool EndSubmitAndWait(VkCommandBuffer cmd) const;

    /**
     * 把颜色目标拷进内部 host-visible 缓冲并读出。
     * 内部自行完成 ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL 的屏障、
     * 提交、等待与 map。out 按 RGBA8 紧凑排列（RowPitch() == width*4）。
     */
    bool Readback(std::vector<uint8_t>& out);

private:
    VulkanOffscreenTarget() = default;

    VulkanDevice*    m_Device      = nullptr;
    VkDevice         m_VkDevice    = VK_NULL_HANDLE;
    VkPhysicalDevice m_Physical    = VK_NULL_HANDLE;
    VkQueue          m_Queue       = VK_NULL_HANDLE;
    uint32_t         m_QueueFamily = 0;

    uint32_t     m_Width  = 0;
    uint32_t     m_Height = 0;
    uint32_t     m_RowPitch = 0;
    VkDeviceSize m_ReadbackSize = 0;

    VkImage        m_ColorImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_ColorMemory = VK_NULL_HANDLE;
    VkImageView    m_ColorView   = VK_NULL_HANDLE;

    VkBuffer       m_ReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_ReadbackMemory = VK_NULL_HANDLE;

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want) const;
};

} // namespace RHI
} // namespace Engine