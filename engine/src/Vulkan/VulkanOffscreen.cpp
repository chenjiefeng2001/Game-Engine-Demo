/**
 * @file VulkanOffscreen.cpp
 * @brief VulkanOffscreenTarget 实现（dynamic rendering 路径）
 *
 * 与 render pass 方案的差别：这里不开 VkRenderPass，而是用
 * vkCmdBeginRendering + VkRenderingInfo 直接把颜色 image 声明为附件。
 * 这是引擎管线（VkPipelineRenderingCreateInfo）唯一兼容的用法。
 *
 * 顺序与 tools/vk_offscreen_probe.cpp 中已通过像素验证的探针一致。
 */
#include "Engine/Vulkan/VulkanOffscreen.h"
#include "Engine/Vulkan/VulkanCommon.h"

#include <cstdio>
#include <cstring>

namespace Engine {
namespace RHI {

namespace {
constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;

bool Failed(const char* what, VkResult r)
{
    // 此前无条件打印并恒返回 false：成功时误报 "failed"，而真正的失败又不会返回
    // true，导致调用方的 `if (Failed(...)) return nullptr;` 形同虚设，会带着
    // VK_NULL_HANDLE 继续往下走。
    if (r == VK_SUCCESS) return false;
    std::fprintf(stderr, "[VulkanOffscreen] %s failed (VkResult=%d)\n", what, static_cast<int>(r));
    return true;
}
} // namespace

uint32_t VulkanOffscreenTarget::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want) const
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(m_Physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

std::unique_ptr<VulkanOffscreenTarget> VulkanOffscreenTarget::Create(
    VulkanDevice& device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return nullptr;

    VkDevice vkDevice = device.GetVkDevice();
    VkPhysicalDevice phys = device.GetVkPhysicalDevice();
    if (vkDevice == VK_NULL_HANDLE || phys == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[VulkanOffscreen] device not initialised\n");
        return nullptr;
    }

    std::unique_ptr<VulkanOffscreenTarget> t(new VulkanOffscreenTarget());
    t->m_Device      = &device;
    t->m_VkDevice    = vkDevice;
    t->m_Physical    = phys;
    t->m_Queue       = device.GetGraphicsQueue();
    t->m_QueueFamily = device.GetGraphicsQueueIndex();
    t->m_Width  = width;
    t->m_Height = height;

    // ── color attachment image ──
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = kOffscreenFormat;
    ici.extent        = { width, height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (Failed("vkCreateImage", vkCreateImage(vkDevice, &ici, nullptr, &t->m_ColorImage))) return nullptr;

    VkMemoryRequirements imgReq{};
    vkGetImageMemoryRequirements(vkDevice, t->m_ColorImage, &imgReq);
    uint32_t imgType = t->FindMemoryType(imgReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imgType == UINT32_MAX) imgType = t->FindMemoryType(imgReq.memoryTypeBits, 0);
    if (imgType == UINT32_MAX) return nullptr;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = imgReq.size;
    mai.memoryTypeIndex = imgType;
    if (Failed("vkAllocateMemory(image)", vkAllocateMemory(vkDevice, &mai, nullptr, &t->m_ColorMemory))) return nullptr;
    vkBindImageMemory(vkDevice, t->m_ColorImage, t->m_ColorMemory, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = t->m_ColorImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = kOffscreenFormat;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (Failed("vkCreateImageView", vkCreateImageView(vkDevice, &ivci, nullptr, &t->m_ColorView))) return nullptr;

    // ── readback buffer (linear, host-visible, tightly packed) ──
    t->m_RowPitch = width * 4;
    t->m_ReadbackSize = static_cast<VkDeviceSize>(t->m_RowPitch) * height;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = t->m_ReadbackSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (Failed("vkCreateBuffer", vkCreateBuffer(vkDevice, &bci, nullptr, &t->m_ReadbackBuffer))) return nullptr;

    VkMemoryRequirements bufReq{};
    vkGetBufferMemoryRequirements(vkDevice, t->m_ReadbackBuffer, &bufReq);
    uint32_t bufType = t->FindMemoryType(bufReq.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bufType == UINT32_MAX) return nullptr;

    VkMemoryAllocateInfo bmai{};
    bmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bmai.allocationSize = bufReq.size;
    bmai.memoryTypeIndex = bufType;
    if (Failed("vkAllocateMemory(readback)", vkAllocateMemory(vkDevice, &bmai, nullptr, &t->m_ReadbackMemory))) return nullptr;
    vkBindBufferMemory(vkDevice, t->m_ReadbackBuffer, t->m_ReadbackMemory, 0);

    return t;
}

VulkanOffscreenTarget::~VulkanOffscreenTarget()
{
    if (m_VkDevice == VK_NULL_HANDLE) return;
    m_Device->WaitIdle();

    if (m_ReadbackBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_VkDevice, m_ReadbackBuffer, nullptr);
    if (m_ReadbackMemory != VK_NULL_HANDLE) vkFreeMemory(m_VkDevice, m_ReadbackMemory, nullptr);
    if (m_ColorView      != VK_NULL_HANDLE) vkDestroyImageView(m_VkDevice, m_ColorView, nullptr);
    if (m_ColorImage     != VK_NULL_HANDLE) vkDestroyImage(m_VkDevice, m_ColorImage, nullptr);
    if (m_ColorMemory    != VK_NULL_HANDLE) vkFreeMemory(m_VkDevice, m_ColorMemory, nullptr);
}

void VulkanOffscreenTarget::BeginRendering(VkCommandBuffer cmd, const float clearColor[4]) const
{
    VkClearValue clear{};
    std::memcpy(&clear.color, clearColor, sizeof(clear.color));

    VkRenderingAttachmentInfo att{};
    att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    att.imageView = m_ColorView;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // 必须把 clearValue 交给附件描述：loadOp=CLEAR 读的是 att.clearValue，
    // 而不是上面那个局部变量。此前 clear 被构造后从未使用，附件保持零初始化，
    // 于是每次都清成 (0,0,0,0) —— 调用方传入的清除色被静默丢弃。
    att.clearValue = clear;

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = { { 0, 0 }, { m_Width, m_Height } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;

    // 声明布局不等于转换布局：图像创建后处于 UNDEFINED，直接 vkCmdBeginRendering
    // 会让 validation 报 expects COLOR_ATTACHMENT_OPTIMAL / current UNDEFINED。
    if (m_ColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        VkImageMemoryBarrier b{};
        b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image            = m_ColorImage;
        b.oldLayout        = m_ColorLayout;
        b.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask    = m_ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                 ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        m_ColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    vkCmdBeginRendering(cmd, &ri);

    // 视口与裁剪：VulkanPipelineState 把二者声明为**动态状态**
    // （VK_DYNAMIC_STATE_VIEWPORT / VK_DYNAMIC_STATE_SCISSOR），因此必须在绘制前
    // 显式设置，否则视口未定义、绘制被整体裁掉，且不会报任何错误。
    // 这里按目标尺寸设成全量；即便管线用的是静态视口，多调用一次也是合法的。
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width  = static_cast<float>(m_Width);
    vp.height = static_cast<float>(m_Height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { m_Width, m_Height };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanOffscreenTarget::EndRendering(VkCommandBuffer cmd) const
{
    vkCmdEndRendering(cmd);
}

VkCommandBuffer VulkanOffscreenTarget::BeginOneShot() const
{
    VkCommandPool pool = m_Device->GetOrCreateThreadCommandPool();
    if (pool == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_VkDevice, &ai, &cmd) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

bool VulkanOffscreenTarget::EndSubmitAndWait(VkCommandBuffer cmd) const
{
    if (cmd == VK_NULL_HANDLE) return false;
    VkCommandPool pool = m_Device->GetOrCreateThreadCommandPool();

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        if (pool) vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
        return false;
    }

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_VkDevice, &fi, nullptr, &fence);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    bool ok = false;
    if (vkQueueSubmit(m_Queue, 1, &si, fence) == VK_SUCCESS &&
        vkWaitForFences(m_VkDevice, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS) {
        ok = true;
    }

    vkDestroyFence(m_VkDevice, fence, nullptr);
    if (pool) vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
    return ok;
}

bool VulkanOffscreenTarget::Readback(std::vector<uint8_t>& out)
{
    out.assign(static_cast<size_t>(m_ReadbackSize), 0);

    VkCommandPool pool = m_Device->GetOrCreateThreadCommandPool();
    if (pool == VK_NULL_HANDLE) return false;

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_VkDevice, &ai, &cmd) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
        return false;
    }

// 渲染作用域结束后 image 处于 COLOR_ATTACHMENT_OPTIMAL，转成 TRANSFER_SRC 才能拷贝。
      // 注意 oldLayout 必须与上面声明的 att.imageLayout 一致：用 ATTACHMENT_OPTIMAL
      // 会触发 VUID-VkImageMemoryBarrier-synchronization2-07793（该布局需要
      // synchronization2 特性），barrier 被丢弃后图像仍是 UNDEFINED，
      // 于是 vkCmdBeginRendering 拿到 UNDEFINED 布局。
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_ColorImage;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { m_Width, m_Height, 1 };
    vkCmdCopyImageToBuffer(cmd, m_ColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_ReadbackBuffer, 1, &copy);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
        return false;
    }

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_VkDevice, &fi, nullptr, &fence);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    bool ok = false;
    if (vkQueueSubmit(m_Queue, 1, &si, fence) == VK_SUCCESS &&
        vkWaitForFences(m_VkDevice, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS) {
        void* mapped = nullptr;
        if (vkMapMemory(m_VkDevice, m_ReadbackMemory, 0, m_ReadbackSize, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(out.data(), mapped, static_cast<size_t>(m_ReadbackSize));
            vkUnmapMemory(m_VkDevice, m_ReadbackMemory);
            ok = true;
        }
    }

    vkDestroyFence(m_VkDevice, fence, nullptr);
    vkFreeCommandBuffers(m_VkDevice, pool, 1, &cmd);
    return ok;
}

} // namespace RHI
} // namespace Engine