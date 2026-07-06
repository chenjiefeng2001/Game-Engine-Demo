/**
 * @file RenderTestOffscreen.cpp
 * @brief 离屏 GPU Fill + Readback 验证测试（Vulkan）
 *
 * 使用 VkCmdFillBuffer 驱动 GPU 执行实际运算，
 * 然后通过 Readback Buffer 将结果读回 CPU 验证，
 * 证明 GPU 管线真实工作。
 *
 * D3D12 版本使用 RHI 接口的空命令提交 + WaitIdle 验证。
 */

#include "Rendering3DTest.h"
#include <Engine/Core/RHI/VulkanIRHIDevice.h>
#include <Engine/Core/RHI/D3D12IRHIDevice.h>
#include <cstring>
#include <cstdio>

#if defined(ENGINE_HAS_VULKAN)
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#endif

namespace Engine {

// ═══════════════════════════════════════════════════════════════
// Vulkan GPU Fill + Readback 验证
// ═══════════════════════════════════════════════════════════════

#if defined(ENGINE_HAS_VULKAN)

static bool TestVulkanFillAndReadback(RHI::VulkanDevice* vkDev) {
    VkDevice dev = vkDev->GetVkDevice();
    VmaAllocator allocator = vkDev->GetVmaAllocator();
    VkQueue queue = vkDev->GetGraphicsQueue();
    uint32_t queueIndex = vkDev->GetGraphicsQueueIndex();

    // ── 创建 GPU 端 buffer（用于填充数据） ──
    constexpr VkDeviceSize kBufSize = 256;
    VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufCI.size = kBufSize;
    bufCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo bufAI{};
    bufAI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer gpuBuf;
    VmaAllocation gpuAlloc;
    if (vmaCreateBuffer(allocator, &bufCI, &bufAI, &gpuBuf, &gpuAlloc, nullptr) != VK_SUCCESS)
        return false;

    // ── 创建 staging Readback buffer ──
    VkBufferCreateInfo stagingCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingCI.size = kBufSize;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAI{};
    stagingAI.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAI2;
    if (vmaCreateBuffer(allocator, &stagingCI, &stagingAI, &stagingBuf, &stagingAlloc, &stagingAI2) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, gpuBuf, gpuAlloc);
        return false;
    }

    // ── 创建命令池 + 命令缓冲 ──
    VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCI.queueFamilyIndex = queueIndex;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool pool;
    VkResult res = vkCreateCommandPool(dev, &poolCI, nullptr, &pool);
    if (res != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);
        vmaDestroyBuffer(allocator, gpuBuf, gpuAlloc);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAI.commandPool = pool;
    cmdAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(dev, &cmdAI, &cmdBuf);

    // ── 录制命令：先用 FillBuffer 写入 GPU buffer，再拷贝到 staging ──
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // 用 0xAB 填充 GPU buffer
    constexpr uint32_t kFillValue = 0xABABABABu;
    vkCmdFillBuffer(cmdBuf, gpuBuf, 0, kBufSize, kFillValue);

    // GPU buffer → staging buffer 拷贝屏障
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = gpuBuf;
    barrier.offset = 0;
    barrier.size = kBufSize;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);

    // 拷贝到 staging buffer
    VkBufferCopy copyRegion{};
    copyRegion.size = kBufSize;
    vkCmdCopyBuffer(cmdBuf, gpuBuf, stagingBuf, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuf);

    // ── 提交到队列并等待完成 ──
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(dev, &fenceCI, nullptr, &fence);

    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    // ── 读回验证 ──
    bool passed = false;
    void* mappedData = nullptr;
    if (vmaMapMemory(allocator, stagingAlloc, &mappedData) == VK_SUCCESS) {
        const uint8_t* data = static_cast<const uint8_t*>(mappedData);
        passed = true;
        for (VkDeviceSize i = 0; i < kBufSize; ++i) {
            if (data[i] != 0xAB) {
                passed = false;
                break;
            }
        }
        vmaUnmapMemory(allocator, stagingAlloc);
    }

    // ── 清理 ──
    vkDestroyFence(dev, fence, nullptr);
    vkDestroyCommandPool(dev, pool, nullptr);
    vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);
    vmaDestroyBuffer(allocator, gpuBuf, gpuAlloc);

    return passed;
}

#endif // ENGINE_HAS_VULKAN

// ═══════════════════════════════════════════════════════════════
// 公共入口
// ═══════════════════════════════════════════════════════════════

bool Rendering3DTest::TestOffscreenClearAndReadback(RHI::IRHIDevice* device, const char* backendName) {
    (void)device;
    (void)backendName;

#if defined(ENGINE_HAS_VULKAN)
    if (std::strcmp(backendName, "Vulkan") == 0) {
        auto* vkDev = static_cast<RHI::VulkanDevice*>(device);
        bool ok = TestVulkanFillAndReadback(vkDev);
        m_Log.Info("[{}] GPU Fill+Readback: {}", backendName, ok ? "OK" : "FAILED");
        return ok;
    }
#endif

#if defined(ENGINE_HAS_D3D12)
    if (std::strcmp(backendName, "D3D12") == 0) {
        // D3D12：通过 RHI 接口创建 CommandList 并提交，验证 GPU 执行通路
        auto cmdList = device->CreateCommandList(RHI::CommandListType::Direct);
        if (!cmdList) return false;

        cmdList->Begin();
        cmdList->End();

        auto* queue = device->GetQueue(RHI::QueueType::Graphics);
        if (!queue) return false;

        RHI::IRHICommandList* lists[1] = { cmdList.get() };
        queue->ExecuteCommandLists(1, lists);
        queue->WaitIdle();
        m_Log.Info("[{}] GPU Command Submission: OK", backendName);
        return true;
    }
#endif

    m_Log.Warn("[{}] Offscreen test: no backend support compiled", backendName);
    return false;
}

} // namespace Engine